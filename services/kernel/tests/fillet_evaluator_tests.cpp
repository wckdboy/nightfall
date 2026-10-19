// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "evaluator.hpp"
#include "part_fixture.hpp"

#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <STEPControl_Reader.hxx>

#include <algorithm>
#include <numbers>
#include <sstream>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace nf;
using namespace nf::kernel_service;
using namespace nf::kernel_service::test;
using Catch::Matchers::WithinAbs;

namespace {
constexpr double pi = std::numbers::pi;
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
ir::Document rounded() {
  auto d = part(2);
  d.params.push_back({"radius", "2 mm", {}, {}, {}, {}});
  d.features.push_back(feature("round", "fillet", R"({
    "edges":{"edges_of":{"created_by":"base","role":"side","from":"outline/r"},
      "except":{"edges_of":{"any":[{"created_by":"base","role":"cap_start"},
                                      {"created_by":"base","role":"cap_end"}]}}},
    "radius":"radius"})"));
  return d;
}
void select(ir::Document& d, const char* text) {
  d.features.back().inputs = set(d.features.back().inputs, "edges", ir::json::parse(text).value());
}
double volume(double height, double radius) { return height * (200 - (4 - pi) * radius * radius); }
double area(double height, double radius) {
  return 2 * (200 - (4 - pi) * radius * radius) + height * (60 - 8 * radius + 2 * pi * radius);
}
void solid(const Evaluation& result, double height = 30, double radius = 2, double dx = 0,
           std::string_view producer = "round") {
  for (const auto& f : result.features)
    for (const auto& e : f.errors) UNSCOPED_INFO(e.pointer << ": " << e.message);
  REQUIRE(result.ok());
  const auto& body = result.bodies.at("plate");
  REQUIRE(body.shape);
  const auto& m = body.shape->measures();
  CHECK_THAT(m.volume, WithinAbs(volume(height, radius), 1e-6));
  CHECK_THAT(m.area, WithinAbs(area(height, radius), 1e-6));
  CHECK_THAT(m.centroid.x, WithinAbs(5 + dx, 1e-8));
  CHECK_THAT(m.centroid.y, WithinAbs(10, 1e-8));
  CHECK_THAT(m.centroid.z, WithinAbs(height / 2, 1e-8));
  CHECK(m.solids == 1);
  CHECK(m.faces == 10);
  CHECK(m.edges == 24);
  CHECK(std::ranges::count_if(body.names, [&](const auto& n) {
          return n.feature == producer && n.role == "fillet";
        }) == 4);
  for (const auto& name : body.names) CHECK(name.entity->owner() == body.shape);
}
void other(ir::Document& d, double width = 10) {
  d.features.insert(d.features.end() - 1,
                    {feature("other_sketch", "sketch", R"({"plane":{"datum":"xy"},"entities":[
     {"id":"r","type":"rect","corner":[50,0],"size":[10,20]}],"constraints":[]})"),
                     feature("other_base", "extrude", R"({"profile":{"sketch":"other_sketch"},
      "distance":30,"result":{"new":"other","body_type":"brep"}})")});
  auto entities = *d.features[2].inputs.find("entities")->if_array();
  entities[0] = set(entities[0], "size", ir::json::Array{width, width == 10 ? 20. : width});
  d.features[2].inputs = set(d.features[2].inputs, "entities", std::move(entities));
}
}  // namespace

TEST_CASE("document fillets apply analytic radii through warm caches and immutable edits",
          "[fillet-evaluator]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {96, 64 * 1024 * 1024});
  auto d = rounded();
  const auto first = evaluator.evaluate(source(d)).value();
  solid(first);
  const auto warm = evaluator.evaluate(source(d)).value();
  solid(warm);
  CHECK(warm.cache_hits == 3);
  CHECK(warm.bodies.at("plate").shape == first.bodies.at("plate").shape);
  for (const auto* radius : {"0.125 mm", "1 mm", "4 mm", "2 mm"}) {
    d.params[3].value = radius;
    solid(evaluator.evaluate(source(d)).value(), 30, std::stod(radius));
  }
  d.params[0].value = "40 mm";
  solid(evaluator.evaluate(source(d)).value(), 40);
  solid(first);
}

TEST_CASE("fillet cache rebinding preserves renamed producers evicted readers and unit equivalence",
          "[fillet-evaluator]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {96, 64 * 1024 * 1024});
  auto d = rounded();
  const auto first = evaluator.evaluate(source(d)).value();
  solid(first);
  d.features.back().id = "softened";
  const auto renamed = evaluator.evaluate(source(d)).value();
  solid(renamed, 30, 2, 0, "softened");
  CHECK(renamed.cache_hits == 3);
  CHECK(std::ranges::none_of(renamed.bodies.at("plate").names,
                             [](const auto& n) { return n.feature == "round"; }));
  d.units.length = "cm";
  auto entities = *d.features[0].inputs.find("entities")->if_array();
  entities[0] = set(entities[0], "size", ir::json::Array{1., 2.});
  d.features[0].inputs = set(d.features[0].inputs, "entities", std::move(entities));
  const auto equivalent = evaluator.evaluate(source(d)).value();
  solid(equivalent, 30, 2, 0, "softened");
  CHECK(equivalent.cache_hits == 3);
  evaluator.clear_cache();
  solid(evaluator.evaluate(source(d)).value(), 30, 2, 0, "softened");
  Evaluator tiny(*kernel, {1, 64 * 1024 * 1024});
  solid(tiny.evaluate(source(d)).value(), 30, 2, 0, "softened");
  solid(tiny.evaluate(source(d)).value(), 30, 2, 0, "softened");
  solid(first);
}

TEST_CASE("fillet edge unions and exclusions track unmodified selector-only bodies",
          "[fillet-evaluator]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {96, 64 * 1024 * 1024});
  auto d = rounded();
  other(d);
  const auto original = *d.features.back().inputs.find("edges");
  const auto excluded =
      ir::json::parse(R"({"edges_of":{"created_by":"other_base","role":"cap_end"}})").value();
  d.features.back().inputs =
      set(d.features.back().inputs, "edges",
          ir::json::Object{{"any", ir::json::Array{original, original, excluded}},
                           {"except", excluded}});
  const auto first = evaluator.evaluate(source(d)).value();
  solid(first);
  CHECK_THAT(first.bodies.at("other").shape->measures().volume, WithinAbs(6000, 1e-6));
  d.features[3].inputs = set(d.features[3].inputs, "distance", 40.);
  const auto changed = evaluator.evaluate(source(d)).value();
  solid(changed);
  CHECK(changed.features.back().hash != first.features.back().hash);
  CHECK_THAT(changed.bodies.at("other").shape->measures().volume, WithinAbs(8000, 1e-6));
  CHECK(changed.bodies.at("other").names.front().feature == "other_base");
}

TEST_CASE("fillet radius and selector diagnostics retire targets then recover",
          "[fillet-evaluator]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {96, 64 * 1024 * 1024});
  auto d = rounded();
  for (const auto* radius : {"0 mm", "-1 mm", "0.00000001 mm"}) {
    d.params[3].value = radius;
    const auto failed = evaluator.evaluate(source(d)).value();
    REQUIRE_FALSE(failed.ok());
    REQUIRE(failed.features.back().errors.size() == 1);
    CHECK(failed.features.back().errors.front().pointer == "/features/2/inputs/radius");
    CHECK_FALSE(failed.bodies.at("plate").shape);
  }
  d.params[3].value = "100 mm";
  CHECK_FALSE(evaluator.evaluate(source(d)).value().bodies.at("plate").shape);
  d = rounded();
  solid(evaluator.evaluate(source(d)).value());
  for (const auto* selection :
       {R"({"created_by":"base","role":"cap_end"})",
        R"({"edges_of":{"created_by":"base","role":"cap_end"},"expect":"one"})",
        R"({"edges_of":{"created_by":"base","role":"cap_end"},"where":{"near":[0,0,30]}})"}) {
    select(d, selection);
    const auto failed = evaluator.evaluate(source(d)).value();
    REQUIRE_FALSE(failed.ok());
    CHECK_FALSE(failed.bodies.at("plate").shape);
    CHECK(failed.features.back().errors.front().pointer.starts_with("/features/2/inputs/edges"));
  }
  solid(evaluator.evaluate(source(rounded())).value());
}

TEST_CASE("suppressed and rolled back fillets preserve executable original solids",
          "[fillet-evaluator]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {96, 64 * 1024 * 1024});
  auto d = rounded();
  d.features.back().suppressed = true;
  auto skipped = evaluator.evaluate(source(d)).value();
  REQUIRE(skipped.ok());
  CHECK_THAT(skipped.bodies.at("plate").shape->measures().volume, WithinAbs(6000, 1e-6));
  d.features.back().suppressed = false;
  d.history_end = "base";
  skipped = evaluator.evaluate(source(d)).value();
  REQUIRE(skipped.ok());
  CHECK_THAT(skipped.bodies.at("plate").shape->measures().volume, WithinAbs(6000, 1e-6));
  d.history_end.reset();
  solid(evaluator.evaluate(source(d)).value());
}

TEST_CASE("multi-body fillet failure publishes no partial solids while independent work continues",
          "[fillet-evaluator]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {96, 64 * 1024 * 1024});
  auto d = rounded();
  other(d, 0.5);
  select(d, R"({"any":[{"edges_of":{"created_by":"base","role":"cap_end"}},
                       {"edges_of":{"created_by":"other_base","role":"cap_end"}}]})");
  d.features.push_back(feature("independent", "extrude", R"({"profile":{"sketch":"outline"},
    "distance":5,"result":{"new":"unrelated","body_type":"brep"}})"));
  const auto failed = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(failed.ok());
  CHECK_FALSE(failed.bodies.at("plate").shape);
  CHECK_FALSE(failed.bodies.at("other").shape);
  CHECK_THAT(failed.bodies.at("unrelated").shape->measures().volume, WithinAbs(1000, 1e-6));
  d.params[3].value = "0.1 mm";
  const auto repaired = evaluator.evaluate(source(d)).value();
  REQUIRE(repaired.ok());
  CHECK(repaired.bodies.at("plate").shape);
  CHECK(repaired.bodies.at("other").shape);
}

TEST_CASE("fillet modified caps retain attachment ancestry after current placement and cache reuse",
          "[fillet-evaluator]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {96, 64 * 1024 * 1024});
  auto d = rounded();
  d.features.insert(d.features.end() - 1, part(3).features.back());
  d.features.push_back(feature("on_cap", "sketch", R"({"plane":{
    "created_by":"base","role":"cap_end","expect":"one"},"entities":[
    {"id":"r","type":"rect","corner":[2,2],"size":[1,1]}],"constraints":[]})"));
  d.features.push_back(feature("boss", "extrude", R"({"profile":{"sketch":"on_cap"},
    "distance":2,"result":{"new":"boss","body_type":"brep"}})"));
  for (int repeat = 0; repeat < 2; ++repeat) {
    const auto result = evaluator.evaluate(source(d)).value();
    solid(result, 30, 2, 3);
    CHECK_THAT(result.bodies.at("boss").shape->measures().volume, WithinAbs(2, 1e-6));
    CHECK_THAT(result.bodies.at("boss").shape->measures().centroid.z, WithinAbs(31, 1e-8));
    if (repeat) CHECK(result.cache_hits == d.features.size());
  }
}

TEST_CASE("retained document fillets export exact STEP and current indexed display meshes",
          "[fillet-evaluator]") {
  Evaluation retained;
  {
    auto kernel = kernel::make_occt_kernel().value();
    Evaluator evaluator(*kernel, {1, 64 * 1024 * 1024});
    retained = evaluator.evaluate(source(rounded())).value();
    solid(retained);
  }
  auto kernel = kernel::make_occt_kernel().value();
  const auto body = retained.bodies.at("plate").shape;
  const auto text = kernel->export_step({body}).value();
  CHECK(text.find("AP242") != std::string::npos);
  CHECK(text.find("CYLINDRICAL_SURFACE") != std::string::npos);
  CHECK(text.find("TESSELLATED") == std::string::npos);
  STEPControl_Reader reader;
  std::istringstream stream(text);
  REQUIRE(reader.ReadStream("rounded.step", stream) == IFSelect_RetDone);
  reader.SetSystemLengthUnit(1);
  REQUIRE(reader.TransferRoots() > 0);
  CHECK(BRepCheck_Analyzer(reader.OneShape()).IsValid());
  GProp_GProps v, a;
  BRepGProp::VolumeProperties(reader.OneShape(), v, 1e-9);
  BRepGProp::SurfaceProperties(reader.OneShape(), a, 1e-9);
  CHECK_THAT(v.Mass(), WithinAbs(volume(30, 2), 1e-5));
  CHECK_THAT(a.Mass(), WithinAbs(area(30, 2), 1e-5));
  const auto mesh = kernel->tessellate(body, {0.02, 0.15}).value();
  CHECK(mesh.faces.size() == 10);
  double mesh_volume = 0;
  const auto origin = mesh.vertices.front().position;
  for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
    const auto p = mesh.vertices.at(mesh.indices[i]).position - origin;
    const auto q = mesh.vertices.at(mesh.indices[i + 1]).position - origin;
    const auto r = mesh.vertices.at(mesh.indices[i + 2]).position - origin;
    mesh_volume += math::dot(p, math::cross(q, r)) / 6;
  }
  CHECK_THAT(mesh_volume, WithinAbs(volume(30, 2), volume(30, 2) * 0.01));
  for (const auto& face : mesh.faces) CHECK(face.face->owner() == body);
  solid(retained);
}
