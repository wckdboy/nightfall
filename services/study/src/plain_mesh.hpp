// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// The RFC 0014 boundary between meshing and physics: standard types only, so the C++17 Netgen
// unit, the OCCT unit and the MFEM unit can all include it.
#pragma once

#include <string>
#include <vector>

namespace nf::study {
/// A linear tetrahedral mesh of one solid, in millimetres.
struct PlainMesh {
  std::vector<double> points;  // x, y, z per node
  std::vector<int> tets;       // four 0-based node indices per tetrahedron
  std::vector<int> triangles;  // three 0-based node indices per boundary triangle
  std::vector<int> faces;      // per boundary triangle: its 1-based face in OCCT's TopExp order
  [[nodiscard]] std::size_t node_count() const { return points.size() / 3; }
  [[nodiscard]] std::size_t tet_count() const { return tets.size() / 4; }
  [[nodiscard]] std::size_t triangle_count() const { return faces.size(); }
};

/// Meshes the solid in a BREP file with Netgen, elements up to `max_size` millimetres. Throws
/// std::runtime_error when Netgen cannot load or mesh it.
PlainMesh mesh_brep(const std::string& path, double max_size);
/// The mesher and its pinned build, for result provenance.
const char* mesher_identity();
}  // namespace nf::study
