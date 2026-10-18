// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "nf/kernel/kernel.hpp"
#include "nf/math/constructors.hpp"
#include "snapshot.hpp"

#include <BRepTools.hxx>

#include <algorithm>
#include <numbers>
#include <sstream>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace nf::kernel;
using namespace nf::math;
using Catch::Matchers::WithinAbs;

namespace {
Extrusion box_profile() {
  const std::vector<std::vector<NurbsCurve2>> paths{
      {make_line(Vec2{}, {10, 0}), make_line(Vec2{10, 0}, {10, 20}),
       make_line(Vec2{10, 20}, {0, 20}), make_line(Vec2{0, 20}, {})}};
  return {{}, regions(paths, FillRule::kEvenOdd).value(), {0, 0, 30}};
}
EntityPtr cap(const OperationResult& result, const std::string& role) {
  return std::ranges::find_if(result.generated, [&](const auto& g) { return g.role == role; })
      ->entity;
}
EntityPtr modified(const OperationResult& result, const EntityPtr& before) {
  return std::ranges::find_if(result.history,
                              [&](const auto& h) { return h.before->same_as(*before); })
      ->modified.front();
}
std::string serialized(const ShapePtr& shape) {
  std::ostringstream stream;
  BRepTools::Write(std::dynamic_pointer_cast<const detail::Snapshot>(shape)->geometry, stream);
  return stream.str();
}
void frame(const Plane& plane, Vec3 point, Vec3 normal) {
  CHECK_THAT(length(plane.normal - normal), WithinAbs(0, 1e-10));
  CHECK_THAT(length(plane.normal), WithinAbs(1, 1e-10));
  CHECK_THAT(length(plane.x_axis), WithinAbs(1, 1e-10));
  CHECK_THAT(dot(plane.normal, plane.x_axis), WithinAbs(0, 1e-10));
  CHECK_THAT(dot(plane.origin - point, normal), WithinAbs(0, 1e-8));
}
}  // namespace

TEST_CASE("planar face queries return outward cap frames without changing snapshots",
          "[face-plane-kernel]") {
  auto kernel = make_occt_kernel().value();
  for (const double sign : {-1.0, 1.0}) {
    auto input = box_profile();
    input.plane = {{7, -3, 11}, {0, 1, 0}, {1, 0, 0}};
    input.displacement = {sign * 30, 0, 0};
    const auto solid = kernel->extrude(input).value();
    const auto saved = serialized(solid.shape);
    const auto start = kernel->face_plane(cap(solid, "cap_start"));
    const auto end = kernel->face_plane(cap(solid, "cap_end"));
    REQUIRE(start);
    REQUIRE(end);
    frame(*start, input.plane.origin, {-sign, 0, 0});
    frame(*end, input.plane.origin + input.displacement, {sign, 0, 0});
    CHECK(serialized(solid.shape) == saved);
  }
}

TEST_CASE("face plane queries honor rigid locations and reflected handedness",
          "[face-plane-kernel]") {
  auto kernel = make_occt_kernel().value();
  const auto source = kernel->extrude(box_profile()).value();
  const auto saved = serialized(source.shape);
  const auto moved =
      kernel->transform(source.shape, {{7, -2, 11}, {}, {0, 1, 0}, std::numbers::pi / 2}).value();
  const auto top = modified(moved, cap(source, "cap_end"));
  const auto plane = kernel->face_plane(top);
  REQUIRE(plane);
  frame(*plane, {37, -2, 11}, {1, 0, 0});
  CHECK_THAT(length(plane->x_axis - Vec3{0, 0, -1}), WithinAbs(0, 1e-10));
  const auto reflected = kernel->mirror(moved.shape, {{}, {0, 1, 0}, {1, 0, 0}}).value();
  const auto mirror_plane = kernel->face_plane(modified(reflected, top));
  REQUIRE(mirror_plane);
  frame(*mirror_plane, {-37, -2, 11}, {-1, 0, 0});
  CHECK(serialized(source.shape) == saved);
}

TEST_CASE("face plane queries use reversed cutting tool face orientation", "[face-plane-kernel]") {
  auto kernel = make_occt_kernel().value();
  const auto source = kernel->extrude(box_profile()).value();
  auto input = box_profile();
  input.plane.origin = {2, 2, 2};
  input.displacement = {0, 0, 3};
  const auto tool = kernel->extrude(input).value();
  const auto cut =
      kernel->boolean_operation(source.shape, {tool.shape}, BooleanOp::kSubtract).value();
  const auto plane = kernel->face_plane(modified(cut, cap(tool, "cap_end")));
  REQUIRE(plane);
  frame(*plane, {2, 2, 5}, {0, 0, -1});
}

TEST_CASE("face plane queries reject curved faces edges null and foreign handles",
          "[face-plane-kernel]") {
  auto kernel = make_occt_kernel().value();
  auto input = box_profile();
  const std::vector<std::vector<NurbsCurve2>> paths{{make_circle(Vec2{}, 2)}};
  input.regions = regions(paths, FillRule::kEvenOdd).value();
  const auto cylinder = kernel->extrude(input).value();
  CHECK_FALSE(kernel->face_plane({}));
  CHECK_FALSE(kernel->face_plane(cap(cylinder, "side")));
  REQUIRE(kernel->face_plane(cap(cylinder, "cap_end")));
  const auto all =
      detail::entities(std::dynamic_pointer_cast<const detail::Snapshot>(cylinder.shape));
  const auto edge =
      std::ranges::find_if(all, [](const auto& e) { return e->kind() == EntityKind::kEdge; });
  REQUIRE(edge != all.end());
  CHECK_FALSE(kernel->face_plane(*edge));
  struct ForeignEntity final : Entity {
    ShapePtr owner() const noexcept override { return {}; }
    EntityKind kind() const noexcept override { return EntityKind::kFace; }
    bool same_as(const Entity& other) const noexcept override { return this == &other; }
  };
  CHECK_FALSE(kernel->face_plane(std::make_shared<ForeignEntity>()));
}

TEST_CASE("face plane handles remain queryable after their producing facade ends",
          "[face-plane-kernel]") {
  EntityPtr retained;
  {
    auto kernel = make_occt_kernel().value();
    retained = cap(kernel->extrude(box_profile()).value(), "cap_end");
  }
  auto reader = make_occt_kernel().value();
  const auto plane = reader->face_plane(retained);
  REQUIRE(plane);
  frame(*plane, {0, 0, 30}, {0, 0, 1});
  CHECK_THAT(retained->owner()->measures().volume, WithinAbs(6000, 1e-7));
}

TEST_CASE("face enumeration returns all unique snapshot-owned faces and exact planar frames",
          "[face-query-kernel]") {
  auto kernel = make_occt_kernel().value();
  const auto solid = kernel->extrude(box_profile()).value();
  const auto saved = serialized(solid.shape);
  const auto queried = kernel->faces(solid.shape);
  REQUIRE(queried);
  REQUIRE(queried->size() == 6);
  CHECK(queried->size() == solid.shape->measures().faces);
  for (std::size_t i = 0; i < queried->size(); ++i) {
    const auto& face = (*queried)[i];
    REQUIRE(face.entity);
    CHECK(face.entity->kind() == EntityKind::kFace);
    CHECK(face.entity->owner() == solid.shape);
    REQUIRE(face.plane);
    const auto direct = kernel->face_plane(face.entity).value();
    frame(*face.plane, direct.origin, direct.normal);
    CHECK_THAT(length(face.plane->x_axis - direct.x_axis), WithinAbs(0, 1e-10));
    for (std::size_t j = 0; j < i; ++j) CHECK_FALSE(face.entity->same_as(*(*queried)[j].entity));
  }
  for (const auto& generated : solid.generated)
    CHECK(std::ranges::count_if(*queried, [&](const auto& face) {
            return face.entity->same_as(*generated.entity);
          }) == 1);
  CHECK(serialized(solid.shape) == saved);
}

TEST_CASE("face enumeration keeps curved faces without inventing planar frames",
          "[face-query-kernel]") {
  auto kernel = make_occt_kernel().value();
  auto input = box_profile();
  const std::vector<std::vector<NurbsCurve2>> paths{{make_circle(Vec2{}, 2)}};
  input.regions = regions(paths, FillRule::kEvenOdd).value();
  const auto cylinder = kernel->extrude(input).value();
  const auto saved = serialized(cylinder.shape);
  const auto queried = kernel->faces(cylinder.shape).value();
  CHECK(queried.size() == cylinder.shape->measures().faces);
  CHECK(std::ranges::count_if(queried, [](const auto& f) { return f.plane.has_value(); }) == 2);
  CHECK(std::ranges::count_if(queried, [](const auto& f) { return !f.plane; }) == 4);
  for (const auto& face : queried) {
    CHECK(face.entity->owner() == cylinder.shape);
    CHECK(kernel->face_plane(face.entity).has_value() == face.plane.has_value());
  }
  CHECK(serialized(cylinder.shape) == saved);
}

TEST_CASE("enumerated frames follow rigid reflection and reversed cavity orientation",
          "[face-query-kernel]") {
  auto kernel = make_occt_kernel().value();
  const auto source = kernel->extrude(box_profile()).value();
  const auto moved =
      kernel->transform(source.shape, {{7, -2, 11}, {}, {0, 1, 0}, std::numbers::pi / 2}).value();
  const auto top = modified(moved, cap(source, "cap_end"));
  const auto reflected = kernel->mirror(moved.shape, {{}, {0, 1, 0}, {1, 0, 0}}).value();
  const auto mirror_top = modified(reflected, top);
  const auto faces = kernel->faces(reflected.shape).value();
  const auto found = std::ranges::find_if(
      faces, [&](const auto& face) { return face.entity->same_as(*mirror_top); });
  REQUIRE(found != faces.end());
  REQUIRE(found->plane);
  frame(*found->plane, {-37, -2, 11}, {-1, 0, 0});
  CHECK_THAT(length(found->plane->x_axis - Vec3{0, 0, -1}), WithinAbs(0, 1e-10));
  auto input = box_profile();
  input.plane.origin = {2, 2, 2};
  input.displacement = {0, 0, 3};
  const auto tool = kernel->extrude(input).value();
  const auto cut =
      kernel->boolean_operation(source.shape, {tool.shape}, BooleanOp::kSubtract).value();
  const auto bottom = modified(cut, cap(tool, "cap_end"));
  const auto cavity = kernel->faces(cut.shape).value();
  const auto reversed =
      std::ranges::find_if(cavity, [&](const auto& face) { return face.entity->same_as(*bottom); });
  REQUIRE(reversed != cavity.end());
  REQUIRE(reversed->plane);
  frame(*reversed->plane, {2, 2, 5}, {0, 0, -1});
}

TEST_CASE("face enumeration includes all separated solid components", "[face-query-kernel]") {
  auto kernel = make_occt_kernel().value();
  const auto source = kernel->extrude(box_profile()).value();
  const auto moved = kernel->transform(source.shape, {{20, 0, 0}, {}, {0, 0, 1}, 0}).value();
  const auto both =
      kernel->boolean_operation(source.shape, {moved.shape}, BooleanOp::kUnion).value();
  const auto faces = kernel->faces(both.shape).value();
  CHECK(faces.size() == 12);
  CHECK(std::ranges::count_if(faces, [](const auto& face) {
          return face.plane && dot(face.plane->normal, Vec3{0, 0, 1}) > 0.5;
        }) == 2);
  CHECK_THAT(both.shape->measures().volume, WithinAbs(12000, 1e-7));
  CHECK_THAT(both.shape->measures().area, WithinAbs(4400, 1e-7));
}

TEST_CASE("face enumeration rejects null foreign and empty snapshots", "[face-query-kernel]") {
  auto kernel = make_occt_kernel().value();
  CHECK_FALSE(kernel->faces({}));
  struct ForeignShape final : Shape {
    Measures m;
    const Measures& measures() const noexcept override { return m; }
    std::size_t estimated_bytes() const noexcept override { return 0; }
  };
  CHECK_FALSE(kernel->faces(std::make_shared<ForeignShape>()));
  CHECK_FALSE(kernel->faces(std::make_shared<detail::Snapshot>()));
}

TEST_CASE("enumerated face handles retain geometry after query and facade teardown",
          "[face-query-kernel]") {
  EntityPtr retained;
  {
    auto kernel = make_occt_kernel().value();
    const auto faces = kernel->faces(kernel->extrude(box_profile())->shape).value();
    retained = std::ranges::find_if(faces, [](const auto& face) {
                 return face.plane && dot(face.plane->normal, Vec3{0, 0, 1}) > 0.5;
               })->entity;
  }
  auto reader = make_occt_kernel().value();
  frame(reader->face_plane(retained).value(), {0, 0, 30}, {0, 0, 1});
  CHECK(reader->faces(retained->owner())->size() == 6);
  CHECK_THAT(retained->owner()->measures().volume, WithinAbs(6000, 1e-7));
}
