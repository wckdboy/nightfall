// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// The OCCT and Netgen half of the smoke test. Netgen's geometry is built inside the library
// (nglib reads the shape from a BREP file): an OCCGeometry constructed in client code fails a
// dynamic_cast across the library boundary.
#include "box_mesh.hpp"

#include <BRepAdaptor_Surface.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepTools.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>

#include <unistd.h>
#include <filesystem>
#include <stdexcept>

#include <meshing.hpp>
namespace nglib {
#include <nglib.h>
#include <nglib_occ.h>
}  // namespace nglib

BoxMesh mesh_box(double x, double y, double z, double maxh) {
  const auto path = std::filesystem::temp_directory_path() /
                    ("nf-study-smoke-" + std::to_string(getpid()) + ".brep");
  const auto shape = BRepPrimAPI_MakeBox(x, y, z).Shape();
  if (!BRepTools::Write(shape, path.c_str())) throw std::runtime_error("BREP write failed");
  netgen::printmessage_importance = 0;
  nglib::Ng_Init();
  auto* geometry = nglib::Ng_OCC_Load_BREP(path.c_str());
  std::filesystem::remove(path);
  if (!geometry) throw std::runtime_error("Netgen could not load the shape");
  auto* handle = nglib::Ng_NewMesh();
  nglib::Ng_Meshing_Parameters parameters;
  parameters.maxh = maxh;
  if (nglib::Ng_OCC_SetLocalMeshSize(geometry, handle, &parameters) != nglib::NG_OK ||
      nglib::Ng_OCC_GenerateEdgeMesh(geometry, handle, &parameters) != nglib::NG_OK ||
      nglib::Ng_OCC_GenerateSurfaceMesh(geometry, handle, &parameters) != nglib::NG_OK ||
      nglib::Ng_GenerateVolumeMesh(handle, &parameters) != nglib::NG_OK)
    throw std::runtime_error("Netgen meshing failed");
  const auto& mesh = *reinterpret_cast<const netgen::Mesh*>(handle);
  BoxMesh out;
  for (int i = 1; i <= nglib::Ng_GetNP(handle); ++i) {
    double p[3];
    nglib::Ng_GetPoint(handle, i, p);
    out.points.insert(out.points.end(), {p[0], p[1], p[2]});
  }
  for (int e = 1; e <= nglib::Ng_GetNE(handle); ++e) {
    int nodes[10];
    if (nglib::Ng_GetVolumeElement(handle, e, nodes) != nglib::NG_TET)
      throw std::runtime_error("Netgen returned a non-tetrahedral element");
    for (int k = 0; k < 4; ++k) out.tets.push_back(nodes[k] - 1);
  }
  for (int s = 1; s <= nglib::Ng_GetNSE(handle); ++s) {
    int nodes[8];
    if (nglib::Ng_GetSurfaceElement(handle, s, nodes) != nglib::NG_TRIG)
      throw std::runtime_error("Netgen returned a non-triangular boundary element");
    for (int k = 0; k < 3; ++k) out.triangles.push_back(nodes[k] - 1);
    // Netgen's surface numbers are 1-based positions in OCCT's TopExp face map.
    out.faces.push_back(mesh.GetFaceDescriptor(mesh.SurfaceElement(s).GetIndex()).SurfNr());
  }
  TopTools_IndexedMapOfShape faces;
  TopExp::MapShapes(shape, TopAbs_FACE, faces);
  for (int i = 1; i <= faces.Extent(); ++i) {
    const auto plane = BRepAdaptor_Surface(TopoDS::Face(faces(i))).Plane();
    const auto n = plane.Axis().Direction();
    const auto o = plane.Location();
    out.planes.push_back({n.X(), n.Y(), n.Z(), n.X() * o.X() + n.Y() * o.Y() + n.Z() * o.Z()});
  }
  // The mesh and geometry stay allocated: releasing nglib's mesh aborted in the RFC 0014
  // spike, and study processes are short-lived.
  return out;
}
