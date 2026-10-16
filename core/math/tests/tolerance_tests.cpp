// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>

#include "nf/math/tolerance.hpp"

using nf::math::near;
using nf::math::Tolerance;

TEST_CASE("standard tolerances are 1e-6 mm and 1e-9 rad", "[tolerance]") {
  constexpr Tolerance tol = Tolerance::standard();
  STATIC_REQUIRE(tol.linear == 1e-6);
  STATIC_REQUIRE(tol.angular == 1e-9);
}

TEST_CASE("near compares with an inclusive absolute bound", "[tolerance]") {
  STATIC_REQUIRE(near(1.0, 1.0, 0.0));
  STATIC_REQUIRE(near(1.0, 1.5, 0.5));
  STATIC_REQUIRE(near(-2.0, -2.25, 0.25));
  STATIC_REQUIRE_FALSE(near(1.0, 1.5000001, 0.5));
}
