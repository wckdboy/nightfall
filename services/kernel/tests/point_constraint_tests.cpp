// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "evaluator.hpp"
#include "model_inputs.hpp"
#include "part_fixture.hpp"

#include <algorithm>
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
Value at(math::Vec2 p) { return Array{p.x, p.y}; }
Value point(const char* id, math::Vec2 p) {
  return Object{{"id", id}, {"type", "point"}, {"at", at(p)}};
}
Value ref(const char* id, const char* role) { return Object{{"entity", id}, {"point", role}}; }
Value constraint(const char* type, Array entities, Value value = {}) {
  Object result{{"type", type}, {"entities", std::move(entities)}};
  if (!value.is_null()) result.push_back({"value", std::move(value)});
  return result;
}
Value sketch(Array entities, Array constraints = {}) {
  return Object{{"plane", Object{{"datum", "xy"}}},
                {"entities", std::move(entities)},
                {"constraints", std::move(constraints)}};
}
Array box(std::vector<math::Vec2> points = {{-5, -10}, {5, -10}, {5, 10}, {-5, 10}}) {
  Array result;
  for (std::size_t i = 0; i < points.size(); ++i)
    result.push_back(Object{{"id", std::string(1, static_cast<char>('a' + i))},
                            {"type", "line"},
                            {"start", at(points[i])},
                            {"end", at(points[(i + 1) % points.size()])}});
  return result;
}
std::expected<Sketch, ir::Error> prepare(Value inputs, ir::Units units = {}) {
  auto f = feature("s", "sketch", "{}");
  f.inputs = std::move(inputs);
  const auto evaluated = ir::FeatureInputs::compile(f).value().evaluate(units).value();
  return prepare_sketch(evaluated);
}
void rejects(Value inputs, const std::string& pointer, const std::string& message) {
  auto result = prepare(std::move(inputs));
  REQUIRE_FALSE(result);
  INFO(result.error().message);
  CHECK(result.error().code == "kernel");
  CHECK(result.error().pointer == "/inputs/constraints/0" + pointer);
  CHECK(result.error().message.find(message) != std::string::npos);
}
ir::Document point_part() {
  auto d = part();
  d.params.push_back({"width", "10 mm", {}, {}, {}, {}});
  d.params.push_back({"required", "10 mm", {}, {}, {}, {}});
  d.features[0].inputs = ir::json::parse(R"({"plane":{"datum":"xy"},"entities":[
    {"id":"a","type":"line","start":["-width / 2",-10],"end":["width / 2",-10]},
    {"id":"b","type":"line","start":["width / 2",-10],"end":["width / 2",10]},
    {"id":"c","type":"line","start":["width / 2",10],"end":["-width / 2",10]},
    {"id":"d","type":"line","start":["-width / 2",10],"end":["-width / 2",-10]},
    {"id":"m","type":"point","at":[0,-10]},
    {"id":"p","type":"point","at":["-width / 2 - 2 mm",0]},
    {"id":"q","type":"point","at":["-width / 2 + 2 mm",0]}],"constraints":[
    {"type":"coincident","entities":[{"entity":"a","point":"end"},{"entity":"b","point":"start"}]},
    {"type":"distance","entities":[{"entity":"a","point":"start"},{"entity":"a","point":"end"}],"value":"required"},
    {"type":"midpoint","entities":["m","a"]},
    {"type":"symmetric","entities":["p","q","d"]}]})")
                             .value();
  return d;
}
void solid(const Evaluation& result, double width) {
  for (const auto& f : result.features)
    for (const auto& e : f.errors) UNSCOPED_INFO(e.pointer << ": " << e.message);
  REQUIRE(result.ok());
  const auto& body = result.bodies.at("plate");
  const auto& m = body.shape->measures();
  CHECK(m.solids == 1);
  CHECK_THAT(m.volume, WithinAbs(600 * width, 1e-8));
  CHECK_THAT(m.area, WithinAbs(100 * width + 1200, 1e-8));
  CHECK_THAT(m.centroid.x, WithinAbs(3, 1e-8));
  CHECK_THAT(m.centroid.y, WithinAbs(0, 1e-8));
  CHECK_THAT(m.centroid.z, WithinAbs(15, 1e-8));
  REQUIRE(body.names.size() == 6);
  for (const auto& name : body.names) {
    CHECK(name.feature == "base");
    CHECK(name.entity->owner() == body.shape);
  }
}
}  // namespace

TEST_CASE("point-qualified references cannot crash whole-entity constraints",
          "[point-constraints][reference-safety]") {
  for (const auto* kind : {"horizontal", "vertical"})
    rejects(sketch(box(), {constraint(kind, {ref("a", "start")})}), "/entities/0/point", "entire");
  for (const auto* kind : {"parallel", "perpendicular", "equal"})
    rejects(sketch(box(), {constraint(kind, {"a", ref("b", "end")})}), "/entities/1/point",
            "entire");
  const auto circle =
      ir::json::parse(R"({"id":"c","type":"circle","center":[0,0],"radius":5})").value();
  rejects(sketch({circle}, {constraint("radius", {ref("c", "center")}, 5.0)}), "/entities/0/point",
          "entire");
}

TEST_CASE("standalone sketch points do not create filled profile curves", "[point-constraints]") {
  const auto only_points = prepare(sketch({point("p", {2, 3}), point("q", {-7, 11})}));
  REQUIRE(only_points);
  CHECK(only_points->paths.empty());
  CHECK(only_points->points.at("p") == math::Vec2{2, 3});
  CHECK(only_points->points.at("q") == math::Vec2{-7, 11});
  auto entities = box();
  entities.push_back(point("p", {0, 0}));
  entities.push_back(point("q", {100, 200}));
  const auto with_points = prepare(sketch(entities));
  REQUIRE(with_points);
  const auto plain = prepare(sketch(box())).value();
  REQUIRE(with_points->paths.size() == plain.paths.size());
  REQUIRE(with_points->paths[0].size() == plain.paths[0].size());
  for (std::size_t i = 0; i < plain.paths[0].size(); ++i)
    for (const double u : {0.0, 0.25, 0.5, 1.0})
      CHECK(with_points->paths[0][i].point(u) == plain.paths[0][i].point(u));
}

TEST_CASE("line endpoint references verify coincidence and distance", "[point-constraints]") {
  CHECK(
      prepare(sketch(box(), {constraint("coincident", {ref("a", "end"), ref("b", "start")}),
                             constraint("distance", {ref("a", "start"), ref("a", "end")}, "1 cm"),
                             constraint("distance", {ref("a", "end"), ref("a", "start")}, 10.0)})));
  rejects(sketch(box(), {constraint("coincident", {ref("a", "start"), ref("a", "end")})}), "",
          "coincident constraint is not satisfied");
  auto values = box();
  values.push_back(point("p", {5, -10}));
  CHECK(prepare(sketch(values, {constraint("coincident", {"p", ref("b", "start")})})));
  CHECK(prepare(sketch({point("p", {0, 0}), point("q", {3, 4})},
                       {constraint("distance", {"p", "q"}, "5 cm")}),
                {"cm", "rad", {}}));
}

TEST_CASE("midpoint and symmetry constraints have explicit point and axis roles",
          "[point-constraints]") {
  auto entities = box();
  entities.insert(entities.end(), {point("m", {0, -10}), point("p", {-7, 2}), point("q", {-3, 2})});
  CHECK(prepare(sketch(
      entities,
      {constraint("midpoint", {"m", "a"}), constraint("midpoint", {"a", "m"}),
       constraint("symmetric", {"p", "q", "d"}), constraint("symmetric", {"q", "p", "d"})})));
  rejects(sketch(entities, {constraint("midpoint", {"p", "a"})}), "",
          "constraint is not satisfied");
  rejects(sketch(entities, {constraint("symmetric", {"p", "q", "b"})}), "",
          "constraint is not satisfied");
}

TEST_CASE("point constraint distances use absolute millimetre tolerances", "[point-constraints]") {
  constexpr double tolerance = 1e-6;
  for (const double factor : {0.25, 4.0}) {
    const double error = factor * tolerance;
    const Array points{point("p", {0, 0}), point("q", {error, 0})};
    for (const auto* kind : {"coincident", "distance"}) {
      const auto c =
          constraint(kind, {"p", "q"}, std::string_view(kind) == "distance" ? Value(0.0) : Value{});
      CHECK(static_cast<bool>(prepare(sketch(points, {c}))) == (factor < 1));
    }
    auto entities = box();
    entities.insert(entities.end(),
                    {point("m", {error, -10}), point("p", {-7, 0}), point("q", {-3 + error, 0})});
    CHECK(static_cast<bool>(prepare(sketch(entities, {constraint("midpoint", {"m", "a"})}))) ==
          (factor < 1));
    CHECK(static_cast<bool>(prepare(
              sketch(entities, {constraint("symmetric", {"p", "q", "d"})}))) == (factor < 1));
  }
  rejects(sketch({point("p", {}), point("q", {3, 4})}, {constraint("distance", {"p", "q"}, -5.0)}),
          "/value", "nonnegative");
}

TEST_CASE("arc point references honor negative sweeps and wrapped start angles",
          "[point-constraints]") {
  auto entities = *ir::json::parse(R"([
    {"id":"arc","type":"arc","center":[3,4],"radius":2,"start_angle":810,"sweep_angle":-180},
    {"id":"chord","type":"line","start":[3,2],"end":[3,6]},
    {"id":"circle","type":"circle","center":[3,4],"radius":2},
    {"id":"ellipse","type":"ellipse","center":[3,4],"radii":[3,2],"rotation":45}])")
                       ->if_array();
  entities.insert(entities.end(),
                  {point("start", {3, 6}), point("end", {3, 2}), point("center", {3, 4})});
  Array constraints{constraint("coincident", {"start", ref("arc", "start")}),
                    constraint("coincident", {"end", ref("arc", "end")})};
  for (const auto* id : {"arc", "circle", "ellipse"})
    constraints.push_back(constraint("coincident", {"center", ref(id, "center")}));
  CHECK(prepare(sketch(entities, constraints)));
  rejects(sketch(entities, {constraint("coincident", {"center", ref("chord", "center")})}),
          "/entities/1/point", "not supported");
  rejects(sketch(entities, {constraint("coincident", {"start", ref("circle", "start")})}),
          "/entities/1/point", "not supported");
  rejects(sketch(entities, {constraint("coincident", {"center", "arc"})}), "/entities/1",
          "point reference");
}

TEST_CASE("point constraints survive rigid placement and reversed symmetry axes",
          "[point-constraints][property]") {
  std::mt19937_64 rng(20261021);
  std::uniform_real_distribution<double> angle(-3.0, 3.0), coordinate(-100.0, 100.0);
  for (int trial = 0; trial < 48; ++trial) {
    const double a = angle(rng), scale = trial % 2 ? 0.01 : 100;
    const math::Vec2 origin{coordinate(rng), coordinate(rng)};
    const auto transform = [&](math::Vec2 p) {
      return origin + math::Vec2{std::cos(a) * p.x - std::sin(a) * p.y,
                                 std::sin(a) * p.x + std::cos(a) * p.y} *
                          scale;
    };
    std::vector<math::Vec2> vertices{{-5, -10}, {5, -10}, {5, 10}, {-5, 10}};
    for (auto& v : vertices) v = transform(v);
    if (trial % 3) std::reverse(vertices.begin(), vertices.end());
    auto entities = box(vertices);
    entities.insert(entities.end(), {point("p", transform({-7, 0})), point("q", transform({-3, 0})),
                                     point("mid", (vertices[0] + vertices[1]) / 2)});
    CAPTURE(trial, scale, a);
    CHECK(prepare(sketch(
        entities, {constraint("symmetric", {"p", "q", "d"}), constraint("midpoint", {"mid", "a"}),
                   constraint("distance", {ref("a", "start"), ref("a", "end")}, 10 * scale)})));
  }
}

TEST_CASE("point constraint arithmetic reports overflow and preserves finite midpoints",
          "[point-constraints]") {
  rejects(sketch({point("p", {-1e308, 0}), point("q", {1e308, 0})},
                 {constraint("distance", {"p", "q"}, 1.0)}),
          "", "finite");
  auto entities = box({{1e308, 0}, {1e308 + 1e293, 0}, {1e308 + 1e293, 20}, {1e308, 20}});
  entities.push_back(point("mid", {1e308, 10}));
  CHECK(prepare(sketch(entities, {constraint("midpoint", {"mid", "d"})})));
  rejects(sketch({point("p", {})}, {constraint("coincident", {"p", ref("p", "start")})}),
          "/entities/1/point", "not supported");
  rejects(sketch({point("p", {})}, {constraint("coincident", {"p", ref("unknown", "center")})}),
          "/entities/1/entity", "unknown");
}

TEST_CASE("point-constrained solids fail dependent edits and restore cached geometry",
          "[point-constraints]") {
  auto d = point_part();
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {32, 16 * 1024 * 1024});
  const auto first = evaluator.evaluate(source(d)).value();
  solid(first, 10);
  d.params.back().value = "12 mm";
  const auto failed = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(failed.ok());
  CHECK(failed.kernel_calls == 0);
  REQUIRE_FALSE(failed.features[0].errors.empty());
  CHECK(failed.features[0].errors[0].pointer == "/features/0/inputs/constraints/1");
  CHECK_FALSE(failed.bodies.at("plate").shape);
  for (const std::size_t i : {1U, 2U}) {
    REQUIRE_FALSE(failed.features[i].errors.empty());
    CHECK(failed.features[i].errors[0].code == "input_failed");
  }
  d.params.back().value = "1 cm";
  const auto restored = evaluator.evaluate(source(d)).value();
  solid(restored, 10);
  CHECK(restored.kernel_calls == 0);
  CHECK(restored.cache_hits == 3);
  CHECK(restored.bodies.at("plate").shape == first.bodies.at("plate").shape);
  d.params[d.params.size() - 2].value = "12 mm";
  d.params.back().value = "12 mm";
  const auto edited = evaluator.evaluate(source(d)).value();
  solid(edited, 12);
  CHECK(edited.kernel_calls == 2);
  CHECK(edited.cache_hits == 0);
  Evaluator cold(*kernel, {0, 0});
  solid(cold.evaluate(source(d)).value(), 12);
  evaluator.clear_cache();
  solid(first, 10);
}

TEST_CASE("point-only sketches retain cache accounting and cannot extrude a solid",
          "[point-constraints]") {
  auto d = part(2);
  d.features[0].inputs =
      sketch({point("p", {0, 0}), point("q", {3, 4})}, {constraint("distance", {"p", "q"}, 5.0)});
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {32, 16 * 1024 * 1024});
  const auto result = evaluator.evaluate(source(d)).value();
  CHECK(result.features[0].status == FeatureStatus::kBuilt);
  CHECK(result.kernel_calls == 0);
  REQUIRE_FALSE(result.features[1].errors.empty());
  CHECK(result.features[1].errors[0].message == "profile has no filled regions");
  const auto point_usage = evaluator.cache_usage();
  CHECK(point_usage.entries == 1);
  evaluator.clear_cache();
  d.features.resize(1);
  d.features[0].inputs = sketch({});
  REQUIRE(evaluator.evaluate(source(d))->ok());
  CHECK(point_usage.estimated_bytes >=
        evaluator.cache_usage().estimated_bytes + 2 * sizeof(math::Vec2));
}

TEST_CASE("standalone point retention obeys the sketch work budget", "[point-constraints]") {
  Array points;
  for (int i = 0; i < 4096; ++i)
    points.push_back(point(("p" + std::to_string(i)).c_str(), {double(i), 0}));
  const auto allowed = prepare(sketch(points));
  REQUIRE(allowed);
  CHECK(allowed->points.size() == 4096);
  CHECK(allowed->paths.empty());
  points.push_back(point("extra", {}));
  const auto rejected = prepare(sketch(points));
  REQUIRE_FALSE(rejected);
  CHECK(rejected.error().pointer == "/inputs/entities/4096");
}
