// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include "nf/math/refine.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

namespace nf::math {

namespace {

// A control point in homogeneous form (w·P, w).
template <typename Point>
struct Hom {
  Point p;
  double w;
};

template <typename Point>
Hom<Point> lerp(double a, const Hom<Point>& x, const Hom<Point>& y) {  // a·x + (1 − a)·y
  return {x.p * a + y.p * (1.0 - a), x.w * a + y.w * (1.0 - a)};
}

template <typename Point>
std::vector<Hom<Point>> to_hom(const NurbsCurve<Point>& c) {
  std::vector<Hom<Point>> h;
  h.reserve(c.points().size());
  for (std::size_t i = 0; i < c.points().size(); ++i) {
    h.push_back({c.points()[i] * c.weights()[i], c.weights()[i]});
  }
  return h;
}

template <typename Point>
std::expected<NurbsCurve<Point>, std::string> from_hom(std::vector<double> knots, int degree,
                                                       const std::vector<Hom<Point>>& h,
                                                       bool rational) {
  auto kv = KnotVector::make(std::move(knots), degree);
  if (!kv) return std::unexpected(kv.error());
  std::vector<Point> points;
  std::vector<double> weights;
  points.reserve(h.size());
  weights.reserve(h.size());
  for (const auto& q : h) {
    points.push_back(q.p / q.w);
    weights.push_back(q.w);
  }
  // Refining a non-rational curve keeps its weights 1 mathematically; rounding must not
  // turn it rational.
  if (!rational) weights.clear();
  return NurbsCurve<Point>::make(std::move(*kv), std::move(points), std::move(weights));
}

int multiplicity(std::span<const double> knots, double u) {
  return static_cast<int>(std::ranges::count(knots, u));
}

double binomial(int n, int k) {
  double b = 1.0;
  for (int i = 1; i <= k; ++i) b = b * (n - k + i) / i;
  return b;
}

}  // namespace

// The NURBS Book A5.1.
template <typename Point>
std::expected<NurbsCurve<Point>, std::string> insert_knot(const NurbsCurve<Point>& curve, double u,
                                                          int times) {
  if (times < 1) return std::unexpected("times must be at least 1");
  if (!(u > curve.domain_start() && u < curve.domain_end())) {
    return std::unexpected("knot must lie strictly inside the domain");
  }
  const int p = curve.degree();
  const std::span<const double> U = curve.knots().knots();
  const int s = multiplicity(U, u);
  if (times > p - s) return std::unexpected("multiplicity would exceed the degree");

  const int k = curve.knots().find_span(u);
  const int r = times;
  const auto P = to_hom(curve);
  const int np = static_cast<int>(P.size()) - 1;
  const auto at = [](int i) { return static_cast<std::size_t>(i); };

  std::vector<double> UQ(U.size() + at(r));
  for (int i = 0; i <= k; ++i) UQ[at(i)] = U[at(i)];
  for (int i = 1; i <= r; ++i) UQ[at(k + i)] = u;
  for (int i = k + 1; i < static_cast<int>(U.size()); ++i) UQ[at(i + r)] = U[at(i)];

  std::vector<Hom<Point>> Q(P.size() + at(r));
  for (int i = 0; i <= k - p; ++i) Q[at(i)] = P[at(i)];
  for (int i = k - s; i <= np; ++i) Q[at(i + r)] = P[at(i)];
  std::array<Hom<Point>, KnotVector::kMaxDegree + 1> R;
  for (int i = 0; i <= p - s; ++i) R[at(i)] = P[at(k - p + i)];
  int L = 0;
  for (int j = 1; j <= r; ++j) {
    L = k - p + j;
    for (int i = 0; i <= p - j - s; ++i) {
      const double alpha = (u - U[at(L + i)]) / (U[at(i + k + 1)] - U[at(L + i)]);
      R[at(i)] = lerp(alpha, R[at(i + 1)], R[at(i)]);
    }
    Q[at(L)] = R[0];
    Q[at(k + r - j - s)] = R[at(p - j - s)];
  }
  for (int i = L + 1; i < k - s; ++i) Q[at(i)] = R[at(i - L)];
  return from_hom(std::move(UQ), p, Q, curve.rational());
}

// The NURBS Book A5.9: split into Bézier segments, elevate each, and remove the knots the
// splitting added, all in one pass.
template <typename Point>
std::expected<NurbsCurve<Point>, std::string> elevate_degree(const NurbsCurve<Point>& curve,
                                                             int by) {
  if (by < 1) return std::unexpected("degree elevation must be at least 1");
  if (by > KnotVector::kMaxDegree - curve.degree()) {
    return std::unexpected("degree would exceed kMaxDegree");
  }
  const int p = curve.degree(), t = by, ph = p + t, ph2 = ph / 2;
  constexpr std::size_t kN = KnotVector::kMaxDegree + 1;
  const auto at = [](int i) { return static_cast<std::size_t>(i); };

  const std::span<const double> U = curve.knots().knots();
  const auto Pw = to_hom(curve);
  const int m = static_cast<int>(U.size()) - 1;

  // Coefficients for elevating one Bézier segment.
  std::array<std::array<double, kN>, kN> bezalfs{};
  bezalfs[0][0] = bezalfs[at(ph)][at(p)] = 1.0;
  for (int i = 1; i <= ph2; ++i) {
    const double inv = 1.0 / binomial(ph, i);
    for (int j = std::max(0, i - t); j <= std::min(p, i); ++j) {
      bezalfs[at(i)][at(j)] = inv * binomial(p, j) * binomial(t, i - j);
    }
  }
  for (int i = ph2 + 1; i <= ph - 1; ++i) {
    for (int j = std::max(0, i - t); j <= std::min(p, i); ++j) {
      bezalfs[at(i)][at(j)] = bezalfs[at(ph - i)][at(p - j)];
    }
  }

  // Each distinct knot gains t, so this bounds both outputs.
  std::vector<double> Uh(U.size() * at(t + 1));
  std::vector<Hom<Point>> Qw(Pw.size() * at(t + 1));
  std::array<Hom<Point>, kN> bpts{}, ebpts{}, next_bpts{};
  std::array<double, kN> alfs{};

  int kind = ph + 1, r = -1, a = p, b = p + 1, cind = 1;
  double ua = U[0];
  Qw[0] = Pw[0];
  for (int i = 0; i <= ph; ++i) Uh[at(i)] = ua;
  for (int i = 0; i <= p; ++i) bpts[at(i)] = Pw[at(i)];

  while (b < m) {
    const int first_b = b;
    while (b < m && U[at(b)] == U[at(b + 1)]) ++b;
    const int mul = b - first_b + 1;
    const double ub = U[at(b)];
    const int oldr = r;
    r = p - mul;
    const int lbz = oldr > 0 ? (oldr + 2) / 2 : 1;
    const int rbz = r > 0 ? ph - (r + 1) / 2 : ph;

    if (r > 0) {  // insert ub r times to isolate the Bézier segment
      const double numer = ub - ua;
      for (int k = p; k > mul; --k) alfs[at(k - mul - 1)] = numer / (U[at(a + k)] - ua);
      for (int j = 1; j <= r; ++j) {
        const int save = r - j, s = mul + j;
        for (int k = p; k >= s; --k)
          bpts[at(k)] = lerp(alfs[at(k - s)], bpts[at(k)], bpts[at(k - 1)]);
        next_bpts[at(save)] = bpts[at(p)];
      }
    }
    for (int i = lbz; i <= ph; ++i) {  // elevate the segment
      ebpts[at(i)] = {Point{}, 0.0};
      for (int j = std::max(0, i - t); j <= std::min(p, i); ++j) {
        const double c = bezalfs[at(i)][at(j)];
        ebpts[at(i)] = {ebpts[at(i)].p + bpts[at(j)].p * c, ebpts[at(i)].w + bpts[at(j)].w * c};
      }
    }
    if (oldr > 1) {  // remove ua (oldr − 1 times) where the previous segment ended
      int first = kind - 2, last = kind;
      const double den = ub - ua;
      const double bet = (ub - Uh[at(kind - 1)]) / den;
      for (int tr = 1; tr < oldr; ++tr) {
        int i = first, j = last, kj = j - kind + 1;
        while (j - i > tr) {
          if (i < cind) {
            const double alf = (ub - Uh[at(i)]) / (ua - Uh[at(i)]);
            Qw[at(i)] = lerp(alf, Qw[at(i)], Qw[at(i - 1)]);
          }
          if (j >= lbz) {
            const double g = j - tr <= kind - ph + oldr ? (ub - Uh[at(j - tr)]) / den : bet;
            ebpts[at(kj)] = lerp(g, ebpts[at(kj)], ebpts[at(kj + 1)]);
          }
          ++i;
          --j;
          --kj;
        }
        --first;
        ++last;
      }
    }
    if (a != p) {
      for (int i = 0; i < ph - oldr; ++i) Uh[at(kind++)] = ua;
    }
    for (int j = lbz; j <= rbz; ++j) Qw[at(cind++)] = ebpts[at(j)];
    if (b < m) {
      for (int j = 0; j < r; ++j) bpts[at(j)] = next_bpts[at(j)];
      for (int j = r; j <= p; ++j) bpts[at(j)] = Pw[at(b - p + j)];
      a = b;
      ++b;
      ua = ub;
    } else {
      for (int i = 0; i <= ph; ++i) Uh[at(kind + i)] = ub;
    }
  }
  Uh.resize(at(kind + ph + 1));
  Qw.resize(at(cind));
  return from_hom(std::move(Uh), ph, Qw, curve.rational());
}

template <typename Point>
std::expected<std::pair<NurbsCurve<Point>, NurbsCurve<Point>>, std::string> split(
    const NurbsCurve<Point>& curve, double u) {
  if (!(u > curve.domain_start() && u < curve.domain_end())) {
    return std::unexpected("split parameter must lie strictly inside the domain");
  }
  const int p = curve.degree();
  const int s = multiplicity(curve.knots().knots(), u);
  NurbsCurve<Point> full = curve;
  if (s < p) {
    auto refined = insert_knot(curve, u, p - s);
    if (!refined) return std::unexpected(refined.error());
    full = std::move(*refined);
  }
  // u now has multiplicity p, at knot indices a .. a + p − 1; control point a − 1 is C(u).
  const std::span<const double> U = full.knots().knots();
  const auto a = static_cast<std::size_t>(std::ranges::find(U, u) - U.begin());
  const auto H = to_hom(full);

  std::vector<double> left_knots(U.begin(), U.begin() + static_cast<std::ptrdiff_t>(a) + p);
  left_knots.push_back(u);
  std::vector<double> right_knots{u};
  right_knots.insert(right_knots.end(), U.begin() + static_cast<std::ptrdiff_t>(a), U.end());
  const std::vector<Hom<Point>> left(H.begin(), H.begin() + static_cast<std::ptrdiff_t>(a));
  const std::vector<Hom<Point>> right(H.begin() + static_cast<std::ptrdiff_t>(a) - 1, H.end());

  auto l = from_hom(std::move(left_knots), p, left, curve.rational());
  if (!l) return std::unexpected(l.error());
  auto rr = from_hom(std::move(right_knots), p, right, curve.rational());
  if (!rr) return std::unexpected(rr.error());
  return std::pair{std::move(*l), std::move(*rr)};
}

template std::expected<NurbsCurve2, std::string> insert_knot(const NurbsCurve2&, double, int);
template std::expected<NurbsCurve3, std::string> insert_knot(const NurbsCurve3&, double, int);
template std::expected<NurbsCurve2, std::string> elevate_degree(const NurbsCurve2&, int);
template std::expected<NurbsCurve3, std::string> elevate_degree(const NurbsCurve3&, int);
template std::expected<std::pair<NurbsCurve2, NurbsCurve2>, std::string> split(const NurbsCurve2&,
                                                                               double);
template std::expected<std::pair<NurbsCurve3, NurbsCurve3>, std::string> split(const NurbsCurve3&,
                                                                               double);

}  // namespace nf::math
