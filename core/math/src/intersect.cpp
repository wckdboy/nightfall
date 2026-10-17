// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include "nf/math/intersect.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
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

double magnitude(Vec2 p) { return std::max(std::abs(p.x), std::abs(p.y)); }

// Tolerances follow the pieces at hand, not the whole curve, so a long span elsewhere does
// not blur small features: a fraction of the local size plus the rounding of coordinates.
double local_tolerance(double scale, Vec2 where, double relative) {
  return relative * scale + 1e-14 * magnitude(where);
}

// A piece is flat when its control points lie within 1e-3 of its size from its chord; two
// flat pieces cross at most once unless they are nearly parallel.
bool flat(const B2& b) {
  const Vec2 a = b.first(), c = b.last();
  const Vec2 ac = c - a;
  const double len = length(ac);
  double off = 0.0;
  for (int i = 1; i < b.degree; ++i) {
    off = std::max(off, len > 0 ? std::abs(cross(ac, b.at(i) - a)) / len : length(b.at(i) - a));
  }
  return off <= 1e-3 * b.diagonal();
}

// Gauss–Newton (with a tiny damping term) on A(ua) − B(ub) = 0: Newton's method at a
// transversal crossing, still converging, more slowly, at a tangency.
// Confined to the given parameter ranges, so it can't wander off to another root.
CurveCurveHit solve(const NurbsCurve2& a, const NurbsCurve2& b, double ua, double ub,
                    std::pair<double, double> ra, std::pair<double, double> rb) {
  for (int it = 0; it < 80; ++it) {
    std::array<Vec2, 2> da{}, db{};
    a.derivatives(ua, 1, da);
    b.derivatives(ub, 1, db);
    const Vec2 f = da[0] - db[0];
    const double m11 = dot(da[1], da[1]), m22 = dot(db[1], db[1]), m12 = -dot(da[1], db[1]);
    const double g1 = dot(da[1], f), g2 = -dot(db[1], f);
    // Marquardt's scaling: damp each diagonal by its own size, since the two parameters can
    // differ in scale by many orders of magnitude.
    const double d11 = m11 * (1 + 1e-14), d22 = m22 * (1 + 1e-14);
    const double det = d11 * d22 - m12 * m12;
    if (!(std::abs(det) > 0)) break;
    const double na = std::clamp(ua - (d22 * g1 - m12 * g2) / det, ra.first, ra.second);
    const double nb = std::clamp(ub - (d11 * g2 - m12 * g1) / det, rb.first, rb.second);
    if (na == ua && nb == ub) break;
    ua = na;
    ub = nb;
  }
  return {ua, ub, a.point(ua)};
}

// Distance from q to the curve near parameter u (Newton on C'·(C − q)).
double local_distance(const NurbsCurve2& c, Vec2 q, double u) {
  for (int it = 0; it < 30; ++it) {
    std::array<Vec2, 3> d{};
    c.derivatives(u, 2, d);
    const Vec2 r = d[0] - q;
    const double df = dot(d[2], r) + dot(d[1], d[1]);
    if (!(df > 0)) break;
    const double next = std::clamp(u - dot(d[1], r) / df, c.domain_start(), c.domain_end());
    if (next == u) break;
    u = next;
  }
  return length(c.point(u) - q);
}

// Hits found from neighbouring pieces can be one point; tangencies, which are solved less
// precisely, can also come out as several points a hair apart (on either side of a closed
// curve's seam, too). Two hits are one when the midpoint of their chord still lies on both
// curves (`touching`); between distinct crossings the curves part.
template <typename Hit>
void merge_hits(std::vector<Hit>& hits, auto key, auto touching) {
  std::ranges::sort(hits, {}, key);
  std::vector<Hit> out;
  for (const Hit& h : hits) {
    if (std::ranges::none_of(out, [&](const Hit& o) { return touching(o, h); })) out.push_back(h);
  }
  hits = std::move(out);
}

}  // namespace

CurveCurveIntersection intersect(const NurbsCurve2& a, const NurbsCurve2& b) {
  const double small = 1e-3 * std::max(detail::control_size(a), detail::control_size(b));
  const auto pa = detail::bezier_pieces(a), pb = detail::bezier_pieces(b);

  CurveCurveIntersection result;
  std::vector<double> accepts;
  std::vector<std::pair<B2, B2>> stack;
  for (const B2& x : pa) {
    for (const B2& y : pb) stack.emplace_back(x, y);
  }
  long budget = 4'000'000;  // pairs examined; a safety net
  while (!stack.empty()) {
    if (--budget < 0) {  // only curves that share a stretch get this far
      result.overlap = true;
      return result;
    }
    const auto [x, y] = stack.back();
    stack.pop_back();
    const double scale = std::max(x.diagonal(), y.diagonal());
    if (!boxes_overlap(x, y, local_tolerance(scale, x.first(), 1e-9))) continue;
    const bool x_small = x.diagonal() <= small, y_small = y.diagonal() <= small;
    if ((x_small && y_small) || (flat(x) && flat(y))) {
      // The pieces' ranges widened by their own width: a root on a boundary is still reached.
      const auto widen = [](const B2& p, const NurbsCurve2& c) {
        const double w = p.u1 - p.u0;
        return std::pair{std::max(c.domain_start(), p.u0 - w), std::min(c.domain_end(), p.u1 + w)};
      };
      const CurveCurveHit h =
          solve(a, b, (x.u0 + x.u1) / 2, (y.u0 + y.u1) / 2, widen(x, a), widen(y, b));
      const double accept = local_tolerance(scale, h.point, 1e-10);
      if (length(h.point - b.point(h.ub)) <= accept) {
        result.hits.push_back(h);
        accepts.push_back(accept);
      }
      // A shared stretch, however short, puts two distinct ends of these pieces on the other
      // curve; a crossing or a tangency puts at most one there.
      std::vector<Vec2> shared;
      for (const Vec2 end : {x.first(), x.last()}) {
        if (local_distance(b, end, h.ub) <= accept) shared.push_back(end);
      }
      for (const Vec2 end : {y.first(), y.last()}) {
        if (local_distance(a, end, h.ua) <= accept) shared.push_back(end);
      }
      for (std::size_t i = 0; i < shared.size(); ++i) {
        for (std::size_t j = i + 1; j < shared.size(); ++j) {
          if (length(shared[i] - shared[j]) > local_tolerance(scale, shared[i], 1e-6)) {
            result.overlap = true;
          }
        }
      }
      if (result.overlap) return result;  // hits are not meaningful once the curves overlap
      continue;
    }
    // Split the larger piece, so the pair shrinks evenly.
    if (!x_small && (y_small || x.diagonal() >= y.diagonal())) {
      const auto [l, r] = x.split();
      stack.emplace_back(l, y);
      stack.emplace_back(r, y);
    } else {
      const auto [l, r] = y.split();
      stack.emplace_back(x, l);
      stack.emplace_back(x, r);
    }
  }
  const double accept = accepts.empty() ? 0.0 : std::ranges::max(accepts);
  merge_hits(result.hits, &CurveCurveHit::ua, [&](const CurveCurveHit& p, const CurveCurveHit& q) {
    const Vec2 mid = (p.point + q.point) / 2.0;
    // Seeded from both hits: the midpoint may lie across a closed curve's seam from one.
    return std::min(local_distance(a, mid, p.ua), local_distance(a, mid, q.ua)) <= accept &&
           std::min(local_distance(b, mid, p.ub), local_distance(b, mid, q.ub)) <= accept;
  });
  return result;
}

CurveLineIntersection intersect_line(const NurbsCurve2& curve, Vec2 origin, Vec2 direction) {
  // Normalised by the largest component first, so huge finite directions don't overflow.
  const double big = magnitude(direction);
  if (!(big > 0 && std::isfinite(big))) throw std::invalid_argument("line direction is zero");
  const Vec2 d = direction / big;
  const Vec2 normal = Vec2{-d.y, d.x} / length(d);
  const double small = 1e-3 * detail::control_size(curve);
  const auto signed_distance = [&](Vec2 q) { return dot(normal, q - origin); };

  CurveLineIntersection result;
  std::vector<double> accepts;
  std::vector<B2> stack = detail::bezier_pieces(curve);
  while (!stack.empty()) {
    const B2 b = stack.back();
    stack.pop_back();
    const double scale = b.diagonal();
    // dist(t) = Σ b_i(t) w_i d_i / w(t): its range is bounded by the control points'.
    double lo = INFINITY, hi = -INFINITY;
    for (int i = 0; i <= b.degree; ++i) {
      const double dist = signed_distance(b.at(i));
      lo = std::min(lo, dist);
      hi = std::max(hi, dist);
    }
    const double near = local_tolerance(scale, b.first(), 1e-10) + 1e-14 * magnitude(origin);
    if (lo > near || hi < -near) continue;         // entirely on one side
    if (lo >= -near && hi <= near && scale > 0) {  // the whole piece lies on the line
      result.overlap = true;
      result.hits.push_back({b.u0, curve.point(b.u0)});
      accepts.push_back(near);
      continue;
    }
    if (scale > small && !flat(b)) {
      const auto [l, r] = b.split();
      stack.push_back(l);
      stack.push_back(r);
      continue;
    }
    // Newton kept inside the piece; when the distance changes sign across it, a bracket is
    // maintained and bisection takes over any step that leaves it, so the root is found.
    double lo_u = b.u0, hi_u = b.u1;
    const double f_lo = signed_distance(curve.point(lo_u));
    const bool bracket = (f_lo > 0) != (signed_distance(curve.point(hi_u)) > 0);
    double u = (lo_u + hi_u) / 2;
    for (int it = 0; it < 100; ++it) {
      std::array<Vec2, 2> dc{};
      curve.derivatives(u, 1, dc);
      const double f = signed_distance(dc[0]), df = dot(normal, dc[1]);
      if (f == 0) break;
      if (bracket) ((f > 0) == (f_lo > 0) ? lo_u : hi_u) = u;
      double next = std::abs(df) > 0 ? u - f / df : (lo_u + hi_u) / 2;
      if (!(next > lo_u && next < hi_u))
        next = bracket ? (lo_u + hi_u) / 2 : std::clamp(next, b.u0, b.u1);
      if (next == u) break;
      u = next;
    }
    const Vec2 q = curve.point(u);
    if (std::abs(signed_distance(q)) <= near) {
      result.hits.push_back({u, q});
      accepts.push_back(near);
    }
  }
  const double accept = accepts.empty() ? 0.0 : std::ranges::max(accepts);
  merge_hits(result.hits, &CurveLineHit::u, [&](const CurveLineHit& p, const CurveLineHit& q) {
    const Vec2 mid = (p.point + q.point) / 2.0;
    return std::abs(signed_distance(mid)) <= accept &&
           std::min(local_distance(curve, mid, p.u), local_distance(curve, mid, q.u)) <= accept;
  });
  return result;
}

}  // namespace nf::math
