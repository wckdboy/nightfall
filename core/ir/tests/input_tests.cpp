// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

#include "nf/ir/hash.hpp"
#include "nf/ir/inputs.hpp"

using namespace nf::ir;

namespace {
Feature feature(std::string type, std::string_view inputs) {
  Feature f;
  f.id = "test";
  f.type = std::move(type);
  const auto parsed = json::parse(inputs);
  REQUIRE(parsed.has_value());
  f.inputs = *parsed;
  return f;
}
FeatureInputs compiled(const Feature& f) {
  const auto result = FeatureInputs::compile(f);
  INFO((result ? "ok" : result.error().front().message));
  REQUIRE(result.has_value());
  return *result;
}
json::Value evaluated(const Feature& f, const Units& units = {},
                      const ParameterValues& params = {}) {
  const auto result = compiled(f).evaluate(units, params);
  INFO((result ? "ok" : result.error().front().message));
  REQUIRE(result.has_value());
  return *result;
}
void number(const json::Value& v, double expected) {
  REQUIRE(v.if_number());
  CHECK(std::abs(*v.if_number() - expected) <= 1e-12 * std::max(1.0, std::abs(expected)));
}
void error(const std::expected<json::Value, Errors>& result, std::string_view code,
           std::string_view pointer) {
  REQUIRE_FALSE(result.has_value());
  CHECK(std::ranges::any_of(result.error(),
                            [&](const auto& e) { return e.code == code && e.pointer == pointer; }));
}
}  // namespace

TEST_CASE("IR feature inputs: values take field dimensions and preserve authored inputs",
          "[ir][inputs]") {
  const auto f = feature(
      "extrude",
      R"json({"profile":{"sketch":"s","regions":[[1,"width"]]},"distance":"width*2","direction":["1",0,0],"draft":90,"result":{"new":"body","body_type":"brep"}})json");
  Units units;
  units.length = "cm";
  const auto before = json::canonical(f.inputs);
  const auto program = compiled(f);
  const auto out = program.evaluate(units, {{"width", {20, kLength}}});
  REQUIRE(out.has_value());
  number(*out->find("distance"), 40);
  number(*out->find("draft"), std::numbers::pi / 2);
  const auto& region = out->find("profile")->find("regions")->if_array()->front();
  number(region.if_array()->at(0), 10);
  number(region.if_array()->at(1), 20);
  number(out->find("direction")->if_array()->at(0), 1);
  CHECK(json::canonical(f.inputs) == before);
  const auto again = program.evaluate(units, {{"width", {30, kLength}}});
  REQUIRE(again.has_value());
  number(*again->find("distance"), 60);
  error(program.evaluate(units, {{"width", {20, kAngle}}}), "unit", "/inputs/distance");
}

TEST_CASE("IR feature inputs: hashes depend on evaluated geometry values", "[ir][inputs][hash]") {
  auto a = feature(
      "extrude",
      R"json({"profile":{"sketch":"s"},"distance":"wall*2","result":{"new":"body","body_type":"brep"},"x-note":{"text":"invalid ( expression"}})json");
  auto b = a;
  b.inputs =
      feature(
          "extrude",
          R"json({"result":{"body_type":"brep","new":"body"},"distance":"6mm","profile":{"sketch":"s"}})json")
          .inputs;
  b.name = "Renamed for the drawing";
  const auto upstream = sha256("sketch");
  const auto hash = [&](const Feature& f, const ParameterValues& params) {
    return feature_hash("1", f.type, evaluated(f, {}, params), {&upstream, 1});
  };
  CHECK(hash(a, {{"wall", {3, kLength}}, {"unused", {7, kScalar}}}) == hash(b, {}));
  CHECK(hash(a, {{"wall", {3, kLength}}, {"unused", {9, kScalar}}}) == hash(b, {}));
  CHECK(hash(a, {{"wall", {4, kLength}}}) != hash(b, {}));
  CHECK_FALSE(evaluated(a, {}, {{"wall", {3, kLength}}}).find("x-note"));
}

TEST_CASE("IR feature inputs: sketch geometry has explicit length angle and scalar fields",
          "[ir][inputs]") {
  const auto f = feature(
      "sketch", R"json({"plane":{"origin":[1,2,3],"normal":[0,0,"1"],"x_direction":"x"},"entities":[
    {"id":"p","type":"point","at":[1,2]},
    {"id":"l","type":"line","start":[0,0],"end":[1,2]},
    {"id":"a","type":"arc","center":[0,0],"radius":2,"start_angle":90,"sweep_angle":"180deg"},
    {"id":"c","type":"circle","center":[0,0],"radius":3},
    {"id":"e","type":"ellipse","center":[0,0],"radii":[2,1],"rotation":90},
    {"id":"r","type":"rect","corner":[0,0],"size":[3,2]},
    {"id":"g","type":"polygon","points":[[0,0],[2,0],[0,2]]},
    {"id":"n","type":"spline","degree":"1","knots":[0,0,1,1],"control_points":[[0,0],[1,2]],"weights":["1",1]},
    {"id":"v","type":"svg","reference":"drawing","placement":{"origin":[1,2],"scale":"2","rotation":90}}
  ],"constraints":[{"type":"distance","entities":["p","l"],"value":2},{"type":"angle","entities":["l","a"],"value":90},{"type":"radius","entities":["c"],"value":"3cm"}]})json");
  Units units;
  units.length = "cm";
  const auto out = evaluated(f, units);
  number(out.find("plane")->find("origin")->if_array()->front(), 10);
  number(out.find("plane")->find("normal")->if_array()->back(), 1);
  const auto& entities = *out.find("entities")->if_array();
  number(entities[0].find("at")->if_array()->front(), 10);
  number(entities[1].find("end")->if_array()->back(), 20);
  number(*entities[2].find("radius"), 20);
  number(*entities[2].find("start_angle"), std::numbers::pi / 2);
  number(*entities[2].find("sweep_angle"), std::numbers::pi);
  number(*entities[3].find("radius"), 30);
  number(*entities[4].find("rotation"), std::numbers::pi / 2);
  number(entities[5].find("size")->if_array()->front(), 30);
  number(entities[6].find("points")->if_array()->at(1).if_array()->front(), 20);
  number(*entities[7].find("degree"), 1);
  number(entities[7].find("knots")->if_array()->back(), 1);
  number(entities[7].find("control_points")->if_array()->back().if_array()->back(), 20);
  number(*entities[8].find("placement")->find("scale"), 2);
  number(*entities[8].find("placement")->find("rotation"), std::numbers::pi / 2);
  const auto& constraints = *out.find("constraints")->if_array();
  number(*constraints[0].find("value"), 20);
  number(*constraints[1].find("value"), std::numbers::pi / 2);
  number(*constraints[2].find("value"), 30);
}

TEST_CASE(
    "IR feature inputs: recursive selectors evaluate filters without treating IDs as expressions",
    "[ir][inputs]") {
  const auto f = feature(
      "fillet",
      R"json({"radius":"r","edges":{"edges_of":{"any":[{"created_by":"base","role":"end","where":{"near":[1,"r",0],"normal":[0,0,"1"],"radius":"r"}},{"faces_of":"body"}],"except":{"created_by":"other"}},"where":{"parallel_to":"z"},"expect":"some"}})json");
  const auto out = evaluated(f, {}, {{"r", {4, kLength}}});
  number(*out.find("radius"), 4);
  const auto& selector = out.find("edges")->find("edges_of")->find("any")->if_array()->front();
  CHECK(*selector.find("created_by")->if_string() == "base");
  number(selector.find("where")->find("near")->if_array()->at(1), 4);
  number(*selector.find("where")->find("radius"), 4);
}

TEST_CASE("IR feature inputs: counts and spline degrees are bounded exact integers",
          "[ir][inputs]") {
  const auto f =
      feature("pattern_linear",
              R"json({"bodies":["body"],"count":"n","spacing":"2mm","direction":"x"})json");
  const auto program = compiled(f);
  for (const double n : {0.0, -1.0, 1.5, 1e20})
    error(program.evaluate({}, {{"n", {n, kScalar}}}), "bounds", "/inputs/count");
  error(program.evaluate({}, {{"n", {2, kLength}}}), "unit", "/inputs/count");
  number(*program.evaluate({}, {{"n", {3, kScalar}}})->find("count"), 3);
  const auto spline = compiled(feature(
      "sketch",
      R"json({"plane":{"datum":"xy"},"entities":[{"id":"s","type":"spline","degree":"n","knots":[0,0,1,1],"control_points":[[0,0],[1,1]]}],"constraints":[]})json"));
  for (const double n : {0.0, 1.5, 10.0})
    error(spline.evaluate({}, {{"n", {n, kScalar}}}), "bounds", "/inputs/entities/0/degree");
}

TEST_CASE("IR feature inputs: independent diagnostics retain field pointers and source offsets",
          "[ir][inputs]") {
  const auto program = compiled(feature(
      "extrude",
      R"json({"profile":{"sketch":"s"},"distance":"missing","draft":"1/0","direction":["2mm",0,1],"result":{"new":"body","body_type":"brep"}})json"));
  const auto out = program.evaluate();
  REQUIRE_FALSE(out.has_value());
  REQUIRE(out.error().size() == 3);
  error(out, "unknown_id", "/inputs/distance");
  error(out, "bounds", "/inputs/draft");
  error(out, "unit", "/inputs/direction/0");
  CHECK(out.error().front().offset == 0);
  const auto bad = FeatureInputs::compile(feature(
      "extrude",
      R"json({"profile":{"sketch":"s"},"distance":"2 + )","draft":"(","result":{"new":"body","body_type":"brep"}
})json"));
  REQUIRE_FALSE(bad.has_value());
  REQUIRE(bad.error().size() == 2);
  CHECK(bad.error()[0].pointer == "/inputs/distance");
  CHECK(bad.error()[0].offset == 4);
  CHECK(bad.error()[1].pointer == "/inputs/draft");
  CHECK_FALSE(FeatureInputs::compile(feature("transform", R"json({"bodies":["body"]})json")));
}

TEST_CASE("IR feature inputs: dynamic code is refused until its evaluator can bind parameters",
          "[ir][inputs]") {
  const auto script = FeatureInputs::compile(feature(
      "script",
      R"json({"source":"return nf.box(args.w)","args":{"w":"width"},"result":{"new":"body","body_type":"brep"}})json"));
  REQUIRE_FALSE(script.has_value());
  CHECK(script.error().front().code == "kernel");
  const auto equation = FeatureInputs::compile(feature(
      "sketch",
      R"json({"plane":{"datum":"xy"},"entities":[{"id":"e","type":"equation_curve","x":"width*t","y":"t*t","t0":0,"t1":1,"tolerance":"0.01mm"}],"constraints":[]})json"));
  REQUIRE_FALSE(equation.has_value());
  CHECK(equation.error().front().pointer == "/inputs/entities/0/type");
}

TEST_CASE("IR feature inputs: generated edits preserve equivalent content hashes",
          "[ir][inputs][property]") {
  const auto program =
      compiled(feature("transform", R"json({"bodies":["body"],"translation":["a*n/n",0,0]})json"));
  std::mt19937 rng(20261017);
  for (int i = 0; i < 250; ++i) {
    const auto a = static_cast<double>(1 + rng() % 10000), n = static_cast<double>(1 + rng() % 100);
    const auto out = program.evaluate({}, {{"a", {a, kLength}}, {"n", {n, kScalar}}});
    REQUIRE(out.has_value());
    const json::Value expected =
        json::Object{{"bodies", json::Array{"body"}}, {"translation", json::Array{a, 0.0, 0.0}}};
    CHECK(feature_hash("1", "transform", *out, {}) == feature_hash("1", "transform", expected, {}));
  }
}

TEST_CASE("IR feature inputs: every supported operation normalizes its numeric fields",
          "[ir][inputs]") {
  const std::pair<const char*, const char*> cases[]{
      {"revolve",
       R"json({"profile":{"sketch":"s"},"axis":{"origin":["2mm",0,0],"direction":["1",0,0]},"angle":"1rad","result":{"new":"b","body_type":"brep"}})json"},
      {"sweep",
       R"json({"profile":{"sketch":"s","regions":[["2mm",0]]},"path":{"sketch":"s","entity":"l"},"result":{"join":"b"}})json"},
      {"loft",
       R"json({"profiles":[{"sketch":"s","regions":[["2mm",0]]},{"sketch":"t"}],"rails":[{"created_by":"rail","where":{"near":["2mm",0,0]}}],"result":{"new":"b","body_type":"brep"}})json"},
      {"chamfer", R"json({"edges":{"created_by":"base"},"distance":"2mm","angle":"1rad"})json"},
      {"shell", R"json({"body":"b","faces":{"faces_of":"b"},"thickness":"2mm"})json"},
      {"hole",
       R"json({"face":{"created_by":"base"},"positions":[{"sketch":"s","entity":"p"}],"diameter":"2mm","depth":"2mm","counterbore":{"diameter":"2mm","depth":"2mm"}})json"},
      {"hole",
       R"json({"face":{"created_by":"base"},"positions":[{"sketch":"s","entity":"p"}],"diameter":"2mm","depth":"2mm","countersink":{"diameter":"2mm","angle":"1rad"}})json"},
      {"boolean", R"json({"op":"subtract","target":"b","tools":["tool"],"keep_tools":true})json"},
      {"pattern_circular",
       R"json({"features":["base"],"count":"2","angle":"1rad","axis":{"origin":["2mm",0,0],"direction":"z"}})json"},
      {"mirror",
       R"json({"bodies":["b"],"plane":{"origin":["2mm",0,0],"normal":"x","x_direction":"z"}})json"},
      {"transform",
       R"json({"bodies":["b"],"translation":["2mm",0,0],"rotation":{"axis":{"origin":["2mm",0,0],"direction":"z"},"angle":"1rad"}})json"},
      {"import", R"json({"reference":"file","result":{"new":"b","body_type":"brep"}})json"}};
  for (const auto& [type, inputs] : cases) {
    INFO(type);
    const auto out = json::canonical(evaluated(feature(type, inputs)));
    for (const auto* expression : {"\"2mm\"", "\"1rad\"", "\"2\"", "\"1\""})
      CHECK(out.find(expression) == std::string::npos);
  }
}

TEST_CASE("IR feature inputs: diagnostics and programmatically built inputs are bounded",
          "[ir][inputs]") {
  json::Array entities;
  for (int i = 0; i < 150; ++i)
    entities.emplace_back(json::Object{{"id", "c" + std::to_string(i)},
                                       {"type", "circle"},
                                       {"center", json::Array{0.0, 0.0}},
                                       {"radius", "missing"}});
  Feature f;
  f.id = "s";
  f.type = "sketch";
  f.inputs = json::Object{{"plane", json::Object{{"datum", "xy"}}},
                          {"entities", std::move(entities)},
                          {"constraints", json::Array{}}};
  const auto result = compiled(f).evaluate();
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().size() == 128);
  auto inputs = *f.inputs.if_object();
  inputs.push_back({"entities", json::Array{}});
  f.inputs = inputs;
  REQUIRE_FALSE(FeatureInputs::compile(f).has_value());
  CHECK(FeatureInputs::compile(f).error().front().code == "parse");
  f.inputs = json::Object{{"x-bytes", std::string(kMaxDocumentBytes + 1, 'a')}};
  CHECK_FALSE(FeatureInputs::compile(f));
  f.inputs = json::Object{{"x-text", std::string(1, static_cast<char>(0xff))}};
  CHECK_FALSE(FeatureInputs::compile(f));
  json::Value nested;
  for (int i = 0; i < json::kMaxDepth + 1; ++i) nested = json::Array{std::move(nested)};
  f.inputs = json::Object{{"x-depth", std::move(nested)}};
  CHECK_FALSE(FeatureInputs::compile(f));
}
