// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "nf/kernel/kernel.hpp"

#include <BRepAlgoAPI_BooleanOperation.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRep_Builder.hxx>
#include <Precision.hxx>
#include <Standard_Version.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_MapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Pln.hxx>

#include <algorithm>
#include <iomanip>
#include <limits>
#include <numbers>
#include <sstream>

#include "nf/kernel/occt_geom.hpp"
#include "nf/math/refine.hpp"
#include "snapshot.hpp"

namespace nf::kernel {
namespace {
bool finite(math::Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
gp_Pnt point(math::Vec3 v) { return {v.x, v.y, v.z}; }
gp_Dir direction(math::Vec3 v) { return {v.x, v.y, v.z}; }
[[noreturn]] void fail(std::string message) { throw KernelError{"kernel", std::move(message)}; }
template <class F>
std::expected<OperationResult, KernelError> guarded(F&& operation) {
  try {
    return operation();
  } catch (const KernelError& e) {
    return std::unexpected(e);
  } catch (const Standard_Failure& e) {
    return std::unexpected(KernelError{"kernel", e.GetMessageString()});
  }
}
detail::SnapshotPtr checked(TopoDS_Shape shape) {
  auto result = detail::snapshot(std::move(shape));
  if (!result) throw result.error();
  return *result;
}
struct EdgeSource {
  TopoDS_Edge edge;
  ProfileEdge from;
};
struct LocalRole {
  std::string role;
  TopoDS_Shape entity;
  std::optional<ProfileEdge> from;
};
OperationResult generated_result(TopoDS_Shape geometry, const std::vector<LocalRole>& roles) {
  auto shape = checked(std::move(geometry));
  OperationResult result{shape, {}, {}, shape->estimated_bytes()};
  TopTools_IndexedMapOfShape faces;
  TopExp::MapShapes(shape->geometry, TopAbs_FACE, faces);
  for (const auto& role : roles) {
    const auto index = faces.FindIndex(role.entity);
    if (!index) fail("generated face is absent from the validated output");
    result.generated.push_back({role.role, detail::entity(shape, faces(index)), role.from});
  }
  result.estimated_bytes +=
      result.generated.size() * (sizeof(Generated) + sizeof(detail::Subshape) + 64);
  return result;
}

class OcctKernel final : public IKernel {
 public:
  explicit OcctKernel(math::Tolerance tolerance) : tolerance_(tolerance) {
    std::ostringstream identity;
    identity.imbue(std::locale::classic());
    identity << "occt/" << OCC_VERSION_COMPLETE
             << ";solid-v1;rigid=located-invariants-v1;boolean=unified-tools-v1;revolve=1;"
                "profile=exact-lines-v1;names=2;mass="
             << detail::kMassRelativeAccuracy
             << std::setprecision(std::numeric_limits<double>::max_digits10)
             << ";linear=" << tolerance.linear << ";angular=" << tolerance.angular;
    identity_ = identity.str();
  }
  std::string_view identity() const noexcept override { return identity_; }
  Capabilities capabilities() const noexcept override { return {true, true, true, true}; }

  std::expected<OperationResult, KernelError> extrude(const Extrusion& input) override {
    return guarded([&] {
      const auto& p = input.plane;
      validate_plane(p);
      if (input.regions.empty() || !finite(input.displacement) ||
          !(math::abs(math::dot(input.displacement, p.normal)) > tolerance_.linear))
        fail("extrusion needs regions and a finite displacement out of their plane");
      BRep_Builder builder;
      TopoDS_Compound compound;
      builder.MakeCompound(compound);
      std::vector<LocalRole> roles;
      for (std::size_t r = 0; r < input.regions.size(); ++r) {
        std::vector<EdgeSource> edges;
        auto face = profile_face(input.regions[r], p, r, edges);
        BRepPrimAPI_MakePrism prism(
            face, gp_Vec(input.displacement.x, input.displacement.y, input.displacement.z), true);
        if (!prism.IsDone()) fail("extrusion failed");
        builder.Add(compound, prism.Shape());
        roles.push_back({"cap_start", prism.FirstShape(), {}});
        roles.push_back({"cap_end", prism.LastShape(), {}});
        for (const auto& source : edges)
          for (const auto& shape : prism.Generated(source.edge))
            if (shape.ShapeType() == TopAbs_FACE) roles.push_back({"side", shape, source.from});
      }
      return generated_result(compound, roles);
    });
  }

  std::expected<OperationResult, KernelError> revolve(const Revolution& input) override {
    return guarded([&] {
      const auto& p = input.plane;
      validate_plane(p);
      auto axis = math::normalized(input.axis_direction, tolerance_);
      const auto angle = std::abs(input.angle);
      if (!axis || !finite(input.axis_origin) ||
          !math::near(math::dot(*axis, p.normal), 0, tolerance_.angular) ||
          !math::near(math::dot(input.axis_origin - p.origin, p.normal), 0, tolerance_.linear))
        fail("revolution needs a finite nonzero axis in the sketch plane");
      if (input.regions.empty() || !std::isfinite(angle) || angle <= tolerance_.angular ||
          angle > 2 * std::numbers::pi)
        fail("revolution needs regions and an angle with 0 < magnitude <= 2 pi");
      const auto radial = math::cross(*axis, p.normal), y = math::cross(p.normal, p.x_axis);
      double low = 0, high = 0;
      for (const auto& region : input.regions)
        for (const auto& curve : region.outer.curves)
          for (const auto v : curve.points()) {
            const auto r =
                math::dot(p.origin + p.x_axis * v.x + y * v.y - input.axis_origin, radial);
            if (!std::isfinite(r)) fail("revolution profile exceeds finite coordinates");
            low = std::min(low, r);
            high = std::max(high, r);
          }
      if (low < -tolerance_.linear && high > tolerance_.linear)
        fail("revolution profile control hulls must stay on one side of the axis");
      if (input.angle < 0) *axis = *axis * -1;
      BRep_Builder builder;
      TopoDS_Compound compound;
      builder.MakeCompound(compound);
      std::vector<LocalRole> roles;
      for (std::size_t r = 0; r < input.regions.size(); ++r) {
        std::vector<EdgeSource> edges;
        auto face = profile_face(input.regions[r], p, r, edges);
        BRepPrimAPI_MakeRevol revol(face, gp_Ax1(point(input.axis_origin), direction(*axis)), angle,
                                    true);
        if (!revol.IsDone() || revol.Shape().IsNull()) fail("revolution failed");
        builder.Add(compound, revol.Shape());
        TopTools_IndexedMapOfShape faces;
        TopExp::MapShapes(revol.Shape(), TopAbs_FACE, faces);
        // A complete turn has no cap faces; OCCT may also close turns within its precision floor.
        for (const auto& cap : {LocalRole{"cap_start", revol.FirstShape(), {}},
                                LocalRole{"cap_end", revol.LastShape(), {}}})
          if (!cap.entity.IsNull() && faces.Contains(cap.entity)) roles.push_back(cap);
        for (const auto& source : edges)
          for (const auto& shape : revol.Generated(source.edge))
            if (shape.ShapeType() == TopAbs_FACE) roles.push_back({"side", shape, source.from});
      }
      return generated_result(compound, roles);
    });
  }

  std::expected<OperationResult, KernelError> transform(const ShapePtr& input,
                                                        const RigidTransform& t) override {
    return guarded([&] {
      const auto source = std::dynamic_pointer_cast<const detail::Snapshot>(input);
      if (!source) fail("shape belongs to an incompatible backend");
      if (!finite(t.translation) || !finite(t.axis_origin) || !std::isfinite(t.angle))
        fail("transform must be finite");
      const auto axis = math::normalized(t.axis_direction, tolerance_);
      if (!axis) fail("rotation needs a nonzero finite axis");
      gp_Trsf transform;
      if (t.angle != 0)
        transform.SetRotation(gp_Ax1(point(t.axis_origin), direction(*axis)), t.angle);
      gp_Trsf translation;
      translation.SetTranslation(gp_Vec(t.translation.x, t.translation.y, t.translation.z));
      transform.PreMultiply(translation);
      // A direct rigid isometry changes only a TopLoc_Location. OCCT shares immutable
      // geometry; no builder edits source topology, tolerances or underlying curves.
      BRepBuilderAPI_Transform operation(source->geometry, transform, false);
      if (!operation.IsDone()) fail("rigid transform failed");
      auto validated = detail::rigid_snapshot(operation.Shape(), *source, transform);
      if (!validated) throw validated.error();
      auto shape = *validated;
      OperationResult result{shape, {}, {}, shape->estimated_bytes() + source->estimated_bytes()};
      for (const auto& before : detail::entities(source)) {
        const auto& entity = static_cast<const detail::Subshape&>(*before);
        const auto after = operation.ModifiedShape(entity.geometry);
        if (after.IsNull()) fail("transform did not retain complete entity history");
        result.history.push_back({before, {detail::entity(shape, after)}, {}, false});
      }
      result.estimated_bytes +=
          result.history.size() * (sizeof(History) + 2 * sizeof(detail::Subshape) + 64);
      return result;
    });
  }

  std::expected<OperationResult, KernelError> boolean_operation(const ShapePtr& target,
                                                                const std::vector<ShapePtr>& tools,
                                                                BooleanOp op) override {
    return guarded([&] {
      if (tools.empty()) fail("boolean needs at least one tool solid");
      BRepAlgoAPI_BooleanOperation operation;
      switch (op) {
        case BooleanOp::kUnion:
          operation.SetOperation(BOPAlgo_FUSE);
          break;
        case BooleanOp::kSubtract:
          operation.SetOperation(BOPAlgo_CUT);
          break;
        case BooleanOp::kIntersect:
          operation.SetOperation(BOPAlgo_COMMON);
          break;
        default:
          fail("unknown boolean operation");
      }
      std::vector<detail::SnapshotPtr> sources;
      auto operand = [&](const ShapePtr& input) {
        const auto source = std::dynamic_pointer_cast<const detail::Snapshot>(input);
        if (!source) fail("boolean shape belongs to an incompatible backend");
        if (std::ranges::find(sources, source) == sources.end()) sources.push_back(source);
        return source->geometry;
      };
      TopTools_ListOfShape arguments, operands;
      arguments.Append(operand(target));
      for (const auto& tool : tools) operands.Append(operand(tool));
      operation.SetArguments(arguments);
      operation.SetTools(operands);
      operation.SetNonDestructive(true);
      operation.SetRunParallel(false);
      operation.SetToFillHistory(true);
      operation.SetFuzzyValue(tolerance_.linear);
      operation.Build();
      if (!operation.IsDone() || operation.HasErrors()) {
        std::ostringstream errors;
        operation.DumpErrors(errors);
        fail("solid boolean failed: " + errors.str());
      }
      // Safe-input unification merges its own history into the boolean's history.
      operation.SimplifyResult(true, true, tolerance_.angular);
      auto shape = checked(operation.Shape());
      OperationResult result{shape, {}, {}, shape->estimated_bytes()};
      TopTools_IndexedMapOfShape members;
      TopExp::MapShapes(shape->geometry, members);
      auto member = [&](const TopoDS_Shape& geometry) {
        const auto index = members.FindIndex(geometry);
        if (!index) fail("boolean history refers outside its output");
        // Use the output orientation, particularly for reversed cutting-tool faces.
        return detail::entity(shape, members(index));
      };
      TopTools_MapOfShape generated;
      for (const auto& source : sources) {
        result.estimated_bytes += source->estimated_bytes();
        for (const auto& before : detail::entities(source)) {
          const auto& geometry = static_cast<const detail::Subshape&>(*before).geometry;
          History h{before, {}, {}, operation.IsDeleted(geometry)};
          for (const auto& after : operation.Modified(geometry))
            h.modified.push_back(member(after));
          if (!h.deleted && h.modified.empty()) h.modified.push_back(member(geometry));
          for (const auto& after : operation.Generated(geometry)) {
            if (after.ShapeType() != TopAbs_EDGE && after.ShapeType() != TopAbs_FACE) continue;
            h.generated.push_back(member(after));
            if (generated.Add(after))
              result.generated.push_back({"intersection", member(after), {}});
          }
          result.estimated_bytes +=
              sizeof(History) + sizeof(detail::Subshape) + 64 +
              (h.modified.size() + h.generated.size()) * (sizeof(detail::Subshape) + 64);
          result.history.push_back(std::move(h));
        }
      }
      result.estimated_bytes +=
          result.generated.size() * (sizeof(Generated) + sizeof(detail::Subshape) + 64);
      return result;
    });
  }

 private:
  void validate_plane(const Plane& p) const {
    if (!finite(p.origin) || !finite(p.normal) || !finite(p.x_axis) ||
        !math::near(math::length(p.normal), 1, tolerance_.angular) ||
        !math::near(math::length(p.x_axis), 1, tolerance_.angular) ||
        !math::near(math::dot(p.normal, p.x_axis), 0, tolerance_.angular))
      fail("sketch plane must have finite orthonormal axes");
  }
  TopoDS_Face profile_face(const math::Region& region, const Plane& p, std::size_t r,
                           std::vector<EdgeSource>& edges) const {
    auto outer = wire(region.outer, p, r, 0, edges);
    BRepBuilderAPI_MakeFace face(
        gp_Pln(gp_Ax3(point(p.origin), direction(p.normal), direction(p.x_axis))), outer, true);
    for (std::size_t h = 0; h < region.holes.size(); ++h)
      face.Add(wire(region.holes[h], p, r, h + 1, edges));
    if (!face.IsDone()) fail("cannot build planar region face");
    return face.Face();
  }
  TopoDS_Wire wire(const math::Loop& loop, const Plane& p, std::size_t region, std::size_t index,
                   std::vector<EdgeSource>& edges) const {
    if (loop.curves.empty()) fail("region boundary is empty");
    BRepBuilderAPI_MakeWire builder;
    const auto y = math::cross(p.normal, p.x_axis);
    for (std::size_t c = 0; c < loop.curves.size(); ++c) {
      const auto& curve = loop.curves[c];
      const auto& next = loop.curves[(c + 1) % loop.curves.size()];
      if (!math::near(curve.point(curve.domain_end()), next.point(next.domain_start()), tolerance_))
        fail("region boundary is not closed");
      // Exact knot-span decomposition also prevents OCCT's surface-area quadrature from
      // integrating across unrecognized knots in a surface of linear extrusion.
      for (const auto& span : math::decompose(curve)) {
        std::vector<math::Vec3> points;
        for (auto v : span.points()) points.push_back(p.origin + p.x_axis * v.x + y * v.y);
        auto edge = [&] {
          // Exact linear spans give OCCT analytic planes/cylinders/cones after revolution.
          // Only the edge parameterization changes; profile-curve provenance is unchanged.
          if (span.degree() == 1 && !span.rational())
            return BRepBuilderAPI_MakeEdge(point(points.front()), point(points.back()));
          auto lifted = math::NurbsCurve3::make(span.knots(), std::move(points),
                                                {span.weights().begin(), span.weights().end()});
          if (!lifted) fail(lifted.error());
          auto converted = to_occt(*lifted);
          if (!converted) fail(converted.error());
          return BRepBuilderAPI_MakeEdge(*converted, span.domain_start(), span.domain_end());
        }();
        if (!edge.IsDone()) fail("cannot build profile edge");
        builder.Add(edge.Edge());
        if (!builder.IsDone()) fail("cannot join profile edges");
        // Joining replaces vertices/edges; history must address the edge actually in the wire.
        edges.push_back({builder.Edge(), {region, index, c}});
      }
    }
    return builder.Wire();
  }
  math::Tolerance tolerance_;
  std::string identity_;
};
}  // namespace

std::expected<std::unique_ptr<IKernel>, KernelError> make_occt_kernel(math::Tolerance tolerance) {
  if (!std::isfinite(tolerance.linear) || !std::isfinite(tolerance.angular) ||
      tolerance.linear < Precision::Confusion() || tolerance.angular < Precision::Angular() ||
      tolerance.angular >= 1)
    return std::unexpected(KernelError{"kernel", "invalid backend tolerances"});
  return std::make_unique<OcctKernel>(tolerance);
}
}  // namespace nf::kernel
