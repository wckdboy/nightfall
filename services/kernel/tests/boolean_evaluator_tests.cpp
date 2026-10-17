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
ir::Document drilled_plate() {
  auto d = part(2);
  d.params.push_back({"radius", "2 mm", {}, {}, {}, {}});
  d.features.push_back(feature("hole_profile", "sketch", R"({"plane":{"origin":[0,0,-5],
    "normal":[0,0,1],"x_direction":[1,0,0]},"entities":[
    {"id":"circle","type":"circle","center":[5,10],"radius":"radius"}],"constraints":[]})"));
  d.features.push_back(feature("drill_solid", "extrude", R"({"profile":{"sketch":"hole_profile"},
    "distance":40,"result":{"new":"drill","body_type":"brep"}})"));
  d.features.push_back(
      feature("bore", "boolean", R"({"op":"subtract","target":"plate","tools":["drill"]})"));
  return d;
}
void check_plate(const Evaluation& result, double radius) {
  REQUIRE(result.ok());
  const auto& body = result.bodies.at("plate");
  REQUIRE(body.shape);
  CHECK_THAT(body.shape->measures().volume,
             WithinAbs(6000 - 30 * std::numbers::pi * radius * radius, 1e-5));
  for (const auto& name : body.names) CHECK(name.entity->owner() == body.shape);
}
}  // namespace

TEST_CASE("boolean evaluator cuts a parametric hole and caches complete naming history") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto document = drilled_plate();
  const auto first = evaluator.evaluate(source(document)).value();
  check_plate(first, 2);
  CHECK(first.kernel_calls == 3);
  CHECK_FALSE(first.bodies.at("drill").shape);
  CHECK_FALSE(first.bodies.at("drill").hash);
  const auto& names = first.bodies.at("plate").names;
  for (const auto* producer : {"base", "drill_solid", "bore"})
    CHECK(std::ranges::any_of(names, [&](const auto& n) { return n.feature == producer; }));
  auto cached = evaluator.evaluate(source(document)).value();
  check_plate(cached, 2);
  CHECK(cached.cache_hits == 5);
  CHECK(cached.kernel_calls == 0);
  CHECK_FALSE(cached.bodies.at("drill").shape);
  CHECK(cached.bodies.at("plate").shape == first.bodies.at("plate").shape);
  document.params.back().value = "3 mm";
  auto resized = evaluator.evaluate(source(document)).value();
  check_plate(resized, 3);
  CHECK(resized.kernel_calls == 2);
  CHECK(resized.cache_hits == 2);
  CHECK(resized.bodies.at("plate").hash != first.bodies.at("plate").hash);
  check_plate(first, 2);
  document.params.back().value = "2 mm";
  auto reverted = evaluator.evaluate(source(document)).value();
  check_plate(reverted, 2);
  CHECK(reverted.kernel_calls == 0);
  CHECK(reverted.cache_hits == 5);
}

TEST_CASE("boolean evaluator preserves tools only when requested and rejects retired consumers") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto document = drilled_plate();
  document.features.push_back(
      feature("move_tool", "transform", R"({"bodies":["drill"],"translation":[2,0,0]})"));
  auto retired = evaluator.evaluate(source(document)).value();
  REQUIRE_FALSE(retired.ok());
  REQUIRE_FALSE(retired.features.back().errors.empty());
  CHECK(retired.features.back().errors.front().code == "input_failed");
  CHECK(retired.features.back().errors.front().pointer == "/features/5/inputs/bodies/0");
  REQUIRE(retired.bodies.at("plate").shape);
  document.features[4].inputs =
      ir::json::parse(R"({"op":"subtract","target":"plate","tools":["drill"],"keep_tools":true})")
          .value();
  auto retained = evaluator.evaluate(source(document)).value();
  check_plate(retained, 2);
  REQUIRE(retained.bodies.at("drill").shape);
  CHECK_THAT(retained.bodies.at("drill").shape->measures().volume,
             WithinAbs(160 * std::numbers::pi, 1e-5));
  CHECK_THAT(retained.bodies.at("drill").shape->measures().centroid.x, WithinAbs(7, 1e-7));
  for (const auto& name : retained.bodies.at("drill").names) CHECK(name.feature == "drill_solid");
}

TEST_CASE("boolean evaluator hashes current target and tool states after transforms") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto document = drilled_plate();
  document.params.push_back({"travel", "20 mm", {}, {}, {}, {}});
  document.features.insert(
      document.features.begin() + 4,
      feature("position", "transform", R"({"bodies":["drill"],"translation":["travel",0,0]})"));
  auto outside = evaluator.evaluate(source(document)).value();
  REQUIRE(outside.ok());
  CHECK_THAT(outside.bodies.at("plate").shape->measures().volume, WithinAbs(6000, 1e-7));
  document.params.back().value = "0 mm";
  auto inside = evaluator.evaluate(source(document)).value();
  check_plate(inside, 2);
  CHECK(inside.kernel_calls == 2);
  CHECK(inside.cache_hits == 4);
  CHECK(inside.bodies.at("plate").hash != outside.bodies.at("plate").hash);
  document.features.insert(
      document.features.begin() + 4,
      feature("move_plate", "transform", R"({"bodies":["plate"],"translation":[20,0,0]})"));
  auto moved = evaluator.evaluate(source(document)).value();
  REQUIRE(moved.ok());
  CHECK_THAT(moved.bodies.at("plate").shape->measures().volume, WithinAbs(6000, 1e-7));
  CHECK(moved.kernel_calls == 2);
}

TEST_CASE("boolean rollback suppression and failure preserve unconsumed tools") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto document = drilled_plate();
  document.history_end = "drill_solid";
  auto rolled = evaluator.evaluate(source(document)).value();
  REQUIRE(rolled.ok());
  CHECK(rolled.features.back().status == FeatureStatus::kRolledBack);
  CHECK(rolled.bodies.at("drill").shape);
  CHECK_THAT(rolled.bodies.at("plate").shape->measures().volume, WithinAbs(6000, 1e-7));
  document.history_end = "bore";
  check_plate(evaluator.evaluate(source(document)).value(), 2);
  document.history_end.reset();
  document.features.back().suppressed = true;
  auto suppressed = evaluator.evaluate(source(document)).value();
  REQUIRE(suppressed.ok());
  CHECK(suppressed.bodies.at("drill").shape);
  CHECK_THAT(suppressed.bodies.at("plate").shape->measures().volume, WithinAbs(6000, 1e-7));
  document.features.back().suppressed = false;
  document.params.back().value = "50 mm";
  auto failed = evaluator.evaluate(source(document)).value();
  REQUIRE_FALSE(failed.ok());
  REQUIRE_FALSE(failed.features.back().errors.empty());
  CHECK(failed.features.back().errors.front().code == "kernel");
  CHECK_FALSE(failed.bodies.at("plate").shape);
  CHECK(failed.bodies.at("drill").shape);
  document.params.back().value = "2 mm";
  check_plate(evaluator.evaluate(source(document)).value(), 2);
}

TEST_CASE("boolean evaluator reports unavailable and invalid tools at their input pointers") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto document = drilled_plate();
  document.features[3].suppressed = true;
  auto missing = evaluator.evaluate(source(document)).value();
  REQUIRE_FALSE(missing.ok());
  REQUIRE_FALSE(missing.features.back().errors.empty());
  CHECK(missing.features.back().errors.front().code == "input_suppressed");
  CHECK(missing.features.back().errors.front().pointer == "/features/4/inputs/tools/0");
  CHECK_FALSE(missing.bodies.at("plate").shape);
  document.features[3].suppressed = false;
  document.features.back().inputs =
      ir::json::parse(R"({"op":"subtract","target":"plate","tools":["plate"]})").value();
  auto self = evaluator.evaluate(source(document)).value();
  REQUIRE_FALSE(self.ok());
  REQUIRE_FALSE(self.features.back().errors.empty());
  CHECK(self.features.back().errors.front().pointer == "/features/4/inputs/tools/0");
  CHECK(self.bodies.at("drill").shape);
}

TEST_CASE("boolean cached provenance rebinds renamed features and misses after source eviction") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto document = drilled_plate();
  const auto original = evaluator.evaluate(source(document)).value();
  check_plate(original, 2);
  document.features[1].id = "new_base";
  document.features[3].id = "new_drill";
  document.features[4].id = "new_bore";
  auto renamed = evaluator.evaluate(source(document)).value();
  check_plate(renamed, 2);
  CHECK(renamed.kernel_calls == 0);
  for (const auto& name : renamed.bodies.at("plate").names) CHECK(name.feature.starts_with("new_"));
  // Refresh only the first sketch, then evict both source solids while retaining the boolean.
  document.history_end = "outline";
  REQUIRE(evaluator.evaluate(source(document))->ok());
  evaluator.set_cache_limits({2, 64 * 1024 * 1024});
  evaluator.set_cache_limits({128, 64 * 1024 * 1024});
  document.history_end.reset();
  auto rebuilt = evaluator.evaluate(source(document)).value();
  check_plate(rebuilt, 2);
  CHECK(rebuilt.kernel_calls == 3);
  CHECK(rebuilt.binding_misses == 1);
  CHECK(rebuilt.bodies.at("plate").shape != original.bodies.at("plate").shape);
  check_plate(original, 2);
}

TEST_CASE("boolean evaluator unions and intersects tool groups with deduplicated names") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto document = part(2);
  document.features.push_back(feature("tool_base", "extrude", R"({"profile":{"sketch":"outline"},
    "distance":"depth","result":{"new":"tool","body_type":"brep"}})"));
  document.features.push_back(
      feature("place", "transform", R"({"bodies":["tool"],"translation":[5,0,0]})"));
  document.features.push_back(
      feature("combine", "boolean",
              R"({"op":"union","target":"plate","tools":["tool","tool"],"keep_tools":true})"));
  const auto joined = evaluator.evaluate(source(document)).value();
  REQUIRE(joined.ok());
  CHECK_THAT(joined.bodies.at("plate").shape->measures().volume, WithinAbs(9000, 1e-7));
  CHECK_THAT(joined.bodies.at("plate").shape->measures().area, WithinAbs(2700, 1e-7));
  REQUIRE(joined.bodies.at("tool").shape);
  const auto& names = joined.bodies.at("plate").names;
  for (const auto& name : names)
    CHECK(std::ranges::count_if(names, [&](const auto& candidate) {
            return name.feature == candidate.feature && name.role == candidate.role &&
                   name.entity->same_as(*candidate.entity);
          }) == 1);
  document.features.back().inputs =
      ir::json::parse(R"({"op":"intersect","target":"plate","tools":["tool"]})").value();
  const auto common = evaluator.evaluate(source(document)).value();
  REQUIRE(common.ok());
  CHECK(common.kernel_calls == 1);
  CHECK(common.cache_hits == 4);
  CHECK_THAT(common.bodies.at("plate").shape->measures().volume, WithinAbs(3000, 1e-7));
  CHECK_THAT(common.bodies.at("plate").shape->measures().area, WithinAbs(1700, 1e-7));
  CHECK_FALSE(common.bodies.at("tool").shape);
}

TEST_CASE("failed booleans block target dependents while tools and independent branches continue") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto document = drilled_plate();
  document.params.back().value = "50 mm";
  document.features.push_back(
      feature("move_target", "transform", R"({"bodies":["plate"],"translation":[2,0,0]})"));
  document.features.push_back(
      feature("move_tool", "transform", R"({"bodies":["drill"],"translation":[2,0,0]})"));
  document.features.push_back(feature("independent", "extrude", R"({"profile":{"sketch":"outline"},
    "distance":10,"result":{"new":"other","body_type":"brep"}})"));
  auto failed = evaluator.evaluate(source(document)).value();
  REQUIRE_FALSE(failed.ok());
  CHECK(failed.features[4].status == FeatureStatus::kFailed);
  REQUIRE_FALSE(failed.features[5].errors.empty());
  CHECK(failed.features[5].errors.front().code == "input_failed");
  CHECK(failed.features[6].status == FeatureStatus::kBuilt);
  CHECK_FALSE(failed.bodies.at("plate").shape);
  REQUIRE(failed.bodies.at("drill").shape);
  CHECK_THAT(failed.bodies.at("drill").shape->measures().centroid.x, WithinAbs(7, 1e-6));
  REQUIRE(failed.bodies.at("other").shape);
  CHECK_THAT(failed.bodies.at("other").shape->measures().volume, WithinAbs(2000, 1e-7));
  CHECK(evaluator.cache_usage().entries == 6);
}
