// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_adapters.hpp>
#include <catch2/generators/catch_generators_random.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numbers>
#include <stdexcept>
#include <vector>

#include "nf/math/constructors.hpp"

using namespace nf::math;
using Catch::Matchers::WithinAbs;
using std::numbers::pi;

namespace {

constexpr Tolerance kExact{.linear = 1e-12};

// Reference de Casteljau evaluation, independent of the NURBS machinery.
Vec3 de_casteljau(std::vector<Vec3> p, double t) {
  for (std::size_t r = 1; r < p.size(); ++r) {
    for (std::size_t i = 0; i + r < p.size(); ++i) p[i] = p[i] * (1 - t) + p[i + 1] * t;
  }
  return p.front();
}

}  // namespace

TEST_CASE("lines are exact", "[constructors]") {
  const NurbsCurve3 l = make_line(Vec3{1, 2, 3}, Vec3{5, 6, 7});
  CHECK(near(l.point(0.25), Vec3{2, 3, 4}, kExact));
  CHECK(near(l.point(1.0), Vec3{5, 6, 7}, kExact));
}

TEST_CASE("polylines pass through every vertex at its chord-length parameter", "[constructors]") {
  const std::vector<Vec2> pts{{0, 0}, {3, 0}, {3, 4}, {0, 4}};  // segment lengths 3, 4, 3
  const NurbsCurve2 c = make_polyline(pts);
  CHECK(near(c.point(0.0), pts[0], kExact));
  CHECK(near(c.point(0.3), pts[1], kExact));
  CHECK(near(c.point(0.7), pts[2], kExact));
  CHECK(near(c.point(1.0), pts[3], kExact));
  CHECK(near(c.point(0.5), Vec2{3, 2}, kExact));
  CHECK_THROWS_AS(make_polyline(std::vector<Vec2>{{1, 1}}), std::invalid_argument);
  CHECK_THROWS_AS(make_polyline(std::vector<Vec2>{{1, 1}, {1, 1}}), std::invalid_argument);
}

TEST_CASE("Bézier curves match de Casteljau", "[constructors][property]") {
  const std::vector<Vec3> p{{0, 0, 0}, {1, 3, 0}, {4, -2, 1}, {6, 1, 2}, {7, 0, -1}};
  const NurbsCurve3 c = make_bezier(p);
  CHECK(c.degree() == 4);
  const double t = GENERATE(take(50, random(0.0, 1.0)));
  CHECK(near(c.point(t), de_casteljau(p, t), kExact));
}

TEST_CASE("arcs and ellipses lie exactly on their conic", "[constructors][property]") {
  const double start = GENERATE(take(6, random(-pi, pi)));
  const double sweep = GENERATE(0.3, pi / 2, 2.0, pi, 4.5, 2 * pi);
  const double rx = 7.5, ry = 2.25;
  const Vec3 c{1, -2, 3}, x{0, 1, 0}, y{0, 0, 1};
  const NurbsCurve3 arc = make_arc(c, x, y, rx, ry, start, start + sweep);
  // Spans: one per ≤ 90°.
  CHECK(arc.points().size() ==
        static_cast<std::size_t>(2 * std::ceil(sweep / (pi / 2) - 1e-12) + 1));
  CHECK(near(arc.point(0.0), c + x * (rx * std::cos(start)) + y * (ry * std::sin(start)), kExact));
  CHECK(near(arc.point(1.0),
             c + x * (rx * std::cos(start + sweep)) + y * (ry * std::sin(start + sweep)), kExact));
  for (double u = 0.0; u <= 1.0; u += 1.0 / 64) {
    const Vec3 d = arc.point(u) - c;
    CHECK(std::abs(d.x) <= 1e-12);  // stays in the plane
    const double e = (d.y / rx) * (d.y / rx) + (d.z / ry) * (d.z / ry);
    CHECK_THAT(e, WithinAbs(1.0, 1e-13));
  }
}

TEST_CASE("full circles are closed, exact and planar", "[constructors][property]") {
  const Vec3 normal{GENERATE(take(5, random(-1.0, 1.0))), 0.4, 0.7};
  const Vec3 c{10, 20, 30};
  const NurbsCurve3 circle = make_circle(c, normal, 3.0);
  CHECK(circle.points().size() == 9);
  CHECK(near(circle.point(0.0), circle.point(1.0), kExact));
  const Vec3 n = *normalized(normal);
  for (double u = 0.0; u <= 1.0; u += 1.0 / 100) {
    const Vec3 d = circle.point(u) - c;
    CHECK_THAT(length(d), WithinAbs(3.0, 1e-12));
    CHECK_THAT(dot(d, n), WithinAbs(0.0, 1e-12));
  }
}

TEST_CASE("2D arcs honour the rotation of the ellipse axes", "[constructors]") {
  const NurbsCurve2 arc = make_arc(Vec2{0, 0}, 4.0, 1.0, pi / 2, 0.0, pi / 2);
  CHECK(near(arc.point(0.0), Vec2{0, 4}, kExact));   // +x axis rotated to +Y
  CHECK(near(arc.point(1.0), Vec2{-1, 0}, kExact));  // +y axis rotated to −X
  CHECK_THROWS_AS(make_arc(Vec2{}, 1, 1, 0, 1.0, 1.0), std::invalid_argument);
  CHECK_THROWS_AS(make_arc(Vec2{}, 1, 1, 0, 0.0, 7.0), std::invalid_argument);
}

TEST_CASE("polylines drop repeated vertices", "[constructors]") {
  const std::vector<Vec2> pts{{0, 0}, {0, 0}, {1, 0}, {1, 0}, {1, 1}, {1, 1}};
  const NurbsCurve2 c = make_polyline(pts);
  CHECK(c.points().size() == 3);
  CHECK(c.point(0.0) == Vec2{0, 0});
  CHECK(c.point(0.5) == Vec2{1, 0});
  CHECK(c.point(1.0) == Vec2{1, 1});
}

TEST_CASE("3D circles start on +X projected into the plane", "[constructors]") {
  for (const Vec3 normal : {Vec3{0.95, 0.1, 0.296}, Vec3{0.3, -0.5, 0.8}, Vec3{0.99, 0.0, 0.141}}) {
    const NurbsCurve3 c = make_circle(Vec3{0, 0, 0}, normal, 2.0);
    const Vec3 n = *normalized(normal);
    const Vec3 px = Vec3{1, 0, 0} - n * dot(Vec3{1, 0, 0}, n);
    const Vec3 expected = px * (2.0 / length(px));
    CHECK(length(c.point(c.domain_start()) - expected) < 1e-12);
  }
  // Normal along X: +X has no projection, so the circle starts on +Y.
  const NurbsCurve3 c = make_circle(Vec3{0, 0, 0}, Vec3{-1, 0, 0}, 1.0);
  CHECK(length(c.point(c.domain_start()) - Vec3{0, 1, 0}) < 1e-12);
}
