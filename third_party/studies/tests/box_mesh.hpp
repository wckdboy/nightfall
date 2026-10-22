// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <vector>

// A plain tetrahedral mesh: no OCCT, Netgen or MFEM type crosses this boundary (RFC 0014).
struct BoxMesh {
  std::vector<double> points;  // x, y, z per node, mm
  std::vector<int> tets;       // four 0-based node indices per tetrahedron
  std::vector<int> triangles;  // three 0-based node indices per boundary triangle
  std::vector<int> faces;      // the 1-based B-rep face each boundary triangle lies on
  // Each face's plane in OCCT's TopExp face order (the order `faces` indexes): a unit normal
  // and its offset, n . p = d.
  std::vector<std::array<double, 4>> planes;
};
// An OCCT box from the origin, meshed by Netgen through nglib with elements up to `maxh`.
BoxMesh mesh_box(double x, double y, double z, double maxh);
