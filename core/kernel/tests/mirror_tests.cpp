// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "nf/kernel/kernel.hpp"
#include "nf/math/constructors.hpp"
#include "snapshot.hpp"

#include <BRepAdaptor_Surface.hxx>
#include <BRepTools.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>

#include <algorithm>
#include <limits>
#include <numbers>
#include <random>
#include <sstream>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace nf::kernel;
using namespace nf::math;
using Catch::Matchers::WithinAbs;

namespace {
ShapePtr box(IKernel& kernel, Vec3 origin = {9, -1, 4}, Vec3 size = {2, 3, 5}) {
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
Vec3 reflected(Vec3 p, const Plane& plane) {
  return p - plane.normal * (2 * dot(p - plane.origin, plane.normal));
}
void measures(const ShapePtr& shape, double volume, double area, Vec3 center,
              std::size_t solids = 1) {
  const auto& m = shape->measures();
  CHECK_THAT(m.volume, WithinAbs(volume, 1e-7));
  CHECK_THAT(m.area, WithinAbs(area, 1e-7));
  CHECK_THAT(m.centroid.x, WithinAbs(center.x, 1e-8));
  CHECK_THAT(m.centroid.y, WithinAbs(center.y, 1e-8));
  CHECK_THAT(m.centroid.z, WithinAbs(center.z, 1e-8));
  CHECK(m.solids == solids);
}
void history(const OperationResult& result, const ShapePtr& source) {
  const auto output = std::dynamic_pointer_cast<const detail::Snapshot>(result.shape);
  TopTools_IndexedMapOfShape members;
  TopExp::MapShapes(output->geometry, members);
  const auto before = detail::entities(std::dynamic_pointer_cast<const detail::Snapshot>(source));
  REQUIRE(result.history.size() == before.size());
  for (const auto& entity : before)
    CHECK(std::ranges::count_if(result.history, [&](const auto& h) {
            return h.before && h.before->same_as(*entity);
          }) == 1);
  for (const auto& h : result.history) {
    CHECK(h.before->owner() == source);
    CHECK_FALSE(h.deleted);
    REQUIRE(h.modified.size() == 1);
    const auto& after = h.modified.front();
    CHECK(after->owner() == result.shape);
    CHECK(after->kind() == h.before->kind());
    const auto& geometry = static_cast<const detail::Subshape&>(*after).geometry;
    REQUIRE(members.Contains(geometry));
    CHECK(geometry.Orientation() == members(members.FindIndex(geometry)).Orientation());
  }
  CHECK(result.generated.empty());
  CHECK(result.estimated_bytes >= source->estimated_bytes() + result.shape->estimated_bytes());
}
}  // namespace

TEST_CASE("exact mirrors preserve oriented solids history and immutable source geometry",
          "[mirror-kernel]") {
  auto kernel = make_occt_kernel().value();
  const auto source = box(*kernel);
  const auto saved = serialized(source);
  const double r = std::sqrt(0.5);
  for (const auto& plane :
       {Plane{}, Plane{{}, {0, 1, 0}, {1, 0, 0}}, Plane{{}, {0, 0, 1}, {0, 1, 0}},
        Plane{{7, -3, 11}, {r, -r, 0}, {r, r, 0}}}) {
    const auto result = kernel->mirror(source, plane);
    REQUIRE(result);
    measures(result->shape, 30, 62, reflected({10, 0.5, 6.5}, plane));
    CHECK(result->shape->measures().faces == 6);
    CHECK(result->shape->measures().edges == 12);
    history(*result, source);
    const auto twice = kernel->mirror(result->shape, plane);
    REQUIRE(twice);
    measures(twice->shape, 30, 62, {10, 0.5, 6.5});
    history(*twice, result->shape);
    CHECK(serialized(source) == saved);
  }
  REQUIRE(kernel->capabilities().mirror);
}

TEST_CASE("mirror placement matches analytic reflection across randomized planes",
          "[mirror-kernel]") {
  auto kernel = make_occt_kernel().value();
  const auto source = box(*kernel);
  std::mt19937_64 rng(20261018);
  std::uniform_real_distribution<double> sample(-10, 10);
  for (int i = 0; i < 32; ++i) {
    const auto n =
        normalized(Vec3{sample(rng), sample(rng), sample(rng)}, Tolerance::standard()).value();
    const auto x = normalized(cross(n, std::abs(n.z) < 0.9 ? Vec3{0, 0, 1} : Vec3{1, 0, 0}),
                              Tolerance::standard())
                       .value();
    const Plane plane{{sample(rng), sample(rng), sample(rng)}, x, n};
    const auto result = kernel->mirror(source, plane);
    REQUIRE(result);
    measures(result->shape, 30, 62, reflected({10, 0.5, 6.5}, plane));
    history(*result, source);
  }
}

TEST_CASE("mirrors reverse a chiral arrangement while preserving every component volume",
          "[mirror-kernel]") {
  auto kernel = make_occt_kernel().value();
  const auto source = kernel
                          ->boolean_operation(box(*kernel),
                                              {box(*kernel, {15, 2, 7}, {1, 1, 1}),
                                               box(*kernel, {6, 7, 12}, {1, 2, 1}),
                                               box(*kernel, {11, 4, 20}, {1, 3, 1})},
                                              BooleanOp::kUnion)
                          .value()
                          .shape;
  const auto saved = serialized(source);
  const Plane plane{{3, 0, 0}, {0, 1, 0}, {1, 0, 0}};
  const auto result = kernel->mirror(source, plane);
  REQUIRE(result);
  CHECK(result->shape->measures().solids == 4);
  const auto output = std::dynamic_pointer_cast<const detail::Snapshot>(result->shape);
  std::vector<double> volumes;
  for (TopExp_Explorer e(output->geometry, TopAbs_SOLID); e.More(); e.Next()) {
    const auto component = detail::snapshot(e.Current()).value();
    const auto& m = component->measures();
    const auto v = std::round(m.volume);
    const auto center = v == 30  ? Vec3{10, 0.5, 6.5}
                        : v == 1 ? Vec3{15.5, 2.5, 7.5}
                        : v == 2 ? Vec3{6.5, 8, 12.5}
                                 : Vec3{11.5, 5.5, 20.5};
    const double area = v == 30 ? 62 : v == 1 ? 6 : v == 2 ? 10 : 14;
    measures(component, v, area, reflected(center, plane));
    volumes.push_back(v);
  }
  std::ranges::sort(volumes);
  CHECK(volumes == std::vector<double>{1, 2, 3, 30});
  history(*result, source);
  CHECK(serialized(source) == saved);
}

TEST_CASE("exact mirrors preserve analytic periodic surfaces holes and enclosed cavities",
          "[mirror-kernel]") {
  auto kernel = make_occt_kernel().value();
  const Plane plane{{2, 0, 0}, {0, 1, 0}, {1, 0, 0}};
  std::vector<std::vector<NurbsCurve2>> paths{
      {make_circle(Vec2{10, 0}, 2)}, {make_circle(Vec2{10, 0}, 1)}, {make_circle(Vec2{30, 0}, 1)}};
  const auto seed =
      kernel->extrude({{}, regions(paths, FillRule::kEvenOdd).value(), {0, 0, 7}}).value().shape;
  const auto saved = serialized(seed);
  const auto result = kernel->mirror(seed, plane);
  REQUIRE(result);
  measures(result->shape, 28 * std::numbers::pi, 64 * std::numbers::pi, {-11, 0, 3.5}, 2);
  CHECK(seed->measures().faces == 16);  // four analytic quadrants per circular boundary
  CHECK(result->shape->measures().faces == 16);
  std::size_t cylinders = 0;
  const auto output = std::dynamic_pointer_cast<const detail::Snapshot>(result->shape);
  for (TopExp_Explorer e(output->geometry, TopAbs_FACE); e.More(); e.Next())
    cylinders += BRepAdaptor_Surface(TopoDS::Face(e.Current())).GetType() == GeomAbs_Cylinder;
  CHECK(cylinders == 12);
  history(*result, seed);
  CHECK(serialized(seed) == saved);
  const auto cavity =
      kernel
          ->boolean_operation(box(*kernel, {9, -1, 4}, {4, 5, 6}),
                              {box(*kernel, {10, 0, 5}, {1, 2, 3})}, BooleanOp::kSubtract)
          .value()
          .shape;
  const auto mirrored_cavity = kernel->mirror(cavity, plane);
  REQUIRE(mirrored_cavity);
  measures(mirrored_cavity->shape, 114, 170, reflected(cavity->measures().centroid, plane));
  CHECK(mirrored_cavity->shape->measures().faces == 12);
  history(*mirrored_cavity, cavity);
}

TEST_CASE("mirrors reject incompatible inputs invalid planes and coordinate overflow",
          "[mirror-kernel]") {
  auto kernel = make_occt_kernel().value();
  const auto source = box(*kernel);
  const auto saved = serialized(source);
  const double inf = std::numeric_limits<double>::infinity();
  CHECK_FALSE(kernel->mirror({}, {}));
  struct ForeignShape final : Shape {
    Measures measured;
    const Measures& measures() const noexcept override { return measured; }
    std::size_t estimated_bytes() const noexcept override { return 0; }
  };
  CHECK_FALSE(kernel->mirror(std::make_shared<ForeignShape>(), {}));
  for (const auto& plane :
       {Plane{{}, {1, 0, 0}, {}}, Plane{{}, {0, 0, 1}, {0, 0, 1}}, Plane{{}, {1, 0, 0}, {0, 0, 2}},
        Plane{{inf, 0, 0}, {0, 1, 0}, {1, 0, 0}}, Plane{{1e308, 0, 0}, {0, 1, 0}, {1, 0, 0}}}) {
    const auto result = kernel->mirror(source, plane);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == "kernel");
    CHECK(serialized(source) == saved);
  }
}

TEST_CASE("mirrored snapshots and history survive their kernel facade", "[mirror-kernel]") {
  OperationResult retained;
  {
    auto kernel = make_occt_kernel().value();
    retained = kernel->mirror(box(*kernel), {{}, {0, 1, 0}, {1, 0, 0}}).value();
  }
  measures(retained.shape, 30, 62, {-10, 0.5, 6.5});
  history(retained, retained.history.front().before->owner());
}
