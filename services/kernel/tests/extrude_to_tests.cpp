// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "evaluator.hpp"
#include "model_inputs.hpp"
#include "part_fixture.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
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
    if (member.key == key) {
      member.value = std::move(value);
      return members;
    }
  members.push_back({std::string(key), std::move(value)});
  return members;
}
ir::Document stopped() {
  auto d = part(2);
  d.params.push_back({"stop_height", "40 mm", {}, {}, {}, {}});
  d.features.insert(
      d.features.begin(),
      {feature("support", "sketch", R"({"plane":{"datum":"xy"},"entities":[
      {"id":"r","type":"rect","corner":[100,100],"size":[2,2]}],"constraints":[]})"),
       feature("stop", "extrude", R"({"profile":{"sketch":"support"},"distance":"stop_height",
      "result":{"new":"stop_body","body_type":"brep"}})")});
  d.features.back() = feature("base", "extrude", R"({"profile":{"sketch":"outline"},
    "to":{"created_by":"stop","role":"cap_end","expect":"one"},
    "result":{"new":"plate","body_type":"brep"}})");
  return d;
}
void selector(ir::Document& d, const char* text) {
  auto& feature = d.features.back();
  feature.inputs = set(feature.inputs, "to", ir::json::parse(text).value());
}
void solid(const Evaluation& result, double volume, double area, math::Vec3 center,
           const std::string& id = "plate") {
  for (const auto& f : result.features)
    for (const auto& e : f.errors) UNSCOPED_INFO(e.pointer << ": " << e.message);
  REQUIRE(result.ok());
  const auto& body = result.bodies.at(id);
  REQUIRE(body.shape);
  const auto& m = body.shape->measures();
  CHECK_THAT(m.volume, WithinAbs(volume, 1e-6));
  CHECK_THAT(m.area, WithinAbs(area, 1e-6));
  CHECK_THAT(m.centroid.x, WithinAbs(center.x, 1e-8));
  CHECK_THAT(m.centroid.y, WithinAbs(center.y, 1e-8));
  CHECK_THAT(m.centroid.z, WithinAbs(center.z, 1e-8));
  CHECK(m.solids == 1);
  for (const auto& name : body.names) CHECK(name.entity->owner() == body.shape);
}
class QueryKernel final : public kernel::IKernel {
 public:
  kernel::IKernel& real;
  int fault = 0, queries = 0;
  explicit QueryKernel(kernel::IKernel& backend) : real(backend) {}
  void damage(kernel::Plane& plane) const {
    if (fault == 2) plane.normal = {0, 0, 2};
    if (fault == 3) plane.x_axis = plane.normal;
    if (fault == 4) plane.origin.z = std::numeric_limits<double>::quiet_NaN();
    if (fault == 5) plane.x_axis.x = std::numeric_limits<double>::infinity();
  }
  std::string_view identity() const noexcept override { return real.identity(); }
  Capabilities capabilities() const noexcept override { return real.capabilities(); }
  std::expected<kernel::OperationResult, kernel::KernelError> loft(
      const kernel::Loft& input) override {
    return real.loft(input);
  }
  std::expected<kernel::Mesh, kernel::KernelError> tessellate(
      const kernel::ShapePtr& shape, const kernel::MeshingOptions& options) override {
    return real.tessellate(shape, options);
  }
  std::expected<std::vector<kernel::EntityPtr>, kernel::KernelError> edges(
      const kernel::EntityPtr& face) override {
    return real.edges(face);
  }
  std::expected<kernel::OperationResult, kernel::KernelError> fillet(
      const kernel::ShapePtr& shape, const std::vector<kernel::EntityPtr>& edges,
      double radius) override {
    return real.fillet(shape, edges, radius);
  }
  std::expected<kernel::OperationResult, kernel::KernelError> chamfer(
      const kernel::ShapePtr& shape, const std::vector<kernel::EntityPtr>& edges,
      double distance) override {
    return real.chamfer(shape, edges, distance);
  }
  std::expected<std::vector<kernel::FaceInfo>, kernel::KernelError> faces(
      const kernel::ShapePtr& shape) override {
    ++queries;
    if (fault == 1) return std::unexpected(kernel::KernelError{"kernel", "query failed"});
    auto faces = real.faces(shape);
    if (faces)
      for (auto& face : *faces)
        if (face.plane) damage(*face.plane);
    return faces;
  }
  std::expected<kernel::Plane, kernel::KernelError> face_plane(
      const kernel::EntityPtr& face) override {
    ++queries;
    if (fault == 1) return std::unexpected(kernel::KernelError{"kernel", "query failed"});
    auto plane = real.face_plane(face);
    if (plane) damage(*plane);
    return plane;
  }
  std::expected<std::string, kernel::KernelError> export_step(
      const std::vector<kernel::ShapePtr>& bodies) override {
    return real.export_step(bodies);
  }
  std::expected<double, kernel::KernelError> face_distance(const kernel::EntityPtr& face,
                                                           math::Vec3 p) override {
    return real.face_distance(face, p);
  }
  std::expected<kernel::OperationResult, kernel::KernelError> extrude(
      const kernel::Extrusion& input) override {
    return real.extrude(input);
  }
  std::expected<kernel::OperationResult, kernel::KernelError> revolve(
      const kernel::Revolution& input) override {
    return real.revolve(input);
  }
  std::expected<kernel::OperationResult, kernel::KernelError> transform(
      const kernel::ShapePtr& shape, const kernel::RigidTransform& input) override {
    return real.transform(shape, input);
  }
  std::expected<kernel::OperationResult, kernel::KernelError> mirror(
      const kernel::ShapePtr& shape, const kernel::Plane& p) override {
    return real.mirror(shape, p);
  }
  std::expected<kernel::OperationResult, kernel::KernelError> boolean_operation(
      const kernel::ShapePtr& shape, const std::vector<kernel::ShapePtr>& tools,
      kernel::BooleanOp op) override {
    return real.boolean_operation(shape, tools, op);
  }
};
}  // namespace

TEST_CASE("extrusions stop at selected supporting planes beyond finite face trims",
          "[extrude-to]") {
  auto d = stopped();
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  const auto first = evaluator.evaluate(source(d)).value();
  solid(first, 8000, 2800, {5, 10, 20});
  solid(first, 160, 328, {101, 101, 20}, "stop_body");
  CHECK(first.kernel_calls == 3);
  CHECK(first.bodies.size() == 2);
  for (const auto& name : first.bodies.at("plate").names) CHECK(name.feature == "base");
  const auto cached = evaluator.evaluate(source(d)).value();
  solid(cached, 8000, 2800, {5, 10, 20});
  CHECK(cached.cache_hits == 4);
  CHECK(cached.kernel_calls == 1);
  CHECK(cached.bodies.at("stop_body").shape == first.bodies.at("stop_body").shape);
  d.params[3].value = "50 mm";
  const auto edited = evaluator.evaluate(source(d)).value();
  solid(edited, 10000, 3400, {5, 10, 25});
  solid(edited, 200, 408, {101, 101, 25}, "stop_body");
  CHECK(edited.cache_hits == 2);
  CHECK(edited.kernel_calls == 3);
  CHECK(edited.features.back().hash != first.features.back().hash);
}

TEST_CASE("planar termination supports oblique extrusion rays without projecting profiles",
          "[extrude-to]") {
  auto d = stopped();
  d.features.back().inputs =
      set(d.features.back().inputs, "direction", ir::json::parse(R"([1,0,1])").value());
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  solid(evaluator.evaluate(source(d)).value(), 8000, 1200 + 1600 * std::sqrt(2.0), {25, 10, 20});
}

TEST_CASE("termination respects the chosen ray and opposite target normals", "[extrude-to]") {
  auto d = stopped();
  d.params[3].value = "-20 mm";
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  const auto behind = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(behind.ok());
  CHECK(behind.features.back().errors.front().pointer == "/features/3/inputs/to");
  d.features.back().inputs = set(d.features.back().inputs, "direction", "-z");
  solid(evaluator.evaluate(source(d)).value(), 4000, 1600, {5, 10, -10});
}

TEST_CASE("extrusion termination composes current body selectors and nearest filters",
          "[extrude-to]") {
  auto d = stopped();
  selector(d, R"({"any":[{"created_by":"stop","role":"cap_end"},
    {"faces_of":"stop_body","where":{"normal":"z"}}],
    "except":{"created_by":"stop","role":"cap_start"},
    "where":{"near":[101,101,45]},"expect":"one"})");
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  const auto first = evaluator.evaluate(source(d)).value();
  solid(first, 8000, 2800, {5, 10, 20});
  CHECK(first.kernel_calls == 4);
  const auto cached = evaluator.evaluate(source(d)).value();
  solid(cached, 8000, 2800, {5, 10, 20});
  CHECK(cached.kernel_calls == 2);
}

TEST_CASE("selected sketch-entity side planes terminate other world-frame profiles",
          "[extrude-to]") {
  auto d = stopped();
  d.features[2].inputs =
      set(d.features[2].inputs, "plane", ir::json::parse(R"({"datum":"yz"})").value());
  selector(d, R"({"created_by":"stop","role":"side","from":"support/r",
    "where":{"normal":"x"},"expect":"one"})");
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  solid(evaluator.evaluate(source(d)).value(), 20400, 6520, {51, 5, 10});
}

TEST_CASE("planar termination follows current source translations", "[extrude-to]") {
  auto d = stopped();
  d.features.insert(
      d.features.begin() + 2,
      feature("move_stop", "transform", R"({"bodies":["stop_body"],"translation":[0,0,"step"]})"));
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  solid(evaluator.evaluate(source(d)).value(), 8600, 2980, {5, 10, 21.5});
  d.params[1].value = "7 mm";
  solid(evaluator.evaluate(source(d)).value(), 9400, 3220, {5, 10, 23.5});
}

TEST_CASE("termination rejects coincident and ambiguous faces at the to pointer", "[extrude-to]") {
  auto kernel = kernel::make_occt_kernel().value();
  for (const auto* text : {R"({"created_by":"stop","role":"cap_start","expect":"one"})",
                           R"({"created_by":"stop","expect":"one"})",
                           R"({"created_by":"stop","role":"missing","expect":"any"})"}) {
    auto d = stopped();
    selector(d, text);
    Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
    const auto failed = evaluator.evaluate(source(d)).value();
    REQUIRE_FALSE(failed.ok());
    CHECK(failed.features.back().errors.front().pointer == "/features/3/inputs/to");
    CHECK_FALSE(failed.bodies.at("plate").shape);
    CHECK(failed.bodies.at("stop_body").shape);
  }
}

TEST_CASE("selected-plane extrusion preserves exact curved profile holes", "[extrude-to]") {
  auto d = stopped();
  d.features[2] = feature("outline", "sketch", R"({"plane":{"datum":"xy"},"entities":[
    {"id":"outer","type":"circle","center":[0,0],"radius":10},
    {"id":"inner","type":"circle","center":[0,0],"radius":5}],"constraints":[]})");
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  solid(evaluator.evaluate(source(d)).value(), 3000 * kPi, 1350 * kPi, {0, 0, 20});
}

TEST_CASE("selected termination planes preserve join cut and intersect result histories",
          "[extrude-to]") {
  auto kernel = kernel::make_occt_kernel().value();
  for (const std::string mode : {"join", "cut", "intersect"}) {
    auto d = stopped();
    d.features[3] = feature("base", "extrude", R"({"profile":{"sketch":"outline"},
      "distance":"depth","result":{"new":"plate","body_type":"brep"}})");
    d.features.push_back(feature("tool_profile", "sketch", R"({"plane":{"datum":"xy"},
      "entities":[{"id":"r","type":"rect","corner":[5,0],"size":[10,20]}],"constraints":[]})"));
    const auto text = std::string(R"({"profile":{"sketch":"tool_profile"},
      "to":{"created_by":"stop","role":"cap_end","expect":"one"},"result":{")") +
                      mode + R"(":"plate"}})";
    d.features.push_back(feature("modify", "extrude", text.c_str()));
    Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
    const auto first = evaluator.evaluate(source(d)).value();
    if (mode == "join")
      solid(first, 11000, 3300, {87500.0 / 11000, 10, 205000.0 / 11000});
    else
      solid(first, 3000, 1700, {mode == "cut" ? 2.5 : 7.5, 10, 15});
    solid(first, 160, 328, {101, 101, 20}, "stop_body");
    CHECK(first.kernel_calls == 5);
    for (const auto* producer : {"base", "modify"})
      CHECK(std::ranges::any_of(first.bodies.at("plate").names,
                                [&](const auto& name) { return name.feature == producer; }));
    const auto cached = evaluator.evaluate(source(d)).value();
    REQUIRE(cached.ok());
    CHECK(cached.cache_hits == 6);
    CHECK(cached.kernel_calls == 1);
    CHECK(cached.bodies.at("plate").shape == first.bodies.at("plate").shape);
  }
}

TEST_CASE("a result target can also supply its own termination plane", "[extrude-to]") {
  auto d = part(2);
  d.features.push_back(feature("tool_profile", "sketch", R"({"plane":{"datum":"xy"},
    "entities":[{"id":"c","type":"circle","center":[5,10],"radius":1}],"constraints":[]})"));
  d.features.push_back(feature("modify", "extrude", R"({"profile":{"sketch":"tool_profile"},
    "to":{"created_by":"base","role":"cap_end","expect":"one"},"result":{"cut":"plate"}})"));
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  solid(evaluator.evaluate(source(d)).value(), 6000 - 30 * kPi, 2200 + 58 * kPi, {5, 10, 15});
  solid(evaluator.evaluate(source(d)).value(), 6000 - 30 * kPi, 2200 + 58 * kPi, {5, 10, 15});
  d.params[0].value = "40 mm";
  solid(evaluator.evaluate(source(d)).value(), 8000 - 40 * kPi, 2800 + 78 * kPi, {5, 10, 20});
}

TEST_CASE("unsupported termination modes and in-plane rays report exact fields", "[extrude-to]") {
  struct Invalid {
    const char* key;
    const char* value;
    const char* pointer;
  };
  auto kernel = kernel::make_occt_kernel().value();
  for (const auto& [key, value, pointer] :
       {Invalid{"symmetric", "true", "/symmetric"}, Invalid{"draft", "1", "/draft"},
        Invalid{"direction", R"("x")", "/direction"},
        Invalid{"direction", R"([1,0,0.0000000001])", "/direction"},
        Invalid{"direction", R"([0,0,0])", "/direction"}}) {
    auto d = stopped();
    d.features.back().inputs = set(d.features.back().inputs, key, ir::json::parse(value).value());
    Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
    const auto failed = evaluator.evaluate(source(d)).value();
    REQUIRE_FALSE(failed.ok());
    CHECK(failed.features.back().errors.front().pointer ==
          "/features/3/inputs" + std::string(pointer));
    CHECK(failed.features.back().errors.front().code == "kernel");
    CHECK_FALSE(failed.bodies.at("plate").shape);
    CHECK(failed.bodies.at("stop_body").shape);
  }
}

TEST_CASE("tilted and near-coincident termination planes fail before solid publication",
          "[extrude-to]") {
  auto kernel = kernel::make_occt_kernel().value();
  for (const bool tilt : {true, false}) {
    auto d = stopped();
    d.features.insert(
        d.features.begin() + 2,
        tilt ? feature("place_stop", "transform", R"({"bodies":["stop_body"],"rotation":{
        "axis":{"origin":[0,0,0],"direction":"y"},"angle":30}})")
             : feature("place_stop", "transform", R"({"bodies":["stop_body"],
          "translation":[0,0,-39.9999995]})"));
    Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
    const auto failed = evaluator.evaluate(source(d)).value();
    REQUIRE_FALSE(failed.ok());
    CHECK(failed.features.back().errors.front().pointer == "/features/4/inputs/to");
    CHECK(failed.features.back().errors.front().code == "kernel");
    CHECK_FALSE(failed.bodies.at("plate").shape);
    CHECK(failed.bodies.at("stop_body").shape);
  }
}

TEST_CASE("profile control hulls bound the effect of angular termination tolerance",
          "[extrude-to]") {
  auto d = stopped();
  d.features.insert(d.features.begin() + 2, feature("tilt_stop", "transform", R"({
    "bodies":["stop_body"],"rotation":{"axis":{"origin":[0,0,0],"direction":"y"},
      "angle":"0.00000004 deg"}})"));
  d.features[3].inputs = set(d.features[3].inputs, "entities",
                             ir::json::parse(R"([
    {"id":"r","type":"rect","corner":[10000,0],"size":[10,20]}])")
                                 .value());
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  const auto failed = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(failed.ok());
  CHECK(failed.features.back().errors.front().pointer == "/features/4/inputs/to");
  CHECK(failed.features.back().errors.front().message.find("full profile") != std::string::npos);
  CHECK_FALSE(failed.bodies.at("plate").shape);
}

TEST_CASE("selected-plane frames are validated before cached termination reuse", "[extrude-to]") {
  auto real = kernel::make_occt_kernel().value();
  for (const bool enumerate : {false, true})
    for (const int fault : {1, 2, 3, 4, 5}) {
      auto d = stopped();
      if (enumerate)
        selector(d, R"({"faces_of":"stop_body","where":{"normal":"z"},"expect":"one"})");
      QueryKernel kernel(*real);
      Evaluator evaluator(kernel, {128, 64 * 1024 * 1024});
      const auto first = evaluator.evaluate(source(d)).value();
      solid(first, 8000, 2800, {5, 10, 20});
      kernel.fault = fault;
      const auto failed = evaluator.evaluate(source(d)).value();
      REQUIRE_FALSE(failed.ok());
      CHECK(failed.features.back().errors.front().pointer == "/features/3/inputs/to");
      CHECK(failed.features.back().errors.front().code == "kernel");
      CHECK_FALSE(failed.bodies.at("plate").shape);
      CHECK(failed.bodies.at("stop_body").shape == first.bodies.at("stop_body").shape);
      kernel.fault = 0;
      const auto retry = evaluator.evaluate(source(d)).value();
      solid(retry, 8000, 2800, {5, 10, 20});
      CHECK(retry.cache_hits == 4);
      CHECK(retry.bodies.at("plate").shape == first.bodies.at("plate").shape);
      CHECK(kernel.queries == 3);
    }
}

TEST_CASE("termination follows consumed source aliases and refuses retained-face ambiguity",
          "[extrude-to]") {
  auto d = stopped();
  d.features.insert(
      d.features.begin() + 2,
      {feature("replacement", "extrude", R"({"profile":{"sketch":"support"},"distance":30,
      "result":{"new":"replacement_body","body_type":"brep"}})"),
       feature("merge", "boolean", R"({"target":"replacement_body","tools":["stop_body"],
      "op":"union","keep_tools":false})")});
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  const auto first = evaluator.evaluate(source(d)).value();
  solid(first, 8000, 2800, {5, 10, 20});
  solid(first, 160, 328, {101, 101, 20}, "replacement_body");
  CHECK_FALSE(first.bodies.at("stop_body").shape);
  d.features[3].inputs = set(d.features[3].inputs, "keep_tools", true);
  const auto ambiguous = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(ambiguous.ok());
  CHECK(ambiguous.features.back().errors.front().code == "selector_count");
  CHECK(ambiguous.features.back().errors.front().pointer == "/features/5/inputs/to");
  CHECK_FALSE(ambiguous.bodies.at("plate").shape);
  CHECK(ambiguous.bodies.at("stop_body").shape);
}

TEST_CASE("unavailable termination sources do not stop independent branches", "[extrude-to]") {
  auto kernel = kernel::make_occt_kernel().value();
  for (const bool suppress : {true, false}) {
    auto d = stopped();
    if (suppress)
      d.features[1].suppressed = true;
    else
      d.params[3].value = "0 mm";
    d.features.push_back(feature("independent", "extrude", R"({"profile":{"sketch":"outline"},
      "distance":2,"result":{"new":"spare","body_type":"brep"}})"));
    Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
    const auto failed = evaluator.evaluate(source(d)).value();
    REQUIRE_FALSE(failed.ok());
    CHECK(failed.features[3].errors.front().code ==
          (suppress ? "input_suppressed" : "input_failed"));
    CHECK(failed.features[3].errors.front().pointer == "/features/3/inputs/to/created_by");
    CHECK_FALSE(failed.bodies.at("plate").shape);
    REQUIRE(failed.bodies.at("spare").shape);
    CHECK_THAT(failed.bodies.at("spare").shape->measures().volume, WithinAbs(400, 1e-6));
  }
  auto d = stopped();
  d.history_end = "stop";
  const auto rolled = Evaluator(*kernel, {128, 64 * 1024 * 1024}).evaluate(source(d)).value();
  REQUIRE(rolled.ok());
  CHECK(rolled.features[3].status == FeatureStatus::kRolledBack);
  CHECK(rolled.kernel_calls == 1);
}

TEST_CASE("cached termination geometry rebinds renamed source and output producers",
          "[extrude-to]") {
  auto d = stopped();
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  const auto first = evaluator.evaluate(source(d)).value();
  solid(first, 8000, 2800, {5, 10, 20});
  d.features[1].id = "new_stop";
  selector(d, R"({"created_by":"new_stop","role":"cap_end","expect":"one"})");
  const auto source_renamed = evaluator.evaluate(source(d)).value();
  solid(source_renamed, 8000, 2800, {5, 10, 20});
  CHECK(source_renamed.bodies.at("stop_body").shape == first.bodies.at("stop_body").shape);
  CHECK(source_renamed.features[1].status == FeatureStatus::kCached);
  d.features[3].id = "new_base";
  const auto rebound = evaluator.evaluate(source(d)).value();
  solid(rebound, 8000, 2800, {5, 10, 20});
  CHECK(rebound.bodies.at("plate").shape == source_renamed.bodies.at("plate").shape);
  CHECK(rebound.cache_hits == 4);
  for (const auto& name : rebound.bodies.at("plate").names) CHECK(name.feature == "new_base");
}

TEST_CASE("termination sources are charged to bounded caches and survive eviction",
          "[extrude-to]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator to(*kernel, {128, 64 * 1024 * 1024}), distance(*kernel, {128, 64 * 1024 * 1024});
  auto d = stopped();
  const auto first = to.evaluate(source(d)).value();
  solid(first, 8000, 2800, {5, 10, 20});
  d.features[3] = feature("base", "extrude", R"({"profile":{"sketch":"outline"},"distance":40,
    "result":{"new":"plate","body_type":"brep"}})");
  solid(distance.evaluate(source(d)).value(), 8000, 2800, {5, 10, 20});
  CHECK(to.cache_usage().estimated_bytes >=
        distance.cache_usage().estimated_bytes +
            first.bodies.at("stop_body").shape->estimated_bytes());
  to.set_cache_limits({0, 0});
  solid(to.evaluate(source(stopped())).value(), 8000, 2800, {5, 10, 20});
  CHECK(to.cache_usage().entries == 0);
  CHECK(to.cache_usage().estimated_bytes == 0);
  solid(first, 8000, 2800, {5, 10, 20});
}

TEST_CASE("termination measures from the actual sketch frame and checks finite ray lengths",
          "[extrude-to]") {
  auto d = stopped();
  d.features[2].inputs =
      set(d.features[2].inputs, "plane",
          ir::json::parse(R"({"origin":[0,0,7],"normal":"z","x_direction":"x"})").value());
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  solid(evaluator.evaluate(source(d)).value(), 6600, 2380, {5, 10, 23.5});
  d = stopped();
  d.features[3].inputs =
      set(d.features[3].inputs, "direction", ir::json::parse(R"([1,0,0.00000001])").value());
  const auto sketch =
      prepare_sketch(ir::FeatureInputs::compile(d.features[2]).value().evaluate().value()).value();
  const auto inputs = ir::FeatureInputs::compile(d.features[3]).value().evaluate().value();
  const kernel::Plane huge{{0, 0, 1e308}, {1, 0, 0}, {0, 0, 1}};
  const auto overflow = prepare_extrusion(inputs, sketch, nullptr, &huge);
  REQUIRE_FALSE(overflow);
  CHECK(overflow.error().pointer == "/inputs/to");
}

TEST_CASE("curved target faces cannot stand in for termination planes", "[extrude-to]") {
  auto d = stopped();
  d.features[0].inputs = set(d.features[0].inputs, "entities",
                             ir::json::parse(R"([
    {"id":"c","type":"circle","center":[100,100],"radius":2}])")
                                 .value());
  selector(d, R"({"created_by":"stop","role":"side","where":{
    "type":"cylinder","near":[101.5,101.5,20]},"expect":"one"})");
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  const auto failed = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(failed.ok());
  CHECK(failed.features.back().errors.front().code == "kernel");
  CHECK(failed.features.back().errors.front().pointer == "/features/3/inputs/to");
  CHECK(failed.features.back().errors.front().message.find("not planar") != std::string::npos);
  CHECK_FALSE(failed.bodies.at("plate").shape);
}

TEST_CASE("termination selector errors preserve nested field and cardinality pointers",
          "[extrude-to]") {
  struct Invalid {
    const char* text;
    const char* pointer;
    const char* code;
  };
  auto kernel = kernel::make_occt_kernel().value();
  for (const auto& [text, pointer, code] :
       {Invalid{R"({"any":[{"created_by":"stop","role":"missing","expect":"one"}]})", "/any/0",
                "selector_count"},
        Invalid{R"({"created_by":"stop","role":"cap_end","where":{"type":"cone"}})", "/where/type",
                "kernel"},
        Invalid{R"({"created_by":"stop","role":"cap_end","except":{
        "edges_of":{"created_by":"stop"}}})",
                "/except/edges_of", "kernel"}}) {
    auto d = stopped();
    selector(d, text);
    Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
    const auto failed = evaluator.evaluate(source(d)).value();
    REQUIRE_FALSE(failed.ok());
    CHECK(failed.features.back().errors.front().code == code);
    CHECK(failed.features.back().errors.front().pointer ==
          "/features/3/inputs/to" + std::string(pointer));
    CHECK_FALSE(failed.bodies.at("plate").shape);
  }
}

TEST_CASE("a tolerance-level target tilt cannot admit an almost in-plane source ray",
          "[extrude-to][extrude-to-guard]") {
  auto d = stopped();
  d.features[3].inputs =
      set(d.features[3].inputs, "direction", ir::json::parse(R"([1,0,0.0000000000001])").value());
  const auto sketch =
      prepare_sketch(ir::FeatureInputs::compile(d.features[2]).value().evaluate().value()).value();
  const auto inputs = ir::FeatureInputs::compile(d.features[3]).value().evaluate().value();
  const kernel::Plane tilted{{0, 0, 40}, {1, 0, -9.9995e-10}, {9.9995e-10, 0, 1}};
  const auto rejected = prepare_extrusion(inputs, sketch, nullptr, &tilted);
  REQUIRE_FALSE(rejected);
  CHECK(rejected.error().pointer == "/inputs/direction");
}
