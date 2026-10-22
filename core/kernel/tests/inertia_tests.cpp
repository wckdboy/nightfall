// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// RFC 0013 mass properties: IKernel::mass_properties against analytic volume, centroid and
// unit-density inertia about the centroid, in world axes.
#include "nf/kernel/kernel.hpp"
#include "nf/math/constructors.hpp"

#include <cmath>
#include <numbers>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace nf::kernel;
using namespace nf::math;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {
constexpr double kPi = std::numbers::pi;
ShapePtr extrude(IKernel& kernel, std::vector<std::vector<NurbsCurve2>> paths, Vec3 origin,
                 double height) {
  return kernel.extrude({{origin}, regions(paths, FillRule::kEvenOdd).value(), {0, 0, height}})
      .value()
      .shape;
}
ShapePtr box(IKernel& kernel, Vec3 origin, Vec3 size) {
  return extrude(kernel,
                 {{make_line(Vec2{}, {size.x, 0}), make_line(Vec2{size.x, 0}, {size.x, size.y}),
                   make_line(Vec2{size.x, size.y}, {0, size.y}), make_line(Vec2{0, size.y}, {})}},
                 origin, size.z);
}
/// Row-major 3x3 entry.
double at(const IKernel::MassProperties& m, int row, int col) {
  return m.inertia[static_cast<std::size_t>(3 * row + col)];
}
}  // namespace

TEST_CASE("a box's inertia about its centroid is analytic wherever it sits", "[kernel][inertia]") {
  auto kernel = make_occt_kernel().value();
  const double a = 10, b = 20, c = 30, v = a * b * c;
  for (const auto origin : {Vec3{}, Vec3{-7, 12, 40}}) {
    CAPTURE(origin.x, origin.y, origin.z);
    const auto m = kernel->mass_properties(box(*kernel, origin, {a, b, c})).value();
    CHECK_THAT(m.volume, WithinRel(v, 1e-9));
    CHECK_THAT(m.centroid.x, WithinAbs(origin.x + a / 2, 1e-9));
    CHECK_THAT(m.centroid.y, WithinAbs(origin.y + b / 2, 1e-9));
    CHECK_THAT(m.centroid.z, WithinAbs(origin.z + c / 2, 1e-9));
    // Unit density: mm^5. Products of inertia vanish for an axis-aligned box.
    CHECK_THAT(at(m, 0, 0), WithinRel(v * (b * b + c * c) / 12, 1e-9));
    CHECK_THAT(at(m, 1, 1), WithinRel(v * (a * a + c * c) / 12, 1e-9));
    CHECK_THAT(at(m, 2, 2), WithinRel(v * (a * a + b * b) / 12, 1e-9));
    for (const auto [i, j] : {std::pair{0, 1}, {0, 2}, {1, 2}}) {
      CHECK_THAT(at(m, i, j), WithinAbs(0, 1e-9 * v * c * c));
      CHECK(at(m, i, j) == at(m, j, i));
    }
  }
}

TEST_CASE("a rotated box's inertia rotates with it", "[kernel][inertia]") {
  auto kernel = make_occt_kernel().value();
  const double a = 10, b = 20, c = 30, v = a * b * c, t = kPi / 6;
  const auto placed = box(*kernel, {}, {a, b, c});
  const auto turned = kernel->transform(placed, {{}, {}, {0, 0, 1}, t}).value().shape;
  const auto m = kernel->mass_properties(turned).value();
  // R diag(Ixx, Iyy, Izz) R^T for a rotation by t about z.
  const double ixx = v * (b * b + c * c) / 12, iyy = v * (a * a + c * c) / 12;
  const double cs = std::cos(t), sn = std::sin(t);
  CHECK_THAT(at(m, 0, 0), WithinRel(cs * cs * ixx + sn * sn * iyy, 1e-9));
  CHECK_THAT(at(m, 1, 1), WithinRel(sn * sn * ixx + cs * cs * iyy, 1e-9));
  CHECK_THAT(at(m, 0, 1), WithinRel(cs * sn * (ixx - iyy), 1e-9));
  CHECK_THAT(at(m, 2, 2), WithinRel(v * (a * a + b * b) / 12, 1e-9));
}

TEST_CASE("a hollow cylinder's inertia is analytic", "[kernel][inertia]") {
  auto kernel = make_occt_kernel().value();
  const double outer = 25, inner = 15, h = 40;
  const auto tube = extrude(*kernel, {{make_circle(Vec2{}, outer)}, {make_circle(Vec2{}, inner)}},
                            {0, 0, -h / 2}, h);
  const auto m = kernel->mass_properties(tube).value();
  const double v = kPi * (outer * outer - inner * inner) * h;
  const double r2 = outer * outer + inner * inner;
  CHECK_THAT(m.volume, WithinRel(v, 1e-9));
  CHECK_THAT(m.centroid.z, WithinAbs(0, 1e-9));
  CHECK_THAT(at(m, 2, 2), WithinRel(v * r2 / 2, 1e-9));
  CHECK_THAT(at(m, 0, 0), WithinRel(v * (3 * r2 + h * h) / 12, 1e-9));
  CHECK_THAT(at(m, 1, 1), WithinRel(v * (3 * r2 + h * h) / 12, 1e-9));
}
