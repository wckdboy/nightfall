// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "evaluator.hpp"
#include "part_fixture.hpp"

#include <algorithm>
#include <numbers>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace nf;
using namespace nf::kernel_service;
using namespace nf::kernel_service::test;
using Catch::Matchers::WithinAbs;

namespace {
ir::Document cut_plate() {
  auto d = part(2);
  d.params.push_back({"tool_x", "12 mm", {}, {}, {}, {}});
  d.params.push_back({"tool_z", "-5 mm", {}, {}, {}, {}});
  d.params.push_back({"tool_width", "2 mm", {}, {}, {}, {}});
  d.features.push_back(feature("tool_outline", "sketch", R"({"plane":{
    "origin":["tool_x",-5,"tool_z"],"normal":[0,0,1],"x_direction":[1,0,0]},
    "entities":[{"id":"r","type":"rect","corner":[0,0],
    "size":["tool_width",30]}],"constraints":[]})"));
  d.features.push_back(
      feature("tool_solid", "extrude", R"({"profile":{"sketch":"tool_outline"},
    "distance":40,"result":{"new":"tool","body_type":"brep"}})"));
  d.features.push_back(
      feature("cut", "boolean",
              R"({"op":"subtract","target":"plate","tools":["tool"]})"));
  d.features.push_back(feature("probe_plane", "sketch", R"({"plane":{
    "created_by":"base","role":"cap_end","expect":"one"},"entities":[
    {"id":"c","type":"circle","center":[5,10],"radius":1}],"constraints":[]})"));
  d.features.push_back(
      feature("probe", "extrude", R"({"profile":{"sketch":"probe_plane"},
    "distance":2,"result":{"new":"probe_body","body_type":"brep"}})"));
  return d;
}
struct Scenario {
  int x, z, width;
  double volume, area;
  std::size_t solids, caps;
};
void check_reference(kernel::IKernel &kernel, const Evaluation &result,
                     const Scenario &s) {
  const auto &body = result.bodies.at("plate");
  REQUIRE(body.shape);
  CHECK_THAT(body.shape->measures().volume, WithinAbs(s.volume, 1e-7));
  CHECK_THAT(body.shape->measures().area, WithinAbs(s.area, 1e-7));
  CHECK(body.shape->measures().solids == s.solids);
  std::vector<kernel::EntityPtr> caps;
  for (const auto &name : body.names) {
    REQUIRE(name.entity);
    CHECK(name.entity->owner() == body.shape);
    if (name.feature == "base" && name.role == "cap_end" &&
        std::ranges::none_of(
            caps, [&](const auto &e) { return e->same_as(*name.entity); }))
      caps.push_back(name.entity);
  }
  REQUIRE(caps.size() == s.caps);
  for (const auto &cap : caps) {
    const auto plane = kernel.face_plane(cap);
    REQUIRE(plane);
    CHECK_THAT(plane->origin.z, WithinAbs(30, 1e-8));
    CHECK_THAT(plane->normal.z, WithinAbs(1, 1e-12));
    if (s.caps == 2) {
      // Both descendants are equally close; topology order cannot choose the
      // intended one.
      const auto distance = kernel.face_distance(cap, {5, 10, 30});
      REQUIRE(distance);
      CHECK_THAT(*distance, WithinAbs(1, 1e-8));
    }
  }
  if (s.caps == 1) {
    REQUIRE(result.ok());
    const auto &probe = result.bodies.at("probe_body");
    REQUIRE(probe.shape);
    CHECK_THAT(probe.shape->measures().volume,
               WithinAbs(2 * std::numbers::pi, 1e-7));
    CHECK_THAT(probe.shape->measures().centroid.x, WithinAbs(5, 1e-8));
    CHECK_THAT(probe.shape->measures().centroid.y, WithinAbs(10, 1e-8));
    CHECK_THAT(probe.shape->measures().centroid.z, WithinAbs(31, 1e-8));
  } else {
    REQUIRE_FALSE(result.ok());
    REQUIRE_FALSE(result.features[5].errors.empty());
    CHECK(result.features[5].errors.front().code == "selector_count");
    CHECK(result.features[5].errors.front().pointer ==
          "/features/5/inputs/plane");
    CHECK(result.features[5].errors.front().message.find(
              "matched " + std::to_string(s.caps)) != std::string::npos);
    REQUIRE_FALSE(result.features[6].errors.empty());
    CHECK(result.features[6].errors.front().code == "input_failed");
    CHECK_FALSE(result.bodies.at("probe_body").shape);
  }
  if (s.caps == 0) {
    // A valid, parallel replacement exists: it must not inherit the deleted
    // cap's intent.
    const auto faces = kernel.faces(body.shape);
    REQUIRE(faces);
    std::size_t replacement = 0;
    for (const auto &face : *faces)
      if (face.plane && face.plane->normal.z > 0.9) {
        ++replacement;
        CHECK_THAT(face.plane->origin.z, WithinAbs(28, 1e-8));
      }
    CHECK(replacement == 1);
  }
}
} // namespace

TEST_CASE("topological naming rejects split and deleted caps across edits "
          "cache and cold replay",
          "[topological-naming]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  auto document = cut_plate();
  const auto retained = evaluator.evaluate(source(document)).value();
  const Scenario baseline{12, -5, 2, 6000, 2200, 1, 1};
  check_reference(*kernel, retained, baseline);
  for (const auto &s : {baseline, Scenario{4, -5, 2, 4800, 3200, 2, 2},
                        Scenario{-1, -5, 2, 5400, 2100, 1, 1},
                        Scenario{-5, 28, 20, 5600, 2080, 1, 0},
                        Scenario{4, -5, 2, 4800, 3200, 2, 2}, baseline}) {
    INFO("tool x/z/width: " << s.x << "/" << s.z << "/" << s.width);
    document.params[3].value = std::to_string(s.x) + " mm";
    document.params[4].value = std::to_string(s.z) + " mm";
    document.params[5].value = std::to_string(s.width) + " mm";
    const auto edited = evaluator.evaluate(source(document)).value();
    check_reference(*kernel, edited, s);
    const auto warm = evaluator.evaluate(source(document)).value();
    check_reference(*kernel, warm, s);
    CHECK(warm.cache_hits > 0);
    CHECK(warm.bodies.at("plate").shape == edited.bodies.at("plate").shape);
    evaluator.clear_cache();
    const auto cold = evaluator.evaluate(source(document)).value();
    check_reference(*kernel, cold, s);
    CHECK(cold.cache_hits == 0);
    CHECK(cold.bodies.at("plate").shape != edited.bodies.at("plate").shape);
    check_reference(*kernel, retained, baseline);
  }
}
