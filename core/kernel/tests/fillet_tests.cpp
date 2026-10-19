// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "nf/kernel/kernel.hpp"
#include "nf/math/constructors.hpp"
#include "snapshot.hpp"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_MapOfShape.hxx>
#include <TopoDS.hxx>

#include <algorithm>
#include <limits>
#include <numbers>
#include <sstream>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace nf::kernel;
using namespace nf::math;
using Catch::Matchers::WithinAbs;

namespace {
constexpr double pi = std::numbers::pi;
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
std::vector<EntityPtr> edges(const ShapePtr& source, bool vertical_only = false) {
  std::vector<EntityPtr> result;
  for (const auto& entity :
       detail::entities(std::dynamic_pointer_cast<const detail::Snapshot>(source))) {
    if (entity->kind() != EntityKind::kEdge) continue;
    const auto& e = static_cast<const detail::Subshape&>(*entity).geometry;
    const auto a = BRep_Tool::Pnt(TopExp::FirstVertex(TopoDS::Edge(e)));
    const auto b = BRep_Tool::Pnt(TopExp::LastVertex(TopoDS::Edge(e)));
    if (!vertical_only || (std::abs(a.X() - b.X()) < 1e-8 && std::abs(a.Y() - b.Y()) < 1e-8 &&
                           std::abs(a.Z() - b.Z()) > 29.999))
      result.push_back(entity);
  }
  return result;
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
  TopTools_MapOfShape covered;
  const auto before = detail::entities(std::dynamic_pointer_cast<const detail::Snapshot>(source));
  REQUIRE(result.history.size() == before.size());
  for (const auto& input : before)
    CHECK(std::ranges::count_if(result.history, [&](const auto& h) {
            return h.before && h.before->same_as(*input);
          }) == 1);
  for (const auto& h : result.history) {
    CHECK(h.before->owner() == source);
    CHECK(h.deleted == h.modified.empty());
    for (const auto& after : h.modified) CHECK(after->kind() == h.before->kind());
    for (const auto& list : {h.modified, h.generated})
      for (const auto& after : list) {
        CHECK(after->owner() == result.shape);
        const auto& geometry = static_cast<const detail::Subshape&>(*after).geometry;
        REQUIRE(members.Contains(geometry));
        CHECK(geometry.Orientation() == members(members.FindIndex(geometry)).Orientation());
        covered.Add(geometry);
      }
  }
  for (const auto& member : detail::entities(output))
    CHECK(covered.Contains(static_cast<const detail::Subshape&>(*member).geometry));
  for (const auto& g : result.generated) {
    CHECK(g.role == "fillet");
    CHECK(g.entity->kind() == EntityKind::kFace);
    CHECK(g.entity->owner() == result.shape);
    CHECK_FALSE(g.from);
    CHECK(covered.Contains(static_cast<const detail::Subshape&>(*g.entity).geometry));
  }
  CHECK(result.estimated_bytes >= source->estimated_bytes() + result.shape->estimated_bytes());
}
}  // namespace

TEST_CASE("constant-radius single edge fillets have exact measures and complete ancestry",
          "[fillet-kernel]") {
  auto kernel = make_occt_kernel().value();
  const auto source = box(*kernel);
  const auto saved = serialized(source.shape);
  const auto selected = edges(source.shape, true).front();
  const auto result = kernel->fillet(source.shape, {selected}, 2);
  INFO((result ? "success" : result.error().message));
  REQUIRE(result);
  const auto& m = result->shape->measures();
  CHECK_THAT(m.volume, WithinAbs(6000 - 120 * (1 - pi / 4), 1e-6));
  CHECK_THAT(m.area, WithinAbs(2072 + 32 * pi, 1e-6));
  CHECK(m.solids == 1);
  CHECK(m.faces == 7);
  CHECK(m.edges == 15);
  CHECK(result->generated.size() == 1);
  history(*result, source.shape);
  const auto& blended = *std::ranges::find_if(
      result->history, [&](const auto& h) { return h.before->same_as(*selected); });
  CHECK(blended.deleted);
  CHECK(std::ranges::count_if(blended.generated,
                              [](const auto& e) { return e->kind() == EntityKind::kFace; }) == 1);
  CHECK(std::ranges::count_if(blended.generated,
                              [](const auto& e) { return e->kind() == EntityKind::kEdge; }) == 4);
  CHECK(serialized(source.shape) == saved);
  const auto duplicate = kernel->fillet(source.shape, {selected, selected}, 2);
  REQUIRE(duplicate);
  CHECK_THAT(duplicate->shape->measures().volume, WithinAbs(m.volume, 1e-6));
  CHECK(kernel->capabilities().fillet);
  CHECK(kernel->identity().find("fillet=") != std::string_view::npos);
}

TEST_CASE("four vertical fillets preserve symmetric analytic geometry and rigid placements",
          "[fillet-kernel]") {
  auto kernel = make_occt_kernel().value();
  const auto source = box(*kernel);
  auto selected = edges(source.shape, true);
  REQUIRE(selected.size() == 4);
  auto input = source.shape;
  const bool placed = GENERATE(false, true);
  const double radius = GENERATE(0.125, 1., 2., 4.);
  if (placed) {
    const auto moved = kernel->transform(input, {{3, 4, 5}, {}, {0, 1, 0}, pi / 2}).value();
    for (auto& edge : selected)
      edge = std::ranges::find_if(moved.history, [&](const auto& h) {
               return h.before->same_as(*edge);
             })->modified.front();
    input = moved.shape;
  }
  const auto saved = serialized(input);
  const auto result = kernel->fillet(input, selected, radius);
  INFO((result ? "success" : result.error().message));
  REQUIRE(result);
  const double cross_section = 200 - (4 - pi) * radius * radius;
  const double perimeter = 60 - 8 * radius + 2 * pi * radius;
  const auto& m = result->shape->measures();
  CHECK_THAT(m.volume, WithinAbs(30 * cross_section, 1e-6));
  CHECK_THAT(m.area, WithinAbs(2 * cross_section + 30 * perimeter, 1e-6));
  CHECK_THAT(m.centroid.x, WithinAbs(placed ? 18 : 5, 1e-8));
  CHECK_THAT(m.centroid.y, WithinAbs(placed ? 14 : 10, 1e-8));
  CHECK_THAT(m.centroid.z, WithinAbs(placed ? 0 : 15, 1e-8));
  CHECK(m.faces == 10);
  CHECK(m.edges == 24);
  CHECK(result->generated.size() == 4);
  history(*result, input);
  CHECK(serialized(input) == saved);
}

TEST_CASE("all-edge fillets trace spherical vertex patches through incident blend edges",
          "[fillet-kernel]") {
  auto kernel = make_occt_kernel().value();
  const auto source = box(*kernel);
  const auto saved = serialized(source.shape);
  const auto result = kernel->fillet(source.shape, edges(source.shape), 1);
  INFO((result ? "success" : result.error().message));
  REQUIRE(result);
  const auto& m = result->shape->measures();
  // Inner 8x18x28 box, face slabs, twelve quarter cylinders and eight sphere octants.
  CHECK_THAT(m.volume, WithinAbs(4032 + 2 * 872 + 54 * pi + 4 * pi / 3, 1e-5));
  CHECK_THAT(m.area, WithinAbs(2 * 872 + 108 * pi + 4 * pi, 1e-5));
  CHECK_THAT(m.centroid.x, WithinAbs(5, 1e-8));
  CHECK_THAT(m.centroid.y, WithinAbs(10, 1e-8));
  CHECK_THAT(m.centroid.z, WithinAbs(15, 1e-8));
  CHECK(m.faces == 26);
  // Each spherical octant also has a degenerate parameter-pole edge.
  CHECK(m.edges == 56);
  CHECK(result->generated.size() == 20);
  history(*result, source.shape);
  std::size_t degenerate = 0, spheres = 0;
  for (const auto& e : edges(result->shape))
    degenerate +=
        BRep_Tool::Degenerated(TopoDS::Edge(static_cast<const detail::Subshape&>(*e).geometry));
  CHECK(degenerate == 8);
  for (const auto& generated : result->generated) {
    const auto& face = static_cast<const detail::Subshape&>(*generated.entity).geometry;
    if (BRepAdaptor_Surface(TopoDS::Face(face)).GetType() != GeomAbs_Sphere) continue;
    ++spheres;
    CHECK(std::ranges::count_if(result->history, [&](const auto& h) {
            return h.before->kind() == EntityKind::kEdge &&
                   std::ranges::any_of(
                       h.generated, [&](const auto& e) { return e->same_as(*generated.entity); });
          }) == 3);
  }
  CHECK(spheres == 8);
  CHECK(serialized(source.shape) == saved);
}

TEST_CASE("annular rim fillets propagate tangent contours with exact toroidal geometry",
          "[fillet-kernel]") {
  auto kernel = make_occt_kernel().value();
  std::vector<std::vector<NurbsCurve2>> paths{{make_circle(Vec2{}, 10)}, {make_circle(Vec2{}, 5)}};
  const auto source =
      kernel->extrude({{}, regions(paths, FillRule::kEvenOdd).value(), {0, 0, 30}}).value();
  auto boundary = kernel->edges(cap(source)).value();
  REQUIRE(boundary.size() == 8);
  if (GENERATE(false, true)) {
    std::vector<EntityPtr> partial;
    for (const double radius : {5., 10.})
      partial.push_back(*std::ranges::find_if(boundary, [&](const auto& e) {
        const auto& geometry = static_cast<const detail::Subshape&>(*e).geometry;
        BRepAdaptor_Curve curve(TopoDS::Edge(geometry));
        return curve.GetType() == GeomAbs_Circle &&
               std::abs(curve.Circle().Radius() - radius) < 1e-8;
      }));
    boundary = std::move(partial);
  }
  const auto saved = serialized(source.shape);
  const auto result = kernel->fillet(source.shape, boundary, 1);
  INFO((result ? "success" : result.error().message));
  REQUIRE(result);
  CHECK_THAT(result->shape->measures().volume, WithinAbs(2220 * pi + 7.5 * pi * pi, 1e-5));
  CHECK_THAT(result->shape->measures().area, WithinAbs(990 * pi + 15 * pi * pi, 1e-5));
  history(*result, source.shape);
  CHECK(serialized(source.shape) == saved);
}

TEST_CASE("fillets reject invalid radii and foreign or mismatched handles without mutation",
          "[fillet-kernel]") {
  auto kernel = make_occt_kernel().value();
  const auto source = box(*kernel);
  const auto saved = serialized(source.shape);
  const auto selected = edges(source.shape, true);
  CHECK_FALSE(kernel->fillet({}, selected, 1));
  CHECK_FALSE(kernel->fillet(source.shape, {}, 1));
  CHECK_FALSE(kernel->fillet(source.shape, {{}}, 1));
  CHECK_FALSE(kernel->fillet(source.shape, {cap(source)}, 1));
  CHECK_FALSE(kernel->fillet(box(*kernel).shape, selected, 1));
  struct ForeignShape final : Shape {
    Measures m;
    const Measures& measures() const noexcept override { return m; }
    std::size_t estimated_bytes() const noexcept override { return 0; }
  };
  struct ForeignEntity final : Entity {
    ShapePtr shape;
    ShapePtr owner() const noexcept override { return shape; }
    EntityKind kind() const noexcept override { return EntityKind::kEdge; }
    bool same_as(const Entity&) const noexcept override { return false; }
  };
  CHECK_FALSE(kernel->fillet(std::make_shared<ForeignShape>(), selected, 1));
  CHECK_FALSE(kernel->fillet(source.shape, {std::make_shared<ForeignEntity>()}, 1));
  auto forged = std::make_shared<detail::Subshape>();
  forged->snapshot = std::dynamic_pointer_cast<const detail::Snapshot>(source.shape);
  forged->geometry =
      static_cast<const detail::Subshape&>(*edges(box(*kernel).shape).front()).geometry;
  CHECK_FALSE(kernel->fillet(source.shape, {forged}, 1));
  for (const double radius : {0., -1., 1e-8, 100., std::numeric_limits<double>::quiet_NaN(),
                              std::numeric_limits<double>::infinity()}) {
    const auto result = kernel->fillet(source.shape, selected, radius);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == "kernel");
    CHECK(serialized(source.shape) == saved);
  }
}

TEST_CASE("fillet snapshots and complete history survive facade and source scope teardown",
          "[fillet-kernel]") {
  OperationResult retained;
  {
    auto kernel = make_occt_kernel().value();
    const auto source = box(*kernel);
    retained = kernel->fillet(source.shape, edges(source.shape, true), 2).value();
  }
  CHECK_THAT(retained.shape->measures().volume, WithinAbs(30 * (200 - 4 * (4 - pi)), 1e-6));
  history(retained, retained.history.front().before->owner());
  auto kernel = make_occt_kernel().value();
  CHECK(kernel->export_step({retained.shape}));
}
