// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "nf/ir/expression.hpp"

namespace nf::ir {
/// Reusable, kernel-free preparation of one feature's numeric inputs (RFC 0005).
/// Schema annotations supply dimensions; strings used as IDs/options are never expressions.
class FeatureInputs {
 public:
  /// Checks the feature schema and compiles numeric expressions once. Cross-references must
  /// be checked in the containing document with read_document/write_document. Errors use
  /// pointers relative to the feature; a caller prepends /features/N for document diagnostics.
  /// Bounds input depth/size and aggregate instructions (one million), with ≤128 errors.
  /// Script and equation_curve require their own parameter-binding evaluator and currently
  /// return a kernel error, preventing an incomplete key from serving stale geometry.
  [[nodiscard]] static std::expected<FeatureInputs, Errors> compile(const Feature& feature);
  /// Produces a new input object with numeric values in mm/radians/scalars. Count and degree
  /// fields must be exact integers (1..2^53 and 1..9 respectively). Authored inputs are intact.
  /// x- extensions are opaque and omitted from evaluator inputs; v1 does not interpret them.
  /// Independent field errors are collected; no partial evaluated object is returned.
  /// This does not check geometric constraints, run a solver or execute kernel operations.
  [[nodiscard]] std::expected<json::Value, Errors> evaluate(
      const Units& units = {}, const ParameterValues& parameters = {}) const;

 private:
  struct Program;
  explicit FeatureInputs(std::shared_ptr<const Program> program) : program_(std::move(program)) {}
  std::shared_ptr<const Program> program_;
};
}  // namespace nf::ir
