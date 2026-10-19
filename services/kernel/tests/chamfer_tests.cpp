// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "evaluator.hpp"
#include "part_fixture.hpp"

#include <algorithm>
#include <cmath>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace nf;
using namespace nf::kernel_service;
using namespace nf::kernel_service::test;
using Catch::Matchers::WithinAbs;

namespace {
using ir::json::Value;
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
ir::Document beveled() {
  auto d = part(2);
  d.params.push_back({"bevel", "1 mm", {}, {}, {}, {}});
  d.features.push_back(feature("bevel_top", "chamfer", R"({
    "edges":{"edges_of":{"created_by":"base","role":"cap_end","expect":"one"}},
    "distance":"bevel"})"));
  return d;
}
void selector(ir::Document& d, const char* text) {
  d.features.back().inputs = set(d.features.back().inputs, "edges", ir::json::parse(text).value());
}
void solid(const Evaluation& result, double depth = 30, double bevel = 1, double dx = 0,
           const std::string& id = "plate") {
  for (const auto& f : result.features)
    for (const auto& e : f.errors) UNSCOPED_INFO(e.pointer << ": " << e.message);
  REQUIRE(result.ok());
  const auto& body = result.bodies.at(id);
  REQUIRE(body.shape);
  const auto& m = body.shape->measures();
  const double removed = 30 * bevel * bevel - 4 * bevel * bevel * bevel / 3;
  const double volume = 200 * depth - removed;
  const double area = 400 + 60 * depth - 120 * bevel + 4 * bevel * bevel +
                      (60 * bevel - 4 * bevel * bevel) * std::sqrt(2.);
  const double moment =
      100 * depth * depth - depth * removed + 10 * std::pow(bevel, 3) - std::pow(bevel, 4) / 3;
  CHECK_THAT(m.volume, WithinAbs(volume, 1e-6));
  CHECK_THAT(m.area, WithinAbs(area, 1e-6));
  CHECK_THAT(m.centroid.x, WithinAbs(5 + dx, 1e-8));
  CHECK_THAT(m.centroid.y, WithinAbs(10, 1e-8));
  CHECK_THAT(m.centroid.z, WithinAbs(moment / volume, 1e-8));
  CHECK(m.solids == 1);
  CHECK(m.faces == 10);
  CHECK(m.edges == 20);
  for (const auto& name : body.names) CHECK(name.entity->owner() == body.shape);
}
void failure(const Evaluation& result, const std::string& code, const std::string& pointer) {
  REQUIRE_FALSE(result.ok());
  const auto& f = result.features.back();
  REQUIRE(f.errors.size() == 1);
  CHECK(f.errors.front().code == code);
  CHECK(f.errors.front().pointer == pointer);
  CHECK_FALSE(result.bodies.at("plate").shape);
}
class QueryKernel final : public kernel::IKernel {
 public:
  kernel::IKernel& real;
  int fault = 0, queries = 0;
  kernel::EntityPtr foreign;
  explicit QueryKernel(kernel::IKernel& backend) : real(backend) {}
  std::string_view identity() const noexcept override { return real.identity(); }
  Capabilities capabilities() const noexcept override { return real.capabilities(); }
  std::expected<kernel::OperationResult, kernel::KernelError> loft(
      const kernel::Loft& input) override {
    return real.loft(input);
  }
  std::expected<kernel::OperationResult, kernel::KernelError> sweep(
      const kernel::Sweep& input) override {
    return real.sweep(input);
  }
  std::expected<kernel::Mesh, kernel::KernelError> tessellate(
      const kernel::ShapePtr& shape, const kernel::MeshingOptions& options) override {
    return real.tessellate(shape, options);
  }
  std::expected<std::vector<kernel::EntityPtr>, kernel::KernelError> edges(
      const kernel::EntityPtr& face) override {
    ++queries;
    if (fault == 1) return std::unexpected(kernel::KernelError{"kernel", "boundary failed"});
    auto edges = real.edges(face);
    if (edges && !edges->empty()) {
      if (fault == 2) edges->front().reset();
      if (fault == 3) edges->front() = face;
      if (fault == 4) edges->front() = real.edges(foreign).value().front();
    }
    return edges;
  }
  std::expected<kernel::OperationResult, kernel::KernelError> fillet(
      const kernel::ShapePtr& shape, const std::vector<kernel::EntityPtr>& edges,
      double radius) override {
    auto result = real.fillet(shape, edges, radius);
    if (result) {
      if (fault == 5) result->history.clear();
      if (fault == 6) result->generated.front().entity = edges.front();
    }
    return result;
  }
  std::expected<kernel::OperationResult, kernel::KernelError> chamfer(
      const kernel::ShapePtr& shape, const std::vector<kernel::EntityPtr>& edges,
      double distance) override {
    auto result = real.chamfer(shape, edges, distance);
    if (result) {
      if (fault == 5) result->history.clear();
      if (fault == 6) result->generated.front().entity = edges.front();
    }
    return result;
  }
  std::expected<std::vector<kernel::FaceInfo>, kernel::KernelError> faces(
      const kernel::ShapePtr& shape) override {
    return real.faces(shape);
  }
  std::expected<kernel::Plane, kernel::KernelError> face_plane(
      const kernel::EntityPtr& face) override {
    return real.face_plane(face);
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

TEST_CASE("chamfers bevel selected top boundaries and preserve exact current provenance",
          "[chamfer]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {96, 64 * 1024 * 1024});
  auto d = beveled();
  const auto first = evaluator.evaluate(source(d)).value();
  solid(first);
  CHECK(std::ranges::count_if(first.bodies.at("plate").names, [](const auto& n) {
          return n.feature == "bevel_top" && n.role == "chamfer";
        }) == 4);
  const auto cached = evaluator.evaluate(source(d)).value();
  solid(cached);
  CHECK(cached.cache_hits == 3);
  CHECK(cached.bodies.at("plate").shape == first.bodies.at("plate").shape);
  d.params[3].value = "2 mm";
  solid(evaluator.evaluate(source(d)).value(), 30, 2);
  d.params[0].value = "40 mm";
  solid(evaluator.evaluate(source(d)).value(), 40, 2);
  d.params[3].value = "1 mm";
  d.params[0].value = "30 mm";
  const auto reverted = evaluator.evaluate(source(d)).value();
  solid(reverted);
  CHECK(reverted.cache_hits == 3);
}

TEST_CASE("chamfer edge unions deduplicate edges and exclusions remove whole boundaries",
          "[chamfer]") {
  auto d = beveled();
  selector(d, R"({"any":[
    {"edges_of":{"created_by":"base","role":"cap_end"}},
    {"edges_of":{"created_by":"base","role":"cap_end"}},
    {"edges_of":{"created_by":"base","role":"cap_start"}}],
    "except":{"edges_of":{"created_by":"base","role":"cap_start"}}})");
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {96, 64 * 1024 * 1024});
  solid(evaluator.evaluate(source(d)).value());
}

TEST_CASE("chamfers use current placed faces and existing face filters", "[chamfer]") {
  auto d = beveled();
  d.features.insert(d.features.end() - 1, part(3).features.back());
  selector(d, R"({"edges_of":{"faces_of":"plate","where":{"normal":"z"},"expect":"one"}})");
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {96, 64 * 1024 * 1024});
  solid(evaluator.evaluate(source(d)).value(), 30, 1, 3);
}

TEST_CASE("chamfer selectors check edge cardinality and reject face roots", "[chamfer]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {96, 64 * 1024 * 1024});
  auto d = beveled();
  selector(d, R"({"edges_of":{"created_by":"base","role":"cap_end"},"expect":"one"})");
  failure(evaluator.evaluate(source(d)).value(), "selector_count", "/features/2/inputs/edges");
  selector(d, R"({"created_by":"base","role":"cap_end"})");
  failure(evaluator.evaluate(source(d)).value(), "kernel", "/features/2/inputs/edges");
}

TEST_CASE("chamfers reject unsupported angles and edge filters at their authored fields",
          "[chamfer]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {96, 64 * 1024 * 1024});
  auto d = beveled();
  d.features.back().inputs = set(d.features.back().inputs, "angle", "30 deg");
  failure(evaluator.evaluate(source(d)).value(), "kernel", "/features/2/inputs/angle");
  d = beveled();
  selector(d, R"({"edges_of":{"created_by":"base","role":"cap_end"},"where":{"near":[0,0,30]}})");
  failure(evaluator.evaluate(source(d)).value(), "kernel", "/features/2/inputs/edges/where");
}

TEST_CASE("chamfer failures retire their intended body and recover after a parameter fix",
          "[chamfer]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {96, 64 * 1024 * 1024});
  auto d = beveled();
  for (const auto* distance : {"0 mm", "-1 mm", "100 mm"}) {
    d.params[3].value = distance;
    const auto bad = evaluator.evaluate(source(d)).value();
    REQUIRE_FALSE(bad.ok());
    CHECK_FALSE(bad.bodies.at("plate").shape);
  }
  d.params[3].value = "1 mm";
  solid(evaluator.evaluate(source(d)).value());
}

TEST_CASE("suppressed and rolled back chamfers leave the original solid executable", "[chamfer]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {96, 64 * 1024 * 1024});
  auto d = beveled();
  d.features.back().suppressed = true;
  const auto skipped = evaluator.evaluate(source(d)).value();
  REQUIRE(skipped.ok());
  CHECK_THAT(skipped.bodies.at("plate").shape->measures().volume, WithinAbs(6000, 1e-6));
  d.features.back().suppressed = false;
  d.history_end = "base";
  const auto rollback = evaluator.evaluate(source(d)).value();
  REQUIRE(rollback.ok());
  CHECK_THAT(rollback.bodies.at("plate").shape->measures().volume, WithinAbs(6000, 1e-6));
}

TEST_CASE("chamfer faces and modified caps remain selectable after placement and cache reuse",
          "[chamfer]") {
  auto d = beveled();
  d.features.push_back(part(3).features.back());
  d.features.push_back(feature("on_cap", "sketch", R"({
    "plane":{"created_by":"base","role":"cap_end","expect":"one"},"entities":[
    {"id":"r","type":"rect","corner":[2,2],"size":[1,1]}],"constraints":[]})"));
  d.features.push_back(feature("cap_boss", "extrude", R"({"profile":{"sketch":"on_cap"},
    "distance":2,"result":{"new":"boss","body_type":"brep"}})"));
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {96, 64 * 1024 * 1024});
  for (int iteration = 0; iteration < 2; ++iteration) {
    const auto result = evaluator.evaluate(source(d)).value();
    solid(result, 30, 1, 3);
    const auto& boss = result.bodies.at("boss").shape->measures();
    CHECK_THAT(boss.volume, WithinAbs(2, 1e-6));
    CHECK_THAT(boss.centroid.z, WithinAbs(31, 1e-8));
    CHECK(std::ranges::count_if(result.bodies.at("plate").names, [](const auto& name) {
            return name.feature == "bevel_top" && name.role == "chamfer";
          }) == 4);
    if (iteration == 1) CHECK(result.cache_hits == d.features.size());
  }
  d.features[4].inputs = set(d.features[4].inputs, "plane",
                             ir::json::parse(R"({
    "created_by":"bevel_top","role":"chamfer","where":{"near":[8,0,29.5]},
    "expect":"one"})")
                                 .value());
  REQUIRE(evaluator.evaluate(source(d)).value().ok());
}

TEST_CASE("chamfer selectors retain excluded body dependencies without modifying those bodies",
          "[chamfer]") {
  auto d = beveled();
  d.features.insert(
      d.features.end() - 1,
      {feature("other_sketch", "sketch", R"({"plane":{"datum":"xy"},"entities":[
       {"id":"r","type":"rect","corner":[50,0],"size":[10,20]}],"constraints":[]})"),
       feature("other_base", "extrude", R"({"profile":{"sketch":"other_sketch"},"distance":"depth",
       "result":{"new":"other","body_type":"brep"}})")});
  selector(d, R"({"any":[
    {"edges_of":{"created_by":"base","role":"cap_end"}},
    {"edges_of":{"created_by":"other_base","role":"cap_end"}}],
    "except":{"edges_of":{"created_by":"other_base","role":"cap_end"}}})");
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {96, 64 * 1024 * 1024});
  const auto first = evaluator.evaluate(source(d)).value();
  solid(first);
  CHECK_THAT(first.bodies.at("other").shape->measures().volume, WithinAbs(6000, 1e-6));
  CHECK(first.bodies.at("other").names.front().feature == "other_base");
  const auto cached = evaluator.evaluate(source(d)).value();
  solid(cached);
  CHECK(cached.cache_hits == d.features.size());
  d.features[3].inputs = set(d.features[3].inputs, "distance", 40.);
  const auto edited = evaluator.evaluate(source(d)).value();
  solid(edited);
  CHECK_THAT(edited.bodies.at("other").shape->measures().volume, WithinAbs(8000, 1e-6));
  CHECK(edited.features.back().hash != first.features.back().hash);
}

TEST_CASE("multi-body chamfers publish atomically and independent branches continue", "[chamfer]") {
  auto d = beveled();
  d.features.insert(
      d.features.end() - 1,
      {feature("small_sketch", "sketch", R"({"plane":{"datum":"xy"},"entities":[
       {"id":"r","type":"rect","corner":[50,0],"size":[0.5,0.5]}],"constraints":[]})"),
       feature("small_base", "extrude", R"({"profile":{"sketch":"small_sketch"},"distance":30,
       "result":{"new":"small","body_type":"brep"}})")});
  selector(d, R"({"edges_of":{"any":[
    {"created_by":"base","role":"cap_end"},{"created_by":"small_base","role":"cap_end"}]}})");
  d.features.push_back(feature("independent", "extrude", R"({"profile":{"sketch":"outline"},
    "distance":5,"result":{"new":"unrelated","body_type":"brep"}})"));
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {96, 64 * 1024 * 1024});
  const auto result = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(result.ok());
  CHECK_FALSE(result.bodies.at("plate").shape);
  CHECK_FALSE(result.bodies.at("small").shape);
  REQUIRE(result.bodies.at("unrelated").shape);
  CHECK_THAT(result.bodies.at("unrelated").shape->measures().volume, WithinAbs(1000, 1e-6));
  d.params[3].value = "0.1 mm";
  const auto repaired = evaluator.evaluate(source(d)).value();
  solid(repaired, 30, 0.1);
  CHECK(repaired.bodies.at("small").shape);
}

TEST_CASE("chamfer cache rebinding survives producer renames eviction and retained readers",
          "[chamfer]") {
  auto d = beveled();
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {96, 64 * 1024 * 1024});
  const auto first = evaluator.evaluate(source(d)).value();
  solid(first);
  d.features.back().id = "renamed";
  const auto renamed = evaluator.evaluate(source(d)).value();
  solid(renamed);
  CHECK(renamed.cache_hits == 3);
  CHECK(std::ranges::none_of(renamed.bodies.at("plate").names,
                             [](const auto& n) { return n.feature == "bevel_top"; }));
  CHECK(std::ranges::count_if(renamed.bodies.at("plate").names,
                              [](const auto& n) { return n.feature == "renamed"; }) == 4);
  evaluator.clear_cache();
  const auto recreated = evaluator.evaluate(source(d)).value();
  solid(recreated);
  CHECK(recreated.bodies.at("plate").shape != first.bodies.at("plate").shape);
  solid(first);
  Evaluator tiny(*kernel, {1, 64 * 1024 * 1024});
  solid(tiny.evaluate(source(d)).value());
  solid(tiny.evaluate(source(d)).value());
}

TEST_CASE("chamfer boundary queries validate handles before cached geometry can be reused",
          "[chamfer]") {
  auto kernel = kernel::make_occt_kernel().value();
  QueryKernel proxy(*kernel);
  Evaluator evaluator(proxy, {96, 64 * 1024 * 1024});
  const auto d = beveled();
  solid(evaluator.evaluate(source(d)).value());
  auto other = part(2);
  const auto separate = evaluator.evaluate(source(other)).value();
  proxy.foreign = std::ranges::find_if(separate.bodies.at("plate").names, [](const auto& name) {
                    return name.role == "cap_end";
                  })->entity;
  // Recreate a genuinely distinct snapshot instead of reusing the warm base.
  evaluator.clear_cache();
  solid(evaluator.evaluate(source(d)).value());
  for (int fault = 1; fault <= 4; ++fault) {
    proxy.fault = fault;
    const auto failed = evaluator.evaluate(source(d)).value();
    REQUIRE_FALSE(failed.ok());
    CHECK_FALSE(failed.bodies.at("plate").shape);
    CHECK(failed.features.back().errors.front().code == "kernel");
  }
  proxy.fault = 0;
  solid(evaluator.evaluate(source(d)).value());
}

TEST_CASE("chamfer naming failures publish neither the new geometry nor partial names",
          "[chamfer]") {
  auto kernel = kernel::make_occt_kernel().value();
  QueryKernel proxy(*kernel);
  Evaluator evaluator(proxy, {96, 64 * 1024 * 1024});
  auto d = beveled();
  for (int fault = 5; fault <= 6; ++fault) {
    evaluator.clear_cache();
    proxy.fault = fault;
    const auto result = evaluator.evaluate(source(d)).value();
    REQUIRE_FALSE(result.ok());
    CHECK_FALSE(result.bodies.at("plate").shape);
    CHECK(result.bodies.at("plate").names.empty());
  }
  proxy.fault = 0;
  solid(evaluator.evaluate(source(d)).value());
}

TEST_CASE("fillet naming failures publish neither partial geometry nor names or corrupt cache",
          "[fillet-evaluator]") {
  auto kernel = kernel::make_occt_kernel().value();
  QueryKernel proxy(*kernel);
  Evaluator evaluator(proxy, {96, 64 * 1024 * 1024});
  auto d = beveled();
  d.features.back() = feature("round", "fillet", R"({
    "edges":{"edges_of":{"created_by":"base","role":"cap_end"}},"radius":1})");
  for (int fault = 5; fault <= 6; ++fault) {
    evaluator.clear_cache();
    proxy.fault = fault;
    const auto result = evaluator.evaluate(source(d)).value();
    REQUIRE_FALSE(result.ok());
    CHECK_FALSE(result.bodies.at("plate").shape);
    CHECK(result.bodies.at("plate").names.empty());
    CHECK(result.features.back().errors.front().code == "kernel");
    proxy.fault = 0;
    const auto recovered = evaluator.evaluate(source(d)).value();
    REQUIRE(recovered.ok());
    CHECK(recovered.bodies.at("plate").shape);
    CHECK_FALSE(recovered.bodies.at("plate").names.empty());
  }
}
