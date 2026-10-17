// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_adapters.hpp>
#include <catch2/generators/catch_generators_random.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <numbers>
#include <random>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "nf/math/constructors.hpp"
#include "nf/math/fit.hpp"
#include "nf/math/query.hpp"

using namespace nf::math;

namespace {

template <typename Point>
NurbsCurve<Point> random_curve(int degree, std::uint32_t seed);

template <>
NurbsCurve<Vec2> random_curve(int degree, std::uint32_t seed) {
  std::mt19937 rng(seed);
  std::uniform_real_distribution<double> coord(-10.0, 10.0), weight(0.3, 3.0);
  const KnotVector kv = KnotVector::uniform_clamped(degree, degree + 5);
  std::vector<Vec2> pts;
  std::vector<double> w;
  for (int i = 0; i < kv.control_point_count(); ++i) {
    pts.push_back({coord(rng), coord(rng)});
    w.push_back(weight(rng));
  }
  return NurbsCurve2::make(kv, pts, w).value();
}

template <>
NurbsCurve<Vec3> random_curve(int degree, std::uint32_t seed) {
  std::mt19937 rng(seed);
  std::uniform_real_distribution<double> coord(-10.0, 10.0), weight(0.3, 3.0);
  const KnotVector kv = KnotVector::uniform_clamped(degree, degree + 5);
  std::vector<Vec3> pts;
  std::vector<double> w;
  for (int i = 0; i < kv.control_point_count(); ++i) {
    pts.push_back({coord(rng), coord(rng), coord(rng)});
    w.push_back(weight(rng));
  }
  return NurbsCurve3::make(kv, pts, w).value();
}

// Brute force: dense samples, then golden-section refinement around the best.
template <typename Point>
double brute_distance(const NurbsCurve<Point>& c, Point p) {
  constexpr int n = 20000;
  int best = 0;
  double bd = INFINITY;
  for (int i = 0; i <= n; ++i) {
    const double d = length(p - c.point(static_cast<double>(i) / n));
    if (d < bd) bd = d, best = i;
  }
  double lo = std::max(0, best - 1) / static_cast<double>(n),
         hi = std::min(n, best + 1) / static_cast<double>(n);
  for (int it = 0; it < 200; ++it) {
    const double a = lo + (hi - lo) / 3, b = hi - (hi - lo) / 3;
    if (length(p - c.point(a)) < length(p - c.point(b))) {
      hi = b;
    } else {
      lo = a;
    }
  }
  return std::min(bd, length(p - c.point((lo + hi) / 2)));
}

template <typename Point>
double segment_distance(Point p, Point a, Point b) {
  const Point ab = b - a;
  const double t = std::clamp(dot(p - a, ab) / std::max(dot(ab, ab), 1e-300), 0.0, 1.0);
  return length(p - (a + ab * t));
}

}  // namespace

TEMPLATE_TEST_CASE("closest point is global", "[query][property]", Vec2, Vec3) {
  const int degree = GENERATE(1, 2, 3, 5);
  const auto seed = GENERATE(take(5, random(1u, 1000000u)));
  const auto c = random_curve<TestType>(degree, seed);
  std::mt19937 rng(seed);
  std::uniform_real_distribution<double> coord(-15.0, 15.0);
  for (int k = 0; k < 10; ++k) {
    TestType p{};
    if constexpr (std::is_same_v<TestType, Vec2>) {
      p = {coord(rng), coord(rng)};
    } else {
      p = {coord(rng), coord(rng), coord(rng)};
    }
    INFO("degree " << degree << " seed " << seed << " k " << k);
    const Closest<TestType> r = closest_point(c, p);
    CHECK(length(r.point - c.point(r.u)) <= 1e-12 * 20);
    CHECK(std::abs(r.distance - length(p - r.point)) <= 1e-12 * 20);
    const double brute = brute_distance(c, p);
    CHECK(r.distance <= brute + 1e-9 * 20);  // never worse than dense sampling
    CHECK(r.distance >= brute - 1e-6);       // and sampling cannot be beaten by more than that
  }
}

TEST_CASE("closest point special cases", "[query]") {
  const NurbsCurve2 circle = make_circle(Vec2{1, 2}, 5.0);
  // Every point is nearest to the centre: any answer, at the right distance, quickly.
  const auto t0 = std::chrono::steady_clock::now();
  const auto r = closest_point(circle, Vec2{1, 2});
  CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(2));
  CHECK(std::abs(r.distance - 5.0) <= 1e-9);
  // A point on the curve.
  const Vec2 on = circle.point(1.234);
  CHECK(closest_point(circle, on).distance <= 1e-12);
  // Nearest to an end, not an interior point.
  const NurbsCurve2 line = make_line(Vec2{0, 0}, Vec2{1, 0});
  const auto e = closest_point(line, Vec2{3, 1});
  CHECK(e.u == 1.0);
  CHECK(e.point == Vec2{1, 0});
}

TEST_CASE("closest point where the curve starts slowly", "[query]") {
  // An involute fitted with its own parameter starts almost at rest (a cusp at t = 0). Just
  // off it there, plain Newton headed for a maximum and stalled 1000× too far away; the
  // safeguarded one finds the foot of the perpendicular.
  std::vector<Vec2> pts;
  std::vector<double> us;
  const double end = 3 * std::numbers::pi;
  const auto f = [](double t) {
    return Vec2{std::cos(t) + t * std::sin(t), std::sin(t) - t * std::cos(t)};
  };
  for (int i = 0; i <= 256; ++i) {
    pts.push_back(f(end * i / 256));
    us.push_back(i / 256.0);
  }
  const auto fit = approximate<Vec2>(pts, 5e-5, 3, us);
  REQUIRE(fit.has_value());
  for (const double t : {0.03, 0.0942, 0.15, 0.3}) {
    const Vec2 q = f(t);
    double brute = INFINITY;
    for (int i = 0; i <= 100000; ++i)
      brute = std::min(brute, length(fit->curve.point(0.05 * i / 100000) - q));
    CHECK(closest_point(fit->curve, q).distance <= brute + 1e-12);
  }
}

TEST_CASE("queries near the ends and at extreme scales", "[query]") {
  // Within a hair of an end, the exact point still wins (review finding).
  const NurbsCurve2 unit = make_line(Vec2{0, 0}, Vec2{1, 0});
  CHECK(closest_point(unit, Vec2{5e-10, 0}).distance == 0.0);
  // Coordinates near the range of double (review finding).
  const auto huge = make_bezier(std::vector<Vec2>{{-1e308, 0}, {0, 1e308}, {1e308, 0}});
  const auto pl = polyline(huge, 1.0);
  CHECK(pl.size() > 1000);  // not just the two ends
  const auto c = closest_point(huge, Vec2{0, 0});
  CHECK(c.distance < 0.6e308);  // the arch's top (5e307 away), not an end (1e308)
}

TEMPLATE_TEST_CASE("polylines stay within the tolerance", "[query][property]", Vec2, Vec3) {
  const int degree = GENERATE(1, 2, 3, 5);
  const auto seed = GENERATE(take(5, random(1u, 1000000u)));
  const double tol = GENERATE(1e-1, 1e-3, 1e-6);
  const auto c = random_curve<TestType>(degree, seed);
  const auto pl = polyline(c, tol);
  REQUIRE(pl.size() >= 2);
  CHECK(pl.front().u == c.domain_start());
  CHECK(pl.back().u == c.domain_end());
  for (const double k : c.knots().knots()) {  // every knot is a vertex
    CHECK(std::ranges::any_of(pl, [&](const auto& v) { return v.u == k; }));
  }
  for (std::size_t i = 1; i < pl.size(); ++i) {
    REQUIRE(pl[i].u > pl[i - 1].u);
    for (int s = 1; s < 8; ++s) {
      const double u = pl[i - 1].u + (pl[i].u - pl[i - 1].u) * s / 8.0;
      CHECK(segment_distance(c.point(u), pl[i - 1].point, pl[i].point) <= tol * (1 + 1e-9));
    }
  }
}

TEST_CASE("polyline vertex count follows the tolerance", "[query]") {
  const NurbsCurve2 circle = make_circle(Vec2{0, 0}, 10.0);
  const auto coarse = polyline(circle, 1e-2), fine = polyline(circle, 1e-4);
  CHECK(fine.size() > coarse.size() * 5);   // sagitta ∝ 1/n², so ~10× more
  CHECK(fine.size() < coarse.size() * 20);  // but not wildly more (a bound, not an oracle)
  CHECK_THROWS_AS(polyline(circle, 0.0), std::invalid_argument);
  CHECK_THROWS_AS(polyline(circle, -1.0), std::invalid_argument);
  CHECK_THROWS_AS(polyline(circle, std::nan("")), std::invalid_argument);
}
