// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "snapshot.hpp"

#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
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
}  // namespace

std::expected<SnapshotPtr, KernelError> snapshot(TopoDS_Shape geometry) {
  try {
    if (geometry.IsNull() || !solid_tree(geometry) || !BRepCheck_Analyzer(geometry).IsValid())
      return std::unexpected(KernelError{"kernel", "output is not valid closed solid geometry"});
    auto result = std::make_shared<Snapshot>();
    result->geometry = std::move(geometry);
    auto& m = result->measured;
    GProp_GProps volume, area;
    BRepGProp::VolumeProperties(result->geometry, volume, kMassRelativeAccuracy, true);
    BRepGProp::SurfaceProperties(result->geometry, area, kMassRelativeAccuracy);
    m.volume = volume.Mass();
    m.area = area.Mass();
    m.centroid = point(volume.CentreOfMass());
    Bnd_Box bounds;
    BRepBndLib::AddOptimal(result->geometry, bounds, false, false);
    if (bounds.IsVoid() || bounds.IsOpen())
      return std::unexpected(KernelError{"kernel", "output has no finite bounds"});
    bounds.Get(m.minimum.x, m.minimum.y, m.minimum.z, m.maximum.x, m.maximum.y, m.maximum.z);
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
