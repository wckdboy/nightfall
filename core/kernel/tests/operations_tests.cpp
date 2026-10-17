// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "nf/kernel/kernel.hpp"
#include "nf/math/constructors.hpp"
#include "snapshot.hpp"

#include <limits>
#include <numbers>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace nf::kernel;
using namespace nf::math;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {
Extrusion box_profile() {
  std::vector<std::vector<NurbsCurve2>> paths{
      {make_line(Vec2{0, 0}, {10, 0}), make_line(Vec2{10, 0}, {10, 20}),
       make_line(Vec2{10, 20}, {0, 20}), make_line(Vec2{0, 20}, {0, 0})}};
  return {{}, regions(paths, FillRule::kEvenOdd).value(), {0, 0, 30}};
}
}  // namespace

TEST_CASE("production extrusion supplies validated solids and local face roles") {
  auto kernel = make_occt_kernel().value();
  auto result = kernel->extrude(box_profile());
  REQUIRE(result);
  const auto& m = result->shape->measures();
  CHECK_THAT(m.volume, WithinAbs(6000, 1e-8));
  CHECK_THAT(m.area, WithinAbs(2200, 1e-8));
  CHECK(m.faces == 6);
  CHECK(m.edges == 12);
  CHECK(result->generated.size() == 6);
  CHECK(result->generated[0].role == "cap_start");
  CHECK(result->generated[1].role == "cap_end");
  for (const auto& generated : result->generated) {
    CHECK(generated.entity->owner() == result->shape);
    CHECK(generated.entity->kind() == EntityKind::kFace);
  }
  CHECK(result->estimated_bytes >= result->shape->estimated_bytes());
  kernel.reset();
  CHECK_THAT(result->shape->measures().volume, WithinAbs(6000, 1e-8));
}

TEST_CASE("production extrusion handles exact circular holes and separate islands") {
  auto kernel = make_occt_kernel().value();
  std::vector<std::vector<NurbsCurve2>> paths{
      {make_circle(Vec2{}, 10)}, {make_circle(Vec2{}, 5)}, {make_circle(Vec2{40, 0}, 2)}};
  Extrusion input{{}, regions(paths, FillRule::kEvenOdd).value(), {0, 0, 7}};
  auto result = kernel->extrude(input);
  REQUIRE(result);
  CHECK(result->shape->measures().solids == 2);
  CHECK_THAT(result->shape->measures().volume, WithinAbs(553 * std::numbers::pi, 1e-5));
  CHECK_THAT(result->shape->measures().area, WithinAbs(396 * std::numbers::pi, 1e-5));
  CHECK(result->generated.size() == result->shape->measures().faces);
  auto moved = kernel->transform(result->shape, {{9, -2, 5}, {}, {1, 2, 3}, 0.7});
  REQUIRE(moved);
  CHECK_THAT(moved->shape->measures().volume, WithinAbs(553 * std::numbers::pi, 1e-5));
  CHECK_THAT(moved->shape->measures().area, WithinAbs(396 * std::numbers::pi, 1e-5));
  const auto actual = std::dynamic_pointer_cast<const detail::Snapshot>(moved->shape);
  const auto measured = detail::snapshot(actual->geometry).value();
  CHECK_THAT(measured->measures().volume, WithinAbs(moved->shape->measures().volume, 1e-5));
  CHECK_THAT(measured->measures().area, WithinAbs(moved->shape->measures().area, 1e-5));
  CHECK_THAT(measured->measures().centroid.x, WithinAbs(moved->shape->measures().centroid.x, 1e-7));
}

TEST_CASE("production extrusion respects plane placement and rejects invalid profiles") {
  auto kernel = make_occt_kernel().value();
  auto input = box_profile();
  SECTION("yz plane, negative extrusion") {
    input.plane = {{7, 2, 3}, {0, 1, 0}, {1, 0, 0}};
    input.displacement = {-30, 0, 0};
    auto result = kernel->extrude(input);
    REQUIRE(result);
    CHECK_THAT(result->shape->measures().volume, WithinAbs(6000, 1e-8));
    CHECK_THAT(result->shape->measures().centroid.x, WithinAbs(-8, 1e-8));
    CHECK_THAT(result->shape->measures().centroid.y, WithinAbs(7, 1e-8));
  }
  SECTION("zero distance") {
    input.displacement = {};
    CHECK_FALSE(kernel->extrude(input));
  }
  SECTION("in plane displacement") {
    input.displacement = {30, 0, 0};
    CHECK_FALSE(kernel->extrude(input));
  }
  SECTION("open loop") {
    input.regions.front().outer.curves.pop_back();
    CHECK_FALSE(kernel->extrude(input));
  }
  SECTION("nonorthogonal plane") {
    input.plane.x_axis = input.plane.normal;
    CHECK_FALSE(kernel->extrude(input));
  }
  SECTION("empty profile") {
    input.regions.clear();
    CHECK_FALSE(kernel->extrude(input));
  }
  SECTION("nonfinite displacement") {
    input.displacement.z = std::numeric_limits<double>::infinity();
    CHECK_FALSE(kernel->extrude(input));
  }
}

TEST_CASE("rigid transforms preserve source snapshots and trace every face and edge") {
  auto kernel = make_occt_kernel().value();
  auto source = kernel->extrude(box_profile()).value();
  RigidTransform input{{7, 0, 0}, {}, {0, 0, 1}, std::numbers::pi / 2};
  auto moved = kernel->transform(source.shape, input);
  REQUIRE(moved);
  CHECK_THAT(moved->shape->measures().volume, WithinAbs(6000, 1e-8));
  CHECK_THAT(moved->shape->measures().centroid.x, WithinAbs(-3, 1e-8));
  CHECK_THAT(moved->shape->measures().centroid.y, WithinAbs(5, 1e-8));
  const auto actual = std::dynamic_pointer_cast<const detail::Snapshot>(moved->shape);
  const auto measured = detail::snapshot(actual->geometry).value();
  CHECK_THAT(measured->measures().centroid.x, WithinAbs(-3, 1e-8));
  CHECK_THAT(measured->measures().centroid.y, WithinAbs(5, 1e-8));
  // OCCT's exact-geometry bounds include its 1e-7 mm gap plus floating-point roundoff.
  CHECK_THAT(measured->measures().minimum.x, WithinAbs(-13, 2e-7));
  CHECK_THAT(measured->measures().maximum.x, WithinAbs(7, 2e-7));
  CHECK_THAT(source.shape->measures().centroid.x, WithinAbs(5, 1e-10));
  CHECK(moved->history.size() == 18);
  for (const auto& change : moved->history) {
    CHECK(change.before->owner() == source.shape);
    REQUIRE(change.modified.size() == 1);
    CHECK(change.modified.front()->owner() == moved->shape);
    CHECK(change.before->kind() == change.modified.front()->kind());
    CHECK_FALSE(change.deleted);
  }
  CHECK(moved->estimated_bytes >=
        source.shape->estimated_bytes() + moved->shape->estimated_bytes());
  CHECK_FALSE(kernel->transform({}, input));
  input.axis_direction = {};
  CHECK_FALSE(kernel->transform(source.shape, input));
  CHECK_THAT(source.shape->measures().volume, WithinAbs(6000, 1e-8));
  // Reinspect the actual B-rep, not just its cached measurements, after success and failure.
  const auto original = std::dynamic_pointer_cast<const detail::Snapshot>(source.shape);
  const auto remeasured = detail::snapshot(original->geometry).value();
  CHECK_THAT(remeasured->measures().centroid.x, WithinAbs(5, 1e-10));
  CHECK_THAT(remeasured->measures().volume, WithinAbs(6000, 1e-8));
  auto repeated = kernel->transform(source.shape, RigidTransform{});
  REQUIRE(repeated);
  CHECK_THAT(repeated->shape->measures().centroid.x, WithinAbs(5, 1e-10));
}

TEST_CASE("backend identity includes tolerances and rejects invalid configuration") {
  auto first = make_occt_kernel().value();
  auto second = make_occt_kernel(Tolerance{1e-5, 1e-8}).value();
  CHECK(first->identity() != second->identity());
  CHECK_FALSE(make_occt_kernel(Tolerance{0, 1e-9}));
  CHECK_FALSE(make_occt_kernel(Tolerance{1e-6, std::numeric_limits<double>::quiet_NaN()}));
}

TEST_CASE("production profiles connect short bridges within the modeling tolerance") {
  auto kernel = make_occt_kernel().value();
  for (const double gap : {5e-7, 5e-8}) {
    CAPTURE(gap);
    Region region{{{make_line(Vec2{0, 0}, {10, 0}), make_line(Vec2{10, 0}, {10, gap}),
                    make_line(Vec2{10, gap}, {10, 20}), make_line(Vec2{10, 20}, {0, 20}),
                    make_line(Vec2{0, 20}, {0, 0})}},
                  {}};
    const auto result = kernel->extrude({{}, {region}, {0, 0, 4}});
    REQUIRE(result);
    CHECK_THAT(result->shape->measures().volume, WithinAbs(800, 1e-5));
    for (const auto& generated : result->generated)
      CHECK(generated.entity->owner() == result->shape);
  }
}

TEST_CASE("exact rational cylinders retain analytic measures over different scales") {
  auto kernel = make_occt_kernel().value();
  for (double radius : {0.1, 1.0, 10.0, 100.0}) {
    std::vector<std::vector<NurbsCurve2>> paths{{make_circle(Vec2{17, -3}, radius)}};
    auto result = kernel->extrude({{}, regions(paths, FillRule::kEvenOdd).value(), {0, 0, 7}});
    REQUIRE(result);
    CHECK_THAT(result->shape->measures().volume,
               WithinAbs(std::numbers::pi * radius * radius * 7, 1e-6));
    // OCCT's adaptive estimate is local to each face; check the assembled area relatively.
    CHECK_THAT(result->shape->measures().area,
               WithinRel(2 * std::numbers::pi * radius * (radius + 7), 1e-8));
    CHECK_THAT(result->shape->measures().centroid.x, WithinAbs(17, 1e-7));
    CHECK_THAT(result->shape->measures().centroid.z, WithinAbs(3.5, 1e-7));
  }
}
