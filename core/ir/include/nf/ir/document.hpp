// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "nf/ir/json.hpp"

namespace nf::ir {

/// A diagnostic: parse, schema, unit, bounds, cycle, unknown_id or forward_reference.
/// Pointers use RFC 6901.
/// The pointer names the field (or root); offsets identify bytes in JSON or expression source.
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
/// read_document enforces the published per-feature schemas, ID namespaces and ordered
/// feature/body/sketch/entity references. Parameter expressions, dependencies and bounds are
/// validated on load/save; feature-field expressions are evaluated separately (expression.hpp).
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
/// without reinterpretation. Collects independent errors, checks unique IDs and typed,
/// backward-only input references, and resolves history_end. Unknown keys must start with "x-";
/// UI and x- extension values are opaque. Feature inputs follow the published v1 schema.
/// Parameter values/bounds must resolve to finite quantities with compatible dimensions and
/// an acyclic value graph; their authored strings are preserved, not replaced with numbers.
/// No geometry is evaluated or external file accessed.
/// All nine top-level fields are required; meta and the three collections may be empty.
/// Reports at most 128 diagnostics per validation stage.
[[nodiscard]] std::expected<Document, Errors> read_document(std::string_view text);

/// Checks an edited envelope and writes the current version in RFC field order, two-space
/// indentation and LF, preserving extensions, UI and inputs. Invalid edits return errors.
/// Optional fields preserve omission; a default-constructed Document is an empty part.
[[nodiscard]] std::expected<std::string, Errors> write_document(const Document& document);

/// The published JSON Schema (2020-12) of a version-1 part document, embedded at build time.
[[nodiscard]] std::string_view part_schema() noexcept;

}  // namespace nf::ir
