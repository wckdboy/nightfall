// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <TopoDS_Shape.hxx>

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

/// Adopt newly built, exclusively owned geometry; callers must never mutate it afterward.
/// Checks all descendants belong to closed solids and computes finite measurements.
std::expected<SnapshotPtr, KernelError> snapshot(TopoDS_Shape geometry);
EntityPtr entity(SnapshotPtr owner, const TopoDS_Shape& geometry);
std::vector<EntityPtr> entities(const SnapshotPtr& owner);
}  // namespace nf::kernel::detail
