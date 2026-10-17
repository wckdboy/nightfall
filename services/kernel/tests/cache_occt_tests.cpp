// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>

#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Trsf.hxx>

#include "geometry_cache.hpp"

namespace {
void box_properties(const TopoDS_Shape& shape, double center_x) {
  REQUIRE_FALSE(shape.IsNull());
  REQUIRE(BRepCheck_Analyzer(shape).IsValid());
  GProp_GProps volume, area;
  BRepGProp::VolumeProperties(shape, volume, 1e-9, true);
  BRepGProp::SurfaceProperties(shape, area, 1e-9);
  CHECK(std::abs(volume.Mass() - 6000) <= 1e-8);
  CHECK(std::abs(area.Mass() - 2200) <= 1e-8);
  CHECK(std::abs(volume.CentreOfMass().X() - center_x) <= 1e-10);
  TopTools_IndexedMapOfShape faces, edges;
  TopExp::MapShapes(shape, TopAbs_FACE, faces);
  TopExp::MapShapes(shape, TopAbs_EDGE, edges);
  CHECK(faces.Extent() == 6);
  CHECK(edges.Extent() == 12);
}
}  // namespace

TEST_CASE("geometry cache: real OCCT snapshots survive eviction without changing source geometry",
          "[cache][occt]") {
  using namespace nf;
  kernel_service::GeometryCache<TopoDS_Shape> cache({1, 65536});
  const auto base = ir::body_hash(std::nullopt, ir::sha256("box"));
  const auto moved = ir::body_hash(base, ir::sha256("translate 7mm"));
  auto shape = std::make_shared<const TopoDS_Shape>(BRepPrimAPI_MakeBox(10, 20, 30).Shape());
  box_properties(*shape, 5);
  REQUIRE(cache.insert(base, shape, 65536));
  auto reader = cache.find(base);
  REQUIRE(reader);
  shape.reset();
  gp_Trsf transform;
  transform.SetTranslation(gp_Vec(7, 0, 0));
  auto changed = std::make_shared<const TopoDS_Shape>(
      BRepBuilderAPI_Transform(*reader, transform, /*theCopyGeom=*/true).Shape());
  box_properties(*changed, 12);
  REQUIRE(cache.insert(moved, changed, 65536));
  CHECK_FALSE(cache.find(base));
  auto cached = cache.find(moved);
  REQUIRE(cached);
  CHECK(cached == changed);
  box_properties(*reader, 5);
  cache.clear();
  changed.reset();
  box_properties(*cached, 12);
}
