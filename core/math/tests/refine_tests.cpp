// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_adapters.hpp>
#include <catch2/generators/catch_generators_random.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <random>
#include <vector>

#include "nf/math/constructors.hpp"
#include "nf/math/refine.hpp"

using namespace nf::math;

namespace {

// A random rational cubic-ish curve whose interior knots include repeats, so refinement
// meets every multiplicity from 1 to p.
NurbsCurve3 random_curve(int degree, std::uint32_t seed) {
  std::mt19937 rng(seed);
  std::uniform_real_distribution<double> coord(-10.0, 10.0), weight(0.3, 3.0), knot(0.05, 0.95);
  std::vector<double> interior;
  for (int i = 0; i < 4; ++i) {
    const double k = knot(rng);
    const int mult = 1 + static_cast<int>(rng() % static_cast<std::uint32_t>(degree));
    interior.insert(interior.end(), static_cast<std::size_t>(mult), k);
  }
  std::ranges::sort(interior);
  std::vector<double> knots(static_cast<std::size_t>(degree + 1), 0.0);
  knots.insert(knots.end(), interior.begin(), interior.end());
  knots.insert(knots.end(), static_cast<std::size_t>(degree + 1), 1.0);
  auto kv = KnotVector::make(std::move(knots), degree).value();
  std::vector<Vec3> points;
  std::vector<double> weights;
  for (int i = 0; i < kv.control_point_count(); ++i) {
    points.push_back({coord(rng), coord(rng), coord(rng)});
    weights.push_back(weight(rng));
  }
  return NurbsCurve3::make(std::move(kv), std::move(points), std::move(weights)).value();
}

double distance(Vec3 a, Vec3 b) { return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z); }

// Largest distance between a and b over 200 parameters in [lo, hi].
double max_deviation(const NurbsCurve3& a, const NurbsCurve3& b, double lo, double hi) {
  double worst = 0.0;
  for (int i = 0; i <= 200; ++i) {
    const double u = lo + (hi - lo) * i / 200.0;
    worst = std::max(worst, distance(a.point(u), b.point(u)));
  }
  return worst;
}

int multiplicity(const NurbsCurve3& c, double u) {
  return static_cast<int>(std::ranges::count(c.knots().knots(), u));
}

constexpr double kTol = 1e-12 * 10.0;  // relative to the ±10 coordinate range

}  // namespace

TEST_CASE("knot insertion (A5.1) leaves the curve unchanged", "[refine][property]") {
  const int degree = GENERATE(1, 2, 3, 5);
  const auto seed = GENERATE(take(10, random(1u, 1000000u)));
  const NurbsCurve3 c = random_curve(degree, seed);
  // A fresh knot, and every existing interior knot up to full multiplicity.
  std::vector<double> targets{0.5 + 0.01 * (seed % 7)};
  for (double k : c.knots().knots()) {
    if (k > 0.0 && k < 1.0) targets.push_back(k);
  }
  for (double u : targets) {
    const int s = multiplicity(c, u);
    for (int times = 1; s + times <= degree; ++times) {
      const auto r = insert_knot(c, u, times);
      REQUIRE(r.has_value());
      CHECK(multiplicity(*r, u) == s + times);
      CHECK(r->points().size() == c.points().size() + static_cast<std::size_t>(times));
      CHECK(max_deviation(c, *r, 0.0, 1.0) <= kTol);
    }
  }
}

TEST_CASE("degree elevation (A5.9) leaves the curve unchanged", "[refine][property]") {
  const int degree = GENERATE(1, 2, 3, 4);
  const int by = GENERATE(1, 2, 3);
  const auto seed = GENERATE(take(10, random(1u, 1000000u)));
  const NurbsCurve3 c = random_curve(degree, seed);
  const auto e = elevate_degree(c, by);
  REQUIRE(e.has_value());
  CHECK(e->degree() == degree + by);
  // Continuity is kept: every interior knot gains exactly `by`.
  for (double k : c.knots().knots()) {
    if (k > 0.0 && k < 1.0) CHECK(multiplicity(*e, k) == multiplicity(c, k) + by);
  }
  CHECK(max_deviation(c, *e, 0.0, 1.0) <= kTol);
}

TEST_CASE("splitting gives two pieces that trace the curve", "[refine][property]") {
  const int degree = GENERATE(1, 2, 3, 5);
  const auto seed = GENERATE(take(10, random(1u, 1000000u)));
  const NurbsCurve3 c = random_curve(degree, seed);
  std::vector<double> at{0.37};
  for (double k : c.knots().knots()) {
    if (k > 0.0 && k < 1.0) at.push_back(k);  // splitting at an existing knot
  }
  for (double u : at) {
    const auto pieces = split(c, u);
    REQUIRE(pieces.has_value());
    const auto& [left, right] = *pieces;
    CHECK(left.domain_start() == 0.0);
    CHECK(left.domain_end() == u);
    CHECK(right.domain_start() == u);
    CHECK(right.domain_end() == 1.0);
    CHECK(max_deviation(c, left, 0.0, u) <= kTol);
    CHECK(max_deviation(c, right, u, 1.0) <= kTol);
    CHECK(distance(left.points().back(), right.points().front()) <= kTol);
    CHECK(distance(left.points().back(), c.point(u)) <= kTol);
  }
}

TEST_CASE("refinement keeps a circle exact", "[refine]") {
  const NurbsCurve2 circle = make_circle(Vec2{1.0, -2.0}, 3.0);
  const auto elevated = elevate_degree(circle, 2);
  REQUIRE(elevated.has_value());
  const auto inserted = insert_knot(*elevated, 0.3, 2);
  REQUIRE(inserted.has_value());
  const auto pieces = split(*inserted, 0.61);
  REQUIRE(pieces.has_value());
  for (const NurbsCurve2* c : {&*elevated, &*inserted, &pieces->first, &pieces->second}) {
    for (int i = 0; i <= 100; ++i) {
      const double u = c->domain_start() + (c->domain_end() - c->domain_start()) * i / 100.0;
      const Vec2 p = c->point(u);
      CHECK(std::abs(std::hypot(p.x - 1.0, p.y + 2.0) - 3.0) <= 1e-12 * 3.0);
    }
  }
}

TEST_CASE("refinement keeps non-rational curves non-rational", "[refine]") {
  const std::vector<Vec3> pts{{0, 0, 0}, {1, 2, 0}, {3, -1, 1}, {4, 0, 2}};
  const NurbsCurve3 c = make_bezier(pts);
  REQUIRE_FALSE(c.rational());
  for (int by = 1; by <= 6; ++by) {
    const auto e = elevate_degree(c, by);
    REQUIRE(e.has_value());
    CHECK_FALSE(e->rational());
    CHECK(std::ranges::all_of(e->weights(), [](double w) { return w == 1.0; }));
  }
  const auto line = elevate_degree(make_bezier(std::vector<Vec3>{{0, 0, 0}, {1, 1, 1}}), 5);
  CHECK_FALSE(line->rational());
  CHECK_FALSE(insert_knot(c, 0.3, 2)->rational());
  CHECK_FALSE(split(c, 0.6)->first.rational());
}

TEST_CASE("refinement rejects invalid requests", "[refine]") {
  const NurbsCurve3 c = random_curve(3, 7);
  CHECK_FALSE(insert_knot(c, 0.0).has_value());     // domain end
  CHECK_FALSE(insert_knot(c, 1.5).has_value());     // outside
  CHECK_FALSE(insert_knot(c, 0.5, 0).has_value());  // times < 1
  CHECK_FALSE(insert_knot(c, 0.5, 4).has_value());  // multiplicity > degree
  CHECK_FALSE(elevate_degree(c, 0).has_value());    // by < 1
  CHECK_FALSE(elevate_degree(c, 7).has_value());    // degree > kMaxDegree
  CHECK_FALSE(split(c, 1.0).has_value());           // domain end
  CHECK_FALSE(split(c, std::nan("")).has_value());  // not a number
  CHECK(elevate_degree(c, 6).has_value());
  CHECK_FALSE(elevate_degree(c, std::numeric_limits<int>::max()).has_value());  // no overflow
  const double existing = c.knots()[4];
  CHECK_FALSE(
      insert_knot(c, existing, std::numeric_limits<int>::max()).has_value());  // exactly kMaxDegree
}
