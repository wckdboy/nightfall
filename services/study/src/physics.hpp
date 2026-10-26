// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// RFC 0014 physics on MFEM: linear static structural and steady thermal analysis of one solid's
// tetrahedral mesh with quadratic (P2) H1 elements. Faces are the mesh's 1-based B-rep face
// numbers. Structural units are mm, N and MPa; thermal units are mm, W and K, with SI material
// and boundary values converted here.
#pragma once

#include <array>
#include <expected>
#include <string>
#include <vector>

#include "plain_mesh.hpp"

namespace nf::study {
struct SolverStats {
  std::size_t unknowns = 0;
  int iterations = 0;
  double relative_residual = 0;
};

/// A total force (N) spread uniformly by area over faces.
struct FaceForce {
  std::vector<int> faces;
  std::array<double, 3> newtons{};
};
/// A pressure (MPa) pushing into the faces, along their inward normal.
struct FacePressure {
  std::vector<int> faces;
  double megapascals = 0;
};
struct StructuralInput {
  double youngs_modulus = 0;  // MPa
  double poisson_ratio = 0;
  std::vector<int> fixed;  // faces held in place
  std::vector<FaceForce> forces;
  std::vector<FacePressure> pressures;
  int order = 2;
};
struct StructuralResult {
  double max_displacement = 0;       // mm, largest nodal displacement magnitude
  double max_von_mises = 0;          // MPa, at quadrature points and vertices
  std::array<double, 3> applied{};   // N, total load
  std::array<double, 3> reaction{};  // N, total support reaction; balances `applied`
  SolverStats solver;
};
std::expected<StructuralResult, std::string> solve_structural(const PlainMesh& mesh,
                                                              const StructuralInput& input);

struct FaceTemperature {
  std::vector<int> faces;
  double kelvin = 0;
};
/// A heat flux (W/m²) into the body through faces.
struct FaceFlux {
  std::vector<int> faces;
  double watts_per_square_metre = 0;
};
/// Convection to an ambient temperature with a film coefficient (W/m²K).
struct FaceConvection {
  std::vector<int> faces;
  double film_coefficient = 0;
  double ambient_kelvin = 0;
};
struct ThermalInput {
  double conductivity = 0;  // W/(m K)
  std::vector<FaceTemperature> temperatures;
  std::vector<FaceFlux> fluxes;
  std::vector<FaceConvection> convection;
  int order = 2;
};
struct ThermalResult {
  double max_temperature = 0, min_temperature = 0;  // K, over nodes
  std::vector<double> heat_in;  // W into the body through each `temperatures` group
  SolverStats solver;
};
std::expected<ThermalResult, std::string> solve_thermal(const PlainMesh& mesh,
                                                        const ThermalInput& input);
}  // namespace nf::study
