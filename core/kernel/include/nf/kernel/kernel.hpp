// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "nf/math/arrangement.hpp"
#include "nf/math/vec.hpp"

namespace nf::kernel {
/// Backend failures are values; no OCCT types cross this boundary (RFC 0007).
struct KernelError {
  std::string code = "kernel", message;
};

/// Measurements of a validated, nonempty collection of closed solids, in mm units.
struct Measures {
  double volume = 0, area = 0;
  math::Vec3 centroid, minimum, maximum;
  std::size_t solids = 0, faces = 0, edges = 0;
};

/// Immutable geometry snapshot. Ownership is independent of a request or kernel facade.
/// Backend calls are serialized by the service; shared ownership does not imply thread safety.
class Shape {
 public:
  virtual ~Shape() = default;
  [[nodiscard]] virtual const Measures& measures() const noexcept = 0;
  /// Retained geometry estimate, not an allocator/RSS bound. Shared data may be charged twice.
  [[nodiscard]] virtual std::size_t estimated_bytes() const noexcept = 0;
};
using ShapePtr = std::shared_ptr<const Shape>;

enum class EntityKind { kFace, kEdge };
/// Transient subshape handle that owns its snapshot. Never persisted as a topology index.
class Entity {
 public:
  virtual ~Entity() = default;
  [[nodiscard]] virtual ShapePtr owner() const noexcept = 0;
  [[nodiscard]] virtual EntityKind kind() const noexcept = 0;
  /// Equality requires the same backend, snapshot and subshape; geometric equality is weaker.
  [[nodiscard]] virtual bool same_as(const Entity& other) const noexcept = 0;
};
using EntityPtr = std::shared_ptr<const Entity>;

/// Orthonormal sketch frame; y_axis = normal × x_axis. Coordinates are millimetres.
struct Plane {
  math::Vec3 origin, x_axis{1, 0, 0}, normal{0, 0, 1};
};
struct Extrusion {
  Plane plane;
  std::vector<math::Region> regions;
  math::Vec3 displacement;
};
/// Rotate about an axis, then translate. Angle is radians; axis direction need not be unit.
struct RigidTransform {
  math::Vec3 translation, axis_origin, axis_direction{0, 0, 1};
  double angle = 0;
};
struct ProfileEdge {
  std::size_t region, loop, curve;  // loop 0 = outer; subsequent loops are holes
};
struct Generated {
  std::string role;  // operation-local cap_start, cap_end or side; never a feature ID
  EntityPtr entity;
  std::optional<ProfileEdge> from;
};
struct History {
  EntityPtr before;
  std::vector<EntityPtr> modified, generated;
  bool deleted = false;
};
struct OperationResult {
  ShapePtr shape;
  std::vector<Generated> generated;
  std::vector<History> history;
  std::size_t estimated_bytes = 0;  // includes retained input snapshots and history
};

/// Service-only modeling boundary. Operations preserve all input snapshots, return complete
/// validated solids or an error, and never read files or interpret feature JSON.
class IKernel {
 public:
  virtual ~IKernel() = default;
  /// Changes with backend version, tolerance, options and naming semantics; included in keys.
  [[nodiscard]] virtual std::string_view identity() const noexcept = 0;
  struct Capabilities {
    bool extrude, rigid_transform;
  };
  [[nodiscard]] virtual Capabilities capabilities() const noexcept = 0;
  [[nodiscard]] virtual std::expected<OperationResult, KernelError> extrude(const Extrusion&) = 0;
  [[nodiscard]] virtual std::expected<OperationResult, KernelError> transform(
      const ShapePtr&, const RigidTransform&) = 0;
};

/// Pinned dynamically linked OCCT backend. Tolerances must be finite, positive and no smaller
/// than OCCT's precision floor. Entity handles remain valid after this facade is destroyed.
[[nodiscard]] std::expected<std::unique_ptr<IKernel>, KernelError> make_occt_kernel(
    math::Tolerance tolerance = math::Tolerance::standard());
}  // namespace nf::kernel
