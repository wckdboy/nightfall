// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <span>
#include "nf/ir/document.hpp"
#include "nf/ir/expression.hpp"

namespace nf::ir::detail {
struct Link {
  std::string kind, target, pointer;
  bool definition = false;
};
struct ValueField {
  std::string kind, pointer;
  const json::Value* value;  // Borrowed from the checked document; valid only while it lives.
};
struct SchemaResult {
  Errors errors;
  std::vector<Link> links;
  std::vector<ValueField> fields;
};
// Validates only our embedded schema's vocabulary, not arbitrary third-party schemas.
// Immutable compiled patterns are shared across calls; unsupported schema keywords fail
// at initialization. Instance validation has depth, work and diagnostic bounds.
[[nodiscard]] SchemaResult check_schema(const json::Value& document,
                                        std::size_t work_limit = 1000000);
// Checks one feature's schema, without resolving its references in a containing document.
[[nodiscard]] SchemaResult check_feature(const json::Value& feature);
// Requires a structurally valid document and links produced by check_schema.
[[nodiscard]] Errors check_links(const Document& document, std::span<const Link> links);
// RFC 0013 sections: component references and overrides, occurrence components and placements,
// joint ends, frame selectors resolved in the joined occurrence's component scope, and value
// and limit dimensions per degree of freedom. `links` are all of check_schema's links (scope
// definitions); `assembly` are those outside root and inline-component features.
[[nodiscard]] Errors check_assembly(const Document& document, std::span<const Link> links,
                                    std::span<const Link> assembly, const ParameterValues& params);
}  // namespace nf::ir::detail
