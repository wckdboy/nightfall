// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <map>

#include "nf/ir/document.hpp"
#include "nf/kernel/kernel.hpp"

namespace nf::kernel_service {
struct Sketch {
  struct Line {
    math::Vec2 start, end;
  };
  kernel::Plane plane;
  std::vector<std::vector<math::NurbsCurve2>> paths;  // already-closed primitives
  std::vector<math::NurbsCurve2> segments;   // unjoined entity curves, valid as open references
  std::map<std::string, math::Vec2> points;  // named references, never profile boundaries
  std::map<std::string, Line> lines;         // authored direction, independent of path traversal
  // Optional metadata parallel to paths/segments; omitted entries have no authored source.
  std::vector<std::vector<std::string>> path_entities;
  std::vector<std::string> segment_entities;
};
/// Local authored entity IDs by final region/loop/curve. Empty sets denote synthetic bridges.
/// No producer/sketch ID is cached here; publication binds the current evaluation's IDs.
using ProfileSources = std::map<std::array<std::size_t, 3>, std::vector<std::string>>;
struct BodyPattern {
  std::size_t count;            // includes the original seed
  kernel::RigidTransform step;  // multiply translation/angle by each copy index
};
/// Private service adapters: accept only schema-checked, dimensionally evaluated FeatureInputs.
/// Preserve exact curves; errors have feature-relative pointers, prefixed by the evaluator.
/// Sketches support rect/circle/arc/ellipse/polygon/spline, including open reference curves.
/// Profile consumers assemble closed, unambiguous mixed chains; open curves are never
/// closed or discarded implicitly, including when selecting a region by an interior point.
/// Verify horizontal/vertical lines, parallel/perpendicular line pairs (1e-9 rad), equal
/// line lengths or circle/arc radii, and circle/arc radius dimensions (1e-6 mm). Reversed
/// lines are equivalent for orientation; equal arcs compare radii, not sweep lengths.
/// Point constraints (1e-6 mm): coincidence/distance between two points, midpoint with
/// one point and one entire line in either order, symmetry of two points about a third
/// entire line. Resolve named points, line/arc start/end and arc/circle/ellipse centers.
/// Bare curve IDs never imply a point; point-qualified IDs cannot stand for entire curves.
/// Tangency supports line/circle/arc pairs with at least one circular curve: radial gap
/// and segment extent within 1e-6 mm, contact inside each arc within 1e-9 rad. Check
/// internal/external circle contact; centers must be separated by more than 1e-6 mm.
/// Angle dimensions compare start-to-end line directions in [0, pi], within 1e-9 rad.
/// Swapping operands or reversing both preserves the angle; reversing one takes its supplement.
/// Fixed whole entities and supported point references retain evaluated authored coordinates;
/// they do not pin values from a previous document or prevent subsequent parameter edits.
/// Coordinates are checked, never solved or moved. Unsupported operands fail explicitly.
/// Work limits: 4096 sketch curves/points combined, spline poles, constraints and profile spans.
/// Revolve/rotation/pattern axes resolve named lines from the supplied evaluated axis sketch; map
/// start and start-to-end direction through its plane. The evaluator may supply an already
/// resolved, directed cylinder axis for selector forms; no geometry queries occur here.
/// Unimplemented entity/selector/mode semantics fail explicitly.
/// The evaluator may supply an already resolved, oriented face plane for a selector sketch.
std::expected<Sketch, ir::Error> prepare_sketch(const ir::json::Value& inputs,
                                                const kernel::Plane* selected_plane = nullptr);
/// to terminates at an already resolved parallel supporting plane, beyond its finite trim.
/// Require positive ray travel and separation >1e-6 mm; reject symmetric to and tilted targets.
/// Direction must leave the sketch plane (|dot| >1e-9); certify cap control hulls within 1e-6 mm.
std::expected<kernel::Extrusion, ir::Error> prepare_extrusion(
    const ir::json::Value& inputs, const Sketch& sketch, ProfileSources* sources = nullptr,
    const kernel::Plane* termination = nullptr);
std::expected<kernel::Revolution, ir::Error> prepare_revolution(
    const ir::json::Value& inputs, const Sketch& sketch, const Sketch* axis_sketch = nullptr,
    ProfileSources* sources = nullptr, const kernel::Axis* selected_axis = nullptr);
/// Ordered profiles use existing fill/region selection; exactly one region without holes
/// per section. Rails are unsupported. Sources retain local addresses per section, without
/// producer IDs; errors identify the corresponding /inputs/profiles entry.
std::expected<kernel::Loft, ir::Error> prepare_loft(const ir::json::Value& inputs,
                                                    const std::vector<const Sketch*>& sketches,
                                                    std::vector<ProfileSources>* sources = nullptr);
/// One selected profile region without holes and one whole open sketch entity as the
/// authored path. Preserve curve direction; selector paths and point-qualified IDs fail.
std::expected<kernel::Sweep, ir::Error> prepare_sweep(const ir::json::Value& inputs,
                                                      const Sketch& profile, const Sketch& path,
                                                      ProfileSources* sources = nullptr);
std::expected<kernel::RigidTransform, ir::Error> prepare_transform(
    const ir::json::Value& inputs, const Sketch* axis_sketch = nullptr,
    const kernel::Axis* selected_axis = nullptr);
/// Unique body targets only; at most 64 total instances, including all seeds. Count 1
/// preserves the seed; larger counts require |spacing| > 1e-6 mm and finite offsets.
std::expected<BodyPattern, ir::Error> prepare_linear_pattern(const ir::json::Value& inputs);
/// Circular patterns share the body/count limit. Signed partial sweeps include both endpoints;
/// sweeps within 1e-9 rad of a full turn distribute unique instances (no duplicate endpoint).
/// |angle| <= 2*pi + 1e-9 rad; count > 1 requires an angular step above 1e-9 rad.
/// Count 1 permits zero sweep but still validates the axis and angle range.
std::expected<BodyPattern, ir::Error> prepare_circular_pattern(
    const ir::json::Value& inputs, const Sketch* axis_sketch = nullptr,
    const kernel::Axis* selected_axis = nullptr);
/// Body mirrors union each current seed with one exact reflection. Datum/explicit planes
/// share sketch frame validation. Unique targets; at most 32 bodies (64 total instances).
/// The evaluator may supply the current oriented plane resolved from a face selector.
std::expected<kernel::Plane, ir::Error> prepare_mirror(
    const ir::json::Value& inputs, const kernel::Plane* selected_plane = nullptr);
}  // namespace nf::kernel_service
