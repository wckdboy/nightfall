// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_adapters.hpp>
#include <catch2/generators/catch_generators_random.hpp>

#include <cmath>
#include <random>
#include <stdexcept>
#include <vector>

#include "nf/math/constructors.hpp"
#include "nf/math/intersect.hpp"
#include "nf/math/query.hpp"

using namespace nf::math;

namespace {

NurbsCurve2 random_cubic(std::mt19937& rng) {
  std::uniform_real_distribution<double> coord(-10.0, 10.0), weight(0.5, 2.0);
  const KnotVector kv = KnotVector::uniform_clamped(3, 6);
  std::vector<Vec2> pts;
  std::vector<double> w;
  for (int i = 0; i < 6; ++i) {
    pts.push_back({coord(rng), coord(rng)});
    w.push_back(weight(rng));
  }
  return NurbsCurve2::make(kv, pts, w).value();
}

// Independent reference: proper crossings between fine polylines of the two curves.
int polyline_crossings(const NurbsCurve2& a, const NurbsCurve2& b) {
  const auto pa = polyline(a, 1e-5), pb = polyline(b, 1e-5);
  int n = 0;
  for (std::size_t i = 1; i < pa.size(); ++i) {
    for (std::size_t j = 1; j < pb.size(); ++j) {
      const Vec2 p = pa[i - 1].point, r = pa[i].point - p, q = pb[j - 1].point, s = pb[j].point - q;
      const double den = cross(r, s);
      if (den == 0) continue;
      const double t = cross(q - p, s) / den, u = cross(q - p, r) / den;
      if (t >= 0 && t < 1 && u >= 0 && u < 1) ++n;
    }
  }
  return n;
}

}  // namespace

TEST_CASE("a line meets a circle at 2, 1 or 0 points", "[intersect]") {
  const NurbsCurve2 circle = make_circle(Vec2{1, 2}, 5.0);
  const auto secant = intersect(circle, make_line(Vec2{-10, 3}, Vec2{10, 3}));
  REQUIRE(secant.hits.size() == 2);
  CHECK_FALSE(secant.overlap);
  for (const auto& h : secant.hits) {
    CHECK(std::abs(length(h.point - Vec2{1, 2}) - 5.0) <= 1e-12 * 10);
    CHECK(std::abs(h.point.y - 3.0) <= 1e-12 * 10);
    CHECK(std::abs(std::abs(h.point.x - 1.0) - std::sqrt(24.0)) <= 1e-11);
  }
  const auto tangent = intersect(circle, make_line(Vec2{-10, 7}, Vec2{10, 7}));
  REQUIRE(tangent.hits.size() == 1);
  CHECK(length(tangent.hits[0].point - Vec2{1, 7}) <= 1e-6);
  CHECK(intersect(circle, make_line(Vec2{-10, 7.01}, Vec2{10, 7.01})).hits.empty());
}

TEST_CASE("two circles cross where the analytic solution says", "[intersect]") {
  const NurbsCurve2 a = make_circle(Vec2{0, 0}, 5.0), b = make_circle(Vec2{6, 1}, 4.0);
  const auto r = intersect(a, b);
  REQUIRE(r.hits.size() == 2);
  // Radical line: x·6 + y·1 = (25 − 16 + 37) / 2 = 23.
  for (const auto& h : r.hits) {
    CHECK(std::abs(length(h.point) - 5.0) <= 1e-11);
    CHECK(std::abs(length(h.point - Vec2{6, 1}) - 4.0) <= 1e-11);
    CHECK(std::abs(6 * h.point.x + h.point.y - 23.0) <= 1e-10);
    CHECK(length(a.point(h.ua) - b.point(h.ub)) <= 1e-11);
  }
  // Externally tangent circles touch once.
  const auto t = intersect(a, make_circle(Vec2{9, 0}, 4.0));
  REQUIRE(t.hits.size() == 1);
  CHECK(length(t.hits[0].point - Vec2{5, 0}) <= 1e-6);
}

TEST_CASE("random rational cubics: every crossing found, each once", "[intersect][property]") {
  const auto seed = GENERATE(take(40, random(1u, 1000000u)));
  std::mt19937 rng(seed);
  const NurbsCurve2 a = random_cubic(rng), b = random_cubic(rng);
  const auto r = intersect(a, b);
  CHECK_FALSE(r.overlap);
  for (const auto& h : r.hits) CHECK(length(a.point(h.ua) - b.point(h.ub)) <= 1e-8 * 30);
  CHECK(static_cast<int>(r.hits.size()) == polyline_crossings(a, b));
  for (std::size_t i = 1; i < r.hits.size(); ++i) CHECK(r.hits[i].ua >= r.hits[i - 1].ua);
}

TEST_CASE("coincident curves are reported as overlapping", "[intersect]") {
  CHECK(intersect(make_line(Vec2{0, 0}, Vec2{10, 0}), make_line(Vec2{5, 0}, Vec2{20, 0})).overlap);
  const NurbsCurve2 c = make_circle(Vec2{1, 1}, 3.0);
  CHECK(intersect(c, c).overlap);
  // Touching at an end is not an overlap.
  const auto end = intersect(make_line(Vec2{0, 0}, Vec2{1, 0}), make_line(Vec2{1, 0}, Vec2{1, 5}));
  CHECK_FALSE(end.overlap);
  REQUIRE(end.hits.size() == 1);
  CHECK(end.hits[0].point == Vec2{1, 0});
}

TEST_CASE("curve–line intersections", "[intersect]") {
  const NurbsCurve2 circle = make_circle(Vec2{0, 0}, 2.0);
  const auto r = intersect_line(circle, Vec2{-1, -1}, Vec2{1, 1});  // the line y = x
  REQUIRE(r.hits.size() == 2);
  for (const auto& h : r.hits) {
    CHECK(std::abs(h.point.x - h.point.y) <= 1e-12 * 4);
    CHECK(std::abs(std::abs(h.point.x) - std::sqrt(2.0)) <= 1e-12 * 4);
  }
  CHECK(intersect_line(circle, Vec2{0, 2}, Vec2{1, 0}).hits.size() == 1);  // tangent
  CHECK(intersect_line(circle, Vec2{0, 3}, Vec2{1, 0}).hits.empty());
  CHECK(intersect_line(make_line(Vec2{0, 1}, Vec2{5, 1}), Vec2{0, 1}, Vec2{2, 0}).overlap);
  CHECK_THROWS_AS(intersect_line(circle, Vec2{0, 0}, Vec2{0, 0}), std::invalid_argument);
  // Random cubics against random lines, against the polyline reference.
  std::mt19937 rng(7);
  std::uniform_real_distribution<double> coord(-10.0, 10.0);
  for (int i = 0; i < 40; ++i) {
    const NurbsCurve2 c = random_cubic(rng);
    const Vec2 o{coord(rng), coord(rng)}, d{coord(rng), coord(rng)};
    const auto hits = intersect_line(c, o, d);
    const NurbsCurve2 long_line = make_line(o - d * 100.0, o + d * 100.0);
    CHECK(static_cast<int>(hits.hits.size()) == polyline_crossings(c, long_line));
    for (const auto& h : hits.hits) CHECK(std::abs(cross(d, h.point - o)) / length(d) <= 1e-8 * 30);
  }
}
