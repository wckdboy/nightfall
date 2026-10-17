// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include "nf/math/fit.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

#include "nf/math/basis.hpp"

namespace nf::math {

namespace {

std::size_t at(int i) { return static_cast<std::size_t>(i); }

// A square banded matrix with `w` diagonals on each side of the main one.
class Banded {
 public:
  Banded(int n, int w) : n_(n), w_(w), a_(at(n) * at(2 * w + 1), 0.0) {}
  double& operator()(int i, int j) { return a_[at(i) * at(2 * w_ + 1) + at(j - i + w_)]; }

  // Gaussian elimination without pivoting, inside the band: stable for the totally positive
  // collocation matrices of interpolation and for symmetric positive definite matrices.
  template <typename Point>
  bool solve(std::vector<Point>& rhs) {
    for (int k = 0; k < n_; ++k) {
      const double pivot = (*this)(k, k);
      if (!(std::abs(pivot) > 0)) return false;
      for (int i = k + 1; i <= std::min(n_ - 1, k + w_); ++i) {
        const double f = (*this)(i, k) / pivot;
        if (f == 0) continue;
        for (int j = k; j <= std::min(n_ - 1, k + w_); ++j) (*this)(i, j) -= f * (*this)(k, j);
        rhs[at(i)] = rhs[at(i)] - rhs[at(k)] * f;
      }
    }
    for (int k = n_ - 1; k >= 0; --k) {
      Point s = rhs[at(k)];
      for (int j = k + 1; j <= std::min(n_ - 1, k + w_); ++j) s = s - rhs[at(j)] * (*this)(k, j);
      rhs[at(k)] = s / (*this)(k, k);
    }
    return true;
  }

 private:
  int n_, w_;
  std::vector<double> a_;
};

// Chord-length parameters on [0, 1] (Eq. 9.5); nullopt if two consecutive points coincide.
template <typename Point>
std::optional<std::vector<double>> chord_parameters(std::span<const Point> q) {
  std::vector<double> u(q.size(), 0.0);
  double total = 0.0;
  for (std::size_t k = 1; k < q.size(); ++k) {
    const double d = length(q[k] - q[k - 1]);
    if (!(d > 0)) return std::nullopt;
    total += d;
    u[k] = total;
  }
  for (double& x : u) x /= total;
  u.back() = 1.0;
  return u;
}

template <typename Point>
std::optional<std::string> check(std::span<const Point> points, int degree) {
  if (degree < 1 || degree > KnotVector::kMaxDegree) return "degree must be 1..kMaxDegree";
  if (points.size() <= static_cast<std::size_t>(degree)) return "need more points than the degree";
  return std::nullopt;
}

// Least squares with n + 1 control points (A9.7); the ends are interpolated.
template <typename Point>
std::optional<NurbsCurve<Point>> least_squares(std::span<const Point> q,
                                               const std::vector<double>& ub, int degree, int n) {
  const int p = degree, m = static_cast<int>(q.size()) - 1;
  std::vector<double> knots(at(p + 1), 0.0);
  const double d = static_cast<double>(m + 1) / (n - p + 1);
  for (int j = 1; j <= n - p; ++j) {  // Eq. 9.69
    const int i = static_cast<int>(j * d);
    const double alpha = j * d - i;
    knots.push_back((1 - alpha) * ub[at(i - 1)] + alpha * ub[at(i)]);
  }
  knots.insert(knots.end(), at(p + 1), 1.0);
  auto kv = KnotVector::make(std::move(knots), p);
  if (!kv) return std::nullopt;

  // Normal equations for the interior control points P_1 … P_{n−1}.
  const int unknowns = n - 1;
  std::vector<Point> rhs(at(std::max(unknowns, 0)), Point{});
  Banded ntn(std::max(unknowns, 1), p);
  for (int k = 1; k < m; ++k) {
    const int span = kv->find_span(ub[at(k)]);
    BasisValues nv;
    basis_functions(*kv, span, ub[at(k)], nv);
    double n0 = 0, nn = 0;
    for (int j = 0; j <= p; ++j) {
      const int i = span - p + j;
      if (i == 0) n0 = nv[at(j)];
      if (i == n) nn = nv[at(j)];
    }
    const Point r = q[at(k)] - q.front() * n0 - q.back() * nn;
    for (int a = 0; a <= p; ++a) {
      const int ia = span - p + a;
      if (ia < 1 || ia > n - 1) continue;
      rhs[at(ia - 1)] = rhs[at(ia - 1)] + r * nv[at(a)];
      for (int b = 0; b <= p; ++b) {
        const int ib = span - p + b;
        if (ib < 1 || ib > n - 1) continue;
        ntn(ia - 1, ib - 1) += nv[at(a)] * nv[at(b)];
      }
    }
  }
  if (unknowns > 0 && !ntn.solve(rhs)) return std::nullopt;
  std::vector<Point> points{q.front()};
  points.insert(points.end(), rhs.begin(), rhs.end());
  points.push_back(q.back());
  auto c = NurbsCurve<Point>::make(std::move(*kv), std::move(points));
  if (!c) return std::nullopt;
  return *c;
}

// Parameter correction (Hoschek): move each parameter to the foot of its data point on the
// curve by Newton. Returns false, leaving u unchanged, if the order would break.
template <typename Point>
bool correct_parameters(const NurbsCurve<Point>& c, std::span<const Point> q,
                        std::vector<double>& u) {
  std::vector<double> next = u;
  for (std::size_t k = 1; k + 1 < q.size(); ++k) {
    for (int it = 0; it < 3; ++it) {
      std::array<Point, 3> d{};
      c.derivatives(next[k], 2, d);
      const Point r = d[0] - q[k];
      const double df = dot(d[2], r) + dot(d[1], d[1]);
      if (!(df > 0)) break;
      next[k] = std::clamp(next[k] - dot(d[1], r) / df, 0.0, 1.0);
    }
  }
  for (std::size_t k = 1; k < next.size(); ++k) {
    if (!(next[k] > next[k - 1])) return false;
  }
  u = std::move(next);
  return true;
}

template <typename Point>
double deviation(const NurbsCurve<Point>& c, std::span<const Point> q,
                 const std::vector<double>& ub) {
  double worst = 0.0;
  for (std::size_t k = 0; k < q.size(); ++k) worst = std::max(worst, length(c.point(ub[k]) - q[k]));
  return worst;
}

}  // namespace

// The NURBS Book A9.1.
template <typename Point>
std::expected<NurbsCurve<Point>, std::string> interpolate(std::span<const Point> q, int degree) {
  if (auto e = check(q, degree)) return std::unexpected(*e);
  const auto ub = chord_parameters(q);
  if (!ub) return std::unexpected("consecutive points coincide");
  const int p = degree, n = static_cast<int>(q.size()) - 1;
  std::vector<double> knots(at(p + 1), 0.0);
  for (int j = 1; j <= n - p; ++j) {  // Eq. 9.8
    double s = 0.0;
    for (int i = j; i < j + p; ++i) s += (*ub)[at(i)];
    knots.push_back(s / p);
  }
  knots.insert(knots.end(), at(p + 1), 1.0);
  auto kv = KnotVector::make(std::move(knots), p);
  if (!kv) return std::unexpected(kv.error());
  Banded a(n + 1, p);
  for (int k = 0; k <= n; ++k) {
    const int span = kv->find_span((*ub)[at(k)]);
    BasisValues nv;
    basis_functions(*kv, span, (*ub)[at(k)], nv);
    for (int j = 0; j <= p; ++j) {
      const int col = span - p + j;
      // Averaged knots keep the collocation matrix inside the band (|col − k| ≤ p).
      if (std::abs(col - k) > p) return std::unexpected("interpolation matrix outside its band");
      a(k, col) = nv[at(j)];
    }
  }
  std::vector<Point> rhs(q.begin(), q.end());
  if (!a.solve(rhs)) return std::unexpected("singular interpolation system");
  rhs.front() = q.front();  // exactly what the end equations say, without rounding
  rhs.back() = q.back();
  return NurbsCurve<Point>::make(std::move(*kv), std::move(rhs));
}

template <typename Point>
std::expected<Fit<Point>, std::string> approximate(std::span<const Point> q, double tolerance,
                                                   int degree) {
  if (auto e = check(q, degree)) return std::unexpected(*e);
  if (!(tolerance > 0 && std::isfinite(tolerance))) {
    return std::unexpected("tolerance must be positive and finite");
  }
  const auto ub = chord_parameters(q);
  if (!ub) return std::unexpected("consecutive points coincide");
  const int m = static_cast<int>(q.size()) - 1;

  std::optional<Fit<Point>> best;
  // A fit with n + 1 control points, refined by parameter correction while it keeps helping.
  const auto attempt = [&](int n) {
    std::vector<double> u = *ub;
    auto c = least_squares(q, u, degree, n);
    if (!c) return false;
    double dev = deviation(*c, q, u);
    for (int round = 0; round < 200 && dev > tolerance; ++round) {
      std::vector<double> v = u;
      if (!correct_parameters(*c, q, v)) break;
      auto refit = least_squares(q, v, degree, n);
      if (!refit) break;
      const double d = deviation(*refit, q, v);
      if (!(d < 0.99 * dev)) break;
      c = std::move(refit);
      u = std::move(v);
      dev = d;
    }
    if (dev > tolerance) return false;
    best = Fit<Point>{std::move(*c), std::move(u), dev};
    return true;
  };
  // Doubling to the first count that fits, then bisection back towards the smallest.
  int lo = degree, hi = degree;  // n is the last control point's index
  for (int n = degree;; n = std::min(m, 2 * n)) {
    if (attempt(n)) {
      hi = n;
      break;
    }
    lo = n;
    if (n == m) break;
  }
  if (!best) {  // even n = m (interpolation) misses: below what floating point can reach
    return std::unexpected("tolerance is below the fit's floating-point accuracy");
  }
  while (hi - lo > 1) {
    const int mid = (lo + hi) / 2;
    if (attempt(mid)) {
      hi = mid;
    } else {
      lo = mid;
    }
  }
  if (!attempt(hi)) return std::unexpected("least-squares fit failed");  // re-run the winner
  return std::move(*best);
}

template std::expected<NurbsCurve2, std::string> interpolate(std::span<const Vec2>, int);
template std::expected<NurbsCurve3, std::string> interpolate(std::span<const Vec3>, int);
template std::expected<Fit<Vec2>, std::string> approximate(std::span<const Vec2>, double, int);
template std::expected<Fit<Vec3>, std::string> approximate(std::span<const Vec3>, double, int);

}  // namespace nf::math
