// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>
#include <vector>

#include "nf/math/arrangement.hpp"
#include "nf/math/constructors.hpp"
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

Arrangement arranged(const std::vector<std::vector<NurbsCurve2>>& paths) {
  auto r = arrange(paths);
  INFO((r ? "" : r.error()));
  REQUIRE(r.has_value());
  return std::move(*r);
}

// Bounded faces' winding numbers, sorted, and their areas' total.
std::vector<int> windings(const Arrangement& a) {
  std::vector<int> w;
  for (const auto& f : a.faces) {
    if (f.area > 1e-9) w.push_back(f.winding);
  }
  std::ranges::sort(w);
  return w;
}

// V − E + (traced cycles) = 2 · (connected components).
void check_euler(const Arrangement& a) {
  std::vector<int> parent(a.vertices.size());
  std::iota(parent.begin(), parent.end(), 0);
  const auto find = [&](int v) {
    while (parent[static_cast<std::size_t>(v)] != v) v = parent[static_cast<std::size_t>(v)];
    return v;
  };
  for (const auto& h : a.half_edges)
    parent[static_cast<std::size_t>(find(h.origin))] = find(h.target);
  int components = 0;
  for (std::size_t v = 0; v < parent.size(); ++v)
    components += find(static_cast<int>(v)) == static_cast<int>(v);
  const auto V = static_cast<int>(a.vertices.size()), E = static_cast<int>(a.half_edges.size() / 2);
  CHECK(V - E + static_cast<int>(a.faces.size()) == 2 * components);
}

}  // namespace

TEST_CASE("overlapping squares: three faces with windings 1, 1, 2", "[arrangement]") {
  const auto a = arranged({square(0, 0, 2), square(1, 1, 2)});
  CHECK(a.vertices.size() == 10);  // 8 corners + 2 crossings
  CHECK(windings(a) == std::vector<int>{1, 1, 2});
  check_euler(a);
  // Total bounded area = union area.
  double area = 0;
  for (const auto& f : a.faces) area += std::max(f.area, 0.0);
  CHECK(std::abs(area - 7.0) <= 1e-9);
  // Each half-edge's next starts where it ends, and faces lie on the left (positive area).
  for (const auto& h : a.half_edges)
    CHECK(a.half_edges[static_cast<std::size_t>(h.next)].origin == h.target);
}

TEST_CASE("orientation decides the winding of the overlap", "[arrangement]") {
  CHECK(windings(arranged({square(0, 0, 2), square(1, 1, 2, false)})) ==
        std::vector<int>{-1, 0, 1});
}

TEST_CASE("nested circles: winding adds up inside", "[arrangement]") {
  const NurbsCurve2 outer = make_circle(Vec2{0, 0}, 5.0), inner = make_circle(Vec2{1, 0}, 2.0);
  const auto same = arranged({{outer}, {inner}});
  CHECK(windings(same) == std::vector<int>{1, 2});
  check_euler(same);
  CHECK(windings(arranged({{outer}, {reverse(inner)}})) == std::vector<int>{0, 1});
}

TEST_CASE("self-crossing paths", "[arrangement]") {
  // A figure eight: two lobes turning opposite ways.
  const auto eight = arranged({polygon({{0, 0}, {2, 2}, {2, 0}, {0, 2}})});
  CHECK(windings(eight) == std::vector<int>{-1, 1});
  check_euler(eight);
  // A pentagram: five points of winding 1 around a centre of winding 2.
  std::vector<Vec2> star;
  for (int k = 0; k < 5; ++k) {
    const double t = std::numbers::pi / 2 + k * 4 * std::numbers::pi / 5;
    star.push_back({std::cos(t), std::sin(t)});
  }
  const auto pentagram = arranged({polygon(star)});
  CHECK(windings(pentagram) == std::vector<int>{1, 1, 1, 1, 1, 2});
  check_euler(pentagram);
}

TEST_CASE("open paths are closed; touching and separate parts", "[arrangement]") {
  // An open chain is closed with a line, as SVG fill does.
  const auto open =
      arranged({{make_line(Vec2{0, 0}, Vec2{4, 0}), make_line(Vec2{4, 0}, Vec2{4, 3})}});
  REQUIRE(windings(open) == std::vector<int>{1});
  // Squares touching at a corner, and a separate one.
  const auto parts = arranged({square(0, 0, 1), square(1, 1, 1), square(5, 5, 1)});
  CHECK(windings(parts) == std::vector<int>{1, 1, 1});
  check_euler(parts);
}

TEST_CASE("tangential contacts keep their faces apart", "[arrangement]") {
  // Circles touching at (1, 0), from outside and from inside (review finding).
  const auto outside = arranged({{make_circle(Vec2{0, 0}, 1.0)}, {make_circle(Vec2{2, 0}, 1.0)}});
  CHECK(windings(outside) == std::vector<int>{1, 1});
  check_euler(outside);
  const auto inside = arranged({{make_circle(Vec2{0, 0}, 1.0)}, {make_circle(Vec2{0.5, 0}, 0.5)}});
  CHECK(windings(inside) == std::vector<int>{1, 2});
  check_euler(inside);
  // A circle touching a square's side.
  CHECK(windings(arranged({square(-1, -3, 2), {make_circle(Vec2{0, 0}, 1.0)}})) ==
        std::vector<int>{1, 1});
}

TEST_CASE("small faces and stationary edges", "[arrangement]") {
  // A face far smaller than the drawing still gets its winding (review finding).
  const auto tiny = arranged({square(0, 0, 10), square(20, 20, 1e-7)});
  int small_faces = 0;
  for (const auto& f : tiny.faces) {
    if (f.area > 0 && f.area < 1e-12) {
      ++small_faces;
      CHECK(f.winding == 1);
    }
  }
  CHECK(small_faces == 1);
  // A side whose derivative vanishes at its midpoint (review finding).
  const auto stalled = make_bezier(std::vector<Vec2>{{0, 0}, {1, 0}, {0, 0}, {1, 0}});
  const auto r = arrange(std::vector<std::vector<NurbsCurve2>>{
      {stalled, make_line(Vec2{1, 0}, Vec2{1, 1}), make_line(Vec2{1, 1}, Vec2{0, 1}),
       make_line(Vec2{0, 1}, Vec2{0, 0})}});
  REQUIRE(r.has_value());
  CHECK(windings(*r) == std::vector<int>{1});
}

TEST_CASE("shared straight edges retain topology with cancelling winding",
          "[arrangement][shared]") {
  const auto a = arranged({square(0, 0, 1), square(1, 0, 1)});
  CHECK(a.vertices.size() == 6);
  CHECK(a.half_edges.size() == 14);
  CHECK(windings(a) == std::vector<int>{1, 1});
  CHECK(std::ranges::count(a.half_edges, 0, &Arrangement::HalfEdge::direction) == 2);
  check_euler(a);
}

TEST_CASE("partial shared straight edges split both boundaries", "[arrangement][shared]") {
  const auto a = arranged({square(0, 0, 4), square(4, 1, 2)});
  CHECK(a.vertices.size() == 8);
  CHECK(a.half_edges.size() == 18);
  CHECK(windings(a) == std::vector<int>{1, 1});
  check_euler(a);
  const auto overlap = arranged({square(0, 0, 2), square(1, 0, 2)});
  CHECK(windings(overlap) == std::vector<int>{1, 1, 2});
  CHECK(std::ranges::count(overlap.half_edges, 2, &Arrangement::HalfEdge::direction) == 2);
  check_euler(overlap);
}

TEST_CASE("shared straight duplicates add signed contributions", "[arrangement][shared]") {
  for (const bool ccw : {true, false}) {
    const auto a = arranged({square(0, 0, 2), square(0, 0, 2, ccw)});
    CHECK(a.half_edges.size() == 8);
    CHECK(windings(a) == std::vector<int>{ccw ? 2 : 0});
    for (const auto& h : a.half_edges) CHECK(std::abs(h.direction) == (ccw ? 2 : 0));
    check_euler(a);
  }
  const auto three = arranged({square(0, 0, 2), square(0, 0, 2), square(0, 0, 2, false)});
  CHECK(windings(three) == std::vector<int>{1});
  for (const auto& h : three.half_edges) CHECK(std::abs(h.direction) == 1);
  check_euler(three);
}

TEST_CASE("shared rational straight spans keep their original parameterization",
          "[arrangement][shared]") {
  auto first = square(0, 0, 4);
  const auto rational =
      NurbsCurve2::make(KnotVector::make({2, 2, 9, 9}, 1).value(), {{0, 0}, {4, 0}}, {1, 7})
          .value();
  first[0] = rational;
  const auto a = arranged({first, square(1, 0, 2)});
  CHECK(windings(a) == std::vector<int>{1, 2});
  check_euler(a);
  int spans = 0;
  for (std::size_t h = 0; h < a.half_edges.size(); h += 2) {
    const auto& c = a.half_edges[h].curve;
    if (!c.rational()) continue;
    ++spans;
    for (const double f : {0.0, 0.25, 0.5, 0.75, 1.0}) {
      const double u = std::lerp(c.domain_start(), c.domain_end(), f);
      CHECK(length(c.point(u) - rational.point(u)) <= 1e-12);
    }
  }
  CHECK(spans == 3);
}

TEST_CASE("shared straight boundaries include multi-span polylines", "[arrangement][shared]") {
  const auto poly = make_polyline(std::vector<Vec2>{{0, 0}, {2, 0}, {2, 2}, {0, 2}, {0, 0}});
  const auto a = arranged({{poly}, square(2, 0, 2)});
  CHECK(windings(a) == std::vector<int>{1, 1});
  CHECK(a.half_edges.size() == 14);
  check_euler(a);
}

TEST_CASE("shared straight seams split at crossings from a third path", "[arrangement][shared]") {
  const auto a = arranged({square(0, 0, 2), square(2, 0, 2), square(1, 1, 2)});
  CHECK(windings(a) == std::vector<int>{1, 1, 1, 2, 2});
  check_euler(a);
  for (const auto& h : a.half_edges) {
    CHECK(a.half_edges[static_cast<std::size_t>(h.next)].origin == h.target);
    CHECK(a.half_edges[static_cast<std::size_t>(h.twin)].direction == -h.direction);
  }
}

TEST_CASE("uncertified shared parameterizations remain explicit errors", "[arrangement][shared]") {
  auto path = square(0, 0, 2);
  path[0] = make_bezier(std::vector<Vec2>{{0, 0}, {0.1, 0}, {0.2, 0}, {2, 0}});
  const auto a = arrange(std::vector<std::vector<NurbsCurve2>>{square(0, 0, 2), path});
  REQUIRE_FALSE(a);
  CHECK(a.error().find("#74") != std::string::npos);
}
