// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include "nf/math/intersect.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <utility>

#include "bezier.hpp"

namespace nf::math {

namespace {

using B2 = detail::Bezier<Vec2>;

bool boxes_overlap(const B2& a, const B2& b, double slack) {
  const auto [alo, ahi] = a.box();
  const auto [blo, bhi] = b.box();
  return ahi.x + slack >= blo.x && bhi.x + slack >= alo.x && ahi.y + slack >= blo.y &&
         bhi.y + slack >= alo.y;
}

double diagonal(const B2& b) { return b.diagonal(); }

// Gauss–Newton (with a tiny damping term) on A(ua) − B(ub) = 0. It is Newton's method when
// the crossing is transversal and still converges, more slowly, at a tangency, where the
// Jacobian is singular.
CurveCurveHit solve(const NurbsCurve2& a, const NurbsCurve2& b, double ua, double ub) {
  for (int it = 0; it < 80; ++it) {
    std::array<Vec2, 2> da{}, db{};
    a.derivatives(ua, 1, da);
    b.derivatives(ub, 1, db);
    const Vec2 f = da[0] - db[0];
    // Normal equations of J = [A′, −B′].
    const double m11 = dot(da[1], da[1]), m22 = dot(db[1], db[1]), m12 = -dot(da[1], db[1]);
    const double g1 = dot(da[1], f), g2 = -dot(db[1], f);
    const double damp = 1e-14 * (m11 + m22);
    const double det = (m11 + damp) * (m22 + damp) - m12 * m12;
    if (!(std::abs(det) > 0)) break;
    const double sa = -((m22 + damp) * g1 - m12 * g2) / det;
    const double sb = -((m11 + damp) * g2 - m12 * g1) / det;
    const double na = std::clamp(ua + sa, a.domain_start(), a.domain_end());
    const double nb = std::clamp(ub + sb, b.domain_start(), b.domain_end());
    if (na == ua && nb == ub) break;
    ua = na;
    ub = nb;
  }
  return {ua, ub, a.point(ua)};
}

template <typename Hit>
void dedupe(std::vector<Hit>& hits, double merge, auto key) {
  std::ranges::sort(hits, {}, key);
  std::vector<Hit> out;
  for (const Hit& h : hits) {
    const bool same =
        std::ranges::any_of(out, [&](const Hit& o) { return length(o.point - h.point) <= merge; });
    if (!same) out.push_back(h);
  }
  hits = std::move(out);
}

}  // namespace

CurveCurveIntersection intersect(const NurbsCurve2& a, const NurbsCurve2& b) {
  const double size = std::max(detail::control_size(a), detail::control_size(b));
  const double small = 1e-3 * size, accept = 1e-8 * size, merge = 1e-7 * size;
  const auto pa = detail::bezier_pieces(a), pb = detail::bezier_pieces(b);

  CurveCurveIntersection result;
  std::vector<std::pair<B2, B2>> stack;
  for (const B2& x : pa) {
    for (const B2& y : pb) stack.emplace_back(x, y);
  }
  long budget = 4'000'000;  // pairs examined; only coincident curves come near it
  while (!stack.empty()) {
    if (--budget < 0) {
      result.overlap = true;
      break;
    }
    const auto [x, y] = stack.back();
    stack.pop_back();
    if (!boxes_overlap(x, y, 1e-9 * size)) continue;
    const bool x_small = diagonal(x) <= small, y_small = diagonal(y) <= small;
    if (x_small && y_small) {
      const CurveCurveHit h = solve(a, b, (x.u0 + x.u1) / 2, (y.u0 + y.u1) / 2);
      if (length(h.point - b.point(h.ub)) <= accept) result.hits.push_back(h);
      continue;
    }
    // Split the larger piece (both if similar), so the pair shrinks evenly.
    if (!x_small && (y_small || diagonal(x) >= diagonal(y))) {
      const auto [l, r] = x.split();
      stack.emplace_back(l, y);
      stack.emplace_back(r, y);
    } else {
      const auto [l, r] = y.split();
      stack.emplace_back(x, l);
      stack.emplace_back(x, r);
    }
  }
  dedupe(result.hits, merge, &CurveCurveHit::ua);
  // Two planar rational Bézier pieces of degrees m and n meet at most m·n times unless they
  // share a component (Bézout), so more distinct points than that means an overlap.
  const auto bound = pa.size() * static_cast<std::size_t>(a.degree()) * pb.size() *
                     static_cast<std::size_t>(b.degree());
  if (result.hits.size() > bound) result.overlap = true;
  return result;
}

CurveLineIntersection intersect_line(const NurbsCurve2& curve, Vec2 origin, Vec2 direction) {
  const double len = length(direction);
  if (!(len > 0 && std::isfinite(len))) throw std::invalid_argument("line direction is zero");
  const Vec2 normal{-direction.y / len, direction.x / len};
  const double size = detail::control_size(curve);
  const double small = 1e-3 * size, accept = 1e-8 * size, merge = 1e-7 * size;
  const auto signed_distance = [&](Vec2 q) { return dot(normal, q - origin); };

  CurveLineIntersection result;
  std::vector<B2> stack = detail::bezier_pieces(curve);
  while (!stack.empty()) {
    const B2 b = stack.back();
    stack.pop_back();
    // dist(t) = Σ b_i(t) w_i d_i / w(t): the sign is that of the numerator's coefficients.
    double lo = INFINITY, hi = -INFINITY;
    for (int i = 0; i <= b.degree; ++i) {
      const double d = signed_distance(b.at(i));
      lo = std::min(lo, d);
      hi = std::max(hi, d);
    }
    if (lo > accept || hi < -accept) continue;  // entirely on one side
    if (lo >= -accept && hi <= accept) {        // the piece lies on the line
      result.overlap = true;
      result.hits.push_back({b.u0, curve.point(b.u0)});
      continue;
    }
    if (diagonal(b) > small) {
      const auto [l, r] = b.split();
      stack.push_back(l);
      stack.push_back(r);
      continue;
    }
    double u = (b.u0 + b.u1) / 2;
    for (int it = 0; it < 80; ++it) {
      std::array<Vec2, 2> d{};
      curve.derivatives(u, 1, d);
      const double f = signed_distance(d[0]), df = dot(normal, d[1]);
      if (!(std::abs(df) > 0)) break;
      const double next = std::clamp(u - f / df, curve.domain_start(), curve.domain_end());
      if (next == u) break;
      u = next;
    }
    const Vec2 q = curve.point(u);
    if (std::abs(signed_distance(q)) <= accept) result.hits.push_back({u, q});
  }
  dedupe(result.hits, merge, &CurveLineHit::u);
  return result;
}

}  // namespace nf::math
