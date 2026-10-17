// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "evaluator.hpp"
#include "part_fixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace nf;
using namespace nf::kernel_service;
using namespace nf::kernel_service::test;
using Catch::Matchers::WithinAbs;

namespace {
ir::Document shared_part(const char* entities, const char* rule = "evenodd") {
  auto d = part(2);
  d.features[0].inputs = ir::json::Object{{"plane", ir::json::Object{{"datum", "xy"}}},
                                          {"entities", ir::json::parse(entities).value()},
                                          {"constraints", ir::json::Array{}}};
  d.features[1].inputs =
      ir::json::Object{{"profile", ir::json::Object{{"sketch", "outline"}, {"fill_rule", rule}}},
                       {"distance", "depth"},
                       {"result", ir::json::Object{{"new", "plate"}, {"body_type", "brep"}}}};
  return d;
}
void check_names(const BodyState& body) {
  REQUIRE_FALSE(body.names.empty());
  for (const auto& name : body.names) CHECK(name.entity->owner() == body.shape);
}
}  // namespace

TEST_CASE("shared straight profiles extrude adjacent rectangles as one solid", "[shared]") {
  auto d = shared_part(R"([
    {"id":"a","type":"rect","corner":[0,0],"size":[10,20]},
    {"id":"b","type":"rect","corner":[10,0],"size":[10,20]}])");
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {32, 16 * 1024 * 1024});
  const auto r = evaluator.evaluate(source(d)).value();
  REQUIRE(r.ok());
  const auto& body = r.bodies.at("plate");
  const auto& m = body.shape->measures();
  CHECK(m.solids == 1);
  CHECK_THAT(m.volume, WithinAbs(12000, 1e-8));
  CHECK_THAT(m.area, WithinAbs(3200, 1e-8));
  CHECK_THAT(m.centroid.x, WithinAbs(10, 1e-9));
  CHECK_THAT(m.centroid.y, WithinAbs(10, 1e-9));
  CHECK_THAT(m.centroid.z, WithinAbs(15, 1e-9));
  check_names(body);
}

TEST_CASE("shared straight profiles preserve nonzero and evenodd extrusion semantics", "[shared]") {
  for (const auto* rule : {"evenodd", "nonzero"}) {
    const auto d = shared_part(R"([
      {"id":"a","type":"rect","corner":[0,0],"size":[10,20]},
      {"id":"b","type":"rect","corner":[5,0],"size":[10,20]}])",
                               rule);
    auto kernel = kernel::make_occt_kernel().value();
    Evaluator evaluator(*kernel, {32, 16 * 1024 * 1024});
    const auto r = evaluator.evaluate(source(d)).value();
    REQUIRE(r.ok());
    const auto& body = r.bodies.at("plate");
    const bool evenodd = std::string_view(rule) == "evenodd";
    CHECK(body.shape->measures().solids == (evenodd ? 2 : 1));
    CHECK_THAT(body.shape->measures().volume, WithinAbs(evenodd ? 6000 : 9000, 1e-8));
    CHECK_THAT(body.shape->measures().centroid.x, WithinAbs(7.5, 1e-9));
    check_names(body);
  }
}

TEST_CASE("shared straight profile edits invalidate dependents and recover cached geometry",
          "[shared]") {
  auto d = shared_part(R"([
    {"id":"a","type":"rect","corner":[0,0],"size":[10,20]},
    {"id":"b","type":"rect","corner":[10,0],"size":["width",20]}])");
  d.params.push_back({"width", "10 mm", {}, {}, {}, {}});
  d.features.push_back(
      feature("move", "transform", R"({"bodies":["plate"],"translation":[3,0,0]})"));
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {32, 16 * 1024 * 1024});
  const auto first = evaluator.evaluate(source(d)).value();
  REQUIRE(first.ok());
  d.params.back().value = "15 mm";
  const auto edited = evaluator.evaluate(source(d)).value();
  REQUIRE(edited.ok());
  CHECK(edited.kernel_calls == 2);
  CHECK(edited.cache_hits == 0);
  CHECK_THAT(edited.bodies.at("plate").shape->measures().volume, WithinAbs(15000, 1e-8));
  CHECK_THAT(edited.bodies.at("plate").shape->measures().centroid.x, WithinAbs(15.5, 1e-9));
  check_names(edited.bodies.at("plate"));
  CHECK_THAT(first.bodies.at("plate").shape->measures().volume, WithinAbs(12000, 1e-8));
  d.params.back().value = "1 cm";
  const auto restored = evaluator.evaluate(source(d)).value();
  REQUIRE(restored.ok());
  CHECK(restored.kernel_calls == 0);
  CHECK(restored.cache_hits == 3);
  CHECK(restored.bodies.at("plate").shape == first.bodies.at("plate").shape);
}

TEST_CASE("duplicate straight profiles report empty fill and recover under nonzero", "[shared]") {
  const char* entities = R"([
    {"id":"a","type":"rect","corner":[0,0],"size":[10,20]},
    {"id":"b","type":"rect","corner":[0,0],"size":[10,20]}])";
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {32, 16 * 1024 * 1024});
  auto d = shared_part(entities);
  d.features.push_back(
      feature("move", "transform", R"({"bodies":["plate"],"translation":[3,0,0]})"));
  const auto failed = evaluator.evaluate(source(d)).value();
  CHECK_FALSE(failed.ok());
  CHECK(failed.kernel_calls == 0);
  REQUIRE_FALSE(failed.features[1].errors.empty());
  CHECK(failed.features[1].errors[0].pointer == "/features/1/inputs/profile");
  CHECK(failed.features[1].errors[0].message == "profile has no filled regions");
  REQUIRE_FALSE(failed.features[2].errors.empty());
  CHECK(failed.features[2].errors[0].code == "input_failed");
  CHECK_FALSE(failed.bodies.at("plate").shape);
  const auto recovered = evaluator.evaluate(source(shared_part(entities, "nonzero"))).value();
  REQUIRE(recovered.ok());
  CHECK_THAT(recovered.bodies.at("plate").shape->measures().volume, WithinAbs(6000, 1e-8));
  CHECK(recovered.cache_hits == 1);
}
