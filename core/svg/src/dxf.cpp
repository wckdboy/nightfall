// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include "nf/svg/dxf.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <vector>

#include "nf/math/query.hpp"
#include "nf/math/refine.hpp"
#include "number.hpp"

namespace nf::svg {

namespace {

using math::NurbsCurve2;
using math::Vec2;

constexpr double kDegrees = 180.0 / std::numbers::pi;

// A circular arc span: centre, radius, start and end angles (radians), turning
// counter-clockwise from start to end when `ccw`.
struct CircularArc {
  Vec2 center;
  double radius, from, sweep;  // sweep > 0 counter-clockwise, < 0 clockwise
};

// A rational quadratic span is a circular arc when its control triangle is isosceles and its
// standard-form weight is the cosine of the angle between chord and tangent (half the sweep).
std::optional<CircularArc> circular(const NurbsCurve2& b) {
  if (b.degree() != 2 || b.points().size() != 3) return std::nullopt;
  const Vec2 p0 = b.points()[0], p1 = b.points()[1], p2 = b.points()[2];
  const auto w = b.weights();
  const double sw = w[1] / std::sqrt(w[0] * w[2]);
  const double a = length(p1 - p0), c = length(p2 - p1), chord = length(p2 - p0);
  if (!(sw < 1.0) || !(a > 0 && c > 0 && chord > 0)) return std::nullopt;
  const double tol = 1e-12 * (a + c);
  const double half = std::acos(std::clamp(dot(p1 - p0, p2 - p0) / (a * chord), -1.0, 1.0));
  if (std::abs(a - c) > tol || std::abs(std::cos(half) - sw) > 1e-12) return std::nullopt;
  const Vec2 mid = (p0 + p2) / 2.0;
  const Vec2 center = mid - (p1 - mid) * (sw * sw / (1.0 - sw * sw));
  const double sweep = 2 * half * (cross(p1 - p0, p2 - p1) > 0 ? 1.0 : -1.0);
  const Vec2 r0 = p0 - center;
  return CircularArc{center, length(r0), std::atan2(r0.y, r0.x), sweep};
}

// How far the span strays from the arc's circle, at its ends and 31 points between.
double residual(const NurbsCurve2& b, const CircularArc& arc) {
  double worst = 0.0;
  for (int i = 0; i <= 32; ++i) {
    const double u = b.domain_start() + (b.domain_end() - b.domain_start()) * i / 32;
    worst = std::max(worst, std::abs(length(b.point(u) - arc.center) - arc.radius));
  }
  return worst;
}

double extent(const NurbsCurve2& c) {
  Vec2 lo = c.points().front(), hi = lo;
  for (const Vec2 q : c.points()) {
    lo = {std::min(lo.x, q.x), std::min(lo.y, q.y)};
    hi = {std::max(hi.x, q.x), std::max(hi.y, q.y)};
  }
  return length(hi - lo);
}

class Writer {
 public:
  explicit Writer(const ExportOptions& options) : options_(options) {}

  void subpath(const Subpath& sp) {
    if (sp.segments.empty()) return;
    if (whole_circle(sp)) return;
    for (const NurbsCurve2& segment : sp.segments) {
      for (const NurbsCurve2& span : math::decompose(segment)) {
        if (span.degree() == 1) {
          flush();
          line(span.points().front(), span.points().back());
        } else if (const auto arc = as_arc(span)) {
          flush();
          emit_arc(*arc);
        } else {
          approximate(span);
        }
      }
    }
    flush();
  }

  std::string finish() {
    if (!(lo_.x <= hi_.x)) lo_ = hi_ = Vec2{};
    std::string out;
    const auto put = [&](int code, std::string_view value) {
      out += std::to_string(code);
      out += '\n';
      out += value;
      out += '\n';
    };
    put(0, "SECTION");
    put(2, "HEADER");
    put(9, "$ACADVER");
    put(1, "AC1009");
    put(9, "$EXTMIN");
    put(10, detail::format_number(lo_.x));
    put(20, detail::format_number(lo_.y));
    put(30, "0");
    put(9, "$EXTMAX");
    put(10, detail::format_number(hi_.x));
    put(20, detail::format_number(hi_.y));
    put(30, "0");
    put(0, "ENDSEC");
    put(0, "SECTION");
    put(2, "ENTITIES");
    out += body_;
    put(0, "ENDSEC");
    put(0, "EOF");
    return out;
  }

  double max_error() const { return max_error_; }

 private:
  // A circular span whose distance from its circle fits the tolerance; that distance (a
  // rounding-level residual for true circles) counts towards the reported error.
  std::optional<CircularArc> as_arc(const NurbsCurve2& span) {
    const auto arc = circular(span);
    if (!arc) return std::nullopt;
    const double off = residual(span, *arc);
    if (off > options_.tolerance) return std::nullopt;
    max_error_ = std::max(max_error_, off);
    return arc;
  }

  // A closed subpath that is a single curve tracing one whole circle.
  bool whole_circle(const Subpath& sp) {
    if (sp.segments.size() != 1) return false;
    const NurbsCurve2& c = sp.segments.front();
    // Closed up to rounding (a circle's end is computed from cos 2π, not exactly 1).
    const Vec2 span = c.points().front() - c.points().back();
    if (length(span) > 1e-12 * (1 + length(c.points().front()))) return false;
    const auto spans = math::decompose(c);
    double total = 0.0;
    std::optional<CircularArc> first;
    for (const NurbsCurve2& s : spans) {
      const auto arc = circular(s);
      if (!arc || residual(s, *arc) > options_.tolerance) return false;
      if (!first) first = arc;
      if (length(arc->center - first->center) > 1e-12 * first->radius ||
          std::abs(arc->radius - first->radius) > 1e-12 * first->radius) {
        return false;
      }
      total += arc->sweep;
    }
    if (std::abs(std::abs(total) - 2 * std::numbers::pi) > 1e-9) return false;
    entity("CIRCLE");
    point(10, first->center);
    num(40, first->radius);
    grow(first->center - Vec2{first->radius, first->radius});
    grow(first->center + Vec2{first->radius, first->radius});
    return true;
  }

  void line(Vec2 a, Vec2 b) {
    entity("LINE");
    point(10, a);
    point(11, b);
    grow(a);
    grow(b);
  }

  void emit_arc(const CircularArc& arc) {
    // DXF arcs run counter-clockwise from the start angle to the end angle.
    const double a0 = arc.sweep > 0 ? arc.from : arc.from + arc.sweep;
    entity("ARC");
    point(10, arc.center);
    num(40, arc.radius);
    num(50, a0 * kDegrees);
    num(51, (a0 + std::abs(arc.sweep)) * kDegrees);
    const Vec2 r{arc.radius, arc.radius};
    grow(arc.center - r);  // the arc lies within its circle's box
    grow(arc.center + r);
  }

  // Approximated spans gather into one POLYLINE until an exact entity or the subpath's end.
  void approximate(const NurbsCurve2& span) {
    // polyline() raises tolerances below 1e-12 of the curve's size to that; report what it used.
    max_error_ = std::max(max_error_, std::max(options_.tolerance, 1e-12 * extent(span)));
    const auto pl = math::polyline(span, options_.tolerance);
    for (const auto& v : pl) {
      if (run_.empty() || v.point != run_.back()) run_.push_back(v.point);
    }
  }

  void flush() {
    if (run_.size() < 2) {
      run_.clear();
      return;
    }
    entity("POLYLINE");
    pair(66, "1");
    point(10, Vec2{});
    pair(70, "0");
    for (const Vec2 p : run_) {
      entity("VERTEX");
      point(10, p);
      grow(p);
    }
    entity("SEQEND");
    run_.clear();
  }

  void entity(std::string_view type) {
    pair(0, type);
    pair(8, "0");
  }
  void point(int code, Vec2 p) {
    num(code, p.x);
    num(code + 10, p.y);
    pair(code + 20, "0");
  }
  void num(int code, double x) { pair(code, detail::format_number(x)); }
  void pair(int code, std::string_view value) {
    body_ += std::to_string(code);
    body_ += '\n';
    body_ += value;
    body_ += '\n';
  }
  void grow(Vec2 p) {
    lo_ = {std::min(lo_.x, p.x), std::min(lo_.y, p.y)};
    hi_ = {std::max(hi_.x, p.x), std::max(hi_.y, p.y)};
  }

  const ExportOptions& options_;
  std::string body_;
  std::vector<Vec2> run_;
  double max_error_ = 0.0;
  Vec2 lo_{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
  Vec2 hi_{-std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
};

}  // namespace

std::expected<Exported, std::string> write_dxf(std::span<const Subpath> model_paths,
                                               const ExportOptions& options) {
  if (!(options.tolerance > 0.0 && std::isfinite(options.tolerance))) {
    return std::unexpected("tolerance must be positive and finite");
  }
  Writer w(options);
  for (const Subpath& sp : model_paths) w.subpath(sp);
  const double error = w.max_error();
  return Exported{w.finish(), error};
}

}  // namespace nf::svg
