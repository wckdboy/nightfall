// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "nf/kernel/kernel.hpp"
#include "nf/math/constructors.hpp"
#include "nf/math/refine.hpp"
#include "snapshot.hpp"

#include <BRepAdaptor_Surface.hxx>
#include <TopoDS.hxx>

#include <algorithm>
#include <numbers>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace nf::kernel;
using namespace nf::math;
using Catch::Matchers::WithinAbs;

namespace {
constexpr double pi = std::numbers::pi;
std::vector<Region> filled(std::vector<std::vector<NurbsCurve2>> paths) {
  const auto result = regions(paths, FillRule::kEvenOdd);
  INFO((result ? "ok" : result.error()));
  REQUIRE(result);
  return *result;
}
void side_types(const OperationResult& result, GeomAbs_SurfaceType type, std::size_t count) {
  std::size_t found = 0;
  for (const auto& role : result.generated) {
    REQUIRE(role.entity);
    CHECK(role.entity->owner() == result.shape);
    if (role.role != "side") continue;
    REQUIRE(role.from);
    const auto& geometry = static_cast<const detail::Subshape&>(*role.entity).geometry;
    const auto actual = BRepAdaptor_Surface(TopoDS::Face(geometry)).GetType();
    found += actual == type;
  }
  CHECK(found == count);
  CHECK(result.generated.size() == result.shape->measures().faces);
}
OperationResult extrude(std::vector<NurbsCurve2> path, double height = 3) {
  auto kernel = make_occt_kernel().value();
  const auto result = kernel->extrude({{}, filled({std::move(path)}), {0, 0, height}});
  INFO((result ? "ok" : result.error().message));
  REQUIRE(result);
  return *result;
}
}  // namespace

TEST_CASE("circular profiles produce analytic cylinders", "[analytic-circle]") {
  auto kernel = make_occt_kernel().value();
  const auto profile = filled({{make_circle(Vec2{0, 0}, 10)}, {make_circle(Vec2{0, 0}, 4)}});
  for (const auto& plane : {Plane{}, Plane{{3, 7, 9}, {0, 1, 0}, {1, 0, 0}}}) {
    const auto result = kernel->extrude({plane, profile, plane.normal * 5});
    REQUIRE(result);
    CHECK_THAT(result->shape->measures().volume, WithinAbs(420 * pi, 1e-5));
    CHECK_THAT(result->shape->measures().area, WithinAbs(308 * pi, 1e-5));
    CHECK_THAT(length(result->shape->measures().centroid - plane.origin - plane.normal * 2.5),
               WithinAbs(0, 1e-7));
    side_types(*result, GeomAbs_Cylinder, 8);
  }
}

TEST_CASE("circular profiles produce analytic torus faces", "[analytic-circle]") {
  auto kernel = make_occt_kernel().value();
  const auto profile = filled({{make_circle(Vec2{20, 0}, 3)}});
  const auto result = kernel->revolve({{}, profile, {}, {0, 1, 0}, 2 * pi});
  REQUIRE(result);
  CHECK_THAT(result->shape->measures().volume, WithinAbs(360 * pi * pi, 1e-5));
  CHECK_THAT(result->shape->measures().area, WithinAbs(240 * pi * pi, 1e-5));
  CHECK_THAT(length(result->shape->measures().centroid), WithinAbs(0, 1e-7));
  side_types(*result, GeomAbs_Torus, 4);
}

TEST_CASE("circular profiles produce analytic sphere faces", "[analytic-circle]") {
  auto kernel = make_occt_kernel().value();
  const auto arc = make_arc(Vec2{0, 0}, 4, 4, 0, -pi / 2, pi / 2);
  const auto profile = filled({{arc, make_line(arc.points().back(), arc.points().front())}});
  const auto result = kernel->revolve({{}, profile, {}, {0, 1, 0}, 2 * pi});
  REQUIRE(result);
  CHECK_THAT(result->shape->measures().volume, WithinAbs(256 * pi / 3, 1e-5));
  CHECK_THAT(result->shape->measures().area, WithinAbs(64 * pi, 1e-5));
  CHECK_THAT(length(result->shape->measures().centroid), WithinAbs(0, 1e-7));
  side_types(*result, GeomAbs_Sphere, 2);
}

TEST_CASE("analytic circular profiles preserve signed partial revolutions and placement",
          "[analytic-circle]") {
  auto kernel = make_occt_kernel().value();
  const auto profile = filled({{make_circle(Vec2{10, 0}, 2)}});
  const Plane plane{{3, 7, 9}, {0, 1, 0}, {1, 0, 0}};
  for (const double sign : {-1.0, 1.0}) {
    const auto result = kernel->revolve({plane, profile, plane.origin, {0, 0, 1}, sign * pi / 2});
    REQUIRE(result);
    const auto& m = result->shape->measures();
    CHECK_THAT(m.volume, WithinAbs(20 * pi * pi, 1e-5));
    CHECK_THAT(m.area, WithinAbs(20 * pi * pi + 8 * pi, 1e-5));
    CHECK_THAT(m.centroid.x, WithinAbs(3 - sign * 20.2 / pi, 1e-7));
    CHECK_THAT(m.centroid.y, WithinAbs(7 + 20.2 / pi, 1e-7));
    CHECK_THAT(m.centroid.z, WithinAbs(9, 1e-7));
    side_types(*result, GeomAbs_Torus, 4);
    CHECK(m.faces == 6);
  }
}

TEST_CASE("analytic circular arcs survive splitting reparameterization and reversed winding",
          "[analytic-circle]") {
  constexpr double start = 0.2, sweep = 1.1, r = 7;
  const auto original = make_arc(Vec2{4, -3}, r, r, 0, start, start + sweep);
  for (const int variant : {0, 1, 2, 3}) {
    auto arc = original;
    if (variant == 1) {
      // A Mobius reparameterization changes the weights, not the circular locus.
      auto weights = std::vector<double>{arc.weights().begin(), arc.weights().end()};
      weights[1] *= 2;
      weights[2] *= 4;
      arc = NurbsCurve2::make(KnotVector::make({2, 2, 2, 9, 9, 9}, 2).value(),
                              {arc.points().begin(), arc.points().end()}, weights)
                .value();
    }
    if (variant == 2) arc = reverse(arc);
    std::vector<NurbsCurve2> path;
    if (variant == 3) {
      const auto halves = split(arc, 0.37).value();
      path = {halves.first, halves.second};
    } else {
      path = {arc};
    }
    path.push_back(make_line(arc.points().back(), arc.points().front()));
    const auto result = extrude(path);
    CHECK_THAT(result.shape->measures().volume,
               WithinAbs(1.5 * r * r * (sweep - std::sin(sweep)), 1e-7));
    side_types(result, GeomAbs_Cylinder, variant == 3 ? 2 : 1);
  }
}

TEST_CASE("elliptical and perturbed quadratic profiles retain their NURBS geometry",
          "[analytic-circle]") {
  for (const double y_radius : {3.0, 5.0 * (1 + 1e-8)}) {
    const auto result = extrude({make_arc(Vec2{0, 0}, 5, y_radius, 0.3, 0, 2 * pi)});
    CHECK_THAT(result.shape->measures().volume, WithinAbs(15 * y_radius * pi, 1e-7));
    side_types(result, GeomAbs_Cylinder, 0);
    side_types(result, GeomAbs_SurfaceOfExtrusion, 4);
  }
  const auto circle = make_arc(Vec2{0, 0}, 5, 5, 0, 0, pi / 2);
  for (const bool move_pole : {false, true}) {
    auto points = std::vector<Vec2>{circle.points().begin(), circle.points().end()};
    auto weights = std::vector<double>{circle.weights().begin(), circle.weights().end()};
    if (move_pole)
      points[1].y += 1e-8;
    else
      weights[1] += 1e-8;
    const auto arc = NurbsCurve2::make(circle.knots(), points, weights).value();
    const auto result = extrude({arc, make_line(points.back(), points.front())});
    side_types(result, GeomAbs_Cylinder, 0);
    side_types(result, GeomAbs_SurfaceOfExtrusion, 1);
  }
}

TEST_CASE("analytic circular arc recognition is stable across scale and angle",
          "[analytic-circle]") {
  for (const double radius : {0.01, 1.0, 100.0})
    for (const double start : {0.0, 0.37, 2.41})
      for (const double sweep : {1.1, pi / 2}) {
        CAPTURE(radius, start, sweep);
        const auto arc =
            make_arc(Vec2{4 * radius, -3 * radius}, radius, radius, 0, start, start + sweep);
        const auto result =
            extrude({arc, make_line(arc.points().back(), arc.points().front())}, 3 * radius);
        const auto volume = 1.5 * radius * radius * radius * (sweep - std::sin(sweep));
        CHECK_THAT(result.shape->measures().volume,
                   WithinAbs(volume, 1e-9 * std::max(1.0, volume)));
        side_types(result, GeomAbs_Cylinder, 1);
      }
}

TEST_CASE("circular profiles keep the authored scale of nearly orthonormal planes",
          "[analytic-circle]") {
  auto kernel = make_occt_kernel().value();
  constexpr double scale = 1 + 5e-10;
  const Plane plane{{}, {1, 0, 0}, {0, 0, scale}};
  const auto result = kernel->extrude({plane, filled({{make_circle(Vec2{}, 5)}}), {0, 0, 3}});
  REQUIRE(result);
  CHECK_THAT(result->shape->measures().volume, WithinAbs(75 * pi * scale, 1e-8));
  side_types(*result, GeomAbs_Cylinder, 0);
  side_types(*result, GeomAbs_SurfaceOfExtrusion, 4);
}

TEST_CASE("translated circular annuli retain analytic chamfer geometry",
          "[analytic-circle][offset-chamfer]") {
  auto kernel = make_occt_kernel().value();
  for (const double offset : {0., 610., -610.}) {
    CAPTURE(offset);
    const Plane plane{{1110 - offset, 0, -8}, {1, 0, 0}, {0, 0, 1}};
    const auto source = kernel->extrude(
        {plane,
         filled({{make_circle(Vec2{offset, 0}, 6)}, {make_circle(Vec2{offset, 0}, 2.8)}}),
         {0, 0, 8}});
    REQUIRE(source);
    side_types(*source, GeomAbs_Cylinder, 8);
    const auto cap =
        std::ranges::find_if(source->generated, [](const auto& g) { return g.role == "cap_end"; });
    REQUIRE(cap != source->generated.end());
    const auto boundary = kernel->edges(cap->entity).value();
    const auto chamfered = kernel->chamfer(source->shape, boundary, 1);
    REQUIRE(chamfered);
    const auto& measured = chamfered->shape->measures();
    CHECK_THAT(measured.volume, WithinAbs(pi * ((36 - 2.8 * 2.8) * 8 - 8.8), 1e-5));
    const double area = pi * (28.16 + 10.56 + 2 * 8.8 * (7 + std::sqrt(2.)));
    CHECK_THAT(measured.area, WithinAbs(area, 1e-5));
    CHECK_THAT(measured.centroid.x, WithinAbs(1110, 1e-8));
    CHECK_THAT(measured.centroid.y, WithinAbs(0, 1e-8));
    for (const auto& g : source->generated) CHECK(g.entity->owner() == source->shape);
  }
}

TEST_CASE("translated ellipses and perturbed quadratics stay in their NURBS representation",
          "[analytic-circle][offset-chamfer]") {
  constexpr Vec2 center{610, 0};
  for (const double perturbation : {1e-8, 1e-10, 1e-12}) {
    CAPTURE(perturbation);
    const auto ellipse = extrude({make_arc(center, 2.8, 2.8 * (1 + perturbation), 0, 0, 2 * pi)});
    side_types(ellipse, GeomAbs_Cylinder, 0);
    side_types(ellipse, GeomAbs_SurfaceOfExtrusion, 4);
    CHECK_THAT(ellipse.shape->measures().volume,
               WithinAbs(3 * pi * 2.8 * 2.8 * (1 + perturbation), 1e-7));
  }
  const auto circle = make_arc(center, 2.8, 2.8, 0, 0, pi / 2);
  for (const bool move_pole : {false, true}) {
    auto points = std::vector<Vec2>{circle.points().begin(), circle.points().end()};
    auto weights = std::vector<double>{circle.weights().begin(), circle.weights().end()};
    if (move_pole)
      points[1].y += 1e-8;
    else
      weights[1] += 1e-8;
    const auto arc = NurbsCurve2::make(circle.knots(), points, weights).value();
    const auto result = extrude({arc, make_line(points.back(), points.front())});
    side_types(result, GeomAbs_Cylinder, 0);
    side_types(result, GeomAbs_SurfaceOfExtrusion, 1);
  }
}
