// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "snapshot.hpp"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Pln.hxx>

#include <numbers>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace nf::kernel;
using Catch::Matchers::WithinAbs;

TEST_CASE("snapshots validate solids and retain analytic measurements") {
  auto result = detail::snapshot(BRepPrimAPI_MakeBox(10, 20, 30).Shape());
  REQUIRE(result);
  const auto& m = (*result)->measures();
  CHECK_THAT(m.volume, WithinAbs(6000, 1e-8));
  CHECK_THAT(m.area, WithinAbs(2200, 1e-8));
  CHECK(m.solids == 1);
  CHECK(m.faces == 6);
  CHECK(m.edges == 12);
  CHECK_THAT(m.centroid.x, WithinAbs(5, 1e-10));
  CHECK_THAT(m.centroid.y, WithinAbs(10, 1e-10));
  CHECK_THAT(m.centroid.z, WithinAbs(15, 1e-10));
  CHECK_THAT(m.minimum.x, WithinAbs(0, 1e-7));
  CHECK_THAT(m.maximum.z, WithinAbs(30, 1e-7));
  CHECK((*result)->estimated_bytes() > 0);
}

TEST_CASE("snapshots accept multiple solids but reject empty or stray geometry") {
  BRep_Builder builder;
  TopoDS_Compound compound;
  builder.MakeCompound(compound);
  CHECK_FALSE(detail::snapshot(compound));
  CHECK_FALSE(detail::snapshot(TopoDS_Shape{}));
  builder.Add(compound, BRepPrimAPI_MakeBox(10, 20, 30).Shape());
  builder.Add(compound, BRepPrimAPI_MakeBox(gp_Pnt(100, 0, 0), 10, 20, 30).Shape());
  auto result = detail::snapshot(compound);
  REQUIRE(result);
  CHECK((*result)->measures().solids == 2);
  CHECK_THAT((*result)->measures().volume, WithinAbs(12000, 1e-8));
  SECTION("stray edge") {
    builder.Add(compound, BRepBuilderAPI_MakeEdge(gp_Pnt(0, 0, 50), gp_Pnt(1, 0, 50)).Edge());
  }
  SECTION("stray face") {
    builder.Add(compound, BRepBuilderAPI_MakeFace(gp_Pln(), 0, 1, 0, 1).Face());
  }
  CHECK_FALSE(detail::snapshot(compound));
}

TEST_CASE("entity handles retain their snapshot and distinguish owning snapshots") {
  auto first = detail::snapshot(BRepPrimAPI_MakeBox(10, 20, 30).Shape()).value();
  auto second = detail::snapshot(BRepPrimAPI_MakeBox(10, 20, 30).Shape()).value();
  auto entities = detail::entities(first);
  REQUIRE(entities.size() == 18);
  auto again = detail::entities(first);
  auto other = detail::entities(second);
  CHECK(entities.front()->same_as(*again.front()));
  CHECK_FALSE(entities.front()->same_as(*other.front()));
  std::weak_ptr<const Shape> weak = first;
  first.reset();
  again.clear();
  REQUIRE_FALSE(weak.expired());
  CHECK_THAT(entities.front()->owner()->measures().volume, WithinAbs(6000, 1e-8));
  entities.clear();
  CHECK(weak.expired());
}

TEST_CASE("compound validation rejects a malformed solid at every position") {
  for (const int malformed : {0, 32, 63}) {
    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);
    for (int i = 0; i < 64; ++i) {
      const auto solid = BRepPrimAPI_MakeBox(gp_Pnt(i * 30, 0, 0), 10, 20, 30).Shape();
      if (i == malformed) {
        // Keep closed topology but break the edge/vertex geometry relationship.
        TopExp_Explorer vertices(solid, TopAbs_VERTEX);
        REQUIRE(vertices.More());
        builder.UpdateVertex(TopoDS::Vertex(vertices.Current()), gp_Pnt(-100, -100, -100), 1e-7);
        TopExp_Explorer shells(solid, TopAbs_SHELL);
        REQUIRE(shells.More());
        REQUIRE(BRep_Tool::IsClosed(shells.Current()));
        REQUIRE_FALSE(BRepCheck_Analyzer(solid).IsValid());
      }
      builder.Add(compound, solid);
    }
    CHECK_FALSE(detail::snapshot(compound));
  }
}

TEST_CASE("solid snapshots allow internal cavities") {
  const auto outer = BRepPrimAPI_MakeBox(10, 20, 30).Shape();
  const auto inner = BRepPrimAPI_MakeBox(gp_Pnt(1, 1, 1), 8, 18, 28).Shape();
  auto result = detail::snapshot(BRepAlgoAPI_Cut(outer, inner).Shape());
  REQUIRE(result);
  CHECK((*result)->measures().solids == 1);
  CHECK((*result)->measures().faces == 12);
  CHECK_THAT((*result)->measures().volume, WithinAbs(1968, 1e-8));
  CHECK_THAT((*result)->measures().area, WithinAbs(3944, 1e-8));
}

TEST_CASE("snapshot mass properties retain translated and rotated tori", "[mass-reference]") {
  constexpr double pi = std::numbers::pi;
  for (const gp_Pnt center : {gp_Pnt(0, 0, 0), gp_Pnt(1e6, -2e6, 3e6)}) {
    const auto geometry = BRepPrimAPI_MakeTorus(gp_Ax2(center, gp_Dir(1, 2, 3)), 20, 3).Shape();
    const auto result = detail::snapshot(geometry);
    REQUIRE(result);
    const auto& m = (*result)->measures();
    CHECK_THAT(m.volume, WithinAbs(360 * pi * pi, 1e-5));
    CHECK_THAT(m.area, WithinAbs(240 * pi * pi, 1e-5));
    CHECK_THAT(m.centroid.x, WithinAbs(center.X(), 1e-7));
    CHECK_THAT(m.centroid.y, WithinAbs(center.Y(), 1e-7));
    CHECK_THAT(m.centroid.z, WithinAbs(center.Z(), 1e-7));
    // A torus has support R * sqrt(1 - normal[i]^2) + r along each world axis.
    CHECK_THAT(m.minimum.x, WithinAbs(center.X() - 20 * std::sqrt(13.0 / 14) - 3, 1e-6));
    CHECK_THAT(m.maximum.x, WithinAbs(center.X() + 20 * std::sqrt(13.0 / 14) + 3, 1e-6));
    CHECK_THAT(m.minimum.y, WithinAbs(center.Y() - 20 * std::sqrt(10.0 / 14) - 3, 1e-6));
    CHECK_THAT(m.maximum.y, WithinAbs(center.Y() + 20 * std::sqrt(10.0 / 14) + 3, 1e-6));
    CHECK_THAT(m.minimum.z, WithinAbs(center.Z() - 20 * std::sqrt(5.0 / 14) - 3, 1e-6));
    CHECK_THAT(m.maximum.z, WithinAbs(center.Z() + 20 * std::sqrt(5.0 / 14) + 3, 1e-6));
  }
}

TEST_CASE("snapshot mass reference preserves cavities and separated unequal solids",
          "[mass-reference]") {
  const gp_Pnt origin(1e6, -2e6, 3e6);
  const auto outer = BRepPrimAPI_MakeBox(origin, 10, 20, 30).Shape();
  const auto inner =
      BRepPrimAPI_MakeBox(gp_Pnt(origin.X() + 1, origin.Y() + 2, origin.Z() + 3), 2, 4, 6).Shape();
  BRep_Builder builder;
  TopoDS_Compound compound;
  builder.MakeCompound(compound);
  builder.Add(compound, BRepAlgoAPI_Cut(outer, inner).Shape());
  // Keep a small separate component far from the larger solid and its cavity.
  builder.Add(compound, BRepPrimAPI_MakeBox(gp_Pnt(-1e6, 2e6, -3e6), 1, 2, 3).Shape());
  const auto result = detail::snapshot(compound);
  REQUIRE(result);
  const auto& m = (*result)->measures();
  CHECK(m.solids == 2);
  CHECK_THAT(m.volume, WithinAbs(6000 - 48 + 6, 1e-7));
  CHECK_THAT(m.area, WithinAbs(2200 + 88 + 22, 1e-7));
  CHECK_THAT(m.centroid.x,
             WithinAbs((6000 * (1e6 + 5) - 48 * (1e6 + 2) + 6 * (-1e6 + 0.5)) / 5958, 1e-7));
  CHECK_THAT(m.centroid.y,
             WithinAbs((6000 * (-2e6 + 10) - 48 * (-2e6 + 4) + 6 * (2e6 + 1)) / 5958, 1e-7));
  CHECK_THAT(m.centroid.z,
             WithinAbs((6000 * (3e6 + 15) - 48 * (3e6 + 6) + 6 * (-3e6 + 1.5)) / 5958, 1e-7));
  CHECK_THAT(m.minimum.x, WithinAbs(-1e6, 1e-6));
  CHECK_THAT(m.minimum.y, WithinAbs(-2e6, 1e-6));
  CHECK_THAT(m.minimum.z, WithinAbs(-3e6, 1e-6));
  CHECK_THAT(m.maximum.x, WithinAbs(1e6 + 10, 1e-6));
  CHECK_THAT(m.maximum.y, WithinAbs(2e6 + 2, 1e-6));
  CHECK_THAT(m.maximum.z, WithinAbs(3e6 + 30, 1e-6));
}
