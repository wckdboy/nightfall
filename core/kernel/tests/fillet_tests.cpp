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
EntityPtr cap(const OperationResult& input, std::string_view role = "cap_end") {
  return std::ranges::find_if(input.generated, [&](const auto& g) { return g.role == role; })
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
EntityPtr current_face(const OperationResult& result, const EntityPtr& before) {
  const auto found = std::ranges::find_if(
      result.history, [&](const auto& h) { return h.before->same_as(*before); });
  REQUIRE(found != result.history.end());
  REQUIRE_FALSE(found->deleted);
  REQUIRE(found->modified.size() == 1);
  return found->modified.front();
}
EntityPtr straight_edge(const ShapePtr& source, Vec3 a, Vec3 b) {
  std::vector<EntityPtr> matches;
  for (const auto& edge : edges(source)) {
    const auto& e = static_cast<const detail::Subshape&>(*edge).geometry;
    BRepAdaptor_Curve curve(TopoDS::Edge(e));
    if (curve.GetType() != GeomAbs_Line) continue;
    const auto p = curve.Value(curve.FirstParameter());
    const auto q = curve.Value(curve.LastParameter());
    auto near = [](const gp_Pnt& x, Vec3 y) { return x.Distance(gp_Pnt{y.x, y.y, y.z}) < 1e-9; };
    if ((near(p, a) && near(q, b)) || (near(p, b) && near(q, a))) matches.push_back(edge);
  }
  REQUIRE(matches.size() == 1);
  return matches.front();
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

TEST_CASE(
    "sequential corner and rim fillets preserve exact Steiner measures and every prior snapshot",
    "[fillet-kernel]") {
  auto kernel = make_occt_kernel().value();
  const auto source = box(*kernel);
  const auto saved = serialized(source.shape);
  const auto corners = kernel->fillet(source.shape, edges(source.shape, true), 2).value();
  const auto saved_corners = serialized(corners.shape);
  const auto top = current_face(corners, cap(source));
  const auto bottom = current_face(corners, cap(source, "cap_start"));
  const auto top_result = kernel->fillet(corners.shape, kernel->edges(top).value(), 1);
  INFO((top_result ? "success" : top_result.error().message));
  REQUIRE(top_result);
  // A convex rounded rectangle has inner-parallel perimeter P - 2*pi*u. Integrate the
  // removed quarter-corner and its first moment; both top/bottom use the same exact loss.
  const double cross_section = 200 - 4 * (4 - pi), perimeter = 44 + 4 * pi;
  const double loss = perimeter * (1 - pi / 4) - 2 * pi * (5. / 6 - pi / 4);
  const double area_change = (pi / 2 - 2) * perimeter + 3 * pi - pi * pi;
  CHECK_THAT(top_result->shape->measures().volume, WithinAbs(30 * cross_section - loss, 1e-5));
  CHECK_THAT(top_result->shape->measures().area,
             WithinAbs(2 * cross_section + 30 * perimeter + area_change, 1e-5));
  CHECK(top_result->shape->measures().faces == 18);
  history(*top_result, corners.shape);
  const auto saved_top = serialized(top_result->shape);
  const auto final = kernel->fillet(top_result->shape,
                                    kernel->edges(current_face(*top_result, bottom)).value(), 1);
  INFO((final ? "success" : final.error().message));
  REQUIRE(final);
  const auto& m = final->shape->measures();
  CHECK_THAT(m.volume, WithinAbs(30 * cross_section - 2 * loss, 1e-5));
  CHECK_THAT(m.area, WithinAbs(2 * cross_section + 30 * perimeter + 2 * area_change, 1e-5));
  CHECK_THAT(m.centroid.x, WithinAbs(5, 1e-8));
  CHECK_THAT(m.centroid.y, WithinAbs(10, 1e-8));
  CHECK_THAT(m.centroid.z, WithinAbs(15, 1e-8));
  CHECK(m.faces == 26);
  history(*final, top_result->shape);
  CHECK(serialized(source.shape) == saved);
  CHECK(serialized(corners.shape) == saved_corners);
  CHECK(serialized(top_result->shape) == saved_top);
}

TEST_CASE("concave reflex fillets add analytic material with complete oriented ancestry",
          "[fillet-kernel]") {
  auto kernel = make_occt_kernel().value();
  const std::vector<Vec2> points{{0, 0}, {10, 0}, {10, 4}, {4, 4}, {4, 10}, {0, 10}};
  std::vector<NurbsCurve2> loop;
  for (std::size_t i = 0; i < points.size(); ++i)
    loop.push_back(make_line(points[i], points[(i + 1) % points.size()]));
  const std::vector<std::vector<NurbsCurve2>> paths{std::move(loop)};
  const auto source =
      kernel->extrude({{}, regions(paths, FillRule::kEvenOdd).value(), {0, 0, 10}}).value();
  const auto selected = straight_edge(source.shape, {4, 4, 0}, {4, 4, 10});
  const auto saved = serialized(source.shape);
  for (const double radius : {0.125, 1., 2., 4.}) {
    CAPTURE(radius);
    const auto result = kernel->fillet(source.shape, {selected}, radius);
    INFO((result ? "success" : result.error().message));
    REQUIRE(result);
    const double added = radius * radius * (1 - pi / 4);
    const double moment = radius * radius * radius * (5. / 6 - pi / 4);
    const double cross_section = 64 + added;
    const double perimeter = 40 - 2 * radius + pi * radius / 2;
    const auto& m = result->shape->measures();
    CHECK_THAT(m.volume, WithinAbs(10 * cross_section, 1e-6));
    CHECK_THAT(m.area, WithinAbs(2 * cross_section + 10 * perimeter, 1e-6));
    CHECK_THAT(m.centroid.x, WithinAbs((248 + 4 * added + moment) / cross_section, 1e-8));
    CHECK_THAT(m.centroid.y, WithinAbs(m.centroid.x, 1e-8));
    CHECK_THAT(m.centroid.z, WithinAbs(5, 1e-8));
    CHECK(m.faces == 9);
    CHECK(m.edges == 21);
    history(*result, source.shape);
    CHECK(serialized(source.shape) == saved);
  }
}

TEST_CASE("fillets close to a through hole preserve its exact circular wall and immutable history",
          "[fillet-kernel]") {
  // Geometry adapted from SolidCraft issue_324_tests.rs at 4c30d944 (MIT OR Apache-2.0).
  // Original exact C++ oracles; this tests positive clearance, not automatic radius reduction.
  auto kernel = make_occt_kernel().value();
  std::vector<std::vector<NurbsCurve2>> paths{
      {make_line(Vec2{}, {40, 0}), make_line(Vec2{40, 0}, {40, 20}),
       make_line(Vec2{40, 20}, {0, 20}), make_line(Vec2{0, 20}, {})},
      {make_circle(Vec2{20, 14.5}, 3)}};
  const auto source =
      kernel->extrude({{}, regions(paths, FillRule::kEvenOdd).value(), {0, 0, 10}}).value();
  const auto selected = straight_edge(source.shape, {0, 20, 10}, {40, 20, 10});
  const auto saved = serialized(source.shape);
  for (const double radius : {1., 2., 2.4}) {
    CAPTURE(radius);
    const auto result = kernel->fillet(source.shape, {selected}, radius);
    INFO((result ? "success" : result.error().message));
    REQUIRE(result);
    const double corner = (1 - pi / 4) * radius * radius;
    const auto& m = result->shape->measures();
    CHECK_THAT(m.volume, WithinAbs(8000 - 90 * pi - 40 * corner, 1e-5));
    CHECK_THAT(m.area,
               WithinAbs(2800 + 42 * pi - 80 * radius + 20 * pi * radius - 2 * corner, 1e-5));
    CHECK_THAT(m.centroid.x, WithinAbs(20, 1e-8));
    history(*result, source.shape);
    const auto faces = kernel->faces(result->shape).value();
    CHECK(std::ranges::count_if(faces, [](const auto& f) {
            return f.cylinder && std::abs(f.cylinder->radius - 3) < 1e-8;
          }) == 4);
    CHECK(serialized(source.shape) == saved);
  }
  CHECK_FALSE(kernel->fillet(source.shape, {selected}, 100));
  CHECK(serialized(source.shape) == saved);
  CHECK(kernel->fillet(source.shape, {selected}, 2));
}
