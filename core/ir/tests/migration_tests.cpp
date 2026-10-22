// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "nf/ir/document.hpp"
#include "nf/ir/hash.hpp"
#include "nf/ir/inputs.hpp"
#include "nf/ir/migration.hpp"
#include "nf/ir/parameters.hpp"

using namespace nf::ir;

namespace {
json::Value parse(std::string_view text) { return json::parse(text).value(); }
std::string read(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  REQUIRE(stream.good());
  return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
/// The document with its version replaced, everything else in place.
json::Value with_version(const json::Value& document, double version) {
  json::Object o = *document.if_object();
  for (auto& m : o)
    if (m.key == "version") m.value = version;
  return o;
}
/// RFC 0005 feature hashes of every feature, with evaluated inputs and no upstream links.
std::vector<std::string> feature_hashes(const Document& d) {
  const auto params = evaluate_parameters(d.params);
  REQUIRE(params.has_value());
  std::vector<std::string> hashes;
  for (const auto& f : d.features) {
    const auto compiled = FeatureInputs::compile(f);
    REQUIRE(compiled.has_value());
    const auto inputs = compiled->evaluate(d.units, params->values);
    REQUIRE(inputs.has_value());
    hashes.push_back(feature_hash("migration-test", f.type, *inputs, {}).hex());
  }
  return hashes;
}
}  // namespace

TEST_CASE("IR migration: 1 to 2 changes only the version", "[ir][migration]") {
  const auto v1 = parse(R"({"format":"nightfall.part","version":1,
    "units":{"length":"in","angle":"rad"},"meta":{"name":"Plate"},"x-tool":[1,{}],
    "params":[{"name":"wall","value":"3 mm"}],"references":[],"features":[],
    "history_end":null,"ui":{"camera":"iso"}})");
  const auto v2 = migrate_1_to_2(v1);
  CHECK(v2 == with_version(v1, 2));
  // Member order is unchanged, so a migrated file diffs on one line.
  REQUIRE(v2.if_object()->size() == v1.if_object()->size());
  for (std::size_t i = 0; i < v1.if_object()->size(); ++i)
    CHECK((*v2.if_object())[i].key == (*v1.if_object())[i].key);
  CHECK(migrate(v1) == v2);
  // The current version, newer versions and non-documents pass through unchanged.
  CHECK(migrate(v2) == v2);
  for (const auto* text :
       {R"({"format":"nightfall.part","version":3})", "[]", "null", R"({"version":"1"})"})
    CHECK(migrate(parse(text)) == parse(text));
}

TEST_CASE("IR migration: every reference part reads as v2 with identical feature hashes",
          "[ir][migration]") {
  std::size_t parts = 0;
  for (const auto& entry : std::filesystem::directory_iterator(NF_REFERENCE_PARTS_DIR)) {
    if (entry.path().extension() != ".nightfall") continue;
    CAPTURE(entry.path().filename().string());
    ++parts;
    const auto text = read(entry.path());
    const auto original = parse(text);
    REQUIRE(*original.find("version") == json::Value(1.0));
    const auto from_v1 = read_document(text);
    REQUIRE(from_v1.has_value());
    const auto written = write_document(*from_v1);
    REQUIRE(written.has_value());
    // Written as version 2 with no new sections: the file changes by its version alone.
    const auto rewritten = parse(*written);
    CHECK(rewritten == with_version(original, 2));
    CHECK_FALSE(rewritten.find("components"));
    CHECK_FALSE(rewritten.find("occurrences"));
    CHECK_FALSE(rewritten.find("joints"));
    const auto from_v2 = read_document(*written);
    REQUIRE(from_v2.has_value());
    CHECK(write_document(*from_v2).value() == *written);
    CHECK(feature_hashes(*from_v1) == feature_hashes(*from_v2));
  }
  CHECK(parts == 20);
}

TEST_CASE("IR migration: v1 diagnostics keep their pointers after migration", "[ir][migration]") {
  const auto result = read_document(R"({"format":"nightfall.part","version":1,
    "units":{"length":"mm","angle":"deg"},"meta":{},"params":[{"name":"w","value":"2 deg"}],
    "references":[],"features":[{"id":"s","type":"sketch","inputs":{"plane":{"datum":"xy"},
      "entities":[{"id":"r","type":"rect","corner":[0,0],"size":["w",1]}],"constraints":[]}}],
    "history_end":null,"ui":{},"components":[]})");
  REQUIRE_FALSE(result.has_value());
  // A v1 file never had assembly sections: the key is unknown there, not silently accepted.
  CHECK(std::ranges::any_of(result.error(), [](const auto& e) {
    return e.code == "schema" && e.pointer == "/components";
  }));
}
