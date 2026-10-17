// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <memory>
#include <string>

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
}  // namespace nf::kernel
