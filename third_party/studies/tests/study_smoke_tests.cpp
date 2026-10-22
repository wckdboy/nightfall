// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// The MFEM half of the RFC 0014 smoke test: Netgen's mesh of an OCCT box is a valid
// tetrahedral mesh with every boundary triangle on a B-rep face, and MFEM solves Poisson's
// equation on it to the exact linear solution.
#include "box_mesh.hpp"

#include <array>
#include <cmath>
#include <set>

#include "mfem.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {
double tet_volume(const BoxMesh& m, std::size_t t) {
  std::array<std::array<double, 3>, 4> p{};
  for (std::size_t k = 0; k < 4; ++k)
    for (std::size_t c = 0; c < 3; ++c)
      p[k][c] = m.points[3 * static_cast<std::size_t>(m.tets[4 * t + k]) + c];
  const auto d = [&](std::size_t k, std::size_t c) { return p[k][c] - p[0][c]; };
  return std::abs(d(1, 0) * (d(2, 1) * d(3, 2) - d(2, 2) * d(3, 1)) -
                  d(1, 1) * (d(2, 0) * d(3, 2) - d(2, 2) * d(3, 0)) +
                  d(1, 2) * (d(2, 0) * d(3, 1) - d(2, 1) * d(3, 0))) /
         6;
}
}  // namespace

TEST_CASE("Netgen meshes an OCCT box with every boundary triangle on its face", "[studies]") {
  const auto mesh = mesh_box(40, 20, 10, 4);
  const auto tets = mesh.tets.size() / 4;
  REQUIRE(tets > 100);
  double volume = 0;
  for (std::size_t t = 0; t < tets; ++t) volume += tet_volume(mesh, t);
  CHECK_THAT(volume, WithinRel(8000.0, 1e-12));
  // Every node of every boundary triangle lies on the plane of the B-rep face it is tagged
  // with, by OCCT's own face order, and all six faces carry triangles.
  REQUIRE(mesh.planes.size() == 6);
  std::set<int> tagged;
  for (std::size_t s = 0; s < mesh.faces.size(); ++s) {
    const int face = mesh.faces[s];
    REQUIRE(face >= 1);
    REQUIRE(face <= 6);
    tagged.insert(face);
    const auto& plane = mesh.planes[static_cast<std::size_t>(face - 1)];
    for (std::size_t k = 0; k < 3; ++k) {
      const auto* p = &mesh.points[3 * static_cast<std::size_t>(mesh.triangles[3 * s + k])];
      CHECK_THAT(plane[0] * p[0] + plane[1] * p[1] + plane[2] * p[2], WithinAbs(plane[3], 1e-9));
    }
  }
  CHECK(tagged == std::set<int>{1, 2, 3, 4, 5, 6});
}

TEST_CASE("MFEM solves Poisson on the Netgen mesh to the exact linear solution", "[studies]") {
  const auto box = mesh_box(40, 20, 10, 5);
  const int nv = static_cast<int>(box.points.size() / 3);
  const int ne = static_cast<int>(box.tets.size() / 4);
  const int nb = static_cast<int>(box.faces.size());
  mfem::Mesh mesh(3, nv, ne, nb);
  for (int i = 0; i < nv; ++i)
    mesh.AddVertex(box.points[3 * static_cast<std::size_t>(i)],
                   box.points[3 * static_cast<std::size_t>(i) + 1],
                   box.points[3 * static_cast<std::size_t>(i) + 2]);
  for (int e = 0; e < ne; ++e) mesh.AddTet(&box.tets[4 * static_cast<std::size_t>(e)], 1);
  for (int s = 0; s < nb; ++s)
    mesh.AddBdrTriangle(&box.triangles[3 * static_cast<std::size_t>(s)],
                        box.faces[static_cast<std::size_t>(s)]);
  mesh.FinalizeTetMesh(1, 1, true);
  // -Laplace(u) = 0 with u = x + 2y + 3z on the whole boundary: P2 reproduces it exactly.
  mfem::H1_FECollection collection(2, 3);
  mfem::FiniteElementSpace space(&mesh, &collection);
  mfem::FunctionCoefficient exact([](const mfem::Vector& x) { return x(0) + 2 * x(1) + 3 * x(2); });
  mfem::Array<int> boundary(mesh.bdr_attributes.Max()), fixed;
  boundary = 1;
  space.GetEssentialTrueDofs(boundary, fixed);
  mfem::GridFunction u(&space);
  u = 0.0;
  u.ProjectBdrCoefficient(exact, boundary);
  mfem::LinearForm b(&space);
  b.Assemble();
  mfem::BilinearForm a(&space);
  a.AddDomainIntegrator(new mfem::DiffusionIntegrator);
  a.Assemble();
  mfem::SparseMatrix A;
  mfem::Vector B, X;
  a.FormLinearSystem(fixed, u, b, A, X, B);
  mfem::GSSmoother preconditioner(A);
  mfem::PCG(A, preconditioner, B, X, 0, 10000, 1e-28, 0.0);
  a.RecoverFEMSolution(X, b, u);
  CHECK(u.ComputeL2Error(exact) <= 1e-9 * std::sqrt(8000.0));
}
