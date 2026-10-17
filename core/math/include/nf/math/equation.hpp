// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// Curves from equations x(t), y(t), z(t) (RFC 0004): exact where NURBS can be (polynomials,
// conics), fitted to a tolerance with the measured deviation otherwise.

#pragma once

#include <expected>
#include <string>
#include <string_view>

#include "nf/math/nurbs_curve.hpp"

namespace nf::math {

struct EquationCurve {
  NurbsCurve3 curve;
  bool exact;            // a polynomial or a conic arc, represented exactly
  double max_deviation;  // 0 when exact; else the largest distance found from the equations
};

/// Expressions in t: numbers, t, pi, e, + − * / ^ (right-associative), parentheses, and sin
/// cos tan asin acos atan sinh cosh tanh exp log sqrt abs. The result traces the curve for t
/// in [t0, t1] (the NURBS parameter is not t, except for polynomials):
/// - every component a polynomial of degree ≤ 9: a Bézier curve, exact;
/// - every component A + B·cos(ωt + φ) + C·sin(ωt + φ) with one ω, sweeping at most a full
///   turn: an elliptical arc (rational quadratic, ≤ 90° per span), exact;
/// - otherwise a least-squares cubic through 256 samples with t as its parameter (up to 16384
///   samples when needed, e.g. for a kink), refitted until |C(t) − f(t)| at 4× denser t is
///   within `tolerance`; that measured distance is reported.
/// Errors: malformed expressions, an empty or non-finite range, non-finite values.
[[nodiscard]] std::expected<EquationCurve, std::string> equation_curve(std::string_view x,
                                                                       std::string_view y,
                                                                       std::string_view z,
                                                                       double t0, double t1,
                                                                       double tolerance = 1e-3);

}  // namespace nf::math
