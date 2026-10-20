// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "evaluator.hpp"

#include <unistd.h>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>

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
Value point(math::Vec3 p) { return Array{p.x, p.y, p.z}; }
Object feature_counts(const kernel_service::Evaluation& evaluation) {
  std::array<std::size_t, 5> counts{};
  for (const auto& f : evaluation.features) ++counts[static_cast<std::size_t>(f.status)];
  return {{"built", numeric(counts[0])},
          {"cached", numeric(counts[1])},
          {"suppressed", numeric(counts[2])},
          {"rolled_back", numeric(counts[3])},
          {"failed", numeric(counts[4])}};
}
constexpr std::string_view kUsage =
    "nf build part.nightfall --out part.step [--set name=expression ...]";
[[noreturn]] void fail(std::string code, std::string message) {
  throw ir::Errors{{std::move(code), std::move(message), "", {}}};
}
[[noreturn]] void io_error() { fail("io", std::strerror(errno)); }
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
std::string apply_overrides(std::string source, std::map<std::string, std::string> values) {
  if (values.empty()) return source;
  auto document = ir::read_document(source);
  if (!document) throw document.error();
  for (auto& parameter : document->params) {
    const auto found = values.find(parameter.name);
    if (found == values.end()) continue;
    parameter.value = found->second;
    values.erase(found);
  }
  if (!values.empty())
    throw ir::Errors{
        {"unknown_id", "unknown parameter override: " + values.begin()->first, "/params", {}}};
  // Validate the final graph and bounds once all definitions have been
  // replaced.
  auto encoded = ir::write_document(*document);
  if (!encoded) throw encoded.error();
  return std::move(*encoded);
}
std::string load(const fs::path& path) {
  if (!fs::is_regular_file(path)) fail("io", "input must be a regular file");
  std::ifstream stream(path, std::ios::binary);
  if (!stream) fail("io", "cannot open input");
  std::array<char, 8192> buffer;
  std::string text;
  while (stream) {
    stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto count = static_cast<std::size_t>(stream.gcount());
    if (count > ir::kMaxDocumentBytes - text.size())
      fail("parse", "document exceeds the 16 MiB limit");
    text.append(buffer.data(), count);
  }
  if (!stream.eof()) fail("io", "cannot read input");
  return text;
}
void publish(const fs::path& output, std::string_view text) {
  auto temporary = (output.parent_path() / ".nightfall-XXXXXX").string();
  int file = mkstemp(temporary.data());
  if (file < 0) io_error();
  try {
    while (!text.empty()) {
      const auto count = ::write(file, text.data(), text.size());
      if (count < 0 && errno == EINTR) continue;
      if (count < 0) io_error();
      if (count == 0) fail("io", "output write made no progress");
      text.remove_prefix(static_cast<std::size_t>(count));
    }
    if (fsync(file) != 0) io_error();
    const int closed = close(file);
    file = -1;
    if (closed != 0) io_error();
    if (std::rename(temporary.c_str(), output.c_str()) != 0) io_error();
  } catch (...) {
    if (file >= 0) close(file);
    std::remove(temporary.c_str());
    throw;
  }
}
}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--help") {
    std::cout << kUsage << '\n';
    return 0;
  }
  Object evidence;
  try {
    auto arguments = parse_arguments(argc, argv);
    const fs::path input(arguments.input), output = fs::absolute(arguments.output);
    if (fs::weakly_canonical(input) == fs::weakly_canonical(output) ||
        (fs::exists(output) && fs::equivalent(input, output)))
      fail("io", "output must not alias the input document");
    auto loaded = load(input);
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
    const auto source = apply_overrides(std::move(loaded), std::move(arguments.overrides));
    evidence.push_back({"effective_source_sha256", ir::sha256(source).hex()});
    const auto tolerance = math::Tolerance::standard();
    auto kernel = kernel::make_occt_kernel(tolerance);
    if (!kernel) fail(kernel.error().code, kernel.error().message);
    evidence.push_back({"kernel", std::string((*kernel)->identity())});
    evidence.push_back({"units", Object{{"length", "mm"}, {"area", "mm^2"}, {"volume", "mm^3"}}});
    evidence.push_back({"comparison_tolerances", Object{{"linear_mm", tolerance.linear},
                                                        {"angular_rad", tolerance.angular}}});
    kernel_service::Evaluator evaluator(**kernel, {96, 64 * 1024 * 1024});
    const auto evaluation_start = Clock::now();
    auto evaluated = evaluator.evaluate(source);
    const double evaluation_ms = elapsed(evaluation_start);
    if (!evaluated) return report(evaluated.error(), 1, evidence);
    evidence.push_back({"feature_status_counts", feature_counts(*evaluated)});
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
        const auto& m = body.shape->measures();
        metrics.emplace_back(Object{{"id", id},
                                    {"state_hash", body.hash->hex()},
                                    {"volume", m.volume},
                                    {"area", m.area},
                                    {"centroid", point(m.centroid)},
                                    {"minimum", point(m.minimum)},
                                    {"maximum", point(m.maximum)},
                                    {"solids", numeric(m.solids)},
                                    {"faces", numeric(m.faces)},
                                    {"edges", numeric(m.edges)},
                                    {"name_records", numeric(body.names.size())}});
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
    publish(output, *encoded);
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
