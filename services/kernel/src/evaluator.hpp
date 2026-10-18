// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <map>

#include "nf/ir/document.hpp"
#include "nf/ir/hash.hpp"
#include "nf/kernel/kernel.hpp"

namespace nf::kernel_service {
enum class FeatureStatus { kBuilt, kCached, kSuppressed, kRolledBack, kFailed };
struct FeatureState {
  std::string id;
  FeatureStatus status = FeatureStatus::kFailed;
  std::optional<ir::Hash> hash;
  ir::Errors errors;
};
/// Operation-local roles rebound to this evaluation's IDs and current immutable snapshot.
/// Selector execution and sketch-entity provenance are not implemented in this first slice.
struct NamedEntity {
  std::string feature, role;
  kernel::EntityPtr entity;
};
struct BodyState {
  kernel::ShapePtr shape;
  std::optional<ir::Hash> hash;
  std::vector<NamedEntity> names;
  std::string unavailable;  // failed/consumed: input_failed; suppressed: input_suppressed
};
struct Evaluation {
  std::vector<FeatureState> features;
  std::map<std::string, BodyState> bodies;
  std::size_t kernel_calls = 0, cache_hits = 0, binding_misses = 0;
  [[nodiscard]] bool ok() const;
};

/// Per-document, serialized service evaluator (RFC 0007). The borrowed kernel must outlive it.
/// Parses/validates before kernel work; then evaluates sketches, B-rep extrusions/revolutions
/// (new/join/cut/intersect), rigid transforms, linear body patterns and solid booleans.
/// Patterns union translated copies into each seed body; count includes the seed. Copied
/// faces retain inherited names and gain the pattern's local "instance" role. All target
/// bodies publish atomically; private copy histories remain in bounded cache entries.
/// Booleans retire tools unless
/// keep_tools is true. Sketches verify line orientation, equal lengths/radii and radius
/// constraints, point coincidence/distance, midpoint and symmetry without moving coordinates.
/// Named points are retained separately from profile curves. Unsupported features are errors;
/// independent branches continue.
/// No files are opened, no cache is persisted, and returned geometry survives cache eviction.
class Evaluator {
 public:
  struct Limits {
    std::size_t entries, estimated_bytes;
  };
  struct Usage {
    std::size_t entries, estimated_bytes;
  };
  Evaluator(kernel::IKernel& kernel, Limits limits);
  ~Evaluator();
  [[nodiscard]] std::expected<Evaluation, ir::Errors> evaluate(std::string_view document);
  void set_cache_limits(Limits limits);
  void clear_cache();
  [[nodiscard]] Usage cache_usage() const;

 private:
  struct State;
  std::unique_ptr<State> state_;
};
}  // namespace nf::kernel_service
