// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "nf/kernel/kernel.hpp"

#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_BooleanOperation.hxx>
#include <BRepAlgoAPI_Check.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepLib_ToolTriangulatedShape.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <DESTEP_Parameters.hxx>
#include <Message_Messenger.hxx>
#include <Poly_Triangulation.hxx>
#include <Precision.hxx>
#include <STEPControl_Writer.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <Standard_Version.hxx>
#include <StepData_StepModel.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_MapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <XSControl_WorkSession.hxx>
#include <gp_Circ.hxx>
#include <gp_Cylinder.hxx>
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
auto guarded(F&& operation) -> std::expected<decltype(operation()), KernelError> {
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
TopoDS_Face checked_face(const EntityPtr& input) {
  const auto entity = std::dynamic_pointer_cast<const detail::Subshape>(input);
  if (!entity || !entity->snapshot || entity->snapshot->geometry.IsNull() ||
      entity->geometry.IsNull() || entity->geometry.ShapeType() != TopAbs_FACE)
    fail("face query requires a face from this backend");
  TopTools_IndexedMapOfShape faces;
  TopExp::MapShapes(entity->snapshot->geometry, TopAbs_FACE, faces);
  const auto index = faces.FindIndex(entity->geometry);
  if (!index) fail("face is absent from its owning snapshot");
  return TopoDS::Face(faces(index));
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

struct CircularArc {
  gp_Circ circle;
  double angle;
};
std::optional<CircularArc> circular_arc(const math::NurbsCurve2& curve, const Plane& plane) {
  if (curve.degree() != 2 || curve.points().size() != 3 || !curve.rational()) return {};
  const double rounding = 64 * std::numeric_limits<double>::epsilon();
  // Plane validation allows modeling tolerance. Arc conversion needs an isometry to
  // roundoff so it cannot erase a small authored scale or shear in an accepted plane.
  if (std::abs(math::dot(plane.x_axis, plane.x_axis) - 1) > rounding ||
      std::abs(math::dot(plane.normal, plane.normal) - 1) > rounding ||
      std::abs(math::dot(plane.x_axis, plane.normal)) > rounding)
    return {};
  // Work relative to the first pole and at unit scale. Intersect endpoint normals to
  // obtain a candidate center; nearly parallel tangents stay in the NURBS representation.
  const auto start = curve.points()[0], delta = curve.points()[1] - start,
             chord = curve.points()[2] - start;
  const auto scale = std::max(math::length(delta), math::length(chord));
  if (!(scale > 0) || !std::isfinite(scale)) return {};
  const auto u = delta / scale, end = chord / scale, v = end - u;
  const double turn = math::cross(u, v);
  if (std::abs(turn) <= 1e-6) return {};
  const auto center = math::Vec2{-u.y, u.x} * (math::dot(end, v) / turn);
  const auto a = center * -1, b = u - center, c = end - center;
  const double radius2 = math::dot(a, a), pole2 = math::dot(b, b), end2 = math::dot(c, c);
  const double epsilon = rounding * std::max({radius2, pole2, end2});
  // All five degree-4 Bernstein coefficients of |N - center*W|² - radius²*W²
  // must vanish. The first is zero by construction; the middle includes the weights.
  // This checks the entire rational quadratic, not sampled points or modeling tolerance.
  if (std::abs(math::dot(a, b) - radius2) > epsilon ||
      std::abs(math::dot(b, c) - radius2) > epsilon || std::abs(end2 - radius2) > epsilon)
    return {};
  const auto weights = curve.weights();
  const double first = weights[0] * weights[2] * (math::dot(a, c) - radius2),
               second = 2 * weights[1] * weights[1] * (pole2 - radius2);
  if (!(first < 0 && second > 0) ||
      std::abs(first + second) > rounding * (std::abs(first) + second))
    return {};
  const double sign = turn > 0 ? 1 : -1;
  const double angle = std::atan2(sign * math::cross(a, c), math::dot(a, c));
  const double radius = std::sqrt(radius2) * scale;
  if (!(angle > 0 && angle < std::numbers::pi && radius > Precision::Confusion()) ||
      !std::isfinite(radius))
    return {};
  const auto y = math::cross(plane.normal, plane.x_axis);
  const auto local_center = start + center * scale;
  const auto origin = plane.origin + plane.x_axis * local_center.x + y * local_center.y;
  if (!finite(origin)) return {};
  return CircularArc{gp_Circ(gp_Ax2(point(origin), direction(plane.normal * sign),
                                    direction(plane.x_axis * a.x + y * a.y)),
                             radius),
                     angle};
}

class OcctKernel final : public IKernel {
 public:
  explicit OcctKernel(math::Tolerance tolerance) : tolerance_(tolerance) {
    std::ostringstream identity;
    identity.imbue(std::locale::classic());
    identity << "occt/" << OCC_VERSION_COMPLETE << ";build=" << NF_OCCT_BUILD_ID
             << ";solid-v1;rigid=located-invariants-v1;mirror=oriented-copy-v1;"
                "boolean=separated-copy-unified-v2;revolve=1;loft=parallel-cubic-history-v1;"
                "sweep=planar-binormal-monotone-v1;"
                "face-plane=uv-outward-v1;faces=analytic-v2;"
                "cylinder-axis=parameter-z-v1;edges=face-loops-v1;chamfer=copy-equal-v1;"
                "fillet=copy-contour-vertex-boundaries-v1;"
                "mesh=indexed-faces-copy-v1;"
                "face-distance=trimmed-v1;step=ap242-mm-v1;"
                "profile=exact-lines-arcs-v3;names=2;mass-reference=shell-corner-v1;"
                "mass-integrator=planar-gauss-v1;area-integrator=analytic-uv-rectangle-gk-v2;mass="
             << detail::kMassRelativeAccuracy
             << std::setprecision(std::numeric_limits<double>::max_digits10)
             << ";linear=" << tolerance.linear << ";angular=" << tolerance.angular;
    identity_ = identity.str();
  }
  std::string_view identity() const noexcept override { return identity_; }
  Capabilities capabilities() const noexcept override {
    return {true, true, true, true, true, true, true, true, true};
  }
  std::expected<std::string, KernelError> export_step(
      const std::vector<ShapePtr>& inputs) override {
    return guarded([&] {
      if (inputs.empty()) fail("STEP export requires at least one solid body");
      BRep_Builder builder;
      TopoDS_Compound compound;
      builder.MakeCompound(compound);
      for (const auto& input : inputs) {
        const auto source = std::dynamic_pointer_cast<const detail::Snapshot>(input);
        if (!source || source->geometry.IsNull())
          fail("STEP export requires snapshots from this backend");
        BRepBuilderAPI_Copy copy(source->geometry, true, false);
        if (!copy.IsDone()) fail("cannot copy STEP input geometry");
        builder.Add(compound, copy.Shape());
      }
      STEPControl_Writer writer;
      Handle(Message_Messenger) messages = new Message_Messenger;
      messages->ChangePrinters().Clear();
      writer.WS()->TransferWriter()->FinderProcess()->SetMessenger(messages);
      writer.Model()->SetLocalLengthUnit(1);
      DESTEP_Parameters options;
      options.WriteSchema = DESTEP_Parameters::WriteMode_StepSchema_AP242DIS;
      options.WriteUnit = UnitsMethods_LengthUnit_Millimeter;
      options.WriteAssembly = DESTEP_Parameters::WriteMode_Assembly_Off;
      options.WriteTessellated = DESTEP_Parameters::RWMode_Tessellated_Off;
      options.WritePrecisionMode = DESTEP_Parameters::WriteMode_PrecisionMode_Session;
      options.WritePrecisionVal = tolerance_.linear;
      if (writer.Transfer(compound, STEPControl_ManifoldSolidBrep, options) != IFSelect_RetDone)
        fail("STEP solid translation failed");
      std::ostringstream stream;
      stream.imbue(std::locale::classic());
      if (writer.WriteStream(stream) != IFSelect_RetDone || !stream.good())
        fail("STEP text encoding failed");
      return stream.str();
    });
  }
  std::expected<Mesh, KernelError> tessellate(const ShapePtr& input,
                                              const MeshingOptions& options) override {
    return guarded([&] {
      const auto source = std::dynamic_pointer_cast<const detail::Snapshot>(input);
      if (!source || !std::isfinite(options.linear_deflection) ||
          !std::isfinite(options.angular_deflection) ||
          options.linear_deflection < tolerance_.linear ||
          options.angular_deflection < tolerance_.angular ||
          options.angular_deflection > std::numbers::pi || !options.max_triangles ||
          options.max_triangles > 1'000'000)
        fail("invalid mesh source or meshing options");
      BRepBuilderAPI_Copy copy(source->geometry, true, false);
      if (!copy.IsDone()) fail("mesh input copy failed");
      IMeshTools_Parameters parameters;
      parameters.Deflection = parameters.DeflectionInterior = options.linear_deflection;
      parameters.Angle = parameters.AngleInterior = options.angular_deflection;
      parameters.EnableControlSurfaceDeflectionAllSurfaces = true;
      BRepMesh_IncrementalMesh mesher(copy.Shape(), parameters);
      if (!mesher.IsDone()) fail("solid tessellation failed");
      Mesh result;
      TopTools_IndexedMapOfShape originals, copied;
      TopExp::MapShapes(source->geometry, TopAbs_FACE, originals);
      TopExp::MapShapes(copy.Shape(), TopAbs_FACE, copied);
      for (int f = 1; f <= originals.Extent(); ++f) {
        const auto index = copied.FindIndex(copy.ModifiedShape(originals(f)));
        if (!index) fail("meshed face is absent from the copied solid");
        // Copy history may omit orientation; read it from the actual solid boundary.
        const auto face = TopoDS::Face(copied(index));
        TopLoc_Location location;
        const auto triangulation = BRep_Tool::Triangulation(face, location);
        if (triangulation.IsNull() || triangulation->NbTriangles() <= 0 ||
            triangulation->NbNodes() <= 0)
          fail("source face has no complete triangulation");
        const auto nodes = static_cast<std::size_t>(triangulation->NbNodes());
        const auto triangles = static_cast<std::size_t>(triangulation->NbTriangles());
        if (result.indices.size() / 3 + triangles > options.max_triangles ||
            result.vertices.size() + nodes > 3 * options.max_triangles)
          fail("mesh output exceeds its triangle or vertex budget");
        BRepLib_ToolTriangulatedShape::ComputeNormals(face, triangulation);
        if (!triangulation->HasNormals()) fail("mesh face has no surface normals");
        const bool reversed = face.Orientation() == TopAbs_REVERSED;
        const auto base = static_cast<std::uint32_t>(result.vertices.size());
        const auto first = static_cast<std::uint32_t>(result.indices.size());
        for (int i = 1; i <= triangulation->NbNodes(); ++i) {
          const auto p = triangulation->Node(i).Transformed(location.Transformation());
          auto n = triangulation->Normal(i).Transformed(location.Transformation());
          if (reversed) n.Reverse();
          const MeshVertex vertex{{p.X(), p.Y(), p.Z()}, {n.X(), n.Y(), n.Z()}};
          if (!finite(vertex.position) || !finite(vertex.normal))
            fail("mesh has non-finite vertex data");
          result.vertices.push_back(vertex);
        }
        for (int i = 1; i <= triangulation->NbTriangles(); ++i) {
          int a, b, c;
          triangulation->Triangle(i).Get(a, b, c);
          if (a < 1 || b < 1 || c < 1 || a > triangulation->NbNodes() ||
              b > triangulation->NbNodes() || c > triangulation->NbNodes())
            fail("mesh triangle has an invalid node index");
          if (reversed) std::swap(b, c);
          const auto ia = base + static_cast<std::uint32_t>(a - 1);
          const auto ib = base + static_cast<std::uint32_t>(b - 1);
          const auto ic = base + static_cast<std::uint32_t>(c - 1);
          const auto& va = result.vertices[ia];
          const auto& vb = result.vertices[ib];
          const auto& vc = result.vertices[ic];
          // Pole nodes can have distinct indices but identical positions. Test before
          // cross products: fused arithmetic can leave a nonzero cancellation residual.
          if (va.position == vb.position || va.position == vc.position ||
              vb.position == vc.position)
            continue;
          const auto normal = math::cross(vb.position - va.position, vc.position - va.position);
          const auto area = math::length(normal);
          if (!std::isfinite(area)) fail("mesh triangle area is not finite");
          if (area > 0 && !(math::dot(normal, va.normal + vb.normal + vc.normal) > 0))
            fail("mesh normals disagree with triangle winding");
          if (area > 0) result.indices.insert(result.indices.end(), {ia, ib, ic});
        }
        const auto count = static_cast<std::uint32_t>(result.indices.size()) - first;
        if (!count) fail("mesh face has only degenerate triangles");
        result.faces.push_back({detail::entity(source, originals(f)), first, count});
      }
      if (result.faces.empty()) fail("solid tessellation has no faces");
      return result;
    });
  }
  std::expected<std::vector<EntityPtr>, KernelError> edges(const EntityPtr& input) override {
    return guarded([&] {
      const auto face = checked_face(input);
      const auto source = std::dynamic_pointer_cast<const detail::Snapshot>(input->owner());
      TopTools_IndexedMapOfShape members;
      TopExp::MapShapes(face, TopAbs_EDGE, members);
      std::vector<EntityPtr> result;
      for (int i = 1; i <= members.Extent(); ++i)
        result.push_back(detail::entity(source, members(i)));
      return result;
    });
  }
  std::expected<OperationResult, KernelError> chamfer(const ShapePtr& input,
                                                      const std::vector<EntityPtr>& edges,
                                                      double distance) override {
    return guarded([&] {
      const auto source = std::dynamic_pointer_cast<const detail::Snapshot>(input);
      if (!source || edges.empty() || !std::isfinite(distance) || distance <= tolerance_.linear)
        fail("chamfer requires a solid, edges and distance above linear tolerance");
      BRepBuilderAPI_Copy copy(source->geometry, true, false);
      if (!copy.IsDone()) fail("chamfer input copy failed");
      BRepFilletAPI_MakeChamfer operation(copy.Shape());
      TopTools_IndexedMapOfShape originals;
      TopExp::MapShapes(source->geometry, TopAbs_EDGE, originals);
      TopTools_MapOfShape selected;
      for (const auto& edge : edges) {
        const auto entity = std::dynamic_pointer_cast<const detail::Subshape>(edge);
        if (!entity || entity->owner() != input || entity->kind() != EntityKind::kEdge ||
            !originals.Contains(entity->geometry))
          fail("chamfer edge is absent from its target snapshot");
        const auto e = TopoDS::Edge(copy.ModifiedShape(entity->geometry));
        if (selected.Add(e)) operation.Add(distance, e);
        if (!operation.Contour(e)) fail("chamfer cannot use the selected edge");
      }
      operation.Build();
      if (!operation.IsDone()) fail("solid chamfer failed");
      auto shape = checked(operation.Shape());
      OperationResult result{shape, {}, {}, source->estimated_bytes() + shape->estimated_bytes()};
      TopTools_IndexedMapOfShape members;
      TopExp::MapShapes(shape->geometry, members);
      auto member = [&](const TopoDS_Shape& geometry) {
        const auto index = members.FindIndex(geometry);
        if (!index) fail("incomplete chamfer history");
        return detail::entity(shape, members(index));
      };
      TopTools_MapOfShape generated;
      TopTools_MapOfShape inherited_faces;
      for (const auto& before : detail::entities(source)) {
        const auto geometry =
            copy.ModifiedShape(static_cast<const detail::Subshape&>(*before).geometry);
        History h{before, {}, {}, operation.IsDeleted(geometry)};
        for (const auto& after : operation.Modified(geometry)) h.modified.push_back(member(after));
        if (!h.deleted && h.modified.empty()) h.modified.push_back(member(geometry));
        if (before->kind() == EntityKind::kFace)
          for (const auto& after : h.modified)
            inherited_faces.Add(static_cast<const detail::Subshape&>(*after).geometry);
        for (const auto& after : operation.Generated(geometry)) {
          if (after.ShapeType() != TopAbs_FACE && after.ShapeType() != TopAbs_EDGE) continue;
          h.generated.push_back(member(after));
          if (after.ShapeType() == TopAbs_FACE && generated.Add(after))
            result.generated.push_back({"chamfer", member(after), {}});
        }
        result.estimated_bytes +=
            sizeof(History) + sizeof(detail::Subshape) + 64 +
            (h.modified.size() + h.generated.size()) * (sizeof(detail::Subshape) + 64);
        result.history.push_back(std::move(h));
      }
      for (int i = 1; i <= members.Extent(); ++i)
        if (members(i).ShapeType() == TopAbs_FACE && !inherited_faces.Contains(members(i)) &&
            !generated.Contains(members(i)))
          fail("chamfer output face has no generated provenance");
      if (result.generated.empty()) fail("chamfer has no generated-face provenance");
      result.estimated_bytes +=
          result.generated.size() * (sizeof(Generated) + sizeof(detail::Subshape) + 64);
      return result;
    });
  }
  std::expected<OperationResult, KernelError> fillet(const ShapePtr& input,
                                                     const std::vector<EntityPtr>& edges,
                                                     double radius) override {
    return guarded([&] {
      const auto source = std::dynamic_pointer_cast<const detail::Snapshot>(input);
      if (!source || edges.empty() || !std::isfinite(radius) || radius <= tolerance_.linear)
        fail("fillet requires a solid, edges and radius above linear tolerance");
      BRepBuilderAPI_Copy copy(source->geometry, true, false);
      if (!copy.IsDone()) fail("fillet input copy failed");
      BRepFilletAPI_MakeFillet operation(copy.Shape());
      TopTools_IndexedMapOfShape originals;
      TopExp::MapShapes(source->geometry, TopAbs_EDGE, originals);
      TopTools_MapOfShape selected;
      for (const auto& edge : edges) {
        const auto entity = std::dynamic_pointer_cast<const detail::Subshape>(edge);
        if (!entity || entity->owner() != input || entity->kind() != EntityKind::kEdge ||
            !originals.Contains(entity->geometry))
          fail("fillet edge is absent from its target snapshot");
        const auto e = TopoDS::Edge(copy.ModifiedShape(entity->geometry));
        if (selected.Add(e)) operation.Add(radius, e);
        if (!operation.Contour(e)) fail("fillet cannot use the selected edge");
      }
      operation.Build();
      if (!operation.IsDone()) fail("solid fillet failed");
      auto shape = checked(operation.Shape());
      OperationResult result{shape, {}, {}, source->estimated_bytes() + shape->estimated_bytes()};
      TopTools_IndexedMapOfShape members;
      TopExp::MapShapes(shape->geometry, members);
      auto member = [&](const TopoDS_Shape& geometry) {
        const auto index = members.FindIndex(geometry);
        if (!index) fail("incomplete fillet history");
        return detail::entity(shape, members(index));
      };
      TopTools_MapOfShape generated, inherited, covered;
      for (const auto& before : detail::entities(source)) {
        const auto geometry =
            copy.ModifiedShape(static_cast<const detail::Subshape&>(*before).geometry);
        History h{before, {}, {}, false};
        for (const auto& after : operation.Modified(geometry)) h.modified.push_back(member(after));
        if (h.modified.empty() && members.Contains(geometry))
          h.modified.push_back(member(geometry));
        h.deleted = h.modified.empty();
        if (h.deleted &&
            (before->kind() == EntityKind::kEdge ? !operation.Contour(TopoDS::Edge(geometry))
                                                 : !operation.IsDeleted(geometry)))
          fail("fillet input has no surviving or deleted provenance");
        for (const auto& after : h.modified)
          inherited.Add(static_cast<const detail::Subshape&>(*after).geometry);
        TopTools_MapOfShape local_generated;
        auto append_faces = [&](const TopoDS_Shape& ancestor) {
          for (const auto& after : operation.Generated(ancestor)) {
            if (after.ShapeType() != TopAbs_FACE) fail("unexpected fillet generated shape");
            if (local_generated.Add(after)) h.generated.push_back(member(after));
            if (generated.Add(after)) result.generated.push_back({"fillet", member(after), {}});
          }
        };
        append_faces(geometry);
        // OCCT reports corner patches from vertices, not the edges meeting there. Bind each
        // patch to every incident contour edge using exact input topology, never a mesh match.
        if (before->kind() == EntityKind::kEdge && operation.Contour(TopoDS::Edge(geometry))) {
          TopTools_IndexedMapOfShape vertices;
          TopExp::MapShapes(geometry, TopAbs_VERTEX, vertices);
          for (int i = 1; i <= vertices.Extent(); ++i) append_faces(vertices(i));
        }
        result.history.push_back(std::move(h));
      }
      for (auto& h : result.history) {
        const auto faces = h.generated;
        TopTools_MapOfShape local_edges;
        // New rails and junction edges descend from their generated blend faces. Existing
        // edges keep OCCT's explicit modified/surviving history. Uncovered output is an error.
        for (const auto& face : faces) {
          TopTools_IndexedMapOfShape boundary;
          TopExp::MapShapes(static_cast<const detail::Subshape&>(*face).geometry, TopAbs_EDGE,
                            boundary);
          for (int i = 1; i <= boundary.Extent(); ++i)
            if (!inherited.Contains(boundary(i)) && local_edges.Add(boundary(i)))
              h.generated.push_back(member(boundary(i)));
        }
        for (const auto& list : {h.modified, h.generated})
          for (const auto& after : list)
            covered.Add(static_cast<const detail::Subshape&>(*after).geometry);
        result.estimated_bytes +=
            sizeof(History) + sizeof(detail::Subshape) + 64 +
            (h.modified.size() + h.generated.size()) * (sizeof(detail::Subshape) + 64);
      }
      for (int i = 1; i <= members.Extent(); ++i)
        if ((members(i).ShapeType() == TopAbs_FACE || members(i).ShapeType() == TopAbs_EDGE) &&
            !covered.Contains(members(i)))
          fail("fillet output face or edge has no provenance");
      if (result.generated.empty()) fail("fillet has no generated-face provenance");
      result.estimated_bytes +=
          result.generated.size() * (sizeof(Generated) + sizeof(detail::Subshape) + 64);
      return result;
    });
  }
  std::expected<std::vector<FaceInfo>, KernelError> faces(const ShapePtr& input) override {
    return guarded([&] {
      const auto source = std::dynamic_pointer_cast<const detail::Snapshot>(input);
      if (!source || source->geometry.IsNull())
        fail("face query requires a snapshot from this backend");
      TopTools_IndexedMapOfShape all;
      TopExp::MapShapes(source->geometry, TopAbs_FACE, all);
      if (all.IsEmpty()) fail("snapshot contains no faces");
      std::vector<FaceInfo> result;
      result.reserve(static_cast<std::size_t>(all.Extent()));
      for (int i = 1; i <= all.Extent(); ++i)
        result.push_back({detail::entity(source, all(i)), planar_frame(TopoDS::Face(all(i))),
                          cylinder_frame(TopoDS::Face(all(i)))});
      return result;
    });
  }
  std::expected<Plane, KernelError> face_plane(const EntityPtr& input) override {
    return guarded([&] {
      const auto plane = planar_frame(checked_face(input));
      if (!plane) fail("selected face is not planar");
      return *plane;
    });
  }

  std::expected<double, KernelError> face_distance(const EntityPtr& input,
                                                   math::Vec3 position) override {
    return guarded([&] {
      if (!finite(position)) fail("distance query requires a finite world point");
      const auto face = checked_face(input);
      BRepBuilderAPI_MakeVertex vertex(point(position));
      const BRepExtrema_DistShapeShape distance(vertex.Shape(), face, Precision::Confusion());
      if (!distance.IsDone() || distance.NbSolution() == 0) fail("face distance query failed");
      const auto value = distance.Value();
      if (!std::isfinite(value) || value < 0) fail("face distance is not finite and nonnegative");
      return value;
    });
  }
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

  std::expected<OperationResult, KernelError> loft(const Loft& input) override {
    return guarded([&] {
      if (input.sections.size() < 2 || input.sections.size() > 32)
        fail("loft requires 2..32 sections");
      const auto& first = input.sections.front().plane;
      BRepOffsetAPI_ThruSections operation(true, false, tolerance_.linear);
      operation.SetMutableInput(false);
      operation.SetParType(Approx_IsoParametric);
      operation.SetMaxDegree(3);
      operation.CheckCompatibility(true);
      std::vector<EdgeSource> edges;
      double previous = 0, order = 0;
      for (std::size_t s = 0; s < input.sections.size(); ++s) {
        const auto& section = input.sections[s];
        validate_plane(section.plane);
        if (math::length(math::cross(section.plane.normal, first.normal)) > tolerance_.angular)
          fail("loft currently requires parallel section planes");
        const double position = math::dot(section.plane.origin - first.origin, first.normal);
        if (!std::isfinite(position)) fail("loft section spacing must be finite");
        if (s) {
          const double delta = position - previous;
          if (s == 1) order = delta > 0 ? 1 : -1;
          if (!std::isfinite(delta) || !(order * delta > tolerance_.linear))
            fail("loft section planes must be distinct and strictly ordered");
        }
        previous = position;
        if (!section.region.holes.empty() || section.region.outer.curves.empty() ||
            section.region.outer.curves.size() > 128)
          fail("loft requires one outer loop of 1..128 curves per section, without holes");
        std::size_t spans = edges.size();
        for (const auto& curve : section.region.outer.curves) {
          const auto knots = curve.knots().knots();
          for (std::size_t i = 1; i < knots.size(); ++i)
            if (knots[i] > knots[i - 1] && ++spans > 1024)
              fail("loft exceeds the 1024 profile-edge bound");
        }
        std::vector<EdgeSource> section_edges;
        const auto face = profile_face(section.region, section.plane, 0, section_edges);
        BRepAlgoAPI_Check profile_check(face, true, true);
        if (profile_check.HasErrors() || !profile_check.IsValid())
          fail("loft section is invalid or self-intersecting");
        if (section_edges.empty() || edges.size() + section_edges.size() > 1024)
          fail("loft exceeds the 1024 profile-edge bound");
        for (std::size_t c = 0; c < section.region.outer.curves.size(); ++c)
          if (std::ranges::none_of(section_edges,
                                   [&](const auto& edge) { return edge.from.curve == c; }))
            fail("loft profile curve collapses at kernel precision");
        operation.AddWire(BRepTools::OuterWire(face));
        for (auto& edge : section_edges) {
          edge.from.section = s;
          edges.push_back(std::move(edge));
        }
      }
      operation.Build();
      if (!operation.IsDone() || operation.Shape().IsNull()) fail("solid loft failed");
      BRepAlgoAPI_Check solid_check(operation.Shape(), true, true);
      if (solid_check.HasErrors() || !solid_check.IsValid())
        fail("loft result is invalid or self-intersecting");
      const auto start = operation.FirstShape(), end = operation.LastShape();
      TopTools_IndexedMapOfShape faces;
      TopExp::MapShapes(operation.Shape(), TopAbs_FACE, faces);
      if (start.IsNull() || end.IsNull() || !faces.Contains(start) || !faces.Contains(end))
        fail("loft has incomplete cap provenance");
      std::vector<LocalRole> roles{{"cap_start", start, {}}, {"cap_end", end, {}}};
      TopTools_MapOfShape covered;
      covered.Add(start);
      covered.Add(end);
      for (const auto& source : edges) {
        bool found = false;
        for (const auto& generated : operation.Generated(source.edge)) {
          if (generated.ShapeType() != TopAbs_FACE) continue;
          if (!faces.Contains(generated) || generated.IsSame(start) || generated.IsSame(end))
            fail("loft has inconsistent side provenance");
          roles.push_back({"side", generated, source.from});
          covered.Add(generated);
          found = true;
        }
        if (!found) fail("loft profile edge has no generated side");
      }
      if (covered.Extent() != faces.Extent()) fail("loft output face has no provenance");
      return generated_result(operation.Shape(), roles);
    });
  }

  std::expected<OperationResult, KernelError> sweep(const Sweep& input) override {
    return guarded([&] {
      validate_plane(input.plane);
      validate_plane(input.path_plane);
      if (input.path.empty() || input.path.size() > 32 || !input.region.holes.empty() ||
          input.region.outer.curves.empty() || input.region.outer.curves.size() > 128)
        fail("sweep requires 1..32 path curves and one profile loop of 1..128 curves, no holes");
      auto bounded_spans = [&](const auto& curves, std::size_t maximum) {
        std::size_t count = 0;
        for (const auto& curve : curves) {
          const auto knots = curve.knots().knots();
          for (std::size_t i = 1; i < knots.size(); ++i)
            if (knots[i] > knots[i - 1] && ++count > maximum)
              fail("sweep exceeds its path/profile span bound");
        }
      };
      bounded_spans(input.path, 64);
      bounded_spans(input.region.outer.curves, 1024);
      const auto y = math::cross(input.path_plane.normal, input.path_plane.x_axis);
      const auto to_world = [&](math::Vec2 p) {
        return input.path_plane.origin + input.path_plane.x_axis * p.x + y * p.y;
      };
      math::Vec2 previous_end, previous_tangent, initial_tangent;
      bool first = true;
      for (const auto& curve : input.path)
        for (const auto& span : math::decompose(curve)) {
          const auto poles = span.points();
          const auto chord = math::normalized(poles.back() - poles.front(), tolerance_);
          if (!chord) fail("sweep path span has no regular chord");
          // Positive weights and strictly increasing scalar Bezier poles certify a
          // strictly positive directional derivative throughout each complete span.
          for (std::size_t i = 1; i < poles.size(); ++i) {
            const double projection = math::dot(poles[i] - poles[i - 1], *chord);
            if (!std::isfinite(projection) || !(projection > tolerance_.linear))
              fail("sweep path span lacks the monotone-pole regularity certificate");
          }
          const auto start = math::normalized(poles[1] - poles.front(), tolerance_);
          const auto end = math::normalized(poles.back() - poles[poles.size() - 2], tolerance_);
          if (!start || !end) fail("sweep path has a degenerate tangent");
          if (!first && (!math::near(previous_end, poles.front(), tolerance_) ||
                         math::length(previous_tangent - *start) > tolerance_.angular))
            fail("sweep path must be connected and tangent-continuous without corners");
          if (first) initial_tangent = *start;
          first = false;
          previous_end = poles.back();
          previous_tangent = *end;
        }
      const auto start = input.path.front().point(input.path.front().domain_start());
      if (math::near(start, previous_end, tolerance_)) fail("closed sweep paths are unsupported");
      if (!math::near(input.plane.origin, to_world(start), tolerance_) ||
          math::length(input.plane.normal - input.path_plane.x_axis * initial_tangent.x -
                       y * initial_tangent.y) > tolerance_.angular)
        fail("sweep profile origin/normal must match the path start/forward tangent");
      std::vector<EdgeSource> path_edges, profile_edges;
      const auto spine = wire({input.path}, input.path_plane, 0, 0, path_edges, false);
      const auto profile = profile_face(input.region, input.plane, 0, profile_edges);
      BRepAlgoAPI_Check path_check(spine, true, true), profile_check(profile, true, true);
      if (!path_check.IsValid() || path_check.HasErrors() || !profile_check.IsValid() ||
          profile_check.HasErrors())
        fail("sweep path or profile is invalid or self-intersecting");
      for (std::size_t c = 0; c < input.region.outer.curves.size(); ++c)
        if (std::ranges::none_of(profile_edges,
                                 [&](const auto& edge) { return edge.from.curve == c; }))
          fail("sweep profile curve collapses at kernel precision");
      BRepOffsetAPI_MakePipeShell operation(spine);
      operation.SetMode(direction(input.path_plane.normal));
      operation.SetTolerance(tolerance_.linear, tolerance_.linear, tolerance_.angular);
      operation.SetMaxDegree(9);
      operation.SetMaxSegments(64);
      operation.SetForceApproxC1(false);
      TopoDS_Vertex first_vertex, last_vertex;
      TopExp::Vertices(spine, first_vertex, last_vertex);
      if (first_vertex.IsNull() || last_vertex.IsNull()) fail("sweep path is not open");
      operation.Add(BRepTools::OuterWire(profile), first_vertex, false, false);
      operation.Build();
      if (!operation.IsDone() || !operation.MakeSolid() || operation.Shape().IsNull())
        fail("solid sweep failed");
      const double error = operation.ErrorOnSurface();
      if (!std::isfinite(error) || error < 0 || error > tolerance_.linear)
        fail("sweep surface construction exceeds the requested linear tolerance");
      BRepAlgoAPI_Check solid_check(operation.Shape(), true, true);
      if (!solid_check.IsValid() || solid_check.HasErrors())
        fail("sweep result is invalid or self-intersecting");
      TopTools_IndexedMapOfShape faces;
      TopExp::MapShapes(operation.Shape(), TopAbs_FACE, faces);
      const auto cap_start = operation.FirstShape(), cap_end = operation.LastShape();
      if (cap_start.IsNull() || cap_end.IsNull() || !faces.Contains(cap_start) ||
          !faces.Contains(cap_end))
        fail("sweep has incomplete cap provenance");
      std::vector<LocalRole> roles{{"cap_start", cap_start, {}}, {"cap_end", cap_end, {}}};
      TopTools_MapOfShape covered;
      covered.Add(cap_start);
      covered.Add(cap_end);
      for (const auto& source : profile_edges) {
        bool found = false;
        for (const auto& generated : operation.Generated(source.edge)) {
          if (generated.ShapeType() != TopAbs_FACE) continue;
          if (!faces.Contains(generated) || generated.IsSame(cap_start) ||
              generated.IsSame(cap_end))
            fail("sweep has inconsistent side provenance");
          roles.push_back({"side", generated, source.from});
          covered.Add(generated);
          found = true;
        }
        if (!found) fail("sweep profile edge has no generated side");
      }
      if (covered.Extent() != faces.Extent()) fail("sweep output face has no provenance");
      return generated_result(operation.Shape(), roles);
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

  std::expected<OperationResult, KernelError> mirror(const ShapePtr& input,
                                                     const Plane& plane) override {
    return guarded([&] {
      const auto source = std::dynamic_pointer_cast<const detail::Snapshot>(input);
      if (!source) fail("shape belongs to an incompatible backend");
      validate_plane(plane);
      gp_Trsf transform;
      transform.SetMirror(
          gp_Ax2(point(plane.origin), direction(plane.normal), direction(plane.x_axis)));
      for (int row = 1; row <= 3; ++row)
        for (int column = 1; column <= 4; ++column)
          if (!std::isfinite(transform.Value(row, column)))
            fail("mirror exceeds finite coordinates");
      // A reflection reverses handedness. Duplicate geometry and fully validate its
      // oriented solids; the direct-isometry relocation shortcut does not apply.
      BRepBuilderAPI_Transform operation(source->geometry, transform, true);
      if (!operation.IsDone()) fail("mirror failed");
      auto shape = checked(operation.Shape());
      OperationResult result{shape, {}, {}, shape->estimated_bytes() + source->estimated_bytes()};
      TopTools_IndexedMapOfShape members;
      TopExp::MapShapes(shape->geometry, members);
      for (const auto& before : detail::entities(source)) {
        const auto& geometry = static_cast<const detail::Subshape&>(*before).geometry;
        const auto after = operation.ModifiedShape(geometry);
        const auto index = members.FindIndex(after);
        if (!index) fail("mirror did not retain complete entity history");
        result.history.push_back({before, {detail::entity(shape, members(index))}, {}, false});
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
      std::vector<std::unique_ptr<BRepBuilderAPI_Copy>> copies;
      auto operand = [&](const ShapePtr& input) {
        const auto source = std::dynamic_pointer_cast<const detail::Snapshot>(input);
        if (!source) fail("boolean shape belongs to an incompatible backend");
        auto found = std::ranges::find(sources, source);
        if (found == sources.end()) {
          auto copy = std::make_unique<BRepBuilderAPI_Copy>(source->geometry, true, false);
          if (!copy->IsDone()) fail("boolean input copy failed");
          sources.push_back(source);
          copies.push_back(std::move(copy));
          return copies.back()->Shape();
        }
        return copies[static_cast<std::size_t>(found - sources.begin())]->Shape();
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
      // Exact tolerance-enlarged boxes prove separation; overlapping boxes prove nothing.
      // The compound path avoids intersection work but retains boolean seam unification.
      bool separated = op == BooleanOp::kUnion;
      std::vector<Bnd_Box> bounds;
      if (separated)
        for (const auto& source : sources) {
          Bnd_Box box;
          BRepBndLib::AddOptimal(source->geometry, box, false, true);
          box.Enlarge(tolerance_.linear);
          if (box.IsVoid() || box.IsOpen()) separated = false;
          for (const auto& previous : bounds)
            if (!box.IsOut(previous)) separated = false;
          bounds.push_back(box);
          if (!separated) break;
        }
      Handle(BRepTools_History) simplification;
      TopoDS_Shape output_geometry;
      if (separated) {
        BRep_Builder builder;
        TopoDS_Compound compound;
        builder.MakeCompound(compound);
        // Adding a child clears its Free flag, so even compound construction uses copies.
        for (const auto& copy : copies) builder.Add(compound, copy->Shape());
        ShapeUpgrade_UnifySameDomain unifier(compound, true, true, true);
        unifier.SetLinearTolerance(tolerance_.linear);
        unifier.SetAngularTolerance(tolerance_.angular);
        unifier.SetSafeInputMode(true);
        unifier.AllowInternalEdges(false);
        unifier.Build();
        output_geometry = unifier.Shape();
        simplification = unifier.History();
        if (simplification.IsNull()) fail("separated union has no unification history");
      } else {
        operation.Build();
        if (!operation.IsDone() || operation.HasErrors()) {
          std::ostringstream errors;
          operation.DumpErrors(errors);
          fail("solid boolean failed: " + errors.str());
        }
        // Safe-input unification merges its own history into the boolean's history.
        operation.SimplifyResult(true, true, tolerance_.angular);
        output_geometry = operation.Shape();
      }
      auto shape = checked(std::move(output_geometry));
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
      for (std::size_t s = 0; s < sources.size(); ++s) {
        const auto& source = sources[s];
        result.estimated_bytes += source->estimated_bytes();
        for (const auto& before : detail::entities(source)) {
          const auto& original = static_cast<const detail::Subshape&>(*before).geometry;
          const auto recorded = copies[s]->ModifiedShape(original);
          History h{
              before,
              {},
              {},
              separated ? simplification->IsRemoved(recorded) : operation.IsDeleted(recorded)};
          for (const auto& after :
               separated ? simplification->Modified(recorded) : operation.Modified(recorded))
            h.modified.push_back(member(after));
          if (!h.deleted && h.modified.empty()) h.modified.push_back(member(recorded));
          for (const auto& after :
               separated ? simplification->Generated(recorded) : operation.Generated(recorded)) {
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
  std::optional<Cylinder> cylinder_frame(const TopoDS_Face& face) const {
    const BRepAdaptor_Surface surface(face);
    if (surface.GetType() != GeomAbs_Cylinder) return {};
    const auto cylinder = surface.Cylinder();
    const auto origin = cylinder.Axis().Location();
    const auto z = cylinder.Axis().Direction();
    Cylinder result{{{origin.X(), origin.Y(), origin.Z()}, {z.X(), z.Y(), z.Z()}},
                    cylinder.Radius()};
    if (!finite(result.axis.origin) || !finite(result.axis.direction) ||
        !std::isfinite(result.radius) || !(result.radius > 0) ||
        !math::near(math::length(result.axis.direction), 1, tolerance_.angular))
      fail("cylinder description must have a finite unit axis and positive radius");
    return result;
  }
  std::optional<Plane> planar_frame(const TopoDS_Face& face) const {
    const BRepAdaptor_Surface surface(face);
    if (surface.GetType() != GeomAbs_Plane) return {};
    const auto position = surface.Plane().Position();
    const auto vec = [](const auto& p) { return math::Vec3{p.X(), p.Y(), p.Z()}; };
    Plane plane{vec(position.Location()), vec(position.XDirection()),
                math::cross(vec(position.XDirection()), vec(position.YDirection()))};
    if (face.Orientation() == TopAbs_REVERSED) plane.normal = plane.normal * -1;
    if (face.Orientation() != TopAbs_FORWARD && face.Orientation() != TopAbs_REVERSED)
      fail("face has no outward orientation");
    validate_plane(plane);
    return plane;
  }
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
                   std::vector<EdgeSource>& edges, bool closed = true) const {
    if (loop.curves.empty()) fail("region boundary is empty");
    BRepBuilderAPI_MakeWire builder;
    const auto y = math::cross(p.normal, p.x_axis);
    for (std::size_t c = 0; c < loop.curves.size(); ++c) {
      const auto& curve = loop.curves[c];
      const auto& next = loop.curves[(c + 1) % loop.curves.size()];
      if ((closed || c + 1 < loop.curves.size()) &&
          !math::near(curve.point(curve.domain_end()), next.point(next.domain_start()), tolerance_))
        fail("region boundary is not closed");
      // Exact knot-span decomposition also prevents OCCT's surface-area quadrature from
      // integrating across unrecognized knots in a surface of linear extrusion.
      for (const auto& span : math::decompose(curve)) {
        std::vector<math::Vec3> points;
        for (auto v : span.points()) points.push_back(p.origin + p.x_axis * v.x + y * v.y);
        // A bridge shorter than OCCT's vertex precision would create a degenerate edge.
        // MakeWire instead welds the adjacent endpoints within that same precision floor.
        if (span.degree() == 1 && !span.rational() &&
            math::length(points.back() - points.front()) <= Precision::Confusion())
          continue;
        auto edge = [&] {
          // Exact linear spans give OCCT analytic planes/cylinders/cones after revolution.
          // Only the edge parameterization changes; profile-curve provenance is unchanged.
          if (span.degree() == 1 && !span.rational())
            return BRepBuilderAPI_MakeEdge(point(points.front()), point(points.back()));
          // Certified circles give analytic cylinders/spheres/tori instead of general
          // swept NURBS. As for lines, only parameterization changes; provenance stays.
          if (const auto arc = circular_arc(span, p))
            return BRepBuilderAPI_MakeEdge(arc->circle, 0, arc->angle);
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
