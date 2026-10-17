// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <iterator>
#include <string>

#include "nf/svg/xml.hpp"

using namespace nf::svg;

namespace {

xml::Element parse(std::string_view text) {
  auto r = xml::parse(text);
  INFO((r ? "" : r.error().message));
  REQUIRE(r.has_value());
  return std::move(*r);
}

std::size_t error_at(std::string_view text) {
  const auto r = xml::parse(text);
  REQUIRE_FALSE(r.has_value());
  CHECK_FALSE(r.error().message.empty());
  return r.error().offset;
}

}  // namespace

TEST_CASE("elements, attributes and nesting", "[svg][xml]") {
  const auto root = parse(
      "\xEF\xBB\xBF<?xml version=\"1.0\"?>\n<!-- c -->\n"
      "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\" \"x\" [ <!ENTITY e \"v\"> ]>\n"
      "<svg xmlns='http://www.w3.org/2000/svg' width=\"10mm\">\n"
      "  <g id=\"a\"><path d=\"M0 0\"/><svg:rect x = '1'/></g>\n"
      "  <![CDATA[ <not an element> ]]><?pi x?>text &amp; more\n"
      "</svg>\n<!-- trailing -->\n");
  CHECK(root.name == "svg");
  CHECK(*root.attribute("width") == "10mm");
  CHECK(root.attribute("height") == nullptr);
  REQUIRE(root.children.size() == 1);
  const auto& g = root.children[0];
  REQUIRE(g.children.size() == 2);
  CHECK(g.children[0].name == "path");
  CHECK(*g.children[0].attribute("d") == "M0 0");
  CHECK(g.children[1].name == "svg:rect");
  CHECK(g.children[1].local_name() == "rect");
  CHECK(*g.children[1].attribute("x") == "1");
}

TEST_CASE("attribute values are decoded and normalised", "[svg][xml]") {
  const auto e =
      parse("<a v=\"&lt;&gt;&amp;&quot;&apos;&#65;&#x42;&#x20AC;&#128512;\" w='a\tb\r\nc\nd'/>");
  CHECK(*e.attribute("v") == "<>&\"'AB\xE2\x82\xAC\xF0\x9F\x98\x80");
  CHECK(*e.attribute("w") == "a b c d");
}

TEST_CASE("malformed documents are errors with offsets", "[svg][xml]") {
  CHECK(error_at("") == 0);
  CHECK(error_at("text") == 0);
  CHECK(error_at("<a>") == 3);                  // unterminated
  CHECK(error_at("<a></b>") == 5);              // mismatched end tag
  CHECK(error_at("<a x=1/>") == 5);             // unquoted
  CHECK(error_at("<a x='1' x='2'/>") == 9);     // duplicate
  CHECK(error_at("<a x='1'y='2'/>") == 8);      // no space between attributes
  CHECK(error_at("<a x='<'/>") == 6);           // '<' in a value
  CHECK(error_at("<a x='&foo;'/>") == 6);       // undefined entity (DOCTYPE ignored)
  CHECK(error_at("<a x='&#0;'/>") == 6);        // invalid code point
  CHECK(error_at("<a x='&#xD800;'/>") == 6);    // surrogate
  CHECK(error_at("<a x='&#x110000;'/>") == 6);  // beyond Unicode
  CHECK(error_at("<a x='&amp'/>") == 6);        // no semicolon
  CHECK(error_at("<a/><b/>") == 4);             // two roots
  CHECK(error_at("<a><!-- x </a>") == 3);       // unterminated comment
  CHECK(error_at("<a>&bogus;</a>") == 3);       // bad entity in text
  CHECK(error_at("<!DOCTYPE a [ <a/>") == 18);  // unterminated DOCTYPE
}

TEST_CASE("nesting depth is bounded", "[svg][xml]") {
  std::string ok, deep;
  for (int i = 0; i < xml::kMaxDepth; ++i) ok += "<g>";
  for (int i = 0; i < xml::kMaxDepth; ++i) ok += "</g>";
  CHECK(xml::parse(ok).has_value());
  for (int i = 0; i <= xml::kMaxDepth; ++i) deep += "<g>";
  CHECK_FALSE(xml::parse(deep).has_value());
  // Far deeper input fails the same way instead of exhausting the stack.
  CHECK_FALSE(xml::parse(std::string(1'000'000, '<')).has_value());
  std::string huge;
  for (int i = 0; i < 100'000; ++i) huge += "<g>";
  CHECK(xml::parse(huge).error().message == "elements nested too deeply");
}

TEST_CASE("random and truncated input never crashes", "[svg][xml][property]") {
  // Fixed-seed stand-in for fuzzing (run under ASan/UBSan nightly): fragments of real syntax
  // spliced at random, plus every prefix of a valid document.
  const std::string valid =
      "<?xml version='1.0'?><!DOCTYPE svg [<!ENTITY x 'y'>]><svg a=\"&amp;&#x41;\">"
      "<g><path d='M0 0'/><![CDATA[x]]><!--c--></g>&lt;</svg>";
  for (std::size_t n = 0; n <= valid.size(); ++n) (void)xml::parse(valid.substr(0, n));
  const char* pieces[] = {"<",         ">",         "/>", "</", "=",       "'",    "\"",
                          "&",         ";",         "#x", "#",  "a",       "<!--", "-->",
                          "<![CDATA[", "]]>",       "<?", "?>", " ",       "\n",   "amp",
                          "\xFF",      "<!DOCTYPE", "[",  "]",  "&#xD800;"};
  std::uint32_t state = 20261017;
  for (int i = 0; i < 5000; ++i) {
    std::string s;
    const int len = static_cast<int>(state % 40);
    for (int j = 0; j < len; ++j) {
      state = state * 1664525u + 1013904223u;
      s += pieces[(state >> 8) % std::size(pieces)];
    }
    state = state * 1664525u + 1013904223u;
    (void)xml::parse(s);
  }
  SUCCEED();
}
