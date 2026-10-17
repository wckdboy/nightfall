// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include "nf/svg/export.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <utility>
#include <vector>

#include "nf/math/refine.hpp"

namespace nf::svg {

namespace {

using math::NurbsCurve2;
using math::Vec2;

double dist(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

// Shortest text that reads back to the same double; never "-0".
std::string format(double x) {
  if (x == 0.0) x = 0.0;
  std::array<char, 32> buf{};
  const auto r = std::to_chars(buf.data(), buf.data() + buf.size(), x);
  return std::string(buf.data(), r.ptr);
}

class Writer {
 public:
  explicit Writer(const ExportOptions& options) : options_(options) {}

  void subpath(const Subpath& sp) {
    if (sp.segments.empty()) return;
    command('M', {sp.segments.front().points().front()});
    for (std::size_t i = 0; i < sp.segments.size(); ++i) {
      const NurbsCurve2& seg = sp.segments[i];
      const bool closing_line = sp.closed && i + 1 == sp.segments.size() && seg.degree() == 1 &&
                                seg.points().size() == 2 &&
                                seg.points().back() == sp.segments.front().points().front();
      if (!closing_line) segment(seg);
    }
    if (sp.closed) out_.text += 'Z';
  }

  std::expected<Exported, std::string> finish() {
    if (failed_) return std::unexpected(*failed_);
    return std::move(out_);
  }

 private:
  void segment(const NurbsCurve2& curve) {
    for (const NurbsCurve2& b : math::decompose(curve)) span(b);
  }

  // One Bézier span.
  void span(const NurbsCurve2& b) {
    const auto p = b.points();
    const auto w = b.weights();
    if (b.degree() == 1) return command('L', {p[1]});
    if (!b.rational() && b.degree() == 2) return command('Q', {p[1], p[2]});
    if (!b.rational() && b.degree() == 3) return command('C', {p[1], p[2], p[3]});
    if (b.degree() == 2) {
      const double sw = w[1] / std::sqrt(w[0] * w[2]);  // standard-form weight
      const bool collinear =
          std::abs(cross(p[1] - p[0], p[2] - p[0])) <=
          1e-14 * (dist(p[0], p[1]) + dist(p[1], p[2])) * (dist(p[0], p[1]) + dist(p[1], p[2]));
      if (!collinear && sw == 1.0) return command('Q', {p[1], p[2]});  // a parabola
      if (!collinear && sw < 1.0) return elliptical_arc(p[0], p[1], p[2], sw);
    }
    approximate(b, b.domain_start(), b.domain_end(), 0);
  }

  // A rational quadratic span in standard form with weight w < 1 is an elliptical arc of
  // sweep 2·acos(w) (affine image of a circular arc). Its centre lies on the line from the
  // chord's midpoint through P1; P0 − C and the tangent at P0 are conjugate semi-diameters.
  void elliptical_arc(Vec2 p0, Vec2 p1, Vec2 p2, double w) {
    const Vec2 mid = (p0 + p2) / 2.0;
    const Vec2 center = mid - (p1 - mid) * (w * w / (1.0 - w * w));
    const double sweep = 2.0 * std::acos(w);
    const Vec2 u = p0 - center;
    const Vec2 v = (p2 - center - u * std::cos(sweep)) / std::sin(sweep);
    // Principal axes of M = [u v] by a closed-form 2×2 SVD: M = R(phi)·diag(sx, sy)·R(theta).
    const double e = (u.x + v.y) / 2, f = (u.x - v.y) / 2, g = (u.y + v.x) / 2, h = (u.y - v.x) / 2;
    const double q = std::hypot(e, h), r = std::hypot(f, g);
    const double phi = (std::atan2(g, f) + std::atan2(h, e)) / 2;
    const double rx = q + r, ry = std::abs(q - r);
    const bool positive = cross(u, v) > 0;  // increasing angle runs from +x toward +y
    command('A', {}, {rx, ry, phi * 180.0 / std::numbers::pi, 0.0, positive ? 1.0 : 0.0}, p2);
  }

  // Cubic Hermite pieces with the curve's own end tangents, halved until a rigorous bound on
  // their distance from the curve is within the tolerance.
  void approximate(const NurbsCurve2& c, double a, double b, int depth) {
    if (failed_) return;
    std::array<Vec2, 2> da{}, db{};
    c.derivatives(a, 1, da);
    c.derivatives(b, 1, db);
    const double len = (b - a) / 3.0;
    const std::array<Vec2, 4> cubic{da[0], da[0] + da[1] * len, db[0] - db[1] * len, db[0]};
    const double bound = deviation_bound(cubic, restrict(c, a, b));
    if (bound > options_.tolerance) {
      if (depth >= kMaxDepth || ++pieces_ > kMaxPieces) {
        failed_ = "tolerance " + format(options_.tolerance) + " is not reachable for this curve";
        return;
      }
      const double m = (a + b) / 2;
      approximate(c, a, m, depth + 1);
      approximate(c, m, b, depth + 1);
      return;
    }
    out_.max_error = std::max(out_.max_error, bound);
    command('C', {cubic[1], cubic[2], cubic[3]});
  }

  // The single-span curve c on [a, b], as a Bézier over that interval.
  static NurbsCurve2 restrict(NurbsCurve2 c, double a, double b) {
    if (a > c.domain_start()) c = math::split(c, a).value().second;
    if (b < c.domain_end()) c = math::split(c, b).value().first;
    return c;
  }

  // Upper bound on |B(t) − C(t)| over t ∈ [0, 1] for a cubic B and a rational Bézier
  // C = N / w of degree p: B·w − N is a polynomial of degree p + 3 whose Bernstein
  // coefficients come from products of Bernstein polynomials, so |B·w − N| ≤ max |num_k|
  // (convex hull) and w ≥ min w_j.
  static double deviation_bound(const std::array<Vec2, 4>& cubic, const NurbsCurve2& c) {
    const int p = c.degree(), n = p + 3;
    const auto binom = [](int top, int k) {
      double r = 1.0;
      for (int i = 1; i <= k; ++i) r = r * (top - k + i) / i;
      return r;
    };
    const auto w = c.weights();
    const auto pts = c.points();
    double worst = 0.0;
    for (int k = 0; k <= n; ++k) {
      Vec2 num{};
      for (int i = std::max(0, k - p); i <= std::min(3, k); ++i) {
        const auto j = static_cast<std::size_t>(k - i);
        const double coef = binom(3, i) * binom(p, k - i) / binom(n, k);
        num = num + (cubic[static_cast<std::size_t>(i)] - pts[j]) * (w[j] * coef);
      }
      worst = std::max(worst, std::hypot(num.x, num.y));
    }
    return worst / std::ranges::min(w);
  }

  void command(char c, std::initializer_list<Vec2> points,
               std::initializer_list<double> numbers = {}, Vec2 end = {}) {
    out_.text += c;
    bool first = true;
    const auto put = [&](double x) {
      if (!first && !(x < 0)) out_.text += ' ';  // a minus sign separates by itself
      first = false;
      out_.text += format(x);
    };
    for (const double x : numbers) put(x);
    for (const Vec2 q : points) {
      put(q.x);
      put(q.y);
    }
    if (c == 'A') {
      put(end.x);
      put(end.y);
    }
  }

  static constexpr int kMaxDepth = 30;
  static constexpr long kMaxPieces = 1L << 20;

  const ExportOptions& options_;
  Exported out_;
  long pieces_ = 0;
  std::optional<std::string> failed_;
};

Subpath flipped(const Subpath& sp) {
  Subpath out{{}, sp.closed};
  for (const NurbsCurve2& c : sp.segments) {
    std::vector<Vec2> points;
    for (const Vec2 p : c.points()) points.push_back({p.x, -p.y});
    out.segments.push_back(
        NurbsCurve2::make(c.knots(), std::move(points),
                          c.rational() ? std::vector<double>(c.weights().begin(), c.weights().end())
                                       : std::vector<double>{})
            .value());
  }
  return out;
}

}  // namespace

std::expected<Exported, std::string> write_path(std::span<const Subpath> paths,
                                                const ExportOptions& options) {
  if (!(options.tolerance > 0.0 && std::isfinite(options.tolerance))) {
    return std::unexpected("tolerance must be positive and finite");
  }
  Writer w(options);
  for (const Subpath& sp : paths) w.subpath(sp);
  return w.finish();
}

std::expected<Exported, std::string> write_document(std::span<const Subpath> model_paths,
                                                    const ExportOptions& options) {
  std::vector<Subpath> svg_paths;
  Vec2 lo{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
  Vec2 hi = lo * -1.0;
  for (const Subpath& sp : model_paths) {
    svg_paths.push_back(flipped(sp));
    for (const NurbsCurve2& c : svg_paths.back().segments) {
      for (const Vec2 p : c.points()) {
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
      }
    }
  }
  if (!(lo.x <= hi.x)) lo = hi = Vec2{};  // nothing to draw
  // A zero-size viewport disables rendering, so a degenerate box (a single horizontal or
  // vertical line, or nothing) gets 1 mm in that direction; the reader ignores the offset.
  for (auto [l, h] : {std::pair{&lo.x, &hi.x}, std::pair{&lo.y, &hi.y}}) {
    if (*h - *l <= 0.0) {
      *l -= 0.5;
      *h += 0.5;
    }
  }
  auto written = write_path(svg_paths, options);
  if (!written) return std::unexpected(written.error());
  const Exported& path = *written;
  const double width = hi.x - lo.x, height = hi.y - lo.y;
  Exported doc;
  doc.max_error = path.max_error;
  doc.text =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" +
      format(width) + "mm\" height=\"" + format(height) + "mm\" viewBox=\"" + format(lo.x) + ' ' +
      format(lo.y) + ' ' + format(width) + ' ' + format(height) + "\">\n  <path d=\"" + path.text +
      "\" fill=\"none\" stroke=\"black\" stroke-width=\"0.1\"/>\n</svg>\n";
  return doc;
}

}  // namespace nf::svg
