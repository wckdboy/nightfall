// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// Geometric queries on NURBS curves by subdivision of their rational Bézier pieces. A piece
// lies inside its control points' bounding box, which bounds every search rigorously.

#pragma once

#include <vector>

#include "nf/math/nurbs_curve.hpp"

namespace nf::math {

template <typename Point>
struct CurvePoint {
  double u;
  Point point;  // C(u)
};

template <typename Point>
struct Closest {
  double u;
  Point point;  // C(u)
  double distance;
};

/// The point of the curve nearest to p: branch and bound over Bézier pieces (pruned by their
/// control-point boxes) down to 1e-3 of the curve's size, then Newton within each surviving
/// piece, with its ends as fallbacks. When several points are equally near (p at a circle's
/// centre) any one of them is returned.
template <typename Point>
[[nodiscard]] Closest<Point> closest_point(const NurbsCurve<Point>& curve, Point p);

/// A polyline through curve points, from start to end, including every knot, such that every
/// point of the curve lies within `tolerance` of it (pieces are split until their control
/// points are that close to the chord). Tolerances below 1e-12 of the curve's size are
/// raised to that. Throws std::invalid_argument unless tolerance is positive and finite.
template <typename Point>
[[nodiscard]] std::vector<CurvePoint<Point>> polyline(const NurbsCurve<Point>& curve,
                                                      double tolerance);

}  // namespace nf::math
