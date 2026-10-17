// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include "nf/ir/inputs.hpp"

#include "schema.hpp"

namespace nf::ir {
struct FeatureInputs::Program {
  struct Field {
    std::string kind, pointer;
    std::variant<double, Expression> code;
  };
  json::Value inputs;
  std::vector<Field> fields;
};
namespace {
void record(Errors& errors, Error error, const std::string& pointer) {
  error.pointer = pointer;
  if (errors.size() < 128) errors.push_back(std::move(error));
}
// Bound even opaque extensions before copying/serializing a programmatically edited value.
bool bounded(const json::Value& v, std::size_t& budget, int depth = 0) {
  if (depth >= json::kMaxDepth || budget == 0) return false;
  --budget;
  if (const auto* text = v.if_string()) {
    if (text->size() > budget) return false;
    budget -= text->size();
  }
  if (const auto* a = v.if_array())
    for (const auto& item : *a)
      if (!bounded(item, budget, depth + 1)) return false;
  if (const auto* o = v.if_object())
    for (const auto& m : *o) {
      if (m.key.size() > budget) return false;
      budget -= m.key.size();
      if (!bounded(m.value, budget, depth + 1)) return false;
    }
  return true;
}
json::Value materialize(const json::Value& v, const std::string& pointer,
                        const std::unordered_map<std::string, double>& values) {
  if (const auto it = values.find(pointer); it != values.end()) return it->second;
  if (const auto* a = v.if_array()) {
    json::Array result;
    result.reserve(a->size());
    for (std::size_t i = 0; i < a->size(); ++i)
      result.push_back(materialize((*a)[i], pointer + "/" + std::to_string(i), values));
    return result;
  }
  if (const auto* o = v.if_object()) {
    json::Object result;
    for (const auto& m : *o)
      if (!m.key.starts_with("x-"))
        // All non-extension keys in the supported schemas are literal ASCII identifiers.
        result.push_back({m.key, materialize(m.value, pointer + "/" + m.key, values)});
    return result;
  }
  return v;
}
}  // namespace

std::expected<FeatureInputs, Errors> FeatureInputs::compile(const Feature& f) {
  const auto fail = [](std::string code, std::string message, std::string pointer) {
    return std::unexpected(Errors{{std::move(code), std::move(message), std::move(pointer), {}}});
  };
  std::size_t budget = kMaxDocumentBytes;
  if (!bounded(f.inputs, budget))
    return fail("schema", "input size/depth limit exceeded", "/inputs");
  const auto text = json::write(json::Object{{"id", f.id}, {"type", f.type}, {"inputs", f.inputs}});
  if (text.size() > kMaxDocumentBytes) return fail("schema", "feature exceeds 16 MiB", "/inputs");
  auto root = json::parse(text);
  if (!root) return fail("parse", root.error().message, "/inputs");
  auto schema = detail::check_feature(*root);
  if (!schema.errors.empty()) return std::unexpected(std::move(schema.errors));
  if (f.type == "script")
    return fail("kernel", "script requires the Nightfall Script evaluator (#93)", "/type");
  if (f.type == "sketch") {
    const auto& entities = *f.inputs.find("entities")->if_array();
    for (std::size_t i = 0; i < entities.size(); ++i)
      if (*entities[i].find("type")->if_string() == "equation_curve")
        return fail("kernel", "equation_curve parameter binding is not implemented (#92)",
                    "/inputs/entities/" + std::to_string(i) + "/type");
  }
  auto program = std::make_shared<Program>();
  Errors errors;
  budget = 1000000;
  for (const auto& field : schema.fields) {
    if (budget == 0) {
      record(errors, {"bounds", "feature instruction limit exceeded", "", {}}, field.pointer);
      break;
    }
    --budget;
    if (const auto* n = field.value->if_number()) {
      program->fields.push_back({field.kind, field.pointer, *n});
      continue;
    }
    auto code = Expression::parse(*field.value->if_string());
    if (!code) {
      record(errors, code.error(), field.pointer);
      continue;
    }
    if (code->instruction_count() > budget) {
      record(errors, {"bounds", "feature instruction limit exceeded", "", {}}, field.pointer);
      break;
    }
    budget -= code->instruction_count();
    program->fields.push_back({field.kind, field.pointer, std::move(*code)});
  }
  if (!errors.empty()) return std::unexpected(std::move(errors));
  program->inputs = f.inputs;
  return FeatureInputs(std::move(program));
}

std::expected<json::Value, Errors> FeatureInputs::evaluate(
    const Units& units, const ParameterValues& parameters) const {
  Errors errors;
  std::unordered_map<std::string, double> values;
  values.reserve(program_->fields.size());
  for (const auto& field : program_->fields) {
    const auto dimension = field.kind == "length"  ? kLength
                           : field.kind == "angle" ? kAngle
                                                   : kScalar;
    std::expected<double, Error> result;
    if (const auto* n = std::get_if<double>(&field.code))
      result = evaluate_input(*n, dimension, units);
    else {
      const auto q = std::get<Expression>(field.code).evaluate(parameters);
      if (!q)
        result = std::unexpected(q.error());
      else if (q->dimension != dimension)
        result =
            std::unexpected(Error{"unit", "expression dimension does not match the field", "", {}});
      else
        result = q->value;
    }
    if (result && (field.kind == "count" || field.kind == "degree") &&
        (*result < 1 || std::trunc(*result) != *result ||
         *result > (field.kind == "degree" ? 9 : 9007199254740992.0)))
      result = std::unexpected(Error{"bounds", "expected an integer in the field's range", "", {}});
    if (!result)
      record(errors, result.error(), field.pointer);
    else
      values.emplace(field.pointer, *result);
  }
  if (!errors.empty()) return std::unexpected(std::move(errors));
  return materialize(program_->inputs, "/inputs", values);
}
}  // namespace nf::ir
