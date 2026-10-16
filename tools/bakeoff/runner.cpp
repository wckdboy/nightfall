// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include "runner.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <string_view>

namespace nf::bakeoff {

namespace {

bool within_relative(double actual, double expected) {
  return std::abs(actual - expected) <= kRelativeTolerance * std::abs(expected);
}

std::string_view history_name(HistorySupport h) {
  switch (h) {
    case HistorySupport::kYes:
      return "yes";
    case HistorySupport::kPartial:
      return "partial";
    case HistorySupport::kNo:
      return "no";
  }
  return "no";
}

void append_string(std::string& out, std::string_view s) {
  out += '"';
  for (const char c : s) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof buf, "\\u%04x", c);
          out += buf;
        } else {
          out += c;
        }
    }
  }
  out += '"';
}

void append_number(std::string& out, double v) {
  if (!std::isfinite(v)) {
    out += "null";
    return;
  }
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.17g", v);
  out += buf;
}

void append_optional_bool(std::string& out, std::optional<bool> v) {
  out += v.has_value() ? (*v ? "true" : "false") : "null";
}

}  // namespace

PartResult run_part(Kernel& kernel, const Part& part) {
  PartResult result{.name = part.name, .category = part.category};
  const auto start = std::chrono::steady_clock::now();
  try {
    const Body body = part.build(kernel);
    result.build_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    result.built = true;
    result.measures = kernel.measure(body);
  } catch (const std::exception& e) {
    result.error = e.what();
    return result;
  }
  if (part.expect.volume)
    result.volume_ok = within_relative(result.measures.volume, *part.expect.volume);
  if (part.expect.area) result.area_ok = within_relative(result.measures.area, *part.expect.area);
  return result;
}

RunReport run_all(Kernel& kernel, const std::vector<Part>& parts) {
  RunReport report{.kernel = std::string(kernel.name()),
                   .version = kernel.version(),
                   .history = kernel.history_support()};
  for (const Part& part : parts) report.parts.push_back(run_part(kernel, part));
  return report;
}

std::string to_json(const RunReport& report) {
  std::string out = "{\"kernel\":";
  append_string(out, report.kernel);
  out += ",\"version\":";
  append_string(out, report.version);
  out += ",\"history_support_declared\":";
  append_string(out, history_name(report.history));
  out += ",\"relative_tolerance\":";
  append_number(out, kRelativeTolerance);
  out += ",\"parts\":[";
  for (std::size_t i = 0; i < report.parts.size(); ++i) {
    const PartResult& p = report.parts[i];
    if (i > 0) out += ',';
    out += "{\"name\":";
    append_string(out, p.name);
    out += ",\"category\":";
    append_string(out, p.category);
    out += ",\"success\":";
    out += p.success() ? "true" : "false";
    out += ",\"built\":";
    out += p.built ? "true" : "false";
    out += ",\"error\":";
    append_string(out, p.error);
    out += ",\"valid\":";
    out += p.measures.valid ? "true" : "false";
    out += ",\"volume\":";
    append_number(out, p.measures.volume);
    out += ",\"area\":";
    append_number(out, p.measures.area);
    out += ",\"faces\":" + std::to_string(p.measures.faces);
    out += ",\"edges\":" + std::to_string(p.measures.edges);
    out += ",\"volume_ok\":";
    append_optional_bool(out, p.volume_ok);
    out += ",\"area_ok\":";
    append_optional_bool(out, p.area_ok);
    out += ",\"build_ms\":";
    append_number(out, p.build_ms);
    out += '}';
  }
  out += "]}\n";
  return out;
}

}  // namespace nf::bakeoff
