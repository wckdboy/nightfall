// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "snapshot.hpp"

#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepGProp_Domain.hxx>
#include <BRepGProp_Face.hxx>
#include <BRepGProp_VinertGK.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Iterator.hxx>

namespace nf::kernel::detail {
namespace {
math::Vec3 point(const gp_Pnt& p) { return {p.X(), p.Y(), p.Z()}; }
bool finite(math::Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool solid_tree(const TopoDS_Shape& shape) {
  if (shape.ShapeType() == TopAbs_SOLID) {
    bool has_shell = false;
    for (TopExp_Explorer shells(shape, TopAbs_SHELL); shells.More(); shells.Next()) {
      has_shell = true;
      if (!BRep_Tool::IsClosed(shells.Current())) return false;
    }
    return has_shell;
  }
  if (shape.ShapeType() != TopAbs_COMPOUND && shape.ShapeType() != TopAbs_COMPSOLID) return false;
  bool has_child = false;
  for (TopoDS_Iterator child(shape); child.More(); child.Next()) {
    has_child = true;
    if (!solid_tree(child.Value())) return false;
  }
  return has_child;
}
bool volume_properties(const TopoDS_Shape& shape, Measures& measured) {
  // A corner keeps each coordinate's first moment away from a symmetry-axis zero.
  // Use each shell's bounds so distant components and cavities integrate locally.
  // The signed face integrals still use exact surfaces and the same GK tolerance.
  math::Vec3 moment;
  for (TopExp_Explorer shells(shape, TopAbs_SHELL); shells.More(); shells.Next()) {
    Bnd_Box bounds;
    BRepBndLib::AddOptimal(shells.Current(), bounds, false, false);
    if (bounds.IsVoid() || bounds.IsOpen()) return false;
    const auto reference = bounds.CornerMax();
    if (!finite(point(reference))) return false;
    BRepGProp_VinertGK integral;
    integral.SetLocation(reference);
    double shell_volume = 0;
    math::Vec3 shell_moment;
    BRepGProp_Face surface(true);
    for (TopExp_Explorer faces(shells.Current(), TopAbs_FACE); faces.More(); faces.Next()) {
      const auto face = TopoDS::Face(faces.Current());
      if (face.Orientation() != TopAbs_FORWARD && face.Orientation() != TopAbs_REVERSED) continue;
      surface.Load(face);
      BRepGProp_Domain domain(face);
      const auto error =
          face.NbChildren() == 0
              ? integral.Perform(surface, kMassRelativeAccuracy, true, false)
              : integral.Perform(surface, domain, kMassRelativeAccuracy, true, false);
      if (error < 0 || !std::isfinite(error)) return false;
      // GProps::Add normalizes after each face and drops first moments whenever a
      // partial signed sum is zero. Keep unnormalized moments until the shell is complete.
      shell_volume += integral.Mass();
      shell_moment =
          shell_moment + point(integral.CentreOfMass().XYZ() - reference.XYZ()) * integral.Mass();
    }
    measured.volume += shell_volume;
    moment = moment + shell_moment + (point(reference) - measured.maximum) * shell_volume;
  }
  if (!(measured.volume > 0)) return false;
  measured.centroid = measured.maximum + moment / measured.volume;
  return true;
}
}  // namespace

std::expected<SnapshotPtr, KernelError> snapshot(TopoDS_Shape geometry) {
  try {
    if (geometry.IsNull() || !solid_tree(geometry) || !BRepCheck_Analyzer(geometry).IsValid())
      return std::unexpected(KernelError{"kernel", "output is not valid closed solid geometry"});
    auto result = std::make_shared<Snapshot>();
    result->geometry = std::move(geometry);
    auto& m = result->measured;
    Bnd_Box bounds;
    BRepBndLib::AddOptimal(result->geometry, bounds, false, false);
    if (bounds.IsVoid() || bounds.IsOpen())
      return std::unexpected(KernelError{"kernel", "output has no finite bounds"});
    bounds.Get(m.minimum.x, m.minimum.y, m.minimum.z, m.maximum.x, m.maximum.y, m.maximum.z);
    if (!volume_properties(result->geometry, m))
      return std::unexpected(KernelError{"kernel", "volume integration failed"});
    GProp_GProps area;
    BRepGProp::SurfaceProperties(result->geometry, area, kMassRelativeAccuracy);
    m.area = area.Mass();
    if (!(m.volume > 0) || !(m.area > 0) || !std::isfinite(m.volume) || !std::isfinite(m.area) ||
        !finite(m.centroid) || !finite(m.minimum) || !finite(m.maximum))
      return std::unexpected(KernelError{"kernel", "output has invalid measurements"});
    TopTools_IndexedMapOfShape solids, faces, edges, vertices;
    TopExp::MapShapes(result->geometry, TopAbs_SOLID, solids);
    TopExp::MapShapes(result->geometry, TopAbs_FACE, faces);
    TopExp::MapShapes(result->geometry, TopAbs_EDGE, edges);
    TopExp::MapShapes(result->geometry, TopAbs_VERTEX, vertices);
    m.solids = static_cast<std::size_t>(solids.Extent());
    m.faces = static_cast<std::size_t>(faces.Extent());
    m.edges = static_cast<std::size_t>(edges.Extent());
    // Initial topology-weighted estimate; not a hard bound on NURBS/allocator storage.
    result->bytes =
        4096 + m.faces * 4096 + m.edges * 2048 + static_cast<std::size_t>(vertices.Extent()) * 256;
    return result;
  } catch (const Standard_Failure& e) {
    return std::unexpected(KernelError{"kernel", e.GetMessageString()});
  }
}

std::expected<SnapshotPtr, KernelError> rigid_snapshot(TopoDS_Shape geometry,
                                                       const Snapshot& source,
                                                       const gp_Trsf& transform) {
  try {
    if (!geometry.IsPartner(source.geometry) ||
        geometry.Orientation() != source.geometry.Orientation())
      return snapshot(std::move(geometry));
    const auto expected = transform * source.geometry.Location().Transformation();
    const auto actual = geometry.Location().Transformation();
    for (int row = 1; row <= 3; ++row)
      for (int column = 1; column <= 4; ++column)
        if (!std::isfinite(actual.Value(row, column)) ||
            actual.Value(row, column) != expected.Value(row, column))
          return snapshot(std::move(geometry));
    if (transform.IsNegative() || transform.ScaleFactor() != 1)
      return snapshot(std::move(geometry));
    auto result = std::make_shared<Snapshot>(source);
    result->geometry = std::move(geometry);
    auto& m = result->measured;
    m.centroid = point(gp_Pnt(m.centroid.x, m.centroid.y, m.centroid.z).Transformed(transform));
    if (transform.Form() == gp_Translation || transform.Form() == gp_Identity) {
      const auto t = transform.TranslationPart();
      m.minimum = m.minimum + math::Vec3{t.X(), t.Y(), t.Z()};
      m.maximum = m.maximum + math::Vec3{t.X(), t.Y(), t.Z()};
    } else {
      Bnd_Box bounds;
      BRepBndLib::AddOptimal(result->geometry, bounds, false, false);
      if (bounds.IsVoid() || bounds.IsOpen())
        return std::unexpected(KernelError{"kernel", "rigid output has no finite bounds"});
      bounds.Get(m.minimum.x, m.minimum.y, m.minimum.z, m.maximum.x, m.maximum.y, m.maximum.z);
    }
    if (!finite(m.centroid) || !finite(m.minimum) || !finite(m.maximum))
      return std::unexpected(KernelError{"kernel", "rigid output has nonfinite measurements"});
    return result;
  } catch (const Standard_Failure& e) {
    return std::unexpected(KernelError{"kernel", e.GetMessageString()});
  }
}

EntityKind Subshape::kind() const noexcept {
  return geometry.ShapeType() == TopAbs_FACE ? EntityKind::kFace : EntityKind::kEdge;
}
bool Subshape::same_as(const Entity& other) const noexcept {
  const auto* rhs = dynamic_cast<const Subshape*>(&other);
  return rhs && snapshot == rhs->snapshot && geometry.IsSame(rhs->geometry);
}
EntityPtr entity(SnapshotPtr owner, const TopoDS_Shape& geometry) {
  auto result = std::make_shared<Subshape>();
  result->snapshot = std::move(owner);
  result->geometry = geometry;
  return result;
}
std::vector<EntityPtr> entities(const SnapshotPtr& owner) {
  std::vector<EntityPtr> result;
  for (auto kind : {TopAbs_FACE, TopAbs_EDGE}) {
    TopTools_IndexedMapOfShape shapes;
    TopExp::MapShapes(owner->geometry, kind, shapes);
    for (int i = 1; i <= shapes.Extent(); ++i) result.push_back(entity(owner, shapes(i)));
  }
  return result;
}
}  // namespace nf::kernel::detail
