// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "evaluator.hpp"
#include "part_fixture.hpp"

#include <algorithm>
#include <numbers>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace nf;
using namespace nf::kernel_service;
using namespace nf::kernel_service::test;
using Catch::Matchers::WithinAbs;

namespace {
constexpr auto kPi = std::numbers::pi;
ir::Document turned_part() {
  auto document = part();
  document.params.push_back({"turn", "360 deg", {}, {}, {}, {}});
  document.features[0] = feature("outline", "sketch", R"({"plane":{"datum":"zx"},"entities":[
    {"id":"r","type":"rect","corner":[0,5],"size":["depth",5]}],"constraints":[]})");
  document.features[1] = feature("base", "revolve", R"({"profile":{"sketch":"outline"},
    "axis":{"origin":[0,0,0],"direction":"z"},"angle":"turn",
    "result":{"new":"plate","body_type":"brep"}})");
  return document;
}
void check_sleeve(const Evaluation& result, double height, double fraction, double x = 3) {
  REQUIRE(result.ok());
  REQUIRE(result.bodies.at("plate").shape);
  const auto& body = result.bodies.at("plate");
  CHECK_THAT(body.shape->measures().volume, WithinAbs(75 * kPi * height * fraction, 1e-5));
  CHECK_THAT(body.shape->measures().centroid.z, WithinAbs(height / 2, 1e-7));
  if (fraction == 1) CHECK_THAT(body.shape->measures().centroid.x, WithinAbs(x, 1e-7));
  REQUIRE_FALSE(body.names.empty());
  for (const auto& name : body.names) {
    CHECK(name.entity->owner() == body.shape);
    CHECK(name.feature == "base");
  }
}
}  // namespace

TEST_CASE("revolution features evaluate full and signed partial turns with dimensioned angles") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto document = turned_part();
  auto full = evaluator.evaluate(source(document)).value();
  check_sleeve(full, 30, 1);
  CHECK(full.kernel_calls == 2);
  CHECK_THAT(full.bodies.at("plate").shape->measures().area, WithinAbs(1050 * kPi, 1e-5));
  for (const auto sign : {-1.0, 1.0}) {
    document.params[3].value = std::to_string(sign * 90) + " deg";
    auto quarter = evaluator.evaluate(source(document)).value();
    check_sleeve(quarter, 30, 0.25);
    CHECK(quarter.kernel_calls == 2);
    CHECK(quarter.cache_hits == 1);
    const auto& m = quarter.bodies.at("plate").shape->measures();
    CHECK_THAT(m.area, WithinAbs(262.5 * kPi + 300, 1e-5));
    CHECK_THAT(m.centroid.x, WithinAbs(3 + 3500 / (225 * kPi), 1e-7));
    CHECK_THAT(m.centroid.y, WithinAbs(sign * 3500 / (225 * kPi), 1e-7));
  }
  document.params[3].value = "1.5707963267948966 rad";
  const auto radians = evaluator.evaluate(source(document)).value();
  check_sleeve(radians, 30, 0.25);
  CHECK(radians.kernel_calls == 0);
  CHECK(radians.cache_hits == 3);
}

TEST_CASE(
    "revolution feature edits invalidate only dependent geometry and restore cached results") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto document = turned_part();
  auto original = evaluator.evaluate(source(document)).value();
  check_sleeve(original, 30, 1);
  document.params[0].value = "60 mm";
  auto taller = evaluator.evaluate(source(document)).value();
  check_sleeve(taller, 60, 1);
  CHECK(taller.kernel_calls == 2);
  CHECK(taller.cache_hits == 0);
  document.params[2].value = 27.0;
  auto unused = evaluator.evaluate(source(document)).value();
  check_sleeve(unused, 60, 1);
  CHECK(unused.kernel_calls == 0);
  CHECK(unused.cache_hits == 3);
  document.params[0].value = "30 mm";
  auto restored = evaluator.evaluate(source(document)).value();
  check_sleeve(restored, 30, 1);
  CHECK(restored.kernel_calls == 0);
  CHECK(restored.bodies.at("plate").shape == original.bodies.at("plate").shape);
  auto cold_kernel = kernel::make_occt_kernel().value();
  Evaluator cold(*cold_kernel, {128, 64 * 1024 * 1024});
  check_sleeve(cold.evaluate(source(document)).value(), 30, 1);
}

TEST_CASE(
    "revolution feature cache names rebind and retained geometry survives upstream eviction") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto document = turned_part();
  auto original = evaluator.evaluate(source(document)).value();
  check_sleeve(original, 30, 1);
  document.features[1].id = "renamed";
  auto renamed = evaluator.evaluate(source(document)).value();
  REQUIRE(renamed.ok());
  CHECK(renamed.kernel_calls == 0);
  for (const auto& name : renamed.bodies.at("plate").names) CHECK(name.feature == "renamed");
  document.history_end = "outline";
  REQUIRE(evaluator.evaluate(source(document))->ok());
  evaluator.set_cache_limits({2, 64 * 1024 * 1024});
  evaluator.set_cache_limits({128, 64 * 1024 * 1024});
  document.history_end.reset();
  auto rebuilt = evaluator.evaluate(source(document)).value();
  REQUIRE(rebuilt.ok());
  CHECK(rebuilt.kernel_calls == 2);
  CHECK(rebuilt.binding_misses == 1);
  for (const auto& name : rebuilt.bodies.at("plate").names)
    CHECK(name.entity->owner() == rebuilt.bodies.at("plate").shape);
  evaluator.clear_cache();
  check_sleeve(original, 30, 1);
}

TEST_CASE("revolution result modes preserve target and tool names without exposing a tool body") {
  auto kernel = kernel::make_occt_kernel().value();
  for (const std::string mode : {"join", "cut", "intersect"}) {
    CAPTURE(mode);
    Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
    auto document = turned_part();
    document.features.pop_back();
    document.features.push_back(feature("tool_profile", "sketch", R"({"plane":{"datum":"zx"},
      "entities":[{"id":"r","type":"rect","corner":[0,7],"size":["depth",5]}],"constraints":[]})"));
    const auto inputs = R"({"profile":{"sketch":"tool_profile"},"axis":{"origin":[0,0,0],
      "direction":"z"},"angle":"turn","result":{")" +
                        mode + R"(":"plate"}})";
    document.features.push_back(feature("modify", "revolve", inputs.c_str()));
    const auto result = evaluator.evaluate(source(document)).value();
    REQUIRE(result.ok());
    CHECK(result.bodies.size() == 1);
    CHECK(result.kernel_calls == 3);
    const auto& body = result.bodies.at("plate");
    REQUIRE(body.shape);
    const double area = mode == "join" ? 119 : mode == "cut" ? 24 : 51;
    CHECK_THAT(body.shape->measures().volume, WithinAbs(area * 30 * kPi, 1e-5));
    for (const auto& name : body.names) CHECK(name.entity->owner() == body.shape);
    for (const auto* producer : {"base", "modify"})
      CHECK(std::ranges::any_of(body.names, [&](const auto& n) { return n.feature == producer; }));
    auto cached = evaluator.evaluate(source(document)).value();
    REQUIRE(cached.ok());
    CHECK(cached.kernel_calls == 0);
    CHECK(cached.cache_hits == 4);
    CHECK(cached.bodies.at("plate").shape == body.shape);
  }
}

TEST_CASE("revolution feature failure blocks dependents while independent branches continue") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto document = turned_part();
  document.params[3].value = "400 deg";
  document.features.push_back(feature("independent", "extrude", R"({"profile":{"sketch":"outline"},
    "distance":10,"result":{"new":"other","body_type":"brep"}})"));
  auto result = evaluator.evaluate(source(document)).value();
  REQUIRE_FALSE(result.ok());
  CHECK_FALSE(result.bodies.at("plate").shape);
  REQUIRE_FALSE(result.features[1].errors.empty());
  CHECK(result.features[1].errors.front().code == "kernel");
  REQUIRE_FALSE(result.features[2].errors.empty());
  CHECK(result.features[2].errors.front().code == "input_failed");
  REQUIRE(result.bodies.at("other").shape);
  CHECK_THAT(result.bodies.at("other").shape->measures().volume, WithinAbs(1500, 1e-7));
  CHECK(evaluator.cache_usage().entries == 2);
}

TEST_CASE("revolution feature rollback and suppressed sketches do not execute geometry") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto document = turned_part();
  document.history_end = "outline";
  auto rolled = evaluator.evaluate(source(document)).value();
  REQUIRE(rolled.ok());
  CHECK(rolled.kernel_calls == 0);
  CHECK(rolled.bodies.empty());
  document.history_end = "base";
  check_sleeve(evaluator.evaluate(source(document)).value(), 30, 1, 0);
  document.history_end.reset();
  document.features[0].suppressed = true;
  auto suppressed = evaluator.evaluate(source(document)).value();
  REQUIRE_FALSE(suppressed.ok());
  REQUIRE_FALSE(suppressed.features[1].errors.empty());
  CHECK(suppressed.features[1].errors.front().code == "input_suppressed");
  CHECK(suppressed.features[1].errors.front().pointer == "/features/1/inputs/profile/sketch");
  CHECK(suppressed.kernel_calls == 0);
}

TEST_CASE("revolution feature point-selected profiles preserve placement and analytic geometry") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto document = turned_part();
  document.features.pop_back();
  document.features[0].inputs = ir::json::parse(R"({"plane":{"origin":[3,7,9],
    "normal":[1,0,0],"x_direction":[0,1,0]},"entities":[
    {"id":"r","type":"rect","corner":[5,0],"size":[5,20]},
    {"id":"c","type":"circle","center":[20,0],"radius":2}],"constraints":[]})")
                                    .value();
  document.features[1].inputs =
      ir::json::parse(R"({"profile":{"sketch":"outline","regions":[[6,10]]},
    "axis":{"origin":[3,7,109],"direction":[0,0,2]},"angle":360,
    "result":{"new":"plate","body_type":"brep"}})")
          .value();
  auto result = evaluator.evaluate(source(document)).value();
  REQUIRE(result.ok());
  REQUIRE(result.bodies.at("plate").shape);
  const auto& m = result.bodies.at("plate").shape->measures();
  CHECK_THAT(m.volume, WithinAbs(1500 * kPi, 1e-5));
  CHECK_THAT(m.centroid.x, WithinAbs(3, 1e-7));
  CHECK_THAT(m.centroid.y, WithinAbs(7, 1e-7));
  CHECK_THAT(m.centroid.z, WithinAbs(19, 1e-7));
}

TEST_CASE("revolution feature unsupported axes and body types have precise diagnostics") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  for (const auto* text : {
           R"({"profile":{"sketch":"outline"},"axis":{"created_by":"outline"},
      "angle":360,"result":{"new":"plate","body_type":"brep"}})",
           R"({"profile":{"sketch":"outline"},"axis":{"origin":[0,0,0],"direction":"z"},
      "angle":360,"result":{"new":"plate","body_type":"mesh"}})"}) {
    auto document = turned_part();
    document.features[1].inputs = ir::json::parse(text).value();
    auto evaluated = evaluator.evaluate(source(document));
    REQUIRE(evaluated);
    const auto& result = *evaluated;
    REQUIRE_FALSE(result.ok());
    REQUIRE_FALSE(result.features[1].errors.empty());
    const auto& error = result.features[1].errors.front();
    CHECK(error.pointer ==
          (document.features[1].inputs.find("result")->find("body_type") &&
                   *document.features[1].inputs.find("result")->find("body_type")->if_string() ==
                       "mesh"
               ? "/features/1/inputs/result/body_type"
               : "/features/1/inputs/axis"));
    CHECK(result.kernel_calls == 0);
  }
}
