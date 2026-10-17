// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include "nf/math/nurbs_curve.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include "nf/math/basis.hpp"

namespace nf::math {

namespace {

bool finite(Vec2 p) { return std::isfinite(p.x) && std::isfinite(p.y); }
bool finite(Vec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }

constexpr int kMaxOrder = KnotVector::kMaxDegree;

// Binomial coefficients up to kMaxOrder.
constexpr std::array<std::array<double, kMaxOrder + 1>, kMaxOrder + 1> kBinomial = [] {
  std::array<std::array<double, kMaxOrder + 1>, kMaxOrder + 1> b{};
  for (std::size_t n = 0; n <= kMaxOrder; ++n) {
    b[n][0] = 1.0;
    for (std::size_t k = 1; k <= n; ++k)
      b[n][k] = b[n - 1][k - 1] + (k <= n - 1 ? b[n - 1][k] : 0.0);
  }
  return b;
}();

}  // namespace

template <typename Point>
std::expected<NurbsCurve<Point>, std::string> NurbsCurve<Point>::make(KnotVector knots,
                                                                      std::vector<Point> points,
                                                                      std::vector<double> weights) {
  if (std::cmp_not_equal(points.size(), knots.control_point_count())) {
    return std::unexpected("need " + std::to_string(knots.control_point_count()) +
                           " control points for this knot vector, got " +
                           std::to_string(points.size()));
  }
  if (!std::ranges::all_of(points, [](const Point& p) { return finite(p); })) {
    return std::unexpected("control points must be finite");
  }
  const bool rational = !weights.empty();
  if (!rational) weights.assign(points.size(), 1.0);
  if (weights.size() != points.size()) return std::unexpected("need one weight per control point");
  if (!std::ranges::all_of(weights, [](double w) { return std::isfinite(w) && w > 0.0; })) {
    return std::unexpected("weights must be positive and finite");
  }
  const bool varies = std::ranges::any_of(weights, [&](double w) { return w != weights.front(); });
  return NurbsCurve(std::make_shared<const Data>(
      Data{std::move(knots), std::move(points), std::move(weights), rational && varies}));
}

// The NURBS Book A4.1 (A3.1 for the non-rational case).
template <typename Point>
Point NurbsCurve<Point>::point(double u) const noexcept {
  const Data& d = *data_;
  const int p = d.knots.degree();
  u = std::clamp(u, domain_start(), domain_end());
  const int span = d.knots.find_span(u);
  BasisValues n;
  basis_functions(d.knots, span, u, n);
  Point sum{};
  double w = 0.0;
  for (int j = 0; j <= p; ++j) {
    const auto i = static_cast<std::size_t>(span - p + j);
    const double nw = n[static_cast<std::size_t>(j)] * d.weights[i];
    sum = sum + d.points[i] * nw;
    w += nw;
  }
  return sum / w;
}

// The NURBS Book A3.2 on the weighted points, then A4.2 for the rational quotient.
template <typename Point>
void NurbsCurve<Point>::derivatives(double u, int n, std::span<Point> out) const noexcept {
  const Data& d = *data_;
  const int p = d.knots.degree();
  const int orders = std::min({n, kMaxOrder, static_cast<int>(out.size()) - 1});
  if (orders < 0) return;
  u = std::clamp(u, domain_start(), domain_end());
  const int span = d.knots.find_span(u);
  BasisDerivatives b;
  basis_derivatives(d.knots, span, u, orders, b);

  std::array<Point, kMaxOrder + 1> aders{};   // derivatives of sum N_i w_i P_i
  std::array<double, kMaxOrder + 1> wders{};  // derivatives of sum N_i w_i
  for (int k = 0; k <= orders; ++k) {
    for (int j = 0; j <= p; ++j) {
      const auto i = static_cast<std::size_t>(span - p + j);
      const double nw = b.at(k, j) * d.weights[i];
      aders[static_cast<std::size_t>(k)] = aders[static_cast<std::size_t>(k)] + d.points[i] * nw;
      wders[static_cast<std::size_t>(k)] += nw;
    }
  }
  for (int k = 0; k <= orders; ++k) {
    Point v = aders[static_cast<std::size_t>(k)];
    for (int i = 1; i <= k; ++i) {
      v = v - out[static_cast<std::size_t>(k - i)] *
                  (kBinomial[static_cast<std::size_t>(k)][static_cast<std::size_t>(i)] *
                   wders[static_cast<std::size_t>(i)]);
    }
    out[static_cast<std::size_t>(k)] = v / wders[0];
  }
  for (std::size_t k = static_cast<std::size_t>(orders) + 1; k < out.size(); ++k) out[k] = Point{};
}

template class NurbsCurve<Vec2>;
template class NurbsCurve<Vec3>;

}  // namespace nf::math
