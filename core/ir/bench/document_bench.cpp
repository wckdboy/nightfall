// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
// Document I/O baseline: parsing, schema and reference validation; no geometry evaluation.
// Run build/release/core/ir/nf_ir_bench after building the nf_ir_bench release target.

#include <algorithm>
#include <chrono>
#include <cstdio>

#include "nf/ir/document.hpp"
#include "nf/ir/hash.hpp"
#include "nf/ir/inputs.hpp"
#include "nf/ir/parameters.hpp"

using namespace nf::ir;
using json::Array;
using json::Object;
using json::Value;

namespace {
Document part(std::size_t count) {
  Document d;
  Parameter wall;
  wall.name = "wall";
  wall.value = "3 mm";
  d.params.push_back(wall);
  const auto add = [&](std::string id, std::string type, Value inputs) {
    Feature f;
    f.id = std::move(id);
    f.type = std::move(type);
    f.inputs = std::move(inputs);
    d.features.push_back(std::move(f));
  };
  add("outline", "sketch",
      Object{{"plane", Object{{"datum", "xy"}}},
             {"entities", Array{Object{{"id", "r"},
                                       {"type", "rect"},
                                       {"corner", Array{0.0, 0.0}},
                                       {"size", Array{40.0, 20.0}}}}},
             {"constraints", Array{}}});
  add("base", "extrude",
      Object{{"profile", Object{{"sketch", "outline"}}},
             {"distance", "wall"},
             {"result", Object{{"new", "plate"}, {"body_type", "brep"}}}});
  for (std::size_t i = 2; i < count; ++i)
    add("move_" + std::to_string(i), "transform",
        Object{{"bodies", Array{"plate"}}, {"translation", Array{1.0, 0.0, 0.0}}});
  return d;
}

template <class F>
double microseconds(F run) {
  std::vector<double> samples;
  volatile std::size_t sink = 0;
  for (int trial = 0; trial < 5; ++trial) {
    const auto start = std::chrono::steady_clock::now();
    std::size_t total = 0;
    for (int i = 0; i < 100; ++i) total += run(i);
    samples.push_back(
        std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start)
            .count() /
        100);
    sink = sink + total;
  }
  std::ranges::sort(samples);
  return samples[2];
}
}  // namespace

int main() {
  std::puts(
      "IR load/save including schema + reference validation (median of 5 x 100, warm schema):");
  for (std::size_t count : {30u, 300u}) {
    auto document = part(count);
    const auto text = write_document(document).value();
    const double load =
        microseconds([&](int) { return read_document(text).value().features.size(); });
    const double save = microseconds([&](int i) {
      document.params[0].value = i % 2 == 0 ? "3 mm" : "4 mm";
      return write_document(document).value().size();
    });
    std::printf("  %zu features, %zu bytes: load %.1f us; edit + save %.1f us\n", count,
                text.size(), load, save);
    std::vector<FeatureInputs> programs;
    for (const auto& feature : document.features)
      programs.push_back(FeatureInputs::compile(feature).value());
    const double hash_inputs = microseconds([&](int edit) {
      document.params[0].value = edit % 2 == 0 ? "3 mm" : "4 mm";
      const auto values = evaluate_parameters(document.params).value().values;
      Hash upstream;
      std::optional<Hash> body;
      for (std::size_t i = 0; i < programs.size(); ++i) {
        const auto inputs = programs[i].evaluate(document.units, values).value();
        const auto key = feature_hash("input-bench-v1", document.features[i].type, inputs,
                                      {&upstream, i == 0 ? 0u : 1u});
        if (i > 0) body = body_hash(body, key);
        upstream = body.value_or(key);
      }
      return static_cast<std::size_t>(upstream.bytes[0]);
    });
    std::printf(
        "  %zu prepared features: parameter edit + inputs + chained hashes %.1f us "
        "(no geometry)\n",
        count, hash_inputs);
    std::vector<Parameter> params;
    for (std::size_t i = 0; i < count; ++i) {
      Parameter p;
      p.name = "p" + std::to_string(i);
      p.value = i == 0 ? "1 mm" : "p" + std::to_string(i - 1) + " + 1 mm";
      p.min = "0 mm";
      params.push_back(std::move(p));
    }
    const double resolve = microseconds([&](int i) {
      params[0].value = i % 2 == 0 ? "1 mm" : "2 mm";
      return evaluate_parameters(params).value().values.size();
    });
    std::printf("  %zu chained parameters: edit + compile + evaluate + bounds %.1f us\n", count,
                resolve);
  }
}
