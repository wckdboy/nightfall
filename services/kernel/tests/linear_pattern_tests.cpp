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
using ir::json::Array;
using ir::json::Object;
using ir::json::Value;
Value set(const Value& object, std::string_view key, Value value) {
  auto members = *object.if_object();
  for (auto& member : members)
    if (member.key == key) member.value = std::move(value);
  return members;
}
ir::Document patterned() {
  auto d = part();
  d.params.push_back({"pitch", "15 mm", {}, {}, {}, {}});
  d.params.push_back({"copies", 3.0, {}, {}, {}, {}});
  d.features.insert(d.features.begin() + 2,
                    feature("repeat", "pattern_linear", R"({"bodies":["plate"],"count":"copies",
        "spacing":"pitch","direction":"x"})"));
  return d;
}
void solid(const Evaluation& result, double volume, double area, math::Vec3 center,
           std::size_t solids, const std::string& id = "plate", bool complete = true) {
  for (const auto& f : result.features)
    for (const auto& e : f.errors) UNSCOPED_INFO(e.pointer << ": " << e.message);
  if (complete) REQUIRE(result.ok());
  const auto& body = result.bodies.at(id);
  REQUIRE(body.shape);
  const auto& m = body.shape->measures();
  CHECK(m.solids == solids);
  CHECK_THAT(m.volume, WithinAbs(volume, 1e-7));
  CHECK_THAT(m.area, WithinAbs(area, 1e-7));
  CHECK_THAT(m.centroid.x, WithinAbs(center.x, 1e-8));
  CHECK_THAT(m.centroid.y, WithinAbs(center.y, 1e-8));
  CHECK_THAT(m.centroid.z, WithinAbs(center.z, 1e-8));
  REQUIRE_FALSE(body.names.empty());
  for (const auto& name : body.names) CHECK(name.entity->owner() == body.shape);
}
void names(const Evaluation& result, const std::string& seed = "base",
           const std::string& pattern = "repeat", const std::string& body = "plate") {
  const auto& named = result.bodies.at(body).names;
  CHECK(std::ranges::any_of(named, [&](const auto& n) { return n.feature == seed; }));
  CHECK(std::ranges::any_of(
      named, [&](const auto& n) { return n.feature == pattern && n.role == "instance"; }));
  for (const auto& n : named) CHECK((n.feature == seed || n.feature == pattern));
}
class FaultKernel final : public kernel::IKernel {
 public:
  kernel::IKernel& backend;
  int transforms = 0, unions = 0, fail_transform = 0, fail_union = 0;
  bool lose_copy_history = false, lose_union_history = false;
  explicit FaultKernel(kernel::IKernel& real) : backend(real) {}
  std::string_view identity() const noexcept override { return backend.identity(); }
  Capabilities capabilities() const noexcept override { return backend.capabilities(); }
  std::expected<std::vector<kernel::EntityPtr>, kernel::KernelError> edges(
      const kernel::EntityPtr& face) override {
    return backend.edges(face);
  }
  std::expected<kernel::OperationResult, kernel::KernelError> chamfer(
      const kernel::ShapePtr& shape, const std::vector<kernel::EntityPtr>& edges,
      double distance) override {
    return backend.chamfer(shape, edges, distance);
  }
  std::expected<std::string, kernel::KernelError> export_step(
      const std::vector<kernel::ShapePtr>& bodies) override {
    return backend.export_step(bodies);
  }
  std::expected<double, kernel::KernelError> face_distance(const kernel::EntityPtr& e,
                                                           math::Vec3 point) override {
    return backend.face_distance(e, point);
  }
  std::expected<std::vector<kernel::FaceInfo>, kernel::KernelError> faces(
      const kernel::ShapePtr& s) override {
    return backend.faces(s);
  }
  std::expected<kernel::Plane, kernel::KernelError> face_plane(
      const kernel::EntityPtr& e) override {
    return backend.face_plane(e);
  }
  std::expected<kernel::OperationResult, kernel::KernelError> mirror(
      const kernel::ShapePtr& shape, const kernel::Plane& plane) override {
    return backend.mirror(shape, plane);
  }
  std::expected<kernel::OperationResult, kernel::KernelError> extrude(
      const kernel::Extrusion& input) override {
    return backend.extrude(input);
  }
  std::expected<kernel::OperationResult, kernel::KernelError> revolve(
      const kernel::Revolution& input) override {
    return backend.revolve(input);
  }
  std::expected<kernel::OperationResult, kernel::KernelError> transform(
      const kernel::ShapePtr& input, const kernel::RigidTransform& t) override {
    if (++transforms == fail_transform)
      return std::unexpected(kernel::KernelError{"kernel", "injected pattern copy failure"});
    auto result = backend.transform(input, t);
    if (result && lose_copy_history) result->history.clear();
    return result;
  }
  std::expected<kernel::OperationResult, kernel::KernelError> boolean_operation(
      const kernel::ShapePtr& target, const std::vector<kernel::ShapePtr>& tools,
      kernel::BooleanOp op) override {
    if (++unions == fail_union)
      return std::unexpected(kernel::KernelError{"kernel", "injected pattern union failure"});
    auto result = backend.boolean_operation(target, tools, op);
    if (result && lose_union_history) result->history.clear();
    return result;
  }
};
}  // namespace

TEST_CASE("linear body patterns repeat original seeds and preserve inherited and instance names",
          "[linear-pattern]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto d = patterned();
  const auto first = evaluator.evaluate(source(d)).value();
  solid(first, 18000, 6600, {23, 10, 15}, 3);
  names(first);
  CHECK(first.kernel_calls == 5);
  CHECK(first.bodies.size() == 1);
  CHECK_THAT(first.bodies.at("plate").shape->measures().minimum.x, WithinAbs(3, 1e-8));
  CHECK_THAT(first.bodies.at("plate").shape->measures().maximum.x, WithinAbs(43, 1e-8));
  const auto cached = evaluator.evaluate(source(d)).value();
  solid(cached, 18000, 6600, {23, 10, 15}, 3);
  names(cached);
  CHECK(cached.kernel_calls == 0);
  CHECK(cached.cache_hits == 4);
  d.params[2].value = 2.0;
  CHECK(evaluator.evaluate(source(d))->cache_hits == 4);
}

TEST_CASE("linear pattern spacing edits and unit equivalent inputs rebuild only dependents",
          "[linear-pattern]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto d = patterned();
  const auto first = evaluator.evaluate(source(d)).value();
  solid(first, 18000, 6600, {23, 10, 15}, 3);
  d.params[3].value = "20 mm";
  const auto edited = evaluator.evaluate(source(d)).value();
  solid(edited, 18000, 6600, {28, 10, 15}, 3);
  CHECK(edited.kernel_calls == 4);
  CHECK(edited.cache_hits == 2);
  CHECK(edited.features[1].hash == first.features[1].hash);
  CHECK(edited.features[2].hash != first.features[2].hash);
  d.params[3].value = "2 cm";
  const auto equivalent = evaluator.evaluate(source(d)).value();
  solid(equivalent, 18000, 6600, {28, 10, 15}, 3);
  CHECK(equivalent.kernel_calls == 0);
  CHECK(equivalent.cache_hits == 4);
  d.params[3].value = "15 mm";
  d.params[4].value = 4.0;
  const auto more = evaluator.evaluate(source(d)).value();
  solid(more, 24000, 8800, {30.5, 10, 15}, 4);
  CHECK(more.kernel_calls == 5);
  CHECK(more.cache_hits == 2);
  solid(first, 18000, 6600, {23, 10, 15}, 3);
}

TEST_CASE("linear body patterns preserve periodic faces and through holes", "[linear-pattern]") {
  auto d = patterned();
  d.features[0] = feature("outline", "sketch", R"({"plane":{"datum":"xy"},"entities":[
    {"id":"outer","type":"circle","center":[0,0],"radius":5},
    {"id":"inner","type":"circle","center":[0,0],"radius":2}],"constraints":[]})");
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  constexpr double pi = std::numbers::pi;
  const auto result = evaluator.evaluate(source(d)).value();
  solid(result, 1890 * pi, 1386 * pi, {18, 0, 15}, 3);
  names(result);
  CHECK(result.bodies.at("plate").shape->measures().faces == 12);
  const auto cached = evaluator.evaluate(source(d)).value();
  solid(cached, 1890 * pi, 1386 * pi, {18, 0, 15}, 3);
  names(cached);
  CHECK(cached.kernel_calls == 0);
  CHECK(cached.cache_hits == 4);
}

TEST_CASE("linear patterns fuse touching and overlapping copies without multiplying mass",
          "[linear-pattern]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  for (const double pitch : {5.0, 10.0, -5.0}) {
    auto d = patterned();
    d.params[3].value = std::to_string(pitch) + " mm";
    const double width = 10 + 2 * std::abs(pitch);
    const auto result = evaluator.evaluate(source(d)).value();
    solid(result, width * 600, 100 * width + 1200, {8 + pitch, 10, 15}, 1);
    names(result);
    CHECK(result.bodies.at("plate").shape->measures().faces == 6);
  }
}

TEST_CASE("linear patterns normalize arbitrary directions and support signed spacing",
          "[linear-pattern]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto d = patterned();
  d.params[3].value = "-15 mm";
  d.features[2].inputs = set(d.features[2].inputs, "direction", Array{2.0, 0.0, 0.0});
  const auto negative = evaluator.evaluate(source(d)).value();
  solid(negative, 18000, 6600, {-7, 10, 15}, 3);
  names(negative);
  d.params[3].value = "50 mm";
  d.features[2].inputs = set(d.features[2].inputs, "direction", Array{0.0, 3.0, 4.0});
  const auto tilted = evaluator.evaluate(source(d)).value();
  solid(tilted, 18000, 6600, {8, 40, 55}, 3);
  names(tilted);
}

TEST_CASE("single instance linear patterns preserve geometry and seed provenance",
          "[linear-pattern]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto d = patterned();
  d.params[3].value = "0 mm";
  d.params[4].value = 1.0;
  const auto result = evaluator.evaluate(source(d)).value();
  solid(result, 6000, 2200, {8, 10, 15}, 1);
  CHECK(result.kernel_calls == 3);
  for (const auto& name : result.bodies.at("plate").names) CHECK(name.feature == "base");
}

TEST_CASE("linear patterns validate body targets spacing count direction and finite work",
          "[linear-pattern]") {
  auto kernel = kernel::make_occt_kernel().value();
  struct Invalid {
    const char* input;
    const char* pointer;
  };
  for (const auto& [input, pointer] :
       {Invalid{R"({"bodies":["plate"],"count":65,"spacing":15,"direction":"x"})", "/count"},
        Invalid{R"({"bodies":["plate"],"count":"2.5","spacing":15,"direction":"x"})", "/count"},
        Invalid{R"({"bodies":["plate"],"count":3,"spacing":0,"direction":"x"})", "/spacing"},
        Invalid{R"({"bodies":["plate"],"count":3,"spacing":"0.0000005 mm","direction":"x"})",
                "/spacing"},
        Invalid{R"({"bodies":["plate"],"count":3,"spacing":"1e308 mm","direction":"x"})",
                "/spacing"},
        Invalid{R"({"bodies":["plate"],"count":3,"spacing":15,"direction":[0,0,0]})", "/direction"},
        Invalid{R"({"bodies":["plate","plate"],"count":3,"spacing":15,"direction":"x"})",
                "/bodies/1"},
        Invalid{R"({"features":["base"],"count":3,"spacing":15,"direction":"x"})", "/features"}}) {
    Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
    auto d = patterned();
    d.features[2].inputs = ir::json::parse(input).value();
    const auto result = evaluator.evaluate(source(d)).value();
    REQUIRE_FALSE(result.ok());
    REQUIRE_FALSE(result.features[2].errors.empty());
    CHECK(result.features[2].errors.front().pointer == "/features/2/inputs" + std::string(pointer));
    CHECK(result.kernel_calls == 1);
    CHECK_FALSE(result.bodies.at("plate").shape);
    CHECK(result.features[3].errors.front().code == "input_failed");
  }
}

TEST_CASE("linear pattern work limit includes all body seeds before copying", "[linear-pattern]") {
  auto d = part(2);
  Array bodies{"plate"};
  for (int i = 1; i < 33; ++i) {
    const auto id = "body_" + std::to_string(i);
    const auto text = R"({"profile":{"sketch":"outline"},"distance":20,"result":{"new":")" + id +
                      R"(","body_type":"brep"}})";
    d.features.push_back(feature("seed_" + std::to_string(i), "extrude", text.c_str()));
    bodies.push_back(id);
  }
  auto pattern = feature("repeat", "pattern_linear", R"({"bodies":["plate"],"count":2,
    "spacing":15,"direction":"x"})");
  pattern.inputs = set(pattern.inputs, "bodies", std::move(bodies));
  d.features.push_back(std::move(pattern));
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  const auto result = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(result.ok());
  REQUIRE_FALSE(result.features.back().errors.empty());
  CHECK(result.features.back().errors.front().pointer == "/features/34/inputs/bodies");
  CHECK(result.features.back().errors.front().message.find("64") != std::string::npos);
  CHECK(result.kernel_calls == 33);
  for (const auto& [id, body] : result.bodies) CHECK_FALSE(body.shape);
}

TEST_CASE("linear pattern admission accepts the full instance budget", "[linear-pattern]") {
  for (const auto count : {32.0, 64.0}) {
    auto f = feature("repeat", "pattern_linear", R"({"bodies":["plate"],"count":64,
      "spacing":15,"direction":[2,0,0]})");
    f.inputs = set(f.inputs, "count", count);
    if (count == 32) f.inputs = set(f.inputs, "bodies", Array{"plate", "other"});
    const auto inputs = ir::FeatureInputs::compile(f).value().evaluate().value();
    const auto admitted = prepare_linear_pattern(inputs);
    REQUIRE(admitted);
    CHECK(admitted->count == static_cast<std::size_t>(count));
    CHECK(admitted->step.translation == math::Vec3{15, 0, 0});
  }
}

TEST_CASE("linear patterns use current body state and later edits follow its hash",
          "[linear-pattern]") {
  auto d = patterned();
  d.features.insert(
      d.features.begin() + 2,
      feature("move_seed", "transform", R"({"bodies":["plate"],"translation":[7,0,0]})"));
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  const auto first = evaluator.evaluate(source(d)).value();
  solid(first, 18000, 6600, {30, 10, 15}, 3);
  d.features[2].inputs = ir::json::parse(R"({"bodies":["plate"],"translation":[10,0,0]})").value();
  const auto changed = evaluator.evaluate(source(d)).value();
  solid(changed, 18000, 6600, {33, 10, 15}, 3);
  CHECK(changed.kernel_calls == 5);
  CHECK(changed.cache_hits == 2);
  CHECK(changed.features[3].hash != first.features[3].hash);
}

TEST_CASE("linear pattern cache roles rebind IDs and reject stale snapshot history",
          "[linear-pattern]") {
  auto d = patterned();
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  const auto first = evaluator.evaluate(source(d)).value();
  solid(first, 18000, 6600, {23, 10, 15}, 3);
  d.features[1].id = "renamed_base";
  d.features[2].id = "renamed_repeat";
  const auto renamed = evaluator.evaluate(source(d)).value();
  solid(renamed, 18000, 6600, {23, 10, 15}, 3);
  names(renamed, "renamed_base", "renamed_repeat");
  CHECK(renamed.kernel_calls == 0);
  CHECK(renamed.cache_hits == 4);
  d.history_end = "outline";
  REQUIRE(evaluator.evaluate(source(d))->ok());
  evaluator.set_cache_limits({3, 64 * 1024 * 1024});
  evaluator.set_cache_limits({128, 64 * 1024 * 1024});
  d.history_end.reset();
  const auto rebuilt = evaluator.evaluate(source(d)).value();
  solid(rebuilt, 18000, 6600, {23, 10, 15}, 3);
  names(rebuilt, "renamed_base", "renamed_repeat");
  CHECK(rebuilt.kernel_calls == 5);
  CHECK(rebuilt.cache_hits == 1);
  CHECK(rebuilt.binding_misses == 2);
  solid(first, 18000, 6600, {23, 10, 15}, 3);
}

TEST_CASE("linear patterns respect suppression rollback and unavailable body diagnostics",
          "[linear-pattern]") {
  auto d = patterned();
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  d.history_end = "base";
  const auto rolled = evaluator.evaluate(source(d)).value();
  solid(rolled, 6000, 2200, {5, 10, 15}, 1);
  CHECK(rolled.features[2].status == FeatureStatus::kRolledBack);
  d.history_end = "repeat";
  const auto inclusive = evaluator.evaluate(source(d)).value();
  solid(inclusive, 18000, 6600, {20, 10, 15}, 3);
  d.history_end.reset();
  d.features[2].suppressed = true;
  const auto skipped = evaluator.evaluate(source(d)).value();
  solid(skipped, 6000, 2200, {8, 10, 15}, 1);
  CHECK(skipped.features[2].status == FeatureStatus::kSuppressed);
  d.features[2].suppressed = false;
  d.features[1].suppressed = true;
  const auto missing = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(missing.ok());
  REQUIRE_FALSE(missing.features[2].errors.empty());
  CHECK(missing.features[2].errors.front().code == "input_suppressed");
  CHECK(missing.features[2].errors.front().pointer == "/features/2/inputs/bodies/0");
  CHECK_FALSE(missing.bodies.at("plate").shape);
}

TEST_CASE("linear multi body pattern failures publish no partial arrays", "[linear-pattern]") {
  auto real = kernel::make_occt_kernel().value();
  for (const bool fail_copy : {false, true}) {
    FaultKernel kernel(*real);
    kernel.fail_transform = fail_copy ? 3 : 0;
    kernel.fail_union = fail_copy ? 0 : 2;
    Evaluator evaluator(kernel, {128, 64 * 1024 * 1024});
    auto d = part(2);
    d.features.push_back(feature("other_base", "extrude", R"({"profile":{"sketch":"outline"},
      "distance":20,"result":{"new":"other","body_type":"brep"}})"));
    d.features.push_back(feature("repeat", "pattern_linear", R"({"bodies":["plate","other"],
      "count":3,"spacing":15,"direction":"x"})"));
    d.features.push_back(
        feature("after", "transform", R"({"bodies":["plate"],"translation":[1,0,0]})"));
    d.features.push_back(feature("independent", "extrude", R"({"profile":{"sketch":"outline"},
      "distance":10,"result":{"new":"third","body_type":"brep"}})"));
    const auto failed = evaluator.evaluate(source(d)).value();
    REQUIRE_FALSE(failed.ok());
    REQUIRE_FALSE(failed.features[3].errors.empty());
    CHECK(failed.features[3].errors.front().message.find("injected") != std::string::npos);
    CHECK_FALSE(failed.bodies.at("plate").shape);
    CHECK_FALSE(failed.bodies.at("other").shape);
    solid(failed, 2000, 1000, {5, 10, 5}, 1, "third", false);
    CHECK(failed.kernel_calls == (fail_copy ? 7 : 9));
    kernel.fail_transform = kernel.fail_union = 0;
    kernel.transforms = kernel.unions = 0;
    const auto retried = evaluator.evaluate(source(d)).value();
    solid(retried, 18000, 6600, {21, 10, 15}, 3);
    solid(retried, 12000, 4800, {20, 10, 10}, 3, "other");
    names(retried);
    names(retried, "other_base", "repeat", "other");
    CHECK(retried.kernel_calls == 7);
    CHECK(retried.cache_hits == 4);
  }
}

TEST_CASE("linear patterns never cache incomplete copy or union naming history",
          "[linear-pattern]") {
  auto real = kernel::make_occt_kernel().value();
  for (const bool copy : {false, true}) {
    FaultKernel kernel(*real);
    kernel.lose_copy_history = copy;
    kernel.lose_union_history = !copy;
    Evaluator evaluator(kernel, {128, 64 * 1024 * 1024});
    const auto d = patterned();
    const auto failed = evaluator.evaluate(source(d)).value();
    REQUIRE_FALSE(failed.ok());
    REQUIRE_FALSE(failed.features[2].errors.empty());
    CHECK(failed.features[2].errors.front().message == "incomplete naming history");
    CHECK_FALSE(failed.bodies.at("plate").shape);
    CHECK(evaluator.cache_usage().entries == 2);
    kernel.lose_copy_history = kernel.lose_union_history = false;
    const auto retried = evaluator.evaluate(source(d)).value();
    solid(retried, 18000, 6600, {23, 10, 15}, 3);
    names(retried);
    CHECK(retried.kernel_calls == 4);
    CHECK(retried.cache_hits == 2);
  }
}

TEST_CASE("linear pattern snapshots survive cache eviction and kernel teardown",
          "[linear-pattern]") {
  Evaluation retained;
  {
    auto kernel = kernel::make_occt_kernel().value();
    Evaluator evaluator(*kernel, {1, 64 * 1024 * 1024});
    const auto d = patterned();
    retained = evaluator.evaluate(source(d)).value();
    solid(retained, 18000, 6600, {23, 10, 15}, 3);
    const auto rebuilt = evaluator.evaluate(source(d)).value();
    solid(rebuilt, 18000, 6600, {23, 10, 15}, 3);
    names(rebuilt);
    CHECK(rebuilt.kernel_calls == 5);
    CHECK(rebuilt.cache_hits == 0);
    evaluator.clear_cache();
    CHECK(evaluator.cache_usage().estimated_bytes == 0);
  }
  solid(retained, 18000, 6600, {23, 10, 15}, 3);
  names(retained);
}
