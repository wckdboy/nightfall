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
  /// sample points of it and should not be used as crossings.
  bool overlap = false;
};

/// Points common to two planar curves. Piece pairs are subdivided until both are flat (or
/// tiny), then solved; acceptance and merging use the size of the pieces involved, not of the
/// whole curves, so small features next to long spans are resolved. Transversal crossings come
/// out to ~1e-12 of the local size, tangencies to ~1e-8 (ill-conditioned there). A shared
/// stretch of any length sets `overlap`.
[[nodiscard]] CurveCurveIntersection intersect(const NurbsCurve2& a, const NurbsCurve2& b);

struct CurveLineHit {
  double u;
  Vec2 point;  // C(u)
};

struct CurveLineIntersection {
  std::vector<CurveLineHit> hits;  // ordered by u
  bool overlap = false;            // part of the curve lies on the line
};

/// Points where the curve meets the infinite line through `origin` along `direction` (any
/// finite non-zero vector). Throws std::invalid_argument otherwise.
[[nodiscard]] CurveLineIntersection intersect_line(const NurbsCurve2& curve, Vec2 origin,
                                                   Vec2 direction);

}  // namespace nf::math
