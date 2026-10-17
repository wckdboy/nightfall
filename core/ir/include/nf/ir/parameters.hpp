// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "nf/ir/expression.hpp"

namespace nf::ir {
struct EvaluatedParameters {
  ParameterValues values;
  /// Deterministic topological order; each value dependency precedes its consumer.
  std::vector<std::string> order;
  /// Direct value dependencies only, unique and in first-use order. Bounds are predicates
  /// checked after values resolve, so a bound may reference its own parameter's value.
  std::unordered_map<std::string, std::vector<std::string>> dependencies;
};

/// Evaluates RFC 0005 parameters without changing their authored values or using document
/// units. Declaration order is arbitrary; cyclic value dependencies are errors naming the
/// cycle (display capped at 64 names). Bounds are inclusive, dimension-checked and never clamp.
/// Invalid upstream values do not cause misleading unknown-ID errors in their dependents.
/// Reports up to 128 field diagnostics, with expression byte offsets when available.
/// Iterative graph traversal supports long chains; limits are 65,536 parameters and
/// 1,000,000 compiled instructions across values/bounds, in addition to per-expression limits.
/// Aggregate expression source is limited to kMaxDocumentBytes, including invalid expressions.
[[nodiscard]] std::expected<EvaluatedParameters, Errors> evaluate_parameters(
    std::span<const Parameter> parameters);
}  // namespace nf::ir
