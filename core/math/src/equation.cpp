// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include "nf/math/equation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <numbers>
#include <optional>
#include <vector>

#include "nf/math/constructors.hpp"
#include "nf/math/expression.hpp"
#include "nf/math/fit.hpp"
#include "nf/math/refine.hpp"

namespace nf::math {

namespace {
double binom(int n, int k) {
  double r = 1.0;
  for (int i = 1; i <= k; ++i) r = r * (n - k + i) / i;
  return r;
}

}  // namespace

std::expected<EquationCurve, std::string> equation_curve(std::string_view xs, std::string_view ys,
                                                         std::string_view zs, double t0, double t1,
                                                         double tolerance) {
  if (!(std::isfinite(t0) && std::isfinite(t1) && t1 > t0)) return std::unexpected("need t0 < t1");
  if (!(tolerance > 0 && std::isfinite(tolerance)))
    return std::unexpected("tolerance must be positive");
  std::vector<Expression> e;
  const std::array<std::string_view, 3> src{xs, ys, zs};
  for (std::size_t i = 0; i < 3; ++i) {
    auto parsed = Expression::parse(src[i]);
    if (!parsed) return std::unexpected(std::string("xyz").substr(i, 1) + ": " + parsed.error());
    e.push_back(std::move(*parsed));
  }

  // Polynomials: Bézier on [t0, t1], parameter t.
  using Poly = std::vector<double>;
  std::array<std::optional<Poly>, 3> p{e[0].polynomial(), e[1].polynomial(), e[2].polynomial()};
  if (p[0] && p[1] && p[2]) {
    int n = 1;
    for (const auto& c : p) n = std::max(n, static_cast<int>(c->size()) - 1);
    const double h = t1 - t0;
    std::vector<Vec3> pts(static_cast<std::size_t>(n + 1));
    for (std::size_t axis = 0; axis < 3; ++axis) {
      Poly c = *p[axis];
      c.resize(static_cast<std::size_t>(n + 1), 0.0);
      Poly q(c.size(), 0.0);  // in s = (t − t0)/h
      for (int k = 0; k <= n; ++k) {
        for (int j = k; j <= n; ++j)
          q[static_cast<std::size_t>(k)] +=
              c[static_cast<std::size_t>(j)] * binom(j, k) * std::pow(t0, j - k) * std::pow(h, k);
      }
      for (int i = 0; i <= n; ++i) {  // power → Bernstein
        double b = 0.0;
        for (int k = 0; k <= i; ++k)
          b += binom(i, k) / binom(n, k) * q[static_cast<std::size_t>(k)];
        double* coord = axis == 0   ? &pts[static_cast<std::size_t>(i)].x
                        : axis == 1 ? &pts[static_cast<std::size_t>(i)].y
                                    : &pts[static_cast<std::size_t>(i)].z;
        *coord = b;
      }
    }
    std::vector<double> knots(static_cast<std::size_t>(n + 1), t0);
    knots.insert(knots.end(), static_cast<std::size_t>(n + 1), t1);
    auto c = NurbsCurve3::make(KnotVector::make(std::move(knots), n).value(), std::move(pts));
    if (!c) return std::unexpected("polynomial coefficients are not finite");
    return EquationCurve{std::move(*c), true, 0.0};
  }

  // a + b cos(ωt) + c sin(ωt) in every component: an affine image of a unit-circle arc.
  std::array<std::optional<Expression::Sinusoid>, 3> g{e[0].sinusoid(), e[1].sinusoid(),
                                                       e[2].sinusoid()};
  if (g[0] && g[1] && g[2]) {
    std::optional<double> omega;
    bool same = true;
    for (const auto& c : g) {
      if (c->omega && omega && *c->omega != *omega) same = false;
      if (c->omega) omega = c->omega;
    }
    if (same && omega && std::abs(*omega) * (t1 - t0) <= 2 * std::numbers::pi) {
      const double a0 = *omega * t0, a1 = *omega * t1;
      NurbsCurve2 unit = make_arc(Vec2{0, 0}, 1, 1, 0, std::min(a0, a1), std::max(a0, a1));
      if (*omega < 0) unit = reverse(unit);
      std::vector<Vec3> pts;
      for (const Vec2 q : unit.points()) {
        pts.push_back({g[0]->a + g[0]->b * q.x + g[0]->c * q.y,
                       g[1]->a + g[1]->b * q.x + g[1]->c * q.y,
                       g[2]->a + g[2]->b * q.x + g[2]->c * q.y});
      }
      auto c = NurbsCurve3::make(unit.knots(), std::move(pts),
                                 std::vector<double>(unit.weights().begin(), unit.weights().end()));
      if (c) return EquationCurve{std::move(*c), true, 0.0};
    }
  }

  // Anything else: fitted with t itself as the parameter (an equation's own parameter is
  // smooth, so no correction is needed), then checked at 4× denser t: |C(u(t)) − f(t)| bounds
  // the distance there. More samples when refitting alone cannot get there (a kink, say).
  const auto at = [&](double t) { return Vec3{e[0](t), e[1](t), e[2](t)}; };
  constexpr int kDense = 4;
  for (const int count : {256, 1024, 4096, 16384}) {
    std::vector<Vec3> samples;
    std::vector<double> us;
    for (int i = 0; i <= count; ++i) {
      const double t = t0 + (t1 - t0) * i / count;
      const Vec3 q = at(t);
      if (!(std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z))) {
        return std::unexpected("the equations are not finite at t = " + std::to_string(t));
      }
      samples.push_back(q);
      us.push_back(static_cast<double>(i) / count);
    }
    double target = tolerance / 2;
    for (int round = 0; round < 3; ++round, target /= 4) {
      auto fit = approximate<Vec3>(samples, target, 3, us);
      if (!fit) break;  // try more samples
      double worst = fit->max_deviation;
      for (int i = 0; i < count * kDense; ++i) {
        const double u = static_cast<double>(i) / (count * kDense);
        worst = std::max(worst, length(fit->curve.point(u) - at(t0 + (t1 - t0) * u)));
      }
      if (worst <= tolerance) return EquationCurve{std::move(fit->curve), false, worst};
    }
  }
  return std::unexpected("could not fit the curve within the tolerance");
}

}  // namespace nf::math
