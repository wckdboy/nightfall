// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// Internal: rational Bézier pieces in homogeneous form, for subdivision algorithms. A piece
// lies in the convex hull of its control points (weights are positive), and so in their
// bounding box, which is what makes the queries' bounds rigorous.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

#include "nf/math/knot_vector.hpp"
#include "nf/math/nurbs_curve.hpp"
#include "nf/math/refine.hpp"

namespace nf::math::detail {

inline Vec2 min_each(Vec2 a, Vec2 b) { return {std::min(a.x, b.x), std::min(a.y, b.y)}; }
inline Vec3 min_each(Vec3 a, Vec3 b) {
  return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
}
inline Vec2 max_each(Vec2 a, Vec2 b) { return {std::max(a.x, b.x), std::max(a.y, b.y)}; }
inline Vec3 max_each(Vec3 a, Vec3 b) {
  return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
}

// Diagonal of the box [lo, hi] without overflow (capped at the largest double).
inline double safe_diagonal(Vec2 lo, Vec2 hi) {
  const double half = std::hypot(hi.x / 2 - lo.x / 2, hi.y / 2 - lo.y / 2);
  return half < std::numeric_limits<double>::max() / 2 ? 2 * half
                                                       : std::numeric_limits<double>::max();
}
inline double safe_diagonal(Vec3 lo, Vec3 hi) {
  const double half = std::hypot(hi.x / 2 - lo.x / 2, hi.y / 2 - lo.y / 2, hi.z / 2 - lo.z / 2);
  return half < std::numeric_limits<double>::max() / 2 ? 2 * half
                                                       : std::numeric_limits<double>::max();
}

template <typename Point>
struct Bezier {
  int degree = 0;
  std::array<Point, KnotVector::kMaxDegree + 1> wp{};  // w·P
  std::array<double, KnotVector::kMaxDegree + 1> w{};
  double u0 = 0.0, u1 = 1.0;  // the piece's range in the original curve's parameter

  [[nodiscard]] Point at(int i) const {
    return wp[static_cast<std::size_t>(i)] / w[static_cast<std::size_t>(i)];
  }
  [[nodiscard]] Point first() const { return at(0); }
  [[nodiscard]] Point last() const { return at(degree); }

  [[nodiscard]] std::pair<Point, Point> box() const {
    Point lo = at(0), hi = lo;
    for (int i = 1; i <= degree; ++i) {
      lo = min_each(lo, at(i));
      hi = max_each(hi, at(i));
    }
    return {lo, hi};
  }

  [[nodiscard]] double diagonal() const {
    const auto [lo, hi] = box();
    return safe_diagonal(lo, hi);
  }

  // de Casteljau in homogeneous coordinates at t ∈ (0, 1): exact for rational pieces.
  [[nodiscard]] std::pair<Bezier, Bezier> split(double t = 0.5) const {
    Bezier left = *this, right = *this;
    auto p = wp;
    auto q = w;
    const auto n = static_cast<std::size_t>(degree);
    left.wp[0] = p[0];
    left.w[0] = q[0];
    right.wp[n] = p[n];
    right.w[n] = q[n];
    for (std::size_t r = 1; r <= n; ++r) {
      for (std::size_t i = 0; i + r <= n; ++i) {
        p[i] = p[i] * (1 - t) + p[i + 1] * t;
        q[i] = q[i] * (1 - t) + q[i + 1] * t;
      }
      left.wp[r] = p[0];
      left.w[r] = q[0];
      right.wp[n - r] = p[n - r];
      right.w[n - r] = q[n - r];
    }
    const double um = u0 + (u1 - u0) * t;
    left.u1 = um;
    right.u0 = um;
    return {left, right};
  }
};

// Diagonal of the control points' bounding box: a curve's size, for relative tolerances.
template <typename Point>
double control_size(const NurbsCurve<Point>& c) {
  Point lo = c.points().front(), hi = lo;
  for (const Point& q : c.points()) {
    lo = min_each(lo, q);
    hi = max_each(hi, q);
  }
  return safe_diagonal(lo, hi);
}

// One piece per non-empty knot span.
template <typename Point>
std::vector<Bezier<Point>> bezier_pieces(const NurbsCurve<Point>& curve) {
  std::vector<Bezier<Point>> out;
  for (const NurbsCurve<Point>& span : decompose(curve)) {
    Bezier<Point> b;
    b.degree = span.degree();
    for (std::size_t i = 0; i < span.points().size(); ++i) {
      b.w[i] = span.weights()[i];
      b.wp[i] = span.points()[i] * b.w[i];
    }
    b.u0 = span.domain_start();
    b.u1 = span.domain_end();
    out.push_back(b);
  }
  return out;
}

}  // namespace nf::math::detail
