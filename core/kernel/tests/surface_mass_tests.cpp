// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "nf/math/constructors.hpp"
#include "snapshot.hpp"

#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Elips.hxx>

#include <chrono>
#include <numbers>
#include <sstream>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace nf::kernel;
using Catch::Matchers::WithinAbs;
namespace {
constexpr double pi = std::numbers::pi;
}

TEST_CASE("surface integration bounds repeated narrow cylindrical fillets",
          "[surface-mass][mass-runtime]") {
  const auto box = BRepPrimAPI_MakeBox(gp_Pnt(24, -0.6, 0), 40, 1.2, 28).Shape();
  BRepFilletAPI_MakeFillet fillet(box);
  for (TopExp_Explorer edges(box, TopAbs_EDGE); edges.More(); edges.Next()) {
    const auto edge = TopoDS::Edge(edges.Current());
    const BRepAdaptor_Curve curve(edge);
    const auto delta =
        curve.Value(curve.LastParameter()).XYZ() - curve.Value(curve.FirstParameter()).XYZ();
    if (std::abs(delta.Z()) > 27) fillet.Add(0.5, edge);
  }
  fillet.Build();
  REQUIRE(fillet.IsDone());
  BRep_Builder builder;
  TopoDS_Compound compound;
  builder.MakeCompound(compound);
  constexpr int count = 1024;
  for (int i = 0; i < count; ++i) {
    gp_Trsf placement;
    placement.SetRotation(gp_Ax1({}, gp_Dir(0, 0, 1)), (i % 64) * pi / 32);
    placement.SetTranslationPart(gp_Vec((i / 64) * 200, 0, 0));
    builder.Add(compound, BRepBuilderAPI_Transform(fillet.Shape(), placement, false).Shape());
  }
  const auto start = std::chrono::steady_clock::now();
  const auto result = detail::snapshot(compound);
  const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start);
  REQUIRE(result);
  INFO("1024 rounded fins: " << elapsed.count() << " s");
  const auto& m = (*result)->measures();
  const double section = 48 - (4 - pi) * 0.25;
  const double perimeter = 2 * (40 + 1.2) - 4 + pi;
  CHECK(m.solids == count);
  CHECK_THAT(m.volume, WithinAbs(count * section * 28, 1e-5));
  CHECK_THAT(m.area, WithinAbs(count * (2 * section + perimeter * 28), 1e-5));
  CHECK_THAT(m.centroid.x, WithinAbs(1500, 1e-7));
  CHECK_THAT(m.centroid.y, WithinAbs(0, 1e-7));
  CHECK_THAT(m.centroid.z, WithinAbs(14, 1e-7));
  // Guard optimized latency; instrumented/Debug builds still verify every mass invariant.
  if (NF_RELEASE_PERFORMANCE) CHECK(elapsed.count() < 2.5);
}

TEST_CASE("surface integration retains elliptic trims and oppositely oriented holes",
          "[surface-mass]") {
  const gp_Ax2 frame(gp_Pnt(13, -17, 21), gp_Dir(1, 2, 3));
  auto wire = [&](double scale) {
    return BRepBuilderAPI_MakeWire(
               BRepBuilderAPI_MakeEdge(gp_Elips(frame, 5 * scale, 3 * scale)).Edge())
        .Wire();
  };
  BRepBuilderAPI_MakeFace face(wire(1));
  face.Add(TopoDS::Wire(wire(0.25).Reversed()));
  const auto result =
      detail::snapshot(BRepPrimAPI_MakePrism(face.Face(), gp_Vec(frame.Direction()) * 7).Shape());
  REQUIRE(result);
  // Independent Simpson integral for the ellipse perimeter, rather than OCCT's area.
  constexpr int steps = 8192;
  double sum = 0;
  for (int i = 0; i <= steps; ++i) {
    const double t = 2 * pi * i / steps;
    sum += (i == 0 || i == steps ? 1
            : i % 2              ? 4
                                 : 2) *
           std::hypot(5 * std::sin(t), 3 * std::cos(t));
  }
  const double perimeter = sum * (2 * pi / steps) / 3;
  const auto& m = (*result)->measures();
  CHECK_THAT(m.volume, WithinAbs(105 * pi * (1 - 0.0625), 1e-7));
  CHECK_THAT(m.area, WithinAbs(30 * pi * (1 - 0.0625) + 7 * perimeter * 1.25, 1e-6));
}

TEST_CASE("surface integration preserves placed cylindrical sectors", "[surface-mass]") {
  for (const double scale : {0.01, 1.0, 100.0}) {
    for (const double angle : {0.03, pi / 3, 1.75 * pi, 2 * pi}) {
      CAPTURE(scale, angle);
      const gp_Ax2 frame(gp_Pnt(15 * scale, -22 * scale, 31 * scale), gp_Dir(1, 2, 3));
      const auto geometry = BRepPrimAPI_MakeCylinder(frame, 3 * scale, 7 * scale, angle).Shape();
      const auto result = detail::snapshot(geometry);
      REQUIRE(result);
      const auto& m = (*result)->measures();
      const double area = (21 * angle + 9 * angle + (angle < 2 * pi ? 42 : 0)) * scale * scale;
      CHECK_THAT(m.volume,
                 WithinAbs(31.5 * angle * scale * scale * scale, std::max(1e-8, m.volume * 1e-9)));
      CHECK_THAT(m.area, WithinAbs(area, std::max(1e-8, area * 1e-9)));
    }
  }
}

TEST_CASE("surface integration uses consistent seam orientation after mirror and union",
          "[surface-mass]") {
  using namespace nf::math;
  auto kernel = make_occt_kernel().value();
  const std::vector<std::vector<NurbsCurve2>> paths{{make_circle(Vec2{76, 66}, 6)},
                                                    {make_circle(Vec2{76, 66}, 4)}};
  const auto seed = kernel
                        ->extrude({{{0, 0, 6}, {1, 0, 0}, {0, 0, 1}},
                                   regions(paths, FillRule::kEvenOdd).value(),
                                   {0, 0, 8}})
                        .value();
  const auto x = kernel->mirror(seed.shape, {{}, {0, 0, 1}, {1, 0, 0}}).value();
  const auto pair = kernel->boolean_operation(seed.shape, {x.shape}, BooleanOp::kUnion).value();
  CHECK_THAT(pair.shape->measures().area, WithinAbs(400 * pi, 1e-6));
  const auto y = kernel->mirror(pair.shape, {{}, {0, 0, 1}, {0, 1, 0}}).value();
  CHECK_THAT(y.shape->measures().area, WithinAbs(400 * pi, 1e-6));
  const auto all = kernel->boolean_operation(pair.shape, {y.shape}, BooleanOp::kUnion).value();
  CHECK_THAT(all.shape->measures().volume, WithinAbs(640 * pi, 1e-6));
  CHECK_THAT(all.shape->measures().area, WithinAbs(800 * pi, 1e-6));
}

TEST_CASE("mass integration preserves deterministic moments on shared placed curved surfaces",
          "[surface-mass][mass-parallel]") {
  const gp_Ax2 frame(gp_Pnt(13, -17, 21), gp_Dir(1, 2, 3));
  gp_Ax2 hole = frame;
  const gp_Vec offset(frame.XDirection());
  hole.SetLocation(frame.Location().Translated(offset));
  const auto outer =
      BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Elips(frame, 5, 3)).Edge()).Wire();
  const auto inner =
      BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Elips(hole, 1.25, 0.75)).Edge()).Wire();
  BRepBuilderAPI_MakeFace face(outer);
  face.Add(TopoDS::Wire(inner.Reversed()));
  const auto seed = BRepPrimAPI_MakePrism(face.Face(), gp_Vec(frame.Direction()) * 7).Shape();
  const auto local_center =
      frame.Location().XYZ() - offset.XYZ() / 15 + frame.Direction().XYZ() * 3.5;
  BRep_Builder builder;
  TopoDS_Compound compound;
  builder.MakeCompound(compound);
  gp_XYZ expected_center;
  constexpr int count = 12;
  for (int i = 0; i < count; ++i) {
    gp_Trsf placement;
    placement.SetRotation(gp_Ax1({}, gp_Dir(2, -1, 3)), i * pi / 7);
    placement.SetTranslationPart(gp_Vec(i * 200, -i * 100, i * 50));
    builder.Add(compound, BRepBuilderAPI_Transform(seed, placement, false).Shape());
    expected_center += gp_Pnt(local_center).Transformed(placement).XYZ() / count;
  }
  std::ostringstream saved;
  BRepTools::Write(compound, saved);
  std::optional<Measures> first;
  for (int pass = 0; pass < 4; ++pass) {
    const auto result = detail::snapshot(compound);
    REQUIRE(result);
    const auto& m = (*result)->measures();
    CHECK(m.solids == count);
    CHECK_THAT(m.volume, WithinAbs(count * 105 * pi * 15 / 16, 1e-7));
    CHECK_THAT(m.centroid.x, WithinAbs(expected_center.X(), 1e-8));
    CHECK_THAT(m.centroid.y, WithinAbs(expected_center.Y(), 1e-8));
    CHECK_THAT(m.centroid.z, WithinAbs(expected_center.Z(), 1e-8));
    if (first) {
      CHECK(m.volume == first->volume);
      CHECK(m.area == first->area);
      CHECK(m.centroid == first->centroid);
    } else {
      first = m;
    }
    std::ostringstream after;
    BRepTools::Write(compound, after);
    CHECK(after.str() == saved.str());
  }
}
