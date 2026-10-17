// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include "nf/math/constructors.hpp"

#include <cmath>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace nf::math {

namespace {

template <typename Point>
NurbsCurve<Point> build(std::vector<double> knots, int degree, std::vector<Point> points,
                        std::vector<double> weights = {}) {
  auto kv = KnotVector::make(std::move(knots), degree);
  if (!kv) throw std::invalid_argument(kv.error());
  auto c = NurbsCurve<Point>::make(*kv, std::move(points), std::move(weights));
  if (!c) throw std::invalid_argument(c.error());
  return *c;
}

template <typename Point>
NurbsCurve<Point> polyline(std::span<const Point> pts) {
  if (pts.size() < 2) throw std::invalid_argument("polyline needs at least 2 points");
  std::vector<double> cumulative{0.0};
  for (std::size_t i = 1; i < pts.size(); ++i) {
    cumulative.push_back(cumulative.back() + length(pts[i] - pts[i - 1]));
  }
  const double total = cumulative.back();
  if (!(total > 0.0)) throw std::invalid_argument("polyline has zero length");
  std::vector<double> knots{0.0};
  for (const double c : cumulative) knots.push_back(c / total);
  knots.push_back(1.0);
  knots[knots.size() - 2] = 1.0;  // exact end, whatever the rounding
  return build(std::move(knots), 1, std::vector<Point>(pts.begin(), pts.end()));
}

template <typename Point>
NurbsCurve<Point> bezier(std::span<const Point> pts) {
  const int degree = static_cast<int>(pts.size()) - 1;
  if (degree < 1 || degree > KnotVector::kMaxDegree) {
    throw std::invalid_argument("Bézier degree must be 1..9");
  }
  std::vector<double> knots(pts.size(), 0.0);
  knots.insert(knots.end(), pts.size(), 1.0);
  return build(std::move(knots), degree, std::vector<Point>(pts.begin(), pts.end()));
}

// A7.1 on the unit circle in (u, v); `map` places (u, v) affinely. Affine maps preserve a
// rational quadratic exactly, which also makes ellipses exact.
template <typename Point, typename Map>
NurbsCurve<Point> unit_arc(double start, double end, Map map) {
  const double sweep = end - start;
  if (!(sweep > 0.0) || sweep > 2.0 * std::numbers::pi * (1.0 + 1e-12)) {
    throw std::invalid_argument("arc sweep must be in (0, 2π]");
  }
  const int spans = static_cast<int>(std::ceil(sweep / (std::numbers::pi / 2.0) - 1e-12));
  const double step = sweep / spans;
  const double w = std::cos(step / 2.0);
  std::vector<Point> points{map(std::cos(start), std::sin(start))};
  std::vector<double> weights{1.0};
  std::vector<double> knots{0.0, 0.0, 0.0};
  for (int i = 1; i <= spans; ++i) {
    const double mid = start + (i - 0.5) * step, a = start + i * step;
    points.push_back(map(std::cos(mid) / w, std::sin(mid) / w));  // tangent intersection
    weights.push_back(w);
    points.push_back(map(std::cos(a), std::sin(a)));
    weights.push_back(1.0);
    if (i < spans) knots.insert(knots.end(), 2, static_cast<double>(i) / spans);
  }
  knots.insert(knots.end(), 3, 1.0);
  return build(std::move(knots), 2, std::move(points), std::move(weights));
}

}  // namespace

NurbsCurve2 make_line(Vec2 a, Vec2 b) { return build<Vec2>({0, 0, 1, 1}, 1, {a, b}); }
NurbsCurve3 make_line(Vec3 a, Vec3 b) { return build<Vec3>({0, 0, 1, 1}, 1, {a, b}); }

NurbsCurve2 make_polyline(std::span<const Vec2> p) { return polyline(p); }
NurbsCurve3 make_polyline(std::span<const Vec3> p) { return polyline(p); }

NurbsCurve2 make_bezier(std::span<const Vec2> p) { return bezier(p); }
NurbsCurve3 make_bezier(std::span<const Vec3> p) { return bezier(p); }

NurbsCurve3 make_arc(Vec3 center, Vec3 x_axis, Vec3 y_axis, double rx, double ry, double start,
                     double end) {
  return unit_arc<Vec3>(start, end, [&](double u, double v) {
    return center + x_axis * (rx * u) + y_axis * (ry * v);
  });
}

NurbsCurve2 make_arc(Vec2 center, double rx, double ry, double rotation, double start, double end) {
  const Vec2 x{std::cos(rotation), std::sin(rotation)}, y{-x.y, x.x};
  return unit_arc<Vec2>(start, end,
                        [&](double u, double v) { return center + x * (rx * u) + y * (ry * v); });
}

NurbsCurve3 make_circle(Vec3 center, Vec3 normal, double r) {
  const auto n = normalized(normal);
  if (!n) throw std::invalid_argument("circle normal has no direction");
  // In-plane x axis: +X projected into the plane, or +Y if the normal is (anti)parallel to X.
  const Vec3 seed = std::abs(n->x) > 0.9 ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
  const Vec3 x = *normalized(seed - *n * dot(seed, *n));
  const Vec3 y = cross(*n, x);
  return make_arc(center, x, y, r, r, 0.0, 2.0 * std::numbers::pi);
}

NurbsCurve2 make_circle(Vec2 center, double r) {
  return make_arc(center, r, r, 0.0, 0.0, 2.0 * std::numbers::pi);
}

}  // namespace nf::math
