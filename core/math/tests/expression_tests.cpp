// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <vector>

#include "nf/math/expression.hpp"

using nf::math::Expression;

namespace {

Expression parse(const char* text) {
  auto r = Expression::parse(text);
  INFO(text << " -> " << (r ? "" : r.error()));
  REQUIRE(r.has_value());
  return std::move(*r);
}

}  // namespace

TEST_CASE("expressions evaluate with the usual precedence", "[expression]") {
  CHECK(parse("1 + 2*3")(0) == 7);
  CHECK(parse("(1 + 2)*3")(0) == 9);
  CHECK(parse("2^3^2")(0) == 512);  // right-associative
  CHECK(parse("-2^2")(0) == -4);
  CHECK(parse("2^-1")(0) == 0.5);
  CHECK(parse("8/2/2")(0) == 2);
  CHECK(parse("t*t - 1")(3) == 8);
  CHECK(parse("pi")(0) == std::numbers::pi);
  CHECK(parse("e")(0) == std::numbers::e);
  CHECK(parse(" 1.5e2 ")(0) == 150);
  CHECK(parse("sqrt(abs(-16)) + exp(0) + log(1) + cos(0)")(0) == 6);
  CHECK(parse("atan(1)*4")(0) == std::numbers::pi);
  CHECK(std::isnan(parse("sqrt(t)")(-1)));
}

TEST_CASE("polynomials are recognised exactly", "[expression]") {
  CHECK(parse("3").polynomial() == std::vector<double>{3});
  CHECK(parse("t").polynomial() == std::vector<double>{0, 1});
  CHECK(parse("(t + 1)^2").polynomial() == std::vector<double>{1, 2, 1});
  CHECK(parse("t*0 + 2").polynomial() == std::vector<double>{2});  // no trailing zeros
  CHECK(parse("t^3/2 - t").polynomial() == std::vector<double>{0, -1, 0, 0.5});
  CHECK(parse("2^3*t").polynomial() == std::vector<double>{0, 8});
  CHECK(parse("sqrt(4)*t").polynomial() == std::vector<double>{0, 2});
  CHECK_FALSE(parse("t^10").polynomial());  // beyond degree 9
  CHECK_FALSE(parse("t^0.5").polynomial());
  CHECK_FALSE(parse("1/t").polynomial());
  CHECK_FALSE(parse("sin(t)").polynomial());
}

TEST_CASE("sinusoids of one frequency are recognised", "[expression]") {
  const auto s = parse("1 + 2*cos(3*t + 0.5) - sin(3*t)/4").sinusoid();
  REQUIRE(s.has_value());
  REQUIRE(s->omega.has_value());
  CHECK(*s->omega == 3);
  for (const double t : {0.0, 0.4, 1.7}) {
    const double expected = 1 + 2 * std::cos(3 * t + 0.5) - std::sin(3 * t) / 4;
    CHECK(std::abs(s->a + s->b * std::cos(3 * t) + s->c * std::sin(3 * t) - expected) <= 1e-14);
  }
  CHECK_FALSE(parse("cos(t) + sin(2*t)").sinusoid());  // two frequencies
  CHECK_FALSE(parse("t*cos(t)").sinusoid());
  CHECK_FALSE(parse("cos(t)*sin(t)").sinusoid());
  CHECK_FALSE(parse("cos(t^2)").sinusoid());
  CHECK(parse("5").sinusoid()->a == 5);
}

TEST_CASE("malformed expressions are errors", "[expression]") {
  for (const char* bad : {"", "sin(", "foo(t)", "t +", "2t", "0x10", "(1", "1)", "sin t", "t$"}) {
    const auto r = Expression::parse(bad);
    INFO(bad);
    CHECK_FALSE(r.has_value());
  }
}
