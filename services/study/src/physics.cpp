// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// MFEM half of RFC 0014: no OCCT or Netgen headers here (OCCT's Handle macro breaks MFEM's).
#include "physics.hpp"
#include "sparse_cholesky.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <memory>

#include "mfem.hpp"

namespace nf::study {
namespace {
constexpr double kTolerance = 1e-12;  // relative residual of the iterative solve
constexpr int kMaxIterations = 100000;

std::unique_ptr<mfem::Mesh> build(const PlainMesh& plain) {
  // MFEM reports reoriented Netgen elements on its stdout stream; results go to stdout as JSON.
  mfem::out.Disable();
  const int nv = static_cast<int>(plain.node_count());
  const int ne = static_cast<int>(plain.tet_count());
  const int nb = static_cast<int>(plain.triangle_count());
  auto mesh = std::make_unique<mfem::Mesh>(3, nv, ne, nb);
  for (std::size_t i = 0; i < plain.node_count(); ++i)
    mesh->AddVertex(plain.points[3 * i], plain.points[3 * i + 1], plain.points[3 * i + 2]);
  for (std::size_t e = 0; e < plain.tet_count(); ++e) mesh->AddTet(&plain.tets[4 * e], 1);
  for (std::size_t s = 0; s < plain.triangle_count(); ++s)
    mesh->AddBdrTriangle(&plain.triangles[3 * s], plain.faces[s]);
  mesh->FinalizeTetMesh(1, 1, true);
  return mesh;
}
/// A boundary-attribute marker selecting `faces`; empty when a face has no triangles.
std::expected<mfem::Array<int>, std::string> marker(const mfem::Mesh& mesh,
                                                    const std::vector<int>& faces) {
  mfem::Array<int> m(mesh.bdr_attributes.Max());
  m = 0;
  for (const int f : faces) {
    if (f < 1 || f > m.Size() || mesh.bdr_attributes.Find(f) < 0)
      return std::unexpected("face " + std::to_string(f) + " has no boundary triangles");
    m[f - 1] = 1;
  }
  return m;
}
double area(mfem::Mesh& mesh, const mfem::Array<int>& selected) {
  double total = 0;
  for (int b = 0; b < mesh.GetNBE(); ++b) {
    if (!selected[mesh.GetBdrAttribute(b) - 1]) continue;
    auto* T = mesh.GetBdrElementTransformation(b);
    const auto& rule = mfem::IntRules.Get(T->GetGeometryType(), 2);
    for (int q = 0; q < rule.GetNPoints(); ++q) {
      T->SetIntPoint(&rule.IntPoint(q));
      total += T->Weight() * rule.IntPoint(q).weight;
    }
  }
  return total;
}
std::expected<SolverStats, std::string> solve(mfem::SparseMatrix& A, mfem::Vector& B,
                                              mfem::Vector& X, std::size_t unknowns, bool direct) {
  // Accuracy of an answer, measured on the system itself: the true residual relative to the load,
  // and the normwise backward error |r| / (|A| |x| + |b|) (maximum norms), which rounding alone
  // keeps near 1e-16 and no solver can push the residual's relative size much below.
  const auto measured = [&](const mfem::Vector& x, int iterations, const char* method) {
    mfem::Vector r(B.Size());
    A.Mult(x, r);
    r -= B;
    double a = 0;
    for (int i = 0; i < A.Height(); ++i) {
      double row = 0;
      for (int k = A.GetI()[i]; k < A.GetI()[i + 1]; ++k) row += std::abs(A.GetData()[k]);
      a = std::max(a, row);
    }
    const double scale = a * x.Normlinf() + B.Normlinf();
    const double norm = B.Norml2();
    return SolverStats{unknowns, iterations, norm > 0 ? r.Norml2() / norm : 0,
                       scale > 0 ? r.Normlinf() / scale : 0, method};
  };
  bool warm = false;  // CG starts from the direct answer when that is not accurate enough
  if (direct) {
    A.SortColumnIndices();
    const auto n = static_cast<std::size_t>(A.Height());
    const auto nnz = static_cast<std::size_t>(A.NumNonZeroElems());
    if (cholesky_solve(A.Height(), {A.GetI(), n + 1}, {A.GetJ(), nnz}, {A.GetData(), nnz},
                       {B.GetData(), n}, {X.GetData(), n})) {
      auto stats = measured(X, 0, "cholesky");
      if (stats.backward_error <= kTolerance) return stats;
      warm = std::isfinite(stats.relative_residual);
    }
  }
  mfem::GSSmoother preconditioner(A);
  mfem::CGSolver cg;
  cg.iterative_mode = warm;
  cg.SetOperator(A);
  cg.SetPreconditioner(preconditioner);
  cg.SetRelTol(kTolerance);
  cg.SetAbsTol(0.0);
  cg.SetMaxIter(kMaxIterations);
  cg.SetPrintLevel(-1);
  cg.Mult(B, X);
  if (!cg.GetConverged())
    return std::unexpected("the solver did not converge after " +
                           std::to_string(cg.GetNumIterations()) + " iterations");
  return measured(X, cg.GetNumIterations(), "cg");
}
}  // namespace

const char* solver_identity() { return "mfem 4.10 " NF_MFEM_BUILD_ID; }

std::expected<StructuralResult, std::string> solve_structural(const PlainMesh& plain,
                                                              const StructuralInput& input) {
  if (!(input.youngs_modulus > 0) || !(input.poisson_ratio > -1 && input.poisson_ratio < 0.5))
    return std::unexpected("material needs E > 0 and -1 < nu < 0.5");
  if (input.fixed.empty()) return std::unexpected("a structural study needs a fixed support");
  auto mesh = build(plain);
  mfem::H1_FECollection collection(input.order, 3);
  mfem::FiniteElementSpace space(mesh.get(), &collection, 3);
  auto fixed = marker(*mesh, input.fixed);
  if (!fixed) return std::unexpected(fixed.error());
  mfem::Array<int> fixed_dofs;
  space.GetEssentialTrueDofs(*fixed, fixed_dofs);
  mfem::LinearForm f(&space);
  std::vector<std::unique_ptr<mfem::Coefficient>> scalars;
  std::vector<std::unique_ptr<mfem::VectorCoefficient>> vectors;
  std::deque<mfem::Array<int>> markers;  // MFEM keeps pointers to these
  for (const auto& load : input.forces) {
    auto selected = marker(*mesh, load.faces);
    if (!selected) return std::unexpected(selected.error());
    const double a = area(*mesh, *selected);
    mfem::Vector traction(3);
    for (int c = 0; c < 3; ++c) traction(c) = load.newtons[static_cast<std::size_t>(c)] / a;
    vectors.push_back(std::make_unique<mfem::VectorConstantCoefficient>(traction));
    markers.push_back(std::move(*selected));
    f.AddBoundaryIntegrator(new mfem::VectorBoundaryLFIntegrator(*vectors.back()), markers.back());
  }
  for (const auto& load : input.pressures) {
    auto selected = marker(*mesh, load.faces);
    if (!selected) return std::unexpected(selected.error());
    // (f, v.n) with f = -p: a pressure pushes against the outward normal.
    scalars.push_back(std::make_unique<mfem::ConstantCoefficient>(-load.megapascals));
    markers.push_back(std::move(*selected));
    f.AddBoundaryIntegrator(new mfem::VectorBoundaryFluxLFIntegrator(*scalars.back()),
                            markers.back());
  }
  f.Assemble();
  const double E = input.youngs_modulus, nu = input.poisson_ratio;
  mfem::ConstantCoefficient lambda(E * nu / ((1 + nu) * (1 - 2 * nu))), mu(E / (2 * (1 + nu)));
  mfem::BilinearForm a(&space);
  a.AddDomainIntegrator(new mfem::ElasticityIntegrator(lambda, mu));
  a.Assemble();
  // The unconstrained operator gives the support reactions: r = K u - f on fixed DOFs.
  mfem::BilinearForm unconstrained(&space);
  unconstrained.AddDomainIntegrator(new mfem::ElasticityIntegrator(lambda, mu));
  unconstrained.Assemble();
  unconstrained.Finalize();
  mfem::GridFunction u(&space);
  u = 0.0;
  const mfem::Vector load(f);  // FormLinearSystem rewrites f while eliminating fixed DOFs
  mfem::SparseMatrix A;
  mfem::Vector B, X;
  a.FormLinearSystem(fixed_dofs, u, f, A, X, B);
  auto stats = solve(A, B, X, static_cast<std::size_t>(space.GetTrueVSize()), input.direct);
  if (!stats) return std::unexpected(stats.error());
  a.RecoverFEMSolution(X, f, u);
  StructuralResult result;
  result.solver = *stats;
  const int ndofs = space.GetNDofs();
  for (int i = 0; i < ndofs; ++i) {
    double m2 = 0;
    for (int c = 0; c < 3; ++c) m2 += std::pow(u(space.DofToVDof(i, c)), 2);
    result.max_displacement = std::max(result.max_displacement, std::sqrt(m2));
  }
  // K u = f + R: the support reaction R is the residual of the unconstrained system.
  mfem::Vector r(u.Size());
  unconstrained.Mult(u, r);
  r -= load;
  for (int i = 0; i < ndofs; ++i)
    for (int c = 0; c < 3; ++c) {
      result.applied[static_cast<std::size_t>(c)] += load(space.DofToVDof(i, c));
    }
  for (int k = 0; k < fixed_dofs.Size(); ++k) {
    const int vdof = fixed_dofs[k];
    result.reaction[static_cast<std::size_t>(
        space.GetOrdering() == mfem::Ordering::byNODES ? vdof / ndofs : vdof % 3)] += r(vdof);
  }
  // Von Mises stress at each element's quadrature points and vertices.
  mfem::DenseMatrix grad(3);
  for (int e = 0; e < mesh->GetNE(); ++e) {
    auto* T = space.GetElementTransformation(e);
    const auto& rule = mfem::IntRules.Get(mesh->GetElementGeometry(e), 2 * input.order);
    std::vector<mfem::IntegrationPoint> points(static_cast<std::size_t>(rule.GetNPoints()));
    for (int q = 0; q < rule.GetNPoints(); ++q)
      points[static_cast<std::size_t>(q)] = rule.IntPoint(q);
    const auto* vertices = mfem::Geometries.GetVertices(mesh->GetElementGeometry(e));
    for (int v = 0; v < vertices->GetNPoints(); ++v) points.push_back(vertices->IntPoint(v));
    for (const auto& ip : points) {
      T->SetIntPoint(&ip);
      u.GetVectorGradient(*T, grad);
      const double trace = grad(0, 0) + grad(1, 1) + grad(2, 2);
      double s[3][3];
      for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
          s[i][j] =
              mu.constant * (grad(i, j) + grad(j, i)) + (i == j ? lambda.constant * trace : 0);
      const double vm =
          std::sqrt(0.5 * (std::pow(s[0][0] - s[1][1], 2) + std::pow(s[1][1] - s[2][2], 2) +
                           std::pow(s[2][2] - s[0][0], 2)) +
                    3 * (s[0][1] * s[0][1] + s[1][2] * s[1][2] + s[0][2] * s[0][2]));
      result.max_von_mises = std::max(result.max_von_mises, vm);
    }
  }
  return result;
}

std::expected<ThermalResult, std::string> solve_thermal(const PlainMesh& plain,
                                                        const ThermalInput& input) {
  if (!(input.conductivity > 0)) return std::unexpected("material needs a conductivity > 0");
  if (input.temperatures.empty() && input.convection.empty())
    return std::unexpected("a thermal study needs a temperature or convection boundary");
  auto mesh = build(plain);
  mfem::H1_FECollection collection(input.order, 3);
  mfem::FiniteElementSpace space(mesh.get(), &collection);
  // Millimetres: W/(m K) -> W/(mm K); W/m^2 and W/(m^2 K) -> per mm^2.
  mfem::ConstantCoefficient k(input.conductivity * 1e-3);
  mfem::GridFunction T(&space);
  T = 0.0;
  mfem::Array<int> fixed(mesh->bdr_attributes.Max());
  fixed = 0;
  std::vector<mfem::Array<int>> groups;
  for (const auto& t : input.temperatures) {
    auto selected = marker(*mesh, t.faces);
    if (!selected) return std::unexpected(selected.error());
    mfem::ConstantCoefficient value(t.kelvin);
    T.ProjectBdrCoefficient(value, *selected);
    for (int i = 0; i < fixed.Size(); ++i) fixed[i] |= (*selected)[i];
    groups.push_back(std::move(*selected));
  }
  mfem::Array<int> fixed_dofs;
  space.GetEssentialTrueDofs(fixed, fixed_dofs);
  mfem::LinearForm f(&space);
  mfem::BilinearForm a(&space), unconstrained(&space);
  std::vector<std::unique_ptr<mfem::ConstantCoefficient>> coefficients;
  std::deque<mfem::Array<int>> markers;  // MFEM keeps pointers to these
  const auto coefficient = [&](double v) {
    coefficients.push_back(std::make_unique<mfem::ConstantCoefficient>(v));
    return coefficients.back().get();
  };
  for (const auto& q : input.fluxes) {
    auto selected = marker(*mesh, q.faces);
    if (!selected) return std::unexpected(selected.error());
    markers.push_back(std::move(*selected));
    f.AddBoundaryIntegrator(
        new mfem::BoundaryLFIntegrator(*coefficient(q.watts_per_square_metre * 1e-6)),
        markers.back());
  }
  for (auto* form : {&a, &unconstrained})
    form->AddDomainIntegrator(new mfem::DiffusionIntegrator(k));
  for (const auto& c : input.convection) {
    auto selected = marker(*mesh, c.faces);
    if (!selected) return std::unexpected(selected.error());
    markers.push_back(std::move(*selected));
    const double h = c.film_coefficient * 1e-6;
    for (auto* form : {&a, &unconstrained})
      form->AddBoundaryIntegrator(new mfem::BoundaryMassIntegrator(*coefficient(h)),
                                  markers.back());
    f.AddBoundaryIntegrator(new mfem::BoundaryLFIntegrator(*coefficient(h * c.ambient_kelvin)),
                            markers.back());
  }
  f.Assemble();
  a.Assemble();
  unconstrained.Assemble();
  unconstrained.Finalize();
  const mfem::Vector source(f);  // FormLinearSystem rewrites f while eliminating fixed DOFs
  mfem::SparseMatrix A;
  mfem::Vector B, X;
  a.FormLinearSystem(fixed_dofs, T, f, A, X, B);
  auto stats = solve(A, B, X, static_cast<std::size_t>(space.GetTrueVSize()), input.direct);
  if (!stats) return std::unexpected(stats.error());
  a.RecoverFEMSolution(X, f, T);
  ThermalResult result;
  result.solver = *stats;
  result.max_temperature = T.Max();
  result.min_temperature = T.Min();
  // Heat into the body through a prescribed-temperature group: the residual on its DOFs.
  mfem::Vector r(T.Size());
  unconstrained.Mult(T, r);
  r -= source;
  for (const auto& group : groups) {
    mfem::Array<int> dofs;
    space.GetEssentialTrueDofs(group, dofs);
    double heat = 0;
    for (int i = 0; i < dofs.Size(); ++i) heat += r(dofs[i]);
    result.heat_in.push_back(heat);
  }
  return result;
}
}  // namespace nf::study
