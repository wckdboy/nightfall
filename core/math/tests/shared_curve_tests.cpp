// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "nf/math/arrangement.hpp"
#include "nf/math/constructors.hpp"
#include "nf/math/query.hpp"
#include "nf/math/refine.hpp"

#include <algorithm>
#include <numbers>
#include <random>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace nf::math;
using Catch::Matchers::WithinAbs;

namespace {
constexpr double pi = std::numbers::pi;
using Path = std::vector<NurbsCurve2>;
Path closed(const NurbsCurve2& curve) {
  Path path{curve};
  if (curve.points().front() != curve.points().back())
    path.push_back(make_line(curve.points().back(), curve.points().front()));
  return path;
}
Path backward(Path path) {
  std::ranges::reverse(path);
  for (auto& c : path) c = reverse(c);
  return path;
}
std::vector<Region> filled(const std::vector<Path>& paths, FillRule rule) {
  auto result = regions(paths, rule);
  INFO((result ? "ok" : result.error()));
  REQUIRE(result);
  return *result;
}
double area(const std::vector<Region>& regions, double scale = 1) {
  double sum = 0;
  const auto add = [&](const Loop& loop) {
    for (const auto& c : loop.curves) {
      const auto samples = polyline(c, 1e-9 * scale);
      for (std::size_t i = 1; i < samples.size(); ++i)
        sum += cross(samples[i - 1].point, samples[i].point) / 2;
    }
  };
  for (const auto& r : regions) {
    add(r.outer);
    for (const auto& h : r.holes) add(h);
  }
  return sum;
}
NurbsCurve2 mobius(const NurbsCurve2& curve, double factor) {
  auto weights = std::vector<double>{curve.weights().begin(), curve.weights().end()};
  for (std::size_t i = 0; i < weights.size(); ++i) weights[i] *= std::pow(factor, i);
  std::vector<double> knots(weights.size(), 2);
  knots.insert(knots.end(), weights.size(), 9);
  return NurbsCurve2::make(KnotVector::make(knots, curve.degree()).value(),
                           {curve.points().begin(), curve.points().end()}, weights)
      .value();
}
}  // namespace

TEST_CASE("shared curved duplicates obey both fill rules and signed winding", "[shared-curves]") {
  const std::vector<std::pair<NurbsCurve2, double>> cases{
      {make_circle(Vec2{}, 2), 4 * pi},
      {make_arc(Vec2{2, -3}, 3, 2, 0.37, 0, 2 * pi), 6 * pi},
      {make_bezier(std::vector<Vec2>{{0, 0}, {0, 3}, {3, 3}, {3, 0}}), 5.4},
      {make_bezier(std::vector<Vec2>{{0, 0}, {4, 0}, {4, 4}, {0, 0}}), 2.4}};
  for (const auto& [curve, expected] : cases) {
    const auto path = closed(curve);
    const auto twice = filled({path, path}, FillRule::kNonZero);
    REQUIRE(twice.size() == 1);
    CHECK_THAT(area(twice), WithinAbs(expected, 1e-7));
    CHECK(filled({path, path}, FillRule::kEvenOdd).empty());
    CHECK(filled({path, backward(path)}, FillRule::kNonZero).empty());
    CHECK_THAT(area(filled({path, path, backward(path)}, FillRule::kNonZero)),
               WithinAbs(expected, 1e-7));
  }
}

TEST_CASE("partially shared arcs form a notch instead of an overlapping-edge error",
          "[shared-curves]") {
  const auto circle = make_circle(Vec2{}, 5);
  const auto cap = make_arc(Vec2{}, 5, 5, 0, 0.37, 1.2);
  const double removed = 12.5 * (0.83 - std::sin(0.83));
  for (const auto rule : {FillRule::kEvenOdd, FillRule::kNonZero}) {
    const auto result = filled({{circle}, backward(closed(cap))}, rule);
    REQUIRE(result.size() == 1);
    CHECK(result[0].holes.empty());
    CHECK_THAT(area(result), WithinAbs(25 * pi - removed, 2e-7));
  }
  CHECK_THAT(area(filled({{circle}, closed(cap)}, FillRule::kNonZero)), WithinAbs(25 * pi, 2e-7));
}

TEST_CASE("shared polynomial subcurves retain their original knot parameters", "[shared-curves]") {
  const auto roof = make_bezier(std::vector<Vec2>{{0, 0}, {0, 3}, {3, 3}, {3, 0}});
  const auto middle = split(split(roof, 0.2)->second, 0.8)->first;
  const auto result = arrange(std::vector<Path>{closed(roof), closed(middle)});
  INFO((result ? "ok" : result.error()));
  REQUIRE(result);
  std::size_t shared = 0;
  for (std::size_t i = 0; i < result->half_edges.size(); i += 2) {
    const auto& edge = result->half_edges[i];
    if (edge.curve.degree() == 1) continue;
    shared += std::abs(edge.direction) == 2;
    for (const double f : {0.0, 0.17, 0.5, 0.83, 1.0}) {
      const auto u = std::lerp(edge.curve.domain_start(), edge.curve.domain_end(), f);
      CHECK_THAT(length(edge.curve.point(u) - roof.point(u)), WithinAbs(0, 1e-12));
    }
  }
  CHECK(shared == 1);
}

TEST_CASE("shared curves tolerate degree elevation and Mobius parameter changes",
          "[shared-curves]") {
  const auto arc = make_arc(Vec2{}, 4, 2, 0.3, 0.2, 1.3);
  const double expected = 4 * (1.1 - std::sin(1.1));
  for (const double factor : {0.25, 1.0, 4.0}) {
    const auto other = elevate_degree(mobius(arc, factor), 2).value();
    for (const bool reversed : {false, true}) {
      const auto path = reversed ? backward(closed(other)) : closed(other);
      const auto result = filled({closed(arc), path}, FillRule::kNonZero);
      CHECK_THAT(area(result), WithinAbs(reversed ? 0 : expected, 1e-7));
      CHECK(filled({closed(arc), path}, FillRule::kEvenOdd).empty());
    }
  }
}

TEST_CASE("shared circular spans split consistently at third-path crossings", "[shared-curves]") {
  const auto circle = make_circle(Vec2{}, 1);
  Path reparameterized;
  for (const auto& c : decompose(circle)) reparameterized.push_back(mobius(c, 3));
  const auto rectangle = make_polyline(
      std::vector<Vec2>{{-0.4, -1.5}, {0.4, -1.5}, {0.4, 1.5}, {-0.4, 1.5}, {-0.4, -1.5}});
  std::vector<Path> paths{{circle}, reparameterized, {rectangle}};
  for (int order = 0; order < 3; ++order) {
    const auto result = filled(paths, FillRule::kEvenOdd);
    REQUIRE(result.size() == 1);
    CHECK(result[0].holes.empty());
    CHECK_THAT(area(result), WithinAbs(2.4, 1e-8));
    std::rotate(paths.begin(), paths.begin() + 1, paths.end());
  }
}

TEST_CASE("nearby distinct curved spans are not merged", "[shared-curves]") {
  const auto arc = make_arc(Vec2{}, 3, 2, 0, 0, pi / 2);
  for (const double delta : {1e-5, 1e-7}) {
    CAPTURE(delta);
    auto poles = std::vector<Vec2>{arc.points().begin(), arc.points().end()};
    poles[1].x += delta;
    const auto other =
        NurbsCurve2::make(arc.knots(), poles, {arc.weights().begin(), arc.weights().end()}).value();
    const auto result = arrange(std::vector<Path>{closed(arc), closed(other)});
    // An unresolved near-tangency may remain an explicit error, never a false shared edge.
    if (!result) continue;
    std::size_t curved = 0;
    for (const auto& h : result->half_edges)
      if (h.curve.degree() > 1) {
        ++curved;
        CHECK(std::abs(h.direction) == 1);
      }
    CHECK(curved >= 4);
  }
}

TEST_CASE("shared curved boundaries preserve external and internal tangencies", "[shared-curves]") {
  const auto circle = make_circle(Vec2{}, 1);
  const auto tangent_box =
      make_polyline(std::vector<Vec2>{{-1, -3}, {1, -3}, {1, -1}, {-1, -1}, {-1, -3}});
  const auto box_result = filled({{circle}, {circle}, {tangent_box}}, FillRule::kNonZero);
  CHECK(box_result.size() == 2);
  CHECK_THAT(area(box_result), WithinAbs(pi + 4, 1e-7));
  for (const bool internal : {false, true}) {
    const auto tangent = make_circle(Vec2{internal ? 0.5 : 2, 0}, internal ? 0.5 : 1);
    const auto result = filled({{circle}, {circle}, {tangent}}, FillRule::kNonZero);
    CHECK(result.size() == (internal ? 1 : 2));
    CHECK_THAT(area(result), WithinAbs((internal ? 1 : 2) * pi, 1e-7));
    const auto parity = filled({{circle}, {circle}, {tangent}}, FillRule::kEvenOdd);
    CHECK(parity.size() == 1);
    CHECK_THAT(area(parity), WithinAbs((internal ? 0.25 : 1) * pi, 1e-7));
  }
}

TEST_CASE("shared elliptic arc regions match analytic area across placements",
          "[shared-curves][property]") {
  std::mt19937_64 rng(20261019);
  std::uniform_real_distribution<double> angle(-pi, pi), start(0.1, 4.5), sweep(0.2, 1.4);
  for (int trial = 0; trial < 48; ++trial) {
    const double scale = trial % 3 == 0 ? 0.01 : trial % 3 == 1 ? 1 : 100;
    const double rotation = angle(rng), a = start(rng), d = sweep(rng);
    CAPTURE(trial, scale, rotation, a, d);
    const Vec2 center{7 * scale, -3 * scale};
    const auto ellipse = make_arc(center, 3 * scale, 2 * scale, rotation, 0, 2 * pi);
    const auto cap = make_arc(center, 3 * scale, 2 * scale, rotation, a, a + d);
    std::vector<Path> paths{{ellipse}, backward(closed(cap))};
    if (trial % 2) std::ranges::reverse(paths);
    const double expected = 6 * pi - 3 * (d - std::sin(d));
    for (const auto rule : {FillRule::kEvenOdd, FillRule::kNonZero}) {
      const auto result = filled(paths, rule);
      REQUIRE(result.size() == 1);
      CHECK(result[0].holes.empty());
      CHECK_THAT(area(result, scale) / (scale * scale), WithinAbs(expected, 2e-7));
    }
  }
}
