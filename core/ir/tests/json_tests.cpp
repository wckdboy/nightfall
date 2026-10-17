// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>

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
  fails("\xef\xbb\xbf{}");  // a byte order mark
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

TEST_CASE("JSON: values compare by content", "[ir][json]") {
  CHECK(ok("[1, {\"a\": \"x\"}]") == ok("[1.0,{\"a\":\"x\"}]"));
  CHECK_FALSE(ok("{\"a\":1,\"b\":2}") == ok("{\"b\":2,\"a\":1}"));  // order matters
  CHECK(Value(Array{Value(1.0), Value("a"), Value(true), Value()}) == ok("[1,\"a\",true,null]"));
  CHECK(Value(Object{{"k", Value(2.0)}}) == ok("{\"k\":2}"));
  CHECK_FALSE(Value(std::numeric_limits<double>::quiet_NaN()) ==
              Value(std::numeric_limits<double>::quiet_NaN()));
}

TEST_CASE("JSON: mutated input never crashes", "[ir][json]") {
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
  }
  CHECK(parsed > 50);  // the mutations do not only produce garbage
}
