// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include "nf/ir/hash.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <algorithm>
#include <limits>

namespace nf::ir {
std::string Hash::hex() const {
  constexpr std::string_view digits = "0123456789abcdef";
  std::string result;
  result.reserve(bytes.size() * 2);
  for (auto b : bytes) {
    result += digits[b >> 4];
    result += digits[b & 15];
  }
  return result;
}

std::optional<Hash> Hash::from_hex(std::string_view text) {
  Hash result;
  if (text.size() != result.bytes.size() * 2) return std::nullopt;
  const auto digit = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  for (std::size_t i = 0; i < result.bytes.size(); ++i) {
    const auto high = digit(text[2 * i]), low = digit(text[2 * i + 1]);
    if (high < 0 || low < 0) return std::nullopt;
    result.bytes[i] = static_cast<std::uint8_t>(16 * high + low);
  }
  return result;
}

Hash sha256(std::string_view bytes) {
  CC_SHA256_CTX context;
  CC_SHA256_Init(&context);
  while (!bytes.empty()) {
    const auto size = std::min(bytes.size(), std::size_t(std::numeric_limits<CC_LONG>::max()));
    CC_SHA256_Update(&context, bytes.data(), static_cast<CC_LONG>(size));
    bytes.remove_prefix(size);
  }
  Hash result;
  CC_SHA256_Final(result.bytes.data(), &context);
  return result;
}

Hash feature_hash(std::string_view version, std::string_view type, const json::Value& inputs,
                  std::span<const Hash> upstream) {
  json::Array hashes;
  hashes.reserve(upstream.size());
  for (const auto& hash : upstream) hashes.emplace_back(hash.hex());
  return sha256(json::canonical(json::Object{{"evaluator_version", std::string(version)},
                                             {"type", std::string(type)},
                                             {"inputs", inputs},
                                             {"upstream", std::move(hashes)}}));
}

Hash body_hash(std::optional<Hash> previous, Hash feature) {
  return sha256(json::canonical(
      json::Object{{"previous", previous ? json::Value(previous->hex()) : json::Value()},
                   {"feature", feature.hex()}}));
}
}  // namespace nf::ir
