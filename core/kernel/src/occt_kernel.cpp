// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "nf/kernel/kernel.hpp"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Builder.hxx>
#include <Precision.hxx>
#include <Standard_Version.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Pln.hxx>

#include <iomanip>
#include <limits>
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

class OcctKernel final : public IKernel {
 public:
  explicit OcctKernel(math::Tolerance tolerance) : tolerance_(tolerance) {
    std::ostringstream identity;
    identity.imbue(std::locale::classic());
    identity << "occt/" << OCC_VERSION_COMPLETE
             << ";solid-v1;rigid=located-invariants-v1;names=1;mass="
             << detail::kMassRelativeAccuracy
             << std::setprecision(std::numeric_limits<double>::max_digits10)
             << ";linear=" << tolerance.linear << ";angular=" << tolerance.angular;
    identity_ = identity.str();
  }
  std::string_view identity() const noexcept override { return identity_; }
  Capabilities capabilities() const noexcept override { return {true, true}; }

  std::expected<OperationResult, KernelError> extrude(const Extrusion& input) override {
    return guarded([&] {
      const auto& p = input.plane;
      if (!finite(p.origin) || !finite(p.normal) || !finite(p.x_axis) ||
          !math::near(math::length(p.normal), 1, tolerance_.angular) ||
          !math::near(math::length(p.x_axis), 1, tolerance_.angular) ||
          !math::near(math::dot(p.normal, p.x_axis), 0, tolerance_.angular))
        fail("sketch plane must have finite orthonormal axes");
      if (input.regions.empty() || !finite(input.displacement) ||
          !(math::abs(math::dot(input.displacement, p.normal)) > tolerance_.linear))
        fail("extrusion needs regions and a finite displacement out of their plane");
      BRep_Builder builder;
      TopoDS_Compound compound;
      builder.MakeCompound(compound);
      std::vector<LocalRole> roles;
      for (std::size_t r = 0; r < input.regions.size(); ++r) {
        const auto& region = input.regions[r];
        std::vector<EdgeSource> edges;
        auto outer = wire(region.outer, p, r, 0, edges);
        BRepBuilderAPI_MakeFace face(
            gp_Pln(gp_Ax3(point(p.origin), direction(p.normal), direction(p.x_axis))), outer, true);
        for (std::size_t h = 0; h < region.holes.size(); ++h)
          face.Add(wire(region.holes[h], p, r, h + 1, edges));
        if (!face.IsDone()) fail("cannot build planar region face");
        BRepPrimAPI_MakePrism prism(
            face.Face(), gp_Vec(input.displacement.x, input.displacement.y, input.displacement.z),
            true);
        if (!prism.IsDone()) fail("extrusion failed");
        builder.Add(compound, prism.Shape());
        roles.push_back({"cap_start", prism.FirstShape(), {}});
        roles.push_back({"cap_end", prism.LastShape(), {}});
        for (const auto& source : edges)
          for (const auto& shape : prism.Generated(source.edge))
            if (shape.ShapeType() == TopAbs_FACE) roles.push_back({"side", shape, source.from});
      }
      auto shape = checked(compound);
      OperationResult result{shape, {}, {}, shape->estimated_bytes()};
      for (const auto& role : roles)
        result.generated.push_back({role.role, detail::entity(shape, role.entity), role.from});
      result.estimated_bytes +=
          result.generated.size() * (sizeof(Generated) + sizeof(detail::Subshape) + 64);
      return result;
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

 private:
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
        auto lifted = math::NurbsCurve3::make(span.knots(), std::move(points),
                                              {span.weights().begin(), span.weights().end()});
        if (!lifted) fail(lifted.error());
        auto converted = to_occt(*lifted);
        if (!converted) fail(converted.error());
        BRepBuilderAPI_MakeEdge edge(*converted, span.domain_start(), span.domain_end());
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
