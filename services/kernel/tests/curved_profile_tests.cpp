// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "evaluator.hpp"
#include "model_inputs.hpp"
#include "part_fixture.hpp"

#include <numbers>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "nf/ir/inputs.hpp"
#include "nf/math/constructors.hpp"

using namespace nf;
using namespace nf::kernel_service;
using namespace nf::kernel_service::test;
using Catch::Matchers::WithinAbs;

namespace {
constexpr double kPi = std::numbers::pi;
ir::json::Value sketch_inputs(const char* entities) {
  return ir::json::Object{{"plane", ir::json::Object{{"datum", "xy"}}},
                          {"entities", ir::json::parse(entities).value()},
                          {"constraints", ir::json::Array{}}};
}
std::expected<Sketch, ir::Error> prepare(const ir::json::Value& value) {
  auto f = feature("outline", "sketch", "{}");
  f.inputs = value;
  return prepare_sketch(ir::FeatureInputs::compile(f).value().evaluate().value());
}
kernel::ShapePtr extrude(const Sketch& sketch, double depth = 4) {
  const auto f = feature("base", "extrude", R"({"profile":{"sketch":"outline"},
    "distance":4,"result":{"new":"plate","body_type":"brep"}})");
  auto input = prepare_extrusion(ir::FeatureInputs::compile(f).value().evaluate().value(), sketch);
  REQUIRE(input);
  input->displacement = sketch.plane.normal * depth;
  auto kernel = kernel::make_occt_kernel().value();
  const auto result = kernel->extrude(*input);
  INFO((result ? "ok" : result.error().message));
  REQUIRE(result);
  return result->shape;
}
const char* half_disk = R"([
  {"id":"arc","type":"arc","center":[0,0],"radius":10,"start_angle":0,"sweep_angle":180},
  {"id":"base","type":"line","start":[-10,0],"end":[10,0]}])";
void set(ir::json::Value& object, std::string_view key, ir::json::Value value) {
  auto members = *object.if_object();
  for (auto& member : members)
    if (member.key == key) member.value = value;
  object = std::move(members);
}
void set_entity(ir::json::Value& inputs, std::string_view key, ir::json::Value value,
                std::size_t i = 0) {
  auto entities = *inputs.find("entities")->if_array();
  set(entities[i], key, std::move(value));
  set(inputs, "entities", std::move(entities));
}
}  // namespace

TEST_CASE("curved profiles preserve signed arcs and analytic semicylinder measures", "[curved]") {
  for (const double sign : {-1.0, 1.0}) {
    auto inputs = sketch_inputs(half_disk);
    set_entity(inputs, "start_angle", sign < 0 ? 180.0 : 0.0);
    set_entity(inputs, "sweep_angle", sign * 180);
    auto sketch = prepare(inputs);
    REQUIRE(sketch);
    REQUIRE(sketch->paths.size() == 1);
    CHECK(sketch->paths[0].size() == 2);
    const auto shape = extrude(*sketch);
    CHECK_THAT(shape->measures().volume, WithinAbs(200 * kPi, 1e-6));
    CHECK_THAT(shape->measures().area, WithinAbs(140 * kPi + 80, 1e-6));
    CHECK_THAT(shape->measures().centroid.y, WithinAbs(40 / (3 * kPi), 1e-7));
  }
}

TEST_CASE("curved profiles accept signed full arcs and normalize large start angles", "[curved]") {
  auto inputs = sketch_inputs(R"([
    {"id":"a","type":"arc","center":[2,3],"radius":10,
     "start_angle":36000000000000,"sweep_angle":360}])");
  for (const double sweep : {-360.0, 360.0}) {
    set_entity(inputs, "sweep_angle", sweep);
    auto sketch = prepare(inputs);
    REQUIRE(sketch);
    const auto shape = extrude(*sketch);
    CHECK_THAT(shape->measures().volume, WithinAbs(400 * kPi, 1e-6));
    CHECK_THAT(shape->measures().centroid.x, WithinAbs(2, 1e-7));
    CHECK_THAT(shape->measures().centroid.y, WithinAbs(3, 1e-7));
  }
}

TEST_CASE("curved profiles preserve rotated ellipses on placed datum planes", "[curved]") {
  auto inputs = sketch_inputs(R"([
    {"id":"e","type":"ellipse","center":[7,11],"radii":[6,3],"rotation":90}])");
  set(inputs, "plane", ir::json::Object{{"datum", "yz"}});
  auto sketch = prepare(inputs);
  REQUIRE(sketch);
  REQUIRE(sketch->paths.size() == 1);
  const auto& curve = sketch->paths[0][0];
  CHECK(curve.rational());
  CHECK_THAT(curve.point(0).x, WithinAbs(7, 1e-10));
  CHECK_THAT(curve.point(0).y, WithinAbs(17, 1e-10));
  const auto shape = extrude(*sketch);
  CHECK_THAT(shape->measures().volume, WithinAbs(72 * kPi, 1e-6));
  CHECK_THAT(shape->measures().centroid.x, WithinAbs(2, 1e-7));
  CHECK_THAT(shape->measures().centroid.y, WithinAbs(7, 1e-7));
  CHECK_THAT(shape->measures().centroid.z, WithinAbs(11, 1e-7));
}

TEST_CASE("curved profiles join unordered arcs lines and reversed rational splines", "[curved]") {
  auto sketch = prepare(sketch_inputs(R"json([
    {"id":"a","type":"arc","center":[0,0],"radius":10,"start_angle":0,"sweep_angle":90},
    {"id":"s","type":"spline","degree":2,"knots":[2,2,2,5,5,5],
     "control_points":[[-10,0],[-10,10],[0,10]],"weights":[1,"sqrt(0.5)",1]},
    {"id":"l","type":"line","start":[-10,0],"end":[10,0]}])json"));
  REQUIRE(sketch);
  REQUIRE(sketch->paths.size() == 1);
  CHECK(sketch->paths[0].size() == 3);
  const auto shape = extrude(*sketch);
  CHECK_THAT(shape->measures().volume, WithinAbs(200 * kPi, 1e-6));
  CHECK_THAT(shape->measures().centroid.y, WithinAbs(40 / (3 * kPi), 1e-7));
}

TEST_CASE("curved profiles support closed non-rational splines", "[curved]") {
  auto sketch = prepare(sketch_inputs(R"([
    {"id":"s","type":"spline","degree":1,"knots":[0,0,1,2,3,4,4],
     "control_points":[[0,0],[10,0],[10,20],[0,20],[0,0]]}])"));
  REQUIRE(sketch);
  CHECK_FALSE(sketch->paths[0][0].rational());
  CHECK_THAT(extrude(*sketch)->measures().volume, WithinAbs(800, 1e-8));
}

TEST_CASE("curved profiles retain exact rational spline knots poles and weights", "[curved]") {
  const auto circle = math::make_circle(math::Vec2{3, 5}, 10);
  ir::json::Array points, knots, weights;
  for (const auto p : circle.points()) points.push_back(ir::json::Array{p.x, p.y});
  for (const auto k : circle.knots().knots()) knots.push_back(k);
  for (const auto w : circle.weights()) weights.push_back(w);
  auto inputs = sketch_inputs("[]");
  set(inputs, "entities",
      ir::json::Array{ir::json::Object{{"id", "s"},
                                       {"type", "spline"},
                                       {"degree", 2.0},
                                       {"knots", knots},
                                       {"control_points", points},
                                       {"weights", weights}}});
  const auto sketch = prepare(inputs);
  REQUIRE(sketch);
  const auto& curve = sketch->paths[0][0];
  for (std::size_t i = 0; i < circle.points().size(); ++i) {
    CHECK(curve.points()[i] == circle.points()[i]);
    CHECK(curve.weights()[i] == circle.weights()[i]);
  }
  for (std::size_t i = 0; i < circle.knots().size(); ++i)
    CHECK(curve.knots()[i] == circle.knots()[i]);
  CHECK_THAT(extrude(*sketch)->measures().volume, WithinAbs(400 * kPi, 1e-6));
}

TEST_CASE("curved semicircle profiles revolve into exact spheres", "[curved]") {
  auto document = part(2);
  document.features[0].inputs = sketch_inputs(half_disk);
  document.features[1] = feature("base", "revolve", R"({"profile":{"sketch":"outline"},
    "axis":{"origin":[0,0,0],"direction":"x"},"angle":360,
    "result":{"new":"plate","body_type":"brep"}})");
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {32, 16 * 1024 * 1024});
  auto result = evaluator.evaluate(source(document)).value();
  REQUIRE(result.ok());
  const auto& measures = result.bodies.at("plate").shape->measures();
  CHECK_THAT(measures.volume, WithinAbs(4000 * kPi / 3, 1e-5));
  CHECK_THAT(measures.area, WithinAbs(400 * kPi, 1e-5));
  CHECK_THAT(math::length(measures.centroid), WithinAbs(0, 1e-7));
}

TEST_CASE("curved profile geometry edits rebuild only dependent features and restore cache",
          "[curved]") {
  auto document = part();
  document.params.push_back({"radius", "10 mm", {}, {}, {}, {}});
  document.features[0].inputs = sketch_inputs(R"([
    {"id":"e","type":"ellipse","center":[0,0],"radii":["radius",5]}])");
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {32, 16 * 1024 * 1024});
  const auto original = evaluator.evaluate(source(document)).value();
  REQUIRE(original.ok());
  CHECK_THAT(original.bodies.at("plate").shape->measures().volume, WithinAbs(1500 * kPi, 1e-5));
  document.params[3].value = "20 mm";
  const auto edited = evaluator.evaluate(source(document)).value();
  REQUIRE(edited.ok());
  CHECK(edited.kernel_calls == 2);
  CHECK(edited.cache_hits == 0);
  CHECK_THAT(edited.bodies.at("plate").shape->measures().volume, WithinAbs(3000 * kPi, 1e-5));
  document.params[3].value = "1 cm";
  const auto restored = evaluator.evaluate(source(document)).value();
  REQUIRE(restored.ok());
  CHECK(restored.kernel_calls == 0);
  CHECK(restored.cache_hits == 3);
  CHECK(restored.bodies.at("plate").shape == original.bodies.at("plate").shape);
  document.params[3].value = "-1 mm";
  const auto failed = evaluator.evaluate(source(document)).value();
  CHECK_FALSE(failed.ok());
  CHECK_FALSE(failed.bodies.at("plate").shape);
  REQUIRE_FALSE(failed.features[0].errors.empty());
  CHECK(failed.features[0].errors[0].pointer == "/features/0/inputs/entities/0/radii");
  REQUIRE_FALSE(failed.features[1].errors.empty());
  CHECK(failed.features[1].errors[0].code == "input_failed");
}

TEST_CASE("curved profiles reject malformed arcs ellipses and splines with field pointers",
          "[curved]") {
  for (const auto sweep : {0.0, 361.0, -361.0}) {
    auto inputs = sketch_inputs(half_disk);
    set_entity(inputs, "sweep_angle", sweep);
    const auto result = prepare(inputs);
    REQUIRE_FALSE(result);
    CHECK(result.error().pointer == "/inputs/entities/0/sweep_angle");
  }
  auto invalid_radius = sketch_inputs(half_disk);
  set_entity(invalid_radius, "radius", -1.0);
  const auto radius = prepare(invalid_radius);
  REQUIRE_FALSE(radius);
  CHECK(radius.error().pointer == "/inputs/entities/0/radius");
  for (const auto& [field, value] : std::vector<std::pair<std::string, ir::json::Value>>{
           {"knots", ir::json::Array{0.0, 0.0, 1.0, 0.0, 1.0, 1.0}},
           {"knots", ir::json::Array{0.0, 0.0, 1.0, 1.0}},
           {"knots", ir::json::Array{1e308, 1e308, 1e308, 1.1e308, 1.1e308, 1.1e308}},
           {"control_points",
            ir::json::Array{ir::json::Array{0.0, 0.0}, ir::json::Array{1.0, 1.0}}},
           {"weights", ir::json::Array{1.0, 0.0, 1.0}},
           {"weights", ir::json::Array{1.0, 1.0}},
           {"weights", ir::json::Array{1e-300, 1e300, 1.0}}}) {
    CAPTURE(field);
    auto inputs = sketch_inputs(R"([{"id":"s","type":"spline","degree":2,
      "knots":[0,0,0,1,1,1],"control_points":[[0,0],[1,1],[2,0]],"weights":[1,1,1]}])");
    set_entity(inputs, field, value);
    const auto result = prepare(inputs);
    REQUIRE_FALSE(result);
    CHECK(result.error().pointer ==
          "/inputs/entities/0/" + (field == "control_points" ? "knots" : field));
  }
}

TEST_CASE("curved profiles reject open chains and junctions shared by closed loops", "[curved]") {
  auto open = sketch_inputs(half_disk);
  set_entity(open, "end", ir::json::Array{11.0, 0.0}, 1);
  const auto gap = prepare(open);
  REQUIRE_FALSE(gap);
  CHECK(gap.error().message.find("open") != std::string::npos);
  auto junction = sketch_inputs(R"([
    {"id":"a","type":"arc","center":[-10,0],"radius":10,"start_angle":0,"sweep_angle":180},
    {"id":"b","type":"line","start":[-20,0],"end":[0,0]},
    {"id":"c","type":"arc","center":[10,0],"radius":10,"start_angle":0,"sweep_angle":180},
    {"id":"d","type":"line","start":[20,0],"end":[0,0]}])");
  const auto ambiguous = prepare(junction);
  REQUIRE_FALSE(ambiguous);
  CHECK(ambiguous.error().message.find("ambiguous") != std::string::npos);
}

TEST_CASE("curved profile spline work is bounded before geometry construction", "[curved]") {
  auto inputs = sketch_inputs(R"([{"id":"s","type":"spline","degree":1,
    "knots":[0,0,1,1],"control_points":[[0,0],[1,1]]}])");
  set_entity(inputs, "control_points", ir::json::Array(4097, ir::json::Array{0.0, 0.0}));
  const auto result = prepare(inputs);
  REQUIRE_FALSE(result);
  CHECK(result.error().pointer == "/inputs/entities/0/control_points");
  CHECK(result.error().message.find("4096") != std::string::npos);
}

TEST_CASE("curved profile holes and interior selections survive exact span decomposition",
          "[curved]") {
  auto document = part(2);
  document.features[0].inputs = sketch_inputs(R"([
    {"id":"outer","type":"ellipse","center":[0,0],"radii":[10,5],"rotation":35},
    {"id":"inner","type":"ellipse","center":[0,0],"radii":[4,2],"rotation":35},
    {"id":"island","type":"ellipse","center":[30,0],"radii":[3,2]}])");
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {32, 16 * 1024 * 1024});
  auto result = evaluator.evaluate(source(document)).value();
  REQUIRE(result.ok());
  CHECK_THAT(result.bodies.at("plate").shape->measures().volume, WithinAbs(1440 * kPi, 1e-5));
  CHECK(result.bodies.at("plate").shape->measures().solids == 2);
  set(document.features[1].inputs, "profile",
      ir::json::Object{{"sketch", "outline"},
                       {"regions", ir::json::Array{ir::json::Array{30.0, 0.0}}}});
  result = evaluator.evaluate(source(document)).value();
  REQUIRE(result.ok());
  CHECK_THAT(result.bodies.at("plate").shape->measures().volume, WithinAbs(180 * kPi, 1e-5));
  CHECK(result.cache_hits == 1);
}

TEST_CASE("curved profiles validate ellipse radii and expression-driven spline degrees",
          "[curved]") {
  for (const double radius : {0.0, -3.0}) {
    auto inputs = sketch_inputs(R"([
      {"id":"e","type":"ellipse","center":[0,0],"radii":[6,3]}])");
    set_entity(inputs, "radii", ir::json::Array{6.0, radius});
    auto result = prepare(inputs);
    REQUIRE_FALSE(result);
    CHECK(result.error().pointer == "/inputs/entities/0/radii");
  }
  for (const char* degree : {"0", "1.5", "10"}) {
    auto f = feature("outline", "sketch", "{}");
    f.inputs = sketch_inputs(R"([{"id":"s","type":"spline","degree":1,
      "knots":[0,0,1,1],"control_points":[[0,0],[1,1]]}])");
    set_entity(f.inputs, "degree", degree);
    const auto compiled = ir::FeatureInputs::compile(f);
    REQUIRE(compiled);
    const auto result = compiled->evaluate();
    REQUIRE_FALSE(result);
    CHECK(result.error()[0].pointer == "/inputs/entities/0/degree");
  }
}

TEST_CASE("curved profile span limits precede expensive arrangement work", "[curved]") {
  Sketch sketch;
  for (std::size_t i = 0; i < 1025; ++i)
    sketch.paths.push_back({math::make_circle(math::Vec2{static_cast<double>(i) * 10, 0}, 2)});
  auto f = feature("base", "extrude", R"({"profile":{"sketch":"outline"},
    "distance":4,"result":{"new":"plate","body_type":"brep"}})");
  const auto result =
      prepare_extrusion(ir::FeatureInputs::compile(f).value().evaluate().value(), sketch);
  REQUIRE_FALSE(result);
  CHECK(result.error().pointer == "/inputs/profile");
  CHECK(result.error().message.find("4096-span") != std::string::npos);
}
