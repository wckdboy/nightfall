// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <random>

#include "nf/ir/expression.hpp"

using namespace nf::ir;

namespace {
Quantity evaluated(std::string_view text, const ParameterValues& params = {}) {
  const auto expression = Expression::parse(text);
  INFO(text);
  REQUIRE(expression.has_value());
  const auto result = expression->evaluate(params);
  INFO((result ? "ok" : result.error().message));
  REQUIRE(result.has_value());
  return *result;
}
void close(Quantity q, double value, Dimension dimension) {
  CHECK(q.dimension == dimension);
  CHECK(std::abs(q.value - value) <= 1e-12 * std::max(1.0, std::abs(value)));
}
void rejects(std::string_view text, std::string_view code, const ParameterValues& params = {}) {
  const auto expression = Expression::parse(text);
  INFO(text);
  REQUIRE(expression.has_value());
  const auto result = expression->evaluate(params);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().code == code);
  CHECK(result.error().pointer.empty());
  REQUIRE(result.error().offset.has_value());
  CHECK(*result.error().offset < text.size());
}
}  // namespace

TEST_CASE("IR expressions: units normalize to millimeters and radians", "[ir][quantity]") {
  for (const auto& [unit, scale] : std::initializer_list<std::pair<const char*, double>>{
           {"mm", 1}, {"cm", 10}, {"m", 1000}, {"in", 25.4}, {"ft", 304.8}}) {
    close(evaluated(std::string("2 ") + unit), 2 * scale, kLength);
  }
  close(evaluated("180deg"), std::numbers::pi, kAngle);
  close(evaluated("2 rad"), 2, kAngle);
  close(evaluated("1 in + 5.4 mm"), 30.8, kLength);
  close(evaluated("2^3^2"), 512, kScalar);
  close(evaluated("-2mm^2"), -4, Dimension{2, 0});
  close(evaluated("2^-1"), 0.5, kScalar);
  close(evaluated("1e-2 m"), 10, kLength);
  close(evaluated("pi + e"), std::numbers::pi + std::numbers::e, kScalar);
  rejects("1 kg", "unit");
}

TEST_CASE("IR expressions: arithmetic preserves and checks dimensions", "[ir][quantity]") {
  const ParameterValues values{{"wall", {3, kLength}}, {"pitch", {20, kLength}}};
  close(evaluated("2*pitch + 10 mm", values), 50, kLength);
  close(evaluated("wall/pitch", values), 0.15, kScalar);
  close(evaluated("wall*pitch", values), 60, Dimension{2, 0});
  close(evaluated("sqrt(wall^2)", values), 3, kLength);
  close(evaluated("(wall^2)^0.5", values), 3, kLength);
  close(evaluated("1/wall", values), 1.0 / 3, Dimension{-1, 0});
  close(evaluated("abs(-wall)", values), 3, kLength);
  for (const char* text : {"wall + 1", "wall + 90 deg", "wall^pitch", "sqrt(wall)", "wall^0.5",
                           "wall^65", "log(wall)", "sin(wall)"})
    rejects(text, "unit", values);
  const auto bad = Expression::parse("wall + 90 deg")->evaluate(values);
  REQUIRE_FALSE(bad.has_value());
  CHECK(bad.error().offset == 5);
}

TEST_CASE("IR expressions: functions check their dimensions and domains", "[ir][quantity]") {
  close(evaluated("sin(90deg) + cos(0rad) + tan(45deg)"), 3, kScalar);
  close(evaluated("asin(1)"), std::numbers::pi / 2, kAngle);
  close(evaluated("acos(0)"), std::numbers::pi / 2, kAngle);
  close(evaluated("atan(1)"), std::numbers::pi / 4, kAngle);
  close(evaluated("sinh(0) + cosh(0) + tanh(0) + exp(0) + log(1)"), 2, kScalar);
  for (const char* text : {"sin(90)", "asin(1mm)", "acos(1deg)", "exp(1mm)", "sinh(1deg)"})
    rejects(text, "unit");
  for (const char* text :
       {"1/0", "sqrt(-1)", "log(0)", "asin(2)", "(-1)^0.5", "exp(1000)", "1e308 m", "1e308*1e308"})
    rejects(text, "bounds");
}

TEST_CASE("IR expressions: names and compiled expressions are reusable", "[ir][quantity]") {
  const auto expression = Expression::parse("wall*2 + wall + allowance");
  REQUIRE(expression.has_value());
  REQUIRE(expression->references().size() == 2);
  CHECK(expression->references()[0].name == "wall");
  CHECK(expression->references()[0].offset == 0);
  CHECK(expression->references()[1].name == "allowance");
  CHECK(expression->instruction_count() == 7);
  auto copy = *expression;
  ParameterValues params{{"wall", {3, kLength}}, {"allowance", {1, kLength}}};
  close(copy.evaluate(params).value(), 10, kLength);
  params["wall"].value = 5;
  close(expression->evaluate(params).value(), 16, kLength);
  rejects("missing + 1", "unknown_id");
  rejects("t", "unknown_id");
  // All valid parameter IDs remain usable. Constants are fallbacks, functions require ().
  close(
      evaluated(
          "pi + e + t + sin",
          {{"pi", {1, kLength}}, {"e", {2, kLength}}, {"t", {3, kLength}}, {"sin", {4, kLength}}}),
      10, kLength);
  rejects("bad", "bounds", {{"bad", {std::numeric_limits<double>::infinity(), kScalar}}});
  rejects("bad", "unit", {{"bad", {1, {std::numeric_limits<int>::max(), 0}}}});
  const auto parse_error = Expression::parse("2 + )");
  REQUIRE_FALSE(parse_error.has_value());
  CHECK(parse_error.error().code == "parse");
  CHECK(parse_error.error().offset == 4);
}

TEST_CASE("IR expressions: document units apply only to numeric feature inputs", "[ir][quantity]") {
  Units units;
  units.length = "in";
  units.angle = "deg";
  close(evaluate_value(2.0).value(), 2, kScalar);
  CHECK(evaluate_input(2.0, kLength, units).value() == 50.8);
  CHECK(std::abs(evaluate_input(90.0, kAngle, units).value() - std::numbers::pi / 2) <= 1e-14);
  CHECK(evaluate_input(2.0, kScalar, units).value() == 2);
  CHECK(evaluate_input("2 mm", kLength, units).value() == 2);
  const ParameterValues values{{"wall", {3, kLength}}};
  CHECK(evaluate_input("wall/2", kLength, units, values).value() == 1.5);
  for (const json::Value& value : {json::Value("2"), json::Value("90 deg"), json::Value(true)}) {
    const auto result = evaluate_input(value, kLength, units);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == (value.if_bool() ? "schema" : "unit"));
  }
  for (const json::Value& value : {json::Value(), json::Value(json::Array{}), json::Value(false)})
    CHECK_FALSE(evaluate_value(value));
  Units large;
  large.length = "m";
  CHECK_FALSE(evaluate_input(1e308, kLength, large));
  units.length = "deg";
  CHECK_FALSE(evaluate_input(1.0, kLength, units));
}

TEST_CASE("IR expressions: generated dimensionally equivalent formulas agree",
          "[ir][quantity][property]") {
  std::mt19937 rng(20261017);
  for (int i = 0; i < 250; ++i) {
    const auto mm = 1 + rng() % 10000;
    const auto count = 1 + rng() % 100;
    const ParameterValues values{{"a", {static_cast<double>(mm), kLength}},
                                 {"n", {static_cast<double>(count), kScalar}}};
    close(evaluated("(a*n)/n", values), mm, kLength);
    close(evaluated("sqrt(a*a)", values), mm, kLength);
    close(evaluated("a/(10mm)", values), static_cast<double>(mm) / 10, kScalar);
  }
}
