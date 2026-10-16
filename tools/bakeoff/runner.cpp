// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include "runner.hpp"

#include <charconv>
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

// Length of the valid UTF-8 sequence starting at s[i], or 0 if it is invalid
// (overlong forms, surrogates and code points above U+10FFFF are rejected).
std::size_t utf8_sequence_length(std::string_view s, std::size_t i) {
  const auto byte = [&](std::size_t k) { return static_cast<unsigned char>(s[k]); };
  const unsigned char lead = byte(i);
  std::size_t n = 0;
  unsigned char lo = 0x80, hi = 0xBF;  // allowed range of the second byte
  if (lead >= 0xC2 && lead <= 0xDF) {
    n = 2;
  } else if (lead >= 0xE0 && lead <= 0xEF) {
    n = 3;
    if (lead == 0xE0) lo = 0xA0;
    if (lead == 0xED) hi = 0x9F;
  } else if (lead >= 0xF0 && lead <= 0xF4) {
    n = 4;
    if (lead == 0xF0) lo = 0x90;
    if (lead == 0xF4) hi = 0x8F;
  } else {
    return 0;
  }
  if (i + n > s.size()) return 0;
  if (byte(i + 1) < lo || byte(i + 1) > hi) return 0;
  for (std::size_t k = 2; k < n; ++k) {
    if (byte(i + k) < 0x80 || byte(i + k) > 0xBF) return 0;
  }
  return n;
}

// JSON string; invalid UTF-8 (e.g. kernel messages in a legacy encoding) becomes U+FFFD so
// the report always parses.
void append_string(std::string& out, std::string_view s) {
  out += '"';
  for (std::size_t i = 0; i < s.size();) {
    const auto c = static_cast<unsigned char>(s[i]);
    if (c >= 0x80) {
      const std::size_t n = utf8_sequence_length(s, i);
      if (n == 0) {
        out += "\\ufffd";
        ++i;
      } else {
        out.append(s.substr(i, n));
        i += n;
      }
      continue;
    }
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
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof buf, "\\u%04x", c);
          out += buf;
        } else {
          out += static_cast<char>(c);
        }
    }
    ++i;
  }
  out += '"';
}

// Shortest representation that round-trips, independent of the C locale.
void append_number(std::string& out, double v) {
  if (!std::isfinite(v)) {
    out += "null";
    return;
  }
  char buf[32];
  const auto [end, ec] = std::to_chars(buf, buf + sizeof buf, v);
  out.append(buf, ec == std::errc{} ? end : buf);
}

// A measurement that was never taken is null, never a misleading 0.
void append_measured(std::string& out, bool measured, double v) {
  if (measured) {
    append_number(out, v);
  } else {
    out += "null";
  }
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
    result.measured = true;
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
    out += p.measured ? (p.measures.valid ? "true" : "false") : "null";
    out += ",\"volume\":";
    append_measured(out, p.measured, p.measures.volume);
    out += ",\"area\":";
    append_measured(out, p.measured, p.measures.area);
    out += ",\"faces\":";
    append_measured(out, p.measured, p.measures.faces);
    out += ",\"edges\":";
    append_measured(out, p.measured, p.measures.edges);
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
