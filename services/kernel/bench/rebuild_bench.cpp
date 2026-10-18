// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
// Real IR parse/validation/evaluation/kernel-validation benchmark. No IPC or tessellation.
#include "../tests/part_fixture.hpp"
#include "evaluator.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <numbers>
#include <stdexcept>

using namespace nf;
using namespace nf::kernel_service;

namespace {
constexpr int kSamples = 30;
struct Part {
  const char* name;
  ir::Document document;
  double area, center_x;
  std::size_t transforms = 28, cold_calls = 29, depth_calls = 29, depth_hits = 1;
  bool scale_profile_with_depth = false;
  double center_z_per_depth = 0.5;
};
void verify(const Evaluation& result, const Part& part, double depth, double tail,
            std::size_t calls, std::size_t hits) {
  for (const auto& feature : result.features)
    for (const auto& error : feature.errors)
      std::fprintf(stderr, "%s: %s\n", error.pointer.c_str(), error.message.c_str());
  if (!result.ok() || result.kernel_calls != calls || result.cache_hits != hits)
    throw std::runtime_error("unexpected evaluator status or execution count");
  const auto& m = result.bodies.at("plate").shape->measures();
  const auto translation = 3 * static_cast<double>(part.transforms - 1) + tail;
  const auto profile_scale = part.scale_profile_with_depth ? depth / 30 : 1;
  if (std::abs(m.volume - part.area * profile_scale * depth) > 1e-5 ||
      std::abs(m.centroid.x - (part.center_x + translation)) > 1e-6 ||
      std::abs(m.centroid.z - depth * part.center_z_per_depth) > 1e-6)
    throw std::runtime_error("analytic geometry verification failed");
}
void measure(kernel::IKernel& backend, const Part& part, const std::string& mode) {
  // Explicit benchmark budget, not a proposed service-wide default.
  Evaluator evaluator(backend, {96, 64 * 1024 * 1024});
  auto document = part.document;
  auto warm = evaluator.evaluate(test::source(document)).value();
  verify(warm, part, 30, 3, part.cold_calls, 0);
  std::vector<double> milliseconds;
  std::size_t calls = 0, hits = 0;
  for (int sample = 0; sample < kSamples; ++sample) {
    double depth = 30, tail = 3;
    if (mode == "cold") evaluator.clear_cache();
    if (mode == "depth edit") {
      depth = 31 + sample;
      document.params[0].value = std::to_string(depth) + " mm";
    }
    if (mode == "last transform edit") {
      tail = 4 + sample;
      document.params[3].value = std::to_string(tail) + " mm";
    }
    if (mode == "unused edit") document.params[2].value = static_cast<double>(sample + 2);
    const auto text = test::source(document);  // authoring/serialization outside timed rebuild
    const auto start = std::chrono::steady_clock::now();
    auto evaluation = evaluator.evaluate(text);
    const auto stop = std::chrono::steady_clock::now();
    if (!evaluation) throw std::runtime_error("document validation failed");
    calls = mode == "unused edit"           ? 0
            : mode == "last transform edit" ? 1
            : mode == "depth edit"          ? part.depth_calls
                                            : part.cold_calls;
    hits = mode == "cold"          ? 0
           : mode == "depth edit"  ? part.depth_hits
           : mode == "unused edit" ? 30
                                   : 29;
    verify(*evaluation, part, depth, tail, calls, hits);
    milliseconds.push_back(std::chrono::duration<double, std::milli>(stop - start).count());
  }
  std::ranges::sort(milliseconds);
  const auto usage = evaluator.cache_usage();
  std::printf(
      "%-12s %-19s median %7.3f ms  p95 %7.3f ms  max %7.3f ms  kernel %zu  hits %zu  cache %zu / "
      "%zu bytes\n",
      part.name, mode.c_str(), (milliseconds[14] + milliseconds[15]) / 2, milliseconds[28],
      milliseconds.back(), calls, hits, usage.entries, usage.estimated_bytes);
  std::fflush(stdout);
}
}  // namespace

int main() {
  try {
    auto backend = kernel::make_occt_kernel().value();
    auto plate = test::part(30);
    plate.params.push_back({"tail", "3 mm", {}, {}, {}, {}});
    plate.features.back().inputs =
        ir::json::parse(R"({"bodies":["plate"],"translation":["tail",0,0]})").value();
    auto bored = plate;
    bored.features[0].inputs = ir::json::parse(R"({"plane":{"datum":"xy"},"entities":[
      {"id":"r","type":"rect","corner":[0,0],"size":[40,20]},
      {"id":"h1","type":"circle","center":[10,10],"radius":2},
      {"id":"h2","type":"circle","center":[30,10],"radius":2}],"constraints":[]})")
                                   .value();
    auto sleeve = plate;
    sleeve.features[0].inputs = ir::json::parse(R"({"plane":{"datum":"zx"},"entities":[
      {"id":"r","type":"rect","corner":[0,5],"size":["depth",5]}],"constraints":[]})")
                                    .value();
    sleeve.features[1] = test::feature("base", "revolve", R"({"profile":{"sketch":"outline"},
      "axis":{"origin":[0,0,0],"direction":"z"},"angle":360,
      "result":{"new":"plate","body_type":"brep"}})");
    auto torus = sleeve;
    torus.features[0].inputs = ir::json::parse(R"({"plane":{"datum":"zx"},"entities":[
      {"id":"section","type":"circle","center":[0,20],"radius":"depth / 10"}],
      "constraints":[]})")
                                   .value();
    auto elliptical = plate;
    elliptical.features[0].inputs = ir::json::parse(R"({"plane":{"datum":"xy"},"entities":[
      {"id":"outer","type":"ellipse","center":[0,0],"radii":["depth / 3",5],"rotation":35},
      {"id":"inner","type":"ellipse","center":[0,0],"radii":["depth / 6",2.5],"rotation":35}],
      "constraints":[]})")
                                        .value();
    auto shared = plate;
    shared.features[0].inputs = ir::json::parse(R"({"plane":{"datum":"xy"},"entities":[
      {"id":"left","type":"rect","corner":["-depth / 3",0],"size":["depth / 3",20]},
      {"id":"right","type":"rect","corner":[0,0],"size":["depth / 3",20]}],
      "constraints":[]})")
                                    .value();
    auto shared_arc = plate;
    shared_arc.features[0].inputs = ir::json::parse(R"({"plane":{"datum":"xy"},"entities":[
      {"id":"outer","type":"circle","center":[0,0],"radius":5},
      {"id":"cap","type":"arc","center":[0,0],"radius":5,"start_angle":0,"sweep_angle":90},
      {"id":"chord","type":"line","start":[0,5],"end":[5,0]}],"constraints":[]})")
                                        .value();
    const double shared_arc_area = 25 * (3 * std::numbers::pi / 4 + 0.5);
    auto cut = plate;
    cut.features.erase(cut.features.begin() + 2, cut.features.begin() + 5);
    cut.features.insert(cut.features.begin() + 2,
                        {test::feature("hole_profile", "sketch", R"({"plane":{"origin":[0,0,-5],
        "normal":[0,0,1],"x_direction":[1,0,0]},"entities":[
        {"id":"c","type":"circle","center":[5,10],"radius":2}],"constraints":[]})"),
                         test::feature("tool", "extrude", R"({"profile":{"sketch":"hole_profile"},
        "distance":100,"result":{"new":"drill","body_type":"brep"}})"),
                         test::feature("cut", "boolean",
                                       R"({"op":"subtract","target":"plate","tools":["drill"]})")});
    std::puts(
        "30-feature real B-rep rebuilds; 30 samples per case; parse/validation/evaluation "
        "included.");
    std::puts(
        "Unique edits; analytic volume/centroid and execution counts checked for every sample. No "
        "IPC/tessellation.");
    for (const auto& part :
         {Part{"plate", plate, 200, 5}, Part{"bored plate", bored, 800 - 8 * std::numbers::pi, 20},
          Part{"boolean cut", cut, 200 - 4 * std::numbers::pi, 5, 25, 28, 27, 3},
          Part{"sleeve", sleeve, 75 * std::numbers::pi, 0, 28, 29, 29, 0},
          Part{"elliptic tube", elliptical, 37.5 * std::numbers::pi, 0, 28, 29, 29, 0, true},
          Part{"shared edge", shared, 400, 0, 28, 29, 29, 0, true},
          Part{"shared arc", shared_arc, shared_arc_area, -125 / (6 * shared_arc_area)},
          Part{"toroidal seal", torus, 12 * std::numbers::pi * std::numbers::pi, 0, 28, 29, 29, 0,
               true, 0}})
      for (const auto& mode : {"cold", "depth edit", "last transform edit", "unused edit"})
        measure(*backend, part, mode);
  } catch (const std::exception& error) {
    std::fprintf(stderr, "benchmark failed: %s\n", error.what());
    return 1;
  }
}
