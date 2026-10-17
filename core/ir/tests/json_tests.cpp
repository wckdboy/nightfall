// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <string>

#include "nf/ir/json.hpp"

using namespace nf::ir::json;

namespace {

Value ok(std::string_view text) {
  auto v = parse(text);
  INFO(text << " -> " << (v ? std::string() : v.error().message));
  REQUIRE(v.has_value());
  return std::move(*v);
}

ParseError fails(std::string_view text) {
  auto v = parse(text);
  INFO(text);
  REQUIRE_FALSE(v.has_value());
  return v.error();
}

}  // namespace

TEST_CASE("JSON: literals, numbers and strings", "[ir][json]") {
  CHECK(ok("null").is_null());
  CHECK(*ok(" true ").if_bool());
  CHECK_FALSE(*ok("false").if_bool());
  CHECK(*ok("-12.5e2").if_number() == -1250.0);
  CHECK(*ok("0").if_number() == 0.0);
  CHECK(std::signbit(*ok("-0").if_number()));
  CHECK(*ok("1E-2").if_number() == 0.01);
  CHECK(*ok(R"("a\"b\\c\/d\b\f\n\r\t")").if_string() == "a\"b\\c/d\b\f\n\r\t");
  CHECK(*ok(R"("\u00e9\u20AC")").if_string() == "\u00e9\u20ac");
  CHECK(*ok(R"("\ud83d\ude00")").if_string() == "\U0001F600");  // surrogate pair
  CHECK(*ok("\"caf\xc3\xa9 \xf0\x9f\x98\x80\"").if_string() == "caf\u00e9 \U0001F600");
}

TEST_CASE("JSON: arrays and objects keep their order", "[ir][json]") {
  const Value v = ok(R"( { "b" : [1, [], {}], "a": {"x": null} } )");
  const Object& o = *v.if_object();
  REQUIRE(o.size() == 2);
  CHECK(o[0].key == "b");
  CHECK(o[1].key == "a");
  const Array& a = *v.find("b")->if_array();
  REQUIRE(a.size() == 3);
  CHECK(a[1].if_array()->empty());
  CHECK(a[2].if_object()->empty());
  CHECK(v.find("a")->find("x")->is_null());
  CHECK(v.find("missing") == nullptr);
  CHECK(a[0].find("b") == nullptr);  // not an object
  CHECK(a[2].offset == 17);          // byte offset of the value
}

TEST_CASE("JSON: integers are exact up to 2^53", "[ir][json]") {
  CHECK(ok("1").if_integer() == 1);
  CHECK(ok("-9007199254740992").if_integer() == -9007199254740992);
  CHECK_FALSE(ok("9007199254740994").if_integer());  // above 2^53: not exact
  CHECK_FALSE(ok("1.5").if_integer());
  CHECK_FALSE(ok("\"1\"").if_integer());
}

TEST_CASE("JSON: malformed input is an error with an offset", "[ir][json]") {
  CHECK(fails("").offset == 0);
  CHECK(fails("[1,]").offset == 3);
  CHECK(fails("{\"a\":1,}").offset == 7);
  CHECK(fails("[1 2]").offset == 3);
  CHECK(fails("nul").offset == 0);
  CHECK(fails("1 2").offset == 2);  // trailing content
  for (const char* bad :
       {"01", "1.", ".5", "+1", "-", "1e", "1e+", "0x10", "NaN", "Infinity", "1e400", "-1e400"}) {
    fails(bad);
  }
  for (const char* bad : {"\"abc", "\"\\x\"", "\"\\u12\"", "\"\\ud800\"", "\"\\udc00x\"",
                          "\"\\ud800\\u0041\"", "\"a\tb\"", "'a'"}) {
    fails(bad);
  }
  // Invalid UTF-8: stray continuation, overlong, encoded surrogate, beyond U+10FFFF, truncated.
  for (const char* bad :
       {"\"\x80\"", "\"\xc0\xaf\"", "\"\xed\xa0\x80\"", "\"\xf4\x90\x80\x80\"", "\"\xe2\x82\""}) {
    fails(bad);
  }
  // I-JSON: no noncharacters, escaped or raw.
  for (const char* bad : {"\"\\ufffe\"", "\"\\uFDD0\"", "\"\\ud83f\\udfff\"", "\"\xef\xbf\xbf\"",
                          "\"\xef\xb7\x90\"", "\"\xf4\x8f\xbf\xbe\""}) {
    CHECK(fails(bad).message.find("noncharacter") != std::string::npos);
  }
  CHECK(*ok("\"\\ufdcf\\ufdf0\\ufffd\"").if_string() ==
        "\ufdcf\ufdf0\ufffd");  // neighbours are fine
  fails("\xef\xbb\xbf{}");      // a byte order mark
  CHECK(fails("{\"a\":1,\"b\":2,\"a\":3}").message.find("duplicate") != std::string::npos);
  fails("{1:2}");
  fails("{\"a\" 1}");
}

TEST_CASE("JSON: nesting is bounded", "[ir][json]") {
  const std::string deep_ok = std::string(kMaxDepth, '[') + std::string(kMaxDepth, ']');
  CHECK(ok(deep_ok).if_array());
  const std::string too_deep = "[" + deep_ok + "]";
  CHECK(fails(too_deep).message.find("deep") != std::string::npos);
}

TEST_CASE("JSON: write reads back to the same value", "[ir][json]") {
  const std::string text = R"({
  "format": "nightfall.part",
  "version": 1,
  "empty": [],
  "none": {},
  "params": [
    {
      "name": "wall",
      "value": "3 mm",
      "x": 0.1,
      "ok": true,
      "z": null
    }
  ],
  "text": "café \"q\" \\ \n \u0001"
}
)";
  const Value v = ok(text);
  const std::string written = write(v);
  CHECK(written == text);  // two-space indentation, member order kept, minimal escapes
  CHECK(ok(written) == v);
}

TEST_CASE("JSON: canonical form follows RFC 8785 (JCS)", "[ir][json]") {
  // RFC 8785 §3.2.2 example.
  const Value v = ok(R"({
    "numbers": [333333333.33333329, 1E30, 4.50, 2e-3, 0.000000000000000000000000001],
    "string": "\u20ac$\u000F\u000aA'\u0042\u0022\u005c\\\"\/",
    "literals": [null, true, false]
  })");
  CHECK(canonical(v) ==
        "{\"literals\":[null,true,false],\"numbers\":[333333333.3333333,1e+30,4.5,0.002,1e-27],"
        "\"string\":\"\u20ac$\\u000f\\nA'B\\\"\\\\\\\\\\\"/\"}");

  // RFC 8785 §3.2.3: keys sort by UTF-16 code units (the emoji's surrogates sort below U+FB33).
  const Value keys = ok(R"({"\u20ac":0,"\r":0,"\ufb33":0,"1":0,"\ud83d\ude00":0,"\u0080":0,
                           "\u00f6":0})");
  CHECK(canonical(keys) ==
        "{\"\\r\":0,\"1\":0,\"\u0080\":0,\"\u00f6\":0,\"\u20ac\":0,"
        "\"\U0001F600\":0,\"\ufb33\":0}");
}

TEST_CASE("JSON: numbers format as ECMAScript does (RFC 8785 appendix B)", "[ir][json]") {
  struct Case {
    std::uint64_t bits;
    const char* text;
  };
  for (const Case c : {Case{0x0000000000000000, "0"},
                       {0x8000000000000000, "0"},
                       {0x0000000000000001, "5e-324"},
                       {0x8000000000000001, "-5e-324"},
                       {0x7fefffffffffffff, "1.7976931348623157e+308"},
                       {0x4340000000000000, "9007199254740992"},
                       {0xc340000000000000, "-9007199254740992"},
                       {0x4430000000000000, "295147905179352830000"},
                       {0x44b52d02c7e14af5, "9.999999999999997e+22"},
                       {0x44b52d02c7e14af6, "1e+23"},
                       {0x44b52d02c7e14af7, "1.0000000000000001e+23"},
                       {0x444b1ae4d6e2ef4e, "999999999999999700000"},
                       {0x444b1ae4d6e2ef4f, "999999999999999900000"},
                       {0x444b1ae4d6e2ef50, "1e+21"},
                       {0x3eb0c6f7a0b5ed8c, "9.999999999999997e-7"},
                       {0x3eb0c6f7a0b5ed8d, "0.000001"},
                       {0x41b3de4355555553, "333333333.3333332"},
                       {0x41b3de4355555554, "333333333.33333325"},
                       {0x41b3de4355555555, "333333333.3333333"},
                       {0x41b3de4355555556, "333333333.3333334"},
                       {0x41b3de4355555557, "333333333.33333343"},
                       {0xbecbf647612f3696, "-0.0000033333333333333333"},
                       {0x43143ff3c1cb0959, "1424953923781206.2"}}) {
    const double x = std::bit_cast<double>(c.bits);
    INFO(c.text);
    CHECK(format_number(x) == c.text);
    CHECK(*ok(c.text).if_number() == x);  // and reads back exactly
  }
  CHECK(format_number(1.5) == "1.5");
  CHECK(format_number(100) == "100");
  CHECK(format_number(1e-7) == "1e-7");
}

TEST_CASE("JSON: values compare by content", "[ir][json]") {
  CHECK(ok("[1, {\"a\": \"x\"}]") == ok("[1.0,{\"a\":\"x\"}]"));
  CHECK_FALSE(ok("{\"a\":1,\"b\":2}") == ok("{\"b\":2,\"a\":1}"));  // order matters
  CHECK(Value(Array{Value(1.0), Value("a"), Value(true), Value()}) == ok("[1,\"a\",true,null]"));
  CHECK(Value(Object{{"k", Value(2.0)}}) == ok("{\"k\":2}"));
  // Values hold only finite numbers, so everything built can be written as JSON.
  CHECK_THROWS_AS(Value(std::numeric_limits<double>::quiet_NaN()), std::domain_error);
  CHECK_THROWS_AS(Value(-std::numeric_limits<double>::infinity()), std::domain_error);
}

TEST_CASE("JSON: mutated input never crashes and what parses round-trips", "[ir][json]") {
  const std::string seed =
      R"({"format":"nightfall.part","version":1,"params":[{"name":"w","value":"2 mm"}],)"
      R"("features":[{"id":"s","inputs":{"at":[0,-1.5e3,true,null],"t":"é😀"}}]})";
  std::mt19937 rng(20261017);
  int parsed = 0;
  for (int round = 0; round < 5000; ++round) {
    std::string s = seed;
    for (int edits = 1 + static_cast<int>(rng() % 4); edits > 0; --edits) {
      const std::size_t at = rng() % s.size();
      switch (rng() % 3) {
        case 0:
          s[at] = static_cast<char>(rng() % 256);
          break;
        case 1:
          s.erase(at, 1);
          break;
        default:
          s.insert(at, 1, "{}[]\",:\\0-e."[rng() % 12]);
      }
    }
    const auto v = parse(s);
    if (!v) {
      CHECK(v.error().offset <= s.size());
      continue;
    }
    ++parsed;
    const auto again = parse(write(*v));
    REQUIRE(again.has_value());
    CHECK(*again == *v);
    CHECK(canonical(*parse(canonical(*v))) == canonical(*v));
  }
  CHECK(parsed > 50);  // the mutations do not only produce garbage
}
