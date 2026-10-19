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
using ir::json::Value;
Value set(const Value& object, std::string_view key, Value value) {
  auto members = *object.if_object();
  for (auto& member : members)
    if (member.key == key) member.value = std::move(value);
  return members;
}
ir::Document mirrored() {
  auto d = part();
  d.params.push_back({"mirror_x", "0 mm", {}, {}, {}, {}});
  d.features[0] = feature("outline", "sketch", R"({"plane":{"datum":"xy"},"entities":[
    {"id":"r","type":"rect","corner":[9,-1],"size":[2,2]}],"constraints":[]})");
  d.features.insert(d.features.begin() + 2,
                    feature("reflect", "mirror", R"({"bodies":["plate"],"plane":{
    "origin":["mirror_x",0,0],"normal":"x","x_direction":"y"}})"));
  return d;
}
ir::Document face_mirrored() {
  auto d = mirrored();
  d.params.push_back({"plane_depth", "40 mm", {}, {}, {}, {}});
  d.features.insert(d.features.begin() + 2,
                    feature("plane_base", "extrude", R"({"profile":{"sketch":"outline"},
    "distance":"plane_depth","result":{"new":"reference","body_type":"brep"}})"));
  d.features[3].inputs = set(
      d.features[3].inputs, "plane",
      ir::json::parse(R"({"created_by":"plane_base","role":"cap_end","expect":"one"})").value());
  return d;
}
void plane_selector(ir::Document& d, const char* text, std::size_t index = 3) {
  d.features[index].inputs = set(d.features[index].inputs, "plane", ir::json::parse(text).value());
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
           const std::string& mirror = "reflect", const std::string& body = "plate") {
  const auto& named = result.bodies.at(body).names;
  CHECK(std::ranges::any_of(named, [&](const auto& n) { return n.feature == seed; }));
  CHECK(std::ranges::any_of(
      named, [&](const auto& n) { return n.feature == mirror && n.role == "instance"; }));
  for (const auto& n : named) CHECK((n.feature == seed || n.feature == mirror));
}
class FaultKernel final : public kernel::IKernel {
 public:
  kernel::IKernel& backend;
  int reflections = 0, unions = 0, fail_mirror = 0, fail_union = 0;
  bool lose_copy_history = false, lose_union_history = false;
  bool fail_query = false, fail_distance = false;
  int queries = 0, distances = 0;
  explicit FaultKernel(kernel::IKernel& real) : backend(real) {}
  std::string_view identity() const noexcept override { return backend.identity(); }
  Capabilities capabilities() const noexcept override { return backend.capabilities(); }
  std::expected<kernel::Mesh, kernel::KernelError> tessellate(
      const kernel::ShapePtr& shape, const kernel::MeshingOptions& options) override {
    return backend.tessellate(shape, options);
  }
  std::expected<std::vector<kernel::EntityPtr>, kernel::KernelError> edges(
      const kernel::EntityPtr& face) override {
    return backend.edges(face);
  }
  std::expected<kernel::OperationResult, kernel::KernelError> fillet(
      const kernel::ShapePtr& shape, const std::vector<kernel::EntityPtr>& edges,
      double radius) override {
    return backend.fillet(shape, edges, radius);
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
    ++distances;
    if (fail_distance)
      return std::unexpected(kernel::KernelError{"kernel", "injected distance failure"});
    return backend.face_distance(e, point);
  }
  std::expected<std::vector<kernel::FaceInfo>, kernel::KernelError> faces(
      const kernel::ShapePtr& s) override {
    ++queries;
    if (fail_query)
      return std::unexpected(kernel::KernelError{"kernel", "injected face query failure"});
    return backend.faces(s);
  }
  std::expected<kernel::Plane, kernel::KernelError> face_plane(
      const kernel::EntityPtr& e) override {
    ++queries;
    if (fail_query)
      return std::unexpected(kernel::KernelError{"kernel", "injected face query failure"});
    return backend.face_plane(e);
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
      const kernel::ShapePtr& shape, const kernel::RigidTransform& input) override {
    return backend.transform(shape, input);
  }
  std::expected<kernel::OperationResult, kernel::KernelError> mirror(
      const kernel::ShapePtr& shape, const kernel::Plane& plane) override {
    if (++reflections == fail_mirror)
      return std::unexpected(kernel::KernelError{"kernel", "injected reflection failure"});
    auto result = backend.mirror(shape, plane);
    if (result && lose_copy_history) result->history.clear();
    return result;
  }
  std::expected<kernel::OperationResult, kernel::KernelError> boolean_operation(
      const kernel::ShapePtr& target, const std::vector<kernel::ShapePtr>& tools,
      kernel::BooleanOp op) override {
    if (++unions == fail_union)
      return std::unexpected(kernel::KernelError{"kernel", "injected mirror union failure"});
    auto result = backend.boolean_operation(target, tools, op);
    if (result && lose_union_history) result->history.clear();
    return result;
  }
};
}  // namespace

TEST_CASE("body mirrors retain original and reflected instances with current names and cache reuse",
          "[mirror-evaluator]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto d = mirrored();
  const auto first = evaluator.evaluate(source(d)).value();
  solid(first, 240, 496, {3, 0, 15}, 2);
  names(first);
  CHECK(first.kernel_calls == 4);
  CHECK(first.bodies.size() == 1);
  CHECK(first.bodies.at("plate").shape->measures().faces == 12);
  CHECK_THAT(first.bodies.at("plate").shape->measures().minimum.x, WithinAbs(-8, 1e-8));
  CHECK_THAT(first.bodies.at("plate").shape->measures().maximum.x, WithinAbs(14, 1e-8));
  const auto cached = evaluator.evaluate(source(d)).value();
  solid(cached, 240, 496, {3, 0, 15}, 2);
  names(cached);
  CHECK(cached.kernel_calls == 0);
  CHECK(cached.cache_hits == 4);
  d.params[2].value = 2.0;
  CHECK(evaluator.evaluate(source(d))->cache_hits == 4);
}

TEST_CASE("mirror plane and seed edits rebuild dependents while equivalent units reuse outputs",
          "[mirror-evaluator]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto d = mirrored();
  const auto first = evaluator.evaluate(source(d)).value();
  solid(first, 240, 496, {3, 0, 15}, 2);
  d.params[3].value = "5 mm";
  const auto shifted = evaluator.evaluate(source(d)).value();
  solid(shifted, 240, 496, {8, 0, 15}, 2);
  CHECK(shifted.kernel_calls == 3);
  CHECK(shifted.cache_hits == 2);
  CHECK(shifted.features[2].hash != first.features[2].hash);
  d.params[3].value = "0.5 cm";
  CHECK(evaluator.evaluate(source(d))->cache_hits == 4);
  d.params[0].value = "4 cm";
  const auto deeper = evaluator.evaluate(source(d)).value();
  solid(deeper, 320, 656, {8, 0, 20}, 2);
  CHECK(deeper.kernel_calls == 4);
  CHECK(deeper.cache_hits == 1);
  Evaluator cold(*kernel, {128, 64 * 1024 * 1024});
  solid(cold.evaluate(source(d)).value(), 320, 656, {8, 0, 20}, 2);
  solid(first, 240, 496, {3, 0, 15}, 2);
}

TEST_CASE("body mirrors support all datum planes and normalized tilted explicit frames",
          "[mirror-evaluator]") {
  struct Case {
    const char* plane;
    double volume, area;
    math::Vec3 center;
    std::size_t solids;
  };
  auto kernel = kernel::make_occt_kernel().value();
  for (const auto& c : {Case{R"({"datum":"xy"})", 240, 488, {13, 0, 0}, 1},
                        Case{R"({"datum":"yz"})", 240, 496, {3, 0, 15}, 2},
                        Case{R"({"datum":"zx"})", 120, 248, {13, 0, 15}, 1},
                        Case{R"({"origin":[0,0,0],"normal":[2,2,0],"x_direction":[0,0,3]})",
                             240,
                             496,
                             {8, -5, 15},
                             2}}) {
    auto d = mirrored();
    d.features[2].inputs = set(d.features[2].inputs, "plane", ir::json::parse(c.plane).value());
    Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
    const auto result = evaluator.evaluate(source(d)).value();
    solid(result, c.volume, c.area, c.center, c.solids);
    names(result);
  }
}

TEST_CASE("mirrored touching overlapping and coincident copies fuse without multiplying material",
          "[mirror-evaluator]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto d = mirrored();
  for (const double x : {9.0, 9.5, 10.0}) {
    d.params[3].value = std::to_string(x) + " mm";
    const double width = 2 * (11 - x);
    const auto result = evaluator.evaluate(source(d)).value();
    solid(result, width * 60, 64 * width + 120, {x + 3, 0, 15}, 1);
    names(result);
    CHECK(result.bodies.at("plate").shape->measures().faces == 6);
  }
}

TEST_CASE("body mirrors preserve exact annular holes and periodic face naming",
          "[mirror-evaluator]") {
  auto d = mirrored();
  d.features[0] = feature("outline", "sketch", R"({"plane":{"datum":"xy"},"entities":[
    {"id":"outer","type":"circle","center":[10,0],"radius":2},
    {"id":"inner","type":"circle","center":[10,0],"radius":1}],"constraints":[]})");
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  constexpr double pi = std::numbers::pi;
  const auto result = evaluator.evaluate(source(d)).value();
  solid(result, 180 * pi, 372 * pi, {3, 0, 15}, 2);
  names(result);
  CHECK(result.bodies.at("plate").shape->measures().faces == 8);
  const auto cached = evaluator.evaluate(source(d)).value();
  solid(cached, 180 * pi, 372 * pi, {3, 0, 15}, 2);
  names(cached);
  CHECK(cached.kernel_calls == 0);
}

TEST_CASE("mirrors consume current multi solid seeds and preserve nested pattern provenance",
          "[mirror-evaluator]") {
  auto d = mirrored();
  d.features.insert(d.features.begin() + 2,
                    feature("radial_seed", "pattern_linear", R"({"bodies":["plate"],"count":2,
    "spacing":4,"direction":"x"})"));
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  const auto first = evaluator.evaluate(source(d)).value();
  solid(first, 480, 992, {3, 0, 15}, 4);
  CHECK(first.kernel_calls == 6);
  CHECK_THAT(first.bodies.at("plate").shape->measures().maximum.x, WithinAbs(18, 1e-8));
  const auto cached = evaluator.evaluate(source(d)).value();
  solid(cached, 480, 992, {3, 0, 15}, 4);
  CHECK(cached.kernel_calls == 0);
  CHECK(cached.cache_hits == 5);
  for (const auto* id : {"radial_seed", "reflect"})
    CHECK(std::ranges::any_of(cached.bodies.at("plate").names, [&](const auto& n) {
      return n.feature == id && n.role == "instance";
    }));
  d.features[2].inputs = set(d.features[2].inputs, "spacing", 6.0);
  const auto edited = evaluator.evaluate(source(d)).value();
  solid(edited, 480, 992, {3, 0, 15}, 4);
  CHECK_THAT(edited.bodies.at("plate").shape->measures().maximum.x, WithinAbs(20, 1e-8));
  CHECK(edited.kernel_calls == 5);
  CHECK(edited.cache_hits == 2);
}

TEST_CASE("body mirrors reject unsupported targets and invalid planes before reflection",
          "[mirror-evaluator]") {
  struct Invalid {
    const char* input;
    const char* pointer;
  };
  auto kernel = kernel::make_occt_kernel().value();
  for (const auto& [input, pointer] :
       {Invalid{R"({"bodies":["plate","plate"],"plane":{"datum":"yz"}})", "/bodies/1"},
        Invalid{R"({"features":["base"],"plane":{"datum":"yz"}})", "/features"},
        Invalid{R"({"bodies":["plate"],"plane":{"created_by":"base","expect":"one"}})", "/plane"},
        Invalid{
            R"({"bodies":["plate"],"plane":{"origin":[0,0,0],"normal":[0,0,0],"x_direction":"y"}})",
            "/plane/normal"},
        Invalid{
            R"({"bodies":["plate"],"plane":{"origin":[0,0,0],"normal":"x","x_direction":[0,0,0]}})",
            "/plane/x_direction"},
        Invalid{R"({"bodies":["plate"],"plane":{"origin":[0,0,0],"normal":"x","x_direction":"x"}})",
                "/plane"}}) {
    auto d = mirrored();
    d.features[2].inputs = ir::json::parse(input).value();
    Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
    const auto result = evaluator.evaluate(source(d)).value();
    REQUIRE_FALSE(result.ok());
    REQUIRE_FALSE(result.features[2].errors.empty());
    CHECK(result.features[2].errors.front().pointer == "/features/2/inputs" + std::string(pointer));
    CHECK(result.kernel_calls == 1);
    CHECK_FALSE(result.bodies.at("plate").shape);
    CHECK(result.features[3].errors.front().code == "input_failed");
  }
}

TEST_CASE("mirror work limits count both instances for every body before reflection",
          "[mirror-evaluator]") {
  auto d = mirrored();
  d.features.resize(2);
  Array bodies{"plate"};
  for (int i = 1; i < 33; ++i) {
    const auto id = "body_" + std::to_string(i);
    const auto text = R"({"profile":{"sketch":"outline"},"distance":20,"result":{"new":")" + id +
                      R"(","body_type":"brep"}})";
    d.features.push_back(feature("seed_" + std::to_string(i), "extrude", text.c_str()));
    bodies.push_back(id);
  }
  auto mirror = feature("reflect", "mirror", R"({"bodies":["plate"],"plane":{"datum":"yz"}})");
  mirror.inputs = set(mirror.inputs, "bodies", std::move(bodies));
  d.features.push_back(std::move(mirror));
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

TEST_CASE("mirror admission accepts the full instance budget", "[mirror-evaluator]") {
  auto f = feature("reflect", "mirror", R"({"bodies":["plate"],"plane":{"datum":"yz"}})");
  Array bodies;
  for (int i = 0; i < 32; ++i) bodies.push_back("body_" + std::to_string(i));
  f.inputs = set(f.inputs, "bodies", std::move(bodies));
  const auto inputs = ir::FeatureInputs::compile(f).value().evaluate().value();
  const auto plane = prepare_mirror(inputs);
  REQUIRE(plane);
  CHECK(plane->origin == math::Vec3{});
  CHECK(plane->normal == math::Vec3{1, 0, 0});
  CHECK(plane->x_axis == math::Vec3{0, 1, 0});
}

TEST_CASE("mirror coordinate overflow invalidates dependents and recovers after a plane edit",
          "[mirror-evaluator]") {
  auto d = mirrored();
  d.params[3].value = "1e308 mm";
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  const auto failed = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(failed.ok());
  CHECK(failed.features[2].errors.front().pointer == "/features/2/inputs");
  CHECK(failed.features[2].errors.front().code == "kernel");
  CHECK(failed.kernel_calls == 2);
  CHECK_FALSE(failed.bodies.at("plate").shape);
  CHECK(failed.features[3].errors.front().code == "input_failed");
  CHECK(evaluator.cache_usage().entries == 2);
  d.params[3].value = "0 mm";
  const auto retried = evaluator.evaluate(source(d)).value();
  solid(retried, 240, 496, {3, 0, 15}, 2);
  CHECK(retried.kernel_calls == 3);
  CHECK(retried.cache_hits == 2);
}

TEST_CASE("mirror cached roles rebind IDs and rebuild after stale seed snapshots",
          "[mirror-evaluator]") {
  auto d = mirrored();
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  const auto first = evaluator.evaluate(source(d)).value();
  solid(first, 240, 496, {3, 0, 15}, 2);
  d.features[1].id = "renamed_base";
  d.features[2].id = "renamed_reflect";
  const auto renamed = evaluator.evaluate(source(d)).value();
  solid(renamed, 240, 496, {3, 0, 15}, 2);
  names(renamed, "renamed_base", "renamed_reflect");
  CHECK(renamed.cache_hits == 4);
  d.history_end = "outline";
  REQUIRE(evaluator.evaluate(source(d))->ok());
  evaluator.set_cache_limits({3, 64 * 1024 * 1024});
  evaluator.set_cache_limits({128, 64 * 1024 * 1024});
  d.history_end.reset();
  const auto rebuilt = evaluator.evaluate(source(d)).value();
  solid(rebuilt, 240, 496, {3, 0, 15}, 2);
  names(rebuilt, "renamed_base", "renamed_reflect");
  CHECK(rebuilt.kernel_calls == 4);
  CHECK(rebuilt.cache_hits == 1);
  CHECK(rebuilt.binding_misses == 2);
  solid(first, 240, 496, {3, 0, 15}, 2);
}

TEST_CASE("body mirrors respect suppression rollback and unavailable seed diagnostics",
          "[mirror-evaluator]") {
  auto d = mirrored();
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  d.history_end = "base";
  const auto rolled = evaluator.evaluate(source(d)).value();
  solid(rolled, 120, 248, {10, 0, 15}, 1);
  CHECK(rolled.features[2].status == FeatureStatus::kRolledBack);
  d.history_end = "reflect";
  solid(evaluator.evaluate(source(d)).value(), 240, 496, {0, 0, 15}, 2);
  d.history_end.reset();
  d.features[2].suppressed = true;
  solid(evaluator.evaluate(source(d)).value(), 120, 248, {13, 0, 15}, 1);
  d.features[2].suppressed = false;
  d.features[1].suppressed = true;
  const auto failed = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(failed.ok());
  CHECK(failed.features[2].errors.front().code == "input_suppressed");
  CHECK(failed.features[2].errors.front().pointer == "/features/2/inputs/bodies/0");
  CHECK_FALSE(failed.bodies.at("plate").shape);
}

TEST_CASE("multi body mirror failures publish no partial copies and recover on retry",
          "[mirror-evaluator]") {
  auto real = kernel::make_occt_kernel().value();
  for (const bool fail_copy : {false, true}) {
    FaultKernel kernel(*real);
    kernel.fail_mirror = fail_copy ? 2 : 0;
    kernel.fail_union = fail_copy ? 0 : 2;
    Evaluator evaluator(kernel, {128, 64 * 1024 * 1024});
    auto d = mirrored();
    d.features.insert(d.features.begin() + 2,
                      feature("other_base", "extrude", R"({"profile":{"sketch":"outline"},
      "distance":20,"result":{"new":"other","body_type":"brep"}})"));
    d.features[3].inputs = set(d.features[3].inputs, "bodies", Array{"plate", "other"});
    d.features.push_back(feature("independent", "extrude", R"({"profile":{"sketch":"outline"},
      "distance":10,"result":{"new":"third","body_type":"brep"}})"));
    const auto failed = evaluator.evaluate(source(d)).value();
    REQUIRE_FALSE(failed.ok());
    CHECK(failed.features[3].errors.front().message.find("injected") != std::string::npos);
    CHECK_FALSE(failed.bodies.at("plate").shape);
    CHECK_FALSE(failed.bodies.at("other").shape);
    solid(failed, 40, 88, {10, 0, 5}, 1, "third", false);
    CHECK(failed.kernel_calls == (fail_copy ? 6 : 7));
    kernel.fail_mirror = kernel.fail_union = 0;
    kernel.reflections = kernel.unions = 0;
    const auto retried = evaluator.evaluate(source(d)).value();
    solid(retried, 240, 496, {3, 0, 15}, 2);
    solid(retried, 160, 336, {0, 0, 10}, 2, "other");
    names(retried);
    names(retried, "other_base", "reflect", "other");
    CHECK(retried.kernel_calls == 5);
    CHECK(retried.cache_hits == 4);
  }
}

TEST_CASE("mirrors reject incomplete reflection or union history without caching it",
          "[mirror-evaluator]") {
  auto real = kernel::make_occt_kernel().value();
  for (const bool copy : {false, true}) {
    FaultKernel kernel(*real);
    kernel.lose_copy_history = copy;
    kernel.lose_union_history = !copy;
    Evaluator evaluator(kernel, {128, 64 * 1024 * 1024});
    const auto d = mirrored();
    const auto failed = evaluator.evaluate(source(d)).value();
    REQUIRE_FALSE(failed.ok());
    CHECK(failed.features[2].errors.front().message == "incomplete naming history");
    CHECK_FALSE(failed.bodies.at("plate").shape);
    CHECK(evaluator.cache_usage().entries == 2);
    kernel.lose_copy_history = kernel.lose_union_history = false;
    const auto retried = evaluator.evaluate(source(d)).value();
    solid(retried, 240, 496, {3, 0, 15}, 2);
    names(retried);
    CHECK(retried.kernel_calls == 3);
    CHECK(retried.cache_hits == 2);
  }
}

TEST_CASE("mirrored snapshots survive cache eviction and producing kernel teardown",
          "[mirror-evaluator]") {
  Evaluation retained;
  {
    auto kernel = kernel::make_occt_kernel().value();
    Evaluator evaluator(*kernel, {1, 64 * 1024 * 1024});
    retained = evaluator.evaluate(source(mirrored())).value();
    const auto rebuilt = evaluator.evaluate(source(mirrored())).value();
    solid(rebuilt, 240, 496, {3, 0, 15}, 2);
    names(rebuilt);
    CHECK(rebuilt.kernel_calls == 4);
    CHECK(rebuilt.cache_hits == 0);
    evaluator.clear_cache();
    CHECK(evaluator.cache_usage().estimated_bytes == 0);
  }
  solid(retained, 240, 496, {3, 0, 15}, 2);
  names(retained);
}

TEST_CASE("face mirror planes preserve reference bodies and query current frames on cache hits",
          "[mirror-selector]") {
  auto real = kernel::make_occt_kernel().value();
  FaultKernel kernel(*real);
  Evaluator evaluator(kernel, {128, 64 * 1024 * 1024});
  auto d = face_mirrored();
  d.history_end = "plane_base";
  const auto before = evaluator.evaluate(source(d)).value();
  d.history_end.reset();
  const auto first = evaluator.evaluate(source(d)).value();
  solid(first, 240, 496, {13, 0, 40}, 2);
  solid(first, 160, 328, {10, 0, 20}, 1, "reference");
  names(first);
  CHECK(first.bodies.at("reference").shape == before.bodies.at("reference").shape);
  CHECK(first.bodies.at("reference").hash == before.bodies.at("reference").hash);
  CHECK(first.bodies.size() == 2);
  CHECK(first.kernel_calls == 4);
  CHECK(first.cache_hits == 3);
  CHECK(kernel.reflections == 1);
  CHECK(kernel.queries == 1);
  const auto cached = evaluator.evaluate(source(d)).value();
  solid(cached, 240, 496, {13, 0, 40}, 2);
  names(cached);
  CHECK(cached.kernel_calls == 1);
  CHECK(cached.cache_hits == 5);
  CHECK(kernel.reflections == 1);
  CHECK(kernel.queries == 2);
}

TEST_CASE("mirror selectors track plane source target and equivalent unit edits",
          "[mirror-selector]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto d = face_mirrored();
  const auto first = evaluator.evaluate(source(d)).value();
  solid(first, 240, 496, {13, 0, 40}, 2);
  d.params[4].value = "50 mm";
  const auto moved = evaluator.evaluate(source(d)).value();
  solid(moved, 240, 496, {13, 0, 50}, 2);
  solid(moved, 200, 408, {10, 0, 25}, 1, "reference");
  CHECK(moved.kernel_calls == 5);
  CHECK(moved.cache_hits == 2);
  CHECK(moved.features[3].hash != first.features[3].hash);
  d.params[4].value = "5 cm";
  CHECK(evaluator.evaluate(source(d))->cache_hits == 5);
  d.params[0].value = "20 mm";
  const auto shorter = evaluator.evaluate(source(d)).value();
  solid(shorter, 160, 336, {13, 0, 50}, 2);
  CHECK(shorter.kernel_calls == 5);
  CHECK(shorter.cache_hits == 2);
  Evaluator cold(*kernel, {128, 64 * 1024 * 1024});
  solid(cold.evaluate(source(d)).value(), 160, 336, {13, 0, 50}, 2);
  solid(first, 240, 496, {13, 0, 40}, 2);
}

TEST_CASE("mirror planes compose generated and body face selectors without extra targets",
          "[mirror-selector]") {
  auto real = kernel::make_occt_kernel().value();
  FaultKernel kernel(*real);
  Evaluator evaluator(kernel, {128, 64 * 1024 * 1024});
  auto d = face_mirrored();
  plane_selector(d, R"({"any":[{"created_by":"plane_base","role":"cap_end"},
    {"faces_of":"reference","where":{"type":"plane","normal":"z"}}],
    "except":{"created_by":"plane_base","role":"cap_start"},
    "where":{"parallel_to":"x","perpendicular_to":"z","near":[10,0,"plane_depth + 1 mm"]},
    "expect":"one"})");
  const auto first = evaluator.evaluate(source(d)).value();
  solid(first, 240, 496, {13, 0, 40}, 2);
  solid(first, 160, 328, {10, 0, 20}, 1, "reference");
  CHECK(first.kernel_calls == 7);
  CHECK(kernel.queries == 1);
  CHECK(kernel.distances == 1);
  CHECK(kernel.reflections == 1);
  const auto cached = evaluator.evaluate(source(d)).value();
  solid(cached, 240, 496, {13, 0, 40}, 2);
  CHECK(cached.kernel_calls == 2);
  CHECK(cached.cache_hits == 5);
  CHECK(kernel.queries == 2);
  CHECK(kernel.distances == 2);
  CHECK(kernel.reflections == 1);
}

TEST_CASE("mirrors use current transformed face planes and may select their own seed",
          "[mirror-selector]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto d = face_mirrored();
  SECTION("current reference placement") {
    d.features.insert(d.features.begin() + 3,
                      feature("move_reference", "transform",
                              R"({"bodies":["reference"],"translation":[0,0,10]})"));
    const auto result = evaluator.evaluate(source(d)).value();
    solid(result, 240, 496, {13, 0, 50}, 2);
    solid(result, 160, 328, {10, 0, 30}, 1, "reference");
    CHECK(result.kernel_calls == 7);
  }
  SECTION("tilted reference plane") {
    d.features.insert(d.features.begin() + 3,
                      feature("tilt_reference", "transform",
                              R"({"bodies":["reference"],"rotation":{"axis":{
      "origin":[0,0,0],"direction":"y"},"angle":90}})"));
    const auto result = evaluator.evaluate(source(d)).value();
    solid(result, 240, 496, {43, 0, 15}, 2);
    solid(result, 160, 328, {20, 0, -10}, 1, "reference");
    CHECK(result.kernel_calls == 7);
  }
  SECTION("same seed snapshot") {
    plane_selector(d, R"({"created_by":"base","role":"cap_end","expect":"one"})");
    const auto result = evaluator.evaluate(source(d)).value();
    solid(result, 240, 488, {13, 0, 30}, 1);
    solid(result, 160, 328, {10, 0, 20}, 1, "reference");
    names(result);
    CHECK(result.kernel_calls == 6);
    CHECK(result.bodies.at("plate").shape->measures().faces == 6);
  }
}

TEST_CASE("mirror selectors follow consumed producer faces but reject retired body references",
          "[mirror-selector]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto d = face_mirrored();
  d.features.insert(
      d.features.begin() + 3,
      feature("merge", "boolean", R"({"target":"plate","tools":["reference"],"op":"union"})"));
  SECTION("producer follows surviving body") {
    const auto result = evaluator.evaluate(source(d)).value();
    solid(result, 320, 648, {13, 0, 40}, 1);
    CHECK_FALSE(result.bodies.at("reference").shape);
    CHECK(result.kernel_calls == 7);
  }
  SECTION("body references remain unavailable") {
    plane_selector(d, R"({"faces_of":"reference","where":{"normal":"z"},"expect":"one"})", 4);
    const auto failed = evaluator.evaluate(source(d)).value();
    REQUIRE_FALSE(failed.ok());
    REQUIRE_FALSE(failed.features[4].errors.empty());
    CHECK(failed.features[4].errors.front().code == "input_failed");
    CHECK(failed.features[4].errors.front().pointer == "/features/4/inputs/plane/faces_of");
    CHECK_FALSE(failed.bodies.at("plate").shape);
    CHECK(failed.kernel_calls == 3);
  }
}

TEST_CASE("mirror selectors reject empty ambiguous tied and nested unsupported planes",
          "[mirror-selector]") {
  struct Invalid {
    const char* selector;
    const char* pointer;
    const char* code;
  };
  auto real = kernel::make_occt_kernel().value();
  for (const auto& [selector, pointer, code] :
       {Invalid{R"({"faces_of":"reference","expect":"one"})", "", "selector_count"},
        Invalid{R"({"faces_of":"reference","where":{"normal":[1,1,1]},"expect":"any"})", "",
                "selector_count"},
        Invalid{R"({"faces_of":"reference","where":{"perpendicular_to":"z","near":[10,0,20]},
                    "expect":"any"})",
                "", "selector_count"},
        Invalid{R"({"any":[{"faces_of":"reference","where":{"radius":0}}]})", "/any/0/where/radius",
                "kernel"},
        Invalid{R"({"any":[{"created_by":"plane_base","role":"missing","expect":"one"}]})",
                "/any/0", "selector_count"}}) {
    INFO(selector);
    FaultKernel kernel(*real);
    Evaluator evaluator(kernel, {128, 64 * 1024 * 1024});
    auto d = face_mirrored();
    plane_selector(d, selector);
    const auto failed = evaluator.evaluate(source(d)).value();
    REQUIRE_FALSE(failed.ok());
    REQUIRE_FALSE(failed.features[3].errors.empty());
    CHECK(failed.features[3].errors.front().code == code);
    CHECK(failed.features[3].errors.front().pointer ==
          "/features/3/inputs/plane" + std::string(pointer));
    CHECK(kernel.reflections == 0);
    CHECK_FALSE(failed.bodies.at("plate").shape);
    solid(failed, 160, 328, {10, 0, 20}, 1, "reference", false);
    CHECK(failed.features[4].errors.front().code == "input_failed");
  }
}

TEST_CASE("uniquely nearest curved mirror planes fail before reflection", "[mirror-selector]") {
  auto real = kernel::make_occt_kernel().value();
  FaultKernel kernel(*real);
  Evaluator evaluator(kernel, {128, 64 * 1024 * 1024});
  auto d = face_mirrored();
  d.features[0] = feature("outline", "sketch", R"({"plane":{"datum":"xy"},"entities":[
    {"id":"r","type":"circle","center":[10,0],"radius":1}],"constraints":[]})");
  plane_selector(d, R"({"faces_of":"reference","where":{"near":[12,1,20]},"expect":"one"})");
  const auto failed = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(failed.ok());
  CHECK(failed.features[3].errors.front().pointer == "/features/3/inputs/plane");
  CHECK(failed.features[3].errors.front().message == "selected face is not planar");
  CHECK(kernel.reflections == 0);
  CHECK(kernel.queries == 1);
  CHECK(kernel.distances == 6);
  solid(failed, 40 * std::numbers::pi, 82 * std::numbers::pi, {10, 0, 20}, 1, "reference", false);
}

TEST_CASE("unavailable mirror sources suppression and rollback do not reuse old reflections",
          "[mirror-selector]") {
  auto real = kernel::make_occt_kernel().value();
  FaultKernel kernel(*real);
  Evaluator evaluator(kernel, {128, 64 * 1024 * 1024});
  auto d = face_mirrored();
  REQUIRE(evaluator.evaluate(source(d))->ok());
  d.features[2].suppressed = true;
  const auto failed = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(failed.ok());
  CHECK(failed.features[3].errors.front().code == "input_suppressed");
  CHECK(failed.features[3].errors.front().pointer == "/features/3/inputs/plane/created_by");
  CHECK_FALSE(failed.bodies.at("plate").shape);
  CHECK(kernel.reflections == 1);
  d.features[2].suppressed = false;
  d.features[3].suppressed = true;
  const auto skipped = evaluator.evaluate(source(d)).value();
  solid(skipped, 120, 248, {13, 0, 15}, 1);
  CHECK(skipped.kernel_calls == 1);
  d.features[3].suppressed = false;
  d.history_end = "plane_base";
  const auto rolled = evaluator.evaluate(source(d)).value();
  solid(rolled, 120, 248, {10, 0, 15}, 1);
  CHECK(rolled.features[3].status == FeatureStatus::kRolledBack);
  CHECK(rolled.kernel_calls == 0);
  d.history_end.reset();
  solid(evaluator.evaluate(source(d)).value(), 240, 496, {13, 0, 40}, 2);
  CHECK(kernel.reflections == 1);
}

TEST_CASE("mirror face query failures precede cached reuse and recover without partial targets",
          "[mirror-selector]") {
  auto real = kernel::make_occt_kernel().value();
  for (const bool distance : {false, true}) {
    FaultKernel kernel(*real);
    Evaluator evaluator(kernel, {128, 64 * 1024 * 1024});
    auto d = face_mirrored();
    if (distance)
      plane_selector(d, R"({"faces_of":"reference",
        "where":{"normal":"z","near":[10,0,41]},"expect":"one"})");
    REQUIRE(evaluator.evaluate(source(d))->ok());
    kernel.fail_query = !distance;
    kernel.fail_distance = distance;
    const auto failed = evaluator.evaluate(source(d)).value();
    REQUIRE_FALSE(failed.ok());
    CHECK(failed.features[3].errors.front().pointer ==
          std::string("/features/3/inputs/plane") + (distance ? "/where/near" : ""));
    CHECK_FALSE(failed.bodies.at("plate").shape);
    solid(failed, 160, 328, {10, 0, 20}, 1, "reference", false);
    CHECK(kernel.reflections == 1);
    CHECK(failed.cache_hits == 3);
    kernel.fail_query = kernel.fail_distance = false;
    const auto recovered = evaluator.evaluate(source(d)).value();
    solid(recovered, 240, 496, {13, 0, 40}, 2);
    CHECK(recovered.cache_hits == 5);
    CHECK(recovered.kernel_calls == (distance ? 2 : 1));
    CHECK(kernel.reflections == 1);
  }
}

TEST_CASE(
    "selected plane mirrors publish multiple targets atomically and preserve reference bodies",
    "[mirror-selector]") {
  auto real = kernel::make_occt_kernel().value();
  for (const bool fail_copy : {false, true}) {
    FaultKernel kernel(*real);
    kernel.fail_mirror = fail_copy ? 2 : 0;
    kernel.fail_union = fail_copy ? 0 : 2;
    Evaluator evaluator(kernel, {128, 64 * 1024 * 1024});
    auto d = face_mirrored();
    d.features.insert(d.features.begin() + 3,
                      feature("other_base", "extrude", R"({"profile":{"sketch":"outline"},
      "distance":20,"result":{"new":"other","body_type":"brep"}})"));
    d.features[4].inputs = set(d.features[4].inputs, "bodies", Array{"plate", "other"});
    const auto failed = evaluator.evaluate(source(d)).value();
    REQUIRE_FALSE(failed.ok());
    CHECK(failed.features[4].errors.front().message.find("injected") != std::string::npos);
    CHECK_FALSE(failed.bodies.at("plate").shape);
    CHECK_FALSE(failed.bodies.at("other").shape);
    solid(failed, 160, 328, {10, 0, 20}, 1, "reference", false);
    kernel.fail_mirror = kernel.fail_union = 0;
    const auto recovered = evaluator.evaluate(source(d)).value();
    solid(recovered, 240, 496, {13, 0, 40}, 2);
    solid(recovered, 160, 336, {10, 0, 40}, 2, "other");
    solid(recovered, 160, 328, {10, 0, 20}, 1, "reference");
    CHECK(recovered.cache_hits == 4);
    CHECK(recovered.kernel_calls == 6);
  }
}

TEST_CASE("mirror selector cache rebinding rejects recreated reference snapshots",
          "[mirror-selector]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto d = face_mirrored();
  const auto first = evaluator.evaluate(source(d)).value();
  solid(first, 240, 496, {13, 0, 40}, 2);
  d.history_end = "base";
  REQUIRE(evaluator.evaluate(source(d))->ok());
  evaluator.set_cache_limits({4, 64 * 1024 * 1024});
  evaluator.set_cache_limits({128, 64 * 1024 * 1024});
  d.history_end.reset();
  const auto rebuilt = evaluator.evaluate(source(d)).value();
  solid(rebuilt, 240, 496, {13, 0, 40}, 2);
  names(rebuilt);
  CHECK(rebuilt.kernel_calls == 5);
  CHECK(rebuilt.cache_hits == 2);
  CHECK(rebuilt.binding_misses == 2);
  CHECK(rebuilt.bodies.at("reference").shape != first.bodies.at("reference").shape);
  solid(first, 240, 496, {13, 0, 40}, 2);
  solid(first, 160, 328, {10, 0, 20}, 1, "reference");
}

TEST_CASE("selected mirror geometry survives small cache budgets and backend teardown",
          "[mirror-selector]") {
  Evaluation retained;
  {
    auto kernel = kernel::make_occt_kernel().value();
    Evaluator evaluator(*kernel, {1, 64 * 1024 * 1024});
    const auto d = face_mirrored();
    retained = evaluator.evaluate(source(d)).value();
    solid(retained, 240, 496, {13, 0, 40}, 2);
    CHECK(evaluator.cache_usage().entries == 1);
    const auto rebuilt = evaluator.evaluate(source(d)).value();
    solid(rebuilt, 240, 496, {13, 0, 40}, 2);
    CHECK(rebuilt.cache_hits == 0);
    CHECK(rebuilt.kernel_calls == 6);
    evaluator.set_cache_limits({128, 1024});
    const auto uncached = evaluator.evaluate(source(d)).value();
    solid(uncached, 240, 496, {13, 0, 40}, 2);
    CHECK(evaluator.cache_usage().estimated_bytes <= 1024);
    evaluator.clear_cache();
    CHECK(evaluator.cache_usage().estimated_bytes == 0);
  }
  solid(retained, 240, 496, {13, 0, 40}, 2);
  solid(retained, 160, 328, {10, 0, 20}, 1, "reference");
  names(retained);
}

TEST_CASE("mirror selector hashes follow producer ID swaps on identical current source snapshots",
          "[mirror-selector]") {
  auto d = face_mirrored();
  d.features.insert(d.features.begin() + 3,
                    {feature("second_profile", "sketch", R"({"plane":{"origin":[20,0,0],
    "normal":"z","x_direction":"x"},"entities":[{"id":"r","type":"rect","corner":[9,-1],
    "size":[2,2]}],"constraints":[]})"),
                     feature("extension", "extrude", R"({"profile":{"sketch":"second_profile"},
    "distance":60,"result":{"join":"reference"}})")});
  SECTION("generated cap") {}
  SECTION("composed cap") {
    plane_selector(d, R"({"any":[{"created_by":"plane_base","role":"cap_end"},
      {"created_by":"plane_base","role":"missing","expect":"any"}],"expect":"one"})",
                   5);
  }
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  const auto first = evaluator.evaluate(source(d)).value();
  solid(first, 240, 496, {13, 0, 40}, 2);
  d.features[2].id = "extension";
  d.features[4].id = "plane_base";
  const auto swapped = evaluator.evaluate(source(d)).value();
  solid(swapped, 240, 496, {13, 0, 60}, 2);
  CHECK(swapped.bodies.at("reference").shape == first.bodies.at("reference").shape);
  CHECK(swapped.bodies.at("reference").hash == first.bodies.at("reference").hash);
  CHECK(swapped.features[5].hash != first.features[5].hash);
  CHECK(swapped.kernel_calls == 4);
  CHECK(swapped.cache_hits == 5);
  solid(first, 240, 496, {13, 0, 40}, 2);
}

TEST_CASE("mirror cache admission charges selector source snapshots beyond operation history",
          "[mirror-selector]") {
  auto kernel = kernel::make_occt_kernel().value();
  auto d = face_mirrored();
  d.history_end = "reflect";
  auto explicit_plane = d;
  plane_selector(explicit_plane, R"({"origin":[0,0,40],"normal":"z","x_direction":"x"})");
  Evaluator plain(*kernel, {128, 64 * 1024 * 1024});
  explicit_plane.history_end = "plane_base";
  REQUIRE(plain.evaluate(source(explicit_plane))->ok());
  const auto before = plain.cache_usage().estimated_bytes;
  explicit_plane.history_end = "reflect";
  REQUIRE(plain.evaluate(source(explicit_plane))->ok());
  const auto entry_bytes = plain.cache_usage().estimated_bytes - before;
  Evaluator selected(*kernel, {128, 64 * 1024 * 1024});
  const auto result = selected.evaluate(source(d)).value();
  REQUIRE(result.ok());
  const auto reference_bytes = result.bodies.at("reference").shape->estimated_bytes();
  CHECK(selected.cache_usage().estimated_bytes >=
        plain.cache_usage().estimated_bytes + reference_bytes + sizeof(kernel::ShapePtr));
  // This admits an explicit-plane entry plus its extra binding, but not the retained source.
  const auto budget = entry_bytes + sizeof(kernel::ShapePtr);
  Evaluator limited(*kernel, {128, budget});
  REQUIRE(limited.evaluate(source(d))->ok());
  const auto repeated = limited.evaluate(source(d)).value();
  REQUIRE(repeated.ok());
  CHECK(repeated.features[3].status == FeatureStatus::kBuilt);
  CHECK(limited.cache_usage().estimated_bytes <= budget);
}
