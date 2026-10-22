// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// RFC 0013 interference and clearance: IKernel::proximity on boxes with analytic answers.
#include "nf/kernel/kernel.hpp"
#include "nf/math/constructors.hpp"
#include "snapshot.hpp"

#include <BRepTools.hxx>

#include <cmath>
#include <sstream>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace nf::kernel;
using namespace nf::math;
using Catch::Matchers::WithinAbs;

namespace {
ShapePtr box(IKernel& kernel, Vec3 origin, Vec3 size) {
  std::vector<std::vector<NurbsCurve2>> paths{
      {make_line(Vec2{}, {size.x, 0}), make_line(Vec2{size.x, 0}, {size.x, size.y}),
       make_line(Vec2{size.x, size.y}, {0, size.y}), make_line(Vec2{0, size.y}, {})}};
  return kernel.extrude({{origin}, regions(paths, FillRule::kEvenOdd).value(), {0, 0, size.z}})
      .value()
      .shape;
}
std::string serialized(const ShapePtr& shape) {
  std::ostringstream stream;
  BRepTools::Write(std::dynamic_pointer_cast<const detail::Snapshot>(shape)->geometry, stream);
  return stream.str();
}
}  // namespace

TEST_CASE("overlapping solids report their exact common volume", "[kernel][proximity]") {
  auto kernel = make_occt_kernel().value();
  const auto a = box(*kernel, {0, 0, 0}, {10, 10, 10});
  const auto b = box(*kernel, {6, 7, 8}, {10, 10, 10});
  const auto before = serialized(a) + serialized(b);
  const auto p = kernel->proximity(a, b).value();
  // The overlap is 4 x 3 x 2 mm.
  CHECK_THAT(p.common_volume, WithinAbs(24, 24 * 1e-6));
  CHECK(p.distance == 0);
  CHECK(serialized(a) + serialized(b) == before);  // inputs are untouched
  // A solid inside another overlaps by its whole volume, though their boundaries never meet.
  const auto inner = box(*kernel, {2, 3, 4}, {1, 2, 3});
  const auto nested = kernel->proximity(a, inner).value();
  CHECK_THAT(nested.common_volume, WithinAbs(6, 6 * 1e-6));
  CHECK(nested.distance == 0);
}

TEST_CASE("touching solids report contact with no common volume", "[kernel][proximity]") {
  auto kernel = make_occt_kernel().value();
  const auto a = box(*kernel, {0, 0, 0}, {10, 10, 10});
  for (const auto origin : {Vec3{10, 0, 0}, Vec3{10, 4, 2}, Vec3{10, 10, 10}}) {
    CAPTURE(origin.x, origin.y, origin.z);
    const auto p = kernel->proximity(a, box(*kernel, origin, {5, 5, 5})).value();
    CHECK_THAT(p.common_volume, WithinAbs(0, 1e-9));
    CHECK_THAT(p.distance, WithinAbs(0, 1e-9));
  }
}

TEST_CASE("separated solids report the minimum distance and closest points",
          "[kernel][proximity]") {
  auto kernel = make_occt_kernel().value();
  const auto a = box(*kernel, {0, 0, 0}, {10, 10, 10});
  // Face to face across x: 3 mm.
  const auto across = kernel->proximity(a, box(*kernel, {13, 4, 0}, {10, 10, 10})).value();
  CHECK_THAT(across.distance, WithinAbs(3, 1e-9));
  CHECK_THAT(across.on_a.x, WithinAbs(10, 1e-9));
  CHECK_THAT(across.on_b.x, WithinAbs(13, 1e-9));
  CHECK_THAT(std::hypot(across.on_b.x - across.on_a.x, across.on_b.y - across.on_a.y,
                        across.on_b.z - across.on_a.z),
             WithinAbs(3, 1e-9));
  CHECK(across.common_volume == 0);
  // Edge to edge across a diagonal gap of (2, 4) mm.
  const auto corner = kernel->proximity(a, box(*kernel, {12, 14, 0}, {5, 5, 10})).value();
  CHECK_THAT(corner.distance, WithinAbs(std::hypot(2.0, 4.0), 1e-9));
  CHECK_THAT(corner.on_a.x, WithinAbs(10, 1e-9));
  CHECK_THAT(corner.on_a.y, WithinAbs(10, 1e-9));
  CHECK_THAT(corner.on_b.x, WithinAbs(12, 1e-9));
  CHECK_THAT(corner.on_b.y, WithinAbs(14, 1e-9));
}
