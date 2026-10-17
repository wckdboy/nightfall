// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_adapters.hpp>
#include <catch2/generators/catch_generators_random.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

#include "nf/math/nurbs_curve.hpp"

using namespace nf::math;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

// Quarter unit circle in the XY plane as a rational quadratic (NURBS Book Ex. 7.1).
NurbsCurve3 quarter_circle() {
  const double w = std::numbers::sqrt2 / 2.0;
  return NurbsCurve3::make(KnotVector::make({0, 0, 0, 1, 1, 1}, 2).value(),
                           {{1, 0, 0}, {1, 1, 0}, {0, 1, 0}}, {1.0, w, 1.0})
      .value();
}

NurbsCurve3 wavy_cubic() {
  return NurbsCurve3::make(KnotVector::make({0, 0, 0, 0, 0.3, 0.6, 1, 1, 1, 1}, 3).value(),
                           {{0, 0, 0}, {1, 2, 0}, {2, -1, 1}, {3, 3, 2}, {4, 0, 1}, {5, 1, 0}},
                           {1.0, 0.5, 2.0, 1.0, 0.8, 1.0})
      .value();
}

}  // namespace

TEST_CASE("curves are validated", "[curve]") {
  const KnotVector k = KnotVector::make({0, 0, 1, 1}, 1).value();
  CHECK(NurbsCurve3::make(k, {{0, 0, 0}, {1, 0, 0}}).has_value());
  CHECK_FALSE(NurbsCurve3::make(k, {{0, 0, 0}}).has_value());                          // count
  CHECK_FALSE(NurbsCurve3::make(k, {{0, 0, 0}, {1, 0, 0}}, {1.0}).has_value());        // weights
  CHECK_FALSE(NurbsCurve3::make(k, {{0, 0, 0}, {1, 0, 0}}, {1.0, -1.0}).has_value());  // w <= 0
  CHECK_FALSE(NurbsCurve3::make(k, {{0, 0, 0}, {NAN, 0, 0}}).has_value());             // finite
}

TEST_CASE("a degree-1 curve is the control polygon", "[curve]") {
  const auto line =
      NurbsCurve2::make(KnotVector::make({0, 0, 1, 1}, 1).value(), {{1, 2}, {5, 10}}).value();
  CHECK_FALSE(line.rational());
  const Vec2 mid = line.point(0.25);
  CHECK_THAT(mid.x, WithinAbs(2.0, 1e-15));
  CHECK_THAT(mid.y, WithinAbs(4.0, 1e-15));
}

TEST_CASE("clamped curves interpolate their end control points", "[curve]") {
  const NurbsCurve3 c = wavy_cubic();
  CHECK(near(c.point(c.domain_start()), Vec3{0, 0, 0}, Tolerance{.linear = 1e-15}));
  CHECK(near(c.point(c.domain_end()), Vec3{5, 1, 0}, Tolerance{.linear = 1e-15}));
}

TEST_CASE("a rational quadratic quarter arc is exactly circular", "[curve][property]") {
  const NurbsCurve3 arc = quarter_circle();
  CHECK(arc.rational());
  const double u = GENERATE(take(100, random(0.0, 1.0)));
  CHECK_THAT(length(arc.point(u)), WithinAbs(1.0, 1e-15));
}

TEST_CASE("curve derivatives (A4.2) match finite differences", "[curve][property]") {
  const NurbsCurve3 c = wavy_cubic();
  const double u = GENERATE(take(40, random(0.02, 0.98)));
  std::array<Vec3, 3> d{};
  c.derivatives(u, 2, d);
  CHECK(near(d[0], c.point(u), Tolerance{.linear = 1e-14}));
  constexpr double h = 1e-6;
  if (c.knots().find_span(u - h) != c.knots().find_span(u + h)) return;  // straddles a knot
  std::array<Vec3, 3> p{}, m{};
  c.derivatives(u + h, 1, p);
  c.derivatives(u - h, 1, m);
  const Vec3 d1 = (c.point(u + h) - c.point(u - h)) / (2 * h);
  const Vec3 d2 = (p[1] - m[1]) / (2 * h);
  CHECK(length(d[1] - d1) <= 1e-6 * (1 + length(d1)));
  CHECK(length(d[2] - d2) <= 1e-4 * (1 + length(d2)));
}

TEST_CASE("the quarter arc's tangent is perpendicular to its radius", "[curve][property]") {
  const NurbsCurve3 arc = quarter_circle();
  const double u = GENERATE(take(50, random(0.0, 1.0)));
  std::array<Vec3, 2> d{};
  arc.derivatives(u, 1, d);
  CHECK_THAT(dot(d[0], d[1]), WithinAbs(0.0, 1e-13));
}

TEST_CASE("evaluation is affine invariant", "[curve][property]") {
  // Moving the control points moves the curve: C'(u) = C(u) + t.
  const NurbsCurve3 c = wavy_cubic();
  const Vec3 t{GENERATE(take(5, random(-100.0, 100.0))), 7.0, -3.0};
  std::vector<Vec3> moved;
  for (const Vec3& p : c.points()) moved.push_back(p + t);
  const NurbsCurve3 m =
      NurbsCurve3::make(c.knots(), moved,
                        std::vector<double>(c.weights().begin(), c.weights().end()))
          .value();
  for (double u = 0; u <= 1.0; u += 0.125) {
    CHECK(near(m.point(u), c.point(u) + t, Tolerance{.linear = 1e-12}));
  }
}

TEST_CASE("weights are normalised exactly and huge weights evaluate", "[curve]") {
  const KnotVector kv = KnotVector::make({0, 0, 1, 1}, 1).value();
  const double big = std::numeric_limits<double>::max();
  const auto equal = NurbsCurve2::make(kv, {{2, 0}, {2, 0}}, {big, big}).value();
  CHECK_FALSE(equal.rational());
  CHECK(equal.weights()[0] == 1.0);
  CHECK(equal.point(0.5) == Vec2{2, 0});
  const auto huge = NurbsCurve2::make(kv, {{0, 0}, {1, 0}}, {big, big / 2}).value();
  CHECK(huge.rational());
  CHECK(huge.weights()[0] >= 1.0);
  CHECK(huge.weights()[0] < 2.0);
  CHECK(std::isfinite(huge.point(0.5).x));
  // Scaling by a power of two changes no point on the curve, bit for bit.
  const auto a = NurbsCurve2::make(kv, {{0, 0}, {1, 3}}, {1, 3}).value();
  const auto b = NurbsCurve2::make(kv, {{0, 0}, {1, 3}}, {4, 12}).value();
  for (double u : {0.0, 0.1, 0.5, 0.77, 1.0}) CHECK(a.point(u) == b.point(u));
  CHECK_FALSE(NurbsCurve2::make(kv, {{0, 0}, {1, 0}}, {big, 1e-300}).has_value());
}

TEST_CASE("rational derivatives are computed above the degree", "[curve]") {
  // Points 0 and 1 with weights 1 and 2: x(u) = 2u / (1 + u) = 2 − 2 / (1 + u), so
  // x^(k)(u) = −2 (−1)^k k! (1 + u)^−(k+1) for k ≥ 1.
  const KnotVector kv = KnotVector::make({0, 0, 1, 1}, 1).value();
  const auto c = NurbsCurve2::make(kv, {{0, 0}, {1, 0}}, {1, 2}).value();
  std::array<Vec2, 13> d{};
  c.derivatives(0.5, 12, d);
  double factorial = 1.0;
  for (int k = 1; k <= 12; ++k) {
    factorial *= k;
    const double expected = -2.0 * (k % 2 ? -1.0 : 1.0) * factorial / std::pow(1.5, k + 1);
    CHECK_THAT(d[static_cast<std::size_t>(k)].x, WithinRel(expected, 1e-10));
  }
}
