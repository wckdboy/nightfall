// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// One in-process document whose authoritative state is the IR source text (RFC 0011, Track 1).
// Every mutation validates, rebuilds through the service evaluator and reports the same
// factual evidence record `nf build` writes. A failed mutation leaves the document unchanged.
#pragma once

#include <expected>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "evaluator.hpp"

namespace nf::cli {

/// Reads a regular file of at most ir::kMaxDocumentBytes; throws ir::Errors (io/parse).
std::string load(const std::filesystem::path& path);
/// Writes text to a temporary sibling, fsyncs and renames it over `output`; throws ir::Errors.
void publish(const std::filesystem::path& output, std::string_view text);
/// Replaces named parameter values and revalidates the whole document.
[[nodiscard]] std::expected<std::string, ir::Errors> apply_overrides(
    std::string source, std::map<std::string, std::string> values);
/// Feature status totals and per-body metrics in the `nf build` evidence layout.
[[nodiscard]] ir::json::Object feature_counts(const kernel_service::Evaluation& evaluation);
[[nodiscard]] ir::json::Object body_metrics(const std::string& id,
                                            const kernel_service::BodyState& body);
/// Diagnostics as `{code, message, pointer[, offset]}` entries.
[[nodiscard]] ir::json::Array error_entries(const ir::Errors& errors);

class Session {
 public:
  /// The outcome of one rebuild: evidence (version 1), feature states, live-body metrics and
  /// the body ids whose state hash changed, appeared or vanished since the previous state.
  struct Build {
    ir::json::Object evidence;
    ir::json::Array features, bodies, changed, added, removed;
    /// Sketches whose solved coordinates replaced authored numbers (RFC 0012 write-back):
    /// `{feature, entities, expressions}`, where `expressions` lists entities with coordinates
    /// authored as expressions, which stay as written and are solved again on every rebuild.
    ir::json::Array written_back;
    bool ok = false;
    [[nodiscard]] ir::json::Object json() const;
  };
  Session(kernel::IKernel& kernel, math::Tolerance tolerance);
  /// Validates, normalizes (read/write_document) and rebuilds `source`. Structural errors keep
  /// the previous state. With `rollback`, any failed feature also keeps it and returns the
  /// feature errors; without it the state is committed and Build::ok reports the failure.
  [[nodiscard]] std::expected<Build, ir::Errors> replace(std::string source, bool rollback);
  [[nodiscard]] std::expected<Build, ir::Errors> set_parameters(
      const std::map<std::string, std::string>& values, bool rollback);
  [[nodiscard]] std::expected<Build, ir::Errors> undo();
  [[nodiscard]] std::expected<Build, ir::Errors> redo();
  /// Forgets undo/redo history, so a document loaded at startup is the first state.
  void clear_history() noexcept { undo_.clear(), redo_.clear(); }
  /// Holds `source` although it does not validate or build, with its errors, so an agent can
  /// inspect the diagnostics and repair it. No bodies; the previous state is discarded.
  void adopt_invalid(std::string source, ir::Errors errors);
  [[nodiscard]] bool can_undo() const noexcept { return !undo_.empty(); }
  [[nodiscard]] bool can_redo() const noexcept { return !redo_.empty(); }
  [[nodiscard]] const std::string& source() const noexcept { return state_.source; }
  /// Units, metadata, parameters, features with status and errors, live bodies and the digest.
  [[nodiscard]] ir::json::Object inspect() const;
  /// Metrics of one live body, or of every live body with totals.
  [[nodiscard]] std::expected<ir::json::Object, ir::Errors> measure(
      const std::optional<std::string>& body) const;
  /// Exact STEP AP242 text of the named live bodies (all of them when empty).
  /// STEP text of the named live bodies (all of them when `bodies` is empty).
  struct StepExport {
    std::string text;
    std::size_t bodies = 0;
  };
  [[nodiscard]] std::expected<StepExport, ir::Errors> export_step(
      const std::vector<std::string>& bodies) const;
  /// Wavefront OBJ of the named live bodies (all when empty): one object per body, world-space
  /// millimetre positions and outward normals from the kernel's indexed display mesh. Derived
  /// data for viewing and downstream tools, never persisted geometry (RFC 0005).
  struct MeshExport {
    std::string obj;
    std::size_t bodies = 0, vertices = 0, triangles = 0;
  };
  [[nodiscard]] std::expected<MeshExport, ir::Errors> export_obj(
      const std::vector<std::string>& bodies, kernel::MeshingOptions options) const;
  /// Faces and edges of one live body: generative names (feature, role, from), analytic type
  /// and frame (plane origin/normal/x axis, cylinder origin/axis/radius) and adjacency. The
  /// indices are display order only; references are selectors built from the names.
  [[nodiscard]] std::expected<ir::json::Object, ir::Errors> topology(const std::string& body) const;

 private:
  struct State {
    std::string source;
    ir::Document document;
    kernel_service::Evaluation evaluation;
    Build build;
    ir::Errors errors;  // document-level errors of an adopted invalid source
  };
  /// With `write_back`, a rebuild that solved a sketch rewrites the authored plain numbers to
  /// the solved coordinates and evaluates the rewritten document instead.
  [[nodiscard]] std::expected<State, ir::Errors> evaluate(std::string source,
                                                          bool write_back = true);
  [[nodiscard]] std::expected<Build, ir::Errors> commit(std::string source, bool rollback,
                                                        std::vector<std::string>& from,
                                                        std::vector<std::string>& to);
  kernel::IKernel& kernel_;
  math::Tolerance tolerance_;
  kernel_service::Evaluator evaluator_;
  State state_;
  std::vector<std::string> undo_, redo_;
};

}  // namespace nf::cli
