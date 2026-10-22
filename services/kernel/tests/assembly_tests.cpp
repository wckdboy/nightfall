// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// RFC 0013 assemblies built from real solids: one parametric link component placed four times
// as a four-bar (ground 100, crank 40, coupler 120, rocker 80 mm), revolute joints on the pin
// holes' cylinder axes, solved and checked against the closed-form linkage position.
#include "assembly.hpp"

#include <cmath>
#include <numbers>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace nf;
using namespace nf::kernel_service;
using Catch::Matchers::WithinAbs;

namespace {
constexpr double kPi = std::numbers::pi;
std::string number(double v) { return ir::json::format_number(v); }

/// The four-bar document. `crank` is the crank length, `theta` its driven angle (deg), and
/// `rocker_drive` optionally drives the rocker too (deg).
std::string four_bar(double crank = 40, double theta = 60, const char* rocker_drive = nullptr,
                     const char* frame_extra = "") {
  const double t = theta * kPi / 180;
  // Rough starting placements near the upper branch.
  const double cx = crank * std::cos(t), cy = crank * std::sin(t);
  std::string joints =
      R"([
    {"id":"input","kind":"revolute","value":"theta",
     "a":{"occurrence":"ground","frame":{"created_by":"bar","role":"side","from":"outline/h0")" +
      std::string(frame_extra) + R"(}},
     "b":{"occurrence":"crank","frame":{"created_by":"bar","role":"side","from":"outline/h0"}}},
    {"id":"elbow","kind":"revolute",
     "a":{"occurrence":"crank","frame":{"created_by":"bar","role":"side","from":"outline/h1"}},
     "b":{"occurrence":"coupler","frame":{"created_by":"bar","role":"side","from":"outline/h0"}}},
    {"id":"wrist","kind":"revolute",
     "a":{"occurrence":"coupler","frame":{"created_by":"bar","role":"side","from":"outline/h1"}},
     "b":{"occurrence":"rocker","frame":{"created_by":"bar","role":"side","from":"outline/h1"}}},
    {"id":"pivot","kind":"revolute")" +
      (rocker_drive ? std::string(R"(,"value":")") + rocker_drive + " deg\"" : std::string()) +
      R"(,
     "a":{"occurrence":"ground","frame":{"created_by":"bar","role":"side","from":"outline/h1"}},
     "b":{"occurrence":"rocker","frame":{"created_by":"bar","role":"side","from":"outline/h0"}}}])";
  return R"({"format":"nightfall.part","version":2,"units":{"length":"mm","angle":"deg"},"meta":{},
  "params":[{"name":"length","value":"100 mm"},{"name":"theta","value":")" +
         number(theta) + R"( deg"}],
  "references":[],"features":[],"history_end":null,"ui":{},
  "components":[{"id":"link","features":[
    {"id":"outline","type":"sketch","inputs":{"plane":{"datum":"xy"},"entities":[
      {"id":"r","type":"rect","corner":[-8,-8],"size":["length + 16 mm",16]},
      {"id":"h0","type":"circle","center":[0,0],"radius":3},
      {"id":"h1","type":"circle","center":["length",0],"radius":3}],"constraints":[]}},
    {"id":"bar","type":"extrude","inputs":{"profile":{"sketch":"outline"},"distance":"5 mm",
      "result":{"new":"bar","body_type":"brep"}}}]}],
  "occurrences":[
    {"id":"ground","component":"link","grounded":true},
    {"id":"crank","component":"link","params":{"length":")" +
         number(crank) + R"( mm"},
     "placement":{"translation":[0,0,0],"rotation":[0,0,)" +
         number(theta + 5) + R"(]}},
    {"id":"coupler","component":"link","params":{"length":"120 mm"},
     "placement":{"translation":[)" +
         number(cx + 1) + "," + number(cy - 1) + R"(,0],"rotation":[0,0,-10]}},
    {"id":"rocker","component":"link","params":{"length":"80 mm"},
     "placement":{"translation":[100,0,0],"rotation":[0,0,80]}}],
  "joints":)" +
         joints + "}";
}

ir::Document parse(const std::string& text) {
  auto document = ir::read_document(text);
  if (!document)
    for (const auto& e : document.error()) UNSCOPED_INFO(e.pointer << ": " << e.message);
  REQUIRE(document);
  return std::move(*document);
}
math::Vec3 place(const Placement& p, math::Vec3 v) {
  const auto& r = p.rotation;
  return {r[0] * v.x + r[1] * v.y + r[2] * v.z + p.translation.x,
          r[3] * v.x + r[4] * v.y + r[5] * v.z + p.translation.y,
          r[6] * v.x + r[7] * v.y + r[8] * v.z + p.translation.z};
}
const AssemblyOccurrence& occurrence(const AssemblyResult& r, const char* id) {
  for (const auto& o : r.occurrences)
    if (o.id == id) return o;
  FAIL("no occurrence " << id);
  return r.occurrences.front();
}
/// Checks the solved linkage against the closed form for the crank direction it took.
void closes(const AssemblyResult& result, double crank) {
  REQUIRE(result.solved);
  CHECK(result.dof == 0);
  CHECK(result.loops == 1);
  for (const auto& j : result.joints) {
    CAPTURE(j.id);
    CHECK(j.residual_mm <= 1e-9);
    CHECK(j.residual_rad <= 1e-9);
    CHECK_FALSE(j.conflicting);
  }
  // Pin centres sit on the hole axes at mid-thickness (z = 2.5 mm).
  const auto c = place(occurrence(result, "crank").placement, {crank, 0, 2.5});
  CHECK_THAT(std::hypot(c.x, c.y), WithinAbs(crank, 1e-9));
  CHECK_THAT(c.z, WithinAbs(2.5, 1e-9));
  // The rocker tip: |R - (100, 0)| = 80 and |R - C| = 120, on the branch the solve kept.
  const double dx = 100 - c.x, dy = -c.y, d = std::hypot(dx, dy);
  const double a = (120 * 120 - 80 * 80 + d * d) / (2 * d), h = std::sqrt(120 * 120 - a * a);
  const double mx = c.x + a * dx / d, my = c.y + a * dy / d;
  const auto r = place(occurrence(result, "rocker").placement, {80, 0, 2.5});
  const double up = std::hypot(r.x - (mx - h * dy / d), r.y - (my + h * dx / d));
  const double down = std::hypot(r.x - (mx + h * dy / d), r.y - (my - h * dx / d));
  CHECK(std::min(up, down) <= 1e-9);
  CHECK_THAT(std::hypot(r.x - 100, r.y), WithinAbs(80, 1e-9));
  CHECK_THAT(std::hypot(r.x - c.x, r.y - c.y), WithinAbs(120, 1e-9));
}
}  // namespace

TEST_CASE("a four-bar of one parametric link closes at the closed-form position", "[assembly]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {64, 64 * 1024 * 1024});
  const auto result = evaluate_assembly(evaluator, *kernel, parse(four_bar()));
  if (!result)
    for (const auto& e : result.error()) UNSCOPED_INFO(e.pointer << ": " << e.message);
  REQUIRE(result);
  closes(*result, 40);
  // The crank turned by the driven 60 degrees about its pin (either sense of the hole axis).
  const auto c = place(occurrence(*result, "crank").placement, {40, 0, 2.5});
  CHECK_THAT(std::abs(std::atan2(c.y, c.x)), WithinAbs(kPi / 3, 1e-9));
  // Each placed bar keeps the exact volume of its component.
  for (const auto& [id, length] : {std::pair{"ground", 100.0}, {"crank", 40.0}, {"rocker", 80.0}}) {
    CAPTURE(id);
    const auto& o = occurrence(*result, id);
    REQUIRE(o.bodies.size() == 1);
    const double volume = ((length + 16) * 16 - 2 * kPi * 9) * 5;
    CHECK_THAT(o.bodies.front().second->measures().volume, WithinAbs(volume, 1e-6));
  }
}

TEST_CASE("assembly edits re-derive frames and re-solve on the same branch", "[assembly]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {64, 64 * 1024 * 1024});
  // A longer crank moves the frames on its far pin; a new drive angle moves the whole loop.
  for (const auto& [crank, theta] :
       {std::pair{40.0, 60.0}, {45.0, 60.0}, {45.0, 30.0}, {40.0, 90.0}, {40.0, 60.0}}) {
    CAPTURE(crank, theta);
    const auto result = evaluate_assembly(evaluator, *kernel, parse(four_bar(crank, theta)));
    REQUIRE(result);
    closes(*result, crank);
    const auto c = place(occurrence(*result, "crank").placement, {crank, 0, 2.5});
    CHECK_THAT(std::abs(std::atan2(c.y, c.x)), WithinAbs(theta * kPi / 180, 1e-9));
  }
}

TEST_CASE("over-driven or unsupported assemblies fail with pointers", "[assembly]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {64, 64 * 1024 * 1024});
  const auto conflict = evaluate_assembly(evaluator, *kernel, parse(four_bar(40, 60, "150")));
  REQUIRE_FALSE(conflict);
  INFO(conflict.error().front().message);
  CHECK(conflict.error().front().pointer.starts_with("/joints/"));
  CHECK(conflict.error().front().message.find("joints conflict") != std::string::npos);
  const auto unsupported =
      evaluate_assembly(evaluator, *kernel, parse(four_bar(40, 60, nullptr, R"(,"x-note":"ok")")));
  REQUIRE(unsupported);  // x- keys are allowed in selectors
  auto filtered = four_bar();
  const std::string plain_end = R"("from":"outline/h0"})";
  filtered.replace(filtered.find(plain_end), plain_end.size(),
                   R"("from":"outline/h0","where":{"type":"cylinder"}})");
  const auto rejected = evaluate_assembly(evaluator, *kernel, parse(filtered));
  REQUIRE_FALSE(rejected);
  CHECK(rejected.error().front().code == "unsupported");
  CHECK(rejected.error().front().pointer == "/joints/0/a/frame/where");
  // A part without occurrences is not an assembly.
  auto plain = ir::Document{};
  const auto empty = evaluate_assembly(evaluator, *kernel, plain);
  REQUIRE(empty);
  CHECK(empty->occurrences.empty());
}
