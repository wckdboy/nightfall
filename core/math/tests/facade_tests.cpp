// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

#include "nf/math/facade.hpp"

using namespace nf::facade;

TEST_CASE("the facade builds, evaluates and queries curves", "[facade]") {
  const Curve2 c = Curve2::circle({1, 2}, 3);
  REQUIRE(c.valid());
  CHECK(c.degree() == 2);
  CHECK(c.rational());
  CHECK(c.knots().size() == c.control_points().size() + 3);
  CHECK(c.weights().size() == c.control_points().size());
  const Point2 p = c.point(c.domain_start() + 0.3 * (c.domain_end() - c.domain_start()));
  CHECK(std::abs(std::hypot(p.x - 1, p.y - 2) - 3) <= 1e-12);
  CHECK(c.polyline(1e-3).size() > 10);
  CHECK(c.polyline(0).empty());
  const Curve2 b = Curve2::bezier({{0, 0}, {1, 2}, {3, 0}});
  REQUIRE(b.valid());
  const Point2 d = b.derivative(0);
  CHECK(d.x == 2);
  CHECK(d.y == 4);
  CHECK(std::abs(b.closest(b.point(0.4)) - 0.4) <= 1e-9);
  const Curve3 s = Curve3::circle({0, 0, 1}, {0, 0, 1}, 2);
  REQUIRE(s.valid());
  CHECK(s.point(s.domain_start()).z == 1);
  const Curve3 n = Curve3::nurbs(1, {0, 0, 1, 1}, {{0, 0, 0}, {1, 1, 1}}, {});
  REQUIRE(n.valid());
  CHECK(n.point(0.5).x == 0.5);
}

TEST_CASE("invalid input gives an invalid curve, never an exception", "[facade]") {
  const double nan = std::numeric_limits<double>::quiet_NaN();
  CHECK_FALSE(Curve2::circle({0, 0}, 0).valid());
  CHECK_FALSE(Curve2::circle({0, 0}, nan).valid());
  CHECK_FALSE(Curve2::arc({0, 0}, 1, 1, 0, 1, 0).valid());
  CHECK_FALSE(Curve2::arc({0, 0}, 0, 1, 0, 0, 1).valid());  // radii must be positive
  CHECK_FALSE(Curve2::arc({0, 0}, 1, -2, 0, 0, 1).valid());
  CHECK_FALSE(Curve2::arc({0, 0}, nan, 1, 0, 0, 1).valid());
  CHECK_FALSE(Curve2::bezier({{0, 0}}).valid());
  CHECK_FALSE(Curve2::line({0, 0}, {nan, 1}).valid());
  CHECK_FALSE(Curve2::nurbs(3, {0, 1}, {{0, 0}}, {}).valid());
  CHECK_FALSE(Curve3::circle({0, 0, 0}, {0, 0, 0}, 1).valid());
  const Curve2 empty;
  CHECK_FALSE(empty.valid());
  CHECK(empty.point(0.5).x == 0);
  CHECK(empty.polyline(0.1).empty());
  CHECK(empty.knots().empty());
}
