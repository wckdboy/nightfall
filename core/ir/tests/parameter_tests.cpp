// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <random>

#include "nf/ir/parameters.hpp"

using namespace nf::ir;

namespace {
Parameter param(std::string name, json::Value value, std::optional<json::Value> min = {},
                std::optional<json::Value> max = {}) {
  Parameter p;
  p.name = std::move(name);
  p.value = std::move(value);
  p.min = std::move(min);
  p.max = std::move(max);
  return p;
}
void has_error(const std::expected<EvaluatedParameters, Errors>& result, std::string_view code,
               std::string_view pointer) {
  REQUIRE_FALSE(result.has_value());
  CHECK(std::ranges::any_of(result.error(), [&](const auto& error) {
    return error.code == code && error.pointer == pointer;
  }));
}
}  // namespace

TEST_CASE("IR parameters: dependencies override declaration order and preserve input",
          "[ir][params]") {
  const std::vector<Parameter> params{param("width", "2 * pitch + 10 mm", "2 * wall", "100 mm"),
                                      param("wall", "2 mm"), param("pitch", "wall * 5"),
                                      param("count", 6.0), param("half", "width / 2")};
  const auto result = evaluate_parameters(params);
  REQUIRE(result.has_value());
  CHECK(result->values.at("width") == Quantity{30, kLength});
  CHECK(result->values.at("half") == Quantity{15, kLength});
  CHECK(result->values.at("count") == Quantity{6, kScalar});
  CHECK(result->dependencies.at("width") == std::vector<std::string>{"pitch"});
  CHECK(result->dependencies.at("pitch") == std::vector<std::string>{"wall"});
  CHECK(result->dependencies.at("wall").empty());
  REQUIRE(result->order.size() == params.size());
  for (const auto& [name, dependencies] : result->dependencies)
    for (const auto& dependency : dependencies)
      CHECK(std::ranges::find(result->order, dependency) < std::ranges::find(result->order, name));
  CHECK(params.front().value == json::Value("2 * pitch + 10 mm"));
  CHECK(params.front().min == json::Value("2 * wall"));
  CHECK(evaluate_parameters({})->values.empty());
}

TEST_CASE("IR parameters: cycles identify the participating names", "[ir][params]") {
  auto result = evaluate_parameters(std::vector{param("wall", "wall * 2")});
  has_error(result, "cycle", "/params/0/value");
  CHECK(result.error().front().message.find("wall -> wall") != std::string::npos);
  result = evaluate_parameters(
      std::vector{param("a", "b"), param("b", "c"), param("c", "a"), param("unrelated", "2 mm")});
  has_error(result, "cycle", "/params/2/value");
  CHECK(result.error().front().message.find("a -> b -> c -> a") != std::string::npos);
  result = evaluate_parameters(
      std::vector{param("a", "b"), param("b", "a"), param("c", "c"), param("dependent", "a")});
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().size() == 2);
}

TEST_CASE("IR parameters: names are checked without confusing constants with dependencies",
          "[ir][params]") {
  has_error(evaluate_parameters(std::vector{param("wall", "missing + 1 mm")}), "unknown_id",
            "/params/0/value");
  const auto missing = evaluate_parameters(std::vector{param("wall", "1mm + missing")});
  REQUIRE_FALSE(missing.has_value());
  CHECK(missing.error().front().offset == 6);
  has_error(evaluate_parameters(std::vector{param("wall", "1 mm", "unknown")}), "unknown_id",
            "/params/0/min");
  has_error(evaluate_parameters(std::vector{param("wall", "1 mm", {}, "unknown")}), "unknown_id",
            "/params/0/max");
  has_error(evaluate_parameters(std::vector{param("wall", "1 + )")}), "parse", "/params/0/value");
  has_error(evaluate_parameters(std::vector{param("wall", true)}), "schema", "/params/0/value");
  has_error(evaluate_parameters(std::vector{param("WALL", 1.0)}), "schema", "/params/0/name");
  has_error(evaluate_parameters(std::vector{param("wall", 1.0), param("wall", 2.0)}), "schema",
            "/params/1/name");
  auto constants = evaluate_parameters(std::vector{param("a", "pi + e")});
  REQUIRE(constants.has_value());
  CHECK(constants->dependencies.at("a").empty());
  auto shadow = evaluate_parameters(std::vector{param("a", "pi + pi"), param("pi", "2 mm")});
  REQUIRE(shadow.has_value());
  CHECK(shadow->values.at("a") == Quantity{4, kLength});
  CHECK(shadow->dependencies.at("a") == std::vector<std::string>{"pi"});
  has_error(evaluate_parameters(std::vector{param("pi", "pi")}), "cycle", "/params/0/value");
}

TEST_CASE("IR parameters: bounds are inclusive, dimensioned and never clamp", "[ir][params]") {
  for (const char* value : {"10 mm", "1 in", "30 mm"}) {
    const auto result = evaluate_parameters(std::vector{param("a", value, "1 cm", "30 mm")});
    REQUIRE(result.has_value());
    CHECK(result->values.at("a") == evaluate_value(value).value());
  }
  for (const char* value : {"9 mm", "31 mm"})
    has_error(evaluate_parameters(std::vector{param("a", value, "1 cm", "30 mm")}), "bounds",
              "/params/0/value");
  has_error(evaluate_parameters(std::vector{param("a", "1 mm", "2 deg")}), "unit", "/params/0/min");
  has_error(evaluate_parameters(std::vector{param("a", "1 mm", {}, 2.0)}), "unit", "/params/0/max");
  has_error(evaluate_parameters(std::vector{param("a", 2.0, 3.0, 1.0)}), "bounds", "/params/0/max");
  has_error(evaluate_parameters(std::vector{param("a", "1 mm", "sqrt(-1)")}), "bounds",
            "/params/0/min");
  has_error(evaluate_parameters(std::vector{param("a", "1 mm", false)}), "schema", "/params/0/min");
  // Bounds use resolved values; self-reference and mutually related bounds are not value cycles.
  const auto self = evaluate_parameters(
      std::vector{param("a", "2 mm", "a/2", "b"), param("b", "4 mm", "a", "b*2")});
  REQUIRE(self.has_value());
  CHECK(self->dependencies.at("a").empty());
  CHECK(self->dependencies.at("b").empty());
}

TEST_CASE("IR parameters: independent evaluation errors retain field locations", "[ir][params]") {
  const auto result = evaluate_parameters(
      std::vector{param("a", "1 mm + 2 deg"), param("dependent", "a * 2"), param("b", "1/0"),
                  param("c", 5.0, {}, 4.0), param("valid", "3 cm")});
  has_error(result, "unit", "/params/0/value");
  has_error(result, "bounds", "/params/2/value");
  has_error(result, "bounds", "/params/3/value");
  CHECK(result.error().size() == 3);
  CHECK(result.error().front().offset == 5);
}

TEST_CASE("IR parameters: long histories use iterative graph traversal and bounded diagnostics",
          "[ir][params]") {
  std::vector<Parameter> params;
  for (int i = 0; i < 10000; ++i)
    params.push_back(
        param("p" + std::to_string(i), i == 0 ? "1 mm" : "p" + std::to_string(i - 1) + " + 1 mm"));
  const auto result = evaluate_parameters(params);
  REQUIRE(result.has_value());
  CHECK(result->values.at("p9999") == Quantity{10000, kLength});
  params[0].value = "p9999";
  const auto cycle = evaluate_parameters(params);
  REQUIRE_FALSE(cycle.has_value());
  CHECK(cycle.error().front().code == "cycle");
  CHECK(cycle.error().front().message.size() < 5000);
  std::vector<Parameter> invalid;
  for (int i = 0; i < 1000; ++i) invalid.push_back(param("p" + std::to_string(i), "missing"));
  CHECK(evaluate_parameters(invalid).error().size() == 128);
  std::vector<Parameter> oversized(65537);
  has_error(evaluate_parameters(oversized), "bounds", "/params");
}

TEST_CASE("IR parameters: aggregate compilation work is bounded", "[ir][params]") {
  std::string expression = "1";
  for (int i = 0; i < 10; ++i) expression = "(" + expression + "+" + expression + ")";
  std::vector<Parameter> params;
  for (int i = 0; i < 500; ++i) params.push_back(param("p" + std::to_string(i), expression));
  const auto result = evaluate_parameters(params);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().front().code == "bounds");
  CHECK(result.error().front().message.find("limit") != std::string::npos);
  const auto large_source =
      evaluate_parameters(std::vector{param("a", std::string(kMaxDocumentBytes + 1, ' '))});
  has_error(large_source, "bounds", "/params/0/value");
}

TEST_CASE("IR parameters: shuffled generated dependency graphs keep their values",
          "[ir][params][property]") {
  std::mt19937 rng(20261017);
  std::vector<Parameter> params{param("p0", "1 mm")};
  std::vector<double> expected{1};
  for (std::size_t i = 1; i < 200; ++i) {
    const auto parent = rng() % i;
    params.push_back(
        param("p" + std::to_string(i), "p" + std::to_string(parent) + " + 1 mm", "0 mm", "200 mm"));
    expected.push_back(expected[parent] + 1);
  }
  for (int trial = 0; trial < 5; ++trial) {
    std::shuffle(params.begin(), params.end(), rng);
    const auto result = evaluate_parameters(params);
    REQUIRE(result.has_value());
    for (std::size_t i = 0; i < expected.size(); ++i)
      CHECK(result->values.at("p" + std::to_string(i)) == Quantity{expected[i], kLength});
  }
}

TEST_CASE("IR parameters: document IO validates expressions and preserves authored values",
          "[ir][params]") {
  Document document;
  document.params = {param("width", "pitch * 2", "10 mm"), param("pitch", "2 cm")};
  document.units.length = "ft";
  const auto saved = write_document(document);
  REQUIRE(saved.has_value());
  const auto loaded = read_document(*saved);
  REQUIRE(loaded.has_value());
  CHECK(loaded->params[0].value == json::Value("pitch * 2"));
  CHECK(evaluate_parameters(loaded->params)->values.at("width") == Quantity{40, kLength});
  document.params[1].value = "width";
  const auto bad = write_document(document);
  REQUIRE_FALSE(bad.has_value());
  CHECK(bad.error().front().code == "cycle");
  auto text = *saved;
  const auto position = text.find("\"2 cm\"");
  REQUIRE(position != std::string::npos);
  text.replace(position, 6, "\"missing\"");
  const auto read = read_document(text);
  REQUIRE_FALSE(read.has_value());
  CHECK(read.error().front().code == "unknown_id");
  CHECK(read.error().front().pointer == "/params/1/value");
}
