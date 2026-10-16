// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "kernel.hpp"
#include "parts.hpp"

namespace nf::bakeoff {

/// RFC 0001 correctness bar: relative error against the analytic value.
inline constexpr double kRelativeTolerance = 1e-6;

struct PartResult {
  std::string name;
  std::string category;
  bool built = false;     ///< The operation script finished without a kernel error.
  std::string error;      ///< Why it didn't, when it didn't.
  bool measured = false;  ///< `measures` were taken (false when building or measuring failed).
  Measures measures;
  double build_ms = 0.0;
  std::optional<bool> volume_ok;  ///< Unset when the part has no volume expectation.
  std::optional<bool> area_ok;

  /// Built, valid, and every stated expectation met.
  [[nodiscard]] bool success() const {
    return built && measured && measures.valid && volume_ok.value_or(true) &&
           area_ok.value_or(true);
  }
};

struct RunReport {
  std::string kernel;
  std::string version;
  HistorySupport history = HistorySupport::kNo;
  std::vector<PartResult> parts;
};

/// Builds and measures one part. Kernel errors and exceptions are recorded, not thrown.
/// A crash still takes down the process; run parts in separate processes when that matters.
[[nodiscard]] PartResult run_part(Kernel& kernel, const Part& part);

[[nodiscard]] RunReport run_all(Kernel& kernel, const std::vector<Part>& parts);

/// The report as JSON (one object; stable key order).
[[nodiscard]] std::string to_json(const RunReport& report);

}  // namespace nf::bakeoff
