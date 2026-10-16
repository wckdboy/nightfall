// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// OpenCASCADE implementation of the bake-off Kernel interface.

#include <BRepAlgoAPI_BooleanOperation.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <Standard_Failure.hxx>
#include <Standard_Version.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax2.hxx>
#include <gp_Pnt.hxx>

#include <memory>
#include <string>
#include <vector>

#include "kernel.hpp"

namespace nf::bakeoff {

namespace {

constexpr double kMassAccuracy = 1e-9;
constexpr double kShellTolerance = 1e-4;

gp_Pnt to_pnt(Vec3 v) { return {v.x, v.y, v.z}; }

bool inside(const TopoDS_Shape& entity, const Region& region) {
  Bnd_Box box;
  BRepBndLib::AddOptimal(entity, box, /*useTriangulation=*/false, /*useShapeTolerance=*/false);
  if (box.IsVoid()) return false;
  double x0, y0, z0, x1, y1, z1;
  box.Get(x0, y0, z0, x1, y1, z1);
  return x0 >= region.min.x && y0 >= region.min.y && z0 >= region.min.z && x1 <= region.max.x &&
         y1 <= region.max.y && z1 <= region.max.z;
}

class OcctKernel final : public Kernel {
 public:
  std::string_view name() const override { return "occt"; }
  std::string version() const override { return OCC_VERSION_COMPLETE; }
  // BRepBuilderAPI_MakeShape::Generated/Modified/IsDeleted on every builder used here.
  HistorySupport history_support() const override { return HistorySupport::kYes; }

  Body box(Vec3 origin, Vec3 size) override {
    return guarded(
        [&] { return BRepPrimAPI_MakeBox(to_pnt(origin), size.x, size.y, size.z).Shape(); });
  }

  Body cylinder(Vec3 base_center, double radius, double height) override {
    return guarded([&] {
      return BRepPrimAPI_MakeCylinder(gp_Ax2(to_pnt(base_center), gp::DZ()), radius, height)
          .Shape();
    });
  }

  Body sphere(Vec3 center, double radius) override {
    return guarded([&] { return BRepPrimAPI_MakeSphere(to_pnt(center), radius).Shape(); });
  }

  Body boolean(BooleanOp op, Body a, Body b) override {
    return guarded([&] {
      std::unique_ptr<BRepAlgoAPI_BooleanOperation> builder;
      switch (op) {
        case BooleanOp::kUnion:
          builder = std::make_unique<BRepAlgoAPI_Fuse>(get(a), get(b));
          break;
        case BooleanOp::kDifference:
          builder = std::make_unique<BRepAlgoAPI_Cut>(get(a), get(b));
          break;
        case BooleanOp::kIntersection:
          builder = std::make_unique<BRepAlgoAPI_Common>(get(a), get(b));
          break;
      }
      if (!builder->IsDone() || builder->HasErrors()) throw KernelError("boolean failed");
      return builder->Shape();
    });
  }

  Body fillet(Body body, Region edges, double radius, FilletKind kind) override {
    if (kind == FilletKind::kG2Blend) throw KernelError("OCCT has no G2 blend fillet");
    return guarded([&] {
      const TopoDS_Shape& shape = get(body);
      BRepFilletAPI_MakeFillet builder(shape);
      TopTools_IndexedMapOfShape all_edges;
      TopExp::MapShapes(shape, TopAbs_EDGE, all_edges);
      int selected = 0;
      for (int i = 1; i <= all_edges.Extent(); ++i) {
        if (inside(all_edges(i), edges)) {
          builder.Add(radius, TopoDS::Edge(all_edges(i)));
          ++selected;
        }
      }
      if (selected == 0) throw KernelError("fillet: no edges in region");
      builder.Build();
      if (!builder.IsDone()) throw KernelError("fillet failed");
      return builder.Shape();
    });
  }

  Body shell(Body body, Region open_faces, double thickness) override {
    return guarded([&] {
      const TopoDS_Shape& shape = get(body);
      TopTools_IndexedMapOfShape all_faces;
      TopExp::MapShapes(shape, TopAbs_FACE, all_faces);
      TopTools_ListOfShape removed;
      for (int i = 1; i <= all_faces.Extent(); ++i) {
        if (inside(all_faces(i), open_faces)) removed.Append(all_faces(i));
      }
      if (removed.IsEmpty()) throw KernelError("shell: no faces in region");
      BRepOffsetAPI_MakeThickSolid builder;
      builder.MakeThickSolidByJoin(shape, removed, -thickness, kShellTolerance);
      if (!builder.IsDone()) throw KernelError("shell failed");
      return builder.Shape();
    });
  }

  Measures measure(Body body) override {
    const TopoDS_Shape& shape = get(body);
    Measures m;
    try {
      m.valid = BRepCheck_Analyzer(shape).IsValid() && is_single_closed_solid(shape);
      GProp_GProps volume_props, area_props;
      BRepGProp::VolumeProperties(shape, volume_props, kMassAccuracy, /*OnlyClosed=*/true);
      BRepGProp::SurfaceProperties(shape, area_props, kMassAccuracy);
      m.volume = volume_props.Mass();
      m.area = area_props.Mass();
      TopTools_IndexedMapOfShape faces, edges;
      TopExp::MapShapes(shape, TopAbs_FACE, faces);
      TopExp::MapShapes(shape, TopAbs_EDGE, edges);
      m.faces = faces.Extent();
      m.edges = edges.Extent();
    } catch (const Standard_Failure& e) {
      throw KernelError(std::string("measure: ") + e.GetMessageString());
    }
    return m;
  }

 private:
  // Runs an OCCT builder, converting OCCT exceptions and null results into KernelError.
  template <typename F>
  Body guarded(F&& make) {
    TopoDS_Shape shape;
    try {
      shape = make();
    } catch (const Standard_Failure& e) {
      throw KernelError(e.GetMessageString());
    }
    if (shape.IsNull()) throw KernelError("operation produced no shape");
    bodies_.push_back(shape);
    return Body{static_cast<int>(bodies_.size()) - 1};
  }

  const TopoDS_Shape& get(Body body) const {
    if (body.id < 0 || static_cast<std::size_t>(body.id) >= bodies_.size()) {
      throw KernelError("invalid body handle");
    }
    return bodies_[static_cast<std::size_t>(body.id)];
  }

  // Exactly one solid with exactly one closed shell, and no faces outside it (a compound of
  // several solids or a solid with stray faces is not a valid part). See Measures::valid.
  static bool is_single_closed_solid(const TopoDS_Shape& shape) {
    TopTools_IndexedMapOfShape solids;
    TopExp::MapShapes(shape, TopAbs_SOLID, solids);
    if (solids.Extent() != 1) return false;
    const TopoDS_Shape& solid = solids(1);
    TopTools_IndexedMapOfShape shells_all, shells_solid, faces_all, faces_solid;
    TopExp::MapShapes(shape, TopAbs_SHELL, shells_all);
    TopExp::MapShapes(solid, TopAbs_SHELL, shells_solid);
    TopExp::MapShapes(shape, TopAbs_FACE, faces_all);
    TopExp::MapShapes(solid, TopAbs_FACE, faces_solid);
    if (shells_all.Extent() != 1 || shells_solid.Extent() != 1) return false;
    if (faces_all.Extent() != faces_solid.Extent()) return false;
    return BRep_Tool::IsClosed(shells_solid(1));
  }

  std::vector<TopoDS_Shape> bodies_;
};

}  // namespace

std::unique_ptr<Kernel> make_occt_kernel() { return std::make_unique<OcctKernel>(); }

}  // namespace nf::bakeoff
