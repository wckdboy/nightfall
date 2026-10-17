// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "nf/math/constructors.hpp"
#include "nf/svg/document.hpp"
#include "nf/svg/export.hpp"

using nf::math::NurbsCurve2;
using nf::math::Vec2;
using namespace nf::svg;

namespace {

Drawing read(std::string_view text) {
  auto r = read_document(text);
  INFO((r ? "" : r.error().message));
  REQUIRE(r.has_value());
  return std::move(*r);
}

// A document whose user unit is one millimetre, so model = (x, -y).
std::string mm(std::string_view body) {
  return R"(<svg xmlns="http://www.w3.org/2000/svg" width="100mm" height="100mm" viewBox="0 0 100 100">)" +
         std::string(body) + "</svg>";
}

double dist(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }
bool close_to(Vec2 a, Vec2 b) { return dist(a, b) <= 1e-12 * (1 + std::hypot(b.x, b.y)); }

std::vector<Vec2> ends(const Subpath& sp) {
  std::vector<Vec2> out{sp.segments.front().points().front()};
  for (const auto& c : sp.segments) out.push_back(c.points().back());
  return out;
}

}  // namespace

TEST_CASE("documents written by write_document read back unchanged", "[svg][document]") {
  std::vector<Subpath> model;
  Subpath square{{}, true};
  const std::vector<Vec2> pts{{-3, -2}, {10.25, -2}, {10.25, 7.5}, {-3, 7.5}};
  for (std::size_t i = 0; i < pts.size(); ++i)
    square.segments.push_back(nf::math::make_line(pts[i], pts[(i + 1) % 4]));
  model.push_back(square);
  model.push_back(
      {{nf::math::make_bezier(std::vector<Vec2>{{0, 0}, {1.0 / 3, 5}, {4, -1}, {6, 2}})}, false});
  model.push_back({{nf::math::make_circle(Vec2{20, 30}, 4.5)}, true});

  const Exported doc = write_document(model).value();
  const Drawing back = read(doc.text);
  CHECK(back.warnings.empty());
  REQUIRE(back.paths.size() == 3);
  // Lines and Béziers: bit for bit.
  for (std::size_t s = 0; s < 2; ++s) {
    REQUIRE(back.paths[s].segments.size() == model[s].segments.size());
    CHECK(back.paths[s].closed == model[s].closed);
    for (std::size_t i = 0; i < model[s].segments.size(); ++i) {
      CHECK(std::ranges::equal(back.paths[s].segments[i].points(), model[s].segments[i].points()));
    }
  }
  // The circle: four arcs on the same circle.
  CHECK(back.paths[2].closed);
  for (const auto& c : back.paths[2].segments) {
    for (int i = 0; i <= 16; ++i) {
      const Vec2 q = c.point(c.domain_start() + (c.domain_end() - c.domain_start()) * i / 16.0);
      CHECK(std::abs(dist(q, {20, 30}) - 4.5) <= 1e-12 * 30);
    }
  }
}

TEST_CASE("width, height and viewBox set the scale; y is negated", "[svg][document]") {
  const auto rect = [](std::string_view root) {
    return read(std::string(root) + R"(<rect x="10" y="20" width="30" height="40"/></svg>)");
  };
  // 100 mm over 200 user units: 0.5 mm per unit.
  auto d = rect(R"(<svg width="100mm" height="50mm" viewBox="0 0 200 100">)");
  CHECK(ends(d.paths[0]) == std::vector<Vec2>{{5, -10}, {20, -10}, {20, -30}, {5, -30}, {5, -10}});
  // Other units for the size.
  d = rect(R"(<svg width="10cm" height="5cm" viewBox="0 0 200 100">)");
  CHECK(close_to(ends(d.paths[0])[1], {20, -10}));
  d = rect(R"(<svg width="3.937in" height="1in" viewBox="0 0 200 50">)");
  CHECK(close_to(ends(d.paths[0])[1], {40 * 3.937 * 25.4 / 200, -20 * 3.937 * 25.4 / 200}));
  // No viewBox: a user unit is a CSS pixel.
  d = rect(R"(<svg width="500" height="500">)");
  CHECK(close_to(ends(d.paths[0])[1], {40 * 25.4 / 96, -20 * 25.4 / 96}));
  CHECK(d.warnings.empty());
  // A viewBox but no size: pixels, with a warning.
  d = rect(R"(<svg viewBox="0 0 10 10">)");
  CHECK(close_to(ends(d.paths[0])[1], {40 * 25.4 / 96, -20 * 25.4 / 96}));
  CHECK(d.warnings.size() == 1);
  // The viewBox origin does not move the model origin.
  d = rect(R"(<svg width="100mm" height="100mm" viewBox="-50 -50 100 100">)");
  CHECK(ends(d.paths[0])[0] == Vec2{10, -20});
}

TEST_CASE("preserveAspectRatio chooses the scale", "[svg][document]") {
  const auto corner = [](std::string_view par) {
    const auto d = read(R"(<svg width="100mm" height="50mm" viewBox="0 0 100 100" )" +
                        std::string(par) + R"(><line x1="0" y1="0" x2="10" y2="10"/></svg>)");
    return d.paths[0].segments[0].points().back();
  };
  CHECK(corner("") == Vec2{5, -5});                                           // meet: min
  CHECK(corner(R"(preserveAspectRatio="xMinYMin slice")") == Vec2{10, -10});  // slice: max
  CHECK(corner(R"(preserveAspectRatio="none")") == Vec2{10, -5});             // per axis
}

TEST_CASE("transforms compose and apply to control points", "[svg][document]") {
  const auto end = [](std::string_view transform) {
    const auto d = read(mm(R"(<g transform=")" + std::string(transform) +
                           R"("><line x1="0" y1="0" x2="1" y2="0"/></g>)"));
    return d.paths[0].segments[0].points().back();
  };
  CHECK(close_to(end("translate(10,20) rotate(90) scale(2)"), {10, -22}));
  CHECK(close_to(end("translate(10)"), {11, 0}));
  CHECK(close_to(end("scale(3 4)"), {3, 0}));
  CHECK(close_to(end("rotate(90, 5, 5)"), {10, -1}));
  CHECK(close_to(end("matrix(1 2 3 4 5 6)"), {6, -8}));
  CHECK(close_to(end("skewY(45)"), {1, -1}));
  CHECK(close_to(end("rotate(450)"), {0, -1}));  // angles reduce modulo 360°
  // Nested groups and a transform on the shape itself.
  const auto d =
      read(mm(R"x(<g transform="translate(1 1)"><g transform="scale(2)">)x"
              R"x(<line x1="0" y1="0" x2="1" y2="0" transform="translate(0 3)"/></g></g>)x"));
  CHECK(close_to(d.paths[0].segments[0].points().front(), {1, -7}));
  CHECK(close_to(d.paths[0].segments[0].points().back(), {3, -7}));
}

TEST_CASE("basic shapes", "[svg][document]") {
  const auto d =
      read(mm(R"(<circle cx="10" cy="10" r="5"/>)"
              R"(<ellipse cx="30" cy="10" rx="6" ry="3"/>)"
              R"(<rect x="0" y="20" width="20" height="10" rx="2"/>)"
              R"(<polyline points="0,40 10,40 10,50"/>)"
              R"(<polygon points="20 40 30 40 30 50"/>)"
              R"(<line x1="40" y1="40" x2="50" y2="45"/>)"
              R"(<rect width="0" height="5"/><circle r="0"/>)"));  // disabled: no geometry
  REQUIRE(d.paths.size() == 6);
  const auto on = [](const Subpath& sp, auto&& f) {
    for (const auto& c : sp.segments) {
      for (int i = 0; i <= 16; ++i)
        f(c.point(c.domain_start() + (c.domain_end() - c.domain_start()) * i / 16.0));
    }
  };
  CHECK(d.paths[0].closed);
  on(d.paths[0], [](Vec2 q) { CHECK(std::abs(dist(q, {10, -10}) - 5) <= 1e-12 * 10); });
  on(d.paths[1], [](Vec2 q) {
    CHECK(std::abs(std::pow((q.x - 30) / 6, 2) + std::pow((q.y + 10) / 3, 2) - 1) <= 1e-12);
  });
  CHECK(d.paths[2].segments.size() == 8);  // 4 sides + 4 rounded corners
  on(d.paths[2], [](Vec2 q) {
    CHECK(q.x >= -1e-12);
    CHECK(q.x <= 20 + 1e-12);
    CHECK(q.y <= -20 + 1e-12);
    CHECK(q.y >= -30 - 1e-12);
  });
  CHECK_FALSE(d.paths[3].closed);
  CHECK(ends(d.paths[3]) == std::vector<Vec2>{{0, -40}, {10, -40}, {10, -50}});
  CHECK(d.paths[4].closed);
  CHECK(ends(d.paths[4]) == std::vector<Vec2>{{20, -40}, {30, -40}, {30, -50}, {20, -40}});
  CHECK(ends(d.paths[5]) == std::vector<Vec2>{{40, -40}, {50, -45}});
}

TEST_CASE("unsupported and hidden content is skipped", "[svg][document]") {
  const auto d = read(mm(R"(<defs><path d="M0 0L1 1"/></defs><title>t</title>)"
                         R"(<sodipodi:namedview xmlns:sodipodi="x"/><text x="1">hi</text>)"
                         R"(<image href="a.png"/><use href="#p"/><use href="#q"/>)"
                         R"(<path d="M0 0L1 1" display="none"/><path d="M0 0L2 0"/>)"));
  REQUIRE(d.paths.size() == 1);
  CHECK(d.paths[0].segments[0].points().back() == Vec2{2, 0});
  CHECK(d.warnings.size() == 4);  // namedview, text, image, use (repeats reported once)
}

TEST_CASE("namespaces are resolved from xmlns bindings", "[svg][document]") {
  // A foreign default namespace hides its subtree; any prefix bound to SVG is SVG.
  const auto d = read(mm(R"(<g xmlns="urn:foreign"><path d="M0 0L1 0"/></g>)"
                         R"(<s:path xmlns:s="http://www.w3.org/2000/svg" d="M0 0L2 0"/>)"
                         R"(<svg:path d="M0 0L3 0"/>)"));  // svg: is unbound here
  REQUIRE(d.paths.size() == 1);
  CHECK(d.paths[0].segments[0].points().back() == Vec2{2, 0});
  CHECK(d.warnings.size() == 2);
  // No xmlns at all: read as SVG.
  CHECK(read(R"(<svg><path d="M0 0L1 0"/></svg>)").paths.size() == 1);
  // A root in another namespace is not an SVG document.
  CHECK_FALSE(read_document(R"(<svg xmlns="urn:x"/>)").has_value());
  CHECK(
      read(R"(<svg:svg xmlns:svg="http://www.w3.org/2000/svg"><svg:path d="M0 0L1 0"/></svg:svg>)")
          .paths.size() == 1);
}

TEST_CASE("switch reads only the first qualifying child", "[svg][document]") {
  const auto d =
      read(mm(R"(<switch><path requiredExtensions="urn:x" d="M0 0L1 0"/>)"
              R"(<path systemLanguage="en" d="M0 0L2 0"/><path d="M0 0L3 0"/></switch>)"));
  REQUIRE(d.paths.size() == 1);
  CHECK(d.paths[0].segments[0].points().back() == Vec2{2, 0});
}

TEST_CASE("empty points draw nothing", "[svg][document]") {
  const auto d = read(mm(R"(<polyline points=""/><polygon points="  "/>)"));
  CHECK(d.paths.empty());
}

TEST_CASE("malformed documents are errors at the element", "[svg][document]") {
  const auto err = [](std::string_view text) {
    const auto r = read_document(text);
    REQUIRE_FALSE(r.has_value());
    return r.error();
  };
  CHECK(err("<html/>").offset == 0);
  const std::string prefix = mm("");
  const auto at = prefix.size() - 6;  // where the body starts in mm()
  CHECK(err(mm(R"(<path d="M0 0 L"/>)")).offset == at);
  CHECK(err(mm(R"x(<g transform="spin(3)"/>)x")).offset == at);
  CHECK(err(mm(R"x(<g transform="rotate(1,2)"/>)x")).offset == at);
  CHECK(err(mm(R"(<rect width="50%" height="5"/>)")).offset == at);
  CHECK(err(mm(R"(<polyline points="0 0 L 1 1"/>)")).offset == at);
  CHECK(err(mm(R"(<polyline points="0 0 1"/>)")).offset == at);
  CHECK(err(mm(R"x(<line x2="1e308" transform="scale(1e10)"/>)x")).offset == at);
  // Every point overflowing to the same infinity is still an error, not a collapse.
  CHECK(err(mm(R"x(<line x1="1" y1="1" x2="2" y2="2" transform="scale(1e308) scale(1e308)"/>)x"))
            .offset == at);
  CHECK(err(mm(R"(<path d="M0 0L1 1"/>)").substr(0, 40)).message.size() > 0);  // truncated XML
}
