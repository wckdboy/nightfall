// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <TopoDS_Shape.hxx>
#include <gp_Trsf.hxx>

#include <expected>
#include <vector>

#include "nf/kernel/kernel.hpp"

namespace nf::kernel::detail {
inline constexpr double kMassRelativeAccuracy = 1e-9;

struct Snapshot final : Shape {
  TopoDS_Shape geometry;
  Measures measured;
  std::size_t bytes = 0;
  const Measures& measures() const noexcept override { return measured; }
  std::size_t estimated_bytes() const noexcept override { return bytes; }
};
using SnapshotPtr = std::shared_ptr<const Snapshot>;

struct Subshape final : Entity {
  SnapshotPtr snapshot;
  TopoDS_Shape geometry;
  ShapePtr owner() const noexcept override { return snapshot; }
  EntityKind kind() const noexcept override;
  bool same_as(const Entity& other) const noexcept override;
};

/// Adopt newly built geometry (or an immutable snapshot with a new rigid location).
/// Callers must never mutate shared geometry afterward.
/// Checks all descendants belong to closed solids and computes finite measurements.
/// Planar faces with analytic line pcurves use exact polynomial Gaussian quadrature;
/// other faces retain adaptive Gauss–Kronrod integration at kMassRelativeAccuracy.
/// Areas on planes/cylinders with line/circle/ellipse pcurves use analytic UV integrals;
/// rectangular extrusion/B-spline/Bezier patches use positive adaptive metric integration
/// (1D for extrusions, nested 2D split at surface knots otherwise). Other surface areas
/// retain adaptive boundary integration at the same requested accuracy.
std::expected<SnapshotPtr, KernelError> snapshot(TopoDS_Shape geometry);
/// Validate a direct rigid relocation of an already validated immutable snapshot. Unchanged
/// topology/orientation plus a finite isometry preserve solid validity and mass properties.
/// New geometry falls back to full validation; rotations recompute exact-geometry bounds.
std::expected<SnapshotPtr, KernelError> rigid_snapshot(TopoDS_Shape geometry,
                                                       const Snapshot& source,
                                                       const gp_Trsf& transform);
EntityPtr entity(SnapshotPtr owner, const TopoDS_Shape& geometry);
std::vector<EntityPtr> entities(const SnapshotPtr& owner);
}  // namespace nf::kernel::detail
