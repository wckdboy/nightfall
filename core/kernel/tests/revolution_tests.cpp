// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "nf/kernel/kernel.hpp"
#include "nf/math/constructors.hpp"
#include "snapshot.hpp"

#include <BRepAdaptor_Surface.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>

#include <algorithm>
#include <limits>
#include <numbers>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace nf::kernel;
using namespace nf::math;
using Catch::Matchers::WithinAbs;
namespace {
constexpr auto kPi = std::numbers::pi;
std::vector<NurbsCurve2> rectangle(Vec2 corner, Vec2 size) {
  const auto a = corner, b = a + Vec2{size.x, 0}, c = a + size, d = a + Vec2{0, size.y};
  return {make_line(a, b), make_line(b, c), make_line(c, d), make_line(d, a)};
}
Revolution sleeve() {
  const std::vector<std::vector<NurbsCurve2>> paths{rectangle({5, 0}, {5, 20})};
  return {{}, regions(paths, FillRule::kEvenOdd).value(), {}, {0, 1, 0}, 2 * kPi};
}
void check_roles(const OperationResult& result, std::size_t caps) {
  const auto output = std::dynamic_pointer_cast<const detail::Snapshot>(result.shape);
  TopTools_IndexedMapOfShape faces;
  TopExp::MapShapes(output->geometry, TopAbs_FACE, faces);
  CHECK(result.generated.size() == result.shape->measures().faces);
  CHECK(std::ranges::count_if(result.generated, [](const auto& role) {
          return role.role == "cap_start" || role.role == "cap_end";
        }) == static_cast<std::ptrdiff_t>(caps));
  for (const auto& role : result.generated) {
    REQUIRE(role.entity);
    CHECK(role.entity->owner() == result.shape);
    CHECK(role.entity->kind() == EntityKind::kFace);
    const auto& geometry = static_cast<const detail::Subshape&>(*role.entity).geometry;
    REQUIRE(faces.Contains(geometry));
    CHECK(geometry.Orientation() == faces(faces.FindIndex(geometry)).Orientation());
    if (role.role == "side") CHECK(role.from.has_value());
  }
}
}  // namespace

TEST_CASE("production revolution builds an exact hollow cylinder with local side history") {
  auto kernel = make_occt_kernel().value();
  CHECK(kernel->capabilities().revolve);
  const auto result = kernel->revolve(sleeve());
  REQUIRE(result);
  const auto& m = result->shape->measures();
  CHECK_THAT(m.volume, WithinAbs(1500 * kPi, 1e-5));
  CHECK_THAT(m.area, WithinAbs(750 * kPi, 1e-5));
  CHECK_THAT(m.centroid.x, WithinAbs(0, 1e-7));
  CHECK_THAT(m.centroid.y, WithinAbs(10, 1e-7));
  CHECK_THAT(m.centroid.z, WithinAbs(0, 1e-7));
  CHECK(m.solids == 1);
  CHECK(m.faces == 4);
  const auto snapshot = std::dynamic_pointer_cast<const detail::Snapshot>(result->shape);
  TopTools_IndexedMapOfShape faces;
  TopExp::MapShapes(snapshot->geometry, TopAbs_FACE, faces);
  std::size_t planes = 0, cylinders = 0;
  for (int i = 1; i <= faces.Extent(); ++i) {
    const auto type = BRepAdaptor_Surface(TopoDS::Face(faces(i))).GetType();
    planes += type == GeomAbs_Plane;
    cylinders += type == GeomAbs_Cylinder;
  }
  CHECK(planes == 2);
  CHECK(cylinders == 2);
  check_roles(*result, 0);
  kernel.reset();
  CHECK_THAT(result->shape->measures().volume, WithinAbs(1500 * kPi, 1e-5));
}

TEST_CASE("production revolution supports signed partial turns and real end caps") {
  auto kernel = make_occt_kernel().value();
  for (const auto sign : {-1.0, 1.0}) {
    auto input = sleeve();
    input.angle = sign * kPi / 2;
    auto result = kernel->revolve(input);
    REQUIRE(result);
    const auto& m = result->shape->measures();
    CHECK_THAT(m.volume, WithinAbs(375 * kPi, 1e-5));
    CHECK_THAT(m.area, WithinAbs(187.5 * kPi + 200, 1e-5));
    CHECK_THAT(m.centroid.x, WithinAbs(1750 / (112.5 * kPi), 1e-7));
    CHECK_THAT(m.centroid.z, WithinAbs(-sign * 1750 / (112.5 * kPi), 1e-7));
    CHECK(m.faces == 6);
    CHECK(m.edges == 12);
    check_roles(*result, 2);
  }
}

TEST_CASE("production revolution retains exact rational torus geometry") {
  auto kernel = make_occt_kernel().value();
  auto input = sleeve();
  const std::vector<std::vector<NurbsCurve2>> paths{{make_circle(Vec2{10, 0}, 2)}};
  input.regions = regions(paths, FillRule::kEvenOdd).value();
  auto result = kernel->revolve(input);
  REQUIRE(result);
  CHECK_THAT(result->shape->measures().volume, WithinAbs(80 * kPi * kPi, 1e-5));
  CHECK_THAT(result->shape->measures().area, WithinAbs(80 * kPi * kPi, 1e-5));
  check_roles(*result, 0);
  auto moved = kernel->transform(result->shape, {{4, -7, 12}, {}, {1, 2, 3}, 0.8});
  REQUIRE(moved);
  CHECK_THAT(moved->shape->measures().volume, WithinAbs(80 * kPi * kPi, 1e-5));
  const auto snapshot = std::dynamic_pointer_cast<const detail::Snapshot>(moved->shape);
  CHECK_THAT(detail::snapshot(snapshot->geometry).value()->measures().volume,
             WithinAbs(80 * kPi * kPi, 1e-5));
}

TEST_CASE("production revolution permits profiles touching the axis on either side") {
  auto kernel = make_occt_kernel().value();
  for (const auto x : {-10.0, 0.0}) {
    auto input = sleeve();
    const std::vector<std::vector<NurbsCurve2>> paths{rectangle({x, 0}, {10, 20})};
    input.regions = regions(paths, FillRule::kEvenOdd).value();
    auto result = kernel->revolve(input);
    REQUIRE(result);
    CHECK_THAT(result->shape->measures().volume, WithinAbs(2000 * kPi, 1e-5));
    CHECK_THAT(result->shape->measures().area, WithinAbs(600 * kPi, 1e-5));
    CHECK(result->shape->measures().faces == 3);
    check_roles(*result, 0);
  }
}

TEST_CASE("production revolution handles placed planes cavities and disjoint regions") {
  auto kernel = make_occt_kernel().value();
  auto input = sleeve();
  input.plane = {{3, 7, 9}, {0, 1, 0}, {1, 0, 0}};
  input.axis_origin = {3, 7, 109};
  input.axis_direction = {0, 0, 2};
  auto placed = kernel->revolve(input);
  REQUIRE(placed);
  CHECK_THAT(placed->shape->measures().volume, WithinAbs(1500 * kPi, 1e-5));
  CHECK_THAT(placed->shape->measures().centroid.z, WithinAbs(19, 1e-7));
  CHECK_THAT(placed->shape->measures().centroid.x, WithinAbs(3, 1e-7));
  CHECK_THAT(placed->shape->measures().centroid.y, WithinAbs(7, 1e-7));
  input = sleeve();
  const std::vector<std::vector<NurbsCurve2>> cavity{rectangle({5, 0}, {5, 20}),
                                                     rectangle({6, 2}, {3, 16})};
  input.regions = regions(cavity, FillRule::kEvenOdd).value();
  auto hollow = kernel->revolve(input);
  REQUIRE(hollow);
  CHECK_THAT(hollow->shape->measures().volume, WithinAbs(780 * kPi, 1e-5));
  CHECK_THAT(hollow->shape->measures().area, WithinAbs(1320 * kPi, 1e-5));
  CHECK(hollow->shape->measures().solids == 1);
  check_roles(*hollow, 0);
  const std::vector<std::vector<NurbsCurve2>> islands{rectangle({5, 0}, {5, 20}),
                                                      rectangle({15, 0}, {2, 20})};
  input.regions = regions(islands, FillRule::kEvenOdd).value();
  auto multiple = kernel->revolve(input);
  REQUIRE(multiple);
  CHECK(multiple->shape->measures().solids == 2);
  CHECK_THAT(multiple->shape->measures().volume, WithinAbs(2780 * kPi, 1e-5));
  CHECK_THAT(multiple->shape->measures().area, WithinAbs(2158 * kPi, 1e-5));
  check_roles(*multiple, 0);
}

TEST_CASE("production revolution rejects invalid axes angles and overlapping sweeps") {
  auto kernel = make_occt_kernel().value();
  for (const auto angle : {0.0, 7.0, -7.0, std::numeric_limits<double>::infinity(),
                           std::numeric_limits<double>::quiet_NaN()}) {
    auto input = sleeve();
    input.angle = angle;
    CHECK_FALSE(kernel->revolve(input));
  }
  auto input = sleeve();
  input.axis_direction = {};
  CHECK_FALSE(kernel->revolve(input));
  input = sleeve();
  input.axis_direction = {0, 0, 1};
  CHECK_FALSE(kernel->revolve(input));
  input = sleeve();
  input.axis_origin.z = 1;
  CHECK_FALSE(kernel->revolve(input));
  input = sleeve();
  input.plane.x_axis = {2, 0, 0};
  CHECK_FALSE(kernel->revolve(input));
  input = sleeve();
  input.regions.clear();
  CHECK_FALSE(kernel->revolve(input));
  const std::vector<std::vector<NurbsCurve2>> crossing{rectangle({-5, 0}, {10, 20})};
  input.regions = regions(crossing, FillRule::kEvenOdd).value();
  CHECK_FALSE(kernel->revolve(input));
  const std::vector<std::vector<NurbsCurve2>> opposite{rectangle({5, 0}, {5, 20}),
                                                       rectangle({-10, 0}, {5, 20})};
  input.regions = regions(opposite, FillRule::kEvenOdd).value();
  CHECK_FALSE(kernel->revolve(input));
}
