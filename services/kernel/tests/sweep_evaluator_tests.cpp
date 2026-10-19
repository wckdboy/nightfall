// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "evaluator.hpp"
#include "part_fixture.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <numbers>
#include <set>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace nf;
using namespace nf::kernel_service;
using namespace nf::kernel_service::test;
using Catch::Matchers::WithinAbs;
namespace {
ir::Document sweep_part() {
  ir::Document d;
  d.params = {{"run", "30 mm", {}, {}, {}, {}},
              {"width", "4 mm", {}, {}, {}, {}},
              {"unused", 1.0, {}, {}, {}, {}}};
  d.features = {feature("profile", "sketch", R"({"plane":{"origin":[0,0,0],"normal":"x",
        "x_direction":"y"},"entities":[{"id":"rim","type":"rect",
        "corner":["-width/2",-1],"size":["width",2]}],"constraints":[]})"),
                feature("route", "sketch", R"({"plane":{"datum":"xy"},"entities":[
        {"id":"rail","type":"line","start":[0,0],"end":["run",0]}],"constraints":[]})"),
                feature("swept", "sweep", R"({"profile":{"sketch":"profile"},
        "path":{"sketch":"route","entity":"rail"},"result":{"new":"conduit","body_type":"brep"}})")};
  return d;
}
Evaluation evaluate(Evaluator& evaluator, const ir::Document& d) {
  const auto result = evaluator.evaluate(source(d));
  REQUIRE(result);
  std::string errors;
  for (const auto& f : result->features)
    for (const auto& e : f.errors) errors += e.pointer + ": " + e.message + "\n";
  INFO(errors);
  REQUIRE(result->ok());
  return *result;
}
void names(const BodyState& body, std::string_view sketch = "profile",
           std::string_view producer = "swept") {
  std::set<std::string> sources;
  for (const auto& name : body.names) {
    REQUIRE(name.entity);
    CHECK(name.entity->owner() == body.shape);
    if (name.feature == producer && name.role == "side") sources.insert(name.from);
  }
  CHECK(sources == std::set{std::string(sketch) + "/rim"});
}
ir::Document rename(ir::Document d, const std::string& before, const std::string& after) {
  auto text = source(d);
  std::size_t at = 0;
  while ((at = text.find(before, at)) != std::string::npos) {
    text.replace(at, before.size(), after);
    at += after.size();
  }
  return ir::read_document(text).value();
}
}  // namespace

TEST_CASE("swept conduit example has independent spline tube mass across parameter edits",
          "[sweep-evaluator][sweep-showcase]") {
  const auto path = std::filesystem::path(NF_REFERENCE_PARTS_DIR).parent_path() / "modeling" /
                    "swept_conduit.nightfall";
  std::ifstream file(path);
  REQUIRE(file);
  auto d = ir::read_document(std::string{std::istreambuf_iterator<char>{file}, {}}).value();
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {64, 64 * 1024 * 1024});
  for (const auto& [reach, lift] : {std::pair{180.0, 100.0}, {220.0, 120.0}}) {
    double length = 0;
    constexpr int n = 8192;
    for (int i = 0; i <= n; ++i) {
      const double t = static_cast<double>(i) / n, u = 1 - t;
      const double dx = reach * (4 * u * u + 2 * u * t + 4 * t * t) / 3;
      const double dy = 6 * lift * u * t;
      length += (i == 0 || i == n ? 1 : i % 2 ? 4 : 2) * std::hypot(dx, dy);
    }
    length /= 3 * n;
    d.params[0].value = ir::json::format_number(reach) + " mm";
    d.params[1].value = ir::json::format_number(lift) + " mm";
    const auto result = evaluate(evaluator, d);
    const auto& body = result.bodies.at("conduit");
    const auto& m = body.shape->measures();
    CHECK_THAT(m.volume, WithinAbs(std::numbers::pi * 75 * length, 1e-3));
    CHECK_THAT(m.area, WithinAbs(50 * std::numbers::pi * length + 150 * std::numbers::pi, 1e-3));
    CHECK(m.solids == 1);
    const auto mesh = kernel->tessellate(body.shape, {});
    REQUIRE(mesh);
    CHECK(mesh->faces.size() == m.faces);
    CHECK_FALSE(mesh->indices.empty());
    const auto step = kernel->export_step({body.shape});
    REQUIRE(step);
    CHECK(step->find("ISO-10303-21;") != std::string::npos);
    CHECK(evaluate(evaluator, d).kernel_calls == 0);
    for (const auto& name : body.names) CHECK(name.entity->owner() == body.shape);
  }
}

TEST_CASE("sweep documents publish independent solid mass and current profile ancestry",
          "[sweep-evaluator]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {64, 64 * 1024 * 1024});
  const auto d = sweep_part();
  const auto original = source(d);
  const auto result = evaluate(evaluator, d);
  const auto& body = result.bodies.at("conduit");
  CHECK_THAT(body.shape->measures().volume, WithinAbs(240, 1e-6));
  CHECK_THAT(body.shape->measures().area, WithinAbs(376, 1e-6));
  names(body);
  CHECK(result.kernel_calls == 1);
  const auto warm = evaluate(evaluator, d);
  CHECK(warm.kernel_calls == 0);
  CHECK(warm.bodies.at("conduit").shape == body.shape);
  CHECK(source(d) == original);
}

TEST_CASE("sweep path and profile edits invalidate the correct dependencies and retain readers",
          "[sweep-evaluator]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {32, 64 * 1024 * 1024});
  auto d = sweep_part();
  const auto first = evaluate(evaluator, d);
  const auto original = first.bodies.at("conduit").shape;
  d.params[0].value = "40 mm";
  const auto path_edit = evaluate(evaluator, d);
  CHECK(path_edit.kernel_calls == 1);
  CHECK(path_edit.features[0].hash == first.features[0].hash);
  CHECK(path_edit.features[1].hash != first.features[1].hash);
  CHECK_THAT(path_edit.bodies.at("conduit").shape->measures().volume, WithinAbs(320, 1e-6));
  d.params[1].value = "6 mm";
  const auto profile_edit = evaluate(evaluator, d);
  CHECK(profile_edit.kernel_calls == 1);
  CHECK(profile_edit.features[1].hash == path_edit.features[1].hash);
  CHECK_THAT(profile_edit.bodies.at("conduit").shape->measures().volume, WithinAbs(480, 1e-6));
  d.params[2].value = 2.0;
  CHECK(evaluate(evaluator, d).kernel_calls == 0);
  CHECK(evaluate(evaluator, sweep_part()).bodies.at("conduit").shape == original);
  evaluator.set_cache_limits({0, 0});
  const auto cold = evaluate(evaluator, sweep_part());
  CHECK(cold.kernel_calls == 1);
  CHECK(cold.bodies.at("conduit").shape != original);
  CHECK_THAT(original->measures().volume, WithinAbs(240, 1e-6));
}

TEST_CASE("sweep documents support exact arc and spline paths", "[sweep-evaluator]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {64, 64 * 1024 * 1024});
  auto d = sweep_part();
  SECTION("quarter circular bend") {
    d.features[0] = feature("profile", "sketch", R"({"plane":{"origin":[20,0,0],"normal":"y",
      "x_direction":"x"},"entities":[{"id":"rim","type":"circle","center":[0,0],"radius":2}],"constraints":[]})");
    d.features[1] = feature("route", "sketch", R"({"plane":{"datum":"xy"},"entities":[
      {"id":"rail","type":"arc","center":[0,0],"radius":20,"start_angle":0,"sweep_angle":90}],"constraints":[]})");
    const auto result = evaluate(evaluator, d);
    CHECK_THAT(result.bodies.at("conduit").shape->measures().volume,
               WithinAbs(40 * std::numbers::pi * std::numbers::pi, 1e-5));
    names(result.bodies.at("conduit"));
  }
  SECTION("quadratic spline path") {
    d.features[0] = feature("profile", "sketch", R"({"plane":{"origin":[0,0,0],"normal":"x",
      "x_direction":"y"},"entities":[{"id":"rim","type":"circle","center":[0,0],"radius":0.5}],"constraints":[]})");
    d.features[1] = feature("route", "sketch", R"({"plane":{"datum":"xy"},"entities":[
      {"id":"rail","type":"spline","degree":2,"knots":[0,0,0,1,1,1],
       "control_points":[[0,0],[10,0],[20,10]]}],"constraints":[]})");
    const auto result = evaluate(evaluator, d);
    CHECK_THAT(result.bodies.at("conduit").shape->measures().volume,
               WithinAbs(std::numbers::pi / 4 * 10 * (std::sqrt(2.0) + std::asinh(1.0)), 1e-4));
    names(result.bodies.at("conduit"));
  }
}

TEST_CASE("sweep names rebind current producer profile and path IDs and support cap selectors",
          "[sweep-evaluator]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {64, 64 * 1024 * 1024});
  auto d = sweep_part();
  d.features.push_back(feature("pad_profile", "sketch",
                               R"({"plane":{"created_by":"swept","role":"cap_end","expect":"one"},
    "entities":[{"id":"r","type":"rect","corner":[-0.5,-0.5],"size":[1,1]}],"constraints":[]})"));
  d.features.push_back(feature(
      "pad", "extrude",
      R"({"profile":{"sketch":"pad_profile"},"distance":2,"result":{"new":"pad","body_type":"brep"}})"));
  const auto original = evaluate(evaluator, d);
  d = rename(d, "swept", "transported");
  const auto producer = evaluate(evaluator, d);
  CHECK(producer.bodies.at("conduit").shape == original.bodies.at("conduit").shape);
  CHECK(producer.features[2].hash == original.features[2].hash);
  names(producer.bodies.at("conduit"), "profile", "transported");
  d.features[0].id = "section";
  d.features[2].inputs = ir::json::Object{{"profile", ir::json::Object{{"sketch", "section"}}},
                                          {"path", *d.features[2].inputs.find("path")},
                                          {"result", *d.features[2].inputs.find("result")}};
  d = rename(d, "route", "spine");
  d = rename(d, "rail", "guide");
  const auto result = evaluate(evaluator, d);
  names(result.bodies.at("conduit"), "section", "transported");
  CHECK_THAT(result.bodies.at("pad").shape->measures().volume, WithinAbs(2, 1e-6));
  // The face-frame query deliberately runs even when all geometry is cached (RFC 0007).
  CHECK(evaluate(evaluator, d).kernel_calls == 1);
}

TEST_CASE("sweep result modes publish target and tool ancestry atomically", "[sweep-evaluator]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {64, 64 * 1024 * 1024});
  for (const auto& [mode, volume] :
       {std::pair{"join", 4000.0}, {"cut", 3760.0}, {"intersect", 240.0}}) {
    auto d = sweep_part();
    d.features.insert(
        d.features.begin(),
        {feature("stock_profile", "sketch",
                 R"({"plane":{"origin":[0,0,-5],"normal":"z","x_direction":"x"},
        "entities":[{"id":"r","type":"rect","corner":[-5,-5],"size":[40,10]}],"constraints":[]})"),
         feature(
             "stock", "extrude",
             R"({"profile":{"sketch":"stock_profile"},"distance":10,"result":{"new":"block","body_type":"brep"}})")});
    d.features.back().inputs =
        ir::json::Object{{"profile", *d.features.back().inputs.find("profile")},
                         {"path", *d.features.back().inputs.find("path")},
                         {"result", ir::json::Object{{mode, "block"}}}};
    const auto result = evaluate(evaluator, d);
    const auto& body = result.bodies.at("block");
    CHECK_THAT(body.shape->measures().volume, WithinAbs(volume, 1e-5));
    for (const auto& name : body.names) CHECK(name.entity->owner() == body.shape);
    CHECK(evaluate(evaluator, d).kernel_calls == 0);
  }
}

TEST_CASE("sweep unsupported path forms suppression and rollback recover without stale geometry",
          "[sweep-evaluator]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {64, 64 * 1024 * 1024});
  auto d = sweep_part();
  std::string pointer = "/features/2/inputs/path/entity";
  SECTION("selector paths are explicit errors") {
    pointer = "/features/2/inputs/path";
    d.features.back().inputs =
        ir::json::Object{{"profile", *d.features.back().inputs.find("profile")},
                         {"path", ir::json::Object{{"created_by", "profile"}, {"role", "side"}}},
                         {"result", *d.features.back().inputs.find("result")}};
  }
  SECTION("closed path") {
    d.features[1] = feature("route", "sketch", R"({"plane":{"datum":"xy"},"entities":[
      {"id":"rail","type":"circle","center":[0,0],"radius":20}],"constraints":[]})");
  }
  SECTION("point path") {
    d.features[1] = feature("route", "sketch", R"({"plane":{"datum":"xy"},"entities":[
      {"id":"rail","type":"point","at":[0,0]}],"constraints":[]})");
  }
  SECTION("suppressed path") {
    d.features[1].suppressed = true;
    pointer = "/features/2/inputs/path/sketch";
  }
  SECTION("suppressed profile") {
    d.features[0].suppressed = true;
    pointer = "/features/2/inputs/profile/sketch";
  }
  SECTION("rolled back sweep") {
    d.history_end = "route";
    const auto result = evaluate(evaluator, d);
    CHECK(result.features.back().status == FeatureStatus::kRolledBack);
    CHECK(result.bodies.empty());
    return;
  }
  const auto failed = evaluator.evaluate(source(d));
  REQUIRE(failed);
  CHECK_FALSE(failed->ok());
  CHECK_FALSE(failed->features.back().errors.empty());
  REQUIRE_FALSE(failed->features.back().errors.empty());
  CHECK(failed->features.back().errors.front().pointer == pointer);
  CHECK_FALSE(failed->bodies.at("conduit").shape);
  const auto restored = evaluate(evaluator, sweep_part());
  CHECK_THAT(restored.bodies.at("conduit").shape->measures().volume, WithinAbs(240, 1e-6));
}
