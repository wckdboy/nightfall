// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include "nf/svg/path.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <numbers>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>

#include <xlocale.h>

#include "nf/math/constructors.hpp"
#include "nf/math/refine.hpp"

namespace nf::svg {

namespace {

using math::NurbsCurve2;
using math::Vec2;

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
bool is_digit(char c) { return c >= '0' && c <= '9'; }
bool is_command(char c) { return std::string_view("MmLlHhVvCcSsQqTtAaZz").contains(c); }
char upper(char c) { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c; }

double angle(Vec2 u, Vec2 v) { return std::atan2(u.x * v.y - u.y * v.x, u.x * v.x + u.y * v.y); }

// Replaces a curve's end control points with exact values (they differ only by rounding),
// so consecutive segments meet exactly.
NurbsCurve2 with_ends(const NurbsCurve2& c, Vec2 a, Vec2 b) {
  std::vector<Vec2> points(c.points().begin(), c.points().end());
  points.front() = a;
  points.back() = b;
  return NurbsCurve2::make(c.knots(), std::move(points),
                           std::vector<double>(c.weights().begin(), c.weights().end()))
      .value();
}

// SVG 2 §9.5.1 / implementation notes B.2.4: endpoint to centre parameterisation.
NurbsCurve2 arc(Vec2 p0, double rx, double ry, double rotation_deg, bool large, bool sweep,
                Vec2 p1) {
  // Reduced first: exact, and keeps a huge finite rotation from overflowing.
  const double phi = std::fmod(rotation_deg, 360.0) * std::numbers::pi / 180.0;
  const double c = std::cos(phi), s = std::sin(phi);
  const Vec2 h = (p0 - p1) / 2.0;
  const Vec2 q{c * h.x + s * h.y, -s * h.x + c * h.y};  // (x1', y1')
  // Work in units of the largest length involved, so no square overflows or underflows.
  const double scale = std::max({std::abs(rx), std::abs(ry), std::abs(q.x), std::abs(q.y)});
  double a = std::abs(rx) / scale, b = std::abs(ry) / scale;
  const Vec2 qs = q / scale;
  const double lambda = qs.x * qs.x / (a * a) + qs.y * qs.y / (b * b);
  if (lambda > 1.0) {
    a *= std::sqrt(lambda);
    b *= std::sqrt(lambda);
  }
  const double num = a * a * b * b - a * a * qs.y * qs.y - b * b * qs.x * qs.x;
  const double den = a * a * qs.y * qs.y + b * b * qs.x * qs.x;
  const double ratio = num / den;
  if (!std::isfinite(ratio)) throw std::range_error("arc out of range");
  const double k = std::sqrt(std::max(0.0, ratio)) * (large == sweep ? -1.0 : 1.0);
  const Vec2 cs{k * a * qs.y / b, -k * b * qs.x / a};  // (cx', cy') / scale
  const Vec2 cq = cs * scale;
  const Vec2 mid = (p0 + p1) / 2.0;
  const Vec2 center{c * cq.x - s * cq.y + mid.x, s * cq.x + c * cq.y + mid.y};
  const Vec2 u{(qs.x - cs.x) / a, (qs.y - cs.y) / b};
  const Vec2 v{(-qs.x - cs.x) / a, (-qs.y - cs.y) / b};
  const double theta = angle({1.0, 0.0}, u);
  double delta = angle(u, v);
  if (!sweep && delta > 0.0) delta -= 2.0 * std::numbers::pi;
  if (sweep && delta < 0.0) delta += 2.0 * std::numbers::pi;
  rx = a * scale;
  ry = b * scale;
  const NurbsCurve2 curve =
      delta > 0.0 ? math::make_arc(center, rx, ry, phi, theta, theta + delta)
                  : math::reverse(math::make_arc(center, rx, ry, phi, theta + delta, theta));
  // The computed arc must reach both endpoints up to rounding before they are snapped;
  // anything else means the numbers were out of range.
  const double tol =
      1e-9 * std::max({rx, ry, std::abs(p0.x), std::abs(p0.y), std::abs(p1.x), std::abs(p1.y)});
  const auto off = [](Vec2 x, Vec2 y) { return std::hypot(x.x - y.x, x.y - y.y); };
  if (!(off(curve.points().front(), p0) <= tol && off(curve.points().back(), p1) <= tol)) {
    throw std::range_error("arc out of range");
  }
  return with_ends(curve, p0, p1);
}

class Parser {
 public:
  explicit Parser(std::string_view d) : d_(d) {}

  std::expected<std::vector<Subpath>, ParseError> run() {
    skip_space();
    while (pos_ < d_.size() && !error_) step();
    if (!error_ && comma_pending_) fail_at(comma_at_, "comma without a following argument");
    if (error_) return std::unexpected(std::move(*error_));
    flush();
    return std::move(out_);
  }

 private:
  // A command; geometry that overflows (huge but finite numbers) is a parse error at the
  // command, never an exception escaping the importer.
  void step() {
    const std::size_t at = pos_;
    try {
      command();
    } catch (const std::exception&) {
      return fail_at(at, "coordinates out of range");
    }
    if (!error_ && !(std::isfinite(p_.x) && std::isfinite(p_.y))) {
      fail_at(at, "coordinates out of range");
    }
  }

  // One command with one set of arguments (implicit repeats come back here).
  void command() {
    char cmd = d_[pos_];
    if (is_command(cmd)) {
      if (comma_pending_) return fail_at(comma_at_, "comma without a following argument");
      ++pos_;
      skip_space();
    } else if (repeat_ != 0 && starts_number()) {
      cmd = repeat_;
    } else {
      return fail("expected a path command");
    }
    const char C = upper(cmd);
    const bool rel = cmd != C;
    if (!started_ && C != 'M') return fail("path data must start with M");
    const Vec2 o = rel ? p_ : Vec2{};  // origin for relative coordinates

    Vec2 a, b, t;
    switch (C) {
      case 'M':
        if (!point(t)) return;
        flush();
        p_ = start_ = o + t;
        started_ = true;
        repeat_ = rel ? 'l' : 'L';
        break;
      case 'Z':
        line(start_);
        current_.closed = true;
        flush();
        repeat_ = 0;
        break;
      case 'L':
        if (!point(t)) return;
        line(o + t);
        break;
      case 'H':
        if (!number(t.x)) return;
        line({o.x + t.x, p_.y});
        break;
      case 'V':
        if (!number(t.y)) return;
        line({p_.x, o.y + t.y});
        break;
      case 'C':
        if (!point(a) || !point(b) || !point(t)) return;
        bezier({p_, o + a, o + b, o + t}, o + b);
        break;
      case 'S':
        if (!point(b) || !point(t)) return;
        bezier({p_, reflected("CS"), o + b, o + t}, o + b);
        break;
      case 'Q':
        if (!point(a) || !point(t)) return;
        bezier({p_, o + a, o + t}, o + a);
        break;
      case 'T': {
        if (!point(t)) return;
        const Vec2 ctrl = reflected("QT");
        bezier({p_, ctrl, o + t}, ctrl);
        break;
      }
      case 'A': {
        double rx = 0, ry = 0, rot = 0;
        bool large = false, sweep = false;
        if (!number(rx) || !number(ry) || !number(rot) || !flag(large) || !flag(sweep) ||
            !point(t)) {
          return;
        }
        const Vec2 end = o + t;
        if (end == p_) break;  // SVG omits an arc to its own start
        if (rx == 0.0 || ry == 0.0) {
          line(end);
          break;
        }
        add(arc(p_, rx, ry, rot, large, sweep, end), end);
        break;
      }
      default:
        break;
    }
    if (C != 'M' && C != 'Z') repeat_ = cmd;
    previous_ = C;
  }

  // The control point S/T start from: the previous one mirrored when the previous command
  // was one of `kinds`, the current point otherwise.
  Vec2 reflected(std::string_view kinds) const {
    return kinds.contains(previous_) ? p_ * 2.0 - last_control_ : p_;
  }

  void line(Vec2 end) {
    if (end != p_) add(math::make_line(p_, end), end);
    last_control_ = end;
  }

  void bezier(std::initializer_list<Vec2> points, Vec2 control) {
    const std::span<const Vec2> pts(points.begin(), points.size());
    if (std::ranges::any_of(pts, [&](Vec2 q) { return q != pts.front(); })) {
      add(math::make_bezier(pts), pts.back());
    } else {
      p_ = pts.back();
    }
    last_control_ = control;
  }

  void add(NurbsCurve2 segment, Vec2 end) {
    current_.segments.push_back(std::move(segment));
    p_ = end;
  }

  void flush() {
    if (!current_.segments.empty()) out_.push_back(std::move(current_));
    current_ = {};
  }

  void skip_space() {
    while (pos_ < d_.size() && is_space(d_[pos_])) ++pos_;
  }
  void skip_separator() {
    skip_space();
    if (pos_ < d_.size() && d_[pos_] == ',') {
      comma_pending_ = true;  // only valid before another argument
      comma_at_ = pos_++;
      skip_space();
    }
  }

  bool starts_number() const {
    if (pos_ >= d_.size()) return false;
    const char c = d_[pos_];
    return is_digit(c) || c == '.' || c == '+' || c == '-';
  }

  // SVG 2 number: sign? (digits ('.' digits?)? | '.' digits) exponent?. The token is
  // scanned here so the grammar is exact, then converted in the C locale.
  bool number(double& out) {
    const std::size_t begin = pos_;
    std::size_t i = pos_;
    const auto digits = [&] {
      const std::size_t from = i;
      while (i < d_.size() && is_digit(d_[i])) ++i;
      return i > from;
    };
    if (i < d_.size() && (d_[i] == '+' || d_[i] == '-')) ++i;
    bool mantissa = digits();
    if (i < d_.size() && d_[i] == '.') {
      ++i;
      mantissa = digits() || mantissa;
    }
    if (!mantissa) return fail("expected a number"), false;
    if (i < d_.size() && (d_[i] == 'e' || d_[i] == 'E')) {
      const std::size_t mark = i++;
      if (i < d_.size() && (d_[i] == '+' || d_[i] == '-')) ++i;
      if (!digits()) i = mark;  // "1e" is the number 1 followed by something else
    }
    static const locale_t c_locale = newlocale(LC_ALL_MASK, "C", nullptr);
    const std::string token(d_.substr(begin, i - begin));
    char* stop = nullptr;
    out = strtod_l(token.c_str(), &stop, c_locale);
    if (stop != token.c_str() + token.size() || !std::isfinite(out)) {
      return fail("number out of range"), false;
    }
    pos_ = i;
    comma_pending_ = false;
    skip_separator();
    return true;
  }

  bool point(Vec2& out) { return number(out.x) && number(out.y); }

  bool flag(bool& out) {
    if (pos_ >= d_.size() || (d_[pos_] != '0' && d_[pos_] != '1')) {
      return fail("expected an arc flag (0 or 1)"), false;
    }
    out = d_[pos_++] == '1';
    comma_pending_ = false;
    skip_separator();
    return true;
  }

  void fail(std::string message) { fail_at(pos_, std::move(message)); }
  void fail_at(std::size_t offset, std::string message) {
    if (!error_) error_ = ParseError{offset, std::move(message)};
  }

  std::string_view d_;
  std::size_t pos_ = 0;
  std::optional<ParseError> error_;
  std::vector<Subpath> out_;
  Subpath current_;
  Vec2 p_{}, start_{}, last_control_{};
  bool started_ = false;
  bool comma_pending_ = false;  // a comma was consumed and needs an argument after it
  std::size_t comma_at_ = 0;
  char repeat_ = 0;    // command implied by bare numbers
  char previous_ = 0;  // previous command (upper case), for S/T reflection
};

}  // namespace

std::expected<std::vector<Subpath>, ParseError> parse_path(std::string_view d) {
  return Parser(d).run();
}

}  // namespace nf::svg
