// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "evaluator.hpp"

namespace nf::kernel_service {
/// A rigid placement: world point = rotation (row-major) * local point + translation, in mm.
struct Placement {
  std::array<double, 9> rotation{1, 0, 0, 0, 1, 0, 0, 0, 1};
  math::Vec3 translation;
};
/// One joint after solving: largest residuals of its constrained coordinates, and what an
/// exporter needs: its kind, occurrence indices, each end's frame in its own occurrence's
/// coordinates (rotation columns are the frame's x, y and z axes) and the first degree of
/// freedom's limits (rad or mm).
struct AssemblyJoint {
  std::string id;
  double residual_mm = 0, residual_rad = 0;
  bool redundant = false, conflicting = false;
  std::string kind;
  std::size_t a = 0, b = 0;
  Placement frame_a, frame_b;
  std::optional<std::pair<double, double>> limits;
};
/// A placed occurrence and its bodies at the solved placement ("occurrence/body").
struct AssemblyOccurrence {
  std::string id;
  Placement placement;
  std::vector<std::pair<std::string, kernel::ShapePtr>> bodies;
  /// The component body's state hash for each entry of `bodies`, in the same order.
  std::vector<std::optional<ir::Hash>> source_hashes;
  /// The component body's generative names rebound to the placed shape, per entry of `bodies`,
  /// so selectors, topology and display see the same names as the component (#328).
  std::vector<std::vector<NamedEntity>> body_names;
};
/// RFC 0013 assembly evaluation.
struct AssemblyResult {
  bool solved = false;
  std::size_t dof = 0, loops = 0, iterations = 0;
  std::vector<AssemblyJoint> joints;
  std::vector<AssemblyOccurrence> occurrences;
  /// Work of the distinct component evaluations, for build evidence.
  std::size_t kernel_calls = 0, cache_hits = 0, binding_misses = 0;
  std::vector<FeatureStatus> component_features;
};

/// Evaluates the occurrences, joints and components of a v2 document (RFC 0013). Each distinct
/// component and parameter set evaluates once through `evaluator` (sharing its cache); joint
/// frames derive from `{created_by, role, from}` selectors on the component's bodies by the
/// plane_centroid and cylinder_axis rules, with offset and flip; the joint solver places every
/// non-grounded occurrence from its authored placement; bodies are published at their solved
/// placements. Errors carry JSON Pointers into /occurrences or /joints. Referenced components,
/// other selector forms and edge/vertex rules are reported as unsupported. A document without
/// occurrences returns an empty result.
std::expected<AssemblyResult, ir::Errors> evaluate_assembly(Evaluator& evaluator,
                                                            kernel::IKernel& kernel,
                                                            const ir::Document& document);
}  // namespace nf::kernel_service
