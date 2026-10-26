// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// Netgen half of RFC 0014 meshing. Compiled as C++17 (Netgen's headers reject C++23) and with
// no Nightfall kernel headers. Netgen builds its geometry inside the library from a BREP file:
// an OCCGeometry constructed here would fail a dynamic_cast across the library boundary.
#include "plain_mesh.hpp"

#include <stdexcept>

#include <meshing.hpp>
namespace nglib {
#include <nglib.h>
#include <nglib_occ.h>
}  // namespace nglib

namespace nf::study {
PlainMesh mesh_brep(const std::string& path, double max_size) {
  if (!(max_size > 0)) throw std::runtime_error("mesh size must be positive");
  netgen::printmessage_importance = 0;
  nglib::Ng_Init();
  auto* geometry = nglib::Ng_OCC_Load_BREP(path.c_str());
  if (!geometry) throw std::runtime_error("Netgen could not load the solid");
  auto* handle = nglib::Ng_NewMesh();
  nglib::Ng_Meshing_Parameters parameters;
  parameters.maxh = max_size;
  if (nglib::Ng_OCC_SetLocalMeshSize(geometry, handle, &parameters) != nglib::NG_OK ||
      nglib::Ng_OCC_GenerateEdgeMesh(geometry, handle, &parameters) != nglib::NG_OK ||
      nglib::Ng_OCC_GenerateSurfaceMesh(geometry, handle, &parameters) != nglib::NG_OK ||
      nglib::Ng_GenerateVolumeMesh(handle, &parameters) != nglib::NG_OK)
    throw std::runtime_error("Netgen could not mesh the solid");
  const auto& mesh = *reinterpret_cast<const netgen::Mesh*>(handle);
  PlainMesh out;
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
  if (out.tets.empty()) throw std::runtime_error("Netgen produced no tetrahedra");
  // The mesh and geometry stay allocated: releasing nglib's mesh aborted in the RFC 0014
  // spike, and study processes are short-lived.
  return out;
}
}  // namespace nf::study
