// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include "nf/ir/parameters.hpp"

#include <algorithm>
#include <array>

namespace nf::ir {
namespace {
struct Field {
  std::optional<Expression> expression;
  double number = 0;
  std::string pointer;
  std::vector<std::size_t> dependencies;
};
using Fields = std::array<std::optional<Field>, 3>;

bool identifier(std::string_view s) {
  const auto letter = [](char c) { return (c >= 'a' && c <= 'z') || c == '_'; };
  return !s.empty() && s.size() <= 64 && letter(s.front()) &&
         std::ranges::all_of(s, [&](char c) { return letter(c) || (c >= '0' && c <= '9'); });
}

class Evaluator {
 public:
  explicit Evaluator(std::span<const Parameter> params) : params_(params), fields_(params.size()) {}
  std::expected<EvaluatedParameters, Errors> run() {
    std::unordered_map<std::string, std::size_t> ids;
    ids.reserve(params_.size());
    for (std::size_t i = 0; i < params_.size(); ++i) {
      if (!identifier(params_[i].name))
        fail("schema", "invalid parameter ID", at(i, "name"));
      else if (!ids.emplace(params_[i].name, i).second)
        fail("schema", "duplicate parameter ID '" + params_[i].name + "'", at(i, "name"));
    }
    if (!errors_.empty()) return std::unexpected(std::move(errors_));
    std::size_t work = 0, source_budget = kMaxDocumentBytes;
    constexpr std::string_view keys[]{"value", "min", "max"};
    for (std::size_t i = 0; i < params_.size(); ++i) {
      const auto& p = params_[i];
      const json::Value* sources[]{&p.value, p.min ? &*p.min : nullptr, p.max ? &*p.max : nullptr};
      for (std::size_t j = 0; j < 3; ++j) {
        if (!sources[j]) continue;
        Field field{{}, 0, at(i, keys[j]), {}};
        if (const auto* n = sources[j]->if_number())
          field.number = *n;
        else if (const auto* text = sources[j]->if_string()) {
          if (text->size() > source_budget) {
            fail("bounds", "parameter source size limit exceeded", field.pointer);
            return std::unexpected(std::move(errors_));
          }
          source_budget -= text->size();
          auto expression = Expression::parse(*text);
          if (!expression) {
            record(expression.error(), field.pointer);
            continue;
          }
          field.expression = std::move(*expression);
        } else {
          fail("schema", "expected a number or expression string", field.pointer);
          continue;
        }
        work += field.expression ? field.expression->instruction_count() : 1;
        if (work > 1000000) {
          fail("bounds", "parameter compilation work limit exceeded", field.pointer);
          return std::unexpected(std::move(errors_));
        }
        if (field.expression)
          for (const auto& symbol : field.expression->references()) {
            if (const auto found = ids.find(symbol.name); found != ids.end())
              field.dependencies.push_back(found->second);
            else if (symbol.name != "pi" && symbol.name != "e")
              record({"unknown_id", "unknown parameter '" + symbol.name + "'", "", symbol.offset},
                     field.pointer);
          }
        fields_[i][j] = std::move(field);
      }
    }
    if (!errors_.empty()) return std::unexpected(std::move(errors_));
    const auto order = topological_order();
    if (!errors_.empty()) return std::unexpected(std::move(errors_));
    EvaluatedParameters result;
    result.values.reserve(params_.size());
    result.order.reserve(params_.size());
    for (const auto i : order) {
      const auto& field = *fields_[i][0];
      result.order.push_back(params_[i].name);
      auto& dependencies = result.dependencies[params_[i].name];
      for (const auto d : field.dependencies) dependencies.push_back(params_[d].name);
      if (ready(field, result.values))
        if (const auto q = evaluate(field, result.values))
          result.values.emplace(params_[i].name, *q);
    }
    for (std::size_t i = 0; i < params_.size(); ++i) check_bounds(i, result.values);
    if (!errors_.empty()) return std::unexpected(std::move(errors_));
    return result;
  }

 private:
  static std::string at(std::size_t i, std::string_view key) {
    return "/params/" + std::to_string(i) + "/" + std::string(key);
  }
  void record(Error error, const std::string& pointer) {
    error.pointer = pointer;
    if (errors_.size() < 128) errors_.push_back(std::move(error));
  }
  void fail(std::string code, std::string message, const std::string& pointer) {
    record({std::move(code), std::move(message), "", std::nullopt}, pointer);
  }
  std::vector<std::size_t> topological_order() {
    struct Frame {
      std::size_t id, next = 0;
    };
    std::vector<Frame> stack;
    std::vector<std::size_t> positions(params_.size()), order;
    std::vector<char> state(params_.size(), 0);
    for (std::size_t root = 0; root < params_.size(); ++root) {
      if (state[root] != 0) continue;
      state[root] = 1;
      positions[root] = 0;
      stack.push_back({root});
      while (!stack.empty()) {
        auto& frame = stack.back();
        const auto& dependencies = fields_[frame.id][0]->dependencies;
        if (frame.next == dependencies.size()) {
          state[frame.id] = 2;
          order.push_back(frame.id);
          stack.pop_back();
          continue;
        }
        const auto d = dependencies[frame.next++];
        if (state[d] == 2) continue;
        if (state[d] == 1) {
          if (errors_.size() < 128) {
            std::string message = "parameter cycle: ";
            const auto end = std::min(stack.size(), positions[d] + 64);
            for (auto k = positions[d]; k < end; ++k) message += params_[stack[k].id].name + " -> ";
            if (end < stack.size()) message += "... -> ";
            fail("cycle", message + params_[d].name, at(frame.id, "value"));
          }
          continue;
        }
        positions[d] = stack.size();
        state[d] = 1;
        stack.push_back({d});
      }
    }
    return order;
  }
  bool ready(const Field& field, const ParameterValues& values) const {
    return std::ranges::all_of(field.dependencies,
                               [&](auto i) { return values.contains(params_[i].name); });
  }
  std::optional<Quantity> evaluate(const Field& field, const ParameterValues& values) {
    if (!field.expression) return Quantity{field.number, kScalar};
    const auto result = field.expression->evaluate(values);
    if (result) return *result;
    record(result.error(), field.pointer);
    return std::nullopt;
  }
  void check_bounds(std::size_t i, const ParameterValues& values) {
    const auto value = values.find(params_[i].name);
    if (value == values.end()) return;
    std::array<std::optional<Quantity>, 2> bounds;
    for (std::size_t j = 0; j < 2; ++j) {
      const auto& field = fields_[i][j + 1];
      if (!field || !ready(*field, values)) continue;
      const auto bound = evaluate(*field, values);
      if (!bound) continue;
      if (bound->dimension != value->second.dimension)
        fail("unit", "bound and parameter must have the same dimension", field->pointer);
      else
        bounds[j] = *bound;
    }
    if (bounds[0] && bounds[1] && bounds[0]->value > bounds[1]->value)
      fail("bounds", "maximum is below minimum", at(i, "max"));
    else if ((bounds[0] && value->second.value < bounds[0]->value) ||
             (bounds[1] && value->second.value > bounds[1]->value))
      fail("bounds", "parameter value is outside its bounds", at(i, "value"));
  }
  std::span<const Parameter> params_;
  std::vector<Fields> fields_;
  Errors errors_;
};
}  // namespace

std::expected<EvaluatedParameters, Errors> evaluate_parameters(std::span<const Parameter> params) {
  if (params.size() > 65536)
    return std::unexpected(
        Errors{{"bounds", "parameter count limit exceeded", "/params", std::nullopt}});
  return Evaluator(params).run();
}
}  // namespace nf::ir
