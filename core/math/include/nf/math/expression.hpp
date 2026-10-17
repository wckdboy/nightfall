// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// Real expressions in one variable t: the language of equation curves (RFC 0004), small and
// strict. Immutable values with shared storage.

#pragma once

#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nf::math {

class Expression {
 public:
  /// Numbers, t, pi, e, + − * / ^ (right-associative; −2^2 is −4), parentheses, and the
  /// functions sin cos tan asin acos atan sinh cosh tanh exp log sqrt abs. No implicit
  /// products ("2t" is an error). Errors name the problem and its byte offset.
  [[nodiscard]] static std::expected<Expression, std::string> parse(std::string_view text);

  /// The value at t (IEEE semantics: may be infinite or NaN).
  [[nodiscard]] double operator()(double t) const;

  /// Coefficients c₀ … cₙ of the expression as a polynomial in t, if it is one of degree
  /// ≤ 9, without trailing zeros (t·0 is the constant 0).
  [[nodiscard]] std::optional<std::vector<double>> polynomial() const;

  /// a + b·cos(ωt) + c·sin(ωt) with ω > 0; ω is unset for a constant (also when every
  /// oscillating term has zero amplitude).
  struct Sinusoid {
    double a = 0, b = 0, c = 0;
    std::optional<double> omega;
  };
  /// The expression in that form, if it is a sum of constants and constant multiples of
  /// sin/cos of one linear argument ωt + φ.
  [[nodiscard]] std::optional<Sinusoid> sinusoid() const;

  struct Node;  // opaque

 private:
  explicit Expression(std::shared_ptr<const Node> root) : root_(std::move(root)) {}
  std::shared_ptr<const Node> root_;
};

}  // namespace nf::math
