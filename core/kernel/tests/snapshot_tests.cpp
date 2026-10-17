// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "snapshot.hpp"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Pln.hxx>

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
