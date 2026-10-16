// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// SPIKE (#8): OCCT implementation of the kernel bridge.

#include "kernel_bridge.h"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <Poly_Triangulation.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Ax2.hxx>
#include <gp_Vec.hxx>

#include <cstdlib>
#include <vector>

struct nf_part {
  TopoDS_Shape shape;
};

extern "C" {

nf_part* nf_part_box(double dx, double dy, double dz) {
  try {
    return new nf_part{BRepPrimAPI_MakeBox(dx, dy, dz).Shape()};
  } catch (const Standard_Failure&) {
    return nullptr;
  }
}

int nf_part_fillet_all(nf_part* part, double radius) {
  try {
    BRepFilletAPI_MakeFillet fillet(part->shape);
    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(part->shape, TopAbs_EDGE, edges);
    for (int i = 1; i <= edges.Extent(); ++i) fillet.Add(radius, TopoDS::Edge(edges(i)));
    fillet.Build();
    if (!fillet.IsDone()) return 1;
    part->shape = fillet.Shape();
    return 0;
  } catch (const Standard_Failure&) {
    return 1;
  }
}

int nf_part_hole(nf_part* part, double x, double y, double radius) {
  try {
    const TopoDS_Shape tool =
        BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(x, y, -1e3), gp::DZ()), radius, 2e3).Shape();
    BRepAlgoAPI_Cut cut(part->shape, tool);
    if (!cut.IsDone() || cut.HasErrors()) return 1;
    part->shape = cut.Shape();
    return 0;
  } catch (const Standard_Failure&) {
    return 1;
  }
}

int nf_part_mesh(const nf_part* part, double deflection, nf_mesh* out) {
  try {
    BRepMesh_IncrementalMesh(part->shape, deflection, false, 0.3);
    std::vector<float> v;
    for (TopExp_Explorer it(part->shape, TopAbs_FACE); it.More(); it.Next()) {
      const TopoDS_Face face = TopoDS::Face(it.Current());
      TopLoc_Location loc;
      const Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
      if (tri.IsNull()) continue;
      const bool reversed = face.Orientation() == TopAbs_REVERSED;
      for (int t = 1; t <= tri->NbTriangles(); ++t) {
        int a, b, c;
        tri->Triangle(t).Get(a, b, c);
        if (reversed) std::swap(b, c);
        const gp_Pnt p[3] = {tri->Node(a).Transformed(loc), tri->Node(b).Transformed(loc),
                             tri->Node(c).Transformed(loc)};
        gp_Vec n = gp_Vec(p[0], p[1]).Crossed(gp_Vec(p[0], p[2]));
        if (n.Magnitude() > 0) n.Normalize();
        for (const gp_Pnt& q : p) {
          v.insert(v.end(), {float(q.X()), float(q.Y()), float(q.Z()), float(n.X()), float(n.Y()),
                             float(n.Z())});
        }
      }
    }
    GProp_GProps props;
    BRepGProp::VolumeProperties(part->shape, props);
    out->vertex_count = v.size() / 6;
    out->vertices = static_cast<float*>(std::malloc(v.size() * sizeof(float)));
    std::copy(v.begin(), v.end(), out->vertices);
    out->volume = props.Mass();
    return 0;
  } catch (const Standard_Failure&) {
    return 1;
  }
}

void nf_part_free(nf_part* part) { delete part; }

void nf_mesh_free(nf_mesh* mesh) {
  std::free(mesh->vertices);
  mesh->vertices = nullptr;
  mesh->vertex_count = 0;
}

}  // extern "C"
