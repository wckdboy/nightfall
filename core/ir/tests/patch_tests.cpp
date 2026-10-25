// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "nf/ir/patch.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace nf::ir;
using json::Value;

namespace {
Value parse(const char* text) { return json::parse(text).value(); }
std::string patched(const char* document, const char* patch) {
  const auto result = apply_patch(parse(document), parse(patch));
  if (!result) {
    for (const auto& e : result.error()) WARN(e.pointer << ": " << e.message);
  }
  REQUIRE(result);
  return json::canonical(*result);
}
Error failure(const char* document, const char* patch) {
  const auto result = apply_patch(parse(document), parse(patch));
  REQUIRE_FALSE(result);
  REQUIRE(result.error().size() == 1);
  CHECK(result.error().front().code == "patch");
  return result.error().front();
}
}  // namespace

TEST_CASE("add inserts members and array elements and replaces existing members", "[ir][patch]") {
  CHECK(patched(R"({"a":1})", R"([{"op":"add","path":"/b","value":[1,2]}])") ==
        R"({"a":1,"b":[1,2]})");
  CHECK(patched(R"({"a":1})", R"([{"op":"add","path":"/a","value":"x"}])") == R"({"a":"x"})");
  CHECK(patched(R"({"a":[1,3]})", R"([{"op":"add","path":"/a/1","value":2}])") ==
        R"({"a":[1,2,3]})");
  CHECK(patched(R"({"a":[1]})", R"([{"op":"add","path":"/a/-","value":2}])") == R"({"a":[1,2]})");
  CHECK(patched(R"({"a":[1]})", R"([{"op":"add","path":"/a/1","value":2}])") == R"({"a":[1,2]})");
  CHECK(patched(R"({"a":{"b":{}}})", R"([{"op":"add","path":"/a/b/c","value":null}])") ==
        R"({"a":{"b":{"c":null}}})");
  CHECK(patched(R"({"a":1})", R"([{"op":"add","path":"","value":{"whole":true}}])") ==
        R"({"whole":true})");
  CHECK(failure(R"({"a":[1]})", R"([{"op":"add","path":"/a/2","value":2}])").message ==
        "add /a/2: array index out of range");
  CHECK(failure(R"({"a":{}})", R"([{"op":"add","path":"/a/b/c","value":1}])").message ==
        "add /a/b/c: path does not exist");
  CHECK(failure(R"({"a":1})", R"([{"op":"add","path":"/a/b","value":1}])").message ==
        "add /a/b: path crosses a scalar");
  CHECK(failure(R"({"a":[1]})", R"([{"op":"add","path":"/a/01","value":1}])").message ==
        "add /a/01: invalid array index '01'");
  CHECK(failure(R"({})", R"([{"op":"add","path":"/a"}])").message == "add /a: value is required");
}

TEST_CASE("remove and replace require an existing target and refuse the root", "[ir][patch]") {
  CHECK(patched(R"({"a":1,"b":2})", R"([{"op":"remove","path":"/a"}])") == R"({"b":2})");
  CHECK(patched(R"({"a":[1,2,3]})", R"([{"op":"remove","path":"/a/1"}])") == R"({"a":[1,3]})");
  CHECK(patched(R"({"a":[1,2]})", R"([{"op":"replace","path":"/a/1","value":9}])") ==
        R"({"a":[1,9]})");
  CHECK(patched(R"({"a":1})", R"([{"op":"replace","path":"","value":[]}])") == "[]");
  CHECK(failure(R"({"a":1})", R"([{"op":"remove","path":"/b"}])").message ==
        "remove /b: path does not exist");
  CHECK(failure(R"({"a":[1]})", R"([{"op":"remove","path":"/a/-"}])").message ==
        "remove /a/-: invalid array index '-'");
  CHECK(failure(R"({"a":1})", R"([{"op":"remove","path":""}])").message ==
        "remove : cannot remove the document root");
  CHECK(failure(R"({"a":1})", R"([{"op":"replace","path":"/b","value":1}])").message ==
        "replace /b: path does not exist");
}

TEST_CASE("move and copy carry values between locations", "[ir][patch]") {
  CHECK(patched(R"({"a":{"x":1},"b":[]})", R"([{"op":"move","from":"/a/x","path":"/b/-"}])") ==
        R"({"a":{},"b":[1]})");
  CHECK(patched(R"({"a":[1,2,3]})", R"([{"op":"move","from":"/a/0","path":"/a/2"}])") ==
        R"({"a":[2,3,1]})");
  CHECK(patched(R"({"a":1})", R"([{"op":"move","from":"/a","path":"/a"}])") == R"({"a":1})");
  CHECK(patched(R"({"a":{"deep":[1]}})", R"([{"op":"copy","from":"/a","path":"/b"}])") ==
        R"({"a":{"deep":[1]},"b":{"deep":[1]}})");
  CHECK(failure(R"({"a":{"b":1}})", R"([{"op":"move","from":"/a","path":"/a/b"}])").message ==
        "move /a/b: cannot move a value into itself");
  CHECK(failure(R"({"a":1})", R"([{"op":"copy","from":"/zz","path":"/b"}])").message ==
        "copy /b: from /zz does not exist");
  CHECK(failure(R"({"a":1})", R"([{"op":"copy","path":"/b"}])").message ==
        "copy /b: from must be a string");
}

TEST_CASE("test compares values and pointers unescape", "[ir][patch]") {
  CHECK(patched(R"({"a":[1,{"b":2}]})", R"([{"op":"test","path":"/a/1","value":{"b":2}}])") ==
        R"({"a":[1,{"b":2}]})");
  CHECK(failure(R"({"a":1})", R"([{"op":"test","path":"/a","value":2}])").message ==
        "test /a: value differs");
  // Object members compare regardless of order, at any depth (RFC 6902 §4.6).
  CHECK(patched(R"({"a":{"b":1,"c":[{"d":2,"e":3}]}})",
                R"([{"op":"test","path":"/a","value":{"c":[{"e":3,"d":2}],"b":1}}])") ==
        R"({"a":{"b":1,"c":[{"d":2,"e":3}]}})");
  CHECK(
      failure(R"({"a":{"b":1}})", R"([{"op":"test","path":"/a","value":{"b":1,"c":2}}])").message ==
      "test /a: value differs");
  CHECK(failure(R"({"a":1})", R"([{"op":"test","path":"/b","value":1}])").message ==
        "test /b: path does not exist");
  CHECK(patched(R"({"a/b":1,"m~n":2})",
                R"([{"op":"remove","path":"/a~1b"},{"op":"test","path":"/m~0n","value":2}])") ==
        R"({"m~n":2})");
  CHECK(failure(R"({})", R"([{"op":"add","path":"/a~2","value":1}])").message ==
        "add /a~2: invalid escape in pointer");
  CHECK(failure(R"({})", R"([{"op":"add","path":"a","value":1}])").message ==
        "add a: pointer must start with '/'");
}

TEST_CASE("patches are atomic and malformed operations name their index", "[ir][patch]") {
  const auto document = parse(R"({"a":1})");
  const auto result = apply_patch(
      document, parse(R"([{"op":"add","path":"/b","value":2},{"op":"remove","path":"/zz"}])"));
  REQUIRE_FALSE(result);
  CHECK(result.error().front().pointer == "/patch/1");
  CHECK(json::canonical(document) == R"({"a":1})");
  CHECK(failure(R"({})", R"({"op":"add"})").pointer == "/patch");
  CHECK(failure(R"({})", R"([1])").message == "operation must be an object");
  CHECK(failure(R"({})", R"([{"path":"/a"}])").message == "op must be a string");
  CHECK(failure(R"({})", R"([{"op":"add"}])").message == "path must be a string");
  CHECK(failure(R"({})", R"([{"op":"frob","path":"/a"}])").message == "unknown op 'frob'");
  CHECK(failure(R"({})", R"([{"op":"frob","path":"/a"}])").pointer == "/patch/0");
  CHECK(patched(R"({"a":1})", "[]") == R"({"a":1})");
}

TEST_CASE("a patched empty part still reads as a valid document", "[ir][patch]") {
  const auto empty = write_document(Document{}).value();
  const auto result =
      apply_patch(parse(empty.c_str()),
                  parse(R"([{"op":"add","path":"/params/-","value":{"name":"wall","value":"3 mm"}},
               {"op":"replace","path":"/meta","value":{"name":"Patched"}}])"));
  REQUIRE(result);
  const auto document = read_document(json::write(*result));
  REQUIRE(document);
  REQUIRE(document->params.size() == 1);
  CHECK(document->params.front().name == "wall");
  CHECK(document->meta.name == "Patched");
}
