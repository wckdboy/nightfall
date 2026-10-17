// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "model_inputs.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "nf/ir/inputs.hpp"

using namespace nf::kernel_service;
using namespace nf;
using Catch::Matchers::WithinAbs;

namespace {
ir::json::Value inputs(const char* kind, const char* text) {
  ir::Feature feature;
  feature.id = "test";
  feature.type = kind;
  feature.inputs = ir::json::parse(text).value();
  return ir::FeatureInputs::compile(feature).value().evaluate().value();
}
constexpr auto sketch_text = R"({"plane":{"datum":"xy"},"entities":[
  {"id":"a","type":"circle","center":[0,0],"radius":10},
  {"id":"b","type":"circle","center":[0,0],"radius":5},
  {"id":"c","type":"rect","corner":[30,0],"size":[10,20]}],"constraints":[]})";
}  // namespace

TEST_CASE("evaluated sketch profiles preserve holes and point-selected regions") {
  const auto sketch = prepare_sketch(inputs("sketch", sketch_text));
  REQUIRE(sketch);
  auto extrusion =
      prepare_extrusion(inputs("extrude", R"({"profile":{"sketch":"s","regions":[[35,10]]},
    "distance":7,"result":{"new":"b","body_type":"brep"}})"),
                        *sketch);
  REQUIRE(extrusion);
  CHECK(extrusion->regions.size() == 1);
  auto kernel = kernel::make_occt_kernel().value();
  auto result = kernel->extrude(*extrusion);
  REQUIRE(result);
  CHECK_THAT(result->shape->measures().volume, WithinAbs(1400, 1e-8));
  auto hole = prepare_extrusion(inputs("extrude", R"({"profile":{"sketch":"s","regions":[[0,0]]},
    "distance":7,"result":{"new":"b","body_type":"brep"}})"),
                                *sketch);
  REQUIRE_FALSE(hole);
  CHECK(hole.error().pointer == "/inputs/profile/regions/0");
  auto boundary =
      prepare_extrusion(inputs("extrude", R"({"profile":{"sketch":"s","regions":[[10,0]]},
    "distance":7,"result":{"new":"b","body_type":"brep"}})"),
                        *sketch);
  CHECK_FALSE(boundary);
}

TEST_CASE("line sketches join unordered reversed edges without silently closing gaps") {
  auto sketch = prepare_sketch(inputs("sketch", R"({"plane":{"datum":"yz"},"entities":[
    {"id":"a","type":"line","start":[0,0],"end":[10,0]},
    {"id":"b","type":"line","start":[0,20],"end":[10,20]},
    {"id":"c","type":"line","start":[10,0],"end":[10,20]},
    {"id":"d","type":"line","start":[0,0],"end":[0,20]}],"constraints":[]})"));
  REQUIRE(sketch);
  auto extrusion = prepare_extrusion(inputs("extrude", R"({"profile":{"sketch":"s"},
    "distance":30,"symmetric":true,"result":{"new":"b","body_type":"brep"}})"),
                                     *sketch);
  REQUIRE(extrusion);
  auto result = kernel::make_occt_kernel().value()->extrude(*extrusion);
  REQUIRE(result);
  CHECK_THAT(result->shape->measures().volume, WithinAbs(6000, 1e-8));
  CHECK_THAT(result->shape->measures().centroid.x, WithinAbs(0, 1e-8));
  CHECK_THAT(result->shape->measures().centroid.y, WithinAbs(5, 1e-8));
  auto open = prepare_sketch(inputs("sketch", R"({"plane":{"datum":"xy"},"entities":[
    {"id":"a","type":"line","start":[0,0],"end":[10,0]}],"constraints":[]})"));
  REQUIRE_FALSE(open);
  CHECK(open.error().code == "kernel");
}

TEST_CASE("unsupported sketch semantics are errors with field pointers") {
  auto constraints = prepare_sketch(inputs("sketch", R"({"plane":{"datum":"xy"},"entities":[
    {"id":"a","type":"circle","center":[0,0],"radius":10}],
    "constraints":[{"type":"radius","entities":["a"],"value":10}]})"));
  REQUIRE_FALSE(constraints);
  CHECK(constraints.error().pointer == "/inputs/constraints");
  auto point = prepare_sketch(inputs("sketch", R"({"plane":{"datum":"xy"},"entities":[
    {"id":"a","type":"point","at":[0,0]}],"constraints":[]})"));
  REQUIRE_FALSE(point);
  CHECK(point.error().pointer == "/inputs/entities/0/type");
  auto radius = prepare_sketch(inputs("sketch", R"({"plane":{"datum":"xy"},"entities":[
    {"id":"a","type":"circle","center":[0,0],"radius":-1}],"constraints":[]})"));
  CHECK_FALSE(radius);
}

TEST_CASE("extrusion modes and rigid transform axes are checked explicitly") {
  auto sketch = prepare_sketch(inputs("sketch", sketch_text)).value();
  for (
      const auto* text :
      {R"({"profile":{"sketch":"s"},"distance":3,"draft":1,"result":{"new":"b","body_type":"brep"}})",
       R"({"profile":{"sketch":"s"},"to":{"faces_of":"other"},"result":{"new":"b","body_type":"brep"}})",
       R"({"profile":{"sketch":"s"},"distance":3,"result":{"new":"b","body_type":"mesh"}})"}) {
    CHECK_FALSE(prepare_extrusion(inputs("extrude", text), sketch));
  }
  CHECK(prepare_extrusion(inputs("extrude", R"({"profile":{"sketch":"s"},
    "distance":3,"result":{"join":"other"}})"),
                          sketch));
  auto rotation = prepare_transform(inputs("transform", R"({"bodies":["b"],"translation":[1,2,3],
    "rotation":{"axis":{"origin":[4,5,6],"direction":"z"},"angle":90}})"));
  REQUIRE(rotation);
  CHECK(rotation->axis_origin == math::Vec3{4, 5, 6});
  CHECK_THAT(rotation->angle, WithinAbs(std::acos(-1.0) / 2, 1e-12));
  CHECK_FALSE(
      prepare_transform(inputs("transform", R"({"bodies":["b","b"],"translation":[0,0,0]})")));
  CHECK_FALSE(prepare_transform(inputs("transform", R"({"bodies":["b"],
    "rotation":{"axis":{"sketch":"s","entity":"e"},"angle":90}})")));
}
