// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <functional>
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
/// Sketch planes select generated faces by producer/role or all current faces via faces_of.
/// Face selectors accept plane/cylinder type, cylinder radius, planar directions and nearest
/// points. Face sets compose with recursive any/except; every node enforces its own expect. from
/// qualifies generated sides by the exact authored sketch/entity; caps have no source. Qualifiers
/// survive splitting/merging, transforms, booleans and pattern history.
struct NamedEntity {
  std::string feature, role;
  kernel::EntityPtr entity;
  std::string from{};
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
  std::size_t kernel_calls = 0, cache_hits = 0, binding_misses = 0;  // includes face queries
  [[nodiscard]] bool ok() const;
};

/// Per-document, serialized service evaluator (RFC 0007). The borrowed kernel must outlive it.
/// Parses/validates before kernel work; then evaluates sketches, B-rep
/// extrusions/revolutions/lofts/sweeps (new/join/cut/intersect), rigid transforms, linear/circular
/// body patterns, mirrors and booleans. Patterns/mirrors union translated/rotated/reflected copies
/// into each seed body. Counts include seeds; mirrors add one reflection per seed. Copied faces
/// retain inherited names and gain the feature's local "instance" role. All target bodies publish
/// atomically; private copy histories remain in bounded cache entries. Face-attached sketches and
/// mirror planes require one analytic planar face, bind all current candidate bodies and query
/// their oriented frame on cache hits too. Booleans transfer producer candidates to their surviving
/// body, retaining tools as candidates when requested. Planar filters use signed normals;
/// parallel/perpendicular describe the plane relative to a direction (sine tolerance 1e-9). Nonzero
/// directions are normalized; curved faces do not match. All filter conditions must hold. A plane
/// always requires exactly one match. near compares trimmed-face distances after other filters,
/// across all candidate bodies. Faces within 1e-6 mm of the minimum all survive; ties remain
/// selector_count. near alone does not imply a planar filter. Each distance is queried/counted
/// before cache reuse. any unions by snapshot/entity identity; except subtracts before this node's
/// filters. Every branch binds dependencies, including excluded/empty branches. Each node checks
/// one/some(default)/any after filtering; face descriptions are shared within a feature only.
/// Transform/circular-pattern/revolution axes require one analytic cylindrical face. Its located
/// parameter Z supplies the unit direction, independent of face orientation; coaxial patches
/// remain distinct matches. Positive finite radius filters match within 1e-6 mm and imply a
/// cylinder; directions still imply a plane. Metadata is validated/queried on cache hits too.
/// Operations modify only authored targets; selector-only source snapshots are retained
/// and conservatively charged separately from operation inputs/history.
/// Extrusion to selects one parallel planar supporting plane, extending beyond its finite trim.
/// The ray must travel forward with separation >1e-6 mm and |direction dot normal| >1e-9.
/// Cap control hulls must stay within 1e-6 mm of the plane; symmetric to/nonzero draft fail.
/// All described/selected plane frames are validated as finite and orthonormal before reuse.
/// Lofts bind 2..32 ordered profile sketches, each selecting one filled region without holes.
/// Parallel section planes and backend work bounds apply; rails are unsupported. Side ancestry
/// retains every section's current sketch/entity IDs. Flat NURBS sides are not fitted to planes.
/// Sweeps bind one selected filled region without holes and one open sketch line/arc/spline.
/// Preserve the authored path direction and kernel's fixed planar binormal; selector paths,
/// spatial paths, corners, closed paths and uncertified spans are explicit errors.
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
  // Called before each executing feature (including cache hits), excluding suppression/rollback.
  // The observer is synchronous, must not throw/reenter, and is never stored.
  using Observer = std::function<void(std::size_t)>;
  [[nodiscard]] std::expected<Evaluation, ir::Errors> evaluate(std::string_view document,
                                                               const Observer& observer = {});
  void set_cache_limits(Limits limits);
  void clear_cache();
  [[nodiscard]] Usage cache_usage() const;

 private:
  struct State;
  std::unique_ptr<State> state_;
};
}  // namespace nf::kernel_service
