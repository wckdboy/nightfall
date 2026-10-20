// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "evaluator.hpp"
#include "model_inputs.hpp"
#include "part_fixture.hpp"

#include <numbers>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "nf/ir/inputs.hpp"

using namespace nf;
using namespace nf::kernel_service;
using namespace nf::kernel_service::test;
using Catch::Matchers::WithinAbs;

namespace {
using ir::json::Array;
using ir::json::Object;
using ir::json::Value;
constexpr double kPi = std::numbers::pi;
Value set(const Value& object, std::string_view key, Value value) {
  auto members = *object.if_object();
  for (auto& member : members)
    if (member.key == key) member.value = std::move(value);
  return members;
}
Value evaluated(const ir::Feature& f) {
  return ir::FeatureInputs::compile(f).value().evaluate().value();
}
ir::Document rotated() {
  auto d = part(2);
  d.params.push_back({"offset", "0 mm", {}, {}, {}, {}});
  d.params.push_back({"height", "20 mm", {}, {}, {}, {}});
  d.features.push_back(feature("axis_sketch", "sketch", R"({
    "plane":{"origin":["offset",0,0],"normal":"z","x_direction":"x"},
    "entities":[{"id":"axis","type":"line","start":[0,0],"end":[0,"height"]}],
    "constraints":[{"type":"vertical","entities":["axis"]},
      {"type":"fixed","entities":[{"entity":"axis","point":"start"}]}]})"));
  d.features.push_back(feature("rotate", "transform", R"({"bodies":["plate"],
    "translation":["step",0,0],
    "rotation":{"axis":{"sketch":"axis_sketch","entity":"axis"},"angle":90}})"));
  return d;
}
ir::Document turned() {
  auto d = part();
  d.features[0] = feature("outline", "sketch", R"({"plane":{"datum":"zx"},
    "entities":[{"id":"r","type":"rect","corner":[0,5],"size":["depth",5]}],"constraints":[]})");
  d.params.push_back({"offset", "0 mm", {}, {}, {}, {}});
  d.features.insert(d.features.begin() + 1, feature("axis_sketch", "sketch", R"({
    "plane":{"datum":"zx"},
    "entities":[{"id":"axis","type":"line","start":[0,"offset"],"end":["depth","offset"]}],
    "constraints":[{"type":"horizontal","entities":["axis"]}]})"));
  d.features[2] = feature("base", "revolve", R"({"profile":{"sketch":"outline"},
    "axis":{"sketch":"axis_sketch","entity":"axis"},"angle":360,
    "result":{"new":"plate","body_type":"brep"}})");
  return d;
}
void solid(const Evaluation& result, double volume, double area, math::Vec3 center,
           const std::string& id = "plate", bool complete = true) {
  for (const auto& f : result.features)
    for (const auto& e : f.errors) UNSCOPED_INFO(e.pointer << ": " << e.message);
  if (complete) REQUIRE(result.ok());
  const auto& body = result.bodies.at(id);
  REQUIRE(body.shape);
  const auto& m = body.shape->measures();
  CHECK(m.solids == 1);
  CHECK_THAT(m.volume, WithinAbs(volume, 1e-7));
  CHECK_THAT(m.area, WithinAbs(area, 1e-7));
  CHECK_THAT(m.centroid.x, WithinAbs(center.x, 1e-8));
  CHECK_THAT(m.centroid.y, WithinAbs(center.y, 1e-8));
  CHECK_THAT(m.centroid.z, WithinAbs(center.z, 1e-8));
  REQUIRE_FALSE(body.names.empty());
  for (const auto& name : body.names) CHECK(name.entity->owner() == body.shape);
}
}  // namespace

TEST_CASE("a standalone constrained centerline drives parametric rigid rotations",
          "[open-sketch]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto d = rotated();
  const auto first = evaluator.evaluate(source(d)).value();
  solid(first, 6000, 2200, {18, 10, -5});
  CHECK(first.kernel_calls == 2);
  d.params[3].value = "5 mm";
  const auto edited = evaluator.evaluate(source(d)).value();
  solid(edited, 6000, 2200, {23, 10, 0});
  CHECK(edited.kernel_calls == 1);
  CHECK(edited.cache_hits == 2);
  CHECK(edited.features[1].hash == first.features[1].hash);
  CHECK(edited.features[3].hash != first.features[3].hash);
  d.params[3].value = "0.5 cm";
  const auto equivalent = evaluator.evaluate(source(d)).value();
  solid(equivalent, 6000, 2200, {23, 10, 0});
  CHECK(equivalent.kernel_calls == 0);
  CHECK(equivalent.cache_hits == 4);
  d.params[4].value = "-20 mm";
  const auto reversed = evaluator.evaluate(source(d)).value();
  solid(reversed, 6000, 2200, {-7, 10, 0});
  CHECK(reversed.kernel_calls == 1);
  solid(first, 6000, 2200, {18, 10, -5});
}

TEST_CASE("a standalone line revolves an independent closed profile", "[open-sketch]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto d = turned();
  const auto sleeve = evaluator.evaluate(source(d)).value();
  solid(sleeve, 2250 * kPi, 1050 * kPi, {3, 0, 15});
  CHECK(sleeve.kernel_calls == 2);
  d.params.back().value = "5 mm";
  const auto cylinder = evaluator.evaluate(source(d)).value();
  solid(cylinder, 750 * kPi, 350 * kPi, {8, 0, 15});
  CHECK(cylinder.kernel_calls == 2);
  CHECK(cylinder.cache_hits == 1);
  d.params.back().value = "0.5 cm";
  const auto equivalent = evaluator.evaluate(source(d)).value();
  solid(equivalent, 750 * kPi, 350 * kPi, {8, 0, 15});
  CHECK(equivalent.kernel_calls == 0);
  CHECK(equivalent.cache_hits == 4);
}

TEST_CASE("open profile consumers fail without invalidating their reference sketch",
          "[open-sketch]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  for (const bool pick : {false, true}) {
    auto d = part(2);
    d.features[0] = feature("outline", "sketch", R"({"plane":{"datum":"xy"},"entities":[
      {"id":"axis","type":"line","start":[0,0],"end":[10,0]},
      {"id":"r","type":"rect","corner":[20,20],"size":[10,20]}],"constraints":[]})");
    if (pick)
      d.features[1].inputs =
          set(d.features[1].inputs, "profile",
              ir::json::parse(R"({"sketch":"outline","regions":[[25,30]]})").value());
    auto other = part(2);
    other.features[0].id = "other_sketch";
    other.features[1] = feature("other_base", "extrude", R"({"profile":{"sketch":"other_sketch"},
      "distance":"depth","result":{"new":"other","body_type":"brep"}})");
    d.features.insert(d.features.end(), other.features.begin(), other.features.end());
    d.features.push_back(feature("rotate_other", "transform", R"({"bodies":["other"],
      "rotation":{"axis":{"sketch":"outline","entity":"axis"},"angle":90}})"));
    const auto result = evaluator.evaluate(source(d)).value();
    REQUIRE_FALSE(result.ok());
    CHECK(result.features[0].hash.has_value());
    REQUIRE_FALSE(result.features[1].errors.empty());
    CHECK(result.features[1].errors.front().pointer == "/features/1/inputs/profile");
    CHECK(result.features[1].errors.front().message.find("open") != std::string::npos);
    CHECK_FALSE(result.bodies.at("plate").shape);
    solid(result, 6000, 2200, {5, -15, 10}, "other", false);
  }
}

TEST_CASE("open reference networks preserve entity and constraint validation", "[open-sketch]") {
  const auto f = feature("s", "sketch", R"({"plane":{"datum":"xy"},"entities":[
    {"id":"x","type":"line","start":[0,0],"end":[10,0]},
    {"id":"y","type":"line","start":[0,0],"end":[0,10]},
    {"id":"diagonal","type":"line","start":[0,0],"end":[10,10]}],
    "constraints":[{"type":"perpendicular","entities":["x","y"]}]})");
  auto sketch = prepare_sketch(evaluated(f));
  REQUIRE(sketch);
  CHECK(sketch->lines.size() == 3);
  const auto axis = evaluated(feature("rotate", "transform", R"({"bodies":["b"],
    "rotation":{"axis":{"sketch":"s","entity":"diagonal"},"angle":90}})"));
  const auto rotation = prepare_transform(axis, &*sketch);
  REQUIRE(rotation);
  CHECK_THAT(rotation->axis_direction.x, WithinAbs(std::sqrt(0.5), 1e-12));
  CHECK_THAT(rotation->axis_direction.y, WithinAbs(std::sqrt(0.5), 1e-12));
  auto invalid = f;
  invalid.inputs = set(f.inputs, "constraints",
                       ir::json::parse(R"([{"type":"parallel","entities":["x","y"]}])").value());
  const auto violation = prepare_sketch(evaluated(invalid));
  REQUIRE_FALSE(violation);
  CHECK(violation.error().pointer == "/inputs/constraints/0");
  invalid.inputs =
      set(f.inputs, "entities",
          ir::json::parse(R"([{"id":"zero","type":"line","start":[0,0],"end":[0,0]}])").value());
  invalid.inputs = set(invalid.inputs, "constraints", Array{});
  const auto zero = prepare_sketch(evaluated(invalid));
  REQUIRE_FALSE(zero);
  CHECK(zero.error().pointer == "/inputs/entities/0");
}

TEST_CASE("open arcs and rational splines remain usable reference sketch geometry",
          "[open-sketch]") {
  const auto f = feature("s", "sketch", R"({"plane":{"datum":"xy"},"entities":[
    {"id":"a","type":"arc","center":[0,0],"radius":5,"start_angle":0,"sweep_angle":90},
    {"id":"s","type":"spline","degree":2,"knots":[2,2,2,5,5,5],
     "control_points":[[20,0],[25,10],[30,0]],"weights":[1,0.5,1]}],"constraints":[]})");
  const auto sketch = prepare_sketch(evaluated(f));
  REQUIRE(sketch);
  CHECK(sketch->paths.empty());
  REQUIRE(sketch->segments.size() == 2);
  CHECK_THAT(sketch->segments[0].point(0).x, WithinAbs(5, 1e-12));
  CHECK_THAT(sketch->segments[0].point(1).y, WithinAbs(5, 1e-12));
  const auto& spline = sketch->segments[1];
  CHECK(spline.rational());
  CHECK(spline.domain_start() == 2);
  CHECK(spline.domain_end() == 5);
  CHECK(spline.points()[1] == math::Vec2{25, 10});
  CHECK(spline.weights()[1] == 0.5);
  CHECK_THAT(spline.point(3.5).x, WithinAbs(25, 1e-12));
  CHECK_THAT(spline.point(3.5).y, WithinAbs(10.0 / 3, 1e-12));
  const auto inputs = evaluated(feature("base", "revolve", R"({"profile":{"sketch":"s"},
    "axis":{"origin":[0,0,0],"direction":"x"},"angle":360,
    "result":{"new":"b","body_type":"brep"}})"));
  const auto profile = prepare_revolution(inputs, *sketch);
  REQUIRE_FALSE(profile);
  CHECK(profile.error().pointer == "/inputs/profile");
  CHECK(profile.error().message.find("open") != std::string::npos);
}

TEST_CASE("invalid or suppressed open axes fail dependents and recover from cache",
          "[open-sketch]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto d = rotated();
  const auto first = evaluator.evaluate(source(d)).value();
  solid(first, 6000, 2200, {18, 10, -5});
  d.params[4].value = "0 mm";
  auto failed = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(failed.ok());
  REQUIRE_FALSE(failed.features[2].errors.empty());
  CHECK(failed.features[2].errors.front().pointer == "/features/2/inputs/constraints/0/entities/0");
  CHECK(failed.features[3].errors.front().code == "input_failed");
  CHECK(failed.kernel_calls == 0);
  CHECK_FALSE(failed.bodies.at("plate").shape);
  d.history_end = "base";
  const auto rolled = evaluator.evaluate(source(d)).value();
  solid(rolled, 6000, 2200, {5, 10, 15});
  CHECK(rolled.features[2].status == FeatureStatus::kRolledBack);
  d.history_end.reset();
  d.features[2].suppressed = true;
  const auto suppressed = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(suppressed.features[3].errors.empty());
  CHECK(suppressed.features[3].errors.front().code == "input_suppressed");
  CHECK(suppressed.features[3].errors.front().pointer == "/features/3/inputs/rotation/axis/sketch");
  d.features[2].suppressed = false;
  d.params[4].value = "20 mm";
  const auto restored = evaluator.evaluate(source(d)).value();
  solid(restored, 6000, 2200, {18, 10, -5});
  CHECK(restored.kernel_calls == 0);
  CHECK(restored.cache_hits == 4);
}

TEST_CASE("cache byte budgets account for retained open spline geometry", "[open-sketch]") {
  auto d = part(1);
  d.features.resize(1);
  Array poles, knots{0.0, 0.0};
  for (int i = 0; i < 1024; ++i) {
    poles.push_back(Array{static_cast<double>(i), 0.0});
    if (i > 0) knots.push_back(static_cast<double>(i));
  }
  knots.push_back(1023.0);
  d.features[0].inputs = Object{{"plane", Object{{"datum", "xy"}}},
                                {"entities", Array{Object{{"id", "s"},
                                                          {"type", "spline"},
                                                          {"degree", 1.0},
                                                          {"knots", knots},
                                                          {"control_points", poles}}}},
                                {"constraints", Array{}}};
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {1, 64 * 1024 * 1024});
  const auto reference = evaluator.evaluate(source(d)).value();
  for (const auto& f : reference.features)
    for (const auto& e : f.errors) UNSCOPED_INFO(e.pointer << ": " << e.message);
  REQUIRE(reference.ok());
  CHECK(reference.kernel_calls == 0);
  const auto usage = evaluator.cache_usage();
  CHECK(usage.entries == 1);
  CHECK(usage.estimated_bytes > poles.size() * sizeof(math::Vec2));
  evaluator.set_cache_limits({1, usage.estimated_bytes - 1});
  CHECK(evaluator.cache_usage().entries == 0);
  const auto rebuilt = evaluator.evaluate(source(d)).value();
  REQUIRE(rebuilt.ok());
  CHECK(rebuilt.cache_hits == 0);
  CHECK(evaluator.cache_usage().estimated_bytes == 0);
}

TEST_CASE("open sketch axes and resulting solid readers survive eviction and teardown",
          "[open-sketch]") {
  Evaluation retained;
  {
    auto kernel = kernel::make_occt_kernel().value();
    Evaluator evaluator(*kernel, {1, 64 * 1024 * 1024});
    const auto d = turned();
    retained = evaluator.evaluate(source(d)).value();
    solid(retained, 2250 * kPi, 1050 * kPi, {3, 0, 15});
    const auto rebuilt = evaluator.evaluate(source(d)).value();
    solid(rebuilt, 2250 * kPi, 1050 * kPi, {3, 0, 15});
    CHECK(rebuilt.kernel_calls == 2);
    CHECK(rebuilt.cache_hits == 0);
    evaluator.clear_cache();
    CHECK(evaluator.cache_usage().estimated_bytes == 0);
  }
  solid(retained, 2250 * kPi, 1050 * kPi, {3, 0, 15});
}
