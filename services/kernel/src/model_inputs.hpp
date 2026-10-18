// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <map>

#include "nf/ir/document.hpp"
#include "nf/kernel/kernel.hpp"

namespace nf::kernel_service {
struct Sketch {
  kernel::Plane plane;
  std::vector<std::vector<math::NurbsCurve2>> paths;
  std::map<std::string, math::Vec2> points;  // named references, never profile boundaries
};
/// Private service adapters: accept only schema-checked, dimensionally evaluated FeatureInputs.
/// Preserve exact curves; errors have feature-relative pointers, prefixed by the evaluator.
/// Sketches support rect/circle/arc/ellipse/polygon/spline and closed mixed curve chains.
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
/// Unimplemented entity/selector/mode semantics fail explicitly.
std::expected<Sketch, ir::Error> prepare_sketch(const ir::json::Value& inputs);
std::expected<kernel::Extrusion, ir::Error> prepare_extrusion(const ir::json::Value& inputs,
                                                              const Sketch& sketch);
std::expected<kernel::Revolution, ir::Error> prepare_revolution(const ir::json::Value& inputs,
                                                                const Sketch& sketch);
std::expected<kernel::RigidTransform, ir::Error> prepare_transform(const ir::json::Value& inputs);
}  // namespace nf::kernel_service
