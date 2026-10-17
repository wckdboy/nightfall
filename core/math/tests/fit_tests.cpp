// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_adapters.hpp>
#include <catch2/generators/catch_generators_random.hpp>

#include <cmath>
#include <numbers>
#include <random>
#include <vector>

#include "nf/math/constructors.hpp"
#include "nf/math/fit.hpp"
#include "nf/math/query.hpp"

using namespace nf::math;

namespace {

std::vector<Vec3> random_walk(std::uint32_t seed, int n) {
  std::mt19937 rng(seed);
  std::uniform_real_distribution<double> step(-1.0, 1.0);
  std::vector<Vec3> pts{{0, 0, 0}};
  for (int i = 1; i < n; ++i)
    pts.push_back(pts.back() + Vec3{1.0 + step(rng) * 0.5, step(rng), step(rng)});
  return pts;
}

}  // namespace

TEST_CASE("interpolation passes through every point", "[fit][property]") {
  const int degree = GENERATE(1, 2, 3, 5);
  const auto seed = GENERATE(take(5, random(1u, 1000000u)));
  const int n = GENERATE(10, 200);
  const auto pts = random_walk(seed, n);
  const auto c = interpolate<Vec3>(pts, degree);
  REQUIRE(c.has_value());
  CHECK(c->degree() == degree);
  CHECK_FALSE(c->rational());
  CHECK(c->points().front() == pts.front());
  CHECK(c->points().back() == pts.back());
  // Each data point is on the curve (checked by distance, independent of the parameters).
  for (const Vec3& q : pts) CHECK(closest_point(*c, q).distance <= 1e-9 * n);
}

TEST_CASE("interpolating collinear points gives a straight curve", "[fit]") {
  std::vector<Vec2> pts;
  for (int i = 0; i < 12; ++i) pts.push_back({i * 1.5, i * 0.5});
  const auto c = interpolate<Vec2>(pts, 3);
  REQUIRE(c.has_value());
  for (const Vec2 p : c->points()) CHECK(std::abs(p.x / 3.0 - p.y) <= 1e-12);
}

TEST_CASE("least squares meets the tolerance with few control points", "[fit][property]") {
  // Dense samples of a circle with ±1e-4 noise.
  const double tol = GENERATE(1e-2, 1e-3);
  std::mt19937 rng(11);
  std::uniform_real_distribution<double> noise(-1e-4, 1e-4);
  std::vector<Vec2> pts;
  for (int i = 0; i <= 1000; ++i) {
    const double t = 1.9 * std::numbers::pi * i / 1000.0;
    pts.push_back({10 * std::cos(t) + noise(rng), 10 * std::sin(t) + noise(rng)});
  }
  const auto fit = approximate<Vec2>(pts, tol, 3);
  REQUIRE(fit.has_value());
  CHECK(fit->max_deviation <= tol);
  CHECK(fit->curve.points().size() < 60);  // far fewer than the 1001 points
  CHECK(fit->curve.points().front() == pts.front());
  CHECK(fit->curve.points().back() == pts.back());
  // The reported deviation is what the curve does at the reported parameters.
  double worst = 0.0;
  for (std::size_t k = 0; k < pts.size(); ++k) {
    worst = std::max(worst, length(fit->curve.point(fit->parameters[k]) - pts[k]));
  }
  CHECK(worst == fit->max_deviation);
  // A tighter tolerance needs at least as many control points.
  const auto tighter = approximate<Vec2>(pts, tol / 10, 3);
  REQUIRE(tighter.has_value());
  CHECK(tighter->curve.points().size() >= fit->curve.points().size());
}

TEST_CASE("least squares recovers a cubic from its samples", "[fit]") {
  const NurbsCurve3 source =
      make_bezier(std::vector<Vec3>{{0, 0, 0}, {2, 5, 1}, {6, -3, 2}, {9, 1, 0}});
  std::vector<Vec3> pts;
  for (int i = 0; i <= 500; ++i) pts.push_back(source.point(i / 500.0));
  const auto fit = approximate<Vec3>(pts, 1e-6, 3);
  REQUIRE(fit.has_value());
  CHECK(fit->max_deviation <= 1e-6);
  CHECK(fit->curve.points().size() == 4);  // parameter correction recovers the cubic itself
}

TEST_CASE("fitting rejects bad input", "[fit]") {
  const std::vector<Vec2> few{{0, 0}, {1, 0}, {2, 1}};
  CHECK_FALSE(interpolate<Vec2>(few, 3).has_value());  // needs more points than the degree
  CHECK(interpolate<Vec2>(few, 2).has_value());
  CHECK_FALSE(interpolate<Vec2>(few, 0).has_value());
  const std::vector<Vec2> dup{{0, 0}, {1, 0}, {1, 0}, {2, 1}, {3, 3}};
  CHECK_FALSE(interpolate<Vec2>(dup, 3).has_value());
  CHECK_FALSE(approximate<Vec2>(dup, 0.1, 3).has_value());
  const std::vector<Vec2> ok{{0, 0}, {1, 0}, {2, 1}, {3, 3}, {4, 2}};
  CHECK_FALSE(approximate<Vec2>(ok, 0.0, 3).has_value());
  CHECK_FALSE(approximate<Vec2>(ok, std::nan(""), 3).has_value());
}
