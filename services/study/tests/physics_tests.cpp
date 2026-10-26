// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// RFC 0014 physics against analytic answers: kernel solids, Netgen meshes, MFEM solves.
#include "geometry.hpp"
#include "nf/math/constructors.hpp"
#include "physics.hpp"

#include <unistd.h>
#include <cmath>
#include <filesystem>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace nf;
using namespace nf::study;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {
kernel::ShapePtr box(kernel::IKernel& kernel, math::Vec3 size) {
  using math::make_line;
  using math::Vec2;
  std::vector<std::vector<math::NurbsCurve2>> paths{
      {make_line(Vec2{}, {size.x, 0}), make_line(Vec2{size.x, 0}, {size.x, size.y}),
       make_line(Vec2{size.x, size.y}, {0, size.y}), make_line(Vec2{0, size.y}, {})}};
  return kernel
      .extrude({{{}}, math::regions(paths, math::FillRule::kEvenOdd).value(), {0, 0, size.z}})
      .value()
      .shape;
}
/// The face of a box whose outward normal is `normal`, as the mesher numbers it.
int face(kernel::IKernel& kernel, const kernel::ShapePtr& solid, math::Vec3 normal) {
  for (const auto& f : kernel.faces(solid).value())
    if (f.plane && math::dot(f.plane->normal, normal) > 1 - 1e-12)
      return face_number(f.entity).value();
  FAIL("no face with that normal");
  return 0;
}
PlainMesh mesh(const kernel::ShapePtr& solid, double size) {
  const auto path =
      std::filesystem::temp_directory_path() / ("nf-physics-" + std::to_string(getpid()) + ".brep");
  REQUIRE(write_brep(solid, path.string()));
  auto result = mesh_brep(path.string(), size);
  std::filesystem::remove(path);
  return result;
}
}  // namespace

TEST_CASE("a cantilever's tip deflection converges to beam theory", "[study][structural]") {
  auto kernel = kernel::make_occt_kernel().value();
  const double L = 200, b = 10, h = 10, E = 69000, nu = 0.33, P = 10;
  const auto beam = box(*kernel, {L, b, h});
  StructuralInput input{E, nu, {face(*kernel, beam, {-1, 0, 0})}, {}, {}, 2};
  input.forces.push_back({{face(*kernel, beam, {1, 0, 0})}, {0, 0, -P}});
  const double beam_theory = P * L * L * L / (3 * E * b * h * h * h / 12);
  for (const double size : {5.0, 3.3, 2.2}) {
    CAPTURE(size);
    const auto result = solve_structural(mesh(beam, size), input).value();
    // The tip moves furthest; shear and the clamped root's restraint stay well inside 2%.
    CHECK_THAT(result.max_displacement, WithinRel(beam_theory, 0.02));
    // The load is what was asked for, and the support carries all of it.
    CHECK_THAT(result.applied[2], WithinAbs(-P, 1e-9 * P));
    for (std::size_t c = 0; c < 3; ++c)
      CHECK_THAT(result.reaction[c] + result.applied[c], WithinAbs(0, 1e-9 * P));
    // Peak stress is at least the beam's root bending stress 6PL/bh^2 (the clamped corners
    // concentrate it further).
    CHECK(result.max_von_mises > 0.9 * 6 * P * L / (b * h * h));
    CHECK(result.solver.relative_residual <= 1e-12);
  }
}

TEST_CASE("a pressure on a clamped column is carried by the support", "[study][structural]") {
  auto kernel = kernel::make_occt_kernel().value();
  const auto column = box(*kernel, {20, 30, 50});
  // 2 MPa pressing down on the 20 x 30 mm top face: the base reacts with exactly p A upward.
  StructuralInput input{200000, 0.3, {face(*kernel, column, {0, 0, -1})}, {}, {}, 2};
  input.pressures.push_back({{face(*kernel, column, {0, 0, 1})}, 2.0});
  const auto result = solve_structural(mesh(column, 6), input).value();
  CHECK_THAT(result.applied[2], WithinAbs(-1200, 1e-9 * 1200));
  CHECK_THAT(result.reaction[2], WithinAbs(1200, 1e-9 * 1200));
  CHECK_THAT(result.applied[0], WithinAbs(0, 1e-9 * 1200));
}

TEST_CASE("a slab between two temperatures conducts kA dT / t", "[study][thermal]") {
  auto kernel = kernel::make_occt_kernel().value();
  // Aluminium, 167 W/(m K): 100 x 100 x 10 mm, 400 K on top and 300 K below.
  const auto slab = box(*kernel, {100, 100, 10});
  ThermalInput input{167, {}, {}, {}, 2};
  input.temperatures.push_back({{face(*kernel, slab, {0, 0, 1})}, 400});
  input.temperatures.push_back({{face(*kernel, slab, {0, 0, -1})}, 300});
  const auto result = solve_thermal(mesh(slab, 10), input).value();
  const double q = 167 * 0.01 * 100 / 0.01;  // W: k A dT / t in SI
  // Quadratic elements hold the linear temperature field exactly.
  REQUIRE(result.heat_in.size() == 2);
  CHECK_THAT(result.heat_in[0], WithinRel(q, 1e-9));
  CHECK_THAT(result.heat_in[1], WithinRel(-q, 1e-9));
  CHECK_THAT(result.max_temperature, WithinAbs(400, 1e-9));
  CHECK_THAT(result.min_temperature, WithinAbs(300, 1e-9));
}

TEST_CASE("adjacent temperature groups conserve heat and empty or inert groups fail",
          "[study][thermal]") {
  auto kernel = kernel::make_occt_kernel().value();
  const auto slab = box(*kernel, {100, 100, 10});
  ThermalInput input{167, {}, {}, {}, 2};
  input.temperatures.push_back({{face(*kernel, slab, {0, 0, 1})}, 400});
  input.temperatures.push_back({{face(*kernel, slab, {0, 0, -1})}, 300});
  input.temperatures.push_back({{face(*kernel, slab, {1, 0, 0})}, 350});  // touches both
  const auto plain = mesh(slab, 10);
  const auto result = solve_thermal(plain, input).value();
  REQUIRE(result.heat_in.size() == 3);
  // Everything else is insulated: what enters leaves. Shared edges count once.
  const double total = result.heat_in[0] + result.heat_in[1] + result.heat_in[2];
  CHECK_THAT(total, WithinAbs(0, 1e-9 * std::abs(result.heat_in[0])));
  auto empty = input;
  empty.temperatures.push_back({{}, 300});
  CHECK_FALSE(solve_thermal(plain, empty));
  ThermalInput inert{167, {}, {}, {}, 2};
  inert.convection.push_back({{face(*kernel, slab, {0, 0, 1})}, 0, 350});
  CHECK_FALSE(solve_thermal(plain, inert));
}

TEST_CASE("convection and a fixed temperature act in series", "[study][thermal]") {
  auto kernel = kernel::make_occt_kernel().value();
  // 300 K below; the top loses heat to 350 K air at 50 W/(m^2 K): q = dT / (1/h + t/k).
  const auto slab = box(*kernel, {100, 100, 10});
  ThermalInput input{1.0, {}, {}, {}, 2};
  input.temperatures.push_back({{face(*kernel, slab, {0, 0, -1})}, 300});
  input.convection.push_back({{face(*kernel, slab, {0, 0, 1})}, 50, 350});
  const auto result = solve_thermal(mesh(slab, 10), input).value();
  const double flux = 50 / (1.0 / 50 + 0.01 / 1.0);  // W/m^2 from the air into the slab
  REQUIRE(result.heat_in.size() == 1);
  CHECK_THAT(result.heat_in[0], WithinRel(-flux * 0.01, 1e-9));  // out through the base
  // The solve stops at a 1e-12 relative residual: about 1e-8 K on a 300 K field.
  CHECK_THAT(result.max_temperature, WithinAbs(300 + flux * 0.01 / 1.0, 1e-8));
}
