// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "evaluator.hpp"
#include "model_inputs.hpp"
#include "part_fixture.hpp"

#include <numbers>
#include <random>

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
Value point(math::Vec2 p) { return Array{p.x, p.y}; }
Value relation(const char* kind, Array entities) {
  return Object{{"type", kind}, {"entities", std::move(entities)}};
}
Value radius(const char* id, Value value) {
  return Object{{"type", "radius"}, {"entities", Array{id}}, {"value", std::move(value)}};
}
Value sketch(Array entities, Array constraints) {
  return Object{{"plane", Object{{"datum", "xy"}}},
                {"entities", std::move(entities)},
                {"constraints", std::move(constraints)}};
}
Value quadrilateral(const std::vector<math::Vec2>& points, Array constraints) {
  Array entities;
  for (std::size_t i = 0; i < 4; ++i)
    entities.push_back(Object{{"id", std::string(1, static_cast<char>('a' + i))},
                              {"type", "line"},
                              {"start", point(points[i])},
                              {"end", point(points[(i + 1) % 4])}});
  return sketch(std::move(entities), std::move(constraints));
}
Value rectangle(Array constraints, double width = 10, double height = 20) {
  return quadrilateral({{0, 0}, {width, 0}, {width, height}, {0, height}}, std::move(constraints));
}
Value circle(Value r, Array constraints) {
  return sketch(
      {Object{
          {"id", "a"}, {"type", "circle"}, {"center", Array{2.0, -3.0}}, {"radius", std::move(r)}}},
      std::move(constraints));
}
std::expected<Sketch, ir::Error> prepare(Value inputs, ir::Units units = {}) {
  auto f = feature("s", "sketch", "{}");
  f.inputs = std::move(inputs);
  auto evaluated = ir::FeatureInputs::compile(f).value().evaluate(units).value();
  return prepare_sketch(evaluated);
}
void rejected(Value inputs, const std::string& suffix, const std::string& message) {
  const auto result = prepare(std::move(inputs));
  REQUIRE_FALSE(result);
  CHECK(result.error().code == "kernel");
  CHECK(result.error().pointer == "/inputs/constraints/0" + suffix);
  CHECK(result.error().message.find(message) != std::string::npos);
}
Array rectangle_constraints() {
  return {relation("horizontal", {"a"}),    relation("vertical", {"b"}),
          relation("parallel", {"a", "c"}), relation("perpendicular", {"a", "b"}),
          relation("equal", {"a", "c"}),    relation("equal", {"b", "d"})};
}
ir::Document constrained_circle() {
  auto d = part();
  d.params.push_back({"actual", "5 mm", {}, {}, {}, {}});
  d.params.push_back({"required", "5 mm", {}, {}, {}, {}});
  d.features[0].inputs = circle("actual", {radius("a", "required")});
  return d;
}
void check_cylinder(const Evaluation& evaluation, double r) {
  REQUIRE(evaluation.ok());
  const auto& body = evaluation.bodies.at("plate");
  const auto& m = body.shape->measures();
  CHECK(m.solids == 1);
  CHECK_THAT(m.volume, WithinAbs(30 * std::numbers::pi * r * r, 1e-5));
  CHECK_THAT(m.area, WithinAbs(2 * std::numbers::pi * r * (r + 30), 1e-5));
  CHECK_THAT(m.centroid.x, WithinAbs(5, 1e-7));
  CHECK_THAT(m.centroid.y, WithinAbs(-3, 1e-7));
  CHECK_THAT(m.centroid.z, WithinAbs(15, 1e-7));
  REQUIRE_FALSE(body.names.empty());
  for (const auto& name : body.names) {
    CHECK(name.feature == "base");
    CHECK(name.entity->owner() == body.shape);
  }
}
}  // namespace

TEST_CASE("sketch constraints verify line relationships without changing geometry",
          "[constraints]") {
  const auto constrained = prepare(rectangle(rectangle_constraints()));
  REQUIRE(constrained);
  const auto plain = prepare(rectangle({})).value();
  REQUIRE(constrained->paths.size() == plain.paths.size());
  for (std::size_t p = 0; p < plain.paths.size(); ++p) {
    REQUIRE(constrained->paths[p].size() == plain.paths[p].size());
    for (std::size_t c = 0; c < plain.paths[p].size(); ++c)
      for (const double u : {0.0, 0.25, 0.5, 0.75, 1.0})
        CHECK(constrained->paths[p][c].point(u) == plain.paths[p][c].point(u));
  }
  for (auto constraint : {relation("horizontal", {"b"}), relation("vertical", {"a"}),
                          relation("parallel", {"a", "b"}), relation("perpendicular", {"a", "c"}),
                          relation("equal", {"a", "b"})}) {
    CAPTURE(ir::json::write(constraint));
    rejected(rectangle({std::move(constraint)}), "", "constraint is not satisfied");
  }
}

TEST_CASE("sketch constraint orientation uses angular tolerance at different sizes",
          "[constraints]") {
  constexpr double angular = 1e-9;
  for (const double size : {0.01, 10.0, 10000.0})
    for (const double factor : {0.25, 4.0}) {
      const double a = factor * angular;
      const auto x = math::Vec2{std::cos(a), std::sin(a)} * size;
      const auto y = math::Vec2{-std::sin(a), std::cos(a)} * size;
      for (auto constraint : {relation("horizontal", {"a"}), relation("vertical", {"b"})}) {
        const auto result = prepare(quadrilateral({{}, x, x + y, y}, {constraint}));
        CHECK(static_cast<bool>(result) == (factor < 1));
      }
      const auto parallel =
          prepare(quadrilateral({{0, 0}, {size, 0}, {size, size + size * std::tan(a)}, {0, size}},
                                {relation("parallel", {"a", "c"})}));
      CHECK(static_cast<bool>(parallel) == (factor < 1));
      const auto perpendicular =
          prepare(quadrilateral({{0, 0}, {size, 0}, {size + size * std::tan(a), size}, {0, size}},
                                {relation("perpendicular", {"a", "b"})}));
      CHECK(static_cast<bool>(perpendicular) == (factor < 1));
    }
}

TEST_CASE("sketch constraints preserve rotated translated rectangles", "[constraints][property]") {
  std::mt19937_64 rng(20261020);
  std::uniform_real_distribution<double> rotation(-3.0, 3.0), location(-100.0, 100.0);
  for (int trial = 0; trial < 48; ++trial) {
    const double angle = rotation(rng), size = trial % 2 ? 0.1 : 100;
    const math::Vec2 origin{location(rng), location(rng)};
    const auto x = math::Vec2{std::cos(angle), std::sin(angle)} * size;
    const auto y = math::Vec2{-std::sin(angle), std::cos(angle)} * (2 * size);
    CAPTURE(trial, angle, size);
    CHECK(prepare(
        quadrilateral({origin, origin + x, origin + x + y, origin + y},
                      {relation("parallel", {"a", "c"}), relation("perpendicular", {"a", "b"}),
                       relation("equal", {"a", "c"})})));
  }
}

TEST_CASE("sketch constraint dimensions use millimetre tolerance after unit conversion",
          "[constraints]") {
  constexpr double linear = 1e-6;
  for (const double factor : {0.25, 4.0}) {
    CHECK(static_cast<bool>(prepare(rectangle({relation("equal", {"a", "b"})}, 10,
                                              10 + factor * linear))) == (factor < 1));
    CHECK(static_cast<bool>(prepare(circle(10.0, {radius("a", 10 + factor * linear)}))) ==
          (factor < 1));
  }
  CHECK(prepare(circle(1.0, {radius("a", "10 mm")}), {"cm", "deg", {}}));
  rejected(circle(10.0, {radius("a", "11 mm")}), "", "radius constraint is not satisfied");
  rejected(circle(10.0, {radius("a", -10.0)}), "/value", "positive");
  rejected(circle(10.0, {radius("a", 0.0)}), "/value", "positive");
}

TEST_CASE("sketch constraints compare circle and partial arc radii", "[constraints]") {
  const auto entities = *ir::json::parse(R"([
    {"id":"a","type":"circle","center":[0,0],"radius":10},
    {"id":"b","type":"arc","center":[30,0],"radius":10,"start_angle":0,"sweep_angle":180},
    {"id":"c","type":"line","start":[20,0],"end":[40,0]}])")
                             ->if_array();
  CHECK(prepare(sketch(entities, {relation("equal", {"a", "b"}), radius("b", "1 cm")})));
  CHECK(prepare(sketch(entities, {relation("equal", {"b", "a"})})));
  rejected(sketch(entities, {radius("b", 9.0)}), "", "constraint is not satisfied");
  auto unequal = *circle(9.0, {}).find("entities")->if_array();
  unequal.insert(unequal.end(), entities.begin() + 1, entities.end());
  rejected(sketch(unequal, {relation("equal", {"a", "b"})}), "", "constraint is not satisfied");
}

TEST_CASE("sketch constraints reject unsupported relations and incompatible references",
          "[constraints]") {
  rejected(circle(10.0, {relation("horizontal", {"a"})}), "/entities/0", "line");
  rejected(rectangle({radius("a", 10.0)}), "/entities/0", "circle or arc");
  auto mixed = *circle(10.0, {}).find("entities")->if_array();
  mixed.push_back(Object{
      {"id", "r"}, {"type", "rect"}, {"corner", Array{30.0, 0.0}}, {"size", Array{10.0, 20.0}}});
  rejected(sketch(mixed, {relation("equal", {"a", "r"})}), "/entities/1", "circle or arc");
  rejected(circle(10.0, {radius("missing", 10.0)}), "/entities/0", "unknown");
  for (const auto* type : {"coincident", "tangent", "midpoint"})
    rejected(circle(10.0, {relation(type, {"a", "a"})}), "/type", "not implemented");
  rejected(circle(10.0, {relation("fixed", {"a"})}), "/type", "not implemented");
  rejected(circle(10.0, {relation("symmetric", {"a", "a", "a"})}), "/type", "not implemented");
}

TEST_CASE("sketch constraint work and derived geometry are bounded", "[constraints]") {
  CHECK(prepare(circle(10.0, Array(4096, radius("a", 10.0)))));
  auto too_many = prepare(circle(10.0, Array(4097, radius("a", 10.0))));
  REQUIRE_FALSE(too_many);
  CHECK(too_many.error().pointer == "/inputs/constraints");
  CHECK(too_many.error().message.find("4096") != std::string::npos);
  rejected(rectangle({relation("horizontal", {"a"})}, 0), "/entities/0", "nonzero");
  rejected(quadrilateral({{-1e308, 0}, {1e308, 0}, {1e308, 20}, {-1e308, 20}},
                         {relation("horizontal", {"a"})}),
           "/entities/0", "finite");
}

TEST_CASE("constrained line sketches build the same exact solid", "[constraints]") {
  auto d = part();
  d.features[0].inputs = rectangle(rectangle_constraints());
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {32, 16 * 1024 * 1024});
  const auto result = evaluator.evaluate(source(d)).value();
  REQUIRE(result.ok());
  CHECK(result.kernel_calls == 2);
  const auto& m = result.bodies.at("plate").shape->measures();
  CHECK_THAT(m.volume, WithinAbs(6000, 1e-8));
  CHECK_THAT(m.area, WithinAbs(2200, 1e-8));
  CHECK_THAT(m.centroid.x, WithinAbs(8, 1e-8));
  CHECK_THAT(m.centroid.y, WithinAbs(10, 1e-8));
  CHECK_THAT(m.centroid.z, WithinAbs(15, 1e-8));
}

TEST_CASE("constraint-only edits invalidate dependents and recover cached solids",
          "[constraints]") {
  auto d = constrained_circle();
  d.features.push_back(feature("independent", "sketch", "{}"));
  d.features.back().inputs = rectangle({});
  d.features.push_back(feature("other", "extrude", R"({"profile":{"sketch":"independent"},
    "distance":30,"result":{"new":"spare","body_type":"brep"}})"));
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {32, 16 * 1024 * 1024});
  const auto first = evaluator.evaluate(source(d)).value();
  check_cylinder(first, 5);
  d.params.back().value = "6 mm";
  const auto failed = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(failed.ok());
  CHECK(failed.kernel_calls == 0);
  CHECK(failed.cache_hits == 2);
  REQUIRE(failed.features[0].errors.size() == 1);
  CHECK(failed.features[0].errors[0].pointer == "/features/0/inputs/constraints/0");
  CHECK(failed.features[0].errors[0].message.find("radius constraint is not satisfied") !=
        std::string::npos);
  for (const std::size_t index : {1U, 2U}) {
    REQUIRE_FALSE(failed.features[index].errors.empty());
    CHECK(failed.features[index].errors[0].code == "input_failed");
  }
  CHECK_FALSE(failed.bodies.at("plate").shape);
  CHECK_THAT(failed.bodies.at("spare").shape->measures().volume, WithinAbs(6000, 1e-8));
  d.params.back().value = "0.5 cm";
  const auto restored = evaluator.evaluate(source(d)).value();
  check_cylinder(restored, 5);
  CHECK(restored.kernel_calls == 0);
  CHECK(restored.cache_hits == 5);
  CHECK(restored.bodies.at("plate").shape == first.bodies.at("plate").shape);
  d.params[d.params.size() - 2].value = "7 mm";
  d.params.back().value = "7 mm";
  const auto changed = evaluator.evaluate(source(d)).value();
  check_cylinder(changed, 7);
  CHECK(changed.kernel_calls == 2);
  CHECK(changed.cache_hits == 2);
  Evaluator cold(*kernel, {0, 0});
  check_cylinder(cold.evaluate(source(d)).value(), 7);
  evaluator.clear_cache();
  check_cylinder(first, 5);
  check_cylinder(changed, 7);
}

TEST_CASE("constraint verification respects suppression rollback and diagnostic locations",
          "[constraints]") {
  auto d = constrained_circle();
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {32, 16 * 1024 * 1024});
  d.features[0].inputs = circle(5.0, {radius("a", "unknown")});
  d.features[0].suppressed = true;
  const auto suppressed = evaluator.evaluate(source(d)).value();
  CHECK(suppressed.features[0].status == FeatureStatus::kSuppressed);
  CHECK(suppressed.features[0].errors.empty());
  CHECK(suppressed.kernel_calls == 0);
  CHECK(suppressed.features[1].errors[0].code == "input_suppressed");
  d.features[0].suppressed = false;
  const auto expression_error = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(expression_error.features[0].errors.empty());
  CHECK(expression_error.features[0].errors[0].pointer == "/features/0/inputs/constraints/0/value");
  d = constrained_circle();
  d.history_end = "base";
  d.features.push_back(feature("later", "sketch", "{}"));
  d.features.back().inputs = circle(5.0, {radius("a", "unknown")});
  const auto rolled = evaluator.evaluate(source(d)).value();
  CHECK(rolled.ok());
  CHECK(rolled.features.back().status == FeatureStatus::kRolledBack);
  CHECK(rolled.features.back().errors.empty());
}
