// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_adapters.hpp>
#include <catch2/generators/catch_generators_random.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <string>
#include <vector>

#include "nf/math/constructors.hpp"
#include "nf/math/refine.hpp"
#include "nf/svg/export.hpp"

using nf::math::KnotVector;
using nf::math::NurbsCurve2;
using nf::math::Vec2;
using namespace nf::svg;

namespace {

std::vector<Subpath> parse(std::string_view d) {
  auto r = parse_path(d);
  INFO(d << " -> " << (r ? "" : r.error().message));
  REQUIRE(r.has_value());
  return std::move(*r);
}

double dist(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

// Distance from q to the curve: dense sampling, then ternary search around the best sample.
double distance_to(const NurbsCurve2& c, Vec2 q) {
  const double a = c.domain_start(), b = c.domain_end();
  constexpr int kSamples = 400;
  int best = 0;
  double best_d = dist(c.point(a), q);
  for (int i = 1; i <= kSamples; ++i) {
    const double d = dist(c.point(a + (b - a) * i / kSamples), q);
    if (d < best_d) best_d = d, best = i;
  }
  double lo = a + (b - a) * std::max(0, best - 1) / kSamples;
  double hi = a + (b - a) * std::min(kSamples, best + 1) / kSamples;
  for (int it = 0; it < 100; ++it) {
    const double m1 = lo + (hi - lo) / 3, m2 = hi - (hi - lo) / 3;
    if (dist(c.point(m1), q) < dist(c.point(m2), q)) {
      hi = m2;
    } else {
      lo = m1;
    }
  }
  return std::min(best_d, dist(c.point((lo + hi) / 2), q));
}

double distance_to(const std::vector<NurbsCurve2>& cs, Vec2 q) {
  double d = INFINITY;
  for (const auto& c : cs) d = std::min(d, distance_to(c, q));
  return d;
}

// Largest distance from samples of `from` to the curves `to`.
double one_sided(const std::vector<NurbsCurve2>& from, const std::vector<NurbsCurve2>& to) {
  double worst = 0.0;
  for (const auto& c : from) {
    for (int i = 0; i <= 32; ++i) {
      const double u = c.domain_start() + (c.domain_end() - c.domain_start()) * i / 32.0;
      worst = std::max(worst, distance_to(to, c.point(u)));
    }
  }
  return worst;
}

std::vector<NurbsCurve2> segments(const std::vector<Subpath>& paths) {
  std::vector<NurbsCurve2> out;
  for (const auto& sp : paths) out.insert(out.end(), sp.segments.begin(), sp.segments.end());
  return out;
}

NurbsCurve2 quadratic(Vec2 a, Vec2 b, Vec2 c, std::vector<double> w) {
  return NurbsCurve2::make(KnotVector::make({0, 0, 0, 1, 1, 1}, 2).value(), {a, b, c}, std::move(w))
      .value();
}

}  // namespace

TEST_CASE("lines and Béziers round-trip bit for bit", "[svg][export]") {
  const auto p1 = parse("M0 0 L10 0 Q12 3 15 0 C16 1 18 1 20 0 L20 10 Z M5 5 l1 1");
  const Exported e = write_path(p1);
  CHECK(e.text == "M0 0L10 0Q12 3 15 0C16 1 18 1 20 0L20 10ZM5 5L6 6");
  CHECK(e.max_error == 0.0);
  const auto p2 = parse(e.text);
  REQUIRE(p2.size() == p1.size());
  for (std::size_t i = 0; i < p1.size(); ++i) {
    CHECK(p2[i].closed == p1[i].closed);
    REQUIRE(p2[i].segments.size() == p1[i].segments.size());
    for (std::size_t j = 0; j < p1[i].segments.size(); ++j) {
      const auto& a = p1[i].segments[j];
      const auto& b = p2[i].segments[j];
      CHECK(std::ranges::equal(a.points(), b.points()));
      CHECK(std::ranges::equal(a.weights(), b.weights()));
    }
  }
  CHECK(write_path(p2).text == e.text);
}

TEST_CASE("awkward numbers are written to read back exactly", "[svg][export]") {
  const std::vector<Vec2> pts{{0.1, -1e-300}, {1.0 / 3.0, 2.5e17}, {-0.0, -123456.789}};
  const Subpath sp{{nf::math::make_polyline(pts)}, false};
  const Exported e = write_path(std::span(&sp, 1));
  const auto back = parse(e.text);
  REQUIRE(back.size() == 1);
  REQUIRE(back[0].segments.size() == 2);
  CHECK(back[0].segments[0].points().front() == Vec2{0.1, -1e-300});
  CHECK(back[0].segments[0].points().back() == Vec2{1.0 / 3.0, 2.5e17});
  CHECK(back[0].segments[1].points().back() == Vec2{0.0, -123456.789});
  CHECK(e.text.find("-0 ") == std::string::npos);
}

TEST_CASE("elliptical arcs are written as A and stay exact", "[svg][export][property]") {
  const double rx = GENERATE(3.0, 7.5);
  const double ry = GENERATE(2.0, 7.5);
  const double rot = GENERATE(0.0, 30.0, -110.0);
  const int large = GENERATE(0, 1);
  const int sweep = GENERATE(0, 1);
  const double t0 = GENERATE(take(3, random(0.0, 6.28)));
  const double phi = rot * std::numbers::pi / 180.0;
  const auto on = [&](double t) {
    const double x = rx * std::cos(t), y = ry * std::sin(t);
    return Vec2{2 + x * std::cos(phi) - y * std::sin(phi),
                -1 + x * std::sin(phi) + y * std::cos(phi)};
  };
  const Vec2 a = on(t0), b = on(t0 + 2.1);
  char d[256];
  std::snprintf(d, sizeof d, "M%.17g %.17g A%.17g %.17g %.17g %d %d %.17g %.17g", a.x, a.y, rx, ry,
                rot, large, sweep, b.x, b.y);
  const auto p1 = parse(d);
  const Exported e = write_path(p1);
  CHECK(e.max_error == 0.0);
  CHECK(e.text.find('C') == std::string::npos);
  CHECK(std::ranges::count(e.text, 'A') ==
        static_cast<long>(nf::math::decompose(p1[0].segments[0]).size()));
  const auto p2 = parse(e.text);
  CHECK(segments(p2).front().points().front() == a);
  CHECK(segments(p2).back().points().back() == b);
  const double scale = std::max(rx, ry);
  CHECK(one_sided(segments(p2), segments(p1)) <= 1e-12 * scale);
  CHECK(one_sided(segments(p1), segments(p2)) <= 1e-12 * scale);
}

TEST_CASE("a circle is four arcs and a Z", "[svg][export]") {
  const Subpath sp{{nf::math::make_circle(Vec2{1, 2}, 3.0)}, true};
  const Exported e = write_path(std::span(&sp, 1));
  CHECK(std::ranges::count(e.text, 'A') == 4);
  CHECK(e.text.back() == 'Z');
  const auto back = parse(e.text);
  REQUIRE(back.size() == 1);
  CHECK(back[0].closed);
  for (const auto& c : back[0].segments) {
    for (int i = 0; i <= 32; ++i) {
      const Vec2 q = c.point(c.domain_start() + (c.domain_end() - c.domain_start()) * i / 32.0);
      CHECK(std::abs(dist(q, {1, 2}) - 3.0) <= 3e-12);
    }
  }
}

TEST_CASE("a parabolic rational quadratic is written as Q", "[svg][export]") {
  // Standard-form weight 2 / sqrt(1 · 4) = 1: a parabola, though not in polynomial form.
  const Subpath sp{{quadratic({0, 0}, {5, 8}, {10, 0}, {1, 2, 4})}, false};
  const Exported e = write_path(std::span(&sp, 1));
  CHECK(e.text == "M0 0Q5 8 10 0");
  CHECK(e.max_error == 0.0);
}

TEST_CASE("curves SVG cannot express are approximated within the tolerance", "[svg][export]") {
  const double tol = GENERATE(0.01, 1e-4);
  const auto cubic =
      nf::math::make_bezier(std::vector<Vec2>{{0, 0}, {10, 30}, {40, -20}, {50, 10}});
  const NurbsCurve2 curve = GENERATE_COPY(
      quadratic({0, 0}, {20, 40}, {40, 0}, {1, 3, 1}),   // hyperbolic
      *nf::math::elevate_degree(cubic, 2),               // degree 5
      quadratic({0, 0}, {20, 40}, {40, 0}, {1, 0.2, 1})  // elliptical: exact, for contrast
  );
  const Subpath sp{{curve}, false};
  const Exported e = write_path(std::span(&sp, 1), {.tolerance = tol});
  CHECK(e.max_error <= tol);
  const auto back = segments(parse(e.text));
  const double slack = 1e-9;
  CHECK(one_sided(back, sp.segments) <= std::max(e.max_error, 1e-12) * 1.5 + slack);
  CHECK(one_sided(sp.segments, back) <= std::max(e.max_error, 1e-12) * 1.5 + slack);
}

TEST_CASE("documents are in millimetres with y up", "[svg][export]") {
  const std::vector<Vec2> square{{0, 0}, {10, 0}, {10, 5}, {0, 5}};
  Subpath sp{{}, true};
  for (std::size_t i = 0; i < square.size(); ++i) {
    sp.segments.push_back(nf::math::make_line(square[i], square[(i + 1) % square.size()]));
  }
  const Exported doc = write_document(std::span(&sp, 1));
  CHECK(doc.text.find(R"(width="10mm" height="5mm" viewBox="0 -5 10 5")") != std::string::npos);
  CHECK(doc.text.find(R"(d="M0 0L10 0L10-5L0-5Z")") != std::string::npos);
  CHECK(doc.text.starts_with("<?xml"));
}
