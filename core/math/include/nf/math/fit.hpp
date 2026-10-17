// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// Fitting NURBS curves to points (The NURBS Book ch. 9). These are approximate
// constructions in the sense of RFC 0004: results carry their measured deviation.

#pragma once

#include <expected>
#include <span>
#include <string>
#include <vector>

#include "nf/math/nurbs_curve.hpp"

namespace nf::math {

/// The non-rational curve of `degree` through every point in order (A9.1): chord-length
/// parameters on [0, 1] (Eq. 9.5) and knots by averaging (Eq. 9.8). Needs more than `degree`
/// points, no two consecutive ones equal.
template <typename Point>
[[nodiscard]] std::expected<NurbsCurve<Point>, std::string> interpolate(
    std::span<const Point> points, int degree = 3);

template <typename Point>
struct Fit {
  NurbsCurve<Point> curve;
  std::vector<double> parameters;  // the parameter assigned to each data point
  /// max |C(parameters[k]) − points[k]|: an upper bound on every data point's distance to
  /// the curve.
  double max_deviation;
};

/// A non-rational curve of `degree` whose least-squares fit (A9.7, knots by Eq. 9.69, ends
/// interpolated) keeps every data point within `tolerance`. Parameters are chord lengths,
/// improved by parameter correction for as long as each round helps by 1 % (up to 200
/// rounds) — or `parameters`, one per point rising strictly from 0 to 1, when the caller knows
/// better ones (an equation's own t), kept as given. The number of control points is
/// searched by doubling, then bisection: usually the fewest, but not always, since fit
/// quality is not monotone in the count (each count has its own knots). Needs more than
/// `degree` points, a positive finite tolerance, and, without parameters, no two
/// consecutive points equal; a tolerance below the fit's floating-point accuracy is an error.
template <typename Point>
[[nodiscard]] std::expected<Fit<Point>, std::string> approximate(
    std::span<const Point> points, double tolerance, int degree = 3,
    std::span<const double> parameters = {});

}  // namespace nf::math
