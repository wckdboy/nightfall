// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_adapters.hpp>
#include <catch2/generators/catch_generators_random.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <limits>

#include "nf/math/vec.hpp"

using namespace nf::math;
using Catch::Matchers::WithinAbs;

namespace {

// Components up to 1 m keep products well inside double precision at 1e-6 mm.
constexpr double kRange = 1000.0;

Vec3 random_vec3() {
  return {GENERATE(take(8, random(-kRange, kRange))), GENERATE(take(4, random(-kRange, kRange))),
          GENERATE(take(4, random(-kRange, kRange)))};
}

}  // namespace

TEST_CASE("Vec3 arithmetic is component-wise", "[vec]") {
  constexpr Vec3 a{1.0, 2.0, 3.0};
  constexpr Vec3 b{-4.0, 0.5, 2.0};
  STATIC_REQUIRE(a + b == Vec3{-3.0, 2.5, 5.0});
  STATIC_REQUIRE(a - b == Vec3{5.0, 1.5, 1.0});
  STATIC_REQUIRE(-a == Vec3{-1.0, -2.0, -3.0});
  STATIC_REQUIRE(a * 2.0 == Vec3{2.0, 4.0, 6.0});
  STATIC_REQUIRE(2.0 * a == a * 2.0);
  STATIC_REQUIRE(a / 2.0 == Vec3{0.5, 1.0, 1.5});
  STATIC_REQUIRE(dot(a, b) == 3.0);
}

TEST_CASE("cross of the basis vectors is right-handed", "[vec]") {
  constexpr Vec3 x{1, 0, 0}, y{0, 1, 0}, z{0, 0, 1};
  STATIC_REQUIRE(cross(x, y) == z);
  STATIC_REQUIRE(cross(y, z) == x);
  STATIC_REQUIRE(cross(z, x) == y);
  STATIC_REQUIRE(cross(Vec2{1, 0}, Vec2{0, 1}) == 1.0);
}

TEST_CASE("cross product is perpendicular to both inputs", "[vec][property]") {
  const Vec3 a = random_vec3();
  const Vec3 b{a.z * 0.5 + 3.0, a.x - 7.0, a.y * 0.25};
  const Vec3 c = cross(a, b);
  // Rounding error in v·(a×b) scales with |a||b||v|.
  const double scale = length(a) * length(b);
  CHECK_THAT(dot(c, a), WithinAbs(0.0, 1e-12 * scale * length(a) + 1e-12));
  CHECK_THAT(dot(c, b), WithinAbs(0.0, 1e-12 * scale * length(b) + 1e-12));
}

TEST_CASE("normalized vectors have unit length", "[vec][property]") {
  const Vec3 a = random_vec3();
  const auto unit = normalized(a);
  REQUIRE(unit.has_value());
  CHECK_THAT(length(*unit), WithinAbs(1.0, 1e-12));
  CHECK(near(*unit * length(a), a, Tolerance{.linear = 1e-9}));
}

TEST_CASE("vectors shorter than the linear tolerance have no direction", "[vec]") {
  const Tolerance tol = Tolerance::standard();
  CHECK_FALSE(normalized(Vec3{}).has_value());
  CHECK_FALSE(normalized(Vec3{tol.linear * 0.5, 0, 0}).has_value());
  CHECK_FALSE(normalized(Vec2{0, tol.linear}).has_value());
  CHECK(normalized(Vec2{0, tol.linear * 2}).has_value());
}

TEST_CASE("near uses the linear tolerance as a distance", "[vec]") {
  const Tolerance tol{.linear = 0.01};
  CHECK(near(Vec3{0, 0, 0}, Vec3{0.006, 0.0079, 0}, tol));        // distance ≈ 0.00992
  CHECK_FALSE(near(Vec3{0, 0, 0}, Vec3{0.006, 0.0081, 0}, tol));  // distance ≈ 0.01008
  CHECK(near(Vec2{1, 1}, Vec2{1, 1.005}, tol));
}

TEST_CASE("normalized survives lengths that overflow a double", "[vec]") {
  const auto unit3 = normalized(Vec3{1e308, 1e308, 0});
  REQUIRE(unit3.has_value());
  CHECK_THAT(unit3->x, WithinAbs(1.0 / std::sqrt(2.0), 1e-15));
  CHECK_THAT(unit3->y, WithinAbs(1.0 / std::sqrt(2.0), 1e-15));
  CHECK(unit3->z == 0.0);
  const auto unit2 = normalized(Vec2{-1.5e308, 1.5e308});
  REQUIRE(unit2.has_value());
  CHECK_THAT(length(*unit2), WithinAbs(1.0, 1e-15));
}

TEST_CASE("normalized rejects NaN and infinite components", "[vec]") {
  const double inf = std::numeric_limits<double>::infinity();
  const double nan = std::numeric_limits<double>::quiet_NaN();
  CHECK_FALSE(normalized(Vec3{nan, 0, 0}).has_value());
  CHECK_FALSE(normalized(Vec3{inf, 1, 0}).has_value());
  CHECK_FALSE(normalized(Vec2{0, -inf}).has_value());
}
