// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include "nf/math/query.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

#include "bezier.hpp"

namespace nf::math {

namespace {

using detail::Bezier;

// Distance from p to the segment [a, b].
template <typename Point>
double segment_distance(Point p, Point a, Point b) {
  const Point ab = b - a;
  const double len2 = dot(ab, ab);
  const double t = len2 > 0 ? std::clamp(dot(p - a, ab) / len2, 0.0, 1.0) : 0.0;
  return length(p - (a + ab * t));
}

// Distance from p to the axis-aligned box [lo, hi].
double box_distance(Vec2 p, Vec2 lo, Vec2 hi) {
  return std::hypot(std::max({lo.x - p.x, 0.0, p.x - hi.x}),
                    std::max({lo.y - p.y, 0.0, p.y - hi.y}));
}
double box_distance(Vec3 p, Vec3 lo, Vec3 hi) {
  return std::hypot(std::max({lo.x - p.x, 0.0, p.x - hi.x}),
                    std::max({lo.y - p.y, 0.0, p.y - hi.y}),
                    std::max({lo.z - p.z, 0.0, p.z - hi.z}));
}

// Diagonal of the control points' box: the curve's size for relative tolerances.
template <typename Point>
double size_of(const NurbsCurve<Point>& c) {
  Point lo = c.points().front(), hi = lo;
  for (const Point& q : c.points()) {
    lo = detail::min_each(lo, q);
    hi = detail::max_each(hi, q);
  }
  return detail::safe_diagonal(lo, hi);
}

}  // namespace

template <typename Point>
Closest<Point> closest_point(const NurbsCurve<Point>& curve, Point p) {
  Closest<Point> best{curve.domain_start(), curve.point(curve.domain_start()), 0.0};
  best.distance = length(p - best.point);
  const auto consider = [&](double u) {
    const Point q = curve.point(u);
    const double d = length(p - q);
    if (d < best.distance) best = {u, q, d};
  };
  consider(curve.domain_end());

  // Newton on f(u) = C'(u)·(C(u) − p) inside [lo, hi], from u.
  const auto polish = [&](double u, double lo, double hi) {
    for (int it = 0; it < 12; ++it) {
      std::array<Point, 3> d{};
      curve.derivatives(u, 2, d);
      const Point r = d[0] - p;
      const double f = dot(d[1], r), df = dot(d[2], r) + dot(d[1], d[1]);
      if (!(std::abs(df) > 0)) break;
      const double next = std::clamp(u - f / df, lo, hi);
      if (next == u) break;
      u = next;
    }
    consider(u);
  };

  // Subdivide only to isolate candidates: the box bound is first-order in a piece's size
  // while distance grows quadratically near a minimum, so pieces are refined to 1e-3 of the
  // curve's size and finished by Newton, with the piece's ends as fallbacks.
  const double small = 1e-3 * size_of(curve);
  std::vector<Bezier<Point>> stack = detail::bezier_pieces(curve);
  long budget = 200'000;  // pieces examined; a safety net, far above what curves need
  while (!stack.empty() && budget-- > 0) {
    const Bezier<Point> b = stack.back();
    stack.pop_back();
    const auto [lo, hi] = b.box();
    if (box_distance(p, lo, hi) >= best.distance) continue;  // cannot improve
    const double um = (b.u0 + b.u1) / 2;
    consider(um);
    if (detail::safe_diagonal(lo, hi) <= small) {
      consider(b.u0);
      consider(b.u1);
      polish(um, b.u0, b.u1);
      continue;
    }
    const auto [left, right] = b.split();
    stack.push_back(left);
    stack.push_back(right);
  }
  return best;
}

template <typename Point>
std::vector<CurvePoint<Point>> polyline(const NurbsCurve<Point>& curve, double tolerance) {
  if (!(tolerance > 0 && std::isfinite(tolerance))) {
    throw std::invalid_argument("polyline tolerance must be positive and finite");
  }
  const double size = size_of(curve);
  tolerance = std::max(tolerance, 1e-12 * size);
  const double scale = std::ldexp(1.0, -std::max(0, std::ilogb(std::max(size, 1.0))));
  std::vector<CurvePoint<Point>> out{{curve.domain_start(), curve.point(curve.domain_start())}};
  // Depth-first, left to right, so points come out in order.
  for (const Bezier<Point>& piece : detail::bezier_pieces(curve)) {
    std::vector<std::pair<Bezier<Point>, int>> stack{{piece, 0}};
    while (!stack.empty()) {
      const auto [b, depth] = stack.back();
      stack.pop_back();
      // Measured on points scaled by a power of two (exact), so huge coordinates can't
      // overflow the chord arithmetic.
      double off = 0.0;
      for (int i = 1; i < b.degree; ++i) {
        off = std::max(off, segment_distance(b.at(i) * scale, b.first() * scale, b.last() * scale));
      }
      if (off > tolerance * scale && depth < 40) {
        const auto [left, right] = b.split();
        stack.push_back({right, depth + 1});
        stack.push_back({left, depth + 1});
      } else {
        out.push_back({b.u1, b.last()});
      }
    }
  }
  return out;
}

template Closest<Vec2> closest_point(const NurbsCurve2&, Vec2);
template Closest<Vec3> closest_point(const NurbsCurve3&, Vec3);
template std::vector<CurvePoint<Vec2>> polyline(const NurbsCurve2&, double);
template std::vector<CurvePoint<Vec3>> polyline(const NurbsCurve3&, double);

}  // namespace nf::math
