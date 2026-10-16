// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// nf_bakeoff --kernel <occt|smlib> [--part <name>] [--repeat N] [--out <file.json>]
// --repeat N: one untimed warm-up build, then N timed builds; build_ms is their median.
// nf_bakeoff --list
// Runs the reference parts on one kernel and writes the JSON report (stdout by default).
// Exits non-zero only for usage or I/O errors: failed parts are results, not tool failures.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "runner.hpp"

namespace {

std::unique_ptr<nf::bakeoff::Kernel> make_kernel(std::string_view name) {
#if NF_BAKEOFF_WITH_OCCT
  if (name == "occt") return nf::bakeoff::make_occt_kernel();
#endif
#if NF_BAKEOFF_WITH_SMLIB
  if (name == "smlib") return nf::bakeoff::make_smlib_kernel();
#endif
  (void)name;
  return nullptr;
}

int usage() {
  std::cerr << "usage: nf_bakeoff --kernel <occt|smlib> [--part <name>] [--repeat N] [--out "
               "<file.json>]\n"
               "       nf_bakeoff --list\n";
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  std::string kernel_name;
  std::string out_path;
  std::string only_part;
  int repeat = 0;
  auto parts = nf::bakeoff::reference_parts();
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--kernel" && i + 1 < argc) {
      kernel_name = argv[++i];
    } else if (arg == "--out" && i + 1 < argc) {
      out_path = argv[++i];
    } else if (arg == "--repeat" && i + 1 < argc) {
      repeat = std::atoi(argv[++i]);
      if (repeat < 1) return usage();
    } else if (arg == "--part" && i + 1 < argc) {
      only_part = argv[++i];
    } else if (arg == "--list") {
      for (const auto& p : parts) std::cout << p.name << ' ' << p.category << '\n';
      return 0;
    } else {
      return usage();
    }
  }
  if (!only_part.empty()) {
    std::erase_if(parts, [&](const auto& p) { return p.name != only_part; });
    if (parts.empty()) {
      std::cerr << "unknown part: '" << only_part << "'\n";
      return usage();
    }
  }
  auto kernel = make_kernel(kernel_name);
  if (!kernel) {
    std::cerr << "unknown or disabled kernel: '" << kernel_name << "'\n";
    return usage();
  }

  auto report = nf::bakeoff::run_all(*kernel, parts);  // also the warm-up when repeating
  if (repeat > 0) {
    for (std::size_t i = 0; i < parts.size(); ++i) {
      if (!report.parts[i].built) continue;
      std::vector<double> samples;
      for (int r = 0; r < repeat; ++r)
        samples.push_back(nf::bakeoff::run_part(*kernel, parts[i]).build_ms);
      std::ranges::sort(samples);
      report.parts[i].build_ms = samples[samples.size() / 2];
    }
  }
  const std::string json = nf::bakeoff::to_json(report);
  if (out_path.empty()) {
    std::cout << json;
  } else {
    std::ofstream file(out_path);
    file << json;
    if (!file.flush()) {
      std::cerr << "cannot write " << out_path << "\n";
      return 1;
    }
  }

  int analytic = 0, passed = 0, consensus = 0, consensus_built = 0;
  for (const auto& p : report.parts) {
    if (p.judged_by_consensus()) {
      ++consensus;
      consensus_built += p.success() ? 1 : 0;
    } else {
      ++analytic;
      passed += p.success() ? 1 : 0;
    }
  }
  std::fprintf(stderr, "%s %s: %d/%d analytic parts passed; %d/%d consensus parts built valid\n",
               report.kernel.c_str(), report.version.c_str(), passed, analytic, consensus_built,
               consensus);
  return 0;
}
