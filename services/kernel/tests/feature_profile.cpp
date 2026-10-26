// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
// Private per-feature wall profile through the existing evaluator observer.
#include "evaluator.hpp"
#include "profiled_kernel.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>

using namespace nf;
int main(int argc, char** argv) {
  try {
    if (argc != 2) throw std::runtime_error("usage: nf_feature_profile document.nightfall");
    std::ifstream file(argv[1], std::ios::binary);
    if (!file) throw std::runtime_error("cannot read source");
    const std::string source{std::istreambuf_iterator<char>{file}, {}};
    const auto document = ir::read_document(source).value();
    auto backend = kernel::make_occt_kernel().value();
    kernel_service::test::ProfiledKernel traced(*backend);
    kernel_service::Evaluator evaluator(traced, {96, 64 * 1024 * 1024});
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    auto previous = start;
    std::optional<std::size_t> active;
    std::vector<double> times(document.features.size());
    const auto observe = [&](std::size_t index) {
      const auto now = Clock::now();
      if (active)
        times.at(*active) = std::chrono::duration<double, std::milli>(now - previous).count();
      active = index;
      previous = now;
      traced.feature = document.features.at(index).id;
    };
    const auto result = evaluator.evaluate(source, observe).value();
    const auto end = Clock::now();
    if (active)
      times.at(*active) = std::chrono::duration<double, std::milli>(end - previous).count();
    if (!result.ok()) throw std::runtime_error("feature build failed");
    ir::json::Array features;
    for (std::size_t i = 0; i < document.features.size(); ++i)
      features.push_back(
          ir::json::Object{{"id", document.features[i].id},
                           {"type", document.features[i].type},
                           {"observer_interval_ms", times[i]},
                           {"native_calls_wall_ms", traced.native_ms[document.features[i].id]}});
    std::size_t bodies = 0, solids = 0;
    for (const auto& [id, body] : result.bodies) {
      (void)id;
      if (body.shape) {
        ++bodies;
        solids += body.shape->measures().solids;
      }
    }
    const auto usage = evaluator.cache_usage();
    std::puts(
        ir::json::canonical(
            ir::json::Object{
                {"input_sha256", ir::sha256(source).hex()},
                {"kernel", std::string(backend->identity())},
                {"bodies", static_cast<double>(bodies)},
                {"solids", static_cast<double>(solids)},
                {"evaluation_ms", std::chrono::duration<double, std::milli>(end - start).count()},
                {"kernel_calls", static_cast<double>(result.kernel_calls)},
                {"cache_hits", static_cast<double>(result.cache_hits)},
                {"cache_entries", static_cast<double>(usage.entries)},
                {"cache_estimated_bytes", static_cast<double>(usage.estimated_bytes)},
                {"scope",
                 "wall intervals between feature-start callbacks; includes preparation/naming; not "
                 "pure solver CPU"},
                {"native_calls", std::move(traced.calls)},
                {"attribution_scope",
                 "native call wall time excludes trace construction; residual includes evaluator "
                 "and profiler overhead; IDs are transient diagnostics"},
                {"features", std::move(features)}})
            .c_str());
  } catch (const std::exception& error) {
    std::fprintf(stderr, "feature profile: %s\n", error.what());
    return 1;
  }
}
