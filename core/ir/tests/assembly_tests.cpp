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

namespace {
json::Value corpus() {
  std::ifstream stream(NF_IR_CORPUS_V2);
  REQUIRE(stream.good());
  const std::string text{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
  auto value = json::parse(text);
  REQUIRE(value.has_value());
  REQUIRE(value->if_array());
  return std::move(*value);
}
const json::Value& named(const json::Value& cases, std::string_view name) {
  for (const auto& c : *cases.if_array())
    if (*c.find("name")->if_string() == name) return *c.find("document");
  FAIL("no corpus case " << name);
  return cases;
}
}  // namespace

TEST_CASE("IR v2: native rules agree with the published assembly corpus", "[ir][assembly]") {
  const auto cases = corpus();
  for (const auto& test : *cases.if_array()) {
    INFO(*test.find("name")->if_string());
    const auto& document = *test.find("document");
    const auto result = detail::check_schema(document);
    CHECK(result.errors.empty() == *test.find("schema_valid")->if_bool());
    const auto loaded = read_document(json::write(document));
    CHECK(loaded.has_value() == *test.find("valid")->if_bool());
    if (!loaded && test.find("error")) {
      const auto& expected = *test.find("error");
      for (const auto& e : loaded.error())
        UNSCOPED_INFO(e.code << " " << e.pointer << ": " << e.message);
      CHECK(std::ranges::any_of(loaded.error(), [&](const auto& e) {
        return e.pointer == *expected.find("pointer")->if_string() &&
               e.code == *expected.find("code")->if_string();
      }));
    }
    if (loaded) {
      const auto written = write_document(*loaded);
      REQUIRE(written.has_value());
      CHECK(json::canonical(*json::parse(*written)) == json::canonical(document));
    }
  }
}

TEST_CASE("IR v2: assembly sections decode into the document", "[ir][assembly]") {
  const auto cases = corpus();
  const auto d = read_document(json::write(named(cases, "assembly with every section")));
  REQUIRE(d.has_value());
  REQUIRE(d->components.size() == 2);
  CHECK(d->components[0].id == "link");
  REQUIRE(d->components[0].features.has_value());
  CHECK(d->components[0].features->size() == 2);
  CHECK(d->components[0].features->at(1).id == "bore");
  CHECK_FALSE(d->components[0].reference.has_value());
  CHECK(d->components[1].reference == "motor");
  CHECK(*d->components[1].params.front().value.if_string() == "8 mm");
  REQUIRE(d->occurrences.size() == 4);
  CHECK(d->occurrences[0].component == "root");
  CHECK(d->occurrences[0].grounded == true);
  CHECK_FALSE(d->occurrences[1].grounded.has_value());
  CHECK(d->occurrences[1].params.front().key == "length");
  CHECK(d->occurrences[2].placement.has_value());
  REQUIRE(d->joints.size() == 4);
  CHECK(d->joints[0].kind == "revolute");
  CHECK(d->joints[0].a.occurrence == "base");
  CHECK(d->joints[0].b.rule == "cylinder_axis");
  CHECK(*d->joints[0].value->if_string() == "0 deg");
  CHECK(d->joints[1].b.flip == true);
  CHECK(d->joints[2].b.occurrence == "drive/rotor");
  CHECK(d->joints[2].b.offset.has_value());
  CHECK_FALSE(d->joints[2].value.has_value());
  CHECK(d->joints[3].limits->if_array()->size() == 3);
}

TEST_CASE("IR v2: sections are written after features in RFC order", "[ir][assembly]") {
  const auto cases = corpus();
  const auto d = read_document(json::write(named(cases, "assembly with every section")));
  REQUIRE(d.has_value());
  const auto written = write_document(*d);
  REQUIRE(written.has_value());
  CHECK(written->starts_with("{\n  \"format\": \"nightfall.part\",\n  \"version\": 2,\n"));
  const auto at = [&](std::string_view key) {
    return written->find("\n  \"" + std::string(key) + "\": ");
  };
  CHECK(at("features") < at("components"));
  CHECK(at("components") < at("occurrences"));
  CHECK(at("occurrences") < at("joints"));
  CHECK(at("joints") < at("history_end"));
  // Removing every assembly section leaves a plain part that omits them entirely.
  auto plain = *d;
  plain.components.clear();
  plain.occurrences.clear();
  plain.joints.clear();
  const auto part = write_document(plain);
  REQUIRE(part.has_value());
  CHECK(part->find("\"components\"") == std::string::npos);
  CHECK(part->find("\"joints\"") == std::string::npos);
}

TEST_CASE("IR v2: occurrences and joints edit through the typed document", "[ir][assembly]") {
  const auto cases = corpus();
  auto d = read_document(json::write(named(cases, "assembly with every section"))).value();
  d.joints[0].value = json::Value("45 deg");
  d.occurrences[3].grounded = true;
  const auto written = write_document(d);
  REQUIRE(written.has_value());
  const auto edited = read_document(*written).value();
  CHECK(*edited.joints[0].value->if_string() == "45 deg");
  CHECK(edited.occurrences[3].grounded == true);
  // An edit that breaks a reference is refused with its pointer, like any invalid edit.
  d.joints[0].a.occurrence = "nowhere";
  const auto refused = write_document(d);
  REQUIRE_FALSE(refused.has_value());
  CHECK(std::ranges::any_of(refused.error(), [](const auto& e) {
    return e.code == "unknown_id" && e.pointer == "/joints/0/a/occurrence";
  }));
}

TEST_CASE("IR v2: densities decode on components and occurrences", "[ir][assembly]") {
  const auto cases = corpus();
  const auto d =
      read_document(json::write(named(cases, "densities on a component and an occurrence")));
  REQUIRE(d.has_value());
  CHECK(d->components[0].density == 7850.0);
  CHECK_FALSE(d->components[1].density.has_value());
  CHECK_FALSE(d->occurrences[0].density.has_value());
  CHECK(d->occurrences[1].density == 2700.5);
}
