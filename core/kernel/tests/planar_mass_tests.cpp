// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "nf/kernel/kernel.hpp"
#include "nf/math/constructors.hpp"
#include "snapshot.hpp"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Ax1.hxx>

#include <algorithm>
#include <chrono>
#include <numbers>
#include <random>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace nf::kernel;
using namespace nf::math;
using Catch::Matchers::WithinAbs;

namespace {
constexpr double pi = std::numbers::pi;
std::vector<Vec2> slant_points(double angle, double scale = 1) {
  const double shift = 10 / std::tan(angle);
  return {Vec2{-5 - shift, -10} * scale, Vec2{5 - shift, -10} * scale, Vec2{5 + shift, 10} * scale,
          Vec2{-5 + shift, 10} * scale};
}
std::vector<NurbsCurve2> path(const std::vector<Vec2>& points) {
  std::vector<NurbsCurve2> result;
  for (std::size_t i = 0; i < points.size(); ++i)
    result.push_back(make_line(points[i], points[(i + 1) % points.size()]));
  return result;
}
Extrusion extrusion(std::vector<std::vector<NurbsCurve2>> paths, double depth) {
  return {{}, regions(paths, FillRule::kEvenOdd).value(), {0, 0, depth}};
}
TopoDS_Shape slant(double angle, double scale = 1) {
  BRepBuilderAPI_MakePolygon wire;
  for (const auto p : slant_points(angle, scale)) wire.Add(gp_Pnt(p.x, p.y, 0));
  wire.Close();
  return BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(wire.Wire()).Face(),
                               gp_Vec(0, 0, 30 * scale))
      .Shape();
}
void check_slant(const Shape& shape, double angle, double scale, const gp_Pnt& center) {
  const auto& m = shape.measures();
  CHECK(m.solids == 1);
  CHECK(m.faces == 6);
  CHECK(m.edges == 12);
  const double volume = 6000 * scale * scale * scale;
  const double area = (1000 + 1200 / std::sin(angle)) * scale * scale;
  CHECK_THAT(m.volume, WithinAbs(volume, 1e-9 * std::max(1.0, volume)));
  CHECK_THAT(m.area, WithinAbs(area, 1e-9 * std::max(1.0, area)));
  CHECK_THAT(m.centroid.x, WithinAbs(center.X(), 1e-7));
  CHECK_THAT(m.centroid.y, WithinAbs(center.Y(), 1e-7));
  CHECK_THAT(m.centroid.z, WithinAbs(center.Z(), 1e-7));
}
}  // namespace

TEST_CASE("unconstrained slanted extrusion bounds polynomial mass integration",
          "[planar-mass][mass-runtime]") {
  auto kernel = make_occt_kernel().value();
  constexpr double angle = 30 * (pi / 180);
  const auto input = extrusion({path(slant_points(angle))}, 30);
  const auto start = std::chrono::steady_clock::now();
  const auto result = kernel->extrude(input);
  const auto elapsed = std::chrono::steady_clock::now() - start;
  REQUIRE(result);
  check_slant(*result->shape, angle, 1, gp_Pnt(0, 0, 15));
  CHECK_THAT(result->shape->measures().volume, WithinAbs(6000, 1e-7));
  CHECK_THAT(result->shape->measures().area, WithinAbs(3400, 1e-7));
  // A broad regression bound for the former multi-second adaptive refinement.
  CHECK(std::chrono::duration<double>(elapsed).count() < 2.0);
  REQUIRE(result->generated.size() == 6);
  for (const auto& face : result->generated) {
    CHECK(face.entity->owner() == result->shape);
    CHECK(face.entity->kind() == EntityKind::kFace);
  }
}

TEST_CASE("polynomial mass integration preserves placed and scaled slanted prisms",
          "[planar-mass][property]") {
  std::mt19937 random(20261024);
  std::uniform_real_distribution<double> angles(15 * pi / 180, 165 * pi / 180), rotations(-pi, pi);
  for (int i = 0; i < 16; ++i) {
    const double angle = angles(random), scale = i % 2 ? 0.01 : 100;
    const auto geometry = slant(angle, scale);
    gp_Trsf placement;
    placement.SetRotation(gp_Ax1({}, gp_Dir(1, 2, 3)), rotations(random));
    placement.SetTranslationPart(i % 3 ? gp_Vec(1e4, -2e4, 3e4) : gp_Vec());
    const auto placed = BRepBuilderAPI_Transform(geometry, placement, true).Shape();
    const auto result = detail::snapshot(placed);
    REQUIRE(result);
    check_slant(**result, angle, scale, gp_Pnt(0, 0, 15 * scale).Transformed(placement));
  }
}

TEST_CASE("polynomial mass integration retains concave profiles and straight holes",
          "[planar-mass]") {
  auto kernel = make_occt_kernel().value();
  const auto input = extrusion({path({{0, 0}, {30, 0}, {30, 10}, {10, 10}, {10, 30}, {0, 30}}),
                                path({{2, 2}, {6, 2}, {6, 6}, {2, 6}})},
                               7);
  const auto result = kernel->extrude(input);
  REQUIRE(result);
  const auto& m = result->shape->measures();
  CHECK(m.solids == 1);
  CHECK(m.faces == 12);
  CHECK_THAT(m.volume, WithinAbs(484 * 7, 1e-7));
  CHECK_THAT(m.area, WithinAbs(1920, 1e-7));
  CHECK_THAT(m.centroid.x, WithinAbs(5436.0 / 484, 1e-7));
  CHECK_THAT(m.centroid.y, WithinAbs(5436.0 / 484, 1e-7));
  CHECK_THAT(m.centroid.z, WithinAbs(3.5, 1e-7));
}

TEST_CASE("polynomial mass integration preserves slanted cavities and separated solids",
          "[planar-mass]") {
  const auto outer = slant(pi / 6);
  gp_Trsf shrink;
  shrink.SetScale(gp_Pnt(0, 0, 15), 0.5);
  const auto cavity = BRepBuilderAPI_Transform(outer, shrink, true).Shape();
  const auto hollow = BRepAlgoAPI_Cut(outer, cavity).Shape();
  gp_Trsf move;
  move.SetTranslation(gp_Vec(1e6, -2e6, 3e6));
  BRep_Builder builder;
  TopoDS_Compound compound;
  builder.MakeCompound(compound);
  builder.Add(compound, BRepBuilderAPI_Transform(hollow, move, true).Shape());
  builder.Add(compound, BRepPrimAPI_MakeBox(gp_Pnt(-1e6, 2e6, -3e6), 1, 2, 3).Shape());
  const auto result = detail::snapshot(compound);
  REQUIRE(result);
  const auto& m = (*result)->measures();
  CHECK(m.solids == 2);
  CHECK_THAT(m.volume, WithinAbs(5256, 1e-5));
  CHECK_THAT(m.area, WithinAbs(4272, 1e-5));
  CHECK_THAT(m.centroid.x, WithinAbs((5250 * 1e6 + 6 * (-1e6 + 0.5)) / 5256, 1e-6));
  CHECK_THAT(m.centroid.y, WithinAbs((5250 * -2e6 + 6 * (2e6 + 1)) / 5256, 1e-6));
  CHECK_THAT(m.centroid.z, WithinAbs((5250 * (3e6 + 15) + 6 * (-3e6 + 1.5)) / 5256, 1e-6));
}

TEST_CASE("planar mass integration retains adaptive treatment of curved trims", "[planar-mass]") {
  auto kernel = make_occt_kernel().value();
  const auto input =
      extrusion({path({{-5, -10}, {5, -10}, {5, 10}, {-5, 10}}), {make_circle(Vec2{}, 2)}}, 3);
  const auto result = kernel->extrude(input);
  REQUIRE(result);
  const auto& m = result->shape->measures();
  CHECK(m.solids == 1);
  CHECK_THAT(m.volume, WithinAbs(600 - 12 * pi, 1e-7));
  CHECK_THAT(m.area, WithinAbs(580 + 4 * pi, 1e-7));
  CHECK_THAT(m.centroid.x, WithinAbs(0, 1e-7));
  CHECK_THAT(m.centroid.y, WithinAbs(0, 1e-7));
  CHECK_THAT(m.centroid.z, WithinAbs(1.5, 1e-7));
}
