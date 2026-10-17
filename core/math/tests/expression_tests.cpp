// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <random>
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
  // Intermediate terms may go higher when they cancel (review finding).
  CHECK(parse("t^10 - t^10 + t").polynomial() == std::vector<double>{0, 1});
  CHECK(parse("t^10 * 0").polynomial() == std::vector<double>{0});
  CHECK_FALSE(parse("t^100").polynomial());
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
  // Zero amplitude is a constant; frequencies are normalised positive (review findings).
  CHECK_FALSE(parse("0*cos(t)").sinusoid()->omega.has_value());
  CHECK(parse("0*cos(t) + cos(2*t)").sinusoid()->omega == 2.0);
  const auto neg = parse("cos(t) + sin(-t)").sinusoid();
  REQUIRE(neg.has_value());
  CHECK(neg->omega == 1.0);
  CHECK(neg->c == -1.0);
}

TEST_CASE("malformed expressions are errors", "[expression]") {
  for (const char* bad : {"", "sin(", "foo(t)", "t +", "2t", "0x10", "(1", "1)", "sin t", "t$"}) {
    const auto r = Expression::parse(bad);
    INFO(bad);
    CHECK_FALSE(r.has_value());
  }
}

TEST_CASE("named expressions compile to ordered instructions without losing source locations",
          "[expression]") {
  const auto program = Expression::compile_named("2 * bolt_pitch + 10 mm");
  REQUIRE(program.has_value());
  REQUIRE(program->size() == 5);
  using K = Expression::Instruction::Kind;
  CHECK((*program)[0].kind == K::kNum);
  CHECK((*program)[0].value == 2);
  CHECK((*program)[1].kind == K::kName);
  CHECK((*program)[1].text == "bolt_pitch");
  CHECK((*program)[1].offset == 4);
  CHECK((*program)[2].kind == K::kMul);
  CHECK((*program)[2].offset == 2);
  CHECK((*program)[3].kind == K::kNum);
  CHECK((*program)[3].value == 10);
  CHECK((*program)[3].text == "mm");
  CHECK((*program)[3].offset == 17);
  CHECK((*program)[4].kind == K::kAdd);
  // Bare built-in names stay symbolic so a parameter namespace can resolve them.
  for (const char* name : {"t", "pi", "e", "sin", "width_2", "_pitch"}) {
    const auto p = Expression::compile_named(name);
    REQUIRE(p.has_value());
    REQUIRE(p->size() == 1);
    CHECK(p->front().kind == K::kName);
    CHECK(p->front().text == name);
  }
  const auto trig = Expression::compile_named("sin(90deg)");
  REQUIRE(trig.has_value());
  REQUIRE(trig->size() == 2);
  CHECK(trig->front().text == "deg");
  CHECK(trig->back().kind == K::kFunc);
  CHECK(trig->back().text == "sin");
  CHECK(trig->back().offset == 0);
  CHECK_FALSE(Expression::parse("2 mm"));
  CHECK_FALSE(Expression::parse("bolt_pitch"));
}

TEST_CASE("named instructions preserve the equation expression language", "[expression]") {
  using K = Expression::Instruction::Kind;
  for (const char* text : {"1 + 2*3", "(1 + 2)*3", "2^3^2", "-2^2", "2^-1", "8/2/2", "t*t - 1",
                           "sqrt(abs(-16)) + exp(0) + log(1) + cos(0)", "atan(1)*4",
                           "sin(t) + tan(t) + sinh(t) + cosh(t) + tanh(t)",
                           "asin(t/10) + acos(t/10)", "1.5e2 + pi - e"}) {
    INFO(text);
    const auto program = Expression::compile_named(text);
    REQUIRE(program.has_value());
    for (double t : {0.0, 0.4, 1.7}) {
      std::vector<double> stack;
      for (const auto& op : *program) {
        if (op.kind == K::kNum) {
          stack.push_back(op.value);
          continue;
        }
        if (op.kind == K::kName) {
          stack.push_back(op.text == "t"    ? t
                          : op.text == "pi" ? std::numbers::pi
                                            : std::numbers::e);
          continue;
        }
        REQUIRE_FALSE(stack.empty());
        const double b = stack.back();
        stack.pop_back();
        if (op.kind == K::kNeg || op.kind == K::kFunc) {
          stack.push_back(op.kind == K::kNeg ? -b : Expression::apply_function(op.text, b));
          continue;
        }
        REQUIRE_FALSE(stack.empty());
        double& a = stack.back();
        if (op.kind == K::kAdd) a += b;
        if (op.kind == K::kSub) a -= b;
        if (op.kind == K::kMul) a *= b;
        if (op.kind == K::kDiv) a /= b;
        if (op.kind == K::kPow) a = std::pow(a, b);
      }
      REQUIRE(stack.size() == 1);
      CHECK(std::abs(stack.front() - parse(text)(t)) <= 1e-14);
    }
  }
  CHECK(std::isnan(Expression::apply_function("not_a_function", 1)));
}

TEST_CASE("expression parsing has explicit source, tree and recursion limits", "[expression]") {
  for (const std::string& text : {std::string(65537, ' '), std::string(10000, '-') + "1",
                                  std::string(10000, '(') + "1" + std::string(10000, ')')}) {
    CHECK_FALSE(Expression::parse(text));
    const auto result = Expression::compile_named(text);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().offset <= text.size());
    CHECK(result.error().message.find("limit") != std::string::npos);
  }
  std::string flat = "1";
  for (int i = 0; i < 10000; ++i) flat += "+1";
  CHECK_FALSE(Expression::parse(flat));
  CHECK_FALSE(Expression::compile_named(flat));
  std::string balanced = "1";
  for (int i = 0; i < 12; ++i) balanced = "(" + balanced + "+" + balanced + ")";
  CHECK_FALSE(Expression::parse(balanced));
  CHECK_FALSE(Expression::compile_named(balanced));
  for (const char* bad : {"", "sin(", "unknown(2)", "wall +", "(1", "1)", "wall$", "1e999"}) {
    const auto result = Expression::compile_named(bad);
    INFO(bad);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().offset <= std::string_view(bad).size());
  }
  const auto error = Expression::compile_named("width + )");
  REQUIRE_FALSE(error.has_value());
  CHECK(error.error().offset == 8);
}

TEST_CASE("bounded random expressions never produce a malformed program",
          "[expression][property]") {
  std::mt19937 rng(20261017);
  constexpr std::string_view alphabet = "0123456789.eE_+-*/^() \t\nwalltcosmm";
  for (int trial = 0; trial < 2500; ++trial) {
    std::string source;
    const auto size = rng() % 100;
    for (std::size_t i = 0; i < size; ++i) source += alphabet[rng() % alphabet.size()];
    const auto program = Expression::compile_named(source);
    if (!program) {
      CHECK(program.error().offset <= source.size());
      continue;
    }
    std::size_t stack = 0;
    using K = Expression::Instruction::Kind;
    for (const auto& op : *program) {
      CHECK(op.offset < source.size());
      if (op.kind == K::kNum || op.kind == K::kName)
        ++stack;
      else if (op.kind == K::kNeg || op.kind == K::kFunc)
        REQUIRE(stack >= 1);
      else {
        REQUIRE(stack >= 2);
        --stack;
      }
    }
    CHECK(stack == 1);
  }
}
