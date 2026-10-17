// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <sstream>
#include <string>
#include <vector>

#include "nf/math/constructors.hpp"
#include "nf/math/refine.hpp"
#include "nf/svg/dxf.hpp"

using nf::math::NurbsCurve2;
using nf::math::Vec2;
using namespace nf::svg;

namespace {

// A DXF entity as its group codes (repeated codes keep their order).
struct Entity {
  std::string type;
  std::multimap<int, std::string> codes;
  [[nodiscard]] double num(int code) const { return std::stod(codes.find(code)->second); }
  [[nodiscard]] Vec2 point(int code) const { return {num(code), num(code + 10)}; }
};

// The ENTITIES section, with VERTEX entities attached to their POLYLINE.
std::vector<Entity> entities(const std::string& dxf,
                             std::vector<std::vector<Vec2>>* polylines = nullptr) {
  std::istringstream in(dxf);
  std::vector<std::pair<int, std::string>> pairs;
  std::string code, value;
  while (std::getline(in, code) && std::getline(in, value))
    pairs.emplace_back(std::stoi(code), value);
  std::vector<Entity> out;
  bool in_entities = false;
  for (std::size_t i = 0; i < pairs.size(); ++i) {
    if (pairs[i] == std::pair{2, std::string("ENTITIES")}) in_entities = true;
    if (!in_entities || pairs[i].first != 0) continue;
    if (pairs[i].second == "ENDSEC") break;
    Entity e{pairs[i].second, {}};
    for (std::size_t j = i + 1; j < pairs.size() && pairs[j].first != 0; ++j)
      e.codes.emplace(pairs[j]);
    if (e.type == "VERTEX" && polylines) polylines->back().push_back(e.point(10));
    if (e.type == "POLYLINE" && polylines) polylines->emplace_back();
    out.push_back(std::move(e));
  }
  return out;
}

double count(const std::vector<Entity>& es, const std::string& type) {
  return static_cast<double>(
      std::ranges::count_if(es, [&](const Entity& e) { return e.type == type; }));
}

double segment_distance(Vec2 p, Vec2 a, Vec2 b) {
  const Vec2 ab = b - a;
  const double t = std::clamp(dot(p - a, ab) / std::max(dot(ab, ab), 1e-300), 0.0, 1.0);
  return length(p - (a + ab * t));
}

}  // namespace

TEST_CASE("lines and the header", "[dxf]") {
  const std::vector<Vec2> pts{{0.1, 0}, {10, 0}, {10, 5.25}, {0, 1.0 / 3}};
  Subpath sp{{}, true};
  for (std::size_t i = 0; i < pts.size(); ++i)
    sp.segments.push_back(nf::math::make_line(pts[i], pts[(i + 1) % 4]));
  const Exported r = write_dxf(std::span(&sp, 1)).value();
  CHECK(r.max_error == 0.0);
  CHECK(r.text.find("$ACADVER\n1\nAC1009") != std::string::npos);
  CHECK(r.text.ends_with("0\nEOF\n"));
  const auto es = entities(r.text);
  REQUIRE(count(es, "LINE") == 4);
  for (std::size_t i = 0; i < 4; ++i) {
    CHECK(es[i].point(10) == pts[i]);  // shortest round-trip numbers: exact
    CHECK(es[i].point(11) == pts[(i + 1) % 4]);
  }
}

TEST_CASE("circles and circular arcs are exact", "[dxf]") {
  const Subpath circle{{nf::math::make_circle(Vec2{1, 2}, 3.0)}, true};
  auto es = entities(write_dxf(std::span(&circle, 1)).value().text);
  REQUIRE(es.size() == 1);
  CHECK(es[0].type == "CIRCLE");
  CHECK(length(es[0].point(10) - Vec2{1, 2}) <= 1e-12);
  CHECK(std::abs(es[0].num(40) - 3.0) <= 1e-12);
  // A clockwise 200° arc becomes counter-clockwise ARCs between the same end points.
  const NurbsCurve2 cw = nf::math::reverse(
      nf::math::make_arc(Vec2{0, 0}, 2, 2, 0, 0.3, 0.3 + 200 * std::numbers::pi / 180));
  const Subpath arc{{cw}, false};
  const Exported r = write_dxf(std::span(&arc, 1)).value();
  CHECK(r.max_error <= 1e-12);  // the circles' rounding-level residual
  es = entities(r.text);
  REQUIRE(count(es, "ARC") == 3);  // ≤ 90° per span
  double total = 0;
  for (const Entity& e : es) {
    CHECK(length(e.point(10)) <= 1e-12);
    CHECK(std::abs(e.num(40) - 2) <= 1e-12);
    total += e.num(51) - e.num(50);
  }
  CHECK(std::abs(total - 200) <= 1e-9);
}

TEST_CASE("ellipses and Béziers become polylines within the tolerance", "[dxf]") {
  const double tol = 1e-3;
  const NurbsCurve2 ellipse = nf::math::make_arc(Vec2{0, 0}, 5, 2, 0.4, 0, 2.5);
  const NurbsCurve2 cubic =
      nf::math::make_bezier(std::vector<Vec2>{{0, 0}, {3, 8}, {7, -4}, {10, 2}});
  for (const NurbsCurve2* c : {&ellipse, &cubic}) {
    const Subpath sp{{*c}, false};
    const Exported r = write_dxf(std::span(&sp, 1), {.tolerance = tol}).value();
    CHECK(r.max_error == tol);
    std::vector<std::vector<Vec2>> pls;
    const auto es = entities(r.text, &pls);
    REQUIRE(pls.size() == 1);
    CHECK(pls[0].front() == c->points().front());
    CHECK(pls[0].back() == c->points().back());
    for (int i = 0; i <= 400; ++i) {
      const Vec2 q = c->point(c->domain_start() + (c->domain_end() - c->domain_start()) * i / 400);
      double d = INFINITY;
      for (std::size_t k = 1; k < pls[0].size(); ++k)
        d = std::min(d, segment_distance(q, pls[0][k - 1], pls[0][k]));
      CHECK(d <= tol * (1 + 1e-9));
    }
  }
  CHECK_FALSE(write_dxf({}, {.tolerance = 0}).has_value());
}

TEST_CASE("the reported error is truthful", "[dxf]") {
  // A quarter ellipse within 5e-7 of a circle is not an exact arc at tolerance 1e-9 (review
  // finding): it becomes a polyline.
  const Subpath near{{nf::math::make_arc(Vec2{0, 0}, 1e6, 1e6 + 5e-7, 0, 0, std::numbers::pi / 2)},
                     false};
  const Exported strict = write_dxf(std::span(&near, 1), {.tolerance = 1e-9}).value();
  CHECK(entities(strict.text).front().type == "POLYLINE");
  // At a looser tolerance it may be an arc, and its residual is reported.
  const Exported loose = write_dxf(std::span(&near, 1), {.tolerance = 1e-6}).value();
  CHECK(entities(loose.text).front().type == "ARC");
  CHECK(loose.max_error > 0);
  CHECK(loose.max_error <= 1e-6);
  // A tolerance below the polyline's floor reports the floor (review finding).
  const Subpath big{
      {nf::math::make_bezier(std::vector<Vec2>{{0, 0}, {3e5, 8e5}, {7e5, -4e5}, {1e6, 2e5}})},
      false};
  const Exported floor = write_dxf(std::span(&big, 1), {.tolerance = 1e-9}).value();
  CHECK(floor.max_error >= 1e-12 * 1e6);
}
