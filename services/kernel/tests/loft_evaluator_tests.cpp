// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "evaluator.hpp"
#include "part_fixture.hpp"
#include "snapshot.hpp"

#include <BRepCheck_Analyzer.hxx>
#include <STEPControl_Reader.hxx>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <numbers>
#include <set>
#include <sstream>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace nf;
using namespace nf::kernel_service;
using namespace nf::kernel_service::test;
using Catch::Matchers::WithinAbs;
namespace {
ir::Document loft_part() {
  ir::Document d;
  d.params = {{"height", "30 mm", {}, {}, {}, {}},
              {"top_scale", 2.0, {}, {}, {}, {}},
              {"unused", 1.0, {}, {}, {}, {}}};
  d.features = {
      feature("section_a", "sketch", R"({"plane":{"datum":"xy"},"entities":[
        {"id":"outline","type":"rect","corner":[-5,-4],"size":[10,8]}],"constraints":[]})"),
      feature("section_b", "sketch", R"({"plane":{"origin":[0,0,"height"],"normal":"z",
        "x_direction":"x"},"entities":[{"id":"outline","type":"rect",
        "corner":["-5 mm * top_scale","-4 mm * top_scale"],
        "size":["10 mm * top_scale","8 mm * top_scale"]}],"constraints":[]})"),
      feature("blend", "loft", R"({"profiles":[{"sketch":"section_a"},{"sketch":"section_b"}],
        "result":{"new":"link","body_type":"brep"}})")};
  return d;
}
Evaluation evaluate(Evaluator& evaluator, const ir::Document& d) {
  auto result = evaluator.evaluate(source(d));
  REQUIRE(result);
  std::string errors;
  for (const auto& f : result->features)
    for (const auto& e : f.errors) errors += e.pointer + ": " + e.message + "\n";
  INFO(errors);
  REQUIRE(result->ok());
  return *result;
}
void frustum_mass(const BodyState& body) {
  REQUIRE(body.shape);
  const auto& m = body.shape->measures();
  CHECK_THAT(m.volume, WithinAbs(5600, 1e-5));
  CHECK_THAT(m.area, WithinAbs(400 + 30 * std::sqrt(916) + 24 * std::sqrt(925), 1e-5));
  CHECK_THAT(m.centroid.z, WithinAbs(30.0 * 17 / 28, 1e-7));
  CHECK(m.faces == 6);
  CHECK(m.solids == 1);
}
void provenance(const BodyState& body, std::set<std::string> expected,
                std::string_view producer = "blend") {
  REQUIRE(body.shape);
  std::set<std::string> sources;
  for (const auto& name : body.names) {
    REQUIRE(name.entity);
    CHECK(name.entity->owner() == body.shape);
    if (name.feature == producer && name.role == "side") sources.insert(name.from);
  }
  CHECK(sources == expected);
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
ir::Document link_example() {
  const auto path = std::filesystem::path(NF_REFERENCE_PARTS_DIR).parent_path() / "modeling" /
                    "lofted_link.nightfall";
  std::ifstream file(path);
  REQUIRE(file);
  return ir::read_document(std::string{std::istreambuf_iterator<char>{file}, {}}).value();
}
std::pair<double, double> link_volume_center(double height, double waist, double bore,
                                             double flange) {
  const std::array<double, 3> a{32, 88 * waist - 120, 112 - 88 * waist},
      b{20, 48 * waist - 76, 72 - 48 * waist};
  double section = 0, moment = 0;
  for (std::size_t i = 0; i < a.size(); ++i)
    for (std::size_t j = 0; j < b.size(); ++j) {
      section += a[i] * b[j] / static_cast<double>(i + j + 1);
      moment += a[i] * b[j] / static_cast<double>(i + j + 2);
    }
  const double volume =
      height * section + 1200 * flange - bore * bore * (height + flange) - 25 * flange;
  const double z = height * height * moment - 600 * flange * flange -
                   bore * bore * (height * height - flange * flange) / 2 + 12.5 * flange * flange;
  return {std::numbers::pi * volume, z / volume};
}
double link_area(double height, double waist, double bore, double flange, int subdivisions) {
  // Independent positive surface metric of (a(t)cos(theta), b(t)sin(theta), height*t),
  // with quadratic interpolation through all three authored elliptic sections.
  const double a1 = 88 * waist - 120, a2 = 112 - 88 * waist, b1 = 48 * waist - 76,
               b2 = 72 - 48 * waist;
  double side = 0, perimeter = 0;
  const auto weight = [=](int i) { return i == 0 || i == subdivisions ? 1 : (i % 2 ? 4 : 2); };
  for (int i = 0; i <= subdivisions; ++i) {
    const double theta = 2 * std::numbers::pi * i / subdivisions;
    const double c2 = std::pow(std::cos(theta), 2), s2 = std::pow(std::sin(theta), 2);
    perimeter += weight(i) * std::sqrt(1600 * s2 + 900 * c2);
    for (int j = 0; j <= subdivisions; ++j) {
      const double t = static_cast<double>(j) / subdivisions;
      const double a = 32 + a1 * t + a2 * t * t, b = 20 + b1 * t + b2 * t * t;
      const double da = a1 + 2 * a2 * t, db = b1 + 2 * b2 * t;
      side += weight(i) * weight(j) *
              std::sqrt(height * height * (b * b * c2 + a * a * s2) +
                        std::pow(a * db * s2 + b * da * c2, 2));
    }
  }
  const double angle_step = 2 * std::numbers::pi / subdivisions;
  return side * angle_step / (9 * subdivisions) + flange * perimeter * angle_step / 3 +
         std::numbers::pi *
             (384 + 2400 - 640 - 2 * bore * bore - 50 + 2 * bore * (height + flange) + 20 * flange);
}
}  // namespace

TEST_CASE("loft features publish exact solids with ancestry from every profile sketch",
          "[loft-evaluator]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  const auto d = loft_part();
  const auto original = source(d);
  auto result = evaluate(evaluator, d);
  frustum_mass(result.bodies.at("link"));
  provenance(result.bodies.at("link"), {"section_a/outline", "section_b/outline"});
  CHECK(result.kernel_calls == 1);
  auto warm = evaluate(evaluator, d);
  CHECK(warm.kernel_calls == 0);
  CHECK(warm.cache_hits == 3);
  CHECK(warm.bodies.at("link").shape == result.bodies.at("link").shape);
  CHECK(source(d) == original);
}

TEST_CASE("loft edits invalidate only dependent sections and preserve retained readers",
          "[loft-evaluator]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto d = loft_part();
  const auto first = evaluate(evaluator, d);
  SECTION("last section plane") {
    d.params[0].value = "60 mm";
    const auto changed = evaluate(evaluator, d);
    CHECK_THAT(changed.bodies.at("link").shape->measures().volume, WithinAbs(11200, 1e-5));
    CHECK(changed.kernel_calls == 1);
    CHECK(changed.cache_hits == 1);
    d.params[0].value = "30 mm";
  }
  SECTION("first section profile") {
    d.features[0].inputs = feature("", "", R"({"plane":{"datum":"xy"},"entities":[
      {"id":"outline","type":"rect","corner":[-10,-8],"size":[20,16]}],"constraints":[]})")
                               .inputs;
    const auto changed = evaluate(evaluator, d);
    CHECK_THAT(changed.bodies.at("link").shape->measures().volume, WithinAbs(9600, 1e-5));
    CHECK(changed.cache_hits == 1);
    d.features[0] = loft_part().features[0];
  }
  SECTION("last section profile") {
    d.params[1].value = 1.0;
    const auto changed = evaluate(evaluator, d);
    CHECK_THAT(changed.bodies.at("link").shape->measures().volume, WithinAbs(2400, 1e-5));
    CHECK(changed.cache_hits == 1);
    d.params[1].value = 2.0;
  }
  SECTION("unused parameter") { d.params[2].value = 50.0; }
  const auto restored = evaluate(evaluator, d);
  CHECK(restored.kernel_calls == 0);
  CHECK(restored.bodies.at("link").shape == first.bodies.at("link").shape);
  evaluator.set_cache_limits({1, 1});
  const auto rebuilt = evaluate(evaluator, d);
  CHECK(rebuilt.kernel_calls == 1);
  frustum_mass(first.bodies.at("link"));
  REQUIRE(kernel->tessellate(first.bodies.at("link").shape, {}));
  frustum_mass(rebuilt.bodies.at("link"));
}

TEST_CASE("loft cache rebinds every current sketch ID and downstream source selector",
          "[loft-evaluator]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto d = loft_part();
  d.features.push_back(feature("side_plane", "sketch", R"({"plane":{"created_by":"blend",
    "role":"cap_end","except":{"created_by":"blend","role":"side","from":"section_b/outline",
    "expect":"some"},"expect":"one"},
    "entities":[{"id":"p","type":"point","at":[0,0]}],"constraints":[]})"));
  const auto original = evaluate(evaluator, d);
  d = rename(std::move(d), "blend", "new_blend");
  const auto producer = evaluate(evaluator, d);
  CHECK(producer.bodies.at("link").shape == original.bodies.at("link").shape);
  CHECK(producer.features[2].status == FeatureStatus::kCached);
  provenance(producer.bodies.at("link"), {"section_a/outline", "section_b/outline"}, "new_blend");
  d = rename(std::move(d), "section_b", "new_section_b");
  const auto renamed = evaluate(evaluator, d);
  // RFC 0007 permits conservative misses when referenced IDs change; inputs are hashed.
  CHECK(renamed.features[2].status == FeatureStatus::kBuilt);
  frustum_mass(renamed.bodies.at("link"));
  provenance(renamed.bodies.at("link"), {"section_a/outline", "new_section_b/outline"},
             "new_blend");
  d = rename(std::move(d), "section_a", "new_section_a");
  const auto both = evaluate(evaluator, d);
  frustum_mass(both.bodies.at("link"));
  provenance(both.bodies.at("link"), {"new_section_a/outline", "new_section_b/outline"},
             "new_blend");
  const auto warm = evaluate(evaluator, d);
  CHECK(warm.bodies.at("link").shape == both.bodies.at("link").shape);
  CHECK(warm.features[2].status == FeatureStatus::kCached);
}

TEST_CASE("loft features support smooth intermediate sections and their current ancestry",
          "[loft-evaluator]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto d = loft_part();
  d.params[1].value = 1.0;
  d.features.insert(d.features.begin() + 1, feature("middle", "sketch", R"({
    "plane":{"origin":[0,0,"height / 2"],"normal":"z","x_direction":"x"},"entities":[
    {"id":"waist","type":"rect","corner":[-7.5,-6],"size":[15,12]}],"constraints":[]})"));
  d.features.back().inputs = feature("", "", R"({"profiles":[{"sketch":"section_a"},
    {"sketch":"middle"},{"sketch":"section_b"}],"result":{"new":"link","body_type":"brep"}})")
                                 .inputs;
  const auto result = evaluate(evaluator, d);
  CHECK_THAT(result.bodies.at("link").shape->measures().volume, WithinAbs(4320, 1e-5));
  provenance(result.bodies.at("link"), {"section_a/outline", "middle/waist", "section_b/outline"});
  d = rename(std::move(d), "blend", "new_blend");
  const auto renamed = evaluate(evaluator, d);
  CHECK(renamed.kernel_calls == 0);
  CHECK(renamed.cache_hits == 4);
  provenance(renamed.bodies.at("link"), {"section_a/outline", "middle/waist", "section_b/outline"},
             "new_blend");
  d = rename(std::move(d), "middle", "new_middle");
  const auto section_renamed = evaluate(evaluator, d);
  CHECK(section_renamed.kernel_calls == 1);
  CHECK(section_renamed.cache_hits == 3);
  provenance(section_renamed.bodies.at("link"),
             {"section_a/outline", "new_middle/waist", "section_b/outline"}, "new_blend");
}

TEST_CASE("loft result modes publish complete current target and tool ancestry atomically",
          "[loft-evaluator]") {
  auto kernel = kernel::make_occt_kernel().value();
  for (const auto& [mode, volume] :
       {std::pair{"join", 64000.0}, {"cut", 58400.0}, {"intersect", 5600.0}}) {
    CAPTURE(mode);
    Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
    auto d = loft_part();
    d.features.insert(
        d.features.begin(),
        {feature("stock_outline", "sketch", R"({"plane":{"datum":"xy"},"entities":[
        {"id":"r","type":"rect","corner":[-20,-20],"size":[40,40]}],"constraints":[]})"),
         feature("stock_base", "extrude", R"({"profile":{"sketch":"stock_outline"},"distance":40,
        "result":{"new":"stock","body_type":"brep"}})")});
    d.features.back().inputs =
        ir::json::Object{{"profiles", *d.features.back().inputs.find("profiles")},
                         {"result", ir::json::Object{{mode, "stock"}}}};
    const auto result = evaluate(evaluator, d);
    const auto& body = result.bodies.at("stock");
    CHECK_THAT(body.shape->measures().volume, WithinAbs(volume, 1e-5));
    for (const auto& name : body.names) CHECK(name.entity->owner() == body.shape);
    if (std::string_view(mode) != "join")
      provenance(body, {"section_a/outline", "section_b/outline"});
    const auto warm = evaluate(evaluator, d);
    CHECK(warm.kernel_calls == 0);
    CHECK(warm.bodies.at("stock").shape == body.shape);
  }
}

TEST_CASE("loft adapters reject unsupported sections at precise pointers and recover cleanly",
          "[loft-evaluator]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto d = loft_part();
  const auto original = evaluate(evaluator, d);
  std::string pointer;
  SECTION("rails") {
    d.features.back().inputs = feature("", "", R"({"profiles":[{"sketch":"section_a"},
      {"sketch":"section_b"}],"rails":[{"sketch":"section_a","entity":"outline"}],
      "result":{"new":"link","body_type":"brep"}})")
                                   .inputs;
    pointer = "/features/2/inputs/rails";
  }
  SECTION("section count bound") {
    ir::json::Array profiles(33, ir::json::Object{{"sketch", "section_a"}});
    d.features.back().inputs =
        ir::json::Object{{"profiles", std::move(profiles)},
                         {"result", ir::json::Object{{"new", "link"}, {"body_type", "brep"}}}};
    pointer = "/features/2/inputs/profiles";
  }
  SECTION("multiple regions") {
    d.features[1].inputs = feature("", "", R"({"plane":{"origin":[0,0,30],"normal":"z",
      "x_direction":"x"},"entities":[{"id":"outline","type":"rect","corner":[0,0],"size":[10,8]},
      {"id":"island","type":"rect","corner":[20,0],"size":[10,8]}],"constraints":[]})")
                               .inputs;
    pointer = "/features/2/inputs/profiles/1";
  }
  SECTION("hole") {
    d.features[1].inputs = feature("", "", R"({"plane":{"origin":[0,0,30],"normal":"z",
      "x_direction":"x"},"entities":[{"id":"outline","type":"rect","corner":[-5,-4],"size":[10,8]},
      {"id":"hole","type":"circle","center":[0,0],"radius":1}],"constraints":[]})")
                               .inputs;
    pointer = "/features/2/inputs/profiles/1";
  }
  SECTION("outside region pick") {
    d.features.back().inputs = feature("", "", R"({"profiles":[{"sketch":"section_a"},
      {"sketch":"section_b","regions":[[100,100]]}],"result":{"new":"link","body_type":"brep"}})")
                                   .inputs;
    pointer = "/features/2/inputs/profiles/1/regions/0";
  }
  auto failed = evaluator.evaluate(source(d));
  REQUIRE(failed);
  REQUIRE_FALSE(failed->ok());
  REQUIRE_FALSE(failed->features.back().errors.empty());
  CHECK(failed->features.back().errors.front().pointer == pointer);
  CHECK_FALSE(failed->bodies.at("link").shape);
  frustum_mass(original.bodies.at("link"));
  const auto restored = evaluate(evaluator, loft_part());
  CHECK(restored.bodies.at("link").shape == original.bodies.at("link").shape);
}

TEST_CASE("loft profile picks preserve selected entity ancestry without unused islands",
          "[loft-evaluator]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto d = loft_part();
  d.features.front().inputs = feature("", "", R"({"plane":{"datum":"xy"},"entities":[
    {"id":"outline","type":"rect","corner":[-5,-4],"size":[10,8]},
    {"id":"island","type":"circle","center":[40,0],"radius":3}],"constraints":[]})")
                                  .inputs;
  d.features.back().inputs = feature("", "", R"({"profiles":[
    {"sketch":"section_a","regions":[[0,0]],"fill_rule":"nonzero"},{"sketch":"section_b"}],
    "result":{"new":"link","body_type":"brep"}})")
                                 .inputs;
  const auto result = evaluate(evaluator, d);
  frustum_mass(result.bodies.at("link"));
  provenance(result.bodies.at("link"), {"section_a/outline", "section_b/outline"});
}

TEST_CASE("loft suppression rollback and failed inputs preserve independent branches",
          "[loft-evaluator]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto d = loft_part();
  const auto original = evaluate(evaluator, d);
  SECTION("rollback before loft") {
    d.history_end = "section_b";
    const auto rolled = evaluate(evaluator, d);
    CHECK(rolled.kernel_calls == 0);
    CHECK(rolled.features.back().status == FeatureStatus::kRolledBack);
    CHECK(rolled.bodies.empty());
  }
  SECTION("suppressed loft") {
    d.features.back().suppressed = true;
    const auto suppressed = evaluate(evaluator, d);
    CHECK(suppressed.kernel_calls == 0);
    CHECK(suppressed.features.back().status == FeatureStatus::kSuppressed);
    CHECK(suppressed.bodies.at("link").unavailable == "input_suppressed");
  }
  SECTION("suppressed section") {
    d.features[1].suppressed = true;
    auto failed = evaluator.evaluate(source(d));
    REQUIRE(failed);
    CHECK_FALSE(failed->ok());
    CHECK(failed->kernel_calls == 0);
    CHECK(failed->features.back().errors.front().code == "input_suppressed");
    CHECK(failed->features.back().errors.front().pointer == "/features/2/inputs/profiles/1/sketch");
  }
  SECTION("invalid loft with independent extrusion") {
    d.params[0].value = "0 mm";
    d.features.push_back(feature("spare", "extrude", R"({"profile":{"sketch":"section_a"},
      "distance":2,"result":{"new":"spare_body","body_type":"brep"}})"));
    auto failed = evaluator.evaluate(source(d));
    REQUIRE(failed);
    CHECK_FALSE(failed->ok());
    CHECK_FALSE(failed->bodies.at("link").shape);
    REQUIRE(failed->bodies.at("spare_body").shape);
    CHECK_THAT(failed->bodies.at("spare_body").shape->measures().volume, WithinAbs(160, 1e-5));
  }
  frustum_mass(original.bodies.at("link"));
  REQUIRE(kernel->tessellate(original.bodies.at("link").shape, {}));
}

TEST_CASE("lofted link example has independent curved mass and exact STEP and display coverage",
          "[loft-evaluator][loft-showcase]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {96, 64 * 1024 * 1024});
  auto d = link_example();
  const auto original = source(d);
  const auto result = evaluate(evaluator, d);
  const auto& body = result.bodies.at("link");
  const auto& m = body.shape->measures();
  const auto [volume, center] = link_volume_center(160, 1, 8, 6);
  const double area = link_area(160, 1, 8, 6, 1024);
  CHECK_THAT(area, WithinAbs(link_area(160, 1, 8, 6, 512), 1e-6));
  CHECK_THAT(m.volume, WithinAbs(volume, 1e-5));
  CHECK_THAT(m.area, WithinAbs(area, 1e-5));
  CHECK_THAT(m.centroid.x, WithinAbs(0, 1e-7));
  CHECK_THAT(m.centroid.y, WithinAbs(0, 1e-7));
  CHECK_THAT(m.centroid.z, WithinAbs(center, 1e-7));
  CHECK(m.solids == 1);
  const auto mesh = kernel->tessellate(body.shape, {}).value();
  CHECK(mesh.faces.size() == m.faces);
  double mesh_volume = 0, mesh_area = 0;
  for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
    const auto a = mesh.vertices[mesh.indices[i]].position;
    const auto b = mesh.vertices[mesh.indices[i + 1]].position;
    const auto c = mesh.vertices[mesh.indices[i + 2]].position;
    mesh_volume += math::dot(a, math::cross(b, c)) / 6;
    mesh_area += math::length(math::cross(b - a, c - a)) / 2;
  }
  CHECK(std::abs(mesh_volume / volume - 1) < 0.01);
  CHECK(std::abs(mesh_area / area - 1) < 0.01);
  const auto text = kernel->export_step({body.shape}).value();
  std::istringstream stream(text);
  STEPControl_Reader reader;
  REQUIRE(reader.ReadStream("lofted-link.step", stream) == IFSelect_RetDone);
  reader.SetSystemLengthUnit(1);
  REQUIRE(reader.TransferRoots() > 0);
  CHECK(BRepCheck_Analyzer(reader.OneShape()).IsValid());
  const auto reloaded = kernel::detail::snapshot(reader.OneShape());
  REQUIRE(reloaded);
  CHECK_THAT((*reloaded)->measures().volume, WithinAbs(volume, 1e-5));
  CHECK_THAT((*reloaded)->measures().area, WithinAbs(area, 1e-5));
  d.params[0].value = "200 mm";
  d.params[1].value = 1.1;
  d.params[2].value = "6 mm";
  d.params[3].value = "12 mm";
  const auto edited = evaluate(evaluator, d);
  const auto [edited_volume, edited_center] = link_volume_center(200, 1.1, 6, 12);
  CHECK_THAT(edited.bodies.at("link").shape->measures().volume, WithinAbs(edited_volume, 1e-5));
  CHECK_THAT(edited.bodies.at("link").shape->measures().area,
             WithinAbs(link_area(200, 1.1, 6, 12, 1024), 1e-5));
  CHECK_THAT(edited.bodies.at("link").shape->measures().centroid.z, WithinAbs(edited_center, 1e-7));
  const auto restored = evaluate(evaluator, link_example());
  CHECK(restored.bodies.at("link").shape == body.shape);
  CHECK(restored.kernel_calls == 0);
  CHECK(source(link_example()) == original);
}
