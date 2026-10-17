// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include "nf/ir/expression.hpp"

#include <cmath>
#include <numbers>
#include <unordered_set>

#include "nf/math/expression.hpp"

namespace nf::ir {
struct Expression::Program {
  math::Expression::Program instructions;
  std::vector<Symbol> symbols;
};

namespace {
bool valid(Dimension d) {
  return d.length >= -64 && d.length <= 64 && d.angle >= -64 && d.angle <= 64;
}
std::optional<Quantity> unit(std::string_view name) {
  constexpr std::pair<std::string_view, Quantity> units[]{
      {"", {1, kScalar}},      {"mm", {1, kLength}},
      {"cm", {10, kLength}},   {"m", {1000, kLength}},
      {"in", {25.4, kLength}}, {"ft", {304.8, kLength}},
      {"rad", {1, kAngle}},    {"deg", {std::numbers::pi / 180, kAngle}}};
  for (const auto& [label, quantity] : units)
    if (label == name) return quantity;
  return std::nullopt;
}
std::optional<Dimension> powered(Dimension d, double exponent) {
  const double length = d.length * exponent, angle = d.angle * exponent;
  if (!std::isfinite(length) || !std::isfinite(angle) || length < -64 || length > 64 ||
      angle < -64 || angle > 64 || std::trunc(length) != length || std::trunc(angle) != angle)
    return std::nullopt;
  return Dimension{static_cast<int>(length), static_cast<int>(angle)};
}
std::unexpected<Error> error(std::string code, std::string message) {
  return std::unexpected(Error{std::move(code), std::move(message), "", std::nullopt});
}
}  // namespace

std::expected<Expression, Error> Expression::parse(std::string_view text) {
  auto code = math::Expression::compile_named(text);
  if (!code) return std::unexpected(Error{"parse", code.error().message, "", code.error().offset});
  auto program = std::make_shared<Program>();
  program->instructions = std::move(*code);
  std::unordered_set<std::string> seen;
  for (const auto& op : program->instructions)
    if (op.kind == math::Expression::Instruction::Kind::kName && seen.insert(op.text).second)
      program->symbols.push_back({op.text, op.offset});
  return Expression(std::move(program));
}

std::span<const Expression::Symbol> Expression::references() const { return program_->symbols; }
std::size_t Expression::instruction_count() const { return program_->instructions.size(); }

std::expected<Quantity, Error> Expression::evaluate(const ParameterValues& params) const {
  using K = math::Expression::Instruction::Kind;
  std::vector<Quantity> stack;
  stack.reserve(program_->instructions.size());
  const auto pop = [&] {
    const auto q = stack.back();
    stack.pop_back();
    return q;
  };
  for (const auto& op : program_->instructions) {
    const auto fail = [&](std::string code, std::string message) {
      return std::unexpected(Error{std::move(code), std::move(message), "", op.offset});
    };
    Quantity q;
    if (op.kind == K::kNum) {
      const auto u = unit(op.text);
      if (!u) return fail("unit", "unknown unit '" + op.text + "'");
      q = {op.value * u->value, u->dimension};
    } else if (op.kind == K::kName) {
      if (const auto found = params.find(op.text); found != params.end())
        q = found->second;
      else if (op.text == "pi" || op.text == "e")
        q = {op.text == "pi" ? std::numbers::pi : std::numbers::e, kScalar};
      else
        return fail("unknown_id", "unknown parameter '" + op.text + "'");
    } else {
      const auto b = pop();
      q = b;
      if (op.kind == K::kNeg)
        q.value = -b.value;
      else if (op.kind == K::kFunc) {
        if (op.text == "sqrt") {
          const auto d = powered(b.dimension, 0.5);
          if (!d) return fail("unit", "sqrt requires even dimension powers");
          q.dimension = *d;
        } else if (op.text != "abs") {
          const bool trig = op.text == "sin" || op.text == "cos" || op.text == "tan";
          if (b.dimension != (trig ? kAngle : kScalar))
            return fail("unit", op.text + (trig ? " requires an angle" : " requires a scalar"));
          q.dimension =
              op.text == "asin" || op.text == "acos" || op.text == "atan" ? kAngle : kScalar;
        }
        q.value = math::Expression::apply_function(op.text, b.value);
      } else {
        const auto a = pop();
        if (op.kind == K::kAdd || op.kind == K::kSub) {
          if (a.dimension != b.dimension)
            return fail("unit", "addition/subtraction requires equal dimensions");
          q = {op.kind == K::kAdd ? a.value + b.value : a.value - b.value, a.dimension};
        } else if (op.kind == K::kMul || op.kind == K::kDiv) {
          const int sign = op.kind == K::kMul ? 1 : -1;
          q = {op.kind == K::kMul ? a.value * b.value : a.value / b.value,
               {a.dimension.length + sign * b.dimension.length,
                a.dimension.angle + sign * b.dimension.angle}};
        } else {
          if (b.dimension != kScalar) return fail("unit", "an exponent must be dimensionless");
          const auto d = powered(a.dimension, b.value);
          if (!d) return fail("unit", "power produces unsupported dimension powers");
          q = {std::pow(a.value, b.value), *d};
        }
      }
    }
    if (!valid(q.dimension)) return fail("unit", "dimension power limit exceeded");
    if (!std::isfinite(q.value))
      return fail("bounds", "expression result is outside the finite real domain");
    stack.push_back(q);
  }
  return stack.back();
}

std::expected<Quantity, Error> evaluate_value(const json::Value& value,
                                              const ParameterValues& params) {
  if (const auto* n = value.if_number()) {
    if (!std::isfinite(*n)) return error("bounds", "value must be finite");
    return Quantity{*n, kScalar};
  }
  if (const auto* text = value.if_string()) {
    const auto expr = Expression::parse(*text);
    if (!expr) return std::unexpected(expr.error());
    return expr->evaluate(params);
  }
  return error("schema", "expected a number or expression string");
}

std::expected<double, Error> evaluate_input(const json::Value& value, Dimension required,
                                            const Units& units, const ParameterValues& params) {
  if (!valid(required)) return error("unit", "unsupported field dimension powers");
  auto q = evaluate_value(value, params);
  if (!q) return std::unexpected(q.error());
  if (value.if_number()) {
    const auto length = unit(units.length), angle = unit(units.angle);
    if (!length || length->dimension != kLength || !angle || angle->dimension != kAngle)
      return error("unit", "invalid document units");
    q->value *= std::pow(length->value, required.length) * std::pow(angle->value, required.angle);
    q->dimension = required;
  }
  if (q->dimension != required)
    return error("unit", "expression dimension does not match the field");
  if (!std::isfinite(q->value)) return error("bounds", "converted value must be finite");
  return q->value;
}
}  // namespace nf::ir
