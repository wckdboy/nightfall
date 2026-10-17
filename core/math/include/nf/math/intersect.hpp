// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// Intersections of planar NURBS curves by subdivision of their rational Bézier pieces:
// candidates are isolated with rigorous exclusion tests (control-point boxes; signed
// distances to a line), then solved by Newton.

#pragma once

#include <vector>

#include "nf/math/nurbs_curve.hpp"

namespace nf::math {

struct CurveCurveHit {
  double ua, ub;  // parameters on the first and second curve
  Vec2 point;     // A(ua), equal to B(ub) within the solver's tolerance
};

struct CurveCurveIntersection {
  std::vector<CurveCurveHit> hits;  // ordered by ua
  /// The curves share a stretch (infinitely many common points); `hits` then holds only
  /// isolated sample points and should not be used.
  bool overlap = false;
};

/// Points common to two planar curves: transversal crossings to ~1e-12 of the curves' size,
/// tangencies to ~1e-8 (the solution is ill-conditioned there). A point within that distance
/// of both curves is reported once.
[[nodiscard]] CurveCurveIntersection intersect(const NurbsCurve2& a, const NurbsCurve2& b);

struct CurveLineHit {
  double u;
  Vec2 point;  // C(u)
};

struct CurveLineIntersection {
  std::vector<CurveLineHit> hits;  // ordered by u
  bool overlap = false;            // part of the curve lies on the line
};

/// Points where the curve meets the infinite line through `origin` along `direction`
/// (non-zero). Throws std::invalid_argument for a zero direction.
[[nodiscard]] CurveLineIntersection intersect_line(const NurbsCurve2& curve, Vec2 origin,
                                                   Vec2 direction);

}  // namespace nf::math
