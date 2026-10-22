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
/// RFC 0013: a part definition placed by occurrences. Exactly one of `features` (an inline
/// feature list, with the root list's schema and its own ID scope) or `reference` (the ID of a
/// `references` entry naming a part document) is present.
struct Component {
  std::string id;
  std::optional<std::vector<Feature>> features;
  std::optional<std::string> reference;
  json::Object params;            // parameter overrides for every occurrence: name -> value
  std::optional<double> density;  // kg/m^3, for mass properties; an occurrence's overrides it
  json::Object extensions;
};
/// RFC 0013: a placed instance. `component` is a component ID or "root", the document's own
/// features. `placement` is the solver's starting point: {"translation": three lengths,
/// "rotation": three angles about the fixed x, y and z axes, applied in that order}.
struct Occurrence {
  std::string id, component;
  json::Object params;  // parameter overrides for this occurrence: name -> value
  std::optional<bool> grounded;
  std::optional<json::Value> placement;
  std::optional<double> density;  // kg/m^3; overrides the component's
  json::Object extensions;
};
/// One end of a joint: a frame derived from the single entity `frame` selects in `occurrence`
/// (an occurrence ID, or a path such as "arm/upper" into a referenced assembly).
struct JointEnd {
  std::string occurrence;
  json::Value frame;                  // an RFC 0005 selector; one entity is implied
  std::optional<std::string> rule;    // plane_centroid, cylinder_axis, circle_center, ...
  std::optional<json::Value> offset;  // {"translation": three lengths, "rotation": three angles}
  std::optional<bool> flip;
  json::Object extensions;
};
/// RFC 0013: one joint per pair of frames, carrying the degrees of freedom of its kind.
/// `value` drives them: one value for a single degree of freedom, or a list with one value or
/// null per degree of freedom; `limits` likewise takes [low, high] ranges.
struct Joint {
  std::string id, kind;
  JointEnd a, b;
  std::optional<json::Value> value, limits;
  json::Object extensions;
};

struct Document {
  Units units;
  Metadata meta;
  std::vector<Parameter> params;
  std::vector<Reference> references;
  std::vector<Feature> features;
  std::vector<Component> components;  // RFC 0013 sections, omitted from files when empty
  std::vector<Occurrence> occurrences;
  std::vector<Joint> joints;
  std::optional<std::string> history_end;
  json::Object ui, extensions;
};

inline constexpr int kDocumentVersion = 2;
inline constexpr std::size_t kMaxDocumentBytes = 16 * 1024 * 1024;

/// Loads a document from UTF-8 JSON, bounded by kMaxDocumentBytes and json::kMaxDepth.
/// Checks the format/version first; version 1 migrates forward (migration.hpp) before any
/// field is read, so its diagnostics keep their pointers; newer versions are refused without
/// reinterpretation. Collects independent errors, checks unique IDs and typed, backward-only
/// input references, and resolves history_end. Unknown keys must start with "x-"; UI and x-
/// extension values are opaque. Feature inputs follow the published schema.
/// Parameter values/bounds must resolve to finite quantities with compatible dimensions and
/// an acyclic value graph; their authored strings are preserved, not replaced with numbers.
/// Version 2 (RFC 0013) adds optional components, occurrences and joints: inline component
/// features resolve in their own scope; occurrences name a component or "root"; joint ends
/// name declared occurrences and frame selectors resolve in that occurrence's component
/// (referenced components are not opened); override, placement, offset, value and limit
/// dimensions are checked, with one value or range per degree of freedom of the joint kind.
/// No geometry is evaluated or external file accessed.
/// All nine original top-level fields are required; meta and the collections may be empty.
/// Reports at most 128 diagnostics per validation stage.
[[nodiscard]] std::expected<Document, Errors> read_document(std::string_view text);

/// Checks an edited envelope and writes the current version in RFC field order, two-space
/// indentation and LF, preserving extensions, UI and inputs. Invalid edits return errors.
/// Optional fields preserve omission; a default-constructed Document is an empty part.
[[nodiscard]] std::expected<std::string, Errors> write_document(const Document& document);

/// The published JSON Schema (2020-12) of a current-version part document, embedded at build
/// time. The version-1 schema stays published beside it.
[[nodiscard]] std::string_view part_schema() noexcept;

}  // namespace nf::ir
