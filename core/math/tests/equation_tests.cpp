// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

#include "nf/math/equation.hpp"
#include "nf/math/query.hpp"

using namespace nf::math;

namespace {

EquationCurve curve(const char* x, const char* y, const char* z, double t0, double t1,
                    double tol = 1e-3) {
  auto r = equation_curve(x, y, z, t0, t1, tol);
  INFO((r ? "" : r.error()));
  REQUIRE(r.has_value());
  return std::move(*r);
}

// Largest distance from points of the equations to the curve, densely and globally.
template <typename F>
double distance_from(const NurbsCurve3& c, F f, double t0, double t1) {
  double worst = 0.0;
  for (int i = 0; i <= 400; ++i)
    worst = std::max(worst, closest_point(c, f(t0 + (t1 - t0) * i / 400)).distance);
  return worst;
}

}  // namespace

TEST_CASE("polynomials are exact Bézier curves in t", "[equation]") {
  const auto r = curve("t", "t^2", "t^3 - 2*t", -1, 2);
  CHECK(r.exact);
  CHECK(r.max_deviation == 0.0);
  CHECK(r.curve.degree() == 3);
  for (int i = 0; i <= 30; ++i) {
    const double t = -1 + 3.0 * i / 30;
    CHECK(length(r.curve.point(t) - Vec3{t, t * t, t * t * t - 2 * t}) <= 1e-12 * 10);
  }
  const auto line = curve("2*t + 1", "0", "-t", 0, 1);
  CHECK(line.exact);
  CHECK(line.curve.degree() == 1);
  CHECK(curve("(t+1)*(t-1)/2", "-(t^2)", "3", 0, 1).curve.degree() == 2);
}

TEST_CASE("sinusoids of one frequency are exact elliptical arcs", "[equation]") {
  const auto circle = curve("3*cos(t)", "3*sin(t)", "0", 0, 2 * std::numbers::pi);
  CHECK(circle.exact);
  CHECK(circle.curve.rational());
  for (int i = 0; i <= 40; ++i) {
    const Vec3 p =
        circle.curve.point(circle.curve.domain_start() +
                           (circle.curve.domain_end() - circle.curve.domain_start()) * i / 40);
    CHECK(std::abs(std::hypot(p.x, p.y) - 3.0) <= 1e-12 * 3);
  }
  // Phase, offsets, mixed sin/cos, another frequency, a negative frequency.
  const auto f = [](double t) {
    return Vec3{1 + 2 * std::cos(2 * t + 0.3), -1 + std::sin(2 * t + 0.3) + 0.5 * std::cos(2 * t),
                4};
  };
  const auto e = curve("1 + 2*cos(2*t + 0.3)", "-1 + sin(2*t + 0.3) + 0.5*cos(2*t)", "4", 0.2, 2.9);
  CHECK(e.exact);
  CHECK(distance_from(e.curve, f, 0.2, 2.9) <= 1e-11);
  CHECK(length(e.curve.point(e.curve.domain_start()) - f(0.2)) <= 1e-12 * 5);
  CHECK(length(e.curve.point(e.curve.domain_end()) - f(2.9)) <= 1e-12 * 5);
  // One frequency written with opposite signs is still one ellipse (review finding).
  CHECK(curve("cos(t)", "sin(-t)", "0", 0, 1).exact);
  const auto back = curve("cos(-t)", "sin(-t)", "t*0", 0, 1);
  CHECK(back.exact);
  CHECK(length(back.curve.point(back.curve.domain_end()) -
               Vec3{std::cos(1.0), -std::sin(1.0), 0}) <= 1e-12);
}

TEST_CASE("other curves are fitted within the tolerance", "[equation]") {
  const double tol = 1e-4;
  // More than a turn, a helix, an involute.
  const auto turns = curve("cos(t)", "sin(t)", "0", 0, 7, tol);
  CHECK_FALSE(turns.exact);
  const auto helix = curve("cos(t)", "sin(t)", "0.2*t", 0, 4 * std::numbers::pi, tol);
  CHECK_FALSE(helix.exact);
  CHECK(helix.max_deviation <= tol);
  CHECK(distance_from(
            helix.curve, [](double t) { return Vec3{std::cos(t), std::sin(t), 0.2 * t}; }, 0,
            4 * std::numbers::pi) <= tol);
  const auto involute =
      curve("cos(t) + t*sin(t)", "sin(t) - t*cos(t)", "0", 0, 3 * std::numbers::pi, tol);
  CHECK_FALSE(involute.exact);
  CHECK(distance_from(
            involute.curve,
            [](double t) {
              return Vec3{std::cos(t) + t * std::sin(t), std::sin(t) - t * std::cos(t), 0};
            },
            0, 3 * std::numbers::pi) <= tol);
  CHECK(curve("exp(t)", "log(t + 2)", "sqrt(t + 1) + abs(t)", -0.5, 1, tol).max_deviation <= tol);
}

TEST_CASE("bad equations and ranges are errors", "[equation]") {
  CHECK_FALSE(equation_curve("sin(", "0", "0", 0, 1).has_value());
  CHECK_FALSE(equation_curve("foo(t)", "0", "0", 0, 1).has_value());
  CHECK_FALSE(equation_curve("t +", "0", "0", 0, 1).has_value());
  CHECK_FALSE(equation_curve("2t", "0", "0", 0, 1).has_value());  // no implicit product
  CHECK_FALSE(equation_curve("0x10", "0", "0", 0, 1).has_value());
  CHECK_FALSE(equation_curve("t", "0", "0", 1, 1).has_value());
  CHECK_FALSE(equation_curve("1/(t-1)", "0", "0", 0, 2).has_value());
  CHECK_FALSE(equation_curve("sqrt(t)", "t", "0", -1, 1).has_value());
  // Undefined only between the fitting samples: caught by the denser check (review finding).
  CHECK_FALSE(equation_curve("sqrt((t-0.0009765625)^2-0.00000001)", "t", "0", 0, 1, 1).has_value());
  CHECK(equation_curve("-2^2", "2^-1", "pi + e", 0, 1)->curve.point(0) ==
        Vec3{-4, 0.5, std::numbers::pi + std::numbers::e});
}
