// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "nf/ir/json.hpp"

namespace nf::ir {

/// A document diagnostic. Codes here are parse, schema or unknown_id; pointer is RFC 6901.
/// JSON syntax errors use the root pointer and a byte offset; other errors name the field.
struct Error {
  std::string code, message, pointer;
  std::optional<std::size_t> offset;
};
using Errors = std::vector<Error>;

struct Units {
  std::string length = "mm", angle = "deg";
  json::Object extensions;
};
struct Metadata {
  std::optional<std::string> name, doc;
  json::Object extensions;
};
struct Parameter {
  std::string name;
  json::Value value;
  std::optional<json::Value> min, max;
  std::optional<std::string> doc;
  json::Object extensions;
};
struct Reference {
  std::string id, kind, path, sha256;
  json::Object extensions;
};
struct Feature {
  std::string id, type;
  std::optional<std::string> name;
  std::optional<bool> suppressed;
  json::Value inputs = json::Object{};
  json::Object extensions;
};

/// The RFC 0005 v1 document. Numeric expressions and feature inputs stay as JSON:
/// read_document enforces the published per-feature schemas. Expression semantics (#91),
/// body/entity IDs and input references remain separate validation work in #90.
/// References are metadata only: reading a document never opens a referenced path.
struct Document {
  Units units;
  Metadata meta;
  std::vector<Parameter> params;
  std::vector<Reference> references;
  std::vector<Feature> features;
  std::optional<std::string> history_end;
  json::Object ui, extensions;
};

inline constexpr int kDocumentVersion = 1;
inline constexpr std::size_t kMaxDocumentBytes = 16 * 1024 * 1024;

/// Loads a v1 envelope from UTF-8 JSON, bounded by kMaxDocumentBytes and json::kMaxDepth.
/// Checks the format/version first; rejects older (none supported yet) and newer versions
/// without reinterpretation. Collects independent field errors, checks unique IDs within
/// each envelope namespace and resolves history_end. Unknown keys must start with "x-";
/// UI and x- extension values are opaque. Feature inputs follow the published v1 schema.
/// No geometry is evaluated or external file accessed.
/// All nine top-level fields are required; meta and the three collections may be empty.
[[nodiscard]] std::expected<Document, Errors> read_document(std::string_view text);

/// Checks an edited envelope and writes the current version in RFC field order, two-space
/// indentation and LF, preserving extensions, UI and inputs. Invalid edits return errors.
/// Optional fields preserve omission; a default-constructed Document is an empty part.
[[nodiscard]] std::expected<std::string, Errors> write_document(const Document& document);

}  // namespace nf::ir
