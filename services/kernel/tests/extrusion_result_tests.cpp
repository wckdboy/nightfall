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
ir::Document modified_plate(const std::string& mode) {
  auto document = part(2);
  document.features.push_back(feature("tool_profile", "sketch", R"({"plane":{"datum":"xy"},
    "entities":[{"id":"r","type":"rect","corner":[5,0],"size":[10,20]}],"constraints":[]})"));
  const auto input = R"({"profile":{"sketch":"tool_profile"},"distance":"depth","result":{")" +
                     mode + R"(":"plate"}})";
  document.features.push_back(feature("modify", "extrude", input.c_str()));
  return document;
}
void valid_names(const BodyState& body) {
  REQUIRE(body.shape);
  for (const auto& name : body.names) CHECK(name.entity->owner() == body.shape);
  for (const auto* producer : {"base", "modify"})
    CHECK(std::ranges::any_of(body.names, [&](const auto& n) { return n.feature == producer; }));
}
}  // namespace

TEST_CASE("extrusion results join cut and intersect existing solids without temporary bodies") {
  auto kernel = kernel::make_occt_kernel().value();
  for (const std::string mode : {"join", "cut", "intersect"}) {
    CAPTURE(mode);
    Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
    const auto document = modified_plate(mode);
    const auto result = evaluator.evaluate(source(document)).value();
    REQUIRE(result.ok());
    CHECK(result.bodies.size() == 1);
    CHECK(result.kernel_calls == 3);
    const auto& body = result.bodies.at("plate");
    valid_names(body);
    CHECK_THAT(body.shape->measures().volume, WithinAbs(mode == "join" ? 9000 : 3000, 1e-7));
    CHECK_THAT(body.shape->measures().area, WithinAbs(mode == "join" ? 2700 : 1700, 1e-7));
    auto cached = evaluator.evaluate(source(document)).value();
    REQUIRE(cached.ok());
    CHECK(cached.kernel_calls == 0);
    CHECK(cached.cache_hits == 4);
    CHECK(cached.bodies.at("plate").shape == body.shape);
  }
}

TEST_CASE("extrusion result dependencies follow the current target and parametric tool") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto document = modified_plate("cut");
  const auto original = evaluator.evaluate(source(document)).value();
  REQUIRE(original.ok());
  document.features[3].inputs = ir::json::parse(R"({"profile":{"sketch":"tool_profile"},
    "distance":10,"result":{"cut":"plate"}})")
                                    .value();
  auto shorter = evaluator.evaluate(source(document)).value();
  REQUIRE(shorter.ok());
  CHECK(shorter.kernel_calls == 2);
  CHECK(shorter.cache_hits == 3);
  CHECK_THAT(shorter.bodies.at("plate").shape->measures().volume, WithinAbs(5000, 1e-7));
  document.features.insert(
      document.features.begin() + 3,
      feature("move", "transform", R"({"bodies":["plate"],"translation":[20,0,0]})"));
  auto moved = evaluator.evaluate(source(document)).value();
  REQUIRE(moved.ok());
  CHECK(moved.kernel_calls == 3);
  CHECK_THAT(moved.bodies.at("plate").shape->measures().volume, WithinAbs(6000, 1e-7));
  CHECK(moved.bodies.at("plate").hash != shorter.bodies.at("plate").hash);
  CHECK_THAT(original.bodies.at("plate").shape->measures().volume, WithinAbs(3000, 1e-7));
}

TEST_CASE("extrusion result history is rebound and retained snapshots survive eviction") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto document = modified_plate("join");
  const auto original = evaluator.evaluate(source(document)).value();
  REQUIRE(original.ok());
  document.features[1].id = "renamed_base";
  document.features[3].id = "renamed_modify";
  auto renamed = evaluator.evaluate(source(document)).value();
  REQUIRE(renamed.ok());
  CHECK(renamed.kernel_calls == 0);
  for (const auto& name : renamed.bodies.at("plate").names)
    CHECK(name.feature.starts_with("renamed_"));
  document.history_end = "outline";
  REQUIRE(evaluator.evaluate(source(document))->ok());
  evaluator.set_cache_limits({2, 64 * 1024 * 1024});
  evaluator.set_cache_limits({128, 64 * 1024 * 1024});
  document.history_end.reset();
  auto rebuilt = evaluator.evaluate(source(document)).value();
  REQUIRE(rebuilt.ok());
  CHECK(rebuilt.kernel_calls == 3);
  CHECK(rebuilt.binding_misses == 1);
  for (const auto& name : rebuilt.bodies.at("plate").names)
    CHECK(name.entity->owner() == rebuilt.bodies.at("plate").shape);
  evaluator.clear_cache();
  CHECK_THAT(original.bodies.at("plate").shape->measures().volume, WithinAbs(9000, 1e-7));
}

TEST_CASE("extrusion result rollback and suppression leave the target unchanged") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto document = modified_plate("cut");
  document.history_end = "tool_profile";
  auto rolled = evaluator.evaluate(source(document)).value();
  REQUIRE(rolled.ok());
  CHECK(rolled.features.back().status == FeatureStatus::kRolledBack);
  CHECK_THAT(rolled.bodies.at("plate").shape->measures().volume, WithinAbs(6000, 1e-7));
  document.history_end = "modify";
  auto inclusive = evaluator.evaluate(source(document)).value();
  REQUIRE(inclusive.ok());
  REQUIRE(inclusive.bodies.at("plate").shape);
  CHECK_THAT(inclusive.bodies.at("plate").shape->measures().volume, WithinAbs(3000, 1e-7));
  document.history_end.reset();
  document.features.back().suppressed = true;
  auto skipped = evaluator.evaluate(source(document)).value();
  REQUIRE(skipped.ok());
  CHECK_THAT(skipped.bodies.at("plate").shape->measures().volume, WithinAbs(6000, 1e-7));
}

TEST_CASE("extrusion result failure never publishes its temporary solid or a stale target") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto document = modified_plate("intersect");
  document.features[2].inputs = ir::json::parse(R"({"plane":{"datum":"xy"},
    "entities":[{"id":"r","type":"rect","corner":[50,0],"size":[10,20]}],"constraints":[]})")
                                    .value();
  document.features.push_back(
      feature("dependent", "transform", R"({"bodies":["plate"],"translation":[2,0,0]})"));
  document.features.push_back(feature("independent", "extrude", R"({"profile":{"sketch":"outline"},
    "distance":10,"result":{"new":"other","body_type":"brep"}})"));
  auto result = evaluator.evaluate(source(document)).value();
  REQUIRE_FALSE(result.ok());
  REQUIRE_FALSE(result.features[3].errors.empty());
  CHECK(result.features[3].errors.front().code == "kernel");
  REQUIRE_FALSE(result.features[4].errors.empty());
  CHECK(result.features[4].errors.front().code == "input_failed");
  CHECK_FALSE(result.bodies.at("plate").shape);
  CHECK(result.bodies.size() == 2);
  REQUIRE(result.bodies.at("other").shape);
  CHECK_THAT(result.bodies.at("other").shape->measures().volume, WithinAbs(2000, 1e-7));
  CHECK(evaluator.cache_usage().entries == 4);
}

TEST_CASE("extrusion result reports a suppressed target before doing kernel work") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto document = modified_plate("cut");
  document.features[1].suppressed = true;
  const auto result = evaluator.evaluate(source(document)).value();
  REQUIRE_FALSE(result.ok());
  REQUIRE_FALSE(result.features.back().errors.empty());
  CHECK(result.features.back().errors.front().code == "input_suppressed");
  CHECK(result.features.back().errors.front().pointer == "/features/3/inputs/result/cut");
  CHECK(result.kernel_calls == 0);
}

TEST_CASE("extrusion result supports symmetric circular cuts with preserved cutting faces") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto document = modified_plate("cut");
  document.features[2].inputs = ir::json::parse(R"({"plane":{"datum":"xy"},"entities":[
    {"id":"c","type":"circle","center":[5,10],"radius":2}],"constraints":[]})")
                                    .value();
  document.features[3].inputs = ir::json::parse(R"({"profile":{"sketch":"tool_profile"},
    "distance":80,"symmetric":true,"result":{"cut":"plate"}})")
                                    .value();
  const auto result = evaluator.evaluate(source(document)).value();
  REQUIRE(result.ok());
  valid_names(result.bodies.at("plate"));
  CHECK_THAT(result.bodies.at("plate").shape->measures().volume,
             WithinAbs(6000 - 120 * std::numbers::pi, 1e-5));
  CHECK_THAT(result.bodies.at("plate").shape->measures().area,
             WithinAbs(2200 + 112 * std::numbers::pi, 1e-5));
}
