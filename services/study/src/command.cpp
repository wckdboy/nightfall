// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// `nf study run <document> <study> [--size L] [--refine a,b,c]`: runs one study and prints its
// result as JSON. `nf study solve <part.step> <spec.json> [...]` runs a study object on a STEP
// part, its faces picked by points (#317). Exit 0 when it ran and every criterion passed, 3 when a
// criterion failed, 1 on an error (printed as {"errors": [...]}), 2 on usage.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

#include "study.hpp"

namespace nf::study {
namespace {
std::optional<std::string> slurp(const char* path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) return std::nullopt;
  return std::string{std::istreambuf_iterator<char>{stream}, {}};
}
std::optional<double> number(std::string_view s) {
  const std::string copy(s);
  char* end = nullptr;
  const double v = std::strtod(copy.c_str(), &end);
  if (copy.empty() || end != copy.c_str() + copy.size() || !(v > 0) || !std::isfinite(v))
    return std::nullopt;
  return v;
}
int usage(const char* message) {
  std::fprintf(stderr,
               "nf study: %s\nusage: nf study run <document> <study> [--size mm] "
               "[--refine mm,mm,mm]\n       nf study solve <part.step> <spec.json> [--size mm] "
               "[--refine mm,mm,mm]\n",
               message);
  return 2;
}
}  // namespace

int run_study_command(int argc, char** argv) {
  const bool solve = argc >= 3 && std::string_view(argv[2]) == "solve";
  if (argc < 5 || (!solve && std::string_view(argv[2]) != "run"))
    return usage("expected run <document> <study> or solve <part.step> <spec.json>");
  RunOptions options;
  for (int i = 5; i < argc; i += 2) {
    const std::string_view option(argv[i]);
    if (i + 1 == argc) return usage("missing option value");
    const std::string_view text(argv[i + 1]);
    if (option == "--size") {
      options.size = number(text);
      if (!options.size) return usage("--size needs a positive length in mm");
    } else if (option == "--refine") {
      for (std::size_t start = 0; start <= text.size();) {
        const auto comma = std::min(text.find(',', start), text.size());
        const auto v = number(text.substr(start, comma - start));
        if (!v) return usage("--refine needs positive lengths in mm, separated by commas");
        options.refine.push_back(*v);
        start = comma + 1;
      }
    } else {
      return usage("unknown option");
    }
  }
  const auto source = slurp(argv[3]);
  if (!source) return usage(solve ? "cannot read the STEP part" : "cannot read the document");
  auto kernel = kernel::make_occt_kernel();
  if (!kernel) return usage("the kernel is unavailable");
  kernel_service::Evaluator evaluator(**kernel, {64, 64 * 1024 * 1024});
  std::expected<ir::json::Object, ir::Errors> result;
  if (solve) {
    const auto spec = slurp(argv[4]);
    if (!spec) return usage("cannot read the study");
    result = run_step_study(**kernel, *source, *spec, options);
  } else {
    result = run_study(evaluator, **kernel, *source, argv[4], options);
  }
  if (!result) {
    ir::json::Array errors;
    for (const auto& e : result.error())
      errors.push_back(
          ir::json::Object{{"code", e.code}, {"message", e.message}, {"pointer", e.pointer}});
    std::printf("%s\n",
                ir::json::canonical(ir::json::Object{{"errors", std::move(errors)}}).c_str());
    return 1;
  }
  std::printf("%s\n", ir::json::canonical(*result).c_str());
  return *ir::json::Value(*result).find("passed")->if_bool() ? 0 : 3;
}
}  // namespace nf::study
