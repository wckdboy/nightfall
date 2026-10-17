// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "evaluator.hpp"
#include "part_fixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace nf;
using namespace nf::kernel_service;
using namespace nf::kernel_service::test;
using Catch::Matchers::WithinAbs;

TEST_CASE("document evaluator executes real geometry and reuses complete outputs") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto document = part();
  auto first = evaluator.evaluate(source(document));
  REQUIRE(first);
  REQUIRE(first->ok());
  CHECK(first->kernel_calls == 2);
  CHECK(first->cache_hits == 0);
  const auto& body = first->bodies.at("plate");
  CHECK_THAT(body.shape->measures().volume, WithinAbs(6000, 1e-8));
  CHECK_THAT(body.shape->measures().centroid.x, WithinAbs(8, 1e-8));
  REQUIRE(body.names.size() == 6);
  for (const auto& name : body.names) {
    CHECK(name.feature == "base");
    CHECK(name.entity->owner() == body.shape);
  }
  auto same = evaluator.evaluate(source(document)).value();
  REQUIRE(same.ok());
  CHECK(same.kernel_calls == 0);
  CHECK(same.cache_hits == 3);
  CHECK(same.bodies.at("plate").shape == body.shape);
  document.params[2].value = 2.0;
  auto unused = evaluator.evaluate(source(document)).value();
  CHECK(unused.kernel_calls == 0);
  CHECK(unused.cache_hits == 3);
  document.params[0].value = "40 mm";
  auto changed = evaluator.evaluate(source(document)).value();
  REQUIRE(changed.ok());
  CHECK(changed.kernel_calls == 2);
  CHECK(changed.cache_hits == 1);
  CHECK_THAT(changed.bodies.at("plate").shape->measures().volume, WithinAbs(8000, 1e-8));
  CHECK_THAT(body.shape->measures().volume, WithinAbs(6000, 1e-8));
}

TEST_CASE("body dependencies follow every intervening modification") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto document = part(30);
  const auto first = evaluator.evaluate(source(document)).value();
  REQUIRE(first.ok());
  CHECK(first.kernel_calls == 29);
  CHECK_THAT(first.bodies.at("plate").shape->measures().centroid.x, WithinAbs(89, 1e-7));
  document.params[1].value = "4 mm";
  const auto changed = evaluator.evaluate(source(document)).value();
  REQUIRE(changed.ok());
  CHECK(changed.kernel_calls == 28);
  CHECK(changed.cache_hits == 2);
  CHECK_THAT(changed.bodies.at("plate").shape->measures().centroid.x, WithinAbs(117, 1e-7));
  CHECK(changed.bodies.at("plate").hash != first.bodies.at("plate").hash);
}

TEST_CASE("rollback is inclusive and suppression skips expressions") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto document = part();
  document.features.back().inputs =
      ir::json::parse(R"({"bodies":["plate"],"translation":["unknown",0,0]})").value();
  document.history_end = "base";
  auto rolled = evaluator.evaluate(source(document)).value();
  REQUIRE(rolled.ok());
  CHECK(rolled.kernel_calls == 1);
  CHECK(rolled.features.back().status == FeatureStatus::kRolledBack);
  CHECK_THAT(rolled.bodies.at("plate").shape->measures().centroid.x, WithinAbs(5, 1e-8));
  document.history_end.reset();
  document.features.back().suppressed = true;
  auto suppressed = evaluator.evaluate(source(document)).value();
  REQUIRE(suppressed.ok());
  CHECK(suppressed.features.back().status == FeatureStatus::kSuppressed);
  document.history_end = "move_2";
  document.features.push_back(
      feature("after", "transform", R"({"bodies":["plate"],"translation":["unknown",0,0]})"));
  CHECK(evaluator.evaluate(source(document))->ok());
}

TEST_CASE("suppressed producers fail dependents while independent branches continue") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto document = part();
  document.features[0].suppressed = true;
  document.features.push_back(feature("independent", "sketch", R"({"plane":{"datum":"xy"},
    "entities":[{"id":"c","type":"circle","center":[0,0],"radius":2}],"constraints":[]})"));
  document.features.push_back(feature("other", "extrude", R"({"profile":{"sketch":"independent"},
    "distance":2,"result":{"new":"other_body","body_type":"brep"}})"));
  auto result = evaluator.evaluate(source(document)).value();
  CHECK_FALSE(result.ok());
  CHECK(result.features[1].errors.front().code == "input_suppressed");
  CHECK(result.features[1].errors.front().pointer == "/features/1/inputs/profile/sketch");
  CHECK(result.features[2].errors.front().code == "input_failed");
  CHECK_FALSE(result.bodies.at("plate").shape);
  REQUIRE(result.bodies.at("other_body").shape);
  CHECK(result.kernel_calls == 1);
}

TEST_CASE("failed modifiers invalidate downstream state instead of using stale solids") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto document = part(4);
  REQUIRE(evaluator.evaluate(source(document))->ok());
  document.features[2].inputs =
      ir::json::parse(R"({"bodies":["plate"],"translation":["unknown",0,0]})").value();
  auto result = evaluator.evaluate(source(document)).value();
  CHECK_FALSE(result.ok());
  CHECK(result.features[2].status == FeatureStatus::kFailed);
  CHECK(result.features[3].errors.front().code == "input_failed");
  CHECK_FALSE(result.bodies.at("plate").shape);
  CHECK_FALSE(result.bodies.at("plate").hash);
  CHECK(result.kernel_calls == 0);
}

TEST_CASE("cached local roles rebind to renamed producing features") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto document = part();
  auto original = evaluator.evaluate(source(document)).value();
  document.features[1].id = "renamed";
  auto renamed = evaluator.evaluate(source(document)).value();
  REQUIRE(renamed.ok());
  CHECK(renamed.kernel_calls == 0);
  CHECK(renamed.cache_hits == 3);
  for (const auto& name : renamed.bodies.at("plate").names) {
    CHECK(name.feature == "renamed");
    CHECK(name.entity->owner() == renamed.bodies.at("plate").shape);
  }
  CHECK(original.bodies.at("plate").names.front().feature == "base");
}

TEST_CASE("retained downstream cache cannot bind history to rebuilt upstream snapshots") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {3, 64 * 1024 * 1024});
  auto document = part();
  auto original = evaluator.evaluate(source(document)).value();
  document.history_end = "outline";
  REQUIRE(evaluator.evaluate(source(document))->ok());
  // Refresh the sketch, evict only the extrusion, then allow space for its rebuilt snapshot.
  evaluator.set_cache_limits({2, 64 * 1024 * 1024});
  evaluator.set_cache_limits({8, 64 * 1024 * 1024});
  document.history_end.reset();
  auto rebuilt = evaluator.evaluate(source(document)).value();
  REQUIRE(rebuilt.ok());
  CHECK(rebuilt.kernel_calls == 2);
  CHECK(rebuilt.binding_misses == 1);
  CHECK(rebuilt.cache_hits == 1);
  CHECK(rebuilt.bodies.at("plate").shape != original.bodies.at("plate").shape);
  for (const auto& name : rebuilt.bodies.at("plate").names)
    CHECK(name.entity->owner() == rebuilt.bodies.at("plate").shape);
  CHECK_THAT(original.bodies.at("plate").shape->measures().centroid.x, WithinAbs(8, 1e-8));
}

TEST_CASE("invalid documents are rejected before any kernel call or cache mutation") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  REQUIRE(evaluator.evaluate(source(part()))->ok());
  auto usage = evaluator.cache_usage();
  CHECK_FALSE(evaluator.evaluate("{}"));
  CHECK(evaluator.cache_usage().entries == usage.entries);
  CHECK(evaluator.cache_usage().estimated_bytes == usage.estimated_bytes);
}

namespace {
class FaultKernel final : public kernel::IKernel {
 public:
  kernel::IKernel& backend;
  int transforms = 0, fail_on = 0;
  bool lose_history = false;
  explicit FaultKernel(kernel::IKernel& real) : backend(real) {}
  std::string_view identity() const noexcept override { return backend.identity(); }
  Capabilities capabilities() const noexcept override { return backend.capabilities(); }
  std::expected<kernel::OperationResult, kernel::KernelError> extrude(
      const kernel::Extrusion& input) override {
    return backend.extrude(input);
  }
  std::expected<kernel::OperationResult, kernel::KernelError> boolean_operation(
      const kernel::ShapePtr& target, const std::vector<kernel::ShapePtr>& tools,
      kernel::BooleanOp op) override {
    auto result = backend.boolean_operation(target, tools, op);
    if (result && lose_history) result->history.clear();
    return result;
  }
  std::expected<kernel::OperationResult, kernel::KernelError> transform(
      const kernel::ShapePtr& shape, const kernel::RigidTransform& input) override {
    if (++transforms == fail_on)
      return std::unexpected(kernel::KernelError{"kernel", "injected failure"});
    auto result = backend.transform(shape, input);
    if (result && lose_history) result->history.clear();
    return result;
  }
};
}  // namespace

TEST_CASE("multi-body features publish atomically and never cache partial success") {
  auto real = kernel::make_occt_kernel().value();
  FaultKernel kernel(*real);
  kernel.fail_on = 2;
  Evaluator evaluator(kernel, {128, 64 * 1024 * 1024});
  auto document = part(2);
  document.features.push_back(feature("other", "extrude", R"({"profile":{"sketch":"outline"},
    "distance":20,"result":{"new":"other_body","body_type":"brep"}})"));
  document.features.push_back(feature(
      "move_both", "transform", R"({"bodies":["plate","other_body"],"translation":[7,0,0]})"));
  document.features.push_back(feature("independent", "extrude", R"({"profile":{"sketch":"outline"},
    "distance":10,"result":{"new":"third","body_type":"brep"}})"));
  auto failed = evaluator.evaluate(source(document)).value();
  CHECK_FALSE(failed.ok());
  CHECK_FALSE(failed.bodies.at("plate").shape);
  CHECK_FALSE(failed.bodies.at("other_body").shape);
  CHECK(failed.bodies.at("third").shape);
  CHECK(failed.kernel_calls == 5);
  kernel.fail_on = 0;
  auto retried = evaluator.evaluate(source(document)).value();
  REQUIRE(retried.ok());
  CHECK(retried.kernel_calls == 2);
  CHECK(retried.cache_hits == 4);
  CHECK_THAT(retried.bodies.at("plate").shape->measures().centroid.x, WithinAbs(12, 1e-8));
  CHECK_THAT(retried.bodies.at("other_body").shape->measures().centroid.x, WithinAbs(12, 1e-8));
}

TEST_CASE("incomplete operation history is an error before cache admission") {
  auto real = kernel::make_occt_kernel().value();
  FaultKernel kernel(*real);
  kernel.lose_history = true;
  Evaluator evaluator(kernel, {128, 64 * 1024 * 1024});
  auto result = evaluator.evaluate(source(part())).value();
  CHECK_FALSE(result.ok());
  CHECK_FALSE(result.bodies.at("plate").shape);
  CHECK(result.features.back().errors.front().code == "kernel");
  CHECK(evaluator.cache_usage().entries == 2);
  kernel.lose_history = false;
  auto retried = evaluator.evaluate(source(part())).value();
  REQUIRE(retried.ok());
  CHECK(retried.kernel_calls == 1);
}

TEST_CASE("boolean naming failure publishes nothing and leaves tools available for retry") {
  auto real = kernel::make_occt_kernel().value();
  FaultKernel kernel(*real);
  kernel.lose_history = true;
  Evaluator evaluator(kernel, {128, 64 * 1024 * 1024});
  auto document = part(2);
  document.features.push_back(feature("tool", "extrude", R"({"profile":{"sketch":"outline"},
    "distance":20,"result":{"new":"tool_body","body_type":"brep"}})"));
  document.features.push_back(
      feature("cut", "boolean", R"({"op":"subtract","target":"plate","tools":["tool_body"]})"));
  auto failed = evaluator.evaluate(source(document)).value();
  REQUIRE_FALSE(failed.ok());
  REQUIRE_FALSE(failed.features.back().errors.empty());
  CHECK(failed.features.back().errors.front().message == "incomplete naming history");
  CHECK_FALSE(failed.bodies.at("plate").shape);
  CHECK(failed.bodies.at("tool_body").shape);
  CHECK(evaluator.cache_usage().entries == 3);
  kernel.lose_history = false;
  auto retried = evaluator.evaluate(source(document)).value();
  REQUIRE(retried.ok());
  CHECK(retried.kernel_calls == 1);
  CHECK(retried.cache_hits == 3);
  CHECK_FALSE(retried.bodies.at("tool_body").shape);
  CHECK_THAT(retried.bodies.at("plate").shape->measures().volume, WithinAbs(2000, 1e-7));
}

TEST_CASE("unsupported modifiers invalidate their intended current bodies") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto document = part();
  document.features.insert(
      document.features.begin() + 2,
      feature("round", "fillet", R"({"edges":{"created_by":"base"},"radius":1})"));
  auto result = evaluator.evaluate(source(document)).value();
  CHECK_FALSE(result.ok());
  CHECK(result.features[2].errors.front().message == "feature is not implemented: fillet");
  CHECK(result.features[3].errors.front().code == "input_failed");
  CHECK_FALSE(result.bodies.at("plate").shape);
}

TEST_CASE("equivalent document units reuse normalized geometry and eviction preserves readers") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto document = part();
  const auto original = evaluator.evaluate(source(document)).value();
  document.units.length = "cm";
  document.features[0].inputs = ir::json::parse(R"({"plane":{"datum":"xy"},"entities":[
    {"id":"r","type":"rect","corner":[0,0],"size":[1,2]}],"constraints":[]})")
                                    .value();
  auto converted = evaluator.evaluate(source(document)).value();
  REQUIRE(converted.ok());
  CHECK(converted.kernel_calls == 0);
  CHECK(converted.cache_hits == 3);
  evaluator.clear_cache();
  CHECK(evaluator.cache_usage().entries == 0);
  CHECK_THAT(original.bodies.at("plate").shape->measures().volume, WithinAbs(6000, 1e-8));
  CHECK(evaluator.evaluate(source(document))->kernel_calls == 2);
}
