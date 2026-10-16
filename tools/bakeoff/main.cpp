// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// nf_bakeoff --kernel <name> [--out <file.json>]
// Runs the reference parts on one kernel and writes the JSON report (stdout by default).
// Exits non-zero only for usage errors: failed parts are results, not tool failures.

#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

#include "runner.hpp"

namespace {

std::unique_ptr<nf::bakeoff::Kernel> make_kernel(std::string_view name) {
#if NF_BAKEOFF_WITH_OCCT
  if (name == "occt") return nf::bakeoff::make_occt_kernel();
#endif
  (void)name;
  return nullptr;
}

int usage() {
  std::cerr << "usage: nf_bakeoff --kernel <occt> [--out <file.json>]\n";
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  std::string kernel_name;
  std::string out_path;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--kernel" && i + 1 < argc) {
      kernel_name = argv[++i];
    } else if (arg == "--out" && i + 1 < argc) {
      out_path = argv[++i];
    } else {
      return usage();
    }
  }
  auto kernel = make_kernel(kernel_name);
  if (!kernel) {
    std::cerr << "unknown or disabled kernel: '" << kernel_name << "'\n";
    return usage();
  }

  const auto report = nf::bakeoff::run_all(*kernel, nf::bakeoff::reference_parts());
  const std::string json = nf::bakeoff::to_json(report);
  if (out_path.empty()) {
    std::cout << json;
  } else {
    std::ofstream(out_path) << json;
  }

  int passed = 0;
  for (const auto& p : report.parts) passed += p.success() ? 1 : 0;
  std::fprintf(stderr, "%s %s: %d/%zu parts passed\n", report.kernel.c_str(),
               report.version.c_str(), passed, report.parts.size());
  return 0;
}
