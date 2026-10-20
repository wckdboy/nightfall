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
Value line(const char* id, math::Vec2 a, math::Vec2 b) {
  return Object{{"id", id}, {"type", "line"}, {"start", at(a)}, {"end", at(b)}};
}
Value circle(const char* id, math::Vec2 c, double r) {
  return Object{{"id", id}, {"type", "circle"}, {"center", at(c)}, {"radius", r}};
}
Value arc(const char* id, math::Vec2 c, double r, double start, double sweep) {
  return Object{{"id", id},    {"type", "arc"},        {"center", at(c)},
                {"radius", r}, {"start_angle", start}, {"sweep_angle", sweep}};
}
Value tangent(Value a, Value b) {
  return Object{{"type", "tangent"}, {"entities", Array{std::move(a), std::move(b)}}};
}
Value sketch(Array entities, Array constraints) {
  return Object{{"plane", Object{{"datum", "xy"}}},
                {"entities", std::move(entities)},
                {"constraints", std::move(constraints)}};
}
std::expected<Sketch, ir::Error> prepare(Value inputs, ir::Units units = {}) {
  auto f = feature("s", "sketch", "{}");
  f.inputs = std::move(inputs);
  const auto evaluated = ir::FeatureInputs::compile(f).value().evaluate(units).value();
  return prepare_sketch(evaluated);
}
void rejects(Value inputs, const std::string& pointer, const std::string& message) {
  const auto result = prepare(std::move(inputs));
  REQUIRE_FALSE(result);
  INFO(result.error().message);
  CHECK(result.error().code == "kernel");
  CHECK(result.error().pointer == "/inputs/constraints/0" + pointer);
  CHECK(result.error().message.find(message) != std::string::npos);
}
Array line_circle(math::Vec2 center, double r = 2, bool reverse = false) {
  return {line("a", reverse ? math::Vec2{5, 0} : math::Vec2{-5, 0},
               reverse ? math::Vec2{-5, 0} : math::Vec2{5, 0}),
          line("b", {5, 0}, {5, -5}), line("c", {5, -5}, {-5, -5}), line("d", {-5, -5}, {-5, 0}),
          circle("e", center, r)};
}
Array closed_arc(double start, double sweep, math::Vec2 other = {4, 0}) {
  Array entities{arc("a", {}, 2, start, sweep), circle("b", other, 2)};
  if (std::abs(sweep) < 360) {
    const auto radial = [](double degrees) {
      const double a = std::remainder(degrees, 360) * kPi / 180;
      return math::Vec2{2 * std::cos(a), 2 * std::sin(a)};
    };
    entities.push_back(line("chord", radial(start + sweep), radial(start)));
  }
  return entities;
}
Value capsule(double r = 5, double angle = 0, math::Vec2 origin = {}, bool reverse = false,
              bool constrained = true) {
  const double c = std::cos(angle), s = std::sin(angle);
  const auto placed = [&](math::Vec2 p) {
    return origin + math::Vec2{c * p.x - s * p.y, s * p.x + c * p.y};
  };
  const auto edge = [&](const char* id, math::Vec2 a, math::Vec2 b) {
    return line(id, placed(reverse ? b : a), placed(reverse ? a : b));
  };
  const double degrees = angle * 180 / kPi;
  Array entities{
      edge("a", {-2 * r, -r}, {2 * r, -r}),
      arc("b", placed({2 * r, 0}), r, degrees + (reverse ? 90 : -90), reverse ? -180 : 180),
      edge("c", {2 * r, r}, {-2 * r, r}),
      arc("d", placed({-2 * r, 0}), r, degrees + (reverse ? 270 : 90), reverse ? -180 : 180)};
  return sketch(entities, constrained ? Array{tangent("a", "b"), tangent("b", "c"),
                                              tangent("c", "d"), tangent("d", "a")}
                                      : Array{});
}
ir::Document rounded_part() {
  auto d = part();
  d.params.push_back({"radius", "5 mm", {}, {}, {}, {}});
  d.params.push_back({"offset", "0 mm", {}, {}, {}, {}});
  d.features[0].inputs = ir::json::parse(R"({"plane":{"datum":"xy"},"entities":[
    {"id":"a","type":"line","start":[-10,"-radius"],"end":[10,"-radius"]},
    {"id":"b","type":"arc","center":["10 mm + offset",0],"radius":"radius","start_angle":-90,"sweep_angle":180},
    {"id":"c","type":"line","start":[10,"radius"],"end":[-10,"radius"]},
    {"id":"d","type":"arc","center":[-10,0],"radius":"radius","start_angle":90,"sweep_angle":180}],
    "constraints":[{"type":"tangent","entities":["a","b"]},{"type":"tangent","entities":["b","c"]},
    {"type":"tangent","entities":["c","d"]},{"type":"tangent","entities":["d","a"]}]})")
                             .value();
  return d;
}
void solid(const Evaluation& result, double r) {
  for (const auto& f : result.features)
    for (const auto& e : f.errors) UNSCOPED_INFO(e.pointer << ": " << e.message);
  REQUIRE(result.ok());
  const auto& body = result.bodies.at("plate");
  const auto& m = body.shape->measures();
  const double area = 40 * r + kPi * r * r, perimeter = 40 + 2 * kPi * r;
  CHECK(m.solids == 1);
  CHECK_THAT(m.volume, WithinAbs(30 * area, 1e-5));
  CHECK_THAT(m.area, WithinAbs(2 * area + 30 * perimeter, 1e-5));
  CHECK_THAT(m.centroid.x, WithinAbs(3, 1e-7));
  CHECK_THAT(m.centroid.y, WithinAbs(0, 1e-7));
  CHECK_THAT(m.centroid.z, WithinAbs(15, 1e-7));
  REQUIRE_FALSE(body.names.empty());
  for (const auto& name : body.names) {
    CHECK(name.feature == "base");
    CHECK(name.entity->owner() == body.shape);
  }
}
}  // namespace

TEST_CASE("tangent constraints preserve exact rounded profile curves", "[tangent-constraints]") {
  for (const bool reverse : {false, true}) {
    const auto constrained = prepare(capsule(5, 0, {}, reverse));
    REQUIRE(constrained);
    const auto plain = prepare(capsule(5, 0, {}, reverse, false)).value();
    REQUIRE(constrained->paths.size() == plain.paths.size());
    REQUIRE(constrained->segments.size() == plain.segments.size());
    for (std::size_t c = 0; c < plain.segments.size(); ++c)
      for (const double u : {0.0, 0.25, 0.5, 0.75, 1.0})
        CHECK(constrained->segments[c].point(u) == plain.segments[c].point(u));
  }
  const auto cm = prepare(capsule(), {"cm", "deg", {}});
  REQUIRE(cm);
  const auto mm = prepare(capsule(50)).value();
  REQUIRE(cm->segments.size() == mm.segments.size());
  CHECK_THAT(math::length(cm->segments[0].point(0) - mm.segments[0].point(0)), WithinAbs(0, 1e-10));
}

TEST_CASE("line circle tangency checks radial gap and finite segment extent",
          "[tangent-constraints]") {
  constexpr double linear = 1e-6;
  for (const bool reverse : {false, true})
    for (const auto& relation : {tangent("a", "e"), tangent("e", "a")})
      for (const double sign : {-1.0, 1.0}) {
        CHECK(prepare(sketch(line_circle({0, sign * 2}, 2, reverse), {relation})));
        for (const double factor : {0.25, 4.0}) {
          const auto gap = prepare(
              sketch(line_circle({0, sign * (2 + factor * linear)}, 2, reverse), {relation}));
          CHECK(static_cast<bool>(gap) == (factor < 1));
          const auto extent = prepare(
              sketch(line_circle({sign * (5 + factor * linear), 2}, 2, reverse), {relation}));
          CHECK(static_cast<bool>(extent) == (factor < 1));
        }
      }
  for (const auto c : {math::Vec2{0, 0}, {0, 1}, {0, 3}, {6, 2}})
    rejects(sketch(line_circle(c), {tangent("a", "e")}), "", "not satisfied");
}

TEST_CASE("circle tangency distinguishes external internal and coincident contact",
          "[tangent-constraints]") {
  constexpr double linear = 1e-6;
  for (const auto& relation : {tangent("a", "b"), tangent("b", "a")})
    for (const double contact : {3.0, 7.0}) {
      CHECK(prepare(sketch({circle("a", {}, 5), circle("b", {contact, 0}, 2)}, {relation})));
      for (const double sign : {-1.0, 1.0})
        for (const double factor : {0.25, 4.0}) {
          const auto result = prepare(
              sketch({circle("a", {}, 5), circle("b", {contact + sign * factor * linear, 0}, 2)},
                     {relation}));
          CHECK(static_cast<bool>(result) == (factor < 1));
        }
    }
  for (const double separation : {1.0, 4.0, 8.0})
    rejects(sketch({circle("a", {}, 5), circle("b", {separation, 0}, 2)}, {tangent("a", "b")}), "",
            "not satisfied");
  for (const double r : {2.0, 5.0})
    rejects(sketch({circle("a", {}, 5), circle("b", {}, r)}, {tangent("a", "b")}), "",
            "distinct centers");
  rejects(sketch({circle("a", {}, 5)}, {tangent("a", "a")}), "", "distinct centers");
}

TEST_CASE("tangent contact respects wrapped reversed and full arc domains",
          "[tangent-constraints]") {
  for (const auto [start, sweep] : {std::pair{-60.0, 120.0},
                                    {60.0, -120.0},
                                    {720.0, 90.0},
                                    {810.0, -90.0},
                                    {180.0, 180.0},
                                    {0.0, -360.0},
                                    {45.0, 360.0}})
    for (const auto& relation : {tangent("a", "b"), tangent("b", "a")})
      CHECK(prepare(sketch(closed_arc(start, sweep), {relation})));
  for (const auto [start, sweep] : {std::pair{45.0, 90.0}, {-45.0, -90.0}}) {
    rejects(sketch(closed_arc(start, sweep), {tangent("a", "b")}), "/entities/0",
            "outside the arc");
    rejects(sketch(closed_arc(start, sweep), {tangent("b", "a")}), "/entities/1",
            "outside the arc");
  }
  CHECK(prepare(sketch({arc("a", {}, 2, -kPi / 3, 2 * kPi), arc("b", {4, 0}, 2, kPi, -2 * kPi)},
                       {tangent("a", "b")}),
                {"mm", "rad", {}}));
}

TEST_CASE("arc contact domain tolerance is angular", "[tangent-constraints]") {
  constexpr double angular = 1e-9;
  for (const double factor : {0.25, 4.0})
    for (const bool reverse : {false, true}) {
      const double a = -factor * angular;
      const auto result = prepare(sketch(
          closed_arc(reverse ? 90 : 0, reverse ? -90 : 90, {4 * std::cos(a), 4 * std::sin(a)}),
          {tangent("a", "b")}));
      CHECK(static_cast<bool>(result) == (factor < 1));
    }
}

TEST_CASE("internal arc tangency uses the correct contact side", "[tangent-constraints]") {
  for (const auto& relation : {tangent("a", "b"), tangent("b", "a")}) {
    auto entities = closed_arc(-60, 120);
    entities[1] = circle("b", {-3, 0}, 5);  // inner a touches the larger b at x=2
    CHECK(prepare(sketch(entities, {relation})));
    entities[1] = circle("b", {3, 0}, 5);  // contact moves to x=-2, outside a
    const auto result = prepare(sketch(entities, {relation}));
    REQUIRE_FALSE(result);
    CHECK(result.error().message.find("outside the arc") != std::string::npos);
  }
  auto entities = line_circle({0, 2});
  entities[4] = arc("e", {0, 2}, 2, 0, 180);
  entities.push_back(line("chord", {-2, 2}, {2, 2}));
  rejects(sketch(entities, {tangent("a", "e")}), "/entities/1", "outside the arc");
  entities[4] = arc("e", {0, 2}, 2, 180, 180);
  CHECK(prepare(sketch(entities, {tangent("a", "e")})));
}

TEST_CASE("tangent operands reject unsupported and degenerate geometry", "[tangent-constraints]") {
  auto entities = line_circle({0, 2});
  rejects(sketch(entities, {tangent("a", "c")}), "", "circle or arc");
  rejects(sketch(entities, {tangent("missing", "e")}), "/entities/0", "unknown");
  rejects(sketch(entities, {tangent(Object{{"entity", "a"}, {"point", "end"}}, "e")}),
          "/entities/0/point", "entire entity");
  rejects(sketch(entities, {tangent("a", Object{{"entity", "e"}, {"point", "center"}})}),
          "/entities/1/point", "entire entity");
  entities[4] = Object{
      {"id", "e"}, {"type", "ellipse"}, {"center", Array{0.0, 2.0}}, {"radii", Array{2.0, 2.0}}};
  rejects(sketch(entities, {tangent("a", "e")}), "/entities/1", "line, circle or arc");
  for (const double r : {0.0, -1.0, 1e-7}) {
    rejects(sketch(line_circle({0, 2}, r), {tangent("a", "e")}), "/entities/1", "positive radius");
  }
  for (const double sweep : {0.0, 1e-10, 361.0, -361.0})
    rejects(sketch({arc("a", {}, 2, 0, sweep), circle("b", {4, 0}, 2)}, {tangent("a", "b")}),
            "/entities/0", "sweep");
  entities = line_circle({0, 2});
  entities[0] = line("a", {}, {});
  rejects(sketch(entities, {tangent("a", "e")}), "/entities/0", "nonzero line");
}

TEST_CASE("tangent arithmetic rejects overflowing derived coordinates", "[tangent-constraints]") {
  rejects(sketch({circle("a", {-1e308, 0}, 5), circle("b", {1e308, 0}, 2)}, {tangent("a", "b")}),
          "", "finite");
  auto entities = line_circle({1e308, 2});
  entities[0] = line("a", {-1e308, 0}, {-1e308, 10});
  rejects(sketch(entities, {tangent("a", "e")}), "", "finite");
  entities[0] = line("a", {-1e308, 0}, {1e308, 0});
  rejects(sketch(entities, {tangent("a", "e")}), "/entities/0", "finite");
}

TEST_CASE("rounded tangency is invariant under rigid placement scale and reversal",
          "[tangent-constraints]") {
  std::mt19937 random(20261022);
  std::uniform_real_distribution<double> angles(-8 * kPi, 8 * kPi), coordinates(-1000, 1000);
  for (int i = 0; i < 48; ++i) {
    const double r = i % 2 ? 0.01 : 100;
    const auto input =
        capsule(r, angles(random), {coordinates(random), coordinates(random)}, i % 3 == 0);
    const auto result = prepare(input);
    if (!result) INFO(result.error().message);
    REQUIRE(result);
    CHECK(result->segments.size() == 4);
  }
}

TEST_CASE("tangent constrained solids rebuild reject edits and restore cached geometry",
          "[tangent-constraints]") {
  auto d = rounded_part();
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {32, 16 * 1024 * 1024});
  const auto first = evaluator.evaluate(source(d)).value();
  solid(first, 5);
  d.params.back().value = "1 mm";
  const auto failed = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(failed.ok());
  CHECK(failed.kernel_calls == 0);
  REQUIRE_FALSE(failed.features[0].errors.empty());
  CHECK(failed.features[0].errors[0].pointer == "/features/0/inputs/constraints/0");
  CHECK(failed.features[0].errors[0].message.find("not satisfied") != std::string::npos);
  CHECK_FALSE(failed.bodies.at("plate").shape);
  for (const std::size_t i : {1U, 2U}) {
    REQUIRE_FALSE(failed.features[i].errors.empty());
    CHECK(failed.features[i].errors[0].code == "input_failed");
  }
  d.params.back().value = "0 cm";
  const auto restored = evaluator.evaluate(source(d)).value();
  solid(restored, 5);
  CHECK(restored.kernel_calls == 0);
  CHECK(restored.cache_hits == 3);
  CHECK(restored.bodies.at("plate").shape == first.bodies.at("plate").shape);
  d.params[d.params.size() - 2].value = "6 mm";
  const auto edited = evaluator.evaluate(source(d)).value();
  solid(edited, 6);
  CHECK(edited.kernel_calls == 2);
  CHECK(edited.cache_hits == 0);
  Evaluator cold(*kernel, {0, 0});
  solid(cold.evaluate(source(d)).value(), 6);
  evaluator.clear_cache();
  solid(first, 5);
}
