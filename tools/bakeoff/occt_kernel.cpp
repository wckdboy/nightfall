// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// OpenCASCADE implementation of the bake-off Kernel interface.

#include <BRepAlgoAPI_BooleanOperation.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepLib.hxx>
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <Geom2d_Line.hxx>
#include <Geom2d_TrimmedCurve.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Standard_Failure.hxx>
#include <Standard_Version.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax3.hxx>
#include <gp_Circ.hxx>
#include <gp_Dir.hxx>
#include <gp_Dir2d.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <cmath>
#include <initializer_list>
#include <memory>
#include <numbers>
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
  // Measured, not declared: a fillet and a boolean are probed for whether every result face
  // and edge traces back through Modified/Generated (RFC 0001 F12).
  HistorySupport history_support() const override { return probe_history(); }

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

  Body rotate(Body body, Vec3 point, Vec3 axis, double degrees) override {
    return guarded([&] {
      gp_Trsf rotation;
      rotation.SetRotation(gp_Ax1(to_pnt(point), gp_Dir(axis.x, axis.y, axis.z)),
                           degrees * std::numbers::pi / 180.0);
      return BRepBuilderAPI_Transform(get(body), rotation, /*copy=*/true).Shape();
    });
  }

  Body cone(Vec3 base_center, double base_radius, double top_radius, double height) override {
    return guarded([&] {
      return BRepPrimAPI_MakeCone(gp_Ax2(to_pnt(base_center), gp::DZ()), base_radius, top_radius,
                                  height)
          .Shape();
    });
  }

  Body torus(Vec3 center, double major_radius, double minor_radius) override {
    return guarded([&] {
      return BRepPrimAPI_MakeTorus(gp_Ax2(to_pnt(center), gp::DZ()), major_radius, minor_radius)
          .Shape();
    });
  }

  Body extrude(const std::vector<Vec2>& profile, double base_z, double height) override {
    return guarded([&] {
      BRepBuilderAPI_MakePolygon polygon;
      for (const Vec2& p : profile) polygon.Add(gp_Pnt(p.x, p.y, base_z));
      polygon.Close();
      const TopoDS_Face face = BRepBuilderAPI_MakeFace(polygon.Wire(), /*OnlyPlane=*/true).Face();
      return BRepPrimAPI_MakePrism(face, gp_Vec(0, 0, height)).Shape();
    });
  }

  Body helix_pipe(double helix_radius, double height, double turns, double tube_radius) override {
    return guarded([&] {
      // An exact helix: a straight line in the (angle, z) parameter plane of a cylinder.
      const double pitch = height / turns;
      const double two_pi = 2.0 * std::numbers::pi;
      Handle(Geom_CylindricalSurface) cylinder =
          new Geom_CylindricalSurface(gp_Ax3(gp::Origin(), gp::DZ()), helix_radius);
      Handle(Geom2d_Line) line = new Geom2d_Line(gp_Pnt2d(0, 0), gp_Dir2d(two_pi, pitch));
      Handle(Geom2d_TrimmedCurve) segment =
          new Geom2d_TrimmedCurve(line, 0.0, turns * std::hypot(two_pi, pitch));
      TopoDS_Edge spine_edge = BRepBuilderAPI_MakeEdge(segment, cylinder).Edge();
      BRepLib::BuildCurves3d(spine_edge);
      const TopoDS_Wire spine = BRepBuilderAPI_MakeWire(spine_edge).Wire();
      // Circular profile at the start point, normal to the helix tangent (0, 2πR, pitch).
      const gp_Circ circle(
          gp_Ax2(gp_Pnt(helix_radius, 0, 0), gp_Dir(0, two_pi * helix_radius, pitch)), tube_radius);
      const TopoDS_Wire profile =
          BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(circle).Edge()).Wire();
      BRepOffsetAPI_MakePipeShell pipe(spine);
      pipe.SetMode(/*IsFrenet=*/true);  // the Frenet frame of a helix is exact and twist-free
      pipe.Add(profile);
      pipe.Build();
      if (!pipe.IsDone() || !pipe.MakeSolid()) throw KernelError("helix pipe failed");
      return pipe.Shape();
    });
  }

  Body variable_fillet(Body body, Region edges, double start_radius, double end_radius) override {
    return guarded([&] {
      const TopoDS_Shape& shape = get(body);
      BRepFilletAPI_MakeFillet builder(shape);
      TopTools_IndexedMapOfShape all_edges;
      TopExp::MapShapes(shape, TopAbs_EDGE, all_edges);
      int selected = 0;
      for (int i = 1; i <= all_edges.Extent(); ++i) {
        if (inside(all_edges(i), edges)) {
          builder.Add(start_radius, end_radius, TopoDS::Edge(all_edges(i)));
          ++selected;
        }
      }
      if (selected == 0) throw KernelError("variable fillet: no edges in region");
      builder.Build();
      if (!builder.IsDone()) throw KernelError("variable fillet failed");
      return builder.Shape();
    });
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
  template <typename Builder>
  static int untraced(Builder& builder, std::initializer_list<TopoDS_Shape> inputs,
                      TopAbs_ShapeEnum kind) {
    TopTools_IndexedMapOfShape traced;
    for (const TopoDS_Shape& input : inputs) {
      for (const TopAbs_ShapeEnum sub : {TopAbs_FACE, TopAbs_EDGE, TopAbs_VERTEX}) {
        TopTools_IndexedMapOfShape subs;
        TopExp::MapShapes(input, sub, subs);
        for (int i = 1; i <= subs.Extent(); ++i) {
          if (!builder.IsDeleted(subs(i)) && builder.Modified(subs(i)).IsEmpty())
            traced.Add(subs(i));
          for (const TopoDS_Shape& m : builder.Modified(subs(i))) traced.Add(m);
          for (const TopoDS_Shape& g : builder.Generated(subs(i))) traced.Add(g);
        }
      }
    }
    TopTools_IndexedMapOfShape result;
    TopExp::MapShapes(builder.Shape(), kind, result);
    int missing = 0;
    for (int i = 1; i <= result.Extent(); ++i) missing += traced.Contains(result(i)) ? 0 : 1;
    return missing;
  }

  // yes = faces and edges traceable through both probes; partial = faces only, or only some
  // operations; no = nothing traceable (RFC 0001 normalization).
  static HistorySupport probe_history() {
    try {
      const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 20.0, 30.0).Shape();
      BRepFilletAPI_MakeFillet fillet(box);
      TopTools_IndexedMapOfShape edges;
      TopExp::MapShapes(box, TopAbs_EDGE, edges);
      fillet.Add(2.0, TopoDS::Edge(edges(1)));
      fillet.Build();
      const TopoDS_Shape plate = BRepPrimAPI_MakeBox(12.0, 12.0, 1.5).Shape();
      const TopoDS_Shape tool =
          BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(6, 6, -1), gp::DZ()), 1.0, 3.5).Shape();
      BRepAlgoAPI_Cut cut(plate, tool);
      if (!fillet.IsDone() || !cut.IsDone()) return HistorySupport::kNo;
      const bool faces = untraced(fillet, {box}, TopAbs_FACE) == 0 &&
                         untraced(cut, {plate, tool}, TopAbs_FACE) == 0;
      const bool all_edges = untraced(fillet, {box}, TopAbs_EDGE) == 0 &&
                             untraced(cut, {plate, tool}, TopAbs_EDGE) == 0;
      if (faces && all_edges) return HistorySupport::kYes;
      return faces ? HistorySupport::kPartial : HistorySupport::kNo;
    } catch (const Standard_Failure&) {
      return HistorySupport::kNo;
    }
  }

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
