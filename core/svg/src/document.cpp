// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include "nf/svg/document.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <exception>
#include <numbers>
#include <optional>
#include <ranges>

#include "nf/svg/xml.hpp"
#include "number.hpp"

namespace nf::svg {

namespace {

using math::NurbsCurve2;
using math::Vec2;

constexpr double kMmPerPx = 25.4 / 96.0;

// x' = a x + c y + e, y' = b x + d y + f (SVG's matrix order).
struct Affine {
  double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
  [[nodiscard]] Vec2 apply(Vec2 p) const { return {a * p.x + c * p.y + e, b * p.x + d * p.y + f}; }
  friend Affine operator*(const Affine& m, const Affine& n) {  // m after n
    return {m.a * n.a + m.c * n.b, m.b * n.a + m.d * n.b,       m.a * n.c + m.c * n.d,
            m.b * n.c + m.d * n.d, m.a * n.e + m.c * n.f + m.e, m.b * n.e + m.d * n.f + m.f};
  }
};

bool is_sep(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ','; }

// Numbers separated by whitespace and/or commas; nullopt if anything else appears.
std::optional<std::vector<double>> number_list(std::string_view s) {
  std::vector<double> out;
  std::size_t i = 0;
  for (;;) {
    while (i < s.size() && is_sep(s[i])) ++i;
    if (i == s.size()) return out;
    const auto r = detail::scan_number(s, i);
    if (r.error != detail::NumberError::kNone) return std::nullopt;
    out.push_back(r.value);
    i = r.end;
  }
}

// A length with an optional absolute unit, in CSS pixels; nullopt for relative units (%,
// em, ex) or malformed text.
std::optional<double> length_px(std::string_view s) {
  while (!s.empty() && is_sep(s.front())) s.remove_prefix(1);
  while (!s.empty() && is_sep(s.back())) s.remove_suffix(1);
  const auto r = detail::scan_number(s, 0);
  if (r.error != detail::NumberError::kNone) return std::nullopt;
  const std::string_view unit = s.substr(r.end);
  constexpr std::array<std::pair<std::string_view, double>, 8> kUnits{{{"", 1.0},
                                                                       {"px", 1.0},
                                                                       {"mm", 96.0 / 25.4},
                                                                       {"cm", 96.0 / 2.54},
                                                                       {"in", 96.0},
                                                                       {"pt", 96.0 / 72.0},
                                                                       {"pc", 16.0},
                                                                       {"Q", 96.0 / 101.6}}};
  for (const auto& [name, px] : kUnits) {
    if (unit == name) return r.value * px;
  }
  return std::nullopt;
}

class Reader {
 public:
  std::expected<Drawing, ParseError> run(std::string_view text) {
    auto root = xml::parse(text);
    if (!root) return std::unexpected(root.error());
    if (root->name != "svg" && root->name != "svg:svg") {
      return std::unexpected(ParseError{root->offset, "the root element is not <svg>"});
    }
    walk(*root, root_scale(*root), true);
    if (error_) return std::unexpected(std::move(*error_));
    return std::move(drawing_);
  }

 private:
  // Model millimetres per root user unit, with y negated.
  Affine root_scale(const xml::Element& root) {
    const auto size = [&](const char* name) -> std::optional<double> {
      const std::string* v = root.attribute(name);
      if (v == nullptr) return std::nullopt;
      const auto px = length_px(*v);
      if (!px || !(*px > 0)) warn(std::string("root ") + name + " \"" + *v + "\" ignored");
      return px && *px > 0 ? std::optional(*px * kMmPerPx) : std::nullopt;
    };
    const std::optional<double> w = size("width"), h = size("height");
    std::optional<std::vector<double>> vb;
    if (const std::string* v = root.attribute("viewBox")) {
      vb = number_list(*v);
      if (!vb || vb->size() != 4 || !((*vb)[2] > 0) || !((*vb)[3] > 0)) {
        warn("viewBox \"" + *v + "\" ignored");
        vb.reset();
      }
    }
    double sx = kMmPerPx, sy = kMmPerPx;
    if (vb && (w || h)) {
      sx = w ? *w / (*vb)[2] : *h / (*vb)[3];
      sy = h ? *h / (*vb)[3] : sx;
      const std::string* par = root.attribute("preserveAspectRatio");
      const bool none = par != nullptr && par->find("none") != std::string::npos;
      if (!w || !h) {
        sy = sx = w ? sx : sy;
      } else if (!none) {
        const bool slice = par != nullptr && par->find("slice") != std::string::npos;
        sx = sy = slice ? std::max(sx, sy) : std::min(sx, sy);
      }
    } else if (vb) {
      warn("no width or height: a user unit is taken as one CSS pixel");
    }
    return Affine{sx, 0, 0, -sy, 0, 0};
  }

  void walk(const xml::Element& e, const Affine& parent, bool root) {
    if (error_) return;
    const std::string_view local = e.local_name();
    if (e.name.find(':') != std::string::npos && !e.name.starts_with("svg:")) {
      return warn("skipped <" + e.name + "> (other namespace)");
    }
    if (const std::string* display = e.attribute("display"); display && *display == "none") return;
    Affine m = parent;
    if (const std::string* t = e.attribute("transform")) {
      const auto parsed = transform(*t);
      if (!parsed) return fail(e, "invalid transform \"" + *t + "\"");
      m = m * *parsed;
    }
    if (local == "svg" &&
        !root) {  // nested viewport: placed at x, y; its own viewBox is not applied
      m = m * Affine{1, 0, 0, 1, user(e, "x"), user(e, "y")};
      if (e.attribute("viewBox") != nullptr) warn("nested <svg> viewBox ignored");
    }
    static constexpr std::array<std::string_view, 4> kGroups{"svg", "g", "a", "switch"};
    static constexpr std::array<std::string_view, 14> kDefinitions{
        "defs",  "symbol",         "clipPath",       "mask",  "pattern", "marker", "metadata",
        "style", "linearGradient", "radialGradient", "title", "desc",    "script", "filter"};
    static constexpr std::array<std::string_view, 7> kShapes{
        "path", "rect", "circle", "ellipse", "line", "polyline", "polygon"};
    if (std::ranges::contains(kGroups, local)) {
      for (const xml::Element& child : e.children) walk(child, m, false);
    } else if (std::ranges::contains(kShapes, local)) {
      try {
        shape(e, local, m);
      } catch (const std::exception&) {
        fail(e, "coordinates out of range");
      }
    } else if (!std::ranges::contains(kDefinitions, local)) {
      warn("skipped <" + e.name + "> (not supported)");
    }
  }

  void shape(const xml::Element& e, std::string_view kind, const Affine& m) {
    std::string d;
    const auto n = [](double x) { return detail::format_number(x); };
    const auto arc = [&](double rx, double ry, double x, double y) {
      return "A" + n(rx) + " " + n(ry) + " 0 0 1 " + n(x) + " " + n(y);
    };
    if (kind == "path") {
      if (const std::string* v = e.attribute("d")) d = *v;
    } else if (kind == "rect") {
      const double x = user(e, "x"), y = user(e, "y"), w = user(e, "width"), h = user(e, "height");
      if (!(w > 0 && h > 0)) return;
      const std::string* rxa = e.attribute("rx");
      const std::string* rya = e.attribute("ry");
      double rx = rxa ? user(e, "rx") : (rya ? user(e, "ry") : 0.0);
      double ry = rya ? user(e, "ry") : rx;
      rx = std::clamp(rx, 0.0, w / 2);
      ry = std::clamp(ry, 0.0, h / 2);
      if (rx > 0 && ry > 0) {
        d = "M" + n(x + rx) + " " + n(y) + "H" + n(x + w - rx) + arc(rx, ry, x + w, y + ry) + "V" +
            n(y + h - ry) + arc(rx, ry, x + w - rx, y + h) + "H" + n(x + rx) +
            arc(rx, ry, x, y + h - ry) + "V" + n(y + ry) + arc(rx, ry, x + rx, y) + "Z";
      } else {
        d = "M" + n(x) + " " + n(y) + "H" + n(x + w) + "V" + n(y + h) + "H" + n(x) + "Z";
      }
    } else if (kind == "circle" || kind == "ellipse") {
      const double cx = user(e, "cx"), cy = user(e, "cy");
      double rx = 0, ry = 0;
      if (kind == "circle") {
        rx = ry = user(e, "r");
      } else {
        const bool has_rx = e.attribute("rx") != nullptr, has_ry = e.attribute("ry") != nullptr;
        rx = has_rx ? user(e, "rx") : user(e, "ry");
        ry = has_ry ? user(e, "ry") : rx;
      }
      if (!(rx > 0 && ry > 0)) return;
      d = "M" + n(cx + rx) + " " + n(cy) + arc(rx, ry, cx, cy + ry) + arc(rx, ry, cx - rx, cy) +
          arc(rx, ry, cx, cy - ry) + arc(rx, ry, cx + rx, cy) + "Z";
    } else if (kind == "line") {
      d = "M" + n(user(e, "x1")) + " " + n(user(e, "y1")) + "L" + n(user(e, "x2")) + " " +
          n(user(e, "y2"));
    } else {  // polyline, polygon
      const std::string* pts = e.attribute("points");
      if (pts == nullptr) return;
      if (!number_list(*pts)) return fail(e, "invalid points");
      d = "M" + *pts + (kind == "polygon" ? "Z" : "");
    }
    if (error_) return;
    auto parsed = parse_path(d);
    if (!parsed) return fail(e, std::string(kind) + ": " + parsed.error().message);
    for (Subpath& sp : *parsed) {
      for (NurbsCurve2& c : sp.segments) {
        std::vector<Vec2> points;
        for (const Vec2 p : c.points()) points.push_back(m.apply(p));
        c = NurbsCurve2::make(c.knots(), std::move(points),
                              c.rational()
                                  ? std::vector<double>(c.weights().begin(), c.weights().end())
                                  : std::vector<double>{})
                .value();
      }
      drawing_.paths.push_back(std::move(sp));
    }
  }

  // A coordinate or length attribute in user units; absent means 0.
  double user(const xml::Element& e, const char* name) {
    const std::string* v = e.attribute(name);
    if (v == nullptr) return 0.0;
    const auto px = length_px(*v);
    if (!px) {
      fail(e, std::string(name) + " \"" + *v + "\" is not an absolute length");
      return 0.0;
    }
    return *px;
  }

  // SVG transform list: matrix, translate, scale, rotate, skewX, skewY (angles in degrees).
  static std::optional<Affine> transform(std::string_view s) {
    Affine m;
    std::size_t i = 0;
    for (;;) {
      while (i < s.size() && is_sep(s[i])) ++i;
      if (i == s.size()) return m;
      const std::size_t name_start = i;
      while (i < s.size() && std::isalpha(static_cast<unsigned char>(s[i]))) ++i;
      const std::string_view name = s.substr(name_start, i - name_start);
      while (i < s.size() && is_sep(s[i]) && s[i] != ',') ++i;
      if (i == s.size() || s[i] != '(') return std::nullopt;
      const std::size_t close = s.find(')', i);
      if (close == std::string_view::npos) return std::nullopt;
      const auto args = number_list(s.substr(i + 1, close - i - 1));
      i = close + 1;
      if (!args) return std::nullopt;
      const std::vector<double>& v = *args;
      const auto rad = [](double deg) { return std::fmod(deg, 360.0) * std::numbers::pi / 180.0; };
      Affine f;
      if (name == "matrix" && v.size() == 6) {
        f = {v[0], v[1], v[2], v[3], v[4], v[5]};
      } else if (name == "translate" && (v.size() == 1 || v.size() == 2)) {
        f = {1, 0, 0, 1, v[0], v.size() == 2 ? v[1] : 0.0};
      } else if (name == "scale" && (v.size() == 1 || v.size() == 2)) {
        f = {v[0], 0, 0, v.size() == 2 ? v[1] : v[0], 0, 0};
      } else if (name == "rotate" && (v.size() == 1 || v.size() == 3)) {
        const double c = std::cos(rad(v[0])), s_ = std::sin(rad(v[0]));
        f = {c, s_, -s_, c, 0, 0};
        if (v.size() == 3)
          f = Affine{1, 0, 0, 1, v[1], v[2]} * f * Affine{1, 0, 0, 1, -v[1], -v[2]};
      } else if (name == "skewX" && v.size() == 1) {
        f = {1, 0, std::tan(rad(v[0])), 1, 0, 0};
      } else if (name == "skewY" && v.size() == 1) {
        f = {1, std::tan(rad(v[0])), 0, 1, 0, 0};
      } else {
        return std::nullopt;
      }
      m = m * f;
    }
  }

  void warn(std::string message) {
    if (!std::ranges::contains(drawing_.warnings, message))
      drawing_.warnings.push_back(std::move(message));
  }
  void fail(const xml::Element& e, std::string message) {
    if (!error_) error_ = ParseError{e.offset, "<" + e.name + "> " + std::move(message)};
  }

  Drawing drawing_;
  std::optional<ParseError> error_;
};

}  // namespace

std::expected<Drawing, ParseError> read_document(std::string_view svg) { return Reader().run(svg); }

}  // namespace nf::svg
