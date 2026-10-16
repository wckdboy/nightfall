// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// Smoke tests for the OCCT build: modeling and STEP round-trip with analytic answers.

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <STEPControl_Reader.hxx>
#include <STEPControl_Writer.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Ax2.hxx>
#include <gp_Pnt.hxx>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numbers>
#include <optional>

using Catch::Matchers::WithinRel;

namespace {

constexpr double kRelTol = 1e-6;

double volume(const TopoDS_Shape& shape) {
  GProp_GProps props;
  BRepGProp::VolumeProperties(shape, props);
  return props.Mass();
}

bool is_valid(const TopoDS_Shape& shape) { return BRepCheck_Analyzer(shape).IsValid(); }

// The box edge running from (0,0,0) to (0,0,height).
std::optional<TopoDS_Edge> z_edge_at_origin(const TopoDS_Shape& box) {
  for (TopExp_Explorer it(box, TopAbs_EDGE); it.More(); it.Next()) {
    const TopoDS_Edge& edge = TopoDS::Edge(it.Current());
    bool at_origin_axis = true;
    for (TopExp_Explorer v(edge, TopAbs_VERTEX); v.More(); v.Next()) {
      const gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(v.Current()));
      at_origin_axis = at_origin_axis && std::abs(p.X()) < 1e-9 && std::abs(p.Y()) < 1e-9;
    }
    if (at_origin_axis) return edge;
  }
  return std::nullopt;
}

}  // namespace

TEST_CASE("box volume is exact", "[occt]") {
  const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 20.0, 30.0).Shape();
  REQUIRE(is_valid(box));
  CHECK_THAT(volume(box), WithinRel(6000.0, kRelTol));
}

TEST_CASE("filleting one box edge removes (1 - pi/4) r^2 L", "[occt]") {
  constexpr double r = 2.0, length = 30.0;
  const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 20.0, length).Shape();
  const auto edge = z_edge_at_origin(box);
  REQUIRE(edge.has_value());

  BRepFilletAPI_MakeFillet fillet(box);
  fillet.Add(r, *edge);
  fillet.Build();
  REQUIRE(fillet.IsDone());
  const TopoDS_Shape result = fillet.Shape();
  REQUIRE(is_valid(result));

  const double expected = 6000.0 - (1.0 - std::numbers::pi / 4.0) * r * r * length;
  CHECK_THAT(volume(result), WithinRel(expected, kRelTol));
}

TEST_CASE("a through-hole removes pi r^2 h", "[occt]") {
  // The same operation that exposes SMLIB-001 in SMLib (docs/known-issues).
  const TopoDS_Shape plate = BRepPrimAPI_MakeBox(12.0, 12.0, 1.5).Shape();
  const TopoDS_Shape tool =
      BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(1.5, 1.5, -1.0), gp::DZ()), 0.75, 3.5).Shape();
  BRepAlgoAPI_Cut cut(plate, tool);
  REQUIRE(cut.IsDone());
  REQUIRE(is_valid(cut.Shape()));
  const double expected = 12.0 * 12.0 * 1.5 - std::numbers::pi * 0.75 * 0.75 * 1.5;
  CHECK_THAT(volume(cut.Shape()), WithinRel(expected, kRelTol));
}

TEST_CASE("STEP round-trip preserves a filleted solid", "[occt][step]") {
  const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 20.0, 30.0).Shape();
  const auto edge = z_edge_at_origin(box);
  REQUIRE(edge.has_value());
  BRepFilletAPI_MakeFillet fillet(box);
  fillet.Add(2.0, *edge);
  const TopoDS_Shape original = fillet.Shape();

  STEPControl_Writer writer;
  REQUIRE(writer.Transfer(original, STEPControl_AsIs) == IFSelect_RetDone);
  REQUIRE(writer.Write("occt_smoke_roundtrip.step") == IFSelect_RetDone);

  STEPControl_Reader reader;
  REQUIRE(reader.ReadFile("occt_smoke_roundtrip.step") == IFSelect_RetDone);
  REQUIRE(reader.TransferRoots() > 0);
  const TopoDS_Shape loaded = reader.OneShape();
  REQUIRE(is_valid(loaded));
  CHECK_THAT(volume(loaded), WithinRel(volume(original), kRelTol));
}
