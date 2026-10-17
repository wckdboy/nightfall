// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "nf/ir/hash.hpp"

using namespace nf::ir;

TEST_CASE("IR hashes: SHA-256 known answers and binary inputs", "[ir][hash]") {
  CHECK(sha256("").hex() == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK(sha256("abc").hex() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK(sha256(std::string(1000000, 'a')).hex() ==
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
  CHECK(sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq").hex() ==
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  CHECK(sha256(std::string_view("a\0b", 3)) != sha256("a"));
}

TEST_CASE("IR hashes: external digests normalize case and reject malformed input", "[ir][hash]") {
  const auto original = sha256("external file");
  auto text = original.hex();
  for (char& c : text)
    if (c >= 'a' && c <= 'f') c = static_cast<char>(c - 'a' + 'A');
  REQUIRE(Hash::from_hex(text).has_value());
  CHECK(*Hash::from_hex(text) == original);
  CHECK(Hash::from_hex(text)->hex() == original.hex());
  CHECK_FALSE(Hash::from_hex(""));
  CHECK_FALSE(Hash::from_hex(text.substr(1)));
  CHECK_FALSE(Hash::from_hex(text + '0'));
  text[31] = 'g';
  CHECK_FALSE(Hash::from_hex(text));
  CHECK(Hash{}.hex() == std::string(64, '0'));
}

TEST_CASE("IR hashes: feature keys use the exact canonical envelope", "[ir][hash]") {
  const auto inputs = json::parse(R"({"result":{"new":"plate","body_type":"brep"},"distance":10})");
  REQUIRE(inputs.has_value());
  const auto upstream = sha256("sketch");
  const auto expected = sha256(
      "{\"evaluator_version\":\"nf-1/occt-7.9.3\",\"inputs\":{\"distance\":10,"
      "\"result\":{\"body_type\":\"brep\",\"new\":\"plate\"}},\"type\":\"extrude\","
      "\"upstream\":[\"" +
      upstream.hex() + "\"]}");
  CHECK(feature_hash("nf-1/occt-7.9.3", "extrude", *inputs, {&upstream, 1}) == expected);
  const auto reordered =
      json::parse(R"({"distance":1e1,"result":{"body_type":"brep","new":"plate"}})");
  REQUIRE(reordered.has_value());
  CHECK(feature_hash("nf-1/occt-7.9.3", "extrude", *reordered, {&upstream, 1}) == expected);
  CHECK(feature_hash("nf-2/occt-7.9.3", "extrude", *inputs, {&upstream, 1}) != expected);
  CHECK(feature_hash("nf-1/occt-7.9.3", "revolve", *inputs, {&upstream, 1}) != expected);
  CHECK(feature_hash("nf-1/occt-7.9.3", "extrude", *inputs, {}) != expected);
}

TEST_CASE("IR hashes: arrays and upstream ordering affect content", "[ir][hash]") {
  const auto a = sha256("a"), b = sha256("b");
  std::array upstream{a, b};
  const auto key = feature_hash("1", "boolean", json::Object{}, upstream);
  std::ranges::reverse(upstream);
  CHECK(feature_hash("1", "boolean", json::Object{}, upstream) != key);
  CHECK(feature_hash("1", "transform", json::Array{1.0, 2.0}, {}) !=
        feature_hash("1", "transform", json::Array{2.0, 1.0}, {}));
  CHECK(feature_hash("1", "transform", -0.0, {}) == feature_hash("1", "transform", 0.0, {}));
}

TEST_CASE("IR hashes: body state includes its complete modification history", "[ir][hash]") {
  const auto base = sha256("extrude"), cut = sha256("cut"), move = sha256("move");
  const auto initial = body_hash(std::nullopt, base);
  CHECK(initial == sha256("{\"feature\":\"" + base.hex() + "\",\"previous\":null}"));
  const auto modified = body_hash(initial, cut);
  CHECK(modified ==
        sha256("{\"feature\":\"" + cut.hex() + "\",\"previous\":\"" + initial.hex() + "\"}"));
  CHECK(body_hash(modified, move) != body_hash(initial, move));
  CHECK(body_hash(modified, move) != body_hash(body_hash(initial, move), cut));
  CHECK(body_hash(std::nullopt, base) != body_hash(Hash{}, base));
}
