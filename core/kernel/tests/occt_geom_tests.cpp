// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_adapters.hpp>
#include <catch2/generators/catch_generators_random.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>
#include <type_traits>
#include <vector>

#include <GeomConvert.hxx>
#include <Geom_Circle.hxx>
#include <gp_Ax2.hxx>

#include "nf/kernel/occt_geom.hpp"
#include "nf/math/constructors.hpp"

using namespace nf;
using math::KnotVector;
using math::Vec2;
using math::Vec3;

namespace {

template <typename Point>
math::NurbsCurve<Point> random_curve(std::uint32_t seed, int degree, bool rational) {
  std::mt19937 rng(seed);
  std::uniform_real_distribution<double> c(-10.0, 10.0), w(0.3, 3.0), k(0.05, 0.95);
  std::vector<double> knots(static_cast<std::size_t>(degree + 1), 0.0);
  std::vector<double> interior{k(rng), k(rng), k(rng)};
  if (degree >= 2) interior.push_back(interior[1]);  // a double knot
  std::ranges::sort(interior);
  knots.insert(knots.end(), interior.begin(), interior.end());
  knots.insert(knots.end(), static_cast<std::size_t>(degree + 1), 1.0);
  auto kv = KnotVector::make(knots, degree).value();
  std::vector<Point> pts;
  std::vector<double> ws;
  for (int i = 0; i < kv.control_point_count(); ++i) {
    if constexpr (std::is_same_v<Point, Vec2>) {
      pts.push_back({c(rng), c(rng)});
    } else {
      pts.push_back({c(rng), c(rng), c(rng)});
    }
    if (rational) ws.push_back(w(rng));
  }
  return math::NurbsCurve<Point>::make(kv, pts, ws).value();
}

}  // namespace

TEST_CASE("3D curves round-trip exactly and evaluate alike in OCCT", "[kernel][occt]") {
  const int degree = GENERATE(1, 2, 3, 5);
  const bool rational = GENERATE(false, true);
  const auto seed = GENERATE(take(4, random(1u, 1000000u)));
  const auto c = random_curve<Vec3>(seed, degree, rational);
  const Handle(Geom_BSplineCurve) occ = kernel::to_occt(c).value();
  CHECK(occ->Degree() == degree);
  CHECK(occ->IsRational() == rational);
  const auto back = kernel::from_occt(*occ);
  REQUIRE(back.has_value());
  CHECK(std::ranges::equal(back->knots().knots(), c.knots().knots()));
  CHECK(std::ranges::equal(back->points(), c.points()));
  CHECK(std::ranges::equal(back->weights(), c.weights()));
  for (int i = 0; i <= 50; ++i) {
    const double u = i / 50.0;
    const gp_Pnt p = occ->Value(u);
    CHECK(length(Vec3{p.X(), p.Y(), p.Z()} - c.point(u)) <= 1e-12 * 20);
  }
}

TEST_CASE("2D curves round-trip exactly", "[kernel][occt]") {
  const auto seed = GENERATE(take(4, random(1u, 1000000u)));
  const auto c = random_curve<Vec2>(seed, 3, true);
  const Handle(Geom2d_BSplineCurve) occ = kernel::to_occt(c).value();
  const auto back = kernel::from_occt(*occ);
  REQUIRE(back.has_value());
  CHECK(std::ranges::equal(back->knots().knots(), c.knots().knots()));
  CHECK(std::ranges::equal(back->points(), c.points()));
  CHECK(std::ranges::equal(back->weights(), c.weights()));
  const gp_Pnt2d p = occ->Value(0.42);
  CHECK(length(Vec2{p.X(), p.Y()} - c.point(0.42)) <= 1e-12 * 20);
}

TEST_CASE("surfaces round-trip exactly and evaluate alike in OCCT", "[kernel][occt]") {
  const math::NurbsCurve3 profile =
      math::make_arc(Vec3{0, 0, 0}, Vec3{0, 0, -1}, Vec3{1, 0, 0}, 2, 2, 0, std::numbers::pi);
  const math::NurbsSurface sphere =
      math::revolve(profile, Vec3{0, 0, 0}, Vec3{0, 0, 1}, 2 * std::numbers::pi);
  const Handle(Geom_BSplineSurface) occ = kernel::to_occt(sphere).value();
  const auto back = kernel::from_occt(*occ);
  REQUIRE(back.has_value());
  CHECK(std::ranges::equal(back->knots_u().knots(), sphere.knots_u().knots()));
  CHECK(std::ranges::equal(back->knots_v().knots(), sphere.knots_v().knots()));
  CHECK(std::ranges::equal(back->points(), sphere.points()));
  CHECK(std::ranges::equal(back->weights(), sphere.weights()));
  const double u1 = sphere.knots_u().domain_end(), v1 = sphere.knots_v().domain_end();
  for (int i = 0; i <= 8; ++i) {
    for (int j = 0; j <= 8; ++j) {
      const double u = u1 * i / 8, v = v1 * j / 8;
      const gp_Pnt p = occ->Value(u, v);
      CHECK(length(Vec3{p.X(), p.Y(), p.Z()} - sphere.point(u, v)) <= 1e-12 * 2);
    }
  }
}

TEST_CASE("OCCT-made B-splines: periodic ones are clamped, high degrees refused",
          "[kernel][occt]") {
  // A periodic cubic B-spline (OCCT's own form for closed curves) becomes clamped.
  TColgp_Array1OfPnt ring(1, 6);
  for (int i = 1; i <= 6; ++i) {
    const double t = 2 * std::numbers::pi * i / 6;
    ring.SetValue(i, gp_Pnt(3 * std::cos(t), 2 * std::sin(t), 0.5 * i));
  }
  TColStd_Array1OfReal ring_knots(1, 7);
  TColStd_Array1OfInteger ring_mults(1, 7);
  for (int i = 1; i <= 7; ++i) {
    ring_knots.SetValue(i, i - 1.0);
    ring_mults.SetValue(i, 1);
  }
  const Handle(Geom_BSplineCurve) periodic =
      new Geom_BSplineCurve(ring, ring_knots, ring_mults, 3, true);
  REQUIRE(periodic->IsPeriodic());
  const auto c = kernel::from_occt(*periodic);
  REQUIRE(c.has_value());
  for (int i = 0; i <= 60; ++i) {
    const double u = periodic->FirstParameter() +
                     (periodic->LastParameter() - periodic->FirstParameter()) * i / 60;
    const gp_Pnt p = periodic->Value(u);
    CHECK(length(Vec3{p.X(), p.Y(), p.Z()} - c->point(u)) <= 1e-12 * 10);
  }
  // And OCCT's conversion of a circle reads back as the circle.
  const Handle(Geom_Circle) circle = new Geom_Circle(gp_Ax2(gp_Pnt(1, 2, 3), gp_Dir(0, 0, 1)), 4.0);
  const auto from_circle = kernel::from_occt(*GeomConvert::CurveToBSplineCurve(circle));
  REQUIRE(from_circle.has_value());
  for (int i = 0; i <= 40; ++i) {
    const double u = from_circle->domain_start() +
                     (from_circle->domain_end() - from_circle->domain_start()) * i / 40;
    const Vec3 q = from_circle->point(u);
    CHECK(std::abs(std::hypot(q.x - 1, q.y - 2) - 4.0) <= 1e-12 * 10);
    CHECK(std::abs(q.z - 3.0) <= 1e-14);
  }
  // Degree 12 (OCCT allows up to 25) is beyond libnf-math.
  TColgp_Array1OfPnt poles(1, 13);
  for (int i = 1; i <= 13; ++i) poles.SetValue(i, gp_Pnt(i, i * i % 7, 0));
  TColStd_Array1OfReal knots(1, 2);
  knots.SetValue(1, 0.0);
  knots.SetValue(2, 1.0);
  TColStd_Array1OfInteger mults(1, 2);
  mults.SetValue(1, 13);
  mults.SetValue(2, 13);
  const Handle(Geom_BSplineCurve) high = new Geom_BSplineCurve(poles, knots, mults, 12);
  CHECK_FALSE(kernel::from_occt(*high).has_value());
}

TEST_CASE("knots OCCT cannot hold are an error, not an exception", "[kernel][occt]") {
  // Distinct knots one ulp apart: valid in libnf-math, below OCCT's Epsilon (review finding).
  const double k = 0.5, next = std::nextafter(0.5, 1.0);
  const auto kv = KnotVector::make({0, 0, k, next, 1, 1}, 1).value();
  const auto c3 = math::NurbsCurve3::make(kv, {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 1}}).value();
  const auto r3 = kernel::to_occt(c3);
  REQUIRE_FALSE(r3.has_value());
  CHECK_FALSE(r3.error().empty());
  const auto c2 = math::NurbsCurve2::make(kv, {{0, 0}, {1, 0}, {1, 1}, {0, 1}}).value();
  CHECK_FALSE(kernel::to_occt(c2).has_value());
}
