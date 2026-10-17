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
};
void verify(const Evaluation& result, double area, double depth, double center_x, double tail,
            std::size_t calls, std::size_t hits) {
  for (const auto& feature : result.features)
    for (const auto& error : feature.errors)
      std::fprintf(stderr, "%s: %s\n", error.pointer.c_str(), error.message.c_str());
  if (!result.ok() || result.kernel_calls != calls || result.cache_hits != hits)
    throw std::runtime_error("unexpected evaluator status or execution count");
  const auto& m = result.bodies.at("plate").shape->measures();
  if (std::abs(m.volume - area * depth) > 1e-5 ||
      std::abs(m.centroid.x - (center_x + 81 + tail)) > 1e-6 ||
      std::abs(m.centroid.z - depth / 2) > 1e-6)
    throw std::runtime_error("analytic geometry verification failed");
}
void measure(kernel::IKernel& backend, const Part& part, const std::string& mode) {
  // Explicit benchmark budget, not a proposed service-wide default.
  Evaluator evaluator(backend, {96, 64 * 1024 * 1024});
  auto document = part.document;
  auto warm = evaluator.evaluate(test::source(document)).value();
  verify(warm, part.area, 30, part.center_x, 3, 29, 0);
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
    calls = mode == "unused edit" ? 0 : mode == "last transform edit" ? 1 : 29;
    hits = mode == "cold" ? 0 : mode == "depth edit" ? 1 : mode == "unused edit" ? 30 : 29;
    verify(*evaluation, part.area, depth, part.center_x, tail, calls, hits);
    milliseconds.push_back(std::chrono::duration<double, std::milli>(stop - start).count());
  }
  std::ranges::sort(milliseconds);
  const auto usage = evaluator.cache_usage();
  std::printf(
      "%-12s %-19s median %7.3f ms  p95 %7.3f ms  max %7.3f ms  kernel %zu  hits %zu  cache %zu / "
      "%zu bytes\n",
      part.name, mode.c_str(), (milliseconds[14] + milliseconds[15]) / 2, milliseconds[28],
      milliseconds.back(), calls, hits, usage.entries, usage.estimated_bytes);
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
    std::puts(
        "30-feature real B-rep rebuilds; 30 samples per case; parse/validation/evaluation "
        "included.");
    std::puts(
        "Unique edits; analytic volume/centroid and execution counts checked for every sample. No "
        "IPC/tessellation.");
    for (const auto& part :
         {Part{"plate", plate, 200, 5}, Part{"bored plate", bored, 800 - 8 * std::numbers::pi, 20}})
      for (const auto& mode : {"cold", "depth edit", "last transform edit", "unused edit"})
        measure(*backend, part, mode);
  } catch (const std::exception& error) {
    std::fprintf(stderr, "benchmark failed: %s\n", error.what());
    return 1;
  }
}
