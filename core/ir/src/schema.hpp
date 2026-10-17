// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <span>
#include "nf/ir/document.hpp"

namespace nf::ir::detail {
struct Link {
  std::string kind, target, pointer;
  bool definition = false;
};
struct SchemaResult {
  Errors errors;
  std::vector<Link> links;
};
// Validates only our embedded schema's vocabulary, not arbitrary third-party schemas.
// Immutable compiled patterns are shared across calls; unsupported schema keywords fail
// at initialization. Instance validation has depth, work and diagnostic bounds.
[[nodiscard]] SchemaResult check_schema(const json::Value& document,
                                        std::size_t work_limit = 1000000);
// Requires a structurally valid document and links produced by check_schema.
[[nodiscard]] Errors check_links(const Document& document, std::span<const Link> links);
}  // namespace nf::ir::detail
