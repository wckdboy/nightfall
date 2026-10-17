// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <span>
#include <unordered_map>

#include "nf/ir/document.hpp"

namespace nf::ir {
/// Integer powers of length and angle, each in [-64, 64]. Base units are mm and radians.
struct Dimension {
  int length = 0, angle = 0;
  bool operator==(const Dimension&) const = default;
};
inline constexpr Dimension kScalar{}, kLength{1, 0}, kAngle{0, 1};
struct Quantity {
  double value = 0;
  Dimension dimension;
  bool operator==(const Quantity&) const = default;
};
using ParameterValues = std::unordered_map<std::string, Quantity>;

/// Immutable, reusable parameter expression, parsed by libnf-math (RFC 0005).
class Expression {
 public:
  struct Symbol {
    std::string name;
    std::size_t offset;
  };
  [[nodiscard]] static std::expected<Expression, Error> parse(std::string_view text);
  /// Parameters resolve before the constants pi/e; t has no implicit value. Numeric unit
  /// suffixes are mm/cm/m/in/ft/deg/rad. Trig takes angles, inverse trig returns angles;
  /// hyperbolic functions, exp and log take scalars. sqrt/powers require integral resulting
  /// dimension powers. Domain/overflow errors use code bounds; dimension errors use unit.
  /// Errors have the root pointer and a source offset; callers attach the field pointer.
  [[nodiscard]] std::expected<Quantity, Error> evaluate(const ParameterValues& params = {}) const;
  /// Unique bare names, in first-use order, including pi/e when present in the source.
  [[nodiscard]] std::span<const Symbol> references() const;
  [[nodiscard]] std::size_t instruction_count() const;

 private:
  struct Program;
  explicit Expression(std::shared_ptr<const Program> program) : program_(std::move(program)) {}
  std::shared_ptr<const Program> program_;
};

/// A parameter value: a JSON number is dimensionless; a string carries its own dimensions.
[[nodiscard]] std::expected<Quantity, Error> evaluate_value(const json::Value& value,
                                                            const ParameterValues& params = {});
/// A feature field: bare JSON numbers take the document units raised to the required powers.
/// Expressions must already have the required dimension; strings like "2" are dimensionless.
/// The returned finite double is in mm/radians. These functions do not modify stored inputs.
[[nodiscard]] std::expected<double, Error> evaluate_input(const json::Value& value,
                                                          Dimension required,
                                                          const Units& units = {},
                                                          const ParameterValues& params = {});
}  // namespace nf::ir
