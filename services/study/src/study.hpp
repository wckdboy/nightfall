// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// RFC 0014 study runner: evaluates a document, resolves one study's body, faces, materials and
// boundary values, meshes the body with Netgen, solves with MFEM and reports quantities,
// criteria and provenance. No OCCT, Netgen or MFEM headers here.
#pragma once

#include <expected>
#include <optional>
#include <string>
#include <vector>

#include "evaluator.hpp"

namespace nf::study {
struct RunOptions {
  std::optional<double> size;  // mm, overriding the study's mesh size
  std::vector<double> refine;  // mm: run at each size and report convergence
  std::string field;           // when set: write the final run's surface field there (JSON, #321)
};
/// Runs study `id` of `source` and returns its result object, or errors with JSON Pointers
/// into /studies.
std::expected<ir::json::Object, ir::Errors> run_study(kernel_service::Evaluator& evaluator,
                                                      kernel::IKernel& kernel,
                                                      const std::string& source,
                                                      const std::string& id,
                                                      const RunOptions& options = {});
/// RFC 0014's study object run on the one solid of `step` (STEP text), its boundaries' faces
/// picked by `{"points": [[x, y, z], ...]}` on the part (#317). Mesh sizes and displacement
/// limits are "<number> <length unit>" strings. Errors carry JSON Pointers into `spec`.
std::expected<ir::json::Object, ir::Errors> run_step_study(kernel::IKernel& kernel,
                                                           const std::string& step,
                                                           const std::string& spec,
                                                           const RunOptions& options = {});
/// The `nf study` command: `nf study run <document> <study> [--size L] [--refine a,b,c]`.
int run_study_command(int argc, char** argv);
}  // namespace nf::study
