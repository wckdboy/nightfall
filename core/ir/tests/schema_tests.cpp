// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>

#include "nf/ir/document.hpp"
#include "schema.hpp"

using namespace nf::ir;

TEST_CASE("IR schema: work exhaustion fails closed through alternatives and negation",
          "[ir][schema]") {
  const auto doc = json::parse(R"({"format":"nightfall.part","version":1,
    "units":{"length":"mm","angle":"deg"},"meta":{},"params":[],"references":[],
    "features":[{"id":"f","type":"hole","inputs":{"face":{"created_by":"base"},
      "positions":[{"sketch":"s","entity":"p"}],"diameter":5,"depth":10,
      "counterbore":{"diameter":8,"depth":2},"countersink":{"diameter":8,"angle":90}}}],
    "history_end":null,"ui":{}})");
  REQUIRE(doc.has_value());
  REQUIRE_FALSE(detail::check_schema(*doc).errors.empty());
  // Conflicting hole forms must never become valid when a smaller budget interrupts
  // the negated schema or an alternative. Exercise budgets before and after that point.
  for (std::size_t limit = 0; limit < 250; ++limit) {
    INFO(limit);
    CHECK_FALSE(detail::check_schema(*doc, limit).errors.empty());
  }
}

TEST_CASE("IR schema: native rules agree with the published schema corpus", "[ir][schema]") {
  std::ifstream stream(NF_IR_CORPUS);
  REQUIRE(stream.good());
  const std::string text{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
  const auto corpus = json::parse(text);
  REQUIRE(corpus.has_value());
  REQUIRE(corpus->if_array());
  for (const auto& test : *corpus->if_array()) {
    INFO(*test.find("name")->if_string());
    const auto result = detail::check_schema(*test.find("document"));
    const bool valid = *test.find("schema_valid")->if_bool();
    CHECK(result.errors.empty() == valid);
    if (!valid && test.find("error")) {
      const auto& pointer = *test.find("error")->find("pointer")->if_string();
      CHECK(
          std::ranges::any_of(result.errors, [&](const auto& e) { return e.pointer == pointer; }));
    }
    const auto loaded = read_document(json::write(*test.find("document")));
    if (!valid) REQUIRE_FALSE(loaded.has_value());
    if (loaded) {
      const auto written = write_document(*loaded);
      REQUIRE(written.has_value());
      CHECK(json::canonical(*json::parse(*written)) == json::canonical(*test.find("document")));
    }
  }
}

TEST_CASE("IR schema: links are retained only from matching alternatives", "[ir][schema]") {
  const auto doc = json::parse(R"({"format":"nightfall.part","version":1,
    "units":{"length":"mm","angle":"deg"},"meta":{},"params":[],"references":[],
    "features":[{"id":"f","type":"fillet","inputs":{"edges":{
      "any":[{"created_by":"base"},{"faces_of":"plate"}],
      "x-plugin":{"created_by":"must_be_ignored"}},"radius":"2 mm"}}],
    "history_end":null,"ui":{"faces_of":"must_also_be_ignored"}})");
  REQUIRE(doc.has_value());
  const auto result = detail::check_schema(*doc);
  REQUIRE(result.errors.empty());
  REQUIRE(result.links.size() == 2);
  CHECK(result.links[0].target == "base");
  CHECK(result.links[0].kind == "feature");
  CHECK(result.links[0].pointer == "/features/0/inputs/edges/any/0/created_by");
  CHECK(result.links[1].target == "plate");
  CHECK(result.links[1].kind == "body");
}

TEST_CASE("IR schema: recursive selectors remain bounded and reject nested typos", "[ir][schema]") {
  Document doc;
  Feature f;
  f.id = "f";
  f.type = "fillet";
  json::Value selector = json::Object{{"created_by", "base"}};
  for (int i = 0; i < 60; ++i) selector = json::Object{{"edges_of", std::move(selector)}};
  f.inputs = json::Object{{"edges", selector}, {"radius", 1.0}};
  doc.features.push_back(f);
  // This stage checks shape only; the reference to base is checked separately in #90.
  REQUIRE(write_document(doc).has_value());
  selector = json::Object{{"created_by", "base"}, {"face_index", 7.0}};
  for (int i = 0; i < 60; ++i) selector = json::Object{{"edges_of", std::move(selector)}};
  doc.features[0].inputs = json::Object{{"edges", std::move(selector)}, {"radius", 1.0}};
  const auto written = write_document(doc);
  REQUIRE_FALSE(written.has_value());
  CHECK(std::ranges::any_of(written.error(),
                            [](const auto& e) { return e.pointer.ends_with("/face_index"); }));
}
