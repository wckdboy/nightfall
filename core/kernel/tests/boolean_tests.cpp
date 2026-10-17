// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "nf/kernel/kernel.hpp"
#include "nf/math/constructors.hpp"
#include "snapshot.hpp"

#include <BRepTools.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>

#include <algorithm>
#include <numbers>
#include <sstream>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace nf::kernel;
using namespace nf::math;
using Catch::Matchers::WithinAbs;

namespace {
ShapePtr box(IKernel& kernel, Vec3 origin = {}, Vec3 size = {10, 10, 10}) {
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
void history(const OperationResult& result, const std::vector<ShapePtr>& inputs) {
  const auto output = std::dynamic_pointer_cast<const detail::Snapshot>(result.shape);
  TopTools_IndexedMapOfShape members;
  TopExp::MapShapes(output->geometry, members);
  std::size_t count = 0;
  for (const auto& input : inputs) {
    count += input->measures().faces + input->measures().edges;
    for (const auto& before :
         detail::entities(std::dynamic_pointer_cast<const detail::Snapshot>(input))) {
      REQUIRE(std::ranges::count_if(
                  result.history, [&](const auto& h) { return h.before->same_as(*before); }) == 1);
    }
  }
  CHECK(result.history.size() == count);
  auto check = [&](const EntityPtr& entity) {
    REQUIRE(entity);
    CHECK(entity->owner() == result.shape);
    const auto& geometry = static_cast<const detail::Subshape&>(*entity).geometry;
    REQUIRE(members.Contains(geometry));
    CHECK(geometry.Orientation() == members(members.FindIndex(geometry)).Orientation());
  };
  for (const auto& h : result.history) {
    CHECK(h.deleted == h.modified.empty());
    for (const auto& entity : h.modified) check(entity);
    for (const auto& entity : h.generated) check(entity);
  }
  for (const auto& role : result.generated) check(role.entity);
  CHECK(result.estimated_bytes >= result.shape->estimated_bytes());
}
}  // namespace

TEST_CASE("solid booleans produce analytic union difference and common with immutable inputs") {
  auto kernel = make_occt_kernel().value();
  REQUIRE(kernel->capabilities().booleans);
  const auto a = box(*kernel), b = box(*kernel, {5, 0, 0});
  const auto saved_a = serialized(a), saved_b = serialized(b);
  for (const auto op : {BooleanOp::kUnion, BooleanOp::kSubtract, BooleanOp::kIntersect}) {
    const auto result = kernel->boolean_operation(a, {b}, op);
    REQUIRE(result);
    const auto& m = result->shape->measures();
    CHECK_THAT(m.volume, WithinAbs(op == BooleanOp::kUnion ? 1500 : 500, 1e-7));
    CHECK_THAT(m.area, WithinAbs(op == BooleanOp::kUnion ? 800 : 400, 1e-7));
    CHECK(m.solids == 1);
    CHECK(m.faces == 6);
    CHECK(m.edges == 12);
    history(*result, {a, b});
    CHECK(serialized(a) == saved_a);
    CHECK(serialized(b) == saved_b);
  }
}

TEST_CASE("solid booleans cut exact circular through holes and preserve tool face history") {
  auto kernel = make_occt_kernel().value();
  const auto target = box(*kernel);
  std::vector<std::vector<NurbsCurve2>> paths{{make_circle(Vec2{5, 5}, 2)}};
  const auto tool =
      kernel->extrude({{{0, 0, -1}}, regions(paths, FillRule::kEvenOdd).value(), {0, 0, 12}})
          .value()
          .shape;
  const auto saved = serialized(tool);
  const auto result = kernel->boolean_operation(target, {tool}, BooleanOp::kSubtract);
  REQUIRE(result);
  CHECK_THAT(result->shape->measures().volume, WithinAbs(1000 - 40 * std::numbers::pi, 1e-5));
  CHECK_THAT(result->shape->measures().area, WithinAbs(600 + 32 * std::numbers::pi, 1e-5));
  CHECK(result->shape->measures().solids == 1);
  CHECK(serialized(tool) == saved);
  history(*result, {target, tool});
  CHECK(std::ranges::any_of(result->history, [&](const auto& h) {
    return h.before->owner() == tool && h.before->kind() == EntityKind::kFace && !h.deleted;
  }));
  CHECK(std::ranges::any_of(result->generated, [](const auto& role) {
    return role.role == "intersection" && role.entity->kind() == EntityKind::kEdge;
  }));
}

TEST_CASE("solid booleans treat tools as a union and support split solids and cavities") {
  auto kernel = make_occt_kernel().value();
  const auto target = box(*kernel);
  const auto first = box(*kernel, {2, -1, -1}, {2, 12, 12});
  const auto second = box(*kernel, {6, -1, -1}, {2, 12, 12});
  auto split = kernel->boolean_operation(target, {first, second}, BooleanOp::kSubtract);
  REQUIRE(split);
  CHECK(split->shape->measures().solids == 3);
  CHECK_THAT(split->shape->measures().volume, WithinAbs(600, 1e-7));
  CHECK_THAT(split->shape->measures().area, WithinAbs(840, 1e-7));
  history(*split, {target, first, second});
  auto common = kernel->boolean_operation(target, {first, second}, BooleanOp::kIntersect);
  REQUIRE(common);
  CHECK(common->shape->measures().solids == 2);
  CHECK_THAT(common->shape->measures().volume, WithinAbs(400, 1e-7));
  CHECK_THAT(common->shape->measures().area, WithinAbs(560, 1e-7));
  history(*common, {target, first, second});
  auto duplicate = kernel->boolean_operation(target, {first, first}, BooleanOp::kSubtract);
  REQUIRE(duplicate);
  CHECK_THAT(duplicate->shape->measures().volume, WithinAbs(800, 1e-7));
  history(*duplicate, {target, first});
  auto cavity =
      kernel->boolean_operation(target, {box(*kernel, {2, 2, 2}, {6, 6, 6})}, BooleanOp::kSubtract);
  REQUIRE(cavity);
  CHECK_THAT(cavity->shape->measures().volume, WithinAbs(784, 1e-7));
  CHECK_THAT(cavity->shape->measures().area, WithinAbs(816, 1e-7));
}

TEST_CASE("solid booleans reject empty or non-solid intersections and keep disjoint solids") {
  auto kernel = make_occt_kernel().value();
  const auto target = box(*kernel), distant = box(*kernel, {20, 0, 0});
  auto joined = kernel->boolean_operation(target, {distant}, BooleanOp::kUnion);
  REQUIRE(joined);
  CHECK(joined->shape->measures().solids == 2);
  CHECK_THAT(joined->shape->measures().volume, WithinAbs(2000, 1e-7));
  CHECK_THAT(joined->shape->measures().area, WithinAbs(1200, 1e-7));
  history(*joined, {target, distant});
  auto unchanged = kernel->boolean_operation(target, {distant}, BooleanOp::kSubtract);
  REQUIRE(unchanged);
  CHECK_THAT(unchanged->shape->measures().volume, WithinAbs(1000, 1e-7));
  history(*unchanged, {target, distant});
  CHECK_FALSE(kernel->boolean_operation(target, {distant}, BooleanOp::kIntersect));
  CHECK_FALSE(kernel->boolean_operation(target, {target}, BooleanOp::kSubtract));
  CHECK_FALSE(kernel->boolean_operation(target, {box(*kernel, {10, 0, 0})}, BooleanOp::kIntersect));
  auto identical = kernel->boolean_operation(target, {target, target}, BooleanOp::kUnion);
  REQUIRE(identical);
  CHECK_THAT(identical->shape->measures().volume, WithinAbs(1000, 1e-7));
  history(*identical, {target});
}

TEST_CASE("solid booleans validate operands and snapshots outlive their producing kernel") {
  auto kernel = make_occt_kernel().value();
  const auto target = box(*kernel), tool = box(*kernel, {5, 0, 0});
  CHECK_FALSE(kernel->boolean_operation({}, {tool}, BooleanOp::kUnion));
  CHECK_FALSE(kernel->boolean_operation(target, {}, BooleanOp::kUnion));
  CHECK_FALSE(kernel->boolean_operation(target, {{}}, BooleanOp::kUnion));
  CHECK_FALSE(kernel->boolean_operation(target, {tool}, static_cast<BooleanOp>(-1)));
  auto result = kernel->boolean_operation(target, {tool}, BooleanOp::kSubtract).value();
  kernel.reset();
  CHECK_THAT(result.shape->measures().volume, WithinAbs(500, 1e-7));
  history(result, {target, tool});
}
