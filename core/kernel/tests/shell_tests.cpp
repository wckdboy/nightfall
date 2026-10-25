// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// RFC 0015 shells: hollow inward to a wall thickness, open faces removed, history exact.
#include "nf/kernel/kernel.hpp"
#include "nf/math/constructors.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <numbers>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace nf::kernel;
using namespace nf::math;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {
ShapePtr prism(IKernel& kernel, std::vector<NurbsCurve2> loop, double height) {
  const std::vector<std::vector<NurbsCurve2>> paths{std::move(loop)};
  return kernel.extrude({{}, regions(paths, FillRule::kEvenOdd).value(), {0, 0, height}})
      .value()
      .shape;
}
/// A 40 x 30 x 20 block at the origin.
ShapePtr block(IKernel& kernel) {
  return prism(kernel,
               {make_line(Vec2{}, {40, 0}), make_line(Vec2{40, 0}, {40, 30}),
                make_line(Vec2{40, 30}, {0, 30}), make_line(Vec2{0, 30}, {})},
               20);
}
/// The faces whose area centroid lies at height z.
std::vector<EntityPtr> faces_at(IKernel& kernel, const ShapePtr& shape, double z) {
  std::vector<EntityPtr> result;
  for (const auto& f : kernel.faces(shape).value())
    if (std::abs(kernel.face_mass(f.entity).value().centroid.z - z) < 1e-9)
      result.push_back(f.entity);
  return result;
}
const History& history_of(const OperationResult& r, const EntityPtr& before) {
  const auto h =
      std::ranges::find_if(r.history, [&](const History& x) { return x.before->same_as(*before); });
  REQUIRE(h != r.history.end());
  return *h;
}
}  // namespace

TEST_CASE("Shell hollows a block inward, its open face becoming the rim", "[shell]") {
  auto kernel = make_occt_kernel().value();
  const auto source = block(*kernel);
  const auto top = faces_at(*kernel, source, 20);
  REQUIRE(top.size() == 1);
  const auto result = kernel->shell(source, top, 2);
  REQUIRE(result);
  CHECK_THAT(result->shape->measures().volume, WithinRel(40.0 * 30 * 20 - 36.0 * 26 * 18, 1e-9));
  CHECK(result->shape->measures().faces == 11);
  CHECK(result->generated.size() == 5);
  for (const auto& g : result->generated) CHECK(g.role == "inner");
  // The bottom stays, and lines a floor 2 mm above it.
  const auto bottom = faces_at(*kernel, source, 0);
  REQUIRE(bottom.size() == 1);
  const auto& floor = history_of(*result, bottom.front());
  REQUIRE(floor.modified.size() == 1);
  CHECK_THAT(kernel->face_mass(floor.modified.front()).value().area, WithinAbs(1200, 1e-9));
  const auto inner = std::ranges::find_if(
      floor.generated, [](const EntityPtr& e) { return e->kind() == EntityKind::kFace; });
  REQUIRE(inner != floor.generated.end());
  CHECK_THAT(kernel->face_mass(*inner).value().centroid.z, WithinAbs(2, 1e-9));
  CHECK_THAT(kernel->face_mass(*inner).value().area, WithinAbs(36.0 * 26, 1e-9));
  // The open top survives as the rim around the opening.
  const auto& rim = history_of(*result, top.front());
  REQUIRE(rim.modified.size() == 1);
  CHECK_THAT(kernel->face_mass(rim.modified.front()).value().area,
             WithinAbs(40.0 * 30 - 36.0 * 26, 1e-9));
  // Every output face is either an input's image or a generated inner wall.
  std::size_t covered = 0;
  for (const auto& f : kernel->faces(result->shape).value())
    covered +=
        std::ranges::any_of(result->history,
                            [&](const History& h) {
                              return std::ranges::any_of(
                                  h.modified, [&](const auto& m) { return m->same_as(*f.entity); });
                            }) ||
        std::ranges::any_of(result->generated,
                            [&](const Generated& g) { return g.entity->same_as(*f.entity); });
  CHECK(covered == 11);
}

TEST_CASE("Shell keeps a wall thick for its body", "[shell]") {
  auto kernel = make_occt_kernel().value();
  // 14 mm into a 40 x 30 x 20 block leaves a 12 x 2 x 6 cavity: the wall is far below the kept
  // area times the thickness, and still a valid shell.
  const auto source = block(*kernel);
  const auto result = kernel->shell(source, faces_at(*kernel, source, 20), 14);
  REQUIRE(result);
  CHECK_THAT(result->shape->measures().volume, WithinRel(40.0 * 30 * 20 - 12.0 * 2 * 6, 1e-9));
}

TEST_CASE("Shell offsets curved walls", "[shell]") {
  auto kernel = make_occt_kernel().value();
  const auto source = prism(*kernel, {make_circle({}, 10)}, 20);
  const auto result = kernel->shell(source, faces_at(*kernel, source, 20), 2);
  REQUIRE(result);
  CHECK_THAT(result->shape->measures().volume,
             WithinRel(std::numbers::pi * (100.0 * 20 - 64.0 * 18), 1e-7));
}

TEST_CASE("Shell rejects bad thicknesses and faces it does not own", "[shell]") {
  auto kernel = make_occt_kernel().value();
  const auto source = block(*kernel);
  const auto other = block(*kernel);
  const auto top = faces_at(*kernel, source, 20);
  for (const double t : {0.0, -2.0, 1e-9, std::numeric_limits<double>::quiet_NaN(), 25.0})
    CHECK_FALSE(kernel->shell(source, top, t));
  CHECK_FALSE(kernel->shell(source, faces_at(*kernel, other, 20), 2));
  const auto edge = kernel->edges(top.front()).value().front();
  CHECK_FALSE(kernel->shell(source, {edge}, 2));
  CHECK_FALSE(kernel->shell(nullptr, {}, 2));
  // A sealed void needs no opening, but OCCT's thick solid needs one; refused until built.
  CHECK_FALSE(kernel->shell(source, {}, 2));
}

TEST_CASE("Shell retries a wall OCCT reports built but leaves unchanged", "[shell]") {
  // A SolidCraft body (block, tab, boss, cross hole) on which OCCT's thick solid with arc joins
  // returns its input as done; with offset self-intersections handled it builds the wall.
  auto kernel = make_occt_kernel().value();
  std::ifstream file(NF_KERNEL_TEST_DATA "/shell_retry.step");
  REQUIRE(file.is_open());
  const std::string text{std::istreambuf_iterator<char>{file}, {}};
  const auto source = kernel->import_step(text).value().front();
  std::vector<EntityPtr> open;
  for (const auto& f : kernel->faces(source).value())
    if (kernel->face_distance(f.entity, {33.965630820756, 31.465630820756, 0}).value() < 1e-6)
      open.push_back(f.entity);
  REQUIRE(open.size() == 1);
  const auto result = kernel->shell(source, open, 1);
  REQUIRE(result);
  // The kept faces' area is 9442.86 mm^2: a 1 mm wall a little under that, at its corners.
  // 9040.4529 by Nightfall's 1e-9 mass integrator; OCCT's default GProp gives 9040.4366
  // (1.8e-6 apart). The input it must not return is 69137.79.
  CHECK_THAT(result->shape->measures().volume, WithinRel(9040.4529, 1e-5));
}
