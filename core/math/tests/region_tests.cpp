// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>
#include <vector>

#include "nf/math/arrangement.hpp"
#include "nf/math/constructors.hpp"
#include "nf/math/query.hpp"
#include "nf/math/refine.hpp"

using namespace nf::math;

namespace {

std::vector<NurbsCurve2> polygon(std::vector<Vec2> pts) {
  std::vector<NurbsCurve2> out;
  for (std::size_t i = 0; i < pts.size(); ++i)
    out.push_back(make_line(pts[i], pts[(i + 1) % pts.size()]));
  return out;
}

std::vector<NurbsCurve2> square(double x, double y, double s, bool ccw = true) {
  std::vector<Vec2> pts{{x, y}, {x + s, y}, {x + s, y + s}, {x, y + s}};
  if (!ccw) std::ranges::reverse(pts);
  return polygon(pts);
}

double area(const Loop& loop) {
  double s = 0;
  for (const NurbsCurve2& c : loop.curves) {
    const auto pl = polyline(c, 1e-9);
    for (std::size_t i = 1; i < pl.size(); ++i) s += cross(pl[i - 1].point, pl[i].point);
  }
  return s / 2;
}

std::vector<Region> regions_of(const std::vector<std::vector<NurbsCurve2>>& paths, FillRule rule,
                               double closure_tolerance = 1e-12) {
  auto r = regions(paths, rule);
  INFO((r ? "" : r.error()));
  REQUIRE(r.has_value());
  // Every loop is closed, outers turn counter-clockwise and holes clockwise.
  for (const Region& g : *r) {
    for (const Loop* loop : [&] {
           std::vector<const Loop*> v{&g.outer};
           for (const Loop& h : g.holes) v.push_back(&h);
           return v;
         }()) {
      for (std::size_t i = 0; i < loop->curves.size(); ++i) {
        const auto& next = loop->curves[(i + 1) % loop->curves.size()];
        CHECK(length(loop->curves[i].points().back() - next.points().front()) <= closure_tolerance);
      }
    }
    CHECK(area(g.outer) > 0);
    for (const Loop& h : g.holes) CHECK(area(h) < 0);
  }
  return std::move(*r);
}

double total_area(const std::vector<Region>& rs) {
  double a = 0;
  for (const Region& g : rs) {
    a += area(g.outer);
    for (const Loop& h : g.holes) a += area(h);
  }
  return a;
}

}  // namespace

TEST_CASE("overlapping squares under both rules", "[regions]") {
  const auto nonzero = regions_of({square(0, 0, 2), square(1, 1, 2)}, FillRule::kNonZero);
  REQUIRE(nonzero.size() == 1);
  CHECK(nonzero[0].holes.empty());
  CHECK(nonzero[0].outer.curves.size() == 8);  // the union's outline
  CHECK(std::abs(total_area(nonzero) - 7.0) <= 1e-9);
  // Even-odd empties the overlap, which touches the outline at two points: two L-shapes.
  const auto evenodd = regions_of({square(0, 0, 2), square(1, 1, 2)}, FillRule::kEvenOdd);
  CHECK(evenodd.size() == 2);
  CHECK(std::abs(total_area(evenodd) - 6.0) <= 1e-9);
}

TEST_CASE("nested shapes: holes by rule and orientation", "[regions]") {
  const NurbsCurve2 outer = make_circle(Vec2{0, 0}, 5.0), inner = make_circle(Vec2{1, 0}, 2.0);
  const double annulus = std::numbers::pi * (25 - 4);
  // Same orientation: non-zero fills the inside too; even-odd makes a hole.
  const auto nz = regions_of({{outer}, {inner}}, FillRule::kNonZero);
  REQUIRE(nz.size() == 1);
  CHECK(nz[0].holes.empty());
  const auto eo = regions_of({{outer}, {inner}}, FillRule::kEvenOdd);
  REQUIRE(eo.size() == 1);
  REQUIRE(eo[0].holes.size() == 1);
  CHECK(std::abs(total_area(eo) - annulus) <= 1e-6);
  // Opposite orientation: a hole under both rules.
  for (const FillRule rule : {FillRule::kNonZero, FillRule::kEvenOdd}) {
    const auto r = regions_of({{outer}, {reverse(inner)}}, rule);
    REQUIRE(r.size() == 1);
    CHECK(r[0].holes.size() == 1);
  }
}

TEST_CASE("a filled island inside a hole is its own region", "[regions]") {
  const auto r =
      regions_of({square(0, 0, 10), square(2, 2, 6), square(4, 4, 2)}, FillRule::kEvenOdd);
  REQUIRE(r.size() == 2);
  const Region& big = area(r[0].outer) > area(r[1].outer) ? r[0] : r[1];
  const Region& island = &big == &r[0] ? r[1] : r[0];
  CHECK(big.holes.size() == 1);
  CHECK(island.holes.empty());
  CHECK(std::abs(area(island.outer) - 4.0) <= 1e-9);
  CHECK(std::abs(total_area(r) - (100 - 36 + 4)) <= 1e-9);
}

TEST_CASE("the pentagram under both rules", "[regions]") {
  std::vector<Vec2> star;
  for (int k = 0; k < 5; ++k) {
    const double t = std::numbers::pi / 2 + k * 4 * std::numbers::pi / 5;
    star.push_back({std::cos(t), std::sin(t)});
  }
  const auto nz = regions_of({polygon(star)}, FillRule::kNonZero);
  REQUIRE(nz.size() == 1);
  CHECK(nz[0].outer.curves.size() == 10);  // the star's outline
  // Even-odd empties the centre, which shares an edge with every point: five triangles.
  const auto eo = regions_of({polygon(star)}, FillRule::kEvenOdd);
  CHECK(eo.size() == 5);
  for (const Region& g : eo) CHECK(g.outer.curves.size() == 3);
}

TEST_CASE("regions of touching curves", "[regions]") {
  // Externally tangent circles: two regions touching at a point (review finding).
  const auto outside = regions_of({{make_circle(Vec2{0, 0}, 1.0)}, {make_circle(Vec2{2, 0}, 1.0)}},
                                  FillRule::kNonZero);
  CHECK(outside.size() == 2);
  // An internally tangent circle under even-odd: the crescent between them.
  const auto crescent = regions_of(
      {{make_circle(Vec2{0, 0}, 1.0)}, {make_circle(Vec2{0.5, 0}, 0.5)}}, FillRule::kEvenOdd);
  REQUIRE(crescent.size() == 1);
  CHECK(std::abs(total_area(crescent) - std::numbers::pi * (1 - 0.25)) <= 1e-6);
}

TEST_CASE("winding numbers of points", "[regions]") {
  const auto sq = square(0, 0, 2);
  // On the boundary it is undefined, on every side (review finding).
  for (const Vec2 p : {Vec2{0, 1}, Vec2{2, 1}, Vec2{1, 0}, Vec2{1, 2}, Vec2{0, 0}}) {
    CHECK_FALSE(winding_number(sq, p).has_value());
  }
  CHECK(winding_number(sq, Vec2{1, 1}) == 1);
  CHECK(winding_number(sq, Vec2{3, 1}) == 0);
  CHECK(winding_number(square(0, 0, 2, false), Vec2{1, 1}) == -1);
  CHECK(regions(std::vector<std::vector<NurbsCurve2>>{}, FillRule::kNonZero)->empty());
}

TEST_CASE("shared straight boundaries disappear inside filled regions", "[regions][shared]") {
  for (const auto rule : {FillRule::kNonZero, FillRule::kEvenOdd}) {
    auto r = regions_of({square(0, 0, 4), square(4, 1, 2)}, rule);
    REQUIRE(r.size() == 1);
    CHECK(r[0].holes.empty());
    CHECK(std::abs(total_area(r) - 20) <= 1e-10);
    r = regions_of({square(0, 0, 2), square(1, 0, 2)}, rule);
    CHECK(r.size() == (rule == FillRule::kNonZero ? 1 : 2));
    CHECK(std::abs(total_area(r) - (rule == FillRule::kNonZero ? 6 : 4)) <= 1e-10);
  }
}

TEST_CASE("duplicate straight outlines obey both fill rules", "[regions][shared]") {
  for (const bool ccw : {true, false}) {
    const auto paths = std::vector<std::vector<NurbsCurve2>>{square(0, 0, 2), square(0, 0, 2, ccw)};
    const auto nz = regions_of(paths, FillRule::kNonZero);
    CHECK(nz.size() == (ccw ? 1 : 0));
    CHECK(std::abs(total_area(nz) - (ccw ? 4 : 0)) <= 1e-10);
    CHECK(regions_of(paths, FillRule::kEvenOdd).empty());
  }
}

TEST_CASE("a straight hole sharing the outline becomes a notch", "[regions][shared]") {
  for (const auto rule : {FillRule::kNonZero, FillRule::kEvenOdd}) {
    const auto r = regions_of({square(0, 0, 4), square(1, 0, 2, false)}, rule);
    REQUIRE(r.size() == 1);
    CHECK(r[0].holes.empty());
    CHECK(std::abs(total_area(r) - 12) <= 1e-10);
  }
}

TEST_CASE("shared straight profiles are invariant to scale rotation and order",
          "[regions][shared]") {
  for (const double scale : {1e-4, 1.0, 1e4}) {
    for (const double angle : {0.0, 0.73, 2.2}) {
      std::vector<std::vector<NurbsCurve2>> paths{square(0, 0, 4), square(4, 1, 2)};
      const auto moved = [&](Vec2 p) {
        return Vec2{(p.x * std::cos(angle) - p.y * std::sin(angle) + 13) * scale,
                    (p.x * std::sin(angle) + p.y * std::cos(angle) - 7) * scale};
      };
      for (auto& path : paths)
        for (auto& c : path) c = make_line(moved(c.points().front()), moved(c.points().back()));
      for (int order = 0; order < 2; ++order) {
        const auto r = regions_of(paths, FillRule::kNonZero, 1e-12 * std::max(scale, 1.0));
        REQUIRE(r.size() == 1);
        CHECK(std::abs(total_area(r) / (scale * scale) - 20) <= 1e-9);
        std::ranges::reverse(paths);
      }
    }
  }
}

TEST_CASE("parallel straight boundaries with a real gap stay separate", "[regions][shared]") {
  for (const double gap : {1e-5, 1e-7}) {
    const auto r = regions_of({square(0, 0, 1), square(1 + gap, 0, 1)}, FillRule::kNonZero);
    REQUIRE(r.size() == 2);
    CHECK(std::abs(total_area(r) - 2) <= 1e-10);
  }
}

TEST_CASE("shared straight rectangle areas match analytic unions and differences",
          "[regions][shared][property]") {
  std::mt19937_64 rng(20261018);
  std::uniform_real_distribution<double> size(0.5, 20), offset(-5, 25);
  for (int trial = 0; trial < 128; ++trial) {
    const double w1 = size(rng), h1 = size(rng), w2 = size(rng), h2 = size(rng), x = offset(rng);
    CAPTURE(trial, w1, h1, w2, h2, x);
    const double overlap =
        std::max(0.0, std::min(w1, x + w2) - std::max(0.0, x)) * std::min(h1, h2);
    const auto first = polygon({{0, 0}, {w1, 0}, {w1, h1}, {0, h1}});
    const auto second = polygon({{x, 0}, {x + w2, 0}, {x + w2, h2}, {x, h2}});
    const auto backward = polygon({{x, h2}, {x + w2, h2}, {x + w2, 0}, {x, 0}});
    const auto united = regions_of({first, second}, FillRule::kNonZero);
    const auto parity = regions_of({first, second}, FillRule::kEvenOdd);
    const auto cancelled = regions_of({first, backward}, FillRule::kNonZero);
    CHECK(std::abs(total_area(united) - (w1 * h1 + w2 * h2 - overlap)) <= 1e-8);
    CHECK(std::abs(total_area(parity) - (w1 * h1 + w2 * h2 - 2 * overlap)) <= 1e-8);
    CHECK(std::abs(total_area(cancelled) - (w1 * h1 + w2 * h2 - 2 * overlap)) <= 1e-8);
  }
}
