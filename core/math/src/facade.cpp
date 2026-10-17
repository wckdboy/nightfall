// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include "nf/math/facade.hpp"

#include <array>
#include <cmath>
#include <exception>
#include <optional>
#include <span>

#include "nf/math/constructors.hpp"
#include "nf/math/query.hpp"

namespace nf::facade {

namespace {

math::Vec2 in(Point2 p) { return {p.x, p.y}; }
math::Vec3 in(Point3 p) { return {p.x, p.y, p.z}; }
Point2 out(math::Vec2 p) { return {p.x, p.y}; }
Point3 out(math::Vec3 p) { return {p.x, p.y, p.z}; }

template <typename Vec, typename P>
std::vector<Vec> in_all(const std::vector<P>& ps) {
  std::vector<Vec> v;
  v.reserve(ps.size());
  for (const P& p : ps) v.push_back(in(p));
  return v;
}

// Runs a construction, turning any failure (invalid input) into an empty curve.
template <typename Curve, typename F>
std::optional<Curve> attempt(F make) noexcept {
  try {
    return make();
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

template <typename Curve, typename P>
std::optional<Curve> general(int degree, const Numbers& knots, const std::vector<P>& points,
                             const Numbers& weights) {
  using Vec = decltype(in(P{}));
  auto kv = math::KnotVector::make(knots, degree);
  if (!kv) return std::nullopt;
  auto c = Curve::make(std::move(*kv), in_all<Vec>(points), weights);
  if (!c) return std::nullopt;
  return std::move(*c);
}

// Shared queries for both dimensions.
template <typename Curve, typename P>
struct Queries {
  static P point(const std::optional<Curve>& c, double u) { return c ? out(c->point(u)) : P{}; }
  static P derivative(const std::optional<Curve>& c, double u) {
    if (!c) return P{};
    std::array<decltype(c->point(0)), 2> d{};
    c->derivatives(u, 1, d);
    return out(d[1]);
  }
  static std::vector<P> polyline(const std::optional<Curve>& c, double tol) {
    std::vector<P> v;
    if (!c || !(tol > 0 && std::isfinite(tol))) return v;
    for (const auto& q : math::polyline(*c, tol)) v.push_back(out(q.point));
    return v;
  }
  static double closest(const std::optional<Curve>& c, P p) {
    return c ? math::closest_point(*c, in(p)).u : 0.0;
  }
  static Numbers knots(const std::optional<Curve>& c) {
    return c ? Numbers(c->knots().knots().begin(), c->knots().knots().end()) : Numbers{};
  }
  static std::vector<P> points(const std::optional<Curve>& c) {
    std::vector<P> v;
    if (c) {
      for (const auto& q : c->points()) v.push_back(out(q));
    }
    return v;
  }
  static Numbers weights(const std::optional<Curve>& c) {
    return c ? Numbers(c->weights().begin(), c->weights().end()) : Numbers{};
  }
};

using Q2 = Queries<math::NurbsCurve2, Point2>;
using Q3 = Queries<math::NurbsCurve3, Point3>;

}  // namespace

struct Curve2::Impl {
  std::optional<math::NurbsCurve2> c;
};
struct Curve3::Impl {
  std::optional<math::NurbsCurve3> c;
};

// ---- Curve2

Curve2::Curve2() : impl_(std::make_shared<const Impl>()) {}

Curve2 Curve2::line(Point2 a, Point2 b) {
  Curve2 r;
  r.impl_ = std::make_shared<const Impl>(
      Impl{attempt<math::NurbsCurve2>([&] { return math::make_line(in(a), in(b)); })});
  return r;
}
Curve2 Curve2::arc(Point2 center, double rx, double ry, double rotation, double start, double end) {
  if (!(rx > 0 && ry > 0 && std::isfinite(rx) && std::isfinite(ry))) return {};
  Curve2 r;
  r.impl_ = std::make_shared<const Impl>(Impl{attempt<math::NurbsCurve2>(
      [&] { return math::make_arc(in(center), rx, ry, rotation, start, end); })});
  return r;
}
Curve2 Curve2::circle(Point2 center, double radius) {
  if (!(radius > 0 && std::isfinite(radius))) return {};
  Curve2 r;
  r.impl_ = std::make_shared<const Impl>(
      Impl{attempt<math::NurbsCurve2>([&] { return math::make_circle(in(center), radius); })});
  return r;
}
Curve2 Curve2::bezier(const Points2& points) {
  Curve2 r;
  const auto v = in_all<math::Vec2>(points);
  r.impl_ = std::make_shared<const Impl>(
      Impl{attempt<math::NurbsCurve2>([&] { return math::make_bezier(std::span(v)); })});
  return r;
}
Curve2 Curve2::nurbs(int degree, const Numbers& knots, const Points2& points,
                     const Numbers& weights) {
  Curve2 r;
  r.impl_ = std::make_shared<const Impl>(
      Impl{general<math::NurbsCurve2>(degree, knots, points, weights)});
  return r;
}
bool Curve2::valid() const { return impl_->c.has_value(); }
int Curve2::degree() const { return impl_->c ? impl_->c->degree() : 0; }
bool Curve2::rational() const { return impl_->c && impl_->c->rational(); }
double Curve2::domain_start() const { return impl_->c ? impl_->c->domain_start() : 0.0; }
double Curve2::domain_end() const { return impl_->c ? impl_->c->domain_end() : 0.0; }
Numbers Curve2::knots() const { return Q2::knots(impl_->c); }
Points2 Curve2::control_points() const { return Q2::points(impl_->c); }
Numbers Curve2::weights() const { return Q2::weights(impl_->c); }
Point2 Curve2::point(double u) const { return Q2::point(impl_->c, u); }
Point2 Curve2::derivative(double u) const { return Q2::derivative(impl_->c, u); }
Points2 Curve2::polyline(double tolerance) const { return Q2::polyline(impl_->c, tolerance); }
double Curve2::closest(Point2 p) const { return Q2::closest(impl_->c, p); }

// ---- Curve3

Curve3::Curve3() : impl_(std::make_shared<const Impl>()) {}

Curve3 Curve3::line(Point3 a, Point3 b) {
  Curve3 r;
  r.impl_ = std::make_shared<const Impl>(
      Impl{attempt<math::NurbsCurve3>([&] { return math::make_line(in(a), in(b)); })});
  return r;
}
Curve3 Curve3::circle(Point3 center, Point3 normal, double radius) {
  if (!(radius > 0 && std::isfinite(radius))) return {};
  Curve3 r;
  r.impl_ = std::make_shared<const Impl>(Impl{attempt<math::NurbsCurve3>(
      [&] { return math::make_circle(in(center), in(normal), radius); })});
  return r;
}
Curve3 Curve3::bezier(const Points3& points) {
  Curve3 r;
  const auto v = in_all<math::Vec3>(points);
  r.impl_ = std::make_shared<const Impl>(
      Impl{attempt<math::NurbsCurve3>([&] { return math::make_bezier(std::span(v)); })});
  return r;
}
Curve3 Curve3::nurbs(int degree, const Numbers& knots, const Points3& points,
                     const Numbers& weights) {
  Curve3 r;
  r.impl_ = std::make_shared<const Impl>(
      Impl{general<math::NurbsCurve3>(degree, knots, points, weights)});
  return r;
}
bool Curve3::valid() const { return impl_->c.has_value(); }
int Curve3::degree() const { return impl_->c ? impl_->c->degree() : 0; }
bool Curve3::rational() const { return impl_->c && impl_->c->rational(); }
double Curve3::domain_start() const { return impl_->c ? impl_->c->domain_start() : 0.0; }
double Curve3::domain_end() const { return impl_->c ? impl_->c->domain_end() : 0.0; }
Numbers Curve3::knots() const { return Q3::knots(impl_->c); }
Points3 Curve3::control_points() const { return Q3::points(impl_->c); }
Numbers Curve3::weights() const { return Q3::weights(impl_->c); }
Point3 Curve3::point(double u) const { return Q3::point(impl_->c, u); }
Point3 Curve3::derivative(double u) const { return Q3::derivative(impl_->c, u); }
Points3 Curve3::polyline(double tolerance) const { return Q3::polyline(impl_->c, tolerance); }
double Curve3::closest(Point3 p) const { return Q3::closest(impl_->c, p); }

}  // namespace nf::facade
