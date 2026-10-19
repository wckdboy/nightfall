// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "evaluator.hpp"
#include "part_fixture.hpp"

#include <algorithm>
#include <limits>
#include <numbers>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

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
    if (member.key == key) member.value = std::move(value);
  return members;
}
constexpr auto kAxis = R"({"created_by":"axis_base","role":"side","from":"axis_outline/c",
  "where":{"type":"cylinder","radius":"2 mm","near":["axis_x + 1.5 mm","1.5 mm",20]},
  "expect":"one"})";
ir::Document rotated() {
  auto d = part(2);
  d.params.push_back({"axis_x", "0 mm", {}, {}, {}, {}});
  d.params.push_back({"axis_height", "40 mm", {}, {}, {}, {}});
  d.features[0] = feature("outline", "sketch", R"({"plane":{"datum":"xy"},"entities":[
    {"id":"r","type":"rect","corner":[9,-1],"size":[2,2]}],"constraints":[]})");
  d.features.insert(d.features.begin(),
                    {feature("axis_outline", "sketch", R"({"plane":{"origin":["axis_x",0,0],
      "normal":"z","x_direction":"x"},"entities":[
      {"id":"c","type":"circle","center":[0,0],"radius":2}],"constraints":[]})"),
                     feature("axis_base", "extrude", R"({"profile":{"sketch":"axis_outline"},
      "distance":"axis_height","result":{"new":"axis_body","body_type":"brep"}})")});
  auto move = feature("spin", "transform", R"({"bodies":["plate"],"translation":["step",0,0],
    "rotation":{"axis":{"origin":[0,0,0],"direction":"z"},"angle":90}})");
  move.inputs = set(move.inputs, "rotation",
                    set(*move.inputs.find("rotation"), "axis", ir::json::parse(kAxis).value()));
  d.features.push_back(std::move(move));
  return d;
}
void axis_selector(ir::Document& d, const char* text) {
  auto& feature = d.features.back();
  const auto selection = ir::json::parse(text).value();
  if (feature.type == "transform")
    feature.inputs =
        set(feature.inputs, "rotation", set(*feature.inputs.find("rotation"), "axis", selection));
  else
    feature.inputs = set(feature.inputs, "axis", selection);
}
void solid(const Evaluation& result, double volume, double area, math::Vec3 center,
           const std::string& id = "plate", std::size_t solids = 1) {
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
  CHECK(m.solids == solids);
  for (const auto& name : body.names) CHECK(name.entity->owner() == body.shape);
}
class QueryKernel final : public kernel::IKernel {
 public:
  kernel::IKernel& real;
  int fault = 0, queries = 0, transforms = 0, fail_transform = 0;
  explicit QueryKernel(kernel::IKernel& backend) : real(backend) {}
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
    auto result = real.faces(shape);
    if (result)
      for (auto& face : *result) {
        if (!face.cylinder) continue;
        if (fault == 2) face.cylinder->axis.direction = {0, 0, 2};
        if (fault == 3) face.cylinder->radius = -1;
        if (fault == 4) face.cylinder->axis.origin.x = std::numeric_limits<double>::quiet_NaN();
        if (fault == 5) face.cylinder->radius = std::numeric_limits<double>::infinity();
        if (fault == 6) face.plane = kernel::Plane{};
        if (fault == 7) face.cylinder.reset();
      }
    return result;
  }
  std::expected<std::string, kernel::KernelError> export_step(
      const std::vector<kernel::ShapePtr>& bodies) override {
    return real.export_step(bodies);
  }
  std::expected<kernel::Plane, kernel::KernelError> face_plane(
      const kernel::EntityPtr& face) override {
    return real.face_plane(face);
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
    if (++transforms == fail_transform)
      return std::unexpected(kernel::KernelError{"kernel", "transform failed"});
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

TEST_CASE("selected cylinder axes rotate only authored targets and query cached sources",
          "[cylinder-axis]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto d = rotated();
  const auto first = evaluator.evaluate(source(d)).value();
  solid(first, 120, 248, {3, -10, 15});
  solid(first, 160 * kPi, 168 * kPi, {0, 0, 20}, "axis_body");
  CHECK(first.kernel_calls == 8);  // 3 operations, enumeration and four patch distances
  const auto cached = evaluator.evaluate(source(d)).value();
  solid(cached, 120, 248, {3, -10, 15});
  CHECK(cached.cache_hits == 5);
  CHECK(cached.kernel_calls == 5);
  CHECK(cached.bodies.at("axis_body").shape == first.bodies.at("axis_body").shape);
  d.params[3].value = "5 mm";
  const auto edited = evaluator.evaluate(source(d)).value();
  solid(edited, 120, 248, {8, -5, 15});
  solid(edited, 160 * kPi, 168 * kPi, {5, 0, 20}, "axis_body");
  CHECK(edited.cache_hits == 2);
  CHECK(edited.features.back().hash != first.features.back().hash);
  d.params[4].value = "50 mm";
  const auto taller = evaluator.evaluate(source(d)).value();
  solid(taller, 120, 248, {8, -5, 15});
  solid(taller, 200 * kPi, 208 * kPi, {5, 0, 25}, "axis_body");
  CHECK(taller.features.back().hash != edited.features.back().hash);
}

TEST_CASE("selected cylinder axes drive circular body patterns", "[cylinder-axis]") {
  auto d = rotated();
  d.features.back() = feature("repeat", "pattern_circular", R"({"bodies":["plate"],
    "count":4,"angle":360,"axis":{"origin":[0,0,0],"direction":"z"}})");
  axis_selector(d, kAxis);
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  const auto first = evaluator.evaluate(source(d)).value();
  solid(first, 480, 992, {0, 0, 15}, "plate", 4);
  solid(first, 160 * kPi, 168 * kPi, {0, 0, 20}, "axis_body");
  CHECK(first.kernel_calls == 11);
  solid(evaluator.evaluate(source(d)).value(), 480, 992, {0, 0, 15}, "plate", 4);
}

TEST_CASE("selected cylinder axes revolve new profiles without publishing source bodies",
          "[cylinder-axis]") {
  auto d = rotated();
  d.features.resize(4);
  d.features[2] = feature("outline", "sketch", R"({"plane":{"datum":"zx"},"entities":[
    {"id":"r","type":"rect","corner":[0,5],"size":["depth",5]}],"constraints":[]})");
  d.features[3] = feature("base", "revolve", R"({"profile":{"sketch":"outline"},"angle":360,
    "axis":{"origin":[0,0,0],"direction":"z"},"result":{"new":"plate","body_type":"brep"}})");
  axis_selector(d, kAxis);
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  solid(evaluator.evaluate(source(d)).value(), 2250 * kPi, 1050 * kPi, {0, 0, 15});
  solid(evaluator.evaluate(source(d)).value(), 2250 * kPi, 1050 * kPi, {0, 0, 15});
}

TEST_CASE("coaxial cylinder patches still require exactly one selected face", "[cylinder-axis]") {
  auto kernel = kernel::make_occt_kernel().value();
  for (const auto* expectation : {"one", "some", "any"}) {
    auto d = rotated();
    const auto text = std::string(R"({"created_by":"axis_base","role":"side","expect":")") +
                      expectation + R"("})";
    axis_selector(d, text.c_str());
    Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
    const auto result = evaluator.evaluate(source(d)).value();
    REQUIRE_FALSE(result.ok());
    CHECK(result.features.back().errors.front().code == "selector_count");
    CHECK(result.features.back().errors.front().pointer == "/features/4/inputs/rotation/axis");
    CHECK_FALSE(result.bodies.at("plate").shape);
    CHECK(result.bodies.at("axis_body").shape);
  }
}

TEST_CASE("cylinder radius filters use length units and absolute linear tolerance",
          "[cylinder-axis]") {
  auto kernel = kernel::make_occt_kernel().value();
  for (const auto* radius : {"0.2 cm", "2.0000005 mm", "2.000002 mm"}) {
    auto d = rotated();
    auto selection = ir::json::parse(kAxis).value();
    selection = set(selection, "where", set(*selection.find("where"), "radius", radius));
    axis_selector(d, ir::json::write(selection).c_str());
    Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
    const auto result = evaluator.evaluate(source(d)).value();
    if (radius == std::string_view("2.000002 mm")) {
      REQUIRE_FALSE(result.ok());
      CHECK(result.features.back().errors.front().code == "selector_count");
    } else
      solid(result, 120, 248, {3, -10, 15});
  }
}

TEST_CASE("selected cylinder axes compose provenance unions exclusions and nearest filters",
          "[cylinder-axis]") {
  auto d = rotated();
  axis_selector(d, R"({"any":[{"created_by":"axis_base","role":"side","from":"axis_outline/c"},
    {"faces_of":"axis_body","where":{"type":"cylinder","radius":2}}],
    "except":{"created_by":"axis_base","role":"cap_start"},
    "where":{"near":[1.5,1.5,20]},"expect":"one"})");
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  solid(evaluator.evaluate(source(d)).value(), 120, 248, {3, -10, 15});
}

TEST_CASE("axis consumers reject selected planar faces at the axis pointer", "[cylinder-axis]") {
  auto d = rotated();
  axis_selector(d, R"({"created_by":"axis_base","role":"cap_end","expect":"one"})");
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  const auto result = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(result.ok());
  CHECK(result.features.back().errors.front().code == "kernel");
  CHECK(result.features.back().errors.front().pointer == "/features/4/inputs/rotation/axis");
  CHECK(result.features.back().errors.front().message.find("cylindrical") != std::string::npos);
}

TEST_CASE("cylinder axes preserve signed rotations and current source placements",
          "[cylinder-axis]") {
  auto d = rotated();
  d.features.back().inputs = set(d.features.back().inputs, "rotation",
                                 set(*d.features.back().inputs.find("rotation"), "angle", -90.0));
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  solid(evaluator.evaluate(source(d)).value(), 120, 248, {3, 10, 15});
  d = rotated();
  d.features.insert(d.features.begin() + 2, feature("place_axis", "transform", R"({
    "bodies":["axis_body"],"translation":[5,0,0],"rotation":{
      "axis":{"origin":[0,0,0],"direction":"y"},"angle":90}})"));
  axis_selector(d, R"({"created_by":"axis_base","role":"side",
    "where":{"type":"cylinder","near":[15,1.5,1.5]},"expect":"one"})");
  const auto placed = evaluator.evaluate(source(d)).value();
  solid(placed, 120, 248, {13, 15, 0});
  solid(placed, 160 * kPi, 168 * kPi, {25, 0, 0}, "axis_body");
}

TEST_CASE("consumed tool cylinder axes resolve through current boolean survivors",
          "[cylinder-axis]") {
  auto d = rotated();
  d.features.insert(
      d.features.begin() + 2,
      {feature("replacement", "extrude", R"({"profile":{"sketch":"axis_outline"},"distance":50,
      "result":{"new":"replacement_body","body_type":"brep"}})"),
       feature("merge", "boolean", R"({"target":"replacement_body","tools":["axis_body"],
      "op":"union"})")});
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  const auto built = evaluator.evaluate(source(d)).value();
  solid(built, 120, 248, {3, -10, 15});
  CHECK_FALSE(built.bodies.at("axis_body").shape);
  solid(built, 200 * kPi, 208 * kPi, {0, 0, 25}, "replacement_body");
  solid(evaluator.evaluate(source(d)).value(), 120, 248, {3, -10, 15});
}

TEST_CASE("cylinder axis source suppression and failure leave independent branches executable",
          "[cylinder-axis]") {
  auto kernel = kernel::make_occt_kernel().value();
  for (const bool suppress : {true, false}) {
    auto d = rotated();
    if (suppress)
      d.features[1].suppressed = true;
    else
      d.params[4].value = "0 mm";
    d.features.push_back(feature("independent", "extrude", R"({
      "profile":{"sketch":"outline"},"distance":"depth",
      "result":{"new":"spare","body_type":"brep"}})"));
    Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
    const auto failed = evaluator.evaluate(source(d)).value();
    REQUIRE_FALSE(failed.ok());
    CHECK(failed.features[4].errors.front().code ==
          (suppress ? "input_suppressed" : "input_failed"));
    CHECK(failed.features[4].errors.front().pointer ==
          "/features/4/inputs/rotation/axis/created_by");
    CHECK_FALSE(failed.bodies.at("plate").shape);
    REQUIRE(failed.bodies.at("spare").shape);
    CHECK_THAT(failed.bodies.at("spare").shape->measures().volume, WithinAbs(120, 1e-6));
  }
}

TEST_CASE("malformed cylinder descriptions are rejected before cached axis reuse",
          "[cylinder-axis]") {
  auto real = kernel::make_occt_kernel().value();
  for (const int fault : {1, 2, 3, 4, 5, 6, 7}) {
    QueryKernel kernel(*real);
    Evaluator evaluator(kernel, {128, 64 * 1024 * 1024});
    const auto d = rotated();
    const auto first = evaluator.evaluate(source(d)).value();
    solid(first, 120, 248, {3, -10, 15});
    kernel.fault = fault;
    const auto failed = evaluator.evaluate(source(d)).value();
    REQUIRE_FALSE(failed.ok());
    CHECK_FALSE(failed.bodies.at("plate").shape);
    CHECK(failed.bodies.at("axis_body").shape == first.bodies.at("axis_body").shape);
    CHECK(failed.features.back().errors.front().code == (fault == 7 ? "selector_count" : "kernel"));
    CHECK(failed.features.back().errors.front().pointer == "/features/4/inputs/rotation/axis");
    kernel.fault = 0;
    const auto retry = evaluator.evaluate(source(d)).value();
    solid(retry, 120, 248, {3, -10, 15});
    CHECK(retry.cache_hits == 5);
    CHECK(kernel.queries == 3);
    CHECK(retry.bodies.at("plate").shape == first.bodies.at("plate").shape);
  }
}

TEST_CASE("selector axis sources are retained and charged separately from operation targets",
          "[cylinder-axis]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator selected(*kernel, {128, 64 * 1024 * 1024}),
      explicit_axis(*kernel, {128, 64 * 1024 * 1024});
  auto d = rotated();
  const auto first = selected.evaluate(source(d)).value();
  solid(first, 120, 248, {3, -10, 15});
  axis_selector(d, R"({"origin":[0,0,0],"direction":"-z"})");
  solid(explicit_axis.evaluate(source(d)).value(), 120, 248, {3, -10, 15});
  CHECK(selected.cache_usage().estimated_bytes >=
        explicit_axis.cache_usage().estimated_bytes +
            first.bodies.at("axis_body").shape->estimated_bytes());
  selected.set_cache_limits({0, 0});
  d = rotated();
  const auto evicted = selected.evaluate(source(d)).value();
  solid(evicted, 120, 248, {3, -10, 15});
  CHECK(evicted.cache_hits == 0);
  CHECK(evicted.kernel_calls == 8);
  CHECK(selected.cache_usage().entries == 0);
  solid(first, 120, 248, {3, -10, 15});
}

TEST_CASE("cached cylinder sources rebind renamed producer identities", "[cylinder-axis]") {
  auto d = rotated();
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  const auto first = evaluator.evaluate(source(d)).value();
  solid(first, 120, 248, {3, -10, 15});
  d.features[1].id = "renamed_axis";
  auto axis = ir::json::parse(kAxis).value();
  axis = set(axis, "created_by", "renamed_axis");
  axis_selector(d, ir::json::write(axis).c_str());
  const auto rebound = evaluator.evaluate(source(d)).value();
  solid(rebound, 120, 248, {3, -10, 15});
  CHECK(rebound.bodies.at("axis_body").shape == first.bodies.at("axis_body").shape);
  CHECK(rebound.features[1].status == FeatureStatus::kCached);
  CHECK(rebound.features.back().hash != first.features.back().hash);
  for (const auto& name : rebound.bodies.at("axis_body").names)
    CHECK(name.feature == "renamed_axis");
}

TEST_CASE("selected cylinder transformations publish multiple targets atomically",
          "[cylinder-axis]") {
  auto d = rotated();
  d.features.insert(d.features.begin() + 4, feature("other", "extrude", R"({
    "profile":{"sketch":"outline"},"distance":20,
    "result":{"new":"other_body","body_type":"brep"}})"));
  d.features.back().inputs =
      set(d.features.back().inputs, "bodies", ir::json::parse(R"(["plate","other_body"])").value());
  auto real = kernel::make_occt_kernel().value();
  QueryKernel kernel(*real);
  kernel.fail_transform = 2;
  Evaluator evaluator(kernel, {128, 64 * 1024 * 1024});
  const auto failed = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(failed.ok());
  CHECK_FALSE(failed.bodies.at("plate").shape);
  CHECK_FALSE(failed.bodies.at("other_body").shape);
  CHECK(failed.bodies.at("axis_body").shape);
  kernel.fail_transform = 0;
  const auto retry = evaluator.evaluate(source(d)).value();
  solid(retry, 120, 248, {3, -10, 15});
  solid(retry, 80, 168, {3, -10, 10}, "other_body");
  solid(retry, 160 * kPi, 168 * kPi, {0, 0, 20}, "axis_body");
  CHECK(retry.kernel_calls == 7);
}

TEST_CASE("radius and type filters remain conjunctive with planar direction filters",
          "[cylinder-axis]") {
  auto kernel = kernel::make_occt_kernel().value();
  for (const auto* where :
       {R"({"type":"cylinder","normal":"z"})", R"({"type":"plane","radius":2})"}) {
    auto d = rotated();
    auto axis = ir::json::parse(kAxis).value();
    axis = set(axis, "where", ir::json::parse(where).value());
    axis_selector(d, ir::json::write(axis).c_str());
    Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
    const auto failed = evaluator.evaluate(source(d)).value();
    REQUIRE_FALSE(failed.ok());
    CHECK(failed.features.back().errors.front().code == "selector_count");
  }
  auto d = rotated();
  axis_selector(d, R"({"faces_of":"axis_body","where":{"radius":2,"near":[1.5,1.5,20]}})");
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  solid(evaluator.evaluate(source(d)).value(), 120, 248, {3, -10, 15});
}

TEST_CASE("selector revolutions keep signed partial sweeps and reject noncoplanar axes",
          "[cylinder-axis]") {
  auto d = rotated();
  d.features.resize(4);
  d.features[2] = feature("outline", "sketch", R"({"plane":{"datum":"zx"},"entities":[
    {"id":"r","type":"rect","corner":[0,5],"size":["depth",5]}],"constraints":[]})");
  d.features[3] = feature("base", "revolve", R"({"profile":{"sketch":"outline"},"angle":90,
    "axis":{"origin":[0,0,0],"direction":"z"},"result":{"new":"plate","body_type":"brep"}})");
  axis_selector(d, kAxis);
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  for (const double angle : {90.0, -90.0}) {
    d.features[3].inputs = set(d.features[3].inputs, "angle", angle);
    solid(evaluator.evaluate(source(d)).value(), 562.5 * kPi, 300 + 262.5 * kPi,
          {140 / (9 * kPi), std::copysign(140 / (9 * kPi), -angle), 15});
  }
  d.features[0].inputs =
      set(d.features[0].inputs, "plane",
          ir::json::parse(R"({"origin":[0,3,0],"normal":"z","x_direction":"x"})").value());
  const auto failed = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(failed.ok());
  CHECK(failed.features[3].errors.front().code == "kernel");
  CHECK(failed.features[3].errors.front().pointer == "/features/3/inputs");
  CHECK_FALSE(failed.bodies.at("plate").shape);
  CHECK(failed.bodies.at("axis_body").shape);
}

TEST_CASE("selector revolutions preserve result joins when the source is also the target",
          "[cylinder-axis]") {
  auto d = rotated();
  d.features.resize(4);
  d.features[2] = feature("outline", "sketch", R"({"plane":{"datum":"zx"},"entities":[
    {"id":"r","type":"rect","corner":[0,5],"size":["depth",5]}],"constraints":[]})");
  d.features[3] = feature("base", "revolve", R"({"profile":{"sketch":"outline"},"angle":360,
    "axis":{"origin":[0,0,0],"direction":"z"},"result":{"join":"axis_body"}})");
  axis_selector(d, kAxis);
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  const auto first = evaluator.evaluate(source(d)).value();
  solid(first, 2410 * kPi, 1218 * kPi, {0, 0, 36950.0 / 2410}, "axis_body", 2);
  solid(evaluator.evaluate(source(d)).value(), 2410 * kPi, 1218 * kPi, {0, 0, 36950.0 / 2410},
        "axis_body", 2);
  CHECK_FALSE(first.bodies.contains("plate"));
}

TEST_CASE("one-instance circular patterns still validate their selected cylinder axis",
          "[cylinder-axis]") {
  auto d = rotated();
  d.features.back() = feature("repeat", "pattern_circular", R"({"bodies":["plate"],
    "count":1,"angle":0,"axis":{"origin":[0,0,0],"direction":"z"}})");
  axis_selector(d, kAxis);
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  solid(evaluator.evaluate(source(d)).value(), 120, 248, {10, 0, 15});
  axis_selector(d, R"({"created_by":"axis_base","role":"cap_end"})");
  const auto failed = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(failed.ok());
  CHECK(failed.features.back().errors.front().pointer == "/features/4/inputs/axis");
  CHECK(failed.features.back().errors.front().code == "kernel");
}
