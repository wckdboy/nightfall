// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "evaluator.hpp"
#include "model_inputs.hpp"
#include "part_fixture.hpp"

#include <algorithm>
#include <numbers>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "nf/ir/inputs.hpp"

using namespace nf;
using namespace nf::kernel_service;
using namespace nf::kernel_service::test;
using Catch::Matchers::WithinAbs;

namespace {
using ir::json::Value;
constexpr double kPi = std::numbers::pi;
Value set(const Value& object, std::string_view key, Value value) {
  auto members = *object.if_object();
  for (auto& member : members)
    if (member.key == key) member.value = std::move(value);
  return members;
}
void reverse_line(ir::Feature& sketch, std::size_t index) {
  auto entities = *sketch.inputs.find("entities")->if_array();
  const auto start = *entities[index].find("start"), end = *entities[index].find("end");
  entities[index] = set(set(entities[index], "start", end), "end", start);
  sketch.inputs = set(sketch.inputs, "entities", std::move(entities));
}
Value lines() {
  // d runs against the boundary traversal: axis direction must remain authored +Y.
  return ir::json::parse(R"({"plane":{"datum":"xy"},"entities":[
    {"id":"a","type":"line","start":[0,0],"end":[10,0]},
    {"id":"b","type":"line","start":[10,0],"end":[10,20]},
    {"id":"d","type":"line","start":[0,0],"end":[0,20]},
    {"id":"c","type":"line","start":[10,20],"end":[0,20]}],"constraints":[]})")
      .value();
}
ir::Document rotated(bool independent = false) {
  auto d = part();
  d.features[0].inputs = lines();
  d.features[2].inputs = ir::json::parse(R"({"bodies":["plate"],"translation":["step",0,0],
    "rotation":{"axis":{"sketch":"outline","entity":"d"},"angle":90}})")
                             .value();
  if (independent) {
    auto axis = feature("axis_sketch", "sketch", "{}");
    axis.inputs = lines();
    axis.inputs =
        set(axis.inputs, "plane",
            ir::json::parse(R"({"origin":["offset",0,0],"normal":"z","x_direction":"x"})").value());
    d.params.push_back({"offset", "0 mm", {}, {}, {}, {}});
    d.features.insert(d.features.begin() + 2, std::move(axis));
    d.features.back().inputs = set(
        d.features.back().inputs, "rotation",
        ir::json::parse(R"({"axis":{"sketch":"axis_sketch","entity":"d"},"angle":90})").value());
  }
  return d;
}
ir::Document turned(bool independent = false) {
  auto d = part();
  d.features[0].inputs = ir::json::parse(R"({"plane":{"datum":"zx"},"entities":[
    {"id":"a","type":"line","start":[0,5],"end":["depth",5]},
    {"id":"b","type":"line","start":["depth",5],"end":["depth",10]},
    {"id":"c","type":"line","start":["depth",10],"end":[0,10]},
    {"id":"d","type":"line","start":[0,5],"end":[0,10]}],"constraints":[]})")
                             .value();
  d.features[1] = feature("base", "revolve", R"({"profile":{"sketch":"outline"},
    "axis":{"sketch":"outline","entity":"a"},"angle":"turn",
    "result":{"new":"plate","body_type":"brep"}})");
  d.params.push_back({"turn", "360 deg", {}, {}, {}, {}});
  if (independent) {
    auto axis = feature("axis_sketch", "sketch", "{}");
    axis.inputs = lines();
    axis.inputs =
        set(axis.inputs, "plane",
            ir::json::parse(R"({"origin":["offset",0,0],"normal":"x","x_direction":"y"})").value());
    d.params.push_back({"offset", "5 mm", {}, {}, {}, {}});
    d.features.insert(d.features.begin() + 1, std::move(axis));
    d.features[2].inputs = set(d.features[2].inputs, "axis",
                               ir::json::parse(R"({"sketch":"axis_sketch","entity":"d"})").value());
  }
  return d;
}
void solid(const Evaluation& result, double volume, double area, math::Vec3 center) {
  for (const auto& f : result.features)
    for (const auto& e : f.errors) UNSCOPED_INFO(e.pointer << ": " << e.message);
  REQUIRE(result.ok());
  const auto& body = result.bodies.at("plate");
  REQUIRE(body.shape);
  const auto& m = body.shape->measures();
  CHECK(m.solids == 1);
  CHECK_THAT(m.volume, WithinAbs(volume, 1e-7));
  CHECK_THAT(m.area, WithinAbs(area, 1e-7));
  CHECK_THAT(m.centroid.x, WithinAbs(center.x, 1e-8));
  CHECK_THAT(m.centroid.y, WithinAbs(center.y, 1e-8));
  CHECK_THAT(m.centroid.z, WithinAbs(center.z, 1e-8));
  REQUIRE_FALSE(body.names.empty());
  for (const auto& name : body.names) {
    CHECK((name.feature == "base" || name.feature == "producer"));
    CHECK(name.entity->owner() == body.shape);
  }
}
void counts(const Evaluation& result, std::size_t calls, std::size_t hits) {
  CHECK(result.kernel_calls == calls);
  CHECK(result.cache_hits == hits);
}
Value evaluated(const ir::Feature& feature) {
  return ir::FeatureInputs::compile(feature).value().evaluate().value();
}
}  // namespace

TEST_CASE("sketch axes preserve authored line direction through profile assembly",
          "[sketch-axis]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto d = rotated();
  auto first = evaluator.evaluate(source(d)).value();
  solid(first, 6000, 2200, {18, 10, -5});
  counts(first, 2, 0);
  reverse_line(d.features[0], 2);
  auto reversed = evaluator.evaluate(source(d)).value();
  solid(reversed, 6000, 2200, {-12, 10, 5});
  counts(reversed, 2, 0);
  reverse_line(d.features[0], 2);
  auto restored = evaluator.evaluate(source(d)).value();
  solid(restored, 6000, 2200, {18, 10, -5});
  counts(restored, 0, 3);
  solid(first, 6000, 2200, {18, 10, -5});
}

TEST_CASE("sketch rotation axes map datum and placed frames into world coordinates",
          "[sketch-axis]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  struct Frame {
    const char* plane;
    math::Vec3 center;
  };
  for (const auto& [plane, center] :
       {Frame{R"({"datum":"xy"})", {18, 10, -5}}, Frame{R"({"datum":"yz"})", {-7, 5, 15}},
        Frame{R"({"datum":"zx"})", {8, -15, 10}},
        Frame{R"({"origin":[7,11,13],"x_direction":[0,0,2],"normal":[0,-3,0]})", {8, 13, 14}}}) {
    auto d = rotated(true);
    d.features[2].inputs = set(d.features[2].inputs, "plane", ir::json::parse(plane).value());
    const auto result = evaluator.evaluate(source(d)).value();
    solid(result, 6000, 2200, center);
  }
}

TEST_CASE("independent axis edits invalidate rotations and equivalent units reuse cache",
          "[sketch-axis]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto d = rotated(true);
  auto first = evaluator.evaluate(source(d)).value();
  solid(first, 6000, 2200, {18, 10, -5});
  counts(first, 2, 0);
  d.params.back().value = "5 mm";
  auto edited = evaluator.evaluate(source(d)).value();
  solid(edited, 6000, 2200, {23, 10, 0});
  counts(edited, 1, 2);
  CHECK(edited.features[1].hash == first.features[1].hash);
  CHECK(edited.features[3].hash != first.features[3].hash);
  d.params.back().value = "0.5 cm";
  auto equivalent = evaluator.evaluate(source(d)).value();
  solid(equivalent, 6000, 2200, {23, 10, 0});
  counts(equivalent, 0, 4);
  d.features[1].id = "producer";
  auto renamed = evaluator.evaluate(source(d)).value();
  solid(renamed, 6000, 2200, {23, 10, 0});
  counts(renamed, 0, 4);
  for (const auto& name : renamed.bodies.at("plate").names) CHECK(name.feature == "producer");
  solid(first, 6000, 2200, {18, 10, -5});
}

TEST_CASE("revolutions use profile-edge axes and signed authored directions", "[sketch-axis]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto d = turned();
  auto full = evaluator.evaluate(source(d)).value();
  solid(full, 750 * kPi, 350 * kPi, {8, 0, 15});
  counts(full, 2, 0);
  d.params.back().value = "90 deg";
  const double radial = 20 / (3 * kPi);
  auto quarter = evaluator.evaluate(source(d)).value();
  solid(quarter, 187.5 * kPi, 300 + 87.5 * kPi, {8 + radial, radial, 15});
  counts(quarter, 2, 1);
  reverse_line(d.features[0], 0);
  auto reversed = evaluator.evaluate(source(d)).value();
  solid(reversed, 187.5 * kPi, 300 + 87.5 * kPi, {8 + radial, -radial, 15});
  counts(reversed, 2, 0);
  reverse_line(d.features[0], 0);
  d.params.back().value = "-90 deg";
  auto negative = evaluator.evaluate(source(d)).value();
  solid(negative, 187.5 * kPi, 300 + 87.5 * kPi, {8 + radial, -radial, 15});
}

TEST_CASE("revolutions depend on a separately evaluated axis sketch", "[sketch-axis]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto d = turned(true);
  auto first = evaluator.evaluate(source(d)).value();
  solid(first, 750 * kPi, 350 * kPi, {8, 0, 15});
  counts(first, 2, 0);
  d.params.back().value = "4 mm";
  auto moved = evaluator.evaluate(source(d)).value();
  solid(moved, 1050 * kPi, 490 * kPi, {7, 0, 15});
  counts(moved, 2, 1);
  CHECK(moved.features[0].hash == first.features[0].hash);
  CHECK(moved.features[2].hash != first.features[2].hash);
  d.params.back().value = "0.4 cm";
  auto equivalent = evaluator.evaluate(source(d)).value();
  solid(equivalent, 1050 * kPi, 490 * kPi, {7, 0, 15});
  counts(equivalent, 0, 4);
}

TEST_CASE("unavailable axis sketches invalidate modifiers while independent branches continue",
          "[sketch-axis]") {
  auto kernel = kernel::make_occt_kernel().value();
  for (const bool suppress : {false, true}) {
    Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
    auto d = rotated(true);
    d.features.push_back(
        feature("downstream", "transform", R"({"bodies":["plate"],"translation":[1,0,0]})"));
    d.features.push_back(feature("independent", "extrude", R"({"profile":{"sketch":"outline"},
      "distance":"depth","result":{"new":"other","body_type":"brep"}})"));
    REQUIRE(evaluator.evaluate(source(d)).value().ok());
    evaluator.clear_cache();
    if (suppress)
      d.features[2].suppressed = true;
    else
      d.features[2].inputs =
          set(d.features[2].inputs, "plane",
              ir::json::parse(R"({"origin":[0,0,0],"normal":[0,0,0],"x_direction":"x"})").value());
    const auto result = evaluator.evaluate(source(d)).value();
    REQUIRE_FALSE(result.ok());
    REQUIRE_FALSE(result.features[3].errors.empty());
    CHECK(result.features[3].errors.front().code ==
          (suppress ? "input_suppressed" : "input_failed"));
    CHECK(result.features[3].errors.front().pointer == "/features/3/inputs/rotation/axis/sketch");
    REQUIRE_FALSE(result.features[4].errors.empty());
    CHECK(result.features[4].errors.front().code == "input_failed");
    CHECK_FALSE(result.bodies.at("plate").shape);
    CHECK(result.bodies.at("other").shape);
    CHECK(result.kernel_calls == 2);
  }
}

TEST_CASE("revolve axis failures and rollback stop dependent geometry", "[sketch-axis]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto d = turned(true);
  d.features[1].suppressed = true;
  auto suppressed = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(suppressed.ok());
  REQUIRE_FALSE(suppressed.features[2].errors.empty());
  CHECK(suppressed.features[2].errors.front().code == "input_suppressed");
  CHECK(suppressed.features[2].errors.front().pointer == "/features/2/inputs/axis/sketch");
  CHECK(suppressed.kernel_calls == 0);
  d.features[1].suppressed = false;
  d.features[1].inputs =
      set(d.features[1].inputs, "plane",
          ir::json::parse(R"({"origin":[0,0,0],"normal":[0,0,0],"x_direction":"x"})").value());
  d.history_end = "outline";
  auto rolled = evaluator.evaluate(source(d)).value();
  REQUIRE(rolled.ok());
  CHECK(rolled.features[1].status == FeatureStatus::kRolledBack);
  CHECK(rolled.features[2].status == FeatureStatus::kRolledBack);
  CHECK(rolled.kernel_calls == 0);
  d.history_end.reset();
  auto failed = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(failed.features[2].errors.empty());
  CHECK(failed.features[2].errors.front().code == "input_failed");
  CHECK(failed.features[2].errors.front().pointer == "/features/2/inputs/axis/sketch");
  CHECK(failed.kernel_calls == 0);
}

TEST_CASE("sketch-axis snapshots and names survive cache eviction and kernel teardown",
          "[sketch-axis]") {
  Evaluation retained;
  {
    auto kernel = kernel::make_occt_kernel().value();
    Evaluator evaluator(*kernel, {1, 64 * 1024 * 1024});
    const auto d = rotated(true);
    retained = evaluator.evaluate(source(d)).value();
    solid(retained, 6000, 2200, {18, 10, -5});
    counts(retained, 2, 0);
    auto rebuilt = evaluator.evaluate(source(d)).value();
    solid(rebuilt, 6000, 2200, {18, 10, -5});
    counts(rebuilt, 2, 0);
    CHECK(evaluator.cache_usage().entries == 1);
    evaluator.clear_cache();
    CHECK(evaluator.cache_usage().estimated_bytes == 0);
  }
  solid(retained, 6000, 2200, {18, 10, -5});
}

TEST_CASE("axis adapters retain named lines and avoid subtracting placed endpoints",
          "[sketch-axis]") {
  auto sketch = prepare_sketch(lines()).value();
  REQUIRE(sketch.lines.size() == 4);
  CHECK(sketch.lines.at("d").start == math::Vec2{0, 0});
  CHECK(sketch.lines.at("d").end == math::Vec2{0, 20});
  CHECK(std::ranges::any_of(sketch.paths.front(), [](const auto& curve) {
    return curve.point(curve.domain_start()) == math::Vec2{0, 20} &&
           curve.point(curve.domain_end()) == math::Vec2{0, 0};
  }));
  const auto inputs = evaluated(feature("rotate", "transform", R"({"bodies":["plate"],
    "rotation":{"axis":{"sketch":"s","entity":"d"},"angle":90}})"));
  auto absent = prepare_transform(inputs);
  REQUIRE_FALSE(absent);
  CHECK(absent.error().pointer == "/inputs/rotation/axis/sketch");
  sketch.plane.origin = {1e16, 1e16, 1e16};
  sketch.lines.at("d").end = {0, 1};
  CHECK(sketch.plane.origin.y + 1 == sketch.plane.origin.y);
  auto placed = prepare_transform(inputs, &sketch);
  REQUIRE(placed);
  CHECK(placed->axis_origin == sketch.plane.origin);
  CHECK(placed->axis_direction == math::Vec3{0, 1, 0});
  sketch.lines.at("d").end = {0, 0};
  auto zero = prepare_transform(inputs, &sketch);
  REQUIRE_FALSE(zero);
  CHECK(zero.error().pointer == "/inputs/rotation/axis/entity");
  sketch.lines.at("d") = {{1e308, 0}, {1e308, 1}};
  sketch.plane.origin = {1e308, 0, 0};
  auto overflow = prepare_transform(inputs, &sketch);
  REQUIRE_FALSE(overflow);
  CHECK(overflow.error().pointer == "/inputs/rotation/axis/entity");
  sketch.lines.clear();
  auto missing = prepare_transform(inputs, &sketch);
  REQUIRE_FALSE(missing);
  CHECK(missing.error().pointer == "/inputs/rotation/axis/entity");
}

TEST_CASE("axis references continue to require a named line in an earlier sketch",
          "[sketch-axis]") {
  for (const auto* entity : {"r", "missing"}) {
    auto d = rotated();
    d.features[0].inputs = part().features[0].inputs;
    const auto axis = ir::json::Object{{"sketch", "outline"}, {"entity", entity}};
    d.features[2].inputs =
        set(d.features[2].inputs, "rotation", ir::json::Object{{"axis", axis}, {"angle", 90.0}});
    auto result = ir::write_document(d);
    REQUIRE_FALSE(result);
    REQUIRE_FALSE(result.error().empty());
    CHECK(result.error().front().code ==
          (std::string_view(entity) == "r" ? "schema" : "unknown_id"));
    CHECK(result.error().front().pointer == "/features/2/inputs/rotation/axis/entity");
  }
  auto d = rotated(true);
  std::swap(d.features[2], d.features[3]);
  const auto forward = ir::write_document(d);
  REQUIRE_FALSE(forward);
  REQUIRE_FALSE(forward.error().empty());
  CHECK(forward.error().front().code == "forward_reference");
  CHECK(forward.error().front().pointer == "/features/2/inputs/rotation/axis/sketch");
}

TEST_CASE("revolve sketch axes outside the profile plane fail without stale geometry",
          "[sketch-axis]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto d = turned(true);
  const auto first = evaluator.evaluate(source(d)).value();
  solid(first, 750 * kPi, 350 * kPi, {8, 0, 15});
  const auto plane = *d.features[1].inputs.find("plane");
  d.features[1].inputs =
      set(d.features[1].inputs, "plane",
          ir::json::parse(R"({"origin":[5,2,0],"normal":"x","x_direction":"y"})").value());
  const auto failed = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(failed.ok());
  REQUIRE_FALSE(failed.features[2].errors.empty());
  CHECK(failed.features[2].errors.front().code == "kernel");
  CHECK(failed.features[2].errors.front().message.find("plane") != std::string::npos);
  CHECK_FALSE(failed.bodies.at("plate").shape);
  CHECK(failed.kernel_calls == 1);
  d.features[1].inputs = set(d.features[1].inputs, "plane", plane);
  const auto restored = evaluator.evaluate(source(d)).value();
  solid(restored, 750 * kPi, 350 * kPi, {8, 0, 15});
  counts(restored, 0, 4);
  solid(first, 750 * kPi, 350 * kPi, {8, 0, 15});
}
