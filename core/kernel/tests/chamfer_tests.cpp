// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "nf/kernel/kernel.hpp"
#include "nf/math/constructors.hpp"
#include "snapshot.hpp"

#include <BRepTools.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>

#include <algorithm>
#include <limits>
#include <numbers>
#include <sstream>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace nf::kernel;
using namespace nf::math;
using Catch::Matchers::WithinAbs;

namespace {
OperationResult box(IKernel& kernel) {
  std::vector<std::vector<NurbsCurve2>> paths{
      {make_line(Vec2{}, {10, 0}), make_line(Vec2{10, 0}, {10, 20}),
       make_line(Vec2{10, 20}, {0, 20}), make_line(Vec2{0, 20}, {})}};
  return kernel.extrude({{}, regions(paths, FillRule::kEvenOdd).value(), {0, 0, 30}}).value();
}
EntityPtr cap(const OperationResult& input) {
  return std::ranges::find_if(input.generated, [](const auto& g) { return g.role == "cap_end"; })
      ->entity;
}
std::string serialized(const ShapePtr& shape) {
  std::ostringstream stream;
  BRepTools::Write(std::dynamic_pointer_cast<const detail::Snapshot>(shape)->geometry, stream);
  return stream.str();
}
void history(const OperationResult& result, const ShapePtr& source) {
  const auto output = std::dynamic_pointer_cast<const detail::Snapshot>(result.shape);
  TopTools_IndexedMapOfShape members;
  TopExp::MapShapes(output->geometry, members);
  const auto before = detail::entities(std::dynamic_pointer_cast<const detail::Snapshot>(source));
  REQUIRE(result.history.size() == before.size());
  for (const auto& input : before)
    CHECK(std::ranges::count_if(result.history, [&](const auto& h) {
            return h.before && h.before->same_as(*input);
          }) == 1);
  for (const auto& h : result.history) {
    CHECK(h.before->owner() == source);
    CHECK((h.deleted || !h.modified.empty()));
    for (const auto& list : {h.modified, h.generated})
      for (const auto& after : list) {
        CHECK(after->owner() == result.shape);
        const auto& geometry = static_cast<const detail::Subshape&>(*after).geometry;
        REQUIRE(members.Contains(geometry));
        CHECK(geometry.Orientation() == members(members.FindIndex(geometry)).Orientation());
      }
  }
  for (const auto& g : result.generated) {
    CHECK(g.role == "chamfer");
    CHECK(g.entity->kind() == EntityKind::kFace);
    CHECK(g.entity->owner() == result.shape);
    CHECK_FALSE(g.from);
  }
  CHECK(result.estimated_bytes >= source->estimated_bytes() + result.shape->estimated_bytes());
}
}  // namespace

TEST_CASE("equal-distance chamfers have analytic measures topology and complete immutable history",
          "[chamfer-kernel]") {
  auto kernel = make_occt_kernel().value();
  const auto source = box(*kernel);
  const auto saved = serialized(source.shape);
  auto boundary = kernel->edges(cap(source));
  REQUIRE(boundary);
  CHECK(boundary->size() == 4);
  for (const auto& edge : *boundary) {
    CHECK(edge->owner() == source.shape);
    CHECK(edge->kind() == EntityKind::kEdge);
  }
  const auto result = kernel->chamfer(source.shape, *boundary, 1);
  REQUIRE(result);
  const auto& m = result->shape->measures();
  CHECK_THAT(m.volume, WithinAbs(6000 - 86. / 3, 1e-6));
  CHECK_THAT(m.area, WithinAbs(2084 + 56 * std::sqrt(2.), 1e-6));
  CHECK_THAT(m.centroid.x, WithinAbs(5, 1e-8));
  CHECK_THAT(m.centroid.y, WithinAbs(10, 1e-8));
  CHECK_THAT(m.centroid.z, WithinAbs((90000 - 860 + 29. / 3) / m.volume, 1e-8));
  CHECK(m.solids == 1);
  CHECK(m.faces == 10);
  CHECK(m.edges == 20);
  CHECK(result->generated.size() == 4);
  history(*result, source.shape);
  CHECK(serialized(source.shape) == saved);
  boundary->push_back(boundary->front());
  const auto duplicate = kernel->chamfer(source.shape, *boundary, 1);
  REQUIRE(duplicate);
  CHECK_THAT(duplicate->shape->measures().volume, WithinAbs(m.volume, 1e-6));
  CHECK(kernel->capabilities().chamfer);
}

TEST_CASE("chamfers honor current rigid placements and surviving face ancestry",
          "[chamfer-kernel]") {
  auto kernel = make_occt_kernel().value();
  const auto source = box(*kernel);
  const auto placed =
      kernel->transform(source.shape, {{3, 4, 5}, {}, {0, 1, 0}, std::numbers::pi / 2}).value();
  const auto face = std::ranges::find_if(placed.history, [&](const auto& h) {
                      return h.before->same_as(*cap(source));
                    })->modified.front();
  const auto result = kernel->chamfer(placed.shape, kernel->edges(face).value(), 1);
  REQUIRE(result);
  const double z = (90000 - 860 + 29. / 3) / (6000 - 86. / 3);
  const auto& m = result->shape->measures();
  CHECK_THAT(m.volume, WithinAbs(6000 - 86. / 3, 1e-6));
  CHECK_THAT(m.centroid.x, WithinAbs(3 + z, 1e-8));
  CHECK_THAT(m.centroid.y, WithinAbs(14, 1e-8));
  CHECK_THAT(m.centroid.z, WithinAbs(0, 1e-8));
  history(*result, placed.shape);
}

TEST_CASE("face boundary queries include holes and chamfers preserve exact circular boundaries",
          "[chamfer-kernel]") {
  auto kernel = make_occt_kernel().value();
  std::vector<std::vector<NurbsCurve2>> paths{{make_circle(Vec2{}, 10)}, {make_circle(Vec2{}, 5)}};
  const auto source =
      kernel->extrude({{}, regions(paths, FillRule::kEvenOdd).value(), {0, 0, 30}}).value();
  const auto edges = kernel->edges(cap(source)).value();
  CHECK(edges.size() == 8);  // four analytic quadrants per circular boundary
  const auto saved = serialized(source.shape);
  const auto result = kernel->chamfer(source.shape, edges, 1);
  REQUIRE(result);
  // Both top rims recede by 1 mm: outer conical and inner conical annular bands.
  const double pi = std::numbers::pi;
  CHECK_THAT(result->shape->measures().volume, WithinAbs(2235 * pi, 1e-5));
  CHECK_THAT(result->shape->measures().area, WithinAbs((990 + 30 * std::sqrt(2.)) * pi, 1e-5));
  history(*result, source.shape);
  CHECK(serialized(source.shape) == saved);
}

TEST_CASE("chamfers and boundary queries reject invalid foreign or mismatched handles",
          "[chamfer-kernel]") {
  auto kernel = make_occt_kernel().value();
  const auto source = box(*kernel);
  const auto saved = serialized(source.shape);
  const auto edges = kernel->edges(cap(source)).value();
  CHECK_FALSE(kernel->edges({}));
  CHECK_FALSE(kernel->edges(edges.front()));
  CHECK_FALSE(kernel->chamfer({}, edges, 1));
  CHECK_FALSE(kernel->chamfer(source.shape, {}, 1));
  CHECK_FALSE(kernel->chamfer(source.shape, {cap(source)}, 1));
  CHECK_FALSE(kernel->chamfer(source.shape, {{}}, 1));
  CHECK_FALSE(kernel->chamfer(box(*kernel).shape, edges, 1));
  struct ForeignShape final : Shape {
    Measures m;
    const Measures& measures() const noexcept override { return m; }
    std::size_t estimated_bytes() const noexcept override { return 0; }
  };
  struct ForeignEntity final : Entity {
    ShapePtr shape;
    ShapePtr owner() const noexcept override { return shape; }
    EntityKind kind() const noexcept override { return EntityKind::kFace; }
    bool same_as(const Entity&) const noexcept override { return false; }
  };
  CHECK_FALSE(kernel->chamfer(std::make_shared<ForeignShape>(), edges, 1));
  CHECK_FALSE(kernel->edges(std::make_shared<ForeignEntity>()));
  CHECK_FALSE(kernel->chamfer(source.shape, {std::make_shared<ForeignEntity>()}, 1));
  for (const double distance : {0., -1., 1e-8, 100., std::numeric_limits<double>::quiet_NaN(),
                                std::numeric_limits<double>::infinity()}) {
    CHECK_FALSE(kernel->chamfer(source.shape, edges, distance));
    CHECK(serialized(source.shape) == saved);
  }
}

TEST_CASE("chamfer snapshots history and boundary handles survive their facade",
          "[chamfer-kernel]") {
  OperationResult retained;
  EntityPtr face;
  std::vector<EntityPtr> edges;
  {
    auto kernel = make_occt_kernel().value();
    const auto source = box(*kernel);
    face = cap(source);
    edges = kernel->edges(face).value();
    retained = kernel->chamfer(source.shape, edges, 1).value();
  }
  CHECK_THAT(retained.shape->measures().volume, WithinAbs(6000 - 86. / 3, 1e-6));
  history(retained, face->owner());
  auto kernel = make_occt_kernel().value();
  CHECK(kernel->edges(face).value().size() == 4);
  CHECK(kernel->chamfer(face->owner(), edges, 1));
}
