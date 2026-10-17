// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "nf/ir/json.hpp"

namespace nf::ir {
/// SHA-256 content identity; no native byte order or platform-dependent serialization.
struct Hash {
  std::array<std::uint8_t, 32> bytes{};
  bool operator==(const Hash&) const = default;
  [[nodiscard]] std::string hex() const;
  /// Exactly 64 ASCII hexadecimal digits, accepting either case. hex() emits lowercase.
  [[nodiscard]] static std::optional<Hash> from_hex(std::string_view text);
};

/// Hashes all bytes, including embedded NULs. Uses the OS CommonCrypto implementation.
[[nodiscard]] Hash sha256(std::string_view bytes);

/// RFC 0005 feature identity: SHA-256 of the JCS object with evaluator_version, type,
/// inputs and upstream keys. Inputs must be validated I-JSON with expressions evaluated
/// to mm/radians/scalars before hashing. This low-level function does not evaluate them.
/// Upstream hashes must have a deterministic order, reflecting their input bindings.
/// The caller's version must cover evaluator semantics, kernel version and configuration.
/// Feature ID, display metadata, UI and parameters are deliberately absent from this API.
[[nodiscard]] Hash feature_hash(std::string_view evaluator_version, std::string_view type,
                                const json::Value& evaluated_inputs,
                                std::span<const Hash> upstream);

/// SHA-256 of the JCS object {feature: hex, previous: hex|null}. A new body has no previous
/// state; every modifier includes the preceding state, not just the body's creating feature.
[[nodiscard]] Hash body_hash(std::optional<Hash> previous, Hash feature);
}  // namespace nf::ir
