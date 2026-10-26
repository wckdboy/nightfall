// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// RFC 0014 studies run from documents: the cantilever and thermal slab examples against their
// analytic answers, criteria, provenance, and every failure with its JSON Pointer.
#include "study.hpp"

#include <cmath>
#include <fstream>
#include <iterator>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace nf;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using ir::json::Value;

namespace {
std::string example(const char* name) {
  std::ifstream stream(std::string(NF_STUDY_EXAMPLES_DIR) + "/" + name, std::ios::binary);
  REQUIRE(stream.is_open());
  return {std::istreambuf_iterator<char>{stream}, {}};
}
std::string replaced(std::string text, const std::string& from, const std::string& to) {
  const auto at = text.find(from);
  REQUIRE(at != std::string::npos);
  return text.replace(at, from.size(), to);
}
struct Runner {
  std::unique_ptr<kernel::IKernel> kernel = kernel::make_occt_kernel().value();
  kernel_service::Evaluator evaluator{*kernel, {64, 64 * 1024 * 1024}};
  Value run(const std::string& source, const std::string& id, study::RunOptions options = {}) {
    auto result = study::run_study(evaluator, *kernel, source, id, options);
    if (!result)
      for (const auto& e : result.error()) UNSCOPED_INFO(e.pointer << ": " << e.message);
    REQUIRE(result);
    return Value(*result);
  }
  ir::Error error(const std::string& source, const std::string& id) {
    auto result = study::run_study(evaluator, *kernel, source, id);
    REQUIRE_FALSE(result);
    REQUIRE_FALSE(result.error().empty());
    return result.error().front();
  }
};
double number(const Value& v, const char* key) {
  REQUIRE(v.find(key));
  REQUIRE(v.find(key)->if_number());
  return *v.find(key)->if_number();
}
}  // namespace

TEST_CASE("the cantilever study converges to beam theory with balanced reactions", "[study][run]") {
  Runner runner;
  const auto source = example("cantilever.nightfall");
  // Three sizes in a constant ratio give an observed convergence order.
  const auto result = runner.run(source, "tip_load", {std::nullopt, {5.0, 10.0 / 3, 20.0 / 9}});
  const double P = 10, L = 200, E = 69000, I = 10.0 * 1000 / 12;
  const double beam = P * L * L * L / (3 * E * I);
  const auto& runs = *result.find("runs")->if_array();
  REQUIRE(runs.size() == 3);
  std::string previous;
  for (const auto& run : runs) {
    CAPTURE(number(run, "mesh_size"));
    CHECK_THAT(number(*run.find("quantities"), "max_displacement"), WithinRel(beam, 0.02));
    const auto& applied = *run.find("applied_force")->if_array();
    const auto& reaction = *run.find("reaction_force")->if_array();
    CHECK_THAT(*applied[2].if_number(), WithinAbs(-P, 1e-9 * P));
    for (std::size_t c = 0; c < 3; ++c)
      CHECK_THAT(*applied[c].if_number() + *reaction[c].if_number(), WithinAbs(0, 1e-9 * P));
    CHECK(number(*run.find("solver"), "relative_residual") <= 1e-12);
    const auto mesh = *run.find("mesh_sha256")->if_string();
    CHECK(mesh != previous);
    previous = mesh;
  }
  // Quadratic elements converge faster than first order on this smooth bending field.
  CHECK(number(*result.find("convergence_order"), "max_displacement") > 1);
  CHECK(*result.find("passed")->if_bool());
  const auto& criteria = *result.find("criteria")->if_array();
  REQUIRE(criteria.size() == 2);
  CHECK(*criteria[0].find("passed")->if_bool());
  const auto& provenance = *result.find("provenance");
  CHECK(provenance.find("document_sha256")->if_string()->size() == 64);
  CHECK(provenance.find("body_hash")->if_string()->size() == 64);
  CHECK(provenance.find("mesher")->if_string()->starts_with("netgen 6.2.2604 3ee489c7"));
  CHECK(provenance.find("solver")->if_string()->starts_with("mfem 4.10 d964264c"));
}

TEST_CASE("the thermal slab study conducts kA dT / t exactly", "[study][run]") {
  Runner runner;
  const auto result = runner.run(example("thermal_slab.nightfall"), "conduction");
  const double q = 167 * 0.01 * 100 / 0.01;  // W
  const auto& heat = *result.find("runs")->if_array()->front().find("heat_in")->if_array();
  REQUIRE(heat.size() == 2);
  CHECK_THAT(*heat[0].if_number(), WithinRel(-q, 1e-9));  // out through the 300 K face
  CHECK_THAT(*heat[1].if_number(), WithinRel(q, 1e-9));   // in through the 400 K face
  CHECK_THAT(number(*result.find("quantities"), "max_temperature"), WithinAbs(400, 1e-9));
  CHECK(*result.find("passed")->if_bool());
}

TEST_CASE("criteria fail when the result crosses their limits", "[study][run]") {
  Runner runner;
  const auto source =
      replaced(example("cantilever.nightfall"), R"("below": "0.5 mm")", R"("below": "0.4 mm")");
  const auto result = runner.run(source, "tip_load");
  CHECK_FALSE(*result.find("passed")->if_bool());
  const auto& criteria = *result.find("criteria")->if_array();
  CHECK_FALSE(*criteria[0].find("passed")->if_bool());
  CHECK(*criteria[1].find("passed")->if_bool());
}

TEST_CASE("study errors carry pointers", "[study][run]") {
  Runner runner;
  const auto source = example("cantilever.nightfall");
  auto e = runner.error(source, "missing");
  CHECK(e.code == "unknown_id");
  CHECK(e.pointer == "/studies");
  // A finite number whose SI value overflows is refused like a wrong unit.
  e = runner.error(replaced(source, R"("69 GPa")", R"("1e306 GPa")"), "tip_load");
  CHECK(e.code == "unit");
  CHECK(e.pointer == "/studies/0/material/youngs_modulus");
  e = runner.error(replaced(source, R"("69 GPa")", R"("69 GPascal")"), "tip_load");
  CHECK(e.code == "unit");
  CHECK(e.pointer == "/studies/0/material/youngs_modulus");
  // A bare number is refused rather than read as newtons.
  e = runner.error(replaced(source, R"("-10 N")", R"("-10")"), "tip_load");
  CHECK(e.code == "unit");
  CHECK(e.pointer == "/studies/0/boundaries/1/value/2");
  e = runner.error(replaced(source, R"("role": "cap_end")", R"("role": "nothing")"), "tip_load");
  CHECK(e.pointer == "/studies/0/boundaries/1/faces");
  e = runner.error(replaced(source, R"("type": "fixed")", R"("type": "temperature")"), "tip_load");
  CHECK(e.pointer == "/studies/0/boundaries/0/type");
  e = runner.error(replaced(source, R"("body": "beam")", R"("body": "plate")"), "tip_load");
  CHECK(e.code == "unknown_id");
  CHECK(e.pointer == "/studies/0/body");
  // A mesh size naming an unknown parameter points at the size, not at nothing.
  e = runner.error(replaced(source, R"("size": "3.3 mm")", R"("size": "pitch / 2")"), "tip_load");
  CHECK(e.pointer == "/studies/0/mesh/size");
}
