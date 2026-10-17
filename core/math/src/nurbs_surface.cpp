// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include "nf/math/nurbs_surface.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <utility>

#include "nf/math/basis.hpp"
#include "nf/math/constructors.hpp"

namespace nf::math {

namespace {

std::size_t at(int i) { return static_cast<std::size_t>(i); }
bool finite(Vec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }

double binomial(int n, int k) {
  double b = 1.0;
  for (int i = 1; i <= k; ++i) b = b * (n - k + i) / i;
  return b;
}

}  // namespace

std::expected<NurbsSurface, std::string> NurbsSurface::make(KnotVector ku, KnotVector kv,
                                                            std::vector<Vec3> points,
                                                            std::vector<double> weights) {
  const auto n = static_cast<std::size_t>(ku.control_point_count()) *
                 static_cast<std::size_t>(kv.control_point_count());
  if (points.size() != n) {
    return std::unexpected("need " + std::to_string(n) + " control points, got " +
                           std::to_string(points.size()));
  }
  if (!std::ranges::all_of(points, finite)) return std::unexpected("control points must be finite");
  const bool rational = !weights.empty();
  if (!rational) weights.assign(n, 1.0);
  if (weights.size() != n) return std::unexpected("need one weight per control point");
  if (!std::ranges::all_of(weights, [](double w) { return std::isfinite(w) && w > 0.0; })) {
    return std::unexpected("weights must be positive and finite");
  }
  const bool varies = std::ranges::any_of(weights, [&](double w) { return w != weights.front(); });
  if (!varies) {
    weights.assign(n, 1.0);
  } else {  // a power of two, as for curves: exact, and evaluation can't overflow
    int e = 0;
    (void)std::frexp(std::ranges::max(weights), &e);
    for (double& w : weights) w = std::ldexp(w, 1 - e);
    if (!std::ranges::all_of(weights, [](double w) { return std::isnormal(w); })) {
      return std::unexpected("weights span too wide a range");
    }
  }
  return NurbsSurface(std::make_shared<const Data>(Data{
      std::move(ku), std::move(kv), std::move(points), std::move(weights), rational && varies}));
}

Vec3 NurbsSurface::point(double u, double v) const noexcept {
  return derivatives(u, v, 0).at(0, 0);
}

// The NURBS Book A3.6 on the weighted points, then A4.4 for the rational quotient.
SurfaceDerivatives NurbsSurface::derivatives(double u, double v, int order) const noexcept {
  const Data& s = *data_;
  constexpr int kM = SurfaceDerivatives::kMaxOrder;
  order = std::clamp(order, 0, kM);
  u = std::clamp(u, s.ku.domain_start(), s.ku.domain_end());
  v = std::clamp(v, s.kv.domain_start(), s.kv.domain_end());
  const int p = s.ku.degree(), q = s.kv.degree(), m = count_v();
  const int su = s.ku.find_span(u), sv = s.kv.find_span(v);
  BasisDerivatives nu, nv;
  basis_derivatives(s.ku, su, u, std::min(order, p), nu);
  basis_derivatives(s.kv, sv, v, std::min(order, q), nv);

  std::array<std::array<Vec3, kM + 1>, kM + 1> a{};    // derivatives of Σ N N w P
  std::array<std::array<double, kM + 1>, kM + 1> w{};  // derivatives of Σ N N w
  for (int k = 0; k <= std::min(order, p); ++k) {
    for (int l = 0; l <= std::min(order - k, q); ++l) {
      for (int i = 0; i <= p; ++i) {
        for (int j = 0; j <= q; ++j) {
          const std::size_t idx = at((su - p + i) * m + (sv - q + j));
          const double b = nu.at(k, i) * nv.at(l, j) * s.weights[idx];
          a[at(k)][at(l)] = a[at(k)][at(l)] + s.points[idx] * b;
          w[at(k)][at(l)] += b;
        }
      }
    }
  }
  SurfaceDerivatives out;
  const auto put = [&](int k, int l, Vec3 x) { out.d[at(k * (kM + 1) + l)] = x; };
  for (int k = 0; k <= order; ++k) {
    for (int l = 0; l <= order - k; ++l) {
      Vec3 x = a[at(k)][at(l)];
      for (int j = 1; j <= l; ++j) x = x - out.at(k, l - j) * (binomial(l, j) * w[0][at(j)]);
      for (int i = 1; i <= k; ++i) {
        x = x - out.at(k - i, l) * (binomial(k, i) * w[at(i)][0]);
        Vec3 y{};
        for (int j = 1; j <= l; ++j)
          y = y + out.at(k - i, l - j) * (binomial(l, j) * w[at(i)][at(j)]);
        x = x - y * binomial(k, i);
      }
      put(k, l, x / w[0][0]);
    }
  }
  return out;
}

std::optional<Vec3> NurbsSurface::normal(double u, double v) const noexcept {
  const auto unit = [](Vec3 a, Vec3 b) -> std::optional<Vec3> {
    const Vec3 n = cross(a, b);
    const double len = length(n), scale = length(a) * length(b);
    if (!(len > 1e-12 * scale) || !(len > 0)) return std::nullopt;
    return n / len;
  };
  const auto d = derivatives(u, v, 1);
  if (auto n = unit(d.at(1, 0), d.at(0, 1))) return n;
  // Degenerate here: the normal of a nearby point, stepping towards the domain's interior.
  const auto toward = [](double x, double lo, double hi) {
    return x + (x <= (lo + hi) / 2 ? 1 : -1) * 1e-7 * (hi - lo);
  };
  const double u2 = toward(u, knots_u().domain_start(), knots_u().domain_end());
  const double v2 = toward(v, knots_v().domain_start(), knots_v().domain_end());
  for (const auto [a, b] : {std::pair{u2, v}, std::pair{u, v2}, std::pair{u2, v2}}) {
    const auto e = derivatives(a, b, 1);
    if (auto n = unit(e.at(1, 0), e.at(0, 1))) return n;
  }
  return std::nullopt;
}

NurbsCurve3 NurbsSurface::iso_u(double u) const {
  const Data& s = *data_;
  u = std::clamp(u, s.ku.domain_start(), s.ku.domain_end());
  const int p = s.ku.degree(), m = count_v(), span = s.ku.find_span(u);
  BasisValues nb;
  basis_functions(s.ku, span, u, nb);
  std::vector<Vec3> pts(at(m));
  std::vector<double> ws(at(m), 0.0);
  for (int j = 0; j < m; ++j) {
    Vec3 h{};
    for (int i = 0; i <= p; ++i) {
      const std::size_t idx = at((span - p + i) * m + j);
      h = h + s.points[idx] * (nb[at(i)] * s.weights[idx]);
      ws[at(j)] += nb[at(i)] * s.weights[idx];
    }
    pts[at(j)] = h / ws[at(j)];
  }
  return NurbsCurve3::make(s.kv, std::move(pts), s.rational ? std::move(ws) : std::vector<double>{})
      .value();
}

NurbsCurve3 NurbsSurface::iso_v(double v) const {
  const Data& s = *data_;
  v = std::clamp(v, s.kv.domain_start(), s.kv.domain_end());
  const int q = s.kv.degree(), m = count_v(), n = count_u(), span = s.kv.find_span(v);
  BasisValues nb;
  basis_functions(s.kv, span, v, nb);
  std::vector<Vec3> pts(at(n));
  std::vector<double> ws(at(n), 0.0);
  for (int i = 0; i < n; ++i) {
    Vec3 h{};
    for (int j = 0; j <= q; ++j) {
      const std::size_t idx = at(i * m + (span - q + j));
      h = h + s.points[idx] * (nb[at(j)] * s.weights[idx]);
      ws[at(i)] += nb[at(j)] * s.weights[idx];
    }
    pts[at(i)] = h / ws[at(i)];
  }
  return NurbsCurve3::make(s.ku, std::move(pts), s.rational ? std::move(ws) : std::vector<double>{})
      .value();
}

NurbsSurface extrude(const NurbsCurve3& profile, Vec3 direction) {
  std::vector<Vec3> pts;
  std::vector<double> ws;
  for (std::size_t i = 0; i < profile.points().size(); ++i) {
    pts.push_back(profile.points()[i]);
    pts.push_back(profile.points()[i] + direction);
    ws.insert(ws.end(), 2, profile.weights()[i]);
  }
  return NurbsSurface::make(profile.knots(), KnotVector::make({0, 0, 1, 1}, 1).value(),
                            std::move(pts),
                            profile.rational() ? std::move(ws) : std::vector<double>{})
      .value();
}

// The NURBS Book A8.1, built on the unit arc of make_arc.
NurbsSurface revolve(const NurbsCurve3& profile, Vec3 origin, Vec3 axis, double angle) {
  const auto a = normalized(axis);
  if (!a) throw std::invalid_argument("revolution axis has no direction");
  const NurbsCurve2 unit = make_arc(Vec2{0, 0}, 1.0, 1.0, 0.0, 0.0, angle);  // validates angle
  std::vector<Vec3> pts;
  std::vector<double> ws;
  for (std::size_t i = 0; i < profile.points().size(); ++i) {
    const Vec3 p = profile.points()[i];
    const Vec3 o = origin + *a * dot(p - origin, *a);  // foot on the axis
    const Vec3 x = p - o, y = cross(*a, x);            // |x| = |y| = radius
    for (std::size_t k = 0; k < unit.points().size(); ++k) {
      pts.push_back(o + x * unit.points()[k].x + y * unit.points()[k].y);
      ws.push_back(profile.weights()[i] * unit.weights()[k]);
    }
  }
  return NurbsSurface::make(profile.knots(), unit.knots(), std::move(pts), std::move(ws)).value();
}

}  // namespace nf::math
