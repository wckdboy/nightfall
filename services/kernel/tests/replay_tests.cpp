// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "evaluator.hpp"
#include "part_fixture.hpp"

#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <STEPControl_Reader.hxx>

#include <algorithm>
#include <iostream>
#include <numbers>
#include <random>
#include <sstream>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace nf;
using namespace nf::kernel_service;
using namespace nf::kernel_service::test;
using Catch::Matchers::WithinAbs;

namespace {
// Scenario contract reference: SolidCraft's engine/tests/atomic_commands.rs at
// 4c30d944 (MIT OR Apache-2.0). These IR fixtures and analytic oracles are original.
struct Scenario {
  int height, copies, pitch, quarter;
  math::Vec3 translation;
  bool skip_pattern = false, skip_mirror = false;
  int end = 4;
};
ir::Document document(const Scenario& s) {
  auto d = part(2);
  d.params = {{"height", std::to_string(s.height) + " mm", {}, {}, {}, {}},
              {"copies", static_cast<double>(s.copies), {}, {}, {}, {}},
              {"pitch", std::to_string(s.pitch) + " mm", {}, {}, {}, {}},
              {"turn", std::to_string(s.quarter * 90) + " deg", {}, {}, {}, {}},
              {"dx", std::to_string(s.translation.x) + " mm", {}, {}, {}, {}},
              {"dy", std::to_string(s.translation.y) + " mm", {}, {}, {}, {}},
              {"dz", std::to_string(s.translation.z) + " mm", {}, {}, {}, {}}};
  d.features[0] = feature("outline", "sketch", R"({"plane":{"datum":"xy"},"entities":[
    {"id":"outer","type":"circle","center":[10,0],"radius":5},
    {"id":"bore","type":"circle","center":[10,0],"radius":2}],"constraints":[]})");
  d.features[1] = feature("base", "extrude", R"({"profile":{"sketch":"outline"},
    "distance":"height","result":{"new":"plate","body_type":"brep"}})");
  d.features.push_back(feature("repeat", "pattern_linear", R"({"bodies":["plate"],
    "count":"copies","spacing":"pitch","direction":"x"})"));
  d.features.push_back(feature("reflect", "mirror", R"({"bodies":["plate"],
    "plane":{"origin":[0,0,0],"normal":"x","x_direction":"y"}})"));
  d.features.push_back(feature("placed", "transform", R"({"bodies":["plate"],
    "translation":["dx","dy","dz"],
    "rotation":{"axis":{"origin":[0,0,0],"direction":"z"},"angle":"turn"}})"));
  d.features[2].suppressed = s.skip_pattern;
  d.features[3].suppressed = s.skip_mirror;
  if (s.end < 4) d.history_end = d.features[static_cast<std::size_t>(s.end)].id;
  return d;
}
math::Vec3 rotate(math::Vec3 p, int quarter) {
  switch (quarter) {
    case 1:
      return {-p.y, p.x, p.z};
    case 2:
      return {-p.x, -p.y, p.z};
    case 3:
      return {p.y, -p.x, p.z};
    default:
      return p;
  }
}
kernel::Measures oracle(const Scenario& s) {
  const int copies = s.end >= 2 && !s.skip_pattern ? s.copies : 1;
  const bool mirrored = s.end >= 3 && !s.skip_mirror;
  const int count = copies * (mirrored ? 2 : 1);
  kernel::Measures m;
  m.solids = static_cast<std::size_t>(count);
  // The extrusion retains four analytic spans per circle. A following union
  // unifies each cylindrical wall; both representations describe the same solid.
  m.faces = (copies > 1 || mirrored ? 4 : 10) * m.solids;
  m.volume = count * 21 * std::numbers::pi * s.height;
  m.area = count * (42 + 14 * s.height) * std::numbers::pi;
  m.minimum = {1e9, 1e9, 1e9};
  m.maximum = {-1e9, -1e9, -1e9};
  for (int i = 0; i < copies; ++i) {
    for (int side = 0; side < (mirrored ? 2 : 1); ++side) {
      math::Vec3 center{(10. + i * s.pitch) * (side ? -1 : 1), 0, s.height / 2.};
      if (s.end == 4) center = rotate(center, s.quarter) + s.translation;
      m.centroid = m.centroid + center / count;
      m.minimum.x = std::min(m.minimum.x, center.x - 5);
      m.minimum.y = std::min(m.minimum.y, center.y - 5);
      m.minimum.z = std::min(m.minimum.z, center.z - s.height / 2.);
      m.maximum.x = std::max(m.maximum.x, center.x + 5);
      m.maximum.y = std::max(m.maximum.y, center.y + 5);
      m.maximum.z = std::max(m.maximum.z, center.z + s.height / 2.);
    }
  }
  return m;
}
void vector(math::Vec3 actual, math::Vec3 expected) {
  CHECK_THAT(actual.x, WithinAbs(expected.x, 1e-6));
  CHECK_THAT(actual.y, WithinAbs(expected.y, 1e-6));
  CHECK_THAT(actual.z, WithinAbs(expected.z, 1e-6));
}
void validate(const Evaluation& result, const Scenario& s) {
  for (const auto& f : result.features)
    for (const auto& e : f.errors) UNSCOPED_INFO(f.id << " " << e.pointer << ": " << e.message);
  REQUIRE(result.ok());
  REQUIRE(result.bodies.size() == 1);
  const auto& body = result.bodies.at("plate");
  REQUIRE(body.shape);
  const auto& m = body.shape->measures();
  const auto expected = oracle(s);
  CHECK_THAT(m.volume, WithinAbs(expected.volume, 1e-6));
  CHECK_THAT(m.area, WithinAbs(expected.area, 1e-6));
  CHECK(m.solids == expected.solids);
  CHECK(m.faces == expected.faces);
  vector(m.centroid, expected.centroid);
  vector(m.minimum, expected.minimum);
  vector(m.maximum, expected.maximum);
  REQUIRE_FALSE(body.names.empty());
  std::vector<kernel::EntityPtr> unique;
  for (const auto& name : body.names) {
    REQUIRE(name.entity);
    CHECK(name.entity->owner() == body.shape);
    CHECK(name.entity->kind() == kernel::EntityKind::kFace);
    CHECK((name.feature == "base" || name.feature == "repeat" || name.feature == "reflect"));
    if (std::ranges::none_of(unique, [&](const auto& face) { return face->same_as(*name.entity); }))
      unique.push_back(name.entity);
  }
  CHECK(unique.size() == m.faces);
}
std::vector<std::string> roles(const Evaluation& result) {
  std::vector<std::string> out;
  for (const auto& name : result.bodies.at("plate").names)
    out.push_back(name.feature + ":" + name.role + ":" + name.from);
  std::ranges::sort(out);
  return out;
}
}  // namespace

TEST_CASE("seeded headless histories recover from failures and replay across cache boundaries",
          "[engine-replay]") {
  struct Saved {
    Scenario scenario;
    std::string source;
    Evaluation result;
  };
  for (std::uint32_t seed = 0; seed < 12; ++seed) {
    CAPTURE(seed);
    std::mt19937 random(20261019 + seed);
    auto kernel = kernel::make_occt_kernel().value();
    Evaluator evaluator(*kernel, {32, 64 * 1024 * 1024});
    std::vector<Saved> history;
    for (int step = 0; step < 6; ++step) {
      CAPTURE(step);
      Scenario s{2 + static_cast<int>(random() % 19),
                 2 + static_cast<int>(random() % 3),
                 16 + static_cast<int>(random() % 15),
                 static_cast<int>(random() % 4),
                 {static_cast<double>(random() % 61) - 30, static_cast<double>(random() % 61) - 30,
                  static_cast<double>(random() % 61) - 30}};
      s.skip_pattern = step == 2;
      s.skip_mirror = step == 3;
      if (step == 4) s.end = 1 + static_cast<int>(random() % 3);
      const auto text = source(document(s));
      auto current = evaluator.evaluate(text).value();
      validate(current, s);
      const auto warm = evaluator.evaluate(text).value();
      validate(warm, s);
      CHECK(warm.kernel_calls == 0);
      CHECK(roles(warm) == roles(current));
      auto equivalent = document(s);
      equivalent.units.length = "cm";
      equivalent.features[0] = feature("outline", "sketch", R"({"plane":{"datum":"xy"},"entities":[
        {"id":"outer","type":"circle","center":[1,0],"radius":0.5},
        {"id":"bore","type":"circle","center":[1,0],"radius":0.2}],"constraints":[]})");
      const auto converted = evaluator.evaluate(source(equivalent)).value();
      validate(converted, s);
      CHECK(converted.kernel_calls == 0);
      CHECK(roles(converted) == roles(current));
      const auto usage = evaluator.cache_usage();
      CHECK_FALSE(evaluator.evaluate("{}"));
      CHECK(evaluator.cache_usage().entries == usage.entries);
      CHECK(evaluator.cache_usage().estimated_bytes == usage.estimated_bytes);
      auto invalid = document(s);
      invalid.history_end.reset();
      invalid.features[3] = feature("reflect", "mirror", R"({"bodies":["plate"],
        "plane":{"origin":["missing",0,0],"normal":"x","x_direction":"y"}})");
      const auto failed = evaluator.evaluate(source(invalid)).value();
      REQUIRE_FALSE(failed.ok());
      REQUIRE_FALSE(failed.features[3].errors.empty());
      CHECK_FALSE(failed.bodies.at("plate").shape);
      CHECK_FALSE(failed.bodies.at("plate").hash);
      const auto recovered = evaluator.evaluate(text).value();
      validate(recovered, s);
      CHECK(roles(recovered) == roles(current));
      evaluator.set_cache_limits({1, 64 * 1024 * 1024});
      evaluator.clear_cache();
      const auto evicted = evaluator.evaluate(text).value();
      validate(evicted, s);
      CHECK(evicted.cache_hits == 0);
      CHECK(roles(evicted) == roles(current));
      Evaluator fresh(*kernel, {32, 64 * 1024 * 1024});
      const auto cold = fresh.evaluate(text).value();
      validate(cold, s);
      CHECK(cold.cache_hits == 0);
      CHECK(roles(cold) == roles(current));
      evaluator.set_cache_limits({32, 64 * 1024 * 1024});
      history.push_back({s, text, std::move(current)});
      for (const auto& retained : history) validate(retained.result, retained.scenario);
    }
    for (const bool reverse : {true, false}) {
      for (std::size_t i = 0; i < history.size(); ++i) {
        const auto& saved = history[reverse ? history.size() - 1 - i : i];
        validate(evaluator.evaluate(saved.source).value(), saved.scenario);
        validate(saved.result, saved.scenario);
      }
    }
  }
  std::cout << "engine replay: 12 seeds, 72 edit states, 72 unit variants and 144 reverse/forward "
               "replays completed\n";
}

TEST_CASE("headless retained engine geometry exports exact STEP and current indexed meshes",
          "[engine-replay]") {
  const Scenario s{9, 3, 24, 1, {12, -7, 3}};
  Evaluation retained;
  {
    auto kernel = kernel::make_occt_kernel().value();
    Evaluator evaluator(*kernel, {1, 64 * 1024 * 1024});
    retained = evaluator.evaluate(source(document(s))).value();
    validate(retained, s);
    evaluator.clear_cache();
  }
  auto kernel = kernel::make_occt_kernel().value();
  const auto body = retained.bodies.at("plate").shape;
  const auto step = kernel->export_step({body}).value();
  CHECK(step.find("AP242") != std::string::npos);
  CHECK(step.find("CYLINDRICAL_SURFACE") != std::string::npos);
  CHECK(step.find("TESSELLATED") == std::string::npos);
  STEPControl_Reader reader;
  std::istringstream input(step);
  REQUIRE(reader.ReadStream("retained.step", input) == IFSelect_RetDone);
  reader.SetSystemLengthUnit(1);
  REQUIRE(reader.TransferRoots() > 0);
  const auto shape = reader.OneShape();
  CHECK(BRepCheck_Analyzer(shape).IsValid());
  GProp_GProps volume, area;
  BRepGProp::VolumeProperties(shape, volume, 1e-9);
  BRepGProp::SurfaceProperties(shape, area, 1e-9);
  CHECK_THAT(volume.Mass(), WithinAbs(oracle(s).volume, 1e-5));
  CHECK_THAT(area.Mass(), WithinAbs(oracle(s).area, 1e-5));
  const auto center = volume.CentreOfMass();
  vector({center.X(), center.Y(), center.Z()}, oracle(s).centroid);
  const auto mesh = kernel->tessellate(body, {0.02, 0.15}).value();
  REQUIRE_FALSE(mesh.indices.empty());
  CHECK(mesh.faces.size() == oracle(s).faces);
  double approximate_volume = 0;
  const auto origin = mesh.vertices.front().position;
  for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
    const auto a = mesh.vertices.at(mesh.indices[i]).position - origin;
    const auto b = mesh.vertices.at(mesh.indices[i + 1]).position - origin;
    const auto c = mesh.vertices.at(mesh.indices[i + 2]).position - origin;
    approximate_volume += math::dot(a, math::cross(b, c)) / 6;
  }
  CHECK_THAT(approximate_volume, WithinAbs(oracle(s).volume, oracle(s).volume * 0.01));
  for (const auto& face : mesh.faces) CHECK(face.face->owner() == body);
  validate(retained, s);
}
