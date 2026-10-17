// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_adapters.hpp>
#include <catch2/generators/catch_generators_random.hpp>

#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

#include "nf/svg/path.hpp"

using nf::math::NurbsCurve2;
using nf::math::Vec2;
using nf::svg::parse_path;
using nf::svg::Subpath;

namespace {

std::vector<Subpath> parse(std::string_view d) {
  auto r = parse_path(d);
  INFO(d << " -> " << (r ? "" : r.error().message));
  REQUIRE(r.has_value());
  return std::move(*r);
}

Vec2 mid(const NurbsCurve2& c) { return c.point((c.domain_start() + c.domain_end()) / 2); }
double dist(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

std::vector<Vec2> points(const NurbsCurve2& c) { return {c.points().begin(), c.points().end()}; }

// Every segment starts exactly (bit for bit) at the previous segment's last control point.
void check_connected(const std::vector<Subpath>& paths) {
  for (const auto& sp : paths) {
    for (std::size_t i = 1; i < sp.segments.size(); ++i) {
      CHECK(sp.segments[i].points().front() == sp.segments[i - 1].points().back());
    }
    if (sp.closed)
      CHECK(sp.segments.back().points().back() == sp.segments.front().points().front());
  }
}

}  // namespace

TEST_CASE("lines, implicit repeats and relative commands", "[svg][path]") {
  const auto p = parse("M10,20 L30 20 40,30 h-10 v5 l-5-5 z");
  REQUIRE(p.size() == 1);
  const auto& s = p[0].segments;
  REQUIRE(s.size() == 6);  // L, L, h, v, l, then Z's closing line
  CHECK(points(s[0]) == std::vector<Vec2>{{10, 20}, {30, 20}});
  CHECK(points(s[1]) == std::vector<Vec2>{{30, 20}, {40, 30}});
  CHECK(points(s[2]) == std::vector<Vec2>{{40, 30}, {30, 30}});
  CHECK(points(s[3]) == std::vector<Vec2>{{30, 30}, {30, 35}});
  CHECK(points(s[4]) == std::vector<Vec2>{{30, 35}, {25, 30}});
  CHECK(points(s[5]) == std::vector<Vec2>{{25, 30}, {10, 20}});
  CHECK(p[0].closed);
  for (const auto& seg : s) CHECK(seg.degree() == 1);
  check_connected(p);
}

TEST_CASE("compact number syntax", "[svg][path]") {
  // "-" and a second "." start new numbers; exponents; a leading "+".
  const auto p = parse("M.5.5L-1-2e1 +3E-1,4.");
  REQUIRE(p.size() == 1);
  REQUIRE(p[0].segments.size() == 2);
  CHECK(points(p[0].segments[0]) == std::vector<Vec2>{{0.5, 0.5}, {-1, -20}});
  CHECK(points(p[0].segments[1]) == std::vector<Vec2>{{-1, -20}, {0.3, 4}});
}

TEST_CASE("m after a moveto repeats as l; Z returns to the subpath start", "[svg][path]") {
  const auto p = parse("m1 1 2 0 0 2 z l 5 0 M 0 0 1 1");
  REQUIRE(p.size() == 3);
  CHECK(points(p[0].segments[1]) == std::vector<Vec2>{{3, 1}, {3, 3}});
  CHECK(p[0].closed);
  // After Z the next segment starts at the closed subpath's start.
  CHECK(points(p[1].segments[0]) == std::vector<Vec2>{{1, 1}, {6, 1}});
  CHECK_FALSE(p[1].closed);
  CHECK(points(p[2].segments[0]) == std::vector<Vec2>{{0, 0}, {1, 1}});
}

TEST_CASE("cubic and quadratic Béziers keep their control points", "[svg][path]") {
  const auto p = parse("M0 0 C1 2 3 4 5 6 S9 10 11 12 Q13 14 15 16 T19 20 t1 1");
  REQUIRE(p[0].segments.size() == 5);
  const auto& s = p[0].segments;
  CHECK(points(s[0]) == std::vector<Vec2>{{0, 0}, {1, 2}, {3, 4}, {5, 6}});
  CHECK(points(s[1]) == std::vector<Vec2>{{5, 6}, {7, 8}, {9, 10}, {11, 12}});  // reflects (3,4)
  CHECK(points(s[2]) == std::vector<Vec2>{{11, 12}, {13, 14}, {15, 16}});
  CHECK(points(s[3]) == std::vector<Vec2>{{15, 16}, {17, 18}, {19, 20}});  // reflects (13,14)
  CHECK(points(s[4]) == std::vector<Vec2>{{19, 20}, {21, 22}, {20, 21}});  // reflects (17,18)
  check_connected(p);
}

TEST_CASE("S and T without a matching predecessor start at the current point", "[svg][path]") {
  const auto p = parse("M0 0 L1 0 S2 1 3 0 Q4 1 5 0 S6 1 7 0 C8 1 9 1 10 0 T12 0");
  const auto& s = p[0].segments;
  REQUIRE(s.size() == 6);
  CHECK(points(s[1]).at(1) == Vec2{1, 0});   // S after L
  CHECK(points(s[3]).at(1) == Vec2{5, 0});   // S after Q
  CHECK(points(s[5]).at(1) == Vec2{10, 0});  // T after C
}

TEST_CASE("arcs are exact ellipses through both endpoints", "[svg][path][property]") {
  const double rx = GENERATE(3.0, 7.5);
  const double ry = GENERATE(2.0, 7.5);
  const double rot = GENERATE(0.0, 30.0, -110.0);
  const int large = GENERATE(0, 1);
  const int sweep = GENERATE(0, 1);
  const double phi = rot * std::numbers::pi / 180.0;
  const double t0 = GENERATE(take(4, random(0.0, 6.28)));
  const double t1 = t0 + GENERATE(0.7, 2.1, 3.9);
  // Two points on a known ellipse centred at (2, -1).
  const auto on = [&](double t) {
    const double x = rx * std::cos(t), y = ry * std::sin(t);
    return Vec2{2 + x * std::cos(phi) - y * std::sin(phi),
                -1 + x * std::sin(phi) + y * std::cos(phi)};
  };
  const Vec2 a = on(t0), b = on(t1);
  char d[256];
  std::snprintf(d, sizeof d, "M%.17g %.17g A%.17g %.17g %.17g %d %d %.17g %.17g", a.x, a.y, rx, ry,
                rot, large, sweep, b.x, b.y);
  const auto p = parse(d);
  REQUIRE(p[0].segments.size() == 1);
  const NurbsCurve2& c = p[0].segments[0];
  CHECK(c.points().front() == a);
  CHECK(c.points().back() == b);
  // Every point satisfies the implicit equation of one of the two ellipses through a and b
  // with these radii; measure the arc's own centre from its midpoint chord instead of
  // assuming which one, then check the swept direction and size.
  double sum_cross = 0.0;
  Vec2 prev = a;
  for (int i = 1; i <= 64; ++i) {
    const Vec2 q = c.point(c.domain_start() + (c.domain_end() - c.domain_start()) * i / 64.0);
    sum_cross += prev.x * q.y - prev.y * q.x;  // shoelace: signed area swept
    prev = q;
  }
  // SVG's positive-angle direction is +X toward +Y, so sweep=1 has positive signed area
  // relative to the chord.
  const double chord_cross = b.x * a.y - b.y * a.x;
  const double signed_area = (sum_cross + chord_cross) / 2.0;
  CHECK((signed_area > 0) == (sweep == 1));
  const double full = std::numbers::pi * rx * ry;
  CHECK((std::abs(signed_area) > full / 2) == (large == 1));
  // On the ellipse: undo the rotation about either candidate centre; one must fit.
  const auto fits = [&](Vec2 center) {
    for (int i = 0; i <= 64; ++i) {
      const Vec2 q = c.point(c.domain_start() + (c.domain_end() - c.domain_start()) * i / 64.0);
      const double dx = q.x - center.x, dy = q.y - center.y;
      const double x = dx * std::cos(phi) + dy * std::sin(phi);
      const double y = -dx * std::sin(phi) + dy * std::cos(phi);
      if (std::abs(x * x / (rx * rx) + y * y / (ry * ry) - 1.0) > 1e-12) return false;
    }
    return true;
  };
  // The other candidate centre is the reflection of (2, -1) through the chord's midpoint.
  const Vec2 m = (a + b) / 2.0;
  CHECK((fits({2, -1}) || fits(m * 2.0 - Vec2{2, -1})));
}

TEST_CASE("arc special cases", "[svg][path]") {
  SECTION("semicircle direction follows the sweep flag") {
    CHECK(dist(mid(parse("M0 0 A5 5 0 0 1 10 0")[0].segments[0]), {5, -5}) < 1e-12);
    CHECK(dist(mid(parse("M0 0 A5 5 0 0 0 10 0")[0].segments[0]), {5, 5}) < 1e-12);
  }
  SECTION("radii too small are scaled up to just reach") {
    const auto c = parse("M0 0 A1 1 0 0 1 10 0")[0].segments[0];
    CHECK(dist(mid(c), {5, -5}) < 1e-12);
  }
  SECTION("a zero radius gives a straight line") {
    const auto c = parse("M0 0 A0 5 0 0 1 10 0")[0].segments[0];
    CHECK(c.degree() == 1);
  }
  SECTION("an arc to its own start is omitted") { CHECK(parse("M3 3 A5 5 0 0 1 3 3").empty()); }
  SECTION("compact flags") {
    const auto p = parse("M0 0a5 5 0 1010 0");
    REQUIRE(p[0].segments.size() == 1);
    CHECK(p[0].segments[0].points().back() == Vec2{10, 0});
  }
  SECTION("a huge finite rotation is reduced, not overflowed") {
    const auto p = parse("M0 0 A1 1 1e308 0 0 2 2");
    REQUIRE(p[0].segments.size() == 1);
    CHECK(p[0].segments[0].points().back() == Vec2{2, 2});
  }
  SECTION("huge but representable arcs are computed without overflow") {
    const auto p = parse("M0 0 A1e308 1e308 0 0 1 1e308 0");
    REQUIRE(p[0].segments.size() == 1);
    const NurbsCurve2& c = p[0].segments[0];
    // Sweep 1 from (0,0) to (1e308,0): the centre is below the chord (y-down: "positive
    // angle" runs +X toward +Y), at (5e307, sqrt(3)/2 * 1e308) for the small arc.
    const Vec2 center{5e307, std::sqrt(3.0) / 2.0 * 1e308};
    for (int i = 0; i <= 16; ++i) {
      const Vec2 q = c.point(c.domain_start() + (c.domain_end() - c.domain_start()) * i / 16.0);
      const double r = std::hypot((q.x - center.x) / 1e308, (q.y - center.y) / 1e308);
      CHECK(std::abs(r - 1.0) < 1e-12);
    }
  }
  SECTION("commas between repeated arguments are fine") {
    CHECK(parse("M0 0,1 1,2 0")[0].segments.size() == 2);
  }
  SECTION("negative radii use their absolute value") {
    CHECK(dist(mid(parse("M0 0 A-5 -5 0 0 1 10 0")[0].segments[0]), {5, -5}) < 1e-12);
  }
}

TEST_CASE("zero-length segments and empty subpaths are dropped", "[svg][path]") {
  CHECK(parse("").empty());
  CHECK(parse("M1 1").empty());
  CHECK(parse("M1 1 Z").empty());
  const auto p = parse("M0 0 L0 0 C0 0 0 0 0 0 L1 0 Z M5 5");
  REQUIRE(p.size() == 1);
  REQUIRE(p[0].segments.size() == 2);  // L1 0 and the closing line
}

TEST_CASE("malformed path data is an error with an offset", "[svg][path]") {
  const auto err = [](std::string_view d) {
    const auto r = parse_path(d);
    REQUIRE_FALSE(r.has_value());
    return r.error();
  };
  CHECK(err("L1 1").offset == 1);                               // must start with M
  CHECK(err("M1").offset == 2);                                 // missing y
  CHECK(err("M1 1 X2 2").offset == 5);                          // unknown command
  CHECK(err("M1 1 Z 2 2").offset == 7);                         // numbers after Z don't repeat it
  CHECK(err("M0 0 A1 1 0 2 0 1 1").offset == 12);               // flag must be 0 or 1
  CHECK(err("M0 0 L1e 2").offset == 7);                         // exponent without digits
  CHECK(err("M0 0 L. 2").offset == 6);                          // a lone dot
  CHECK(err("M0 0 L1e999 2").offset == 6);                      // out of range
  CHECK(err("M0,,0").offset == 3);                              // double comma
  CHECK(err("M0 0,").offset == 4);                              // trailing comma
  CHECK(err("M0 0,L1 1").offset == 4);                          // comma before a command
  CHECK(err("M1e308 0 l1e308 0").offset == 9);                  // overflows to infinity
  CHECK(err("M1e308 0 m1e308 0").offset == 9);                  // even with nothing drawn
  CHECK(err("M0 0 A1e308 1e308 0 1 1 1.5e308 0").offset == 5);  // control points overflow
  CHECK_FALSE(err("M1 1 X2 2").message.empty());
}
