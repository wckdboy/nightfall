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
#include "nf/math/nurbs_surface.hpp"

using namespace nf::math;

namespace {

NurbsSurface random_surface(std::uint32_t seed, int p, int q) {
  std::mt19937 rng(seed);
  std::uniform_real_distribution<double> coord(-5.0, 5.0), weight(0.4, 2.5);
  const KnotVector ku = KnotVector::uniform_clamped(p, p + 3),
                   kv = KnotVector::uniform_clamped(q, q + 2);
  std::vector<Vec3> pts;
  std::vector<double> ws;
  for (int i = 0; i < ku.control_point_count() * kv.control_point_count(); ++i) {
    pts.push_back({coord(rng), coord(rng), coord(rng)});
    ws.push_back(weight(rng));
  }
  return NurbsSurface::make(ku, kv, pts, ws).value();
}

}  // namespace

TEST_CASE("a bilinear patch interpolates its corners bilinearly", "[surface]") {
  const KnotVector k = KnotVector::make({0, 0, 1, 1}, 1).value();
  const auto s = NurbsSurface::make(k, k, {{0, 0, 0}, {0, 1, 1}, {1, 0, 2}, {1, 1, 0}}).value();
  // P(i, j) = points[i·2 + j]: u runs (0,0,0)→(1,0,2), v runs (0,0,0)→(0,1,1).
  const Vec3 p = s.point(0.25, 0.5);
  const Vec3 expected = Vec3{0, 0, 0} * (0.75 * 0.5) + Vec3{0, 1, 1} * (0.75 * 0.5) +
                        Vec3{1, 0, 2} * (0.25 * 0.5) + Vec3{1, 1, 0} * (0.25 * 0.5);
  CHECK(length(p - expected) <= 1e-15);
  CHECK_FALSE(NurbsSurface::make(k, k, {{0, 0, 0}}).has_value());
  CHECK_FALSE(NurbsSurface::make(k, k, {{0, 0, 0}, {0, 1, 1}, {1, 0, 2}, {1, 1, 0}}, {1, 1, 1, -1})
                  .has_value());
}

TEST_CASE("surface derivatives match central differences", "[surface][property]") {
  const int p = GENERATE(1, 2, 3);
  const int q = GENERATE(2, 3);
  const auto seed = GENERATE(take(4, random(1u, 1000000u)));
  const NurbsSurface s = random_surface(seed, p, q);
  std::mt19937 rng(seed);
  std::uniform_real_distribution<double> param(0.05, 0.95);
  for (int t = 0; t < 10; ++t) {
    const double u = param(rng), v = param(rng);
    const auto d = s.derivatives(u, v, 3);
    CHECK(length(d.at(0, 0) - s.point(u, v)) <= 1e-12);
    constexpr double h = 1e-5;
    for (int k = 0; k <= 2; ++k) {
      for (int l = 0; l + k <= 2; ++l) {
        const Vec3 du =
            (s.derivatives(u + h, v, 3).at(k, l) - s.derivatives(u - h, v, 3).at(k, l)) / (2 * h);
        const Vec3 dv =
            (s.derivatives(u, v + h, 3).at(k, l) - s.derivatives(u, v - h, 3).at(k, l)) / (2 * h);
        const bool same_span_u = s.knots_u().find_span(u + h) == s.knots_u().find_span(u - h);
        const bool same_span_v = s.knots_v().find_span(v + h) == s.knots_v().find_span(v - h);
        if (same_span_u) CHECK(length(du - d.at(k + 1, l)) <= 1e-4 * (1 + length(du)));
        if (same_span_v) CHECK(length(dv - d.at(k, l + 1)) <= 1e-4 * (1 + length(dv)));
      }
    }
  }
}

TEST_CASE("isocurves are exactly the surface", "[surface][property]") {
  const auto seed = GENERATE(take(5, random(1u, 1000000u)));
  const NurbsSurface s = random_surface(seed, 3, 2);
  for (const double fixed : {0.0, 0.3, 0.77, 1.0}) {
    const NurbsCurve3 cu = s.iso_u(fixed), cv = s.iso_v(fixed);
    for (int i = 0; i <= 20; ++i) {
      const double t = i / 20.0;
      CHECK(length(cu.point(t) - s.point(fixed, t)) <= 1e-12 * 10);
      CHECK(length(cv.point(t) - s.point(t, fixed)) <= 1e-12 * 10);
    }
  }
}

TEST_CASE("an extruded circle is a cylinder", "[surface]") {
  const NurbsCurve3 circle = make_circle(Vec3{1, 2, 0}, Vec3{0, 0, 1}, 3.0);
  const NurbsSurface s = extrude(circle, Vec3{0, 0, 5});
  for (int i = 0; i <= 16; ++i) {
    for (int j = 0; j <= 4; ++j) {
      const double u = i / 16.0, v = j / 4.0;
      const Vec3 p = s.point(u, v);
      CHECK(std::abs(std::hypot(p.x - 1, p.y - 2) - 3.0) <= 1e-12 * 5);
      CHECK(std::abs(p.z - 5 * v) <= 1e-12 * 5);
      const auto n = s.normal(u, v);
      REQUIRE(n.has_value());
      CHECK(std::abs(n->z) <= 1e-12);  // radial
    }
  }
  CHECK(length(s.iso_v(0.5).point(0.3) - (circle.point(0.3) + Vec3{0, 0, 2.5})) <= 1e-12 * 5);
}

TEST_CASE("a revolved semicircle is a sphere, poles included", "[surface]") {
  const double r = 2.0;
  // Semicircle in the xz-plane from the south pole to the north pole.
  const NurbsCurve3 profile =
      make_arc(Vec3{0, 0, 0}, Vec3{0, 0, -1}, Vec3{1, 0, 0}, r, r, 0.0, std::numbers::pi);
  const double angle = GENERATE(2 * std::numbers::pi, std::numbers::pi / 2, 3.5);
  const NurbsSurface s = revolve(profile, Vec3{0, 0, 0}, Vec3{0, 0, 1}, angle);
  for (int i = 0; i <= 12; ++i) {
    for (int j = 0; j <= 12; ++j) {
      const double u = s.knots_u().domain_start() +
                       (s.knots_u().domain_end() - s.knots_u().domain_start()) * i / 12.0;
      const double v = s.knots_v().domain_start() +
                       (s.knots_v().domain_end() - s.knots_v().domain_start()) * j / 12.0;
      const Vec3 p = s.point(u, v);
      CHECK(std::abs(length(p) - r) <= 1e-12 * r);
      const auto n = s.normal(u, v);
      REQUIRE(n.has_value());
      CHECK(std::abs(std::abs(dot(*n, p / length(p))) - 1.0) <= 1e-6);  // radial, poles too
    }
  }
  // The sweep covers the requested angle.
  const Vec3 end = s.point(
      s.knots_u().domain_start() + 0.5 * (s.knots_u().domain_end() - s.knots_u().domain_start()),
      s.knots_v().domain_end());
  CHECK(std::abs(std::atan2(end.y, end.x) - std::remainder(angle, 2 * std::numbers::pi)) <= 1e-12);
}

TEST_CASE("revolving a line parallel to the axis gives a cylinder", "[surface]") {
  const NurbsSurface s = revolve(make_line(Vec3{3, 0, 0}, Vec3{3, 0, 4}), Vec3{0, 0, 0},
                                 Vec3{0, 0, 2}, 2 * std::numbers::pi);
  for (int i = 0; i <= 10; ++i) {
    const Vec3 p = s.point(0.4, s.knots_v().domain_end() * i / 10.0);
    CHECK(std::abs(std::hypot(p.x, p.y) - 3.0) <= 1e-12 * 3);
    CHECK(std::abs(p.z - 1.6) <= 1e-12);
  }
  CHECK_THROWS(revolve(make_line(Vec3{3, 0, 0}, Vec3{3, 0, 4}), Vec3{0, 0, 0}, Vec3{0, 0, 0}, 1.0));
  CHECK_THROWS(revolve(make_line(Vec3{3, 0, 0}, Vec3{3, 0, 4}), Vec3{0, 0, 0}, Vec3{0, 0, 1}, 7.0));
}
