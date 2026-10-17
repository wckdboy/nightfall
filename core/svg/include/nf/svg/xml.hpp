// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// A small, strict XML reader for SVG files: elements and attributes only. Not a general XML
// parser: no DTD processing (a DOCTYPE is skipped; its entities are not defined), no
// namespace resolution (names are kept as written), and text content is discarded. Input
// is untrusted, so every malformed construct is an error and nesting depth is bounded.

#pragma once

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "nf/svg/path.hpp"  // ParseError

namespace nf::svg::xml {

struct Element {
  std::string name;  // as written, including any prefix ("svg:path")
  std::vector<std::pair<std::string, std::string>> attributes;  // values entity-decoded
  std::vector<Element> children;
  std::size_t offset = 0;  // byte offset of the start tag

  /// The attribute's decoded value, or nullptr.
  [[nodiscard]] const std::string* attribute(std::string_view attribute_name) const;
  /// The name without its prefix.
  [[nodiscard]] std::string_view local_name() const;
};

inline constexpr int kMaxDepth = 256;

/// Parses a document and returns its root element.
[[nodiscard]] std::expected<Element, ParseError> parse(std::string_view text);

}  // namespace nf::svg::xml
