// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
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

std::vector<Region> regions_of(const std::vector<std::vector<NurbsCurve2>>& paths, FillRule rule) {
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
        CHECK(length(loop->curves[i].points().back() - next.points().front()) <= 1e-12);
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

TEST_CASE("winding numbers of points", "[regions]") {
  const auto sq = square(0, 0, 2);
  CHECK(winding_number(sq, Vec2{1, 1}) == 1);
  CHECK(winding_number(sq, Vec2{3, 1}) == 0);
  CHECK(winding_number(square(0, 0, 2, false), Vec2{1, 1}) == -1);
  CHECK(regions(std::vector<std::vector<NurbsCurve2>>{}, FillRule::kNonZero)->empty());
}
