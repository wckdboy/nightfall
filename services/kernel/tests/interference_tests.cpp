// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// RFC 0013 interference and clearance over placed "occurrence/body" solids: analytic boxes.
#include "assembly_queries.hpp"
#include "nf/math/constructors.hpp"

#include <cmath>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace nf;
using namespace nf::kernel_service;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {
kernel::ShapePtr box(kernel::IKernel& kernel, math::Vec3 origin, math::Vec3 size) {
  using math::make_line;
  using math::Vec2;
  std::vector<std::vector<math::NurbsCurve2>> paths{
      {make_line(Vec2{}, {size.x, 0}), make_line(Vec2{size.x, 0}, {size.x, size.y}),
       make_line(Vec2{size.x, size.y}, {0, size.y}), make_line(Vec2{0, size.y}, {})}};
  return kernel
      .extrude({{origin}, math::regions(paths, math::FillRule::kEvenOdd).value(), {0, 0, size.z}})
      .value()
      .shape;
}
}  // namespace

TEST_CASE("interference pairs bodies of different occurrences and measures overlap",
          "[assembly][interference]") {
  auto kernel = kernel::make_occt_kernel().value();
  const std::vector<PlacedBody> bodies{
      {"base/plate", box(*kernel, {0, 0, 0}, {20, 20, 5})},
      // The base's own boss overlaps its plate: one occurrence, never reported.
      {"base/boss", box(*kernel, {5, 5, 4}, {4, 4, 4})},
      // 2 mm into the plate over 10 x 10 mm: 200 mm^3.
      {"lid/plate", box(*kernel, {0, 0, 3}, {10, 10, 6})},
      // Resting on the plate's top face: contact, no volume.
      {"shim/plate", box(*kernel, {12, 12, 5}, {4, 4, 1})},
      // Far away: its bounding box meets nothing, so it is never checked exactly.
      {"spare/plate", box(*kernel, {100, 0, 0}, {5, 5, 5})},
  };
  std::size_t checked = 0;
  const auto found = check_interference(*kernel, bodies, {}, &checked).value();
  REQUIRE(found.size() == 3);
  // Sorted by body ids, a < b.
  CHECK(found[0].a == "base/boss");
  CHECK(found[0].b == "lid/plate");
  CHECK_THAT(found[0].volume, WithinRel(4.0 * 4 * 4, 1e-6));  // the boss sits inside the lid
  CHECK(found[1].a == "base/plate");
  CHECK(found[1].b == "lid/plate");
  CHECK_THAT(found[1].volume, WithinRel(200.0, 1e-6));
  CHECK_FALSE(found[1].touching);
  CHECK(found[2].a == "base/plate");
  CHECK(found[2].b == "shim/plate");
  CHECK(found[2].touching);
  CHECK(found[2].volume == 0);
  // Only three pairs' boxes meet (boss-lid, plate-lid, plate-shim); the rest are skipped.
  CHECK(checked == 3);
  // Restricted to the shim, only its contact remains.
  const auto shim = check_interference(*kernel, bodies, {"shim"}).value();
  REQUIRE(shim.size() == 1);
  CHECK(shim[0].b == "shim/plate");
}

TEST_CASE("clearance is the smallest gap between two sets of bodies", "[assembly][interference]") {
  auto kernel = kernel::make_occt_kernel().value();
  const std::vector<PlacedBody> a{{"arm/link", box(*kernel, {0, 0, 0}, {10, 2, 2})},
                                  {"arm/pin", box(*kernel, {9, 0, 2}, {1, 1, 6})}};
  const std::vector<PlacedBody> b{{"stop/block", box(*kernel, {14, -5, 0}, {4, 10, 10})}};
  const auto c = measure_clearance(*kernel, a, b).value();
  CHECK_THAT(c.distance, WithinAbs(4, 1e-9));
  CHECK(c.b == "stop/block");
  CHECK_THAT(c.on_a.x, WithinAbs(10, 1e-9));
  CHECK_THAT(c.on_b.x, WithinAbs(14, 1e-9));
  CHECK_FALSE(measure_clearance(*kernel, a, {}).has_value());
}
