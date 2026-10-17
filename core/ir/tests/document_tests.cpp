// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <random>
#include <string>

#include "nf/ir/document.hpp"

using namespace nf::ir;

namespace {

constexpr std::string_view kPart = R"({
  "format": "nightfall.part", "version": 1,
  "units": {"length": "mm", "angle": "deg"},
  "meta": {"name": "Mounting bracket", "doc": "A parametric plate"},
  "params": [{"name": "width", "value": "40 mm", "min": "10 mm", "max": "200 mm"},
             {"name": "wall", "value": "3 mm", "doc": "Wall thickness"},
             {"name": "count", "value": 4}],
  "references": [{"id": "logo", "kind": "svg", "path": "art/logo.svg",
    "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"}],
  "features": [
    {"id": "outline", "type": "sketch", "inputs": {
      "plane": {"datum": "xy"}, "entities": [
        {"id": "r", "type": "rect", "corner": [0, 0], "size": ["width", "20 mm"]}],
      "constraints": []}},
    {"id": "base", "type": "extrude", "name": "Base plate", "suppressed": false,
      "inputs": {"profile": {"sketch": "outline"}, "distance": "wall",
        "result": {"new": "plate", "body_type": "brep"}}}
  ],
  "history_end": "base", "ui": {"camera": {"eye": [60, 40, 80]}}
})";

std::string replace(std::string text, std::string_view from, std::string_view to) {
  const auto at = text.find(from);
  REQUIRE(at != std::string::npos);
  text.replace(at, from.size(), to);
  return text;
}

Document read_ok(std::string_view text = kPart) {
  auto doc = read_document(text);
  if (!doc) {
    for (const auto& e : doc.error()) INFO(e.code << " " << e.pointer << ": " << e.message);
  }
  REQUIRE(doc.has_value());
  return std::move(*doc);
}

void has_error(const std::expected<Document, Errors>& result, std::string_view code,
               std::string_view pointer) {
  REQUIRE_FALSE(result.has_value());
  for (const auto& e : result.error()) {
    CHECK_FALSE(e.message.empty());
    if (e.code == code && e.pointer == pointer) return;
  }
  FAIL("missing " << code << " at " << pointer);
}

}  // namespace

TEST_CASE("IR document: reads named parameters and an ordered feature history", "[ir][document]") {
  const auto doc = read_ok();
  CHECK(doc.units.length == "mm");
  CHECK(doc.units.angle == "deg");
  CHECK(doc.meta.name == "Mounting bracket");
  CHECK(doc.meta.doc == "A parametric plate");
  REQUIRE(doc.params.size() == 3);
  CHECK(doc.params[0].name == "width");
  CHECK(*doc.params[0].value.if_string() == "40 mm");
  CHECK(*doc.params[0].min->if_string() == "10 mm");
  CHECK(*doc.params[0].max->if_string() == "200 mm");
  CHECK(doc.params[1].doc == "Wall thickness");
  CHECK(*doc.params[2].value.if_number() == 4.0);
  REQUIRE(doc.references.size() == 1);
  CHECK(doc.references[0].id == "logo");
  CHECK(doc.references[0].path == "art/logo.svg");
  REQUIRE(doc.features.size() == 2);
  CHECK(doc.features[0].id == "outline");
  CHECK_FALSE(doc.features[0].suppressed.has_value());
  CHECK(doc.features[1].name == "Base plate");
  CHECK(doc.features[1].suppressed == false);
  CHECK(*doc.features[1].inputs.find("distance")->if_string() == "wall");
  CHECK(doc.history_end == "base");
  CHECK(doc.ui.size() == 1);
}

TEST_CASE("IR document: readable save preserves content and edits", "[ir][document]") {
  auto doc = read_ok();
  auto written = write_document(doc);
  REQUIRE(written.has_value());
  CHECK(written->starts_with("{\n  \"format\": \"nightfall.part\",\n  \"version\": 1,\n"));
  CHECK(written->ends_with("\n"));
  CHECK(*json::parse(*written) == *json::parse(kPart));
  CHECK(write_document(read_ok(*written)) == *written);
  doc.params[0].value = json::Value("80 mm");
  doc.features[1].suppressed = true;
  doc.history_end = "outline";
  written = write_document(doc);
  REQUIRE(written.has_value());
  const auto edited = read_ok(*written);
  CHECK(*edited.params[0].value.if_string() == "80 mm");
  CHECK(edited.features[1].suppressed == true);
  CHECK(edited.history_end == "outline");
  // Omission, an explicit false, and an empty display label survive independently.
  doc.features[0].name = "";
  written = write_document(doc);
  REQUIRE(written.has_value());
  CHECK(read_ok(*written).features[0].name == "");
  CHECK_FALSE(read_ok(*written).features[0].suppressed.has_value());
}

TEST_CASE("IR document: extensions and opaque input data round trip", "[ir][document]") {
  auto text =
      replace(std::string(kPart), "\"version\": 1,", "\"version\": 1, \"x-plugin\": [1, {}],");
  text = replace(text, "\"length\": \"mm\"", "\"x-display\": true, \"length\": \"mm\"");
  text = replace(text, "\"name\": \"width\"", "\"name\": \"width\", \"x-source\": {\"a/b~c\": 1}");
  text = replace(text, "\"kind\": \"svg\"", "\"kind\": \"svg\", \"x-origin\": null");
  text = replace(text, "\"type\": \"sketch\"", "\"type\": \"sketch\", \"x-color\": [1, 0, 0]");
  auto doc = read_ok(text);
  doc.meta.extensions = {{"x-note", json::Value("café")}};
  auto written = write_document(doc);
  REQUIRE(written.has_value());
  const auto again = read_ok(*written);
  CHECK(again.extensions == doc.extensions);
  CHECK(again.units.extensions == doc.units.extensions);
  CHECK(again.meta.extensions == doc.meta.extensions);
  CHECK(again.params[0].extensions == doc.params[0].extensions);
  CHECK(again.references[0].extensions == doc.references[0].extensions);
  CHECK(again.features[0].extensions == doc.features[0].extensions);
  CHECK(again.features[0].inputs == doc.features[0].inputs);
  CHECK(again.ui == doc.ui);
}

TEST_CASE("IR document: new empty documents and all document units", "[ir][document]") {
  Document empty;
  for (const auto* length : {"mm", "cm", "m", "in", "ft"}) {
    for (const auto* angle : {"deg", "rad"}) {
      empty.units.length = length;
      empty.units.angle = angle;
      auto text = write_document(empty);
      REQUIRE(text.has_value());
      const auto doc = read_ok(*text);
      CHECK(doc.units.length == length);
      CHECK(doc.units.angle == angle);
      CHECK(doc.params.empty());
      CHECK(doc.references.empty());
      CHECK(doc.features.empty());
      CHECK_FALSE(doc.history_end.has_value());
    }
  }
}

TEST_CASE("IR document: format and version are checked before document fields", "[ir][document]") {
  for (const auto* text : {"[]", "null", "true", "42"})
    has_error(read_document(text), "schema", "");
  has_error(read_document("{}"), "schema", "/format");
  has_error(read_document(R"({"format":"another.part","version":1})"), "schema", "/format");
  for (const auto* version : {"0", "-1", "1.5", "\"1\"", "null", "true", "1e30"}) {
    has_error(
        read_document(std::string(R"({"format":"nightfall.part","version":)") + version + "}"),
        "schema", "/version");
  }
  const auto newer = read_document(R"({"format":"nightfall.part","version":2,"future":true})");
  has_error(newer, "schema", "/version");
  REQUIRE(newer.error().size() == 1);
  CHECK(newer.error()[0].message.find("newer Nightfall (version 2)") != std::string::npos);
}

TEST_CASE("IR document: field errors carry escaped JSON Pointers", "[ir][document]") {
  struct Case {
    std::string_view from, to, pointer;
  };
  for (const auto& [from, to, pointer] :
       {Case{"\"length\": \"mm\"", "\"length\": \"inch\"", "/units/length"},
        {"\"angle\": \"deg\"", "\"angle\": 0", "/units/angle"},
        {"\"doc\": \"A parametric plate\"", "\"doc\": []", "/meta/doc"},
        {"\"value\": \"40 mm\"", "\"value\": null", "/params/0/value"},
        {"\"min\": \"10 mm\"", "\"min\": true", "/params/0/min"},
        {"\"max\": \"200 mm\"", "\"max\": {}", "/params/0/max"},
        {"\"path\": \"art/logo.svg\"", "\"path\": \"\"", "/references/0/path"},
        {"\"kind\": \"svg\"", "\"kind\": false", "/references/0/kind"},
        {"\"sha256\": \"0123", "\"sha256\": \"g123", "/references/0/sha256"},
        {"\"type\": \"sketch\"", "\"type\": \"sktech\"", "/features/0/type"},
        {"\"suppressed\": false", "\"suppressed\": 1", "/features/1/suppressed"},
        {"\"name\": \"Base plate\"", "\"name\": null", "/features/1/name"},
        {"\"history_end\": \"base\"", "\"history_end\": 1", "/history_end"},
        {"\"version\": 1,", "\"version\": 1, \"mist/ake~\": 0,", "/mist~1ake~0"},
        {"\"value\": \"40 mm\"", "\"value\": \"40 mm\", \"vlaue\": 2", "/params/0/vlaue"}}) {
    INFO(from);
    has_error(read_document(replace(std::string(kPart), from, to)), "schema", pointer);
  }
  const auto many = read_document(
      replace(replace(std::string(kPart), "\"length\": \"mm\"", "\"length\": \"inch\""),
              "\"value\": \"40 mm\"", "\"value\": null"));
  has_error(many, "schema", "/units/length");
  has_error(many, "schema", "/params/0/value");
}

TEST_CASE("IR document: IDs are bounded and unique within their namespace", "[ir][document]") {
  for (const auto& bad : std::vector<std::string>{"", "Width", "1width", "with-hyphen", "a/b", "é",
                                                  std::string(65, 'a')}) {
    auto text = replace(std::string(kPart), "\"name\": \"width\"", "\"name\": \"" + bad + "\"");
    has_error(read_document(text), "schema", "/params/0/name");
  }
  read_ok(replace(std::string(kPart), "\"name\": \"width\"",
                  "\"name\": \"" + std::string(64, 'a') + "\""));
  read_ok(replace(std::string(kPart), "\"name\": \"width\"", "\"name\": \"_a09\""));
  has_error(read_document(replace(std::string(kPart), "\"name\": \"count\"", "\"name\": \"wall\"")),
            "schema", "/params/2/name");
  has_error(read_document(replace(std::string(kPart), "\"id\": \"base\"", "\"id\": \"outline\"")),
            "schema", "/features/1/id");
  // A parameter, reference and feature can deliberately share a name.
  read_ok(replace(replace(std::string(kPart), "\"name\": \"width\"", "\"name\": \"outline\""),
                  "\"id\": \"logo\"", "\"id\": \"outline\""));
  has_error(read_document(replace(std::string(kPart), "\"history_end\": \"base\"",
                                  "\"history_end\": \"missing\"")),
            "unknown_id", "/history_end");
}

TEST_CASE("IR document: save rejects invalid edits and colliding extensions", "[ir][document]") {
  auto doc = read_ok();
  doc.features[1].id = "outline";
  auto written = write_document(doc);
  REQUIRE_FALSE(written.has_value());
  CHECK(written.error()[0].pointer == "/features/1/id");
  doc = read_ok();
  doc.extensions.push_back({"version", json::Value(2.0)});
  written = write_document(doc);
  REQUIRE_FALSE(written.has_value());
  CHECK(written.error()[0].code == "schema");
  CHECK(written.error()[0].pointer == "/version");
  // An extension cannot impersonate an optional field that happens to be absent.
  doc = read_ok();
  doc.features[0].extensions.push_back({"suppressed", json::Value(true)});
  written = write_document(doc);
  REQUIRE_FALSE(written.has_value());
  CHECK(written.error()[0].pointer == "/features/0/suppressed");
  doc = read_ok();
  doc.params[0].extensions = {{"x-note", json::Value(1.0)}, {"x-note", json::Value(2.0)}};
  written = write_document(doc);
  REQUIRE_FALSE(written.has_value());
  CHECK(written.error()[0].code == "parse");
  doc = read_ok();
  doc.meta.name = std::string("\xff");
  REQUIRE_FALSE(write_document(doc).has_value());
}

TEST_CASE("IR document: in-memory nesting is checked before serialization", "[ir][document]") {
  auto doc = read_ok();
  // root(0), features(1), feature(2), inputs(3), then the extension value(4).
  json::Value nested;
  for (int i = 4; i < json::kMaxDepth; ++i) nested = json::Array{std::move(nested)};
  doc.features[0].inputs = json::Object{{"x-nested", nested}};
  REQUIRE(write_document(doc).has_value());
  doc.features[0].inputs = json::Object{{"x-nested", json::Array{std::move(nested)}}};
  const auto written = write_document(doc);
  REQUIRE_FALSE(written.has_value());
  CHECK(written.error()[0].pointer == "/features/0/inputs");
}

TEST_CASE("IR document: every envelope field and collection item is checked", "[ir][document]") {
  const auto seed = json::parse(kPart);
  REQUIRE(seed.has_value());
  for (const auto& member : *seed->if_object()) {
    json::Object missing;
    for (const auto& other : *seed->if_object())
      if (other.key != member.key) missing.push_back(other);
    has_error(read_document(json::write(missing)), "schema", "/" + member.key);
  }
  for (const auto* field : {"units", "meta", "params", "references", "features", "ui"}) {
    for (const auto* invalid : {"null", "true", "0", "\"\""}) {
      json::Object bad = *seed->if_object();
      for (auto& member : bad)
        if (member.key == field) member.value = *json::parse(invalid);
      has_error(read_document(json::write(bad)), "schema", "/" + std::string(field));
    }
  }
  for (const auto* field : {"params", "references", "features"}) {
    json::Object bad = *seed->if_object();
    for (auto& member : bad)
      if (member.key == field) member.value = json::Array{json::Value(true)};
    has_error(read_document(json::write(bad)), "schema", "/" + std::string(field) + "/0");
  }
  auto doc = read_ok();
  doc.references.push_back(doc.references[0]);
  const auto written = write_document(doc);
  REQUIRE_FALSE(written.has_value());
  CHECK(written.error()[0].pointer == "/references/1/id");
}

TEST_CASE("IR document: all v1 feature envelopes remain stored through rollback",
          "[ir][document]") {
  Document doc;
  for (const auto* type : {"sketch", "extrude", "revolve", "sweep", "loft", "fillet", "chamfer",
                           "shell", "hole", "boolean", "pattern_linear", "pattern_circular",
                           "mirror", "transform", "import", "script"}) {
    Feature f;
    f.id = type;
    f.type = type;
    doc.features.push_back(std::move(f));
  }
  doc.history_end = "extrude";
  doc.features.back().suppressed = true;
  const auto text = write_document(doc);
  REQUIRE(text.has_value());
  const auto again = read_ok(*text);
  CHECK(again.features.size() == 16);
  CHECK(again.features.back().suppressed == true);
  // Rollback and suppression never hide malformed stored envelopes.
  doc.features.back().type = "unknown";
  const auto invalid = write_document(doc);
  REQUIRE_FALSE(invalid.has_value());
  CHECK(invalid.error()[0].pointer == "/features/15/type");
}

TEST_CASE("IR document: parse errors and size limits are reported", "[ir][document]") {
  auto result = read_document("{\"format\":}");
  has_error(result, "parse", "");
  CHECK(result.error()[0].offset == 10);
  result = read_document(std::string(kMaxDocumentBytes + 1, ' '));
  has_error(result, "parse", "");
  CHECK(result.error()[0].message.find("size limit") != std::string::npos);
}

TEST_CASE("IR document: malformed mutations never crash or lose accepted data", "[ir][document]") {
  std::mt19937 rng(20261017);
  int accepted = 0;
  for (int i = 0; i < 1500; ++i) {
    auto text = std::string(kPart);
    const auto at = rng() % text.size();
    text[at] = "{}[]\",:null0123 xyz"[rng() % 18];
    const auto doc = read_document(text);
    if (!doc) continue;
    ++accepted;
    const auto written = write_document(*doc);
    REQUIRE(written.has_value());
    CHECK(write_document(read_ok(*written)) == *written);
    CHECK(json::canonical(*json::parse(*written)) == json::canonical(*json::parse(text)));
  }
  CHECK(accepted > 20);
}
