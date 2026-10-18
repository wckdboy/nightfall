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
constexpr double kPi = std::numbers::pi;
Value at(math::Vec2 p) { return Array{p.x, p.y}; }
Value ref(const char* id, const char* role) { return Object{{"entity", id}, {"point", role}}; }
Value angle(Value a, Value b, Value value) {
  return Object{{"type", "angle"},
                {"entities", Array{std::move(a), std::move(b)}},
                {"value", std::move(value)}};
}
Value fixed(Value entity) {
  return Object{{"type", "fixed"}, {"entities", Array{std::move(entity)}}};
}
Value sketch(Array entities, Array constraints) {
  return Object{{"plane", Object{{"datum", "xy"}}},
                {"entities", std::move(entities)},
                {"constraints", std::move(constraints)}};
}
Value squares(double theta, Array constraints, double size = 10, bool reverse_a = false,
              bool reverse_b = false, math::Vec2 origin = {}, double rotation = 0) {
  Array entities;
  const auto square = [&](std::string id, math::Vec2 center, double orientation, bool reverse) {
    const auto x = math::Vec2{std::cos(orientation), std::sin(orientation)} * size;
    const auto y = math::Vec2{-x.y, x.x};
    const math::Vec2 points[]{center, center + x, center + x + y, center + y};
    for (std::size_t i = 0; i < 4; ++i) {
      const auto a = points[i], b = points[(i + 1) % 4];
      entities.push_back(Object{{"id", i ? id + std::to_string(i) : id},
                                {"type", "line"},
                                {"start", at(reverse && i == 0 ? b : a)},
                                {"end", at(reverse && i == 0 ? a : b)}});
    }
  };
  square("a", origin, rotation, reverse_a);
  square("b", origin + math::Vec2{4 * size, 3 * size}, rotation + theta, reverse_b);
  return sketch(std::move(entities), std::move(constraints));
}
std::expected<Sketch, ir::Error> prepare(Value inputs, ir::Units units = {}) {
  auto f = feature("s", "sketch", "{}");
  f.inputs = std::move(inputs);
  return prepare_sketch(ir::FeatureInputs::compile(f).value().evaluate(units).value());
}
void rejects(Value inputs, const std::string& pointer, const std::string& message) {
  const auto result = prepare(std::move(inputs));
  REQUIRE_FALSE(result);
  INFO(result.error().message);
  CHECK(result.error().code == "kernel");
  CHECK(result.error().pointer == "/inputs" + pointer);
  CHECK(result.error().message.find(message) != std::string::npos);
}
void unchanged(const Sketch& actual, const Sketch& expected) {
  CHECK(actual.points == expected.points);
  REQUIRE(actual.paths.size() == expected.paths.size());
  for (std::size_t p = 0; p < expected.paths.size(); ++p) {
    REQUIRE(actual.paths[p].size() == expected.paths[p].size());
    for (std::size_t c = 0; c < expected.paths[p].size(); ++c)
      for (const double u : {0.0, 0.25, 0.5, 0.75, 1.0})
        CHECK(actual.paths[p][c].point(u) == expected.paths[p][c].point(u));
  }
  REQUIRE(actual.segments.size() == expected.segments.size());
  for (std::size_t c = 0; c < expected.segments.size(); ++c)
    for (const double u : {0.0, 0.25, 0.5, 0.75, 1.0})
      CHECK(actual.segments[c].point(u) == expected.segments[c].point(u));
}
Array fixed_entities() {
  return *ir::json::parse(R"([
    {"id":"a","type":"line","start":[0,0],"end":[2,0]},
    {"id":"b","type":"line","start":[2,0],"end":[2,2]},
    {"id":"c","type":"line","start":[2,2],"end":[0,2]},
    {"id":"d","type":"line","start":[0,2],"end":[0,0]},
    {"id":"circle","type":"circle","center":[20,0],"radius":2},
    {"id":"arc","type":"arc","center":[40,0],"radius":2,"start_angle":0,"sweep_angle":180},
    {"id":"chord","type":"line","start":[38,0],"end":[42,0]},
    {"id":"ellipse","type":"ellipse","center":[60,0],"radii":[3,2],"rotation":20},
    {"id":"rect","type":"rect","corner":[80,0],"size":[2,3]},
    {"id":"polygon","type":"polygon","points":[[100,0],[102,0],[101,2]]},
    {"id":"spline","type":"spline","degree":1,"knots":[0,0,1,2,3,4,4],
      "control_points":[[120,0],[122,0],[122,2],[120,2],[120,0]]},
    {"id":"p","type":"point","at":[140,0]}])")
              ->if_array();
}
ir::Document angled_part() {
  auto d = part();
  d.params.push_back({"theta", "45 deg", {}, {}, {}, {}});
  d.params.push_back({"required", "45 deg", {}, {}, {}, {}});
  d.features[0].inputs = ir::json::parse(R"json({"plane":{"datum":"xy"},"entities":[
    {"id":"a","type":"line","start":["-5 mm - 10 mm / tan(theta)",-10],"end":["5 mm - 10 mm / tan(theta)",-10]},
    {"id":"b","type":"line","start":["5 mm - 10 mm / tan(theta)",-10],"end":["5 mm + 10 mm / tan(theta)",10]},
    {"id":"c","type":"line","start":["5 mm + 10 mm / tan(theta)",10],"end":["-5 mm + 10 mm / tan(theta)",10]},
    {"id":"d","type":"line","start":["-5 mm + 10 mm / tan(theta)",10],"end":["-5 mm - 10 mm / tan(theta)",-10]}],
    "constraints":[{"type":"angle","entities":["a","b"],"value":"required"},
      {"type":"fixed","entities":["a"]},{"type":"fixed","entities":[{"entity":"b","point":"end"}]}]})json")
                             .value();
  return d;
}
void solid(const Evaluation& result, double theta) {
  for (const auto& f : result.features)
    for (const auto& e : f.errors) UNSCOPED_INFO(e.pointer << ": " << e.message);
  REQUIRE(result.ok());
  const auto& body = result.bodies.at("plate");
  const auto& m = body.shape->measures();
  CHECK(m.solids == 1);
  CHECK_THAT(m.volume, WithinAbs(6000, 1e-7));
  CHECK_THAT(m.area, WithinAbs(1000 + 1200 / std::sin(theta), 1e-7));
  CHECK_THAT(m.centroid.x, WithinAbs(3, 1e-7));
  CHECK_THAT(m.centroid.y, WithinAbs(0, 1e-7));
  CHECK_THAT(m.centroid.z, WithinAbs(15, 1e-7));
  REQUIRE(body.names.size() == 6);
  for (const auto& name : body.names) {
    CHECK(name.feature == "base");
    CHECK(name.entity->owner() == body.shape);
  }
}
}  // namespace

TEST_CASE("angle dimensions verify acute obtuse and endpoint angles without moving curves",
          "[angle-fixed-constraints]") {
  for (const double degrees : {0.0, 30.0, 90.0, 150.0, 180.0}) {
    const double radians = degrees * kPi / 180;
    const auto constrained = prepare(squares(radians, {angle("a", "b", degrees)}));
    REQUIRE(constrained);
    unchanged(*constrained, prepare(squares(radians, {})).value());
  }
}

TEST_CASE("angle dimensions use ordered line directions but symmetric operand order",
          "[angle-fixed-constraints]") {
  for (const bool reverse_a : {false, true})
    for (const bool reverse_b : {false, true}) {
      const double expected = reverse_a != reverse_b ? 120 : 60;
      for (const auto& constraint : {angle("a", "b", expected), angle("b", "a", expected)})
        CHECK(prepare(squares(kPi / 3, {constraint}, 10, reverse_a, reverse_b)));
      rejects(squares(kPi / 3, {angle("a", "b", 180 - expected)}, 10, reverse_a, reverse_b),
              "/constraints/0", "angle constraint is not satisfied");
    }
}

TEST_CASE("angle dimensions have stable angular tolerance near zero and pi at different scales",
          "[angle-fixed-constraints]") {
  constexpr double angular = 1e-9;
  for (const double expected : {0.0, kPi / 3, kPi})
    for (const double size : {0.01, 10.0, 10000.0})
      for (const double factor : {0.25, 4.0}) {
        const double actual = expected + (expected == kPi ? -1 : 1) * factor * angular;
        const auto result =
            prepare(squares(actual, {angle("a", "b", expected)}, size), {"mm", "rad", {}});
        CHECK(static_cast<bool>(result) == (factor < 1));
        if (factor > 1) {
          REQUIRE_FALSE(result);
          CHECK(result.error().pointer == "/inputs/constraints/0");
          CHECK(result.error().message.find(" rad") != std::string::npos);
        }
      }
}

TEST_CASE("angle dimensions normalize document units and dimensional expressions",
          "[angle-fixed-constraints]") {
  for (const auto& length : {"mm", "cm"})
    for (const auto& dimension : {Value(60.0), Value("60 deg"), Value("pi / 3 * 1 rad")})
      CHECK(prepare(squares(kPi / 3, {angle("a", "b", dimension)}), {length, "deg", {}}));
  CHECK(prepare(squares(kPi / 3, {angle("a", "b", kPi / 3)}), {"mm", "rad", {}}));
  for (const double invalid : {-0.001, 180.001, 360.0})
    rejects(squares(kPi / 3, {angle("a", "b", invalid)}), "/constraints/0/value",
            "between zero and pi");
  auto d = angled_part();
  d.params.back().value = "3 mm";
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {32, 16 * 1024 * 1024});
  const auto invalid = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(invalid.ok());
  CHECK(invalid.kernel_calls == 0);
  REQUIRE_FALSE(invalid.features[0].errors.empty());
  CHECK(invalid.features[0].errors[0].pointer == "/features/0/inputs/constraints/0/value");
}

TEST_CASE("angle dimensions reject incompatible degenerate and overflowing line references",
          "[angle-fixed-constraints]") {
  rejects(squares(kPi / 3, {angle(ref("a", "start"), "b", 60.0)}),
          "/constraints/0/entities/0/point", "entire entity");
  rejects(squares(kPi / 3, {angle("a", ref("b", "end"), 60.0)}), "/constraints/0/entities/1/point",
          "entire entity");
  rejects(squares(kPi / 3, {angle("a", "missing", 60.0)}), "/constraints/0/entities/1", "unknown");
  rejects(sketch(fixed_entities(), {angle("a", "circle", 60.0)}), "/constraints/0/entities/1",
          "requires a line");
  auto entities = *squares(kPi / 3, {}).find("entities")->if_array();
  entities[0] =
      Object{{"id", "a"}, {"type", "line"}, {"start", Array{0.0, 0.0}}, {"end", Array{0.0, 0.0}}};
  rejects(sketch(entities, {angle("a", "b", 60.0)}), "/constraints/0/entities/0", "nonzero");
  entities[0] = Object{
      {"id", "a"}, {"type", "line"}, {"start", Array{-1e308, 0.0}}, {"end", Array{1e308, 0.0}}};
  rejects(sketch(entities, {angle("a", "b", 60.0)}), "/constraints/0/entities/0", "finite");
}

TEST_CASE("angle dimensions are invariant under rigid placements and independent line reversal",
          "[angle-fixed-constraints]") {
  std::mt19937 random(20261023);
  std::uniform_real_distribution<double> angle_degrees(5, 175), rotation(-8 * kPi, 8 * kPi),
      coordinate(-1000, 1000);
  for (int i = 0; i < 48; ++i) {
    const double degrees = angle_degrees(random), size = i % 2 ? 0.01 : 100;
    const bool reverse_a = i % 3 == 0, reverse_b = i % 5 == 0;
    const auto result = prepare(squares(
        degrees * kPi / 180, {angle("a", "b", reverse_a != reverse_b ? 180 - degrees : degrees)},
        size, reverse_a, reverse_b, {coordinate(random), coordinate(random)}, rotation(random)));
    if (!result) UNSCOPED_INFO(result.error().message);
    REQUIRE(result);
    CHECK(result->segments.size() == 8);
  }
}

TEST_CASE("fixed references preserve every supported entity and point role",
          "[angle-fixed-constraints]") {
  const auto entities = fixed_entities();
  Array constraints;
  for (const auto& entity : entities) constraints.push_back(fixed(*entity.find("id")));
  for (const auto& point :
       {ref("a", "start"), ref("a", "end"), ref("arc", "start"), ref("arc", "end"),
        ref("arc", "center"), ref("circle", "center"), ref("ellipse", "center")})
    constraints.push_back(fixed(point));
  const auto constrained = prepare(sketch(entities, constraints));
  REQUIRE(constrained);
  unchanged(*constrained, prepare(sketch(entities, {})).value());
  CHECK(constrained->points.at("p") == math::Vec2{140, 0});
}

TEST_CASE("fixed references do not hide invalid geometry references or other constraints",
          "[angle-fixed-constraints]") {
  const auto entities = fixed_entities();
  rejects(sketch(entities, {fixed("missing")}), "/constraints/0/entities/0", "unknown");
  rejects(sketch(entities, {fixed(ref("missing", "center"))}), "/constraints/0/entities/0/entity",
          "unknown");
  for (const auto& point :
       {ref("a", "center"), ref("circle", "start"), ref("spline", "start"), ref("p", "center")})
    rejects(sketch(entities, {fixed(point)}), "/constraints/0/entities/0/point", "not supported");
  rejects(sketch(entities, {fixed("a"), angle("a", "b", 45.0)}), "/constraints/1", "not satisfied");
  rejects(
      sketch(
          {Object{{"id", "c"}, {"type", "circle"}, {"center", Array{0.0, 0.0}}, {"radius", -2.0}}},
          {fixed("c")}),
      "/entities/0/radius", "radius");
  const auto zero =
      Object{{"id", "a"}, {"type", "line"}, {"start", Array{0.0, 0.0}}, {"end", Array{0.0, 0.0}}};
  rejects(sketch({zero}, {fixed("a")}), "/entities/0", "zero-length");
}

TEST_CASE("angle driven fixed geometry rebuilds and recovers from constraint only edits",
          "[angle-fixed-constraints]") {
  auto d = angled_part();
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {32, 16 * 1024 * 1024});
  const auto first = evaluator.evaluate(source(d)).value();
  solid(first, kPi / 4);
  d.params.back().value = "60 deg";
  const auto failed = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(failed.ok());
  CHECK(failed.kernel_calls == 0);
  REQUIRE_FALSE(failed.features[0].errors.empty());
  CHECK(failed.features[0].errors[0].pointer == "/features/0/inputs/constraints/0");
  CHECK_FALSE(failed.bodies.at("plate").shape);
  for (const std::size_t i : {1U, 2U}) {
    REQUIRE_FALSE(failed.features[i].errors.empty());
    CHECK(failed.features[i].errors[0].code == "input_failed");
  }
  d.params.back().value = "0.7853981633974483 rad";
  const auto restored = evaluator.evaluate(source(d)).value();
  solid(restored, kPi / 4);
  CHECK(restored.kernel_calls == 0);
  CHECK(restored.cache_hits == 3);
  CHECK(restored.bodies.at("plate").shape == first.bodies.at("plate").shape);
  for (const auto& [value, theta] : {std::pair{"60 deg", kPi / 3}, {"120 deg", 2 * kPi / 3}}) {
    d.params[d.params.size() - 2].value = value;
    d.params.back().value = value;
    const auto edited = evaluator.evaluate(source(d)).value();
    solid(edited, theta);
    CHECK(edited.kernel_calls == 2);
    CHECK(edited.cache_hits == 0);
    Evaluator cold(*kernel, {0, 0});
    solid(cold.evaluate(source(d)).value(), theta);
  }
  evaluator.clear_cache();
  solid(first, kPi / 4);
}
