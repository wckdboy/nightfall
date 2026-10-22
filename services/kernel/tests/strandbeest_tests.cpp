// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// The Strandbeest showcase (examples/assemblies/strandbeest.nightfall): Theo Jansen's linkage,
// four legs on a two-throw crankshaft. Driven around a full turn, every toe must land where
// Jansen's linkage puts it, computed here independently by circle intersections.
#include "assembly.hpp"
#include "assembly_queries.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <map>
#include <numbers>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace nf;
using namespace nf::kernel_service;
using Catch::Matchers::WithinAbs;

namespace {
constexpr double kPi = std::numbers::pi;
// Jansen's holy numbers (mm), as the document's parameters default them.
constexpr double a = 38, b = 41.5, c = 39.3, d = 40.1, e = 55.8, f = 39.4, g = 36.7, h = 65.7,
                 i = 49, j = 50, k = 61.9, l = 7.8, m = 15;

struct P2 {
  double x, y;
};
/// The intersection of circles (p, rp) and (q, rq) on the left of p -> q (side +1) or right.
P2 meet(P2 p, double rp, P2 q, double rq, double side) {
  const double dx = q.x - p.x, dy = q.y - p.y, dist = std::hypot(dx, dy);
  const double along = (rp * rp - rq * rq + dist * dist) / (2 * dist);
  const double off = std::sqrt(rp * rp - along * along);
  const double mx = p.x + along * dx / dist, my = p.y + along * dy / dist;
  return {mx - side * off * dy / dist, my + side * off * dx / dist};
}
/// The toe of a leg whose pivot is at (-a, -l), mirrored in x when hand is -1.
P2 toe(double theta, double hand) {
  const P2 crank{hand * m * std::cos(theta), m * std::sin(theta)}, pivot{-a, -l};
  const auto knee = meet(crank, j, pivot, b, -1);
  const auto ankle = meet(crank, k, pivot, c, +1);
  const auto thigh = meet(pivot, d, knee, e, +1);
  const auto heel = meet(thigh, f, ankle, g, -1);
  const auto t = meet(heel, h, ankle, i, -1);
  return {hand * t.x, t.y};
}

ir::Document strandbeest(double crank_deg) {
  std::ifstream stream(NF_ASSEMBLY_EXAMPLES_DIR "/strandbeest.nightfall", std::ios::binary);
  REQUIRE(stream.is_open());
  auto document = ir::read_document(std::string{std::istreambuf_iterator<char>{stream}, {}});
  if (!document)
    for (const auto& error : document.error())
      UNSCOPED_INFO(error.pointer << ": " << error.message);
  REQUIRE(document);
  for (auto& p : document->params)
    if (p.name == "crank") p.value = ir::json::format_number(crank_deg) + " deg";
  return std::move(*document);
}
const AssemblyOccurrence& occurrence(const AssemblyResult& r, const std::string& id) {
  for (const auto& o : r.occurrences)
    if (o.id == id) return o;
  FAIL("no occurrence " << id);
  return r.occurrences.front();
}
}  // namespace

TEST_CASE("the Strandbeest walks on Jansen's closed-form toe path", "[assembly][showcase]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {256, 256 * 1024 * 1024});
  // The foot triangle's toe in its own frame: ankle at the origin, heel on +x.
  const double toe_x = (g * g + i * i - h * h) / (2 * g), toe_y = std::sqrt(i * i - toe_x * toe_x);
  for (double crank = 0; crank < 360; crank += 45) {
    CAPTURE(crank);
    const auto result = evaluate_assembly(evaluator, *kernel, strandbeest(crank));
    if (!result)
      for (const auto& error : result.error())
        UNSCOPED_INFO(error.pointer << ": " << error.message);
    REQUIRE(result);
    REQUIRE(result->solved);
    CHECK(result->dof == 0);
    CHECK(result->loops == 12);
    CHECK(result->occurrences.size() == 26);
    REQUIRE(result->joints.size() == 37);
    for (const auto& joint : result->joints) {
      CAPTURE(joint.id);
      CHECK(joint.residual_mm <= 1e-9);
      CHECK(joint.residual_rad <= 1e-9);
    }
    // The back pair runs half a turn behind the front pair.
    for (const auto& [leg, hand, phase] :
         {std::tuple{"fl", 1.0, 0.0}, std::tuple{"fr", -1.0, 0.0}, std::tuple{"bl", 1.0, 180.0},
          std::tuple{"br", -1.0, 180.0}}) {
      CAPTURE(leg);
      const auto& p = occurrence(*result, std::string("foot_") + leg).placement;
      const auto& r = p.rotation;
      const double x = r[0] * toe_x + r[1] * hand * toe_y + p.translation.x;
      const double y = r[3] * toe_x + r[4] * hand * toe_y + p.translation.y;
      const auto expected = toe((crank + phase) * kPi / 180, hand);
      CHECK_THAT(x, WithinAbs(expected.x, 1e-9));
      CHECK_THAT(y, WithinAbs(expected.y, 1e-9));
    }
  }
}

TEST_CASE("the Strandbeest's solved link outlines have their closed-form volumes",
          "[assembly][showcase]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {256, 256 * 1024 * 1024});
  const auto result = evaluate_assembly(evaluator, *kernel, strandbeest(30));
  REQUIRE(result);
  // A link is a 12 mm stadium around two 3 mm pin holes, 4 mm thick; each outline was drawn
  // roughly at 50 mm and solved from its constraints.
  for (const auto& [id, length] :
       {std::pair{"j_fl", j}, std::pair{"k_fr", k}, std::pair{"c_bl", c}, std::pair{"f_br", f}}) {
    CAPTURE(id);
    const auto& o = occurrence(*result, id);
    REQUIRE(o.bodies.size() == 1);
    const double volume = (length * 12 + kPi * 36 - 2 * kPi * 9) * 4;
    CHECK_THAT(o.bodies.front().second->measures().volume, WithinAbs(volume, 1e-9 * volume));
  }
  // The rocker: a 12 mm wide rounded triangle around its three pin holes (sides b, e, d). Its
  // area is the triangle, three side strips and one full disc, less the holes.
  const double s = (b + e + d) / 2, triangle = std::sqrt(s * (s - b) * (s - e) * (s - d));
  const double rocker = (triangle + 6 * (b + e + d) + kPi * 36 - 3 * kPi * 9) * 4;
  CHECK_THAT(occurrence(*result, "rocker_fl").bodies.front().second->measures().volume,
             WithinAbs(rocker, 1e-9 * rocker));
}

TEST_CASE("the Strandbeest's links clear each other and the pins over a full turn",
          "[assembly][showcase]") {
  // Parts on the hip pins sit nearest the frame and the crank links outermost, so no pin
  // passes through a link it does not carry. The motion study found the earlier stacking
  // drove each lower crank link through a hip pin for about 40 degrees of every turn.
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {256, 256 * 1024 * 1024});
  for (double crank = 0; crank < 360; crank += 45) {
    CAPTURE(crank);
    const auto result = evaluate_assembly(evaluator, *kernel, strandbeest(crank));
    REQUIRE(result);
    std::vector<PlacedBody> bodies;
    for (const auto& o : result->occurrences)
      for (const auto& [id, shape] : o.bodies) bodies.emplace_back(o.id + "/" + id, shape);
    REQUIRE(bodies.size() == 32);
    std::size_t checked = 0;
    const auto found = check_interference(*kernel, bodies, {}, &checked);
    REQUIRE(found);
    for (const auto& clash : *found)
      UNSCOPED_INFO(clash.a << " / " << clash.b << ": " << clash.volume << " mm^3");
    CHECK(found->empty());
    CHECK(checked > 0);  // the broad phase passed real candidates to the exact check
  }
}

TEST_CASE("the Strandbeest's mass is balanced front to back at every crank angle",
          "[assembly][showcase][mass]") {
  // The back half mirrors the front half about the frame's mid-plane, z = 2 mm, at every
  // pose (the back pair runs half a turn behind), so the centre of mass stays on that plane.
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {256, 256 * 1024 * 1024});
  for (const double crank : {0.0, 75.0, 210.0}) {
    CAPTURE(crank);
    const auto document = strandbeest(crank);
    const auto result = evaluate_assembly(evaluator, *kernel, document);
    REQUIRE(result);
    std::map<std::string, double> density;
    std::vector<PlacedBody> bodies;
    double expected = 0;
    for (const auto& o : result->occurrences) {
      const auto& component = *std::ranges::find(
          document.components,
          std::ranges::find(document.occurrences, o.id, &ir::Occurrence::id)->component,
          &ir::Component::id);
      REQUIRE(component.density);
      density[o.id] = *component.density;
      for (const auto& [id, shape] : o.bodies) {
        bodies.emplace_back(o.id + "/" + id, shape);
        expected += *component.density * shape->measures().volume * 1e-9;
      }
    }
    const auto mass = mass_properties(*kernel, bodies, density).value();
    CHECK(mass.without_density.empty());
    CHECK_THAT(mass.total.mass, WithinAbs(expected, 1e-9 * expected));
    CHECK_THAT(mass.total.centroid.z, WithinAbs(2, 1e-9));
  }
}
