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
constexpr double kPi = std::numbers::pi;
using ir::json::Value;
Value set(const Value& object, std::string_view key, Value value) {
  auto members = *object.if_object();
  for (auto& member : members)
    if (member.key == key) member.value = std::move(value);
  return members;
}
ir::Document attached() {
  auto d = part(2);
  d.params.push_back({"height", "2 mm", {}, {}, {}, {}});
  d.features.push_back(feature("face_sketch", "sketch", R"({"plane":{"created_by":"base",
    "role":"cap_end","expect":"one"},"entities":[{"id":"r","type":"circle",
    "center":[5,10],"radius":2}],"constraints":[]})"));
  d.features.push_back(feature("boss", "extrude", R"({"profile":{"sketch":"face_sketch"},
    "distance":"height","result":{"new":"boss_body","body_type":"brep"}})"));
  return d;
}
void selector(ir::Document& d, const char* text, std::size_t index = 2) {
  d.features[index].inputs = set(d.features[index].inputs, "plane", ir::json::parse(text).value());
}
void solid(const Evaluation& result, double volume, double area, math::Vec3 center,
           const std::string& id = "boss_body", bool complete = true) {
  for (const auto& f : result.features)
    for (const auto& e : f.errors) UNSCOPED_INFO(e.pointer << ": " << e.message);
  if (complete) REQUIRE(result.ok());
  const auto& body = result.bodies.at(id);
  REQUIRE(body.shape);
  const auto& m = body.shape->measures();
  CHECK_THAT(m.volume, WithinAbs(volume, 1e-7));
  CHECK_THAT(m.area, WithinAbs(area, 1e-7));
  CHECK_THAT(m.centroid.x, WithinAbs(center.x, 1e-8));
  CHECK_THAT(m.centroid.y, WithinAbs(center.y, 1e-8));
  CHECK_THAT(m.centroid.z, WithinAbs(center.z, 1e-8));
  CHECK(m.solids == 1);
  REQUIRE_FALSE(body.names.empty());
  for (const auto& n : body.names) CHECK(n.entity->owner() == body.shape);
}
void boss(const Evaluation& result, math::Vec3 center = {5, 10, 31}) {
  solid(result, 8 * kPi, 16 * kPi, center);
}
ir::Document transferred(bool keep = false, bool intersect = false) {
  auto d = attached();
  d.features.insert(d.features.begin() + 2,
                    feature("tool", "extrude", R"({"profile":{"sketch":"outline"},
    "distance":40,"result":{"new":"other","body_type":"brep"}})"));
  auto merge = feature("merge", "boolean", R"({"target":"plate","tools":["other"],
    "op":"union","keep_tools":false})");
  merge.inputs = set(merge.inputs, "keep_tools", keep);
  if (intersect) merge.inputs = set(merge.inputs, "op", "intersect");
  d.features.insert(d.features.begin() + 3, std::move(merge));
  selector(d, R"({"created_by":"tool","role":"cap_end","expect":"one"})", 4);
  return d;
}
class QueryKernel final : public kernel::IKernel {
 public:
  kernel::IKernel& backend;
  bool fail_query = false;
  bool null_face = false, omit_faces = false;
  kernel::EntityPtr wrong_owner;
  std::size_t queries = 0;
  explicit QueryKernel(kernel::IKernel& real) : backend(real) {}
  std::string_view identity() const noexcept override { return backend.identity(); }
  Capabilities capabilities() const noexcept override { return backend.capabilities(); }
  std::expected<std::vector<kernel::FaceInfo>, kernel::KernelError> faces(
      const kernel::ShapePtr& s) override {
    ++queries;
    if (fail_query)
      return std::unexpected(kernel::KernelError{"kernel", "injected face enumeration failure"});
    auto result = backend.faces(s);
    if (result && null_face) result->front().entity.reset();
    if (result && wrong_owner) result->front().entity = wrong_owner;
    if (result && omit_faces) result->clear();
    return result;
  }
  std::expected<kernel::Plane, kernel::KernelError> face_plane(
      const kernel::EntityPtr& e) override {
    ++queries;
    if (fail_query)
      return std::unexpected(kernel::KernelError{"kernel", "injected plane query failure"});
    return backend.face_plane(e);
  }
  std::expected<kernel::OperationResult, kernel::KernelError> extrude(
      const kernel::Extrusion& e) override {
    return backend.extrude(e);
  }
  std::expected<kernel::OperationResult, kernel::KernelError> revolve(
      const kernel::Revolution& r) override {
    return backend.revolve(r);
  }
  std::expected<kernel::OperationResult, kernel::KernelError> transform(
      const kernel::ShapePtr& s, const kernel::RigidTransform& t) override {
    return backend.transform(s, t);
  }
  std::expected<kernel::OperationResult, kernel::KernelError> mirror(
      const kernel::ShapePtr& s, const kernel::Plane& p) override {
    return backend.mirror(s, p);
  }
  std::expected<kernel::OperationResult, kernel::KernelError> boolean_operation(
      const kernel::ShapePtr& s, const std::vector<kernel::ShapePtr>& tools,
      kernel::BooleanOp op) override {
    return backend.boolean_operation(s, tools, op);
  }
};
}  // namespace

TEST_CASE("generative face selectors attach sketches to current caps and query on cache hits",
          "[face-attached]") {
  auto real = kernel::make_occt_kernel().value();
  QueryKernel kernel(*real);
  Evaluator evaluator(kernel, {128, 64 * 1024 * 1024});
  auto d = attached();
  const auto first = evaluator.evaluate(source(d)).value();
  boss(first);
  solid(first, 6000, 2200, {5, 10, 15}, "plate");
  CHECK(first.kernel_calls == 3);
  CHECK(kernel.queries == 1);
  const auto cached = evaluator.evaluate(source(d)).value();
  boss(cached);
  CHECK(cached.kernel_calls == 1);
  CHECK(cached.cache_hits == 4);
  CHECK(kernel.queries == 2);
  d.params[2].value = 2.0;
  CHECK(evaluator.evaluate(source(d))->kernel_calls == 1);
  for (const auto& n : cached.bodies.at("boss_body").names) CHECK(n.feature == "boss");
}

TEST_CASE("face attachments track depth height units and independent branch edits",
          "[face-attached]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto d = attached();
  const auto first = evaluator.evaluate(source(d)).value();
  boss(first);
  d.params[0].value = "40 mm";
  const auto deeper = evaluator.evaluate(source(d)).value();
  boss(deeper, {5, 10, 41});
  CHECK(deeper.kernel_calls == 3);
  CHECK(deeper.cache_hits == 1);
  CHECK(deeper.features[2].hash != first.features[2].hash);
  d.params[0].value = "4 cm";
  CHECK(evaluator.evaluate(source(d))->cache_hits == 4);
  d.params[3].value = "4 mm";
  const auto taller = evaluator.evaluate(source(d)).value();
  solid(taller, 16 * kPi, 24 * kPi, {5, 10, 42});
  CHECK(taller.kernel_calls == 2);
  CHECK(taller.cache_hits == 3);
  Evaluator cold(*kernel, {128, 64 * 1024 * 1024});
  solid(cold.evaluate(source(d)).value(), 16 * kPi, 24 * kPi, {5, 10, 42});
  boss(first);
}

TEST_CASE("face attachments follow current rigid body placement and outward cap orientation",
          "[face-attached]") {
  auto kernel = kernel::make_occt_kernel().value();
  auto d = attached();
  d.features.insert(
      d.features.begin() + 2,
      feature("move_host", "transform", R"({"bodies":["plate"],"translation":[7,-2,5]})"));
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  boss(evaluator.evaluate(source(d)).value(), {12, 8, 36});
  d.features[2].inputs = ir::json::parse(R"({"bodies":["plate"],"rotation":{
    "axis":{"origin":[0,0,0],"direction":"y"},"angle":"90 deg"}})")
                             .value();
  const auto rotated = evaluator.evaluate(source(d)).value();
  boss(rotated, {31, 10, -5});
  CHECK(rotated.kernel_calls == 3);
  CHECK(rotated.cache_hits == 2);
  auto bottom = attached();
  selector(bottom, R"({"created_by":"base","role":"cap_start"})");
  bottom.features[2].inputs =
      set(bottom.features[2].inputs, "entities",
          ir::json::parse(R"([{"id":"r","type":"circle","center":[5,-10],"radius":2}])").value());
  boss(evaluator.evaluate(source(bottom)).value(), {5, 10, -1});
}

TEST_CASE("face selectors reject missing and ambiguous names regardless of plane expectation",
          "[face-attached]") {
  auto kernel = kernel::make_occt_kernel().value();
  for (const auto* text : {R"({"created_by":"base","role":"missing","expect":"any"})",
                           R"({"created_by":"base","expect":"some"})",
                           R"({"created_by":"base","role":"side","expect":"one"})"}) {
    auto d = attached();
    selector(d, text);
    Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
    const auto failed = evaluator.evaluate(source(d)).value();
    REQUIRE_FALSE(failed.ok());
    CHECK(failed.features[2].errors.front().code == "selector_count");
    CHECK(failed.features[2].errors.front().pointer == "/features/2/inputs/plane");
    CHECK_FALSE(failed.bodies.at("boss_body").shape);
    solid(failed, 6000, 2200, {5, 10, 15}, "plate", false);
    CHECK(failed.kernel_calls == 1);
  }
}

TEST_CASE("face selectors deduplicate merged names and reject separated pattern caps",
          "[face-attached]") {
  auto d = attached();
  d.features.insert(d.features.begin() + 2,
                    feature("repeat", "pattern_linear", R"({"bodies":["plate"],"count":2,
    "spacing":10,"direction":"x"})"));
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  boss(evaluator.evaluate(source(d)).value());
  d.features[2].inputs = set(d.features[2].inputs, "spacing", 15.0);
  const auto separated = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(separated.ok());
  CHECK(separated.features[3].errors.front().code == "selector_count");
  CHECK(separated.features[3].errors.front().message.find("2") != std::string::npos);
  CHECK_FALSE(separated.bodies.at("boss_body").shape);
  CHECK(separated.bodies.at("plate").shape->measures().solids == 2);
}

TEST_CASE("face provenance follows consumed boolean tools into the surviving current body",
          "[face-attached]") {
  auto d = transferred();
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  const auto first = evaluator.evaluate(source(d)).value();
  boss(first, {5, 10, 41});
  CHECK_FALSE(first.bodies.at("other").shape);
  solid(first, 8000, 2800, {5, 10, 20}, "plate");
  CHECK(first.kernel_calls == 5);
  d.features.insert(
      d.features.begin() + 4,
      feature("move_result", "transform", R"({"bodies":["plate"],"translation":[3,4,5]})"));
  const auto moved = evaluator.evaluate(source(d)).value();
  boss(moved, {8, 14, 46});
  CHECK(moved.kernel_calls == 3);
  CHECK(moved.cache_hits == 4);
  auto deleted = transferred();
  selector(deleted, R"({"created_by":"base","role":"cap_end"})", 4);
  const auto gone = evaluator.evaluate(source(deleted)).value();
  REQUIRE_FALSE(gone.ok());
  CHECK(gone.features[4].errors.front().code == "selector_count");
}

TEST_CASE("retained boolean tools contribute both candidate bodies and ambiguity",
          "[face-attached]") {
  auto d = transferred(true);
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  const auto ambiguous = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(ambiguous.ok());
  CHECK(ambiguous.features[4].errors.front().code == "selector_count");
  CHECK(ambiguous.features[4].errors.front().message.find("2") != std::string::npos);
  CHECK(ambiguous.bodies.at("other").shape);
  d = transferred(true, true);
  const auto first = evaluator.evaluate(source(d)).value();
  boss(first, {5, 10, 41});
  d.params[0].value = "20 mm";
  const auto changed = evaluator.evaluate(source(d)).value();
  boss(changed, {5, 10, 41});
  CHECK(changed.features[4].hash != first.features[4].hash);
  CHECK(changed.kernel_calls == 4);
  CHECK(changed.cache_hits == 2);
}

TEST_CASE("generative face provenance follows successive boolean retirements", "[face-attached]") {
  auto d = transferred();
  d.features.insert(
      d.features.begin() + 4,
      {feature("taller", "extrude", R"({"profile":{"sketch":"outline"},"distance":50,
      "result":{"new":"third","body_type":"brep"}})"),
       feature("again", "boolean", R"({"target":"third","tools":["plate"],"op":"union"})")});
  selector(d, R"({"created_by":"tool","role":"cap_start","expect":"any"})", 6);
  d.features[6].inputs =
      set(d.features[6].inputs, "entities",
          ir::json::parse(R"([{"id":"r","type":"circle","center":[5,-10],"radius":2}])").value());
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  const auto first = evaluator.evaluate(source(d)).value();
  boss(first, {5, 10, -1});
  solid(first, 10000, 3400, {5, 10, 25}, "third");
  CHECK_FALSE(first.bodies.at("plate").shape);
  CHECK_FALSE(first.bodies.at("other").shape);
  CHECK(first.kernel_calls == 7);
  const auto cached = evaluator.evaluate(source(d)).value();
  boss(cached, {5, 10, -1});
  CHECK(cached.kernel_calls == 1);
  CHECK(cached.cache_hits == 8);
}

TEST_CASE("consumed cutting tool cap planes keep current reversed face orientation",
          "[face-attached][face-filters]") {
  auto d = attached();
  d.features.insert(
      d.features.begin() + 2,
      {feature("cut_profile", "sketch",
               R"({"plane":{"origin":[0,0,2],"normal":"z","x_direction":"x"},
      "entities":[{"id":"r","type":"rect","corner":[2,2],"size":[4,6]}],"constraints":[]})"),
       feature("drill", "extrude", R"({"profile":{"sketch":"cut_profile"},"distance":3,
      "result":{"new":"tool_body","body_type":"brep"}})"),
       feature("cut", "boolean", R"({"target":"plate","tools":["tool_body"],"op":"subtract"})")});
  selector(d, R"({"created_by":"drill","role":"cap_end"})", 5);
  SECTION("generated cap") {}
  SECTION("normal-filtered cap") {
    selector(d, R"({"created_by":"drill","role":"cap_end","where":{"normal":"-z"}})", 5);
  }
  d.features[5].inputs =
      set(d.features[5].inputs, "entities",
          ir::json::parse(R"([{"id":"r","type":"circle","center":[5,-5],"radius":2}])").value());
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  const auto result = evaluator.evaluate(source(d)).value();
  boss(result, {5, 5, 4});
  solid(result, 5928, 2308,
        {(6000 * 5.0 - 72 * 4) / 5928, (6000 * 10.0 - 72 * 5) / 5928,
         (6000 * 15.0 - 72 * 3.5) / 5928},
        "plate");
}

TEST_CASE("unsupported face selector sets and qualifiers fail at their fields", "[face-attached]") {
  struct Invalid {
    const char* selector;
    const char* field;
  };
  auto kernel = kernel::make_occt_kernel().value();
  for (const auto& [text, field] :
       {Invalid{R"({"created_by":"base","from":"outline/r"})", "from"},
        Invalid{R"({"created_by":"base","except":{"created_by":"base","role":"side"}})", "except"},
        Invalid{R"({"any":[{"created_by":"base"}]})", "any"},
        Invalid{R"({"edges_of":{"created_by":"base"}})", "edges_of"}}) {
    auto d = attached();
    selector(d, text);
    Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
    const auto result = evaluator.evaluate(source(d)).value();
    REQUIRE_FALSE(result.ok());
    CHECK(result.features[2].errors.front().pointer ==
          "/features/2/inputs/plane/" + std::string(field));
    CHECK(result.features[2].errors.front().code == "kernel");
    CHECK(result.kernel_calls == 1);
  }
}

TEST_CASE("face attachments respect suppression rollback and failed current body bindings",
          "[face-attached]") {
  auto d = attached();
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  d.history_end = "base";
  const auto rolled = evaluator.evaluate(source(d)).value();
  REQUIRE(rolled.ok());
  CHECK(rolled.kernel_calls == 1);
  CHECK(rolled.features[2].status == FeatureStatus::kRolledBack);
  d.history_end.reset();
  d.features[1].suppressed = true;
  const auto suppressed = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(suppressed.ok());
  CHECK(suppressed.features[2].errors.front().code == "input_suppressed");
  CHECK(suppressed.features[2].errors.front().pointer == "/features/2/inputs/plane/created_by");
  d.features[1].suppressed = false;
  d.params[0].value = "0 mm";
  const auto missing = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(missing.ok());
  CHECK(missing.features[2].errors.front().code == "input_failed");
  CHECK(missing.features[2].errors.front().pointer == "/features/2/inputs/plane/created_by");
  d.params[0].value = "30 mm";
  d.features[2].suppressed = true;
  const auto skipped = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(skipped.ok());
  CHECK(skipped.kernel_calls == 0);
  CHECK(skipped.features[3].errors.front().code == "input_suppressed");
  d = transferred(true, true);
  d.features.insert(d.features.begin() + 4,
                    feature("bad_move", "transform", R"({"bodies":["plate"],"rotation":{
    "axis":{"origin":[0,0,0],"direction":[0,0,0]},"angle":20}})"));
  const auto failed = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(failed.ok());
  CHECK(failed.features[5].errors.front().code == "input_failed");
  CHECK(failed.features[5].errors.front().pointer == "/features/5/inputs/plane/created_by");
  CHECK_FALSE(failed.bodies.at("boss_body").shape);
  CHECK(failed.bodies.at("other").shape);
}

TEST_CASE("face query failures leave hosts available and invalidate downstream cache reuse",
          "[face-attached]") {
  auto real = kernel::make_occt_kernel().value();
  QueryKernel kernel(*real);
  Evaluator evaluator(kernel, {128, 64 * 1024 * 1024});
  const auto d = attached();
  boss(evaluator.evaluate(source(d)).value());
  kernel.fail_query = true;
  const auto failed = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(failed.ok());
  CHECK(failed.features[2].errors.front().message.find("injected") != std::string::npos);
  CHECK(failed.features[2].errors.front().pointer == "/features/2/inputs/plane");
  CHECK_FALSE(failed.bodies.at("boss_body").shape);
  solid(failed, 6000, 2200, {5, 10, 15}, "plate", false);
  kernel.fail_query = false;
  const auto retry = evaluator.evaluate(source(d)).value();
  boss(retry);
  CHECK(retry.kernel_calls == 1);
  CHECK(retry.cache_hits == 4);
}

TEST_CASE("face sketch cache bindings reject recreated snapshots and rebind producer IDs",
          "[face-attached]") {
  auto d = attached();
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  const auto first = evaluator.evaluate(source(d)).value();
  boss(first);
  d.history_end = "outline";
  REQUIRE(evaluator.evaluate(source(d))->ok());
  evaluator.set_cache_limits({3, 64 * 1024 * 1024});
  evaluator.set_cache_limits({128, 64 * 1024 * 1024});
  d.history_end.reset();
  const auto rebuilt = evaluator.evaluate(source(d)).value();
  boss(rebuilt);
  CHECK(rebuilt.binding_misses == 1);
  CHECK(rebuilt.cache_hits == 2);
  CHECK(rebuilt.kernel_calls == 2);
  d.features[1].id = "renamed_base";
  selector(d, R"({"created_by":"renamed_base","role":"cap_end","expect":"one"})");
  const auto renamed = evaluator.evaluate(source(d)).value();
  boss(renamed);
  CHECK(renamed.kernel_calls == 2);
  CHECK(renamed.cache_hits == 2);
  CHECK(std::ranges::all_of(renamed.bodies.at("plate").names,
                            [](const auto& n) { return n.feature == "renamed_base"; }));
  boss(first);
}

TEST_CASE("face selectors invalidate cached planes when producer IDs swap on identical snapshots",
          "[face-attached][face-filters]") {
  auto d = attached();
  d.features.insert(
      d.features.begin() + 2,
      {feature("second_profile", "sketch", R"({"plane":{"origin":[20,0,0],"normal":"z",
      "x_direction":"x"},"entities":[{"id":"r","type":"rect","corner":[0,0],"size":[10,20]}],
      "constraints":[]})"),
       feature("extension", "extrude", R"({"profile":{"sketch":"second_profile"},"distance":40,
      "result":{"join":"plate"}})")});
  SECTION("generated cap") {}
  SECTION("normal-filtered cap") {
    selector(d, R"({"created_by":"base","role":"cap_end","where":{"normal":"z"}})", 4);
  }
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  const auto first = evaluator.evaluate(source(d)).value();
  boss(first);
  d.features[1].id = "extension";
  d.features[3].id = "base";
  const auto swapped = evaluator.evaluate(source(d)).value();
  boss(swapped, {25, 10, 41});
  CHECK(swapped.bodies.at("plate").shape == first.bodies.at("plate").shape);
  CHECK(swapped.features[4].hash != first.features[4].hash);
  CHECK(swapped.kernel_calls == 2);
  CHECK(swapped.cache_hits == 4);
  boss(first);
}

TEST_CASE("face sketch admission charges retained source geometry to its byte budget",
          "[face-attached]") {
  auto d = attached();
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  const auto first = evaluator.evaluate(source(d)).value();
  boss(first);
  const auto bytes = first.bodies.at("plate").shape->estimated_bytes();
  Evaluator limited(*kernel, {128, bytes});
  d.history_end = "face_sketch";
  const auto result = limited.evaluate(source(d)).value();
  REQUIRE(result.ok());
  CHECK(limited.cache_usage().entries == 1);  // outline only; bound sketch exceeds bytes
  CHECK(limited.cache_usage().estimated_bytes < bytes);
  const auto rebuilt = limited.evaluate(source(d)).value();
  REQUIRE(rebuilt.ok());
  CHECK(rebuilt.kernel_calls == 2);
  CHECK(rebuilt.cache_hits == 1);
}

TEST_CASE("face attached snapshots survive cache eviction and kernel facade teardown",
          "[face-attached]") {
  Evaluation retained;
  {
    auto kernel = kernel::make_occt_kernel().value();
    Evaluator evaluator(*kernel, {1, 64 * 1024 * 1024});
    retained = evaluator.evaluate(source(attached())).value();
    const auto rebuilt = evaluator.evaluate(source(attached())).value();
    boss(rebuilt);
    CHECK(rebuilt.kernel_calls == 3);
    CHECK(rebuilt.cache_hits == 0);
    evaluator.clear_cache();
    CHECK(evaluator.cache_usage().estimated_bytes == 0);
  }
  boss(retained);
  solid(retained, 6000, 2200, {5, 10, 15}, "plate");
}

TEST_CASE("body face selectors follow depth edits and query through complete cache hits",
          "[face-filters]") {
  auto d = attached();
  selector(d, R"({"faces_of":"plate","where":{"type":"plane","normal":"z"},"expect":"one"})");
  auto real = kernel::make_occt_kernel().value();
  QueryKernel kernel(*real);
  Evaluator evaluator(kernel, {128, 64 * 1024 * 1024});
  const auto first = evaluator.evaluate(source(d)).value();
  boss(first);
  CHECK(first.kernel_calls == 3);
  CHECK(kernel.queries == 1);
  const auto cached = evaluator.evaluate(source(d)).value();
  boss(cached);
  CHECK(cached.kernel_calls == 1);
  CHECK(cached.cache_hits == 4);
  CHECK(kernel.queries == 2);
  d.params[0].value = "40 mm";
  const auto edited = evaluator.evaluate(source(d)).value();
  boss(edited, {5, 10, 41});
  CHECK(edited.features[2].hash != first.features[2].hash);
  CHECK(edited.kernel_calls == 3);
  CHECK(edited.cache_hits == 1);
  d.params[0].value = "4 cm";
  const auto equivalent = evaluator.evaluate(source(d)).value();
  boss(equivalent, {5, 10, 41});
  CHECK(equivalent.kernel_calls == 1);
  CHECK(equivalent.cache_hits == 4);
  Evaluator cold(kernel, {128, 64 * 1024 * 1024});
  boss(cold.evaluate(source(d)).value(), {5, 10, 41});
  boss(first);
}

TEST_CASE("generated face filters use signed normals and preserve side parameter frames",
          "[face-filters]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto d = attached();
  selector(d, R"({"created_by":"base","where":{"type":"plane","normal":[0,0,12]}})");
  boss(evaluator.evaluate(source(d)).value());
  selector(d, R"({"created_by":"base","where":{"normal":"-z"}})");
  boss(evaluator.evaluate(source(d)).value(), {5, -10, -1});
  selector(d, R"({"created_by":"base","role":"side","where":{"normal":"x"}})");
  boss(evaluator.evaluate(source(d)).value(), {11, 5, 10});
  selector(d, R"({"created_by":"base","role":"cap_end","where":{}})");
  boss(evaluator.evaluate(source(d)).value());
}

TEST_CASE("planar filters exclude curved faces without excluding their planar caps",
          "[face-filters]") {
  auto d = attached();
  d.features[0].inputs =
      set(d.features[0].inputs, "entities",
          ir::json::parse(R"([{"id":"r","type":"circle","center":[0,0],"radius":4}])").value());
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  selector(d, R"({"faces_of":"plate","where":{"type":"plane","normal":"z"}})");
  const auto top = evaluator.evaluate(source(d)).value();
  boss(top);
  solid(top, 480 * kPi, 272 * kPi, {0, 0, 15}, "plate");
  selector(d, R"({"faces_of":"plate","where":{"type":"plane"}})");
  const auto caps = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(caps.ok());
  CHECK(caps.features[2].errors.front().code == "selector_count");
  CHECK(caps.features[2].errors.front().message.find("2") != std::string::npos);
  selector(d, R"({"created_by":"base","role":"side","where":{"normal":"z"}})");
  const auto sides = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(sides.ok());
  CHECK(sides.features[2].errors.front().code == "selector_count");
  CHECK(sides.features[2].errors.front().message.find("0") != std::string::npos);
}

TEST_CASE("parallel and perpendicular filters describe the plane relative to a direction",
          "[face-filters]") {
  auto d = attached();
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  selector(d, R"({"faces_of":"plate","where":{"normal":"z","parallel_to":"x",
    "perpendicular_to":"-z"}})");
  boss(evaluator.evaluate(source(d)).value());
  selector(d, R"({"created_by":"base","role":"side","where":{"normal":"x",
    "parallel_to":"z","perpendicular_to":"x"}})");
  boss(evaluator.evaluate(source(d)).value(), {11, 5, 10});
  selector(d, R"({"faces_of":"plate","where":{"normal":"z","parallel_to":"z"}})");
  const auto none = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(none.ok());
  CHECK(none.features[2].errors.front().code == "selector_count");
  CHECK(none.features[2].errors.front().message.find("0") != std::string::npos);
  selector(d, R"({"faces_of":"plate","where":{"perpendicular_to":"z"}})");
  const auto two = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(two.ok());
  CHECK(two.features[2].errors.front().code == "selector_count");
  CHECK(two.features[2].errors.front().message.find("2") != std::string::npos);
}

TEST_CASE("planar direction filters honor explicit angular tolerance", "[face-filters]") {
  struct Case {
    const char* where;
    bool matches;
  };
  auto kernel = kernel::make_occt_kernel().value();
  for (const auto& [where, matches] : {Case{R"({"normal":[0.0000000005,0,1]})", true},
                                       Case{R"({"normal":[0.000000002,0,1]})", false},
                                       Case{R"({"parallel_to":[1,0,0.0000000005]})", true},
                                       Case{R"({"parallel_to":[1,0,0.000000002]})", false},
                                       Case{R"({"perpendicular_to":[0.0000000005,0,-1]})", true},
                                       Case{R"({"perpendicular_to":[0.000000002,0,-1]})", false}}) {
    auto d = attached();
    selector(d, R"({"created_by":"base","role":"cap_end","where":{}})");
    const auto plane =
        set(*d.features[2].inputs.find("plane"), "where", ir::json::parse(where).value());
    d.features[2].inputs = set(d.features[2].inputs, "plane", plane);
    Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
    const auto result = evaluator.evaluate(source(d)).value();
    INFO(where);
    CHECK(result.ok() == matches);
    if (matches)
      boss(result);
    else {
      CHECK(result.features[2].errors.front().code == "selector_count");
      CHECK_FALSE(result.bodies.at("boss_body").shape);
    }
  }
}

TEST_CASE("direction filters evaluate scalar expressions before selecting current frames",
          "[face-filters]") {
  auto d = attached();
  d.params.push_back({"sign", 2.0, {}, {}, {}, {}});
  selector(d, R"({"faces_of":"plate","where":{"normal":[0,0,"sign"]}})");
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  boss(evaluator.evaluate(source(d)).value());
  d.params.back().value = -3.0;
  const auto bottom = evaluator.evaluate(source(d)).value();
  boss(bottom, {5, -10, -1});
  CHECK(bottom.kernel_calls == 2);
  CHECK(bottom.cache_hits == 2);
  selector(d, R"({"faces_of":"plate","where":{"normal":[0,0,1e308]}})");
  boss(evaluator.evaluate(source(d)).value());
  selector(d, R"({"faces_of":"plate","where":{"normal":[1e308,1e308,1e308]}})");
  const auto diagonal = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(diagonal.ok());
  CHECK(diagonal.features[2].errors.front().code == "selector_count");
}

TEST_CASE("filtered attachments follow tilted and mirrored world normals", "[face-filters]") {
  auto d = attached();
  d.features.insert(d.features.begin() + 2,
                    feature("tilt", "transform", R"({"bodies":["plate"],"rotation":{
        "axis":{"origin":[0,0,0],"direction":"y"},"angle":"45 deg"}})"));
  selector(d, R"({"faces_of":"plate","where":{"normal":[1,0,1]}})", 3);
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  boss(evaluator.evaluate(source(d)).value(), {36 / std::sqrt(2.0), 10, 26 / std::sqrt(2.0)});
  d = attached();
  d.features.insert(d.features.begin() + 2,
                    feature("reflect", "mirror", R"({"bodies":["plate"],"plane":{"datum":"xy"}})"));
  selector(d, R"({"created_by":"base","role":"cap_end","where":{"normal":"-z"}})", 3);
  const auto reflected = evaluator.evaluate(source(d)).value();
  boss(reflected, {5, -10, -31});
  CHECK(reflected.bodies.at("plate").shape->measures().solids == 1);
}

TEST_CASE("filtered body faces use current boolean geometry and reject retired body IDs",
          "[face-filters]") {
  auto d = transferred();
  selector(d, R"({"faces_of":"plate","where":{"normal":"z"}})", 4);
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  boss(evaluator.evaluate(source(d)).value(), {5, 10, 41});
  selector(d, R"({"faces_of":"other","where":{"normal":"z"}})", 4);
  const auto retired = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(retired.ok());
  CHECK(retired.features[4].errors.front().code == "input_failed");
  CHECK(retired.features[4].errors.front().pointer == "/features/4/inputs/plane/faces_of");
  CHECK_FALSE(retired.bodies.at("boss_body").shape);
  solid(retired, 8000, 2800, {5, 10, 20}, "plate", false);
}

TEST_CASE("filtered provenance binds candidates even when no named face survives on one body",
          "[face-filters]") {
  auto d = transferred(true, true);
  selector(d, R"({"created_by":"tool","role":"cap_end","where":{"normal":"z"}})", 4);
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  const auto first = evaluator.evaluate(source(d)).value();
  boss(first, {5, 10, 41});
  d.params[0].value = "20 mm";
  const auto edit = evaluator.evaluate(source(d)).value();
  boss(edit, {5, 10, 41});
  CHECK(edit.features[4].hash != first.features[4].hash);
  CHECK(edit.features[5].hash != first.features[5].hash);
  CHECK(edit.bodies.at("other").shape == first.bodies.at("other").shape);
}

TEST_CASE("body face selectors require exactly one face with all expectation modes",
          "[face-filters]") {
  auto kernel = kernel::make_occt_kernel().value();
  for (const auto* text : {R"({"faces_of":"plate","expect":"any"})",
                           R"({"faces_of":"plate","where":{},"expect":"some"})",
                           R"({"faces_of":"plate","where":{"normal":[1,1,1]},"expect":"any"})",
                           R"({"created_by":"base","role":"side","where":{"type":"plane"}})"}) {
    auto d = attached();
    selector(d, text);
    Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
    const auto result = evaluator.evaluate(source(d)).value();
    REQUIRE_FALSE(result.ok());
    CHECK(result.features[2].errors.front().code == "selector_count");
    CHECK(result.features[2].errors.front().pointer == "/features/2/inputs/plane");
    CHECK(result.kernel_calls == 2);
    CHECK_FALSE(result.bodies.at("boss_body").shape);
  }
}

TEST_CASE("unsupported face filters and invalid directions identify their exact fields",
          "[face-filters]") {
  struct Invalid {
    const char* where;
    const char* field;
  };
  auto kernel = kernel::make_occt_kernel().value();
  for (const auto& [where, field] :
       {Invalid{R"({"type":"cylinder"})", "type"}, Invalid{R"({"near":[0,0,0]})", "near"},
        Invalid{R"({"radius":2})", "radius"}, Invalid{R"({"convex":true})", "convex"},
        Invalid{R"({"concave":false})", "concave"}, Invalid{R"({"normal":[0,0,0]})", "normal"},
        Invalid{R"({"parallel_to":[0,0,0]})", "parallel_to"},
        Invalid{R"({"perpendicular_to":[0,0,0]})", "perpendicular_to"}}) {
    auto d = attached();
    selector(d, R"({"faces_of":"plate","where":{}})");
    d.features[2].inputs =
        set(d.features[2].inputs, "plane",
            set(*d.features[2].inputs.find("plane"), "where", ir::json::parse(where).value()));
    Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
    const auto result = evaluator.evaluate(source(d)).value();
    REQUIRE_FALSE(result.ok());
    CHECK(result.features[2].errors.front().code == "kernel");
    CHECK(result.features[2].errors.front().pointer ==
          "/features/2/inputs/plane/where/" + std::string(field));
    CHECK(result.kernel_calls == 1);
  }
}

TEST_CASE("body face queries respect suppression rollback and failed current modifiers",
          "[face-filters]") {
  auto d = attached();
  selector(d, R"({"faces_of":"plate","where":{"normal":"z"}})");
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  d.features[1].suppressed = true;
  const auto suppressed = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(suppressed.ok());
  CHECK(suppressed.features[2].errors.front().code == "input_suppressed");
  CHECK(suppressed.features[2].errors.front().pointer == "/features/2/inputs/plane/faces_of");
  d.features[1].suppressed = false;
  d.history_end = "base";
  const auto rolled = evaluator.evaluate(source(d)).value();
  REQUIRE(rolled.ok());
  CHECK(rolled.kernel_calls == 1);
  CHECK(rolled.features[2].status == FeatureStatus::kRolledBack);
  d.history_end.reset();
  d.features.insert(d.features.begin() + 2,
                    feature("bad", "transform", R"({"bodies":["plate"],"rotation":{
    "axis":{"origin":[0,0,0],"direction":[0,0,0]},"angle":20}})"));
  const auto failed = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(failed.ok());
  CHECK(failed.features[3].errors.front().code == "input_failed");
  CHECK(failed.features[3].errors.front().pointer == "/features/3/inputs/plane/faces_of");
  CHECK_FALSE(failed.bodies.at("boss_body").shape);
}

TEST_CASE("face enumeration failures prevent stale downstream reuse and preserve the host",
          "[face-filters]") {
  auto real = kernel::make_occt_kernel().value();
  QueryKernel kernel(*real);
  Evaluator evaluator(kernel, {128, 64 * 1024 * 1024});
  auto d = attached();
  selector(d, R"({"faces_of":"plate","where":{"normal":"z"}})");
  boss(evaluator.evaluate(source(d)).value());
  kernel.fail_query = true;
  const auto failed = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(failed.ok());
  CHECK(failed.features[2].errors.front().pointer == "/features/2/inputs/plane");
  CHECK(failed.features[2].errors.front().message.find("injected") != std::string::npos);
  CHECK_FALSE(failed.bodies.at("boss_body").shape);
  solid(failed, 6000, 2200, {5, 10, 15}, "plate", false);
  kernel.fail_query = false;
  const auto retry = evaluator.evaluate(source(d)).value();
  boss(retry);
  CHECK(retry.kernel_calls == 1);
  CHECK(retry.cache_hits == 4);
}

TEST_CASE("filtered selectors reject malformed handles and missing provenance in face queries",
          "[face-filters]") {
  auto real = kernel::make_occt_kernel().value();
  QueryKernel kernel(*real);
  Evaluator evaluator(kernel, {128, 64 * 1024 * 1024});
  auto d = attached();
  selector(d, R"({"created_by":"base","role":"cap_end","where":{"normal":"z"}})");
  const auto first = evaluator.evaluate(source(d)).value();
  boss(first);
  SECTION("null handle") { kernel.null_face = true; }
  SECTION("different snapshot") {
    kernel.wrong_owner = first.bodies.at("boss_body").names.front().entity;
  }
  SECTION("missing named face") { kernel.omit_faces = true; }
  const auto failed = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(failed.ok());
  CHECK(failed.features[2].errors.front().code == "kernel");
  CHECK(failed.features[2].errors.front().pointer == "/features/2/inputs/plane");
  CHECK_FALSE(failed.bodies.at("boss_body").shape);
  solid(failed, 6000, 2200, {5, 10, 15}, "plate", false);
  kernel.null_face = kernel.omit_faces = false;
  kernel.wrong_owner.reset();
  boss(evaluator.evaluate(source(d)).value());
}

TEST_CASE("filtered sketch bindings revalidate recreated snapshots and account for retained bytes",
          "[face-filters]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto d = attached();
  selector(d, R"({"faces_of":"plate","where":{"normal":"z"}})");
  const auto first = evaluator.evaluate(source(d)).value();
  boss(first);
  d.history_end = "outline";
  REQUIRE(evaluator.evaluate(source(d))->ok());
  evaluator.set_cache_limits({3, 64 * 1024 * 1024});
  evaluator.set_cache_limits({128, 64 * 1024 * 1024});
  d.history_end.reset();
  const auto rebuilt = evaluator.evaluate(source(d)).value();
  boss(rebuilt);
  CHECK(rebuilt.binding_misses == 1);
  CHECK(rebuilt.cache_hits == 2);
  CHECK(rebuilt.kernel_calls == 2);
  Evaluator limited(*kernel, {128, first.bodies.at("plate").shape->estimated_bytes()});
  d.history_end = "face_sketch";
  REQUIRE(limited.evaluate(source(d))->ok());
  CHECK(limited.cache_usage().entries == 1);
  CHECK(limited.evaluate(source(d))->kernel_calls == 2);
  boss(first);
}

TEST_CASE("filtered snapshots survive cache eviction and producing facade teardown",
          "[face-filters]") {
  Evaluation retained;
  {
    auto kernel = kernel::make_occt_kernel().value();
    Evaluator evaluator(*kernel, {1, 64 * 1024 * 1024});
    auto d = attached();
    selector(d, R"({"faces_of":"plate","where":{"normal":"z"}})");
    retained = evaluator.evaluate(source(d)).value();
    const auto rebuilt = evaluator.evaluate(source(d)).value();
    boss(rebuilt);
    CHECK(rebuilt.kernel_calls == 3);
    CHECK(rebuilt.cache_hits == 0);
    evaluator.clear_cache();
  }
  boss(retained);
  solid(retained, 6000, 2200, {5, 10, 15}, "plate");
}
