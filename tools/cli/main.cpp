// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include <chrono>
#include <iostream>
#include <map>

#include "mcp.hpp"
#include "session.hpp"

namespace {
namespace fs = std::filesystem;
using namespace nf;
using ir::json::Array;
using ir::json::Object;
using ir::json::Value;
using Clock = std::chrono::steady_clock;
double elapsed(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
Value numeric(std::size_t n) { return static_cast<double>(n); }
constexpr std::string_view kUsage =
    "nf build part.nightfall --out part.step [--set name=expression ...]\n"
    "nf mcp [--in part.nightfall] [--allow-path DIR ...]";
[[noreturn]] void fail(std::string code, std::string message) {
  throw ir::Errors{{std::move(code), std::move(message), "", {}}};
}
int report(const ir::Errors& errors, int status = 1, const Object& evidence = {}) {
  ir::json::Array entries;
  for (const auto& e : errors) {
    ir::json::Object entry{{"code", e.code}, {"message", e.message}, {"pointer", e.pointer}};
    if (e.offset) entry.push_back({"offset", static_cast<double>(*e.offset)});
    entries.push_back(std::move(entry));
  }
  Object result{{"errors", std::move(entries)}};
  if (!evidence.empty()) result.push_back({"evidence", evidence});
  std::cerr << ir::json::canonical(result) << '\n';
  return status;
}
struct Arguments {
  std::string input, output;
  std::map<std::string, std::string> overrides;
};
Arguments parse_arguments(int argc, char** argv) {
  auto usage = [](std::string message) {
    fail("usage", std::move(message) + "; usage: " + std::string(kUsage));
  };
  if (argc < 3 || std::string_view(argv[1]) != "build") usage("expected a build input");
  Arguments result{argv[2], {}, {}};
  const auto letter = [](char c) { return (c >= 'a' && c <= 'z') || c == '_'; };
  for (int i = 3; i < argc; i += 2) {
    const std::string_view option(argv[i]);
    if (i + 1 == argc) usage("missing option value");
    const std::string_view value(argv[i + 1]);
    if (option == "--out") {
      if (!result.output.empty()) usage("--out must be specified once");
      if (value.empty()) usage("output path must not be empty");
      result.output = value;
    } else if (option == "--set") {
      const auto equal = value.find('=');
      if (equal == std::string_view::npos || equal == 0 || equal + 1 == value.size())
        usage("--set requires name=expression");
      const auto name = value.substr(0, equal);
      if (name.size() > 64 || !letter(name.front())) usage("invalid parameter name");
      for (const char c : name)
        if (!letter(c) && !(c >= '0' && c <= '9')) usage("invalid parameter name");
      if (!result.overrides.emplace(name, value.substr(equal + 1)).second)
        usage("repeated parameter override: " + std::string(name));
    } else {
      usage("unknown option: " + std::string(option));
    }
  }
  if (result.output.empty()) usage("--out is required");
  return result;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--help") {
    std::cout << kUsage << '\n';
    return 0;
  }
  if (argc >= 2 && std::string_view(argv[1]) == "mcp") return cli::run_mcp(argc, argv);
  Object evidence;
  try {
    auto arguments = parse_arguments(argc, argv);
    const fs::path input(arguments.input), output = fs::absolute(arguments.output);
    if (fs::weakly_canonical(input) == fs::weakly_canonical(output) ||
        (fs::exists(output) && fs::equivalent(input, output)))
      fail("io", "output must not alias the input document");
    auto loaded = cli::load(input);
    const auto input_hash = ir::sha256(loaded).hex();
    Object overrides;
    for (const auto& [name, expression] : arguments.overrides)
      overrides.push_back({name, expression});
    evidence = {{"version", 1.},
                {"input_sha256", input_hash},
                {"request_sha256",
                 ir::sha256(ir::json::canonical(Object{{"input_sha256", input_hash},
                                                       {"overrides", std::move(overrides)}}))
                     .hex()}};
    auto source = cli::apply_overrides(std::move(loaded), std::move(arguments.overrides));
    if (!source) throw source.error();
    evidence.push_back({"effective_source_sha256", ir::sha256(*source).hex()});
    const auto tolerance = math::Tolerance::standard();
    auto kernel = kernel::make_occt_kernel(tolerance);
    if (!kernel) fail(kernel.error().code, kernel.error().message);
    evidence.push_back({"kernel", std::string((*kernel)->identity())});
    evidence.push_back({"units", Object{{"length", "mm"}, {"area", "mm^2"}, {"volume", "mm^3"}}});
    evidence.push_back({"comparison_tolerances", Object{{"linear_mm", tolerance.linear},
                                                        {"angular_rad", tolerance.angular}}});
    kernel_service::Evaluator evaluator(**kernel, {96, 64 * 1024 * 1024});
    const auto evaluation_start = Clock::now();
    auto evaluated = evaluator.evaluate(*source);
    const double evaluation_ms = elapsed(evaluation_start);
    if (!evaluated) return report(evaluated.error(), 1, evidence);
    evidence.push_back({"feature_status_counts", cli::feature_counts(*evaluated)});
    const auto usage = evaluator.cache_usage();
    evidence.push_back({"cache", Object{{"hits", numeric(evaluated->cache_hits)},
                                        {"kernel_calls", numeric(evaluated->kernel_calls)},
                                        {"binding_misses", numeric(evaluated->binding_misses)},
                                        {"entries", numeric(usage.entries)},
                                        {"estimated_bytes", numeric(usage.estimated_bytes)}}});
    if (!evaluated->ok()) {
      ir::Errors errors;
      for (const auto& f : evaluated->features)
        errors.insert(errors.end(), f.errors.begin(), f.errors.end());
      return report(errors, 1, evidence);
    }
    std::vector<kernel::ShapePtr> shapes;
    Array bodies, metrics;
    for (const auto& [id, body] : evaluated->bodies)
      if (body.shape) {
        shapes.push_back(body.shape);
        bodies.push_back(id);
        metrics.emplace_back(cli::body_metrics(id, body));
      }
    const auto export_start = Clock::now();
    auto encoded = (*kernel)->export_step(shapes);
    const double export_ms = elapsed(export_start);
    if (!encoded) fail(encoded.error().code, encoded.error().message);
    // Keep failure context free of success-only geometry/artifact claims until
    // publication.
    auto complete = evidence;
    complete.push_back({"body_metrics", std::move(metrics)});
    complete.push_back({"checks", Object{{"closed_solids", "kernel_validated"},
                                         {"step_roundtrip", "not_run"},
                                         {"physics", "not_run"}}});
    complete.push_back({"measurement_error_bounds", nullptr});
    complete.push_back(
        {"timing_ms", Object{{"evaluation", evaluation_ms}, {"step_export", export_ms}}});
    complete.push_back({"step", Object{{"sha256", ir::sha256(*encoded).hex()},
                                       {"bytes", numeric(encoded->size())}}});
    const auto receipt = ir::json::canonical(Object{{"output", arguments.output},
                                                    {"bodies", std::move(bodies)},
                                                    {"evidence", std::move(complete)}});
    cli::publish(output, *encoded);
    std::cout << receipt << '\n';
    return 0;
  } catch (const ir::Errors& errors) {
    return report(errors, !errors.empty() && errors.front().code == "usage" ? 2 : 1, evidence);
  } catch (const fs::filesystem_error& error) {
    return report({{"io", error.what(), "", {}}}, 1, evidence);
  } catch (const std::exception& error) {
    return report({{"internal", error.what(), "", {}}}, 1, evidence);
  }
}
