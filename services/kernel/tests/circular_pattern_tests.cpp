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
ir::Document patterned() {
  auto d = part();
  d.params.push_back({"sweep", "360 deg", {}, {}, {}, {}});
  d.params.push_back({"copies", 4.0, {}, {}, {}, {}});
  d.features[0] = feature("outline", "sketch", R"({"plane":{"datum":"xy"},"entities":[
    {"id":"r","type":"rect","corner":[9,-1],"size":[2,2]}],"constraints":[]})");
  d.features.insert(d.features.begin() + 2,
                    feature("repeat", "pattern_circular", R"({"bodies":["plate"],"count":"copies",
    "angle":"sweep","axis":{"origin":[0,0,0],"direction":"z"}})"));
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
  std::expected<kernel::OperationResult, kernel::KernelError> loft(
      const kernel::Loft& input) override {
    return backend.loft(input);
  }
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
      return std::unexpected(kernel::KernelError{"kernel", "injected circular copy failure"});
    auto result = backend.transform(input, t);
    if (result && lose_copy_history) result->history.clear();
    return result;
  }
  std::expected<kernel::OperationResult, kernel::KernelError> boolean_operation(
      const kernel::ShapePtr& target, const std::vector<kernel::ShapePtr>& tools,
      kernel::BooleanOp op) override {
    if (++unions == fail_union)
      return std::unexpected(kernel::KernelError{"kernel", "injected circular union failure"});
    auto result = backend.boolean_operation(target, tools, op);
    if (result && lose_union_history) result->history.clear();
    return result;
  }
};
}  // namespace

TEST_CASE("circular full turns space unique instances with inherited and local names",
          "[circular-pattern]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto d = patterned();
  const auto first = evaluator.evaluate(source(d)).value();
  solid(first, 480, 992, {3, 0, 15}, 4);
  names(first);
  CHECK(first.kernel_calls == 6);
  CHECK(first.bodies.size() == 1);
  CHECK(first.bodies.at("plate").shape->measures().faces == 24);
  CHECK_THAT(first.bodies.at("plate").shape->measures().minimum.x, WithinAbs(-8, 1e-8));
  CHECK_THAT(first.bodies.at("plate").shape->measures().maximum.x, WithinAbs(14, 1e-8));
  const auto cached = evaluator.evaluate(source(d)).value();
  solid(cached, 480, 992, {3, 0, 15}, 4);
  names(cached);
  CHECK(cached.kernel_calls == 0);
  CHECK(cached.cache_hits == 4);
  d.params[2].value = 2.0;
  CHECK(evaluator.evaluate(source(d))->cache_hits == 4);
}

TEST_CASE("circular partial sweeps include both endpoints and preserve signed direction",
          "[circular-pattern]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto d = patterned();
  d.params[4].value = 3.0;
  for (const double sign : {1.0, -1.0}) {
    d.params[3].value = sign > 0 ? "180 deg" : "-180 deg";
    const auto result = evaluator.evaluate(source(d)).value();
    solid(result, 360, 744, {3, sign * 10 / 3, 15}, 3);
    names(result);
    CHECK(result.kernel_calls == (sign > 0 ? 5 : 4));
    CHECK(result.cache_hits == (sign > 0 ? 0 : 2));
    Evaluator cold(*kernel, {128, 64 * 1024 * 1024});
    solid(cold.evaluate(source(d)).value(), 360, 744, {3, sign * 10 / 3, 15}, 3);
  }
}

TEST_CASE("circular angle count and seed edits rebuild only dependent features",
          "[circular-pattern]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto d = patterned();
  const auto first = evaluator.evaluate(source(d)).value();
  solid(first, 480, 992, {3, 0, 15}, 4);
  d.params[4].value = 3.0;
  const auto fewer = evaluator.evaluate(source(d)).value();
  solid(fewer, 360, 744, {3, 0, 15}, 3);
  CHECK(fewer.kernel_calls == 4);
  CHECK(fewer.cache_hits == 2);
  CHECK(fewer.features[2].hash != first.features[2].hash);
  d.params[3].value = "180 deg";
  const auto partial = evaluator.evaluate(source(d)).value();
  solid(partial, 360, 744, {3, 10.0 / 3, 15}, 3);
  CHECK(partial.kernel_calls == 4);
  CHECK(partial.cache_hits == 2);
  d.params[3].value = "pi * 1 rad";
  d.units.angle = "rad";
  const auto equivalent = evaluator.evaluate(source(d)).value();
  solid(equivalent, 360, 744, {3, 10.0 / 3, 15}, 3);
  CHECK(equivalent.kernel_calls == 0);
  CHECK(equivalent.cache_hits == 4);
  d.params[0].value = "4 cm";
  const auto deeper = evaluator.evaluate(source(d)).value();
  solid(deeper, 480, 984, {3, 10.0 / 3, 20}, 3);
  CHECK(deeper.kernel_calls == 5);
  CHECK(deeper.cache_hits == 1);
  solid(first, 480, 992, {3, 0, 15}, 4);
}

TEST_CASE("circular copies use current seed state and normalize arbitrary placed axes",
          "[circular-pattern]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto d = patterned();
  d.params[4].value = 2.0;
  d.params[3].value = "180 deg";
  d.features[2].inputs = set(d.features[2].inputs, "axis",
                             ir::json::parse(R"({"origin":[0,5,0],"direction":[3,0,0]})").value());
  solid(evaluator.evaluate(source(d)).value(), 240, 496, {13, 5, 0}, 2);
  d.features.insert(
      d.features.begin() + 2,
      feature("move_seed", "transform", R"({"bodies":["plate"],"translation":[7,0,0]})"));
  const auto moved = evaluator.evaluate(source(d)).value();
  solid(moved, 240, 496, {20, 5, 0}, 2);
  CHECK(moved.kernel_calls == 4);
  CHECK(moved.cache_hits == 2);
}

TEST_CASE("circular sketch axes track independent edits direction and unit equivalent placement",
          "[circular-pattern]") {
  auto d = patterned();
  d.params[3].value = "180 deg";
  d.params[4].value = 3.0;
  d.params.push_back({"axis_x", "5 mm", {}, {}, {}, {}});
  d.features[2].inputs = set(d.features[2].inputs, "axis",
                             ir::json::parse(R"({"sketch":"centerline","entity":"axis"})").value());
  d.features.insert(d.features.begin() + 2,
                    feature("centerline", "sketch", R"({"plane":{"datum":"zx"},"entities":[
    {"id":"axis","type":"line","start":[0,"axis_x"],"end":[10,"axis_x"]}],"constraints":[]})"));
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  const auto first = evaluator.evaluate(source(d)).value();
  solid(first, 360, 744, {8, 5.0 / 3, 15}, 3);
  CHECK(first.kernel_calls == 5);
  d.params[5].value = "2 mm";
  const auto shifted = evaluator.evaluate(source(d)).value();
  solid(shifted, 360, 744, {5, 8.0 / 3, 15}, 3);
  CHECK(shifted.kernel_calls == 4);
  CHECK(shifted.cache_hits == 2);
  CHECK(shifted.features[3].hash != first.features[3].hash);
  CHECK(shifted.features[1].hash == first.features[1].hash);
  d.params[5].value = "0.2 cm";
  CHECK(evaluator.evaluate(source(d))->cache_hits == 5);
  d.features[2].inputs = ir::json::parse(R"({"plane":{"datum":"zx"},"entities":[
    {"id":"axis","type":"line","start":[10,"axis_x"],"end":[0,"axis_x"]}],"constraints":[]})")
                             .value();
  const auto reversed = evaluator.evaluate(source(d)).value();
  solid(reversed, 360, 744, {5, -8.0 / 3, 15}, 3);
  CHECK(reversed.kernel_calls == 4);
  d.features[2].suppressed = true;
  const auto unavailable = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(unavailable.ok());
  CHECK(unavailable.features[3].errors.front().code == "input_suppressed");
  CHECK(unavailable.features[3].errors.front().pointer == "/features/3/inputs/axis/sketch");
  CHECK_FALSE(unavailable.bodies.at("plate").shape);
  d.features[2].suppressed = false;
  d.features[2].inputs = ir::json::parse(R"({"plane":{"datum":"zx"},"entities":[
    {"id":"axis","type":"line","start":[0,0],"end":[0,0]}],"constraints":[]})")
                             .value();
  const auto failed = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(failed.ok());
  CHECK(failed.features[3].errors.front().code == "input_failed");
  CHECK(failed.features[3].errors.front().pointer == "/features/3/inputs/axis/sketch");
}

TEST_CASE("circular sketch axes map explicit frames into world placement", "[circular-pattern]") {
  auto d = patterned();
  d.params[3].value = "180 deg";
  d.params[4].value = 2.0;
  d.features[2].inputs = set(d.features[2].inputs, "axis",
                             ir::json::parse(R"({"sketch":"centerline","entity":"axis"})").value());
  d.features.insert(d.features.begin() + 2,
                    feature("centerline", "sketch", R"({"plane":{"origin":[0,5,0],
    "normal":[0,1,0],"x_direction":[1,0,0]},"entities":[
    {"id":"axis","type":"line","start":[0,0],"end":[3,0]}],"constraints":[]})"));
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  solid(evaluator.evaluate(source(d)).value(), 240, 496, {13, 5, 0}, 2);
}

TEST_CASE("circular arrays union touching and coincident copies without multiplying mass",
          "[circular-pattern]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto d = patterned();
  d.features[0] = feature("outline", "sketch", R"({"plane":{"datum":"xy"},"entities":[
    {"id":"r","type":"rect","corner":[0,0],"size":[2,2]}],"constraints":[]})");
  const auto touching = evaluator.evaluate(source(d)).value();
  solid(touching, 480, 512, {3, 0, 15}, 1);
  names(touching);
  CHECK(touching.bodies.at("plate").shape->measures().faces == 6);
  d.features[0] = feature("outline", "sketch", R"({"plane":{"datum":"xy"},"entities":[
    {"id":"r","type":"rect","corner":[-1,-1],"size":[2,2]}],"constraints":[]})");
  const auto coincident = evaluator.evaluate(source(d)).value();
  solid(coincident, 120, 248, {3, 0, 15}, 1);
  names(coincident);
}

TEST_CASE("circular body arrays preserve periodic faces and through holes", "[circular-pattern]") {
  auto d = patterned();
  d.features[0] = feature("outline", "sketch", R"({"plane":{"datum":"xy"},"entities":[
    {"id":"outer","type":"circle","center":[10,0],"radius":2},
    {"id":"inner","type":"circle","center":[10,0],"radius":1}],"constraints":[]})");
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  constexpr double pi = std::numbers::pi;
  const auto result = evaluator.evaluate(source(d)).value();
  solid(result, 360 * pi, 744 * pi, {3, 0, 15}, 4);
  names(result);
  CHECK(result.bodies.at("plate").shape->measures().faces == 16);
  const auto cached = evaluator.evaluate(source(d)).value();
  solid(cached, 360 * pi, 744 * pi, {3, 0, 15}, 4);
  names(cached);
  CHECK(cached.kernel_calls == 0);
}

TEST_CASE("single circular instances allow zero sweep and preserve seed provenance",
          "[circular-pattern]") {
  auto d = patterned();
  d.params[3].value = "0 deg";
  d.params[4].value = 1.0;
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  const auto result = evaluator.evaluate(source(d)).value();
  solid(result, 120, 248, {13, 0, 15}, 1);
  CHECK(result.kernel_calls == 3);
  for (const auto& name : result.bodies.at("plate").names) CHECK(name.feature == "base");
}

TEST_CASE("circular patterns validate targets count angle and axes before copying",
          "[circular-pattern]") {
  struct Invalid {
    const char* input;
    const char* pointer;
  };
  auto kernel = kernel::make_occt_kernel().value();
  for (
      const auto& [input, pointer] :
      {Invalid{
           R"({"count":65,"angle":360,"axis":{"origin":[0,0,0],"direction":"z"},"bodies":["plate"]})",
           "/count"},
       Invalid{
           R"({"count":"2.5","angle":360,"axis":{"origin":[0,0,0],"direction":"z"},"bodies":["plate"]})",
           "/count"},
       Invalid{
           R"({"count":4,"angle":0,"axis":{"origin":[0,0,0],"direction":"z"},"bodies":["plate"]})",
           "/angle"},
       Invalid{
           R"({"count":4,"angle":"0.00000001 deg","axis":{"origin":[0,0,0],"direction":"z"},"bodies":["plate"]})",
           "/angle"},
       Invalid{
           R"({"count":4,"angle":361,"axis":{"origin":[0,0,0],"direction":"z"},"bodies":["plate"]})",
           "/angle"},
       Invalid{
           R"({"count":4,"angle":-361,"axis":{"origin":[0,0,0],"direction":"z"},"bodies":["plate"]})",
           "/angle"},
       Invalid{
           R"({"count":4,"angle":360,"axis":{"origin":[0,0,0],"direction":[0,0,0]},"bodies":["plate"]})",
           "/axis/direction"},
       Invalid{
           R"({"count":4,"angle":360,"axis":{"origin":[0,0,0],"direction":"z"},"bodies":["plate","plate"]})",
           "/bodies/1"},
       Invalid{
           R"({"count":4,"angle":360,"axis":{"origin":[0,0,0],"direction":"z"},"features":["base"]})",
           "/features"},
       Invalid{
           R"({"count":4,"angle":360,"axis":{"created_by":"base","expect":"one"},"bodies":["plate"]})",
           "/axis"}}) {
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

TEST_CASE("circular work limits include every seed before copying", "[circular-pattern]") {
  auto d = patterned();
  d.features.resize(2);
  Array bodies{"plate"};
  for (int i = 1; i < 33; ++i) {
    const auto id = "body_" + std::to_string(i);
    const auto text = R"({"profile":{"sketch":"outline"},"distance":20,"result":{"new":")" + id +
                      R"(","body_type":"brep"}})";
    d.features.push_back(feature("seed_" + std::to_string(i), "extrude", text.c_str()));
    bodies.push_back(id);
  }
  auto pattern = feature("repeat", "pattern_circular", R"({"bodies":["plate"],"count":2,
    "angle":360,"axis":{"origin":[0,0,0],"direction":"z"}})");
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

TEST_CASE("circular admission accepts the instance budget and bounds angular resolution",
          "[circular-pattern]") {
  for (const auto count : {32.0, 64.0}) {
    auto f = feature("repeat", "pattern_circular", R"({"bodies":["plate"],"count":64,
      "angle":"2*pi * 1 rad","axis":{"origin":[1,2,3],"direction":[0,0,2]}})");
    f.inputs = set(f.inputs, "count", count);
    if (count == 32) f.inputs = set(f.inputs, "bodies", Array{"plate", "other"});
    const auto inputs = ir::FeatureInputs::compile(f).value().evaluate().value();
    const auto admitted = prepare_circular_pattern(inputs);
    REQUIRE(admitted);
    CHECK(admitted->count == static_cast<std::size_t>(count));
    CHECK_THAT(admitted->step.angle, WithinAbs(2 * std::numbers::pi / count, 1e-15));
    CHECK(admitted->step.axis_origin == math::Vec3{1, 2, 3});
    CHECK(admitted->step.axis_direction == math::Vec3{0, 0, 1});
  }
  for (const double sign : {-1.0, 1.0}) {
    auto f = feature("repeat", "pattern_circular", R"({"bodies":["plate"],"count":3,
      "angle":0,"axis":{"origin":[0,0,0],"direction":"z"}})");
    for (const double scale : {0.99, 1.0, 1.01}) {
      f.inputs = set(f.inputs, "angle", sign * scale * 2e-9);
      const auto inputs = ir::FeatureInputs::compile(f).value().evaluate({"mm", "rad", {}}).value();
      const auto admitted = prepare_circular_pattern(inputs);
      CHECK(admitted.has_value() == (scale > 1));
      if (!admitted) CHECK(admitted.error().pointer == "/inputs/angle");
    }
    f.inputs = set(f.inputs, "angle", sign * (2 * std::numbers::pi + 2e-9));
    const auto inputs = ir::FeatureInputs::compile(f).value().evaluate({"mm", "rad", {}}).value();
    const auto rejected = prepare_circular_pattern(inputs);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().pointer == "/inputs/angle");
  }
}

TEST_CASE("circular full turns canonicalize angular tolerance without duplicate endpoints",
          "[circular-pattern]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto d = patterned();
  for (const auto* angle :
       {"-360 deg", "2*pi * 1 rad - 0.0000000005 rad", "2*pi * 1 rad + 0.0000000005 rad",
        "2*pi * 1 rad - 0.000000001 rad", "2*pi * 1 rad + 0.000000001 rad",
        "-2*pi * 1 rad - 0.000000001 rad", "-2*pi * 1 rad + 0.000000001 rad"}) {
    d.params[3].value = angle;
    const auto result = evaluator.evaluate(source(d)).value();
    solid(result, 480, 992, {3, 0, 15}, 4);
    names(result);
    CHECK(result.kernel_calls == (std::string_view(angle) == "-360 deg" ? 6 : 5));
  }
}

TEST_CASE("circular arrays propagate nested pattern names from multi solid current seeds",
          "[circular-pattern]") {
  auto d = patterned();
  d.features.insert(d.features.begin() + 2,
                    feature("radial_seed", "pattern_linear", R"({"bodies":["plate"],"count":2,
    "spacing":4,"direction":"x"})"));
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  const auto first = evaluator.evaluate(source(d)).value();
  solid(first, 960, 1984, {3, 0, 15}, 8);
  CHECK(first.kernel_calls == 8);
  const auto cached = evaluator.evaluate(source(d)).value();
  solid(cached, 960, 1984, {3, 0, 15}, 8);
  CHECK(cached.kernel_calls == 0);
  CHECK(cached.cache_hits == 5);
  for (const auto* id : {"radial_seed", "repeat"})
    CHECK(std::ranges::any_of(cached.bodies.at("plate").names, [&](const auto& n) {
      return n.feature == id && n.role == "instance";
    }));
  d.params[4].value = 3.0;
  const auto edited = evaluator.evaluate(source(d)).value();
  solid(edited, 720, 1488, {3, 0, 15}, 6);
  CHECK(edited.kernel_calls == 4);
  CHECK(edited.cache_hits == 3);
  solid(first, 960, 1984, {3, 0, 15}, 8);
}

TEST_CASE("circular cache roles rebind IDs and reject stale snapshot history",
          "[circular-pattern]") {
  auto d = patterned();
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  const auto first = evaluator.evaluate(source(d)).value();
  solid(first, 480, 992, {3, 0, 15}, 4);
  d.features[1].id = "renamed_base";
  d.features[2].id = "renamed_repeat";
  const auto renamed = evaluator.evaluate(source(d)).value();
  solid(renamed, 480, 992, {3, 0, 15}, 4);
  names(renamed, "renamed_base", "renamed_repeat");
  CHECK(renamed.kernel_calls == 0);
  CHECK(renamed.cache_hits == 4);
  d.history_end = "outline";
  REQUIRE(evaluator.evaluate(source(d))->ok());
  evaluator.set_cache_limits({3, 64 * 1024 * 1024});
  evaluator.set_cache_limits({128, 64 * 1024 * 1024});
  d.history_end.reset();
  const auto rebuilt = evaluator.evaluate(source(d)).value();
  solid(rebuilt, 480, 992, {3, 0, 15}, 4);
  names(rebuilt, "renamed_base", "renamed_repeat");
  CHECK(rebuilt.kernel_calls == 6);
  CHECK(rebuilt.cache_hits == 1);
  CHECK(rebuilt.binding_misses == 2);
  solid(first, 480, 992, {3, 0, 15}, 4);
}

TEST_CASE("circular patterns respect suppression rollback and unavailable seeds",
          "[circular-pattern]") {
  auto d = patterned();
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  d.history_end = "base";
  const auto rolled = evaluator.evaluate(source(d)).value();
  solid(rolled, 120, 248, {10, 0, 15}, 1);
  CHECK(rolled.features[2].status == FeatureStatus::kRolledBack);
  d.history_end = "repeat";
  solid(evaluator.evaluate(source(d)).value(), 480, 992, {0, 0, 15}, 4);
  d.history_end.reset();
  d.features[2].suppressed = true;
  solid(evaluator.evaluate(source(d)).value(), 120, 248, {13, 0, 15}, 1);
  d.features[2].suppressed = false;
  d.features[1].suppressed = true;
  const auto unavailable = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(unavailable.ok());
  CHECK(unavailable.features[2].errors.front().code == "input_suppressed");
  CHECK(unavailable.features[2].errors.front().pointer == "/features/2/inputs/bodies/0");
  CHECK_FALSE(unavailable.bodies.at("plate").shape);
}

TEST_CASE("circular multi body failures publish no partial arrays and recover on retry",
          "[circular-pattern]") {
  auto real = kernel::make_occt_kernel().value();
  for (const bool fail_copy : {false, true}) {
    FaultKernel kernel(*real);
    kernel.fail_transform = fail_copy ? 4 : 0;
    kernel.fail_union = fail_copy ? 0 : 2;
    Evaluator evaluator(kernel, {128, 64 * 1024 * 1024});
    auto d = patterned();
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
    CHECK(failed.kernel_calls == (fail_copy ? 8 : 11));
    kernel.fail_transform = kernel.fail_union = 0;
    kernel.transforms = kernel.unions = 0;
    const auto retried = evaluator.evaluate(source(d)).value();
    solid(retried, 480, 992, {3, 0, 15}, 4);
    solid(retried, 320, 672, {0, 0, 10}, 4, "other");
    names(retried);
    names(retried, "other_base", "repeat", "other");
    CHECK(retried.kernel_calls == 9);
    CHECK(retried.cache_hits == 4);
  }
}

TEST_CASE("circular patterns reject incomplete copy and union naming without caching them",
          "[circular-pattern]") {
  auto real = kernel::make_occt_kernel().value();
  for (const bool copy : {false, true}) {
    FaultKernel kernel(*real);
    kernel.lose_copy_history = copy;
    kernel.lose_union_history = !copy;
    Evaluator evaluator(kernel, {128, 64 * 1024 * 1024});
    const auto d = patterned();
    const auto failed = evaluator.evaluate(source(d)).value();
    REQUIRE_FALSE(failed.ok());
    CHECK(failed.features[2].errors.front().message == "incomplete naming history");
    CHECK_FALSE(failed.bodies.at("plate").shape);
    CHECK(evaluator.cache_usage().entries == 2);
    kernel.lose_copy_history = kernel.lose_union_history = false;
    const auto retried = evaluator.evaluate(source(d)).value();
    solid(retried, 480, 992, {3, 0, 15}, 4);
    names(retried);
    CHECK(retried.kernel_calls == 5);
    CHECK(retried.cache_hits == 2);
  }
}

TEST_CASE("circular snapshots survive cache eviction and kernel teardown", "[circular-pattern]") {
  Evaluation retained;
  {
    auto kernel = kernel::make_occt_kernel().value();
    Evaluator evaluator(*kernel, {1, 64 * 1024 * 1024});
    retained = evaluator.evaluate(source(patterned())).value();
    const auto rebuilt = evaluator.evaluate(source(patterned())).value();
    solid(rebuilt, 480, 992, {3, 0, 15}, 4);
    names(rebuilt);
    CHECK(rebuilt.kernel_calls == 6);
    CHECK(rebuilt.cache_hits == 0);
    evaluator.clear_cache();
    CHECK(evaluator.cache_usage().estimated_bytes == 0);
  }
  solid(retained, 480, 992, {3, 0, 15}, 4);
  names(retained);
}
