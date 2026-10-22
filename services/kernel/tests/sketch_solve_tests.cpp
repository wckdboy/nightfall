// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// Sketch constraints are solved (RFC 0012) from the authored coordinates, and the independent
// checker verifies the result. Measured through the whole evaluator: a rough sketch becomes the
// exact solid its constraints describe.
#include "evaluator.hpp"
#include "part_fixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace nf;
using namespace nf::kernel_service;
using namespace nf::kernel_service::test;
using Catch::Matchers::WithinAbs;

namespace {
/// A four-line loop drawn roughly by hand: about 9 x 19 mm, slightly skewed, with width and
/// height as dimension constraints driven by parameters.
ir::Document rough_plate(const char* width = "10 mm", const char* height = "20 mm") {
  auto d = part(2);  // sketch and extrusion, no placement
  d.params.push_back({"width", width, {}, {}, {}, {}});
  d.params.push_back({"height", height, {}, {}, {}, {}});
  d.features[0].inputs = ir::json::parse(R"({"plane":{"datum":"xy"},"entities":[
    {"id":"origin","type":"point","at":[0,0]},
    {"id":"a","type":"line","start":[0.3,-0.2],"end":[9.1,0.4]},
    {"id":"b","type":"line","start":[9.1,0.4],"end":[8.7,19.3]},
    {"id":"c","type":"line","start":[8.7,19.3],"end":[-0.4,18.8]},
    {"id":"d","type":"line","start":[-0.4,18.8],"end":[0.3,-0.2]}],"constraints":[
    {"type":"fixed","entities":["origin"]},
    {"type":"coincident","entities":[{"entity":"a","point":"start"},"origin"]},
    {"type":"coincident","entities":[{"entity":"a","point":"end"},{"entity":"b","point":"start"}]},
    {"type":"coincident","entities":[{"entity":"b","point":"end"},{"entity":"c","point":"start"}]},
    {"type":"coincident","entities":[{"entity":"c","point":"end"},{"entity":"d","point":"start"}]},
    {"type":"coincident","entities":[{"entity":"d","point":"end"},{"entity":"a","point":"start"}]},
    {"type":"horizontal","entities":["a"]},
    {"type":"vertical","entities":["b"]},
    {"type":"parallel","entities":["a","c"]},
    {"type":"perpendicular","entities":["a","d"]},
    {"type":"distance","entities":[{"entity":"a","point":"start"},{"entity":"a","point":"end"}],"value":"width"},
    {"type":"distance","entities":[{"entity":"b","point":"start"},{"entity":"b","point":"end"}],"value":"height"}]})")
                             .value();
  return d;
}
void plate(const Evaluation& result, double width, double height) {
  for (const auto& f : result.features)
    for (const auto& e : f.errors) UNSCOPED_INFO(e.pointer << ": " << e.message);
  REQUIRE(result.ok());
  const auto& m = result.bodies.at("plate").shape->measures();
  // Depth 30 mm (part fixture). The solver converges residuals to 1e-9, so solved
  // coordinates are checked at 1e-8 mm, far inside the kernel's 1e-6 mm tolerance.
  CHECK_THAT(m.volume, WithinAbs(width * height * 30, 1e-6 * width * height * 30));
  CHECK_THAT(m.area, WithinAbs(2 * (width * height + 30 * (width + height)), 1e-6 * 3000));
  CHECK_THAT(m.minimum.x, WithinAbs(0, 1e-8));
  CHECK_THAT(m.minimum.y, WithinAbs(0, 1e-8));
  CHECK_THAT(m.maximum.x, WithinAbs(width, 1e-8));
  CHECK_THAT(m.maximum.y, WithinAbs(height, 1e-8));
  CHECK(m.faces == 6);
}
}  // namespace

TEST_CASE("a rough sketch solves to the exact solid its constraints describe", "[sketch-solve]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {32, 16 * 1024 * 1024});
  plate(evaluator.evaluate(source(rough_plate())).value(), 10, 20);
}

TEST_CASE("dimension parameters drive the solved sketch", "[sketch-solve]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {32, 16 * 1024 * 1024});
  // The authored coordinates stay rough; only the driving dimensions change.
  for (const auto& [w, h, width, height] : {std::tuple{"10 mm", "20 mm", 10.0, 20.0},
                                            {"25 mm", "8 mm", 25.0, 8.0},
                                            {"1 in", "2 cm", 25.4, 20.0},
                                            {"10 mm", "20 mm", 10.0, 20.0}}) {
    CAPTURE(w, h);
    plate(evaluator.evaluate(source(rough_plate(w, h))).value(), width, height);
  }
}

TEST_CASE("conflicting sketch constraints fail and name every conflict", "[sketch-solve]") {
  auto d = rough_plate();
  auto inputs = *d.features[0].inputs.if_object();
  for (auto& member : inputs)
    if (member.key == "constraints") {
      auto constraints = *member.value.if_array();
      // The same side cannot also be 12 mm long.
      constraints.push_back(ir::json::parse(R"({"type":"distance","entities":[
        {"entity":"a","point":"start"},{"entity":"a","point":"end"}],"value":"12 mm"})")
                                .value());
      member.value = std::move(constraints);
    }
  d.features[0].inputs = std::move(inputs);
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {32, 16 * 1024 * 1024});
  const auto result = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(result.ok());
  REQUIRE(result.features[0].errors.size() == 1);
  const auto& error = result.features[0].errors[0];
  INFO(error.message);
  // The first authored violation keeps its pointer and residual; the conflict is named.
  CHECK(error.pointer.starts_with("/features/0/inputs/constraints/"));
  CHECK(error.message.find("not satisfied") != std::string::npos);
  CHECK(error.message.find("sketch constraints conflict: /constraints/") != std::string::npos);
  CHECK(error.message.find("/constraints/12") != std::string::npos);
  CHECK_FALSE(result.bodies.at("plate").shape);
}

TEST_CASE("a solution that would collapse geometry is refused with the authored violation",
          "[sketch-solve]") {
  // A lone vertical line told to be horizontal: the least-squares answer is a point.
  auto d = part(2);
  d.features[0].inputs = ir::json::parse(R"({"plane":{"datum":"xy"},"entities":[
    {"id":"r","type":"rect","corner":[0,0],"size":[10,20]},
    {"id":"v","type":"line","start":[20,0],"end":[20,10]}],"constraints":[
    {"type":"horizontal","entities":["v"]}]})")
                             .value();
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {32, 16 * 1024 * 1024});
  const auto result = evaluator.evaluate(source(d)).value();
  REQUIRE_FALSE(result.ok());
  const auto& error = result.features[0].errors.at(0);
  INFO(error.message);
  CHECK(error.pointer == "/features/0/inputs/constraints/0");
  CHECK(error.message.find("horizontal constraint is not satisfied") != std::string::npos);
  CHECK(error.message.find("solving it would leave invalid geometry") != std::string::npos);
}

TEST_CASE("satisfied sketches keep their authored coordinates exactly", "[sketch-solve]") {
  // Already-exact coordinates: the checker passes them and the solver never runs, so the
  // profile is the authored one bit for bit, as before solving existed.
  auto exact = rough_plate();
  auto inputs = ir::json::canonical(exact.features[0].inputs);
  for (const auto& [from, to] : {std::pair{"[0.3,-0.2]", "[0,0]"},
                                 {"[9.1,0.4]", "[10,0]"},
                                 {"[8.7,19.3]", "[10,20]"},
                                 {"[-0.4,18.8]", "[0,20]"}}) {
    for (auto at = inputs.find(from); at != std::string::npos; at = inputs.find(from))
      inputs.replace(at, std::string_view(from).size(), to);
  }
  REQUIRE(inputs.find("0.3") == std::string::npos);
  exact.features[0].inputs = ir::json::parse(inputs).value();
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {32, 16 * 1024 * 1024});
  const auto solved = evaluator.evaluate(source(rough_plate())).value();
  const auto authored = evaluator.evaluate(source(exact)).value();
  plate(authored, 10, 20);
  const auto& a = authored.bodies.at("plate").shape->measures();
  const auto& s = solved.bodies.at("plate").shape->measures();
  CHECK(a.maximum.x == 10.0);
  CHECK(a.maximum.y == 20.0);
  CHECK_THAT(s.volume, WithinAbs(a.volume, 1e-9 * a.volume));  // 1e-8 mm coordinates
}
