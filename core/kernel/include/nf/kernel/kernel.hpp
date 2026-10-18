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
struct Axis {
  math::Vec3 origin, direction{0, 0, 1};  // finite origin and unit direction
};
struct Cylinder {
  Axis axis;      // located surface parameter Z; reversing the face does not reverse this axis
  double radius;  // finite, positive millimetres
};
struct FaceInfo {
  EntityPtr entity;
  std::optional<Plane> plane;  // located outward frame for analytic planes; empty for curved faces
  std::optional<Cylinder> cylinder{};  // exact analytic cylinders only; no fitting/approximation
};
struct Extrusion {
  Plane plane;
  std::vector<math::Region> regions;
  math::Vec3 displacement;
};
/// Signed rotation in radians, with 0 < |angle| <= 2π, about an axis in the sketch plane.
/// Region control hulls must all lie on the same side of the axis (touching it is allowed).
/// This conservative restriction prevents overlapping sweeps without approximating curves.
struct Revolution {
  Plane plane;
  std::vector<math::Region> regions;
  math::Vec3 axis_origin, axis_direction{0, 1, 0};
  double angle = 0;
};
/// Rotate about an axis, then translate. Angle is radians; axis direction need not be unit.
struct RigidTransform {
  math::Vec3 translation, axis_origin, axis_direction{0, 0, 1};
  double angle = 0;
};
enum class BooleanOp { kUnion, kSubtract, kIntersect };
struct ProfileEdge {
  std::size_t region, loop, curve;  // loop 0 = outer; subsequent loops are holes
};
struct Generated {
  std::string role;  // operation-local semantic role; never a feature ID
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
    bool extrude, rigid_transform, booleans, revolve, mirror, chamfer;
  };
  [[nodiscard]] virtual Capabilities capabilities() const noexcept = 0;
  /// Encode all supplied closed-solid snapshots as exact STEP AP242 B-rep text in millimetres.
  /// Preserve each body (including coincident ones) and every input snapshot/handle. Reject
  /// empty lists and null/foreign snapshots. No files are opened; serialize backend access.
  [[nodiscard]] virtual std::expected<std::string, KernelError> export_step(
      const std::vector<ShapePtr>&) = 0;
  /// Enumerate every unique face of a validated snapshot, including curved faces and all solids.
  /// Handles retain their owner; planar frames follow face_plane's convention. Cylinders expose
  /// their located parameter axis/radius, independent of face orientation. Other curved types
  /// have neither description. Order is unspecified; never use it as persistent identity.
  /// Reject null/foreign snapshots; do not mutate.
  [[nodiscard]] virtual std::expected<std::vector<FaceInfo>, KernelError> faces(
      const ShapePtr&) = 0;
  /// Unique boundary edges from every trimming loop of a face, including holes and seams.
  /// Handles retain the same snapshot; reject null/foreign/non-face handles. No mutation.
  [[nodiscard]] virtual std::expected<std::vector<EntityPtr>, KernelError> edges(
      const EntityPtr& face) = 0;
  /// Equal setback in mm on both incident faces, with tangent-contour propagation. Require
  /// nonempty edges owned by target and finite distance > linear tolerance; deduplicate edges.
  /// Return validated solids, complete face/edge history and new faces with role "chamfer".
  /// Fail on unsupported geometry/history; preserve inputs even on failure.
  [[nodiscard]] virtual std::expected<OperationResult, KernelError> chamfer(
      const ShapePtr& target, const std::vector<EntityPtr>& edges, double distance) = 0;
  /// Query an analytic planar face's located parameter frame with outward normal. Keep its U axis;
  /// y = normal cross x restores a right-handed frame on reversed/reflected faces.
  /// Reject nonplanar, non-face, foreign or invalid handles. The query does not mutate inputs.
  [[nodiscard]] virtual std::expected<Plane, KernelError> face_plane(const EntityPtr&) = 0;
  /// Minimum Euclidean distance in mm from a finite world point to the trimmed face, including
  /// boundaries and holes. Accept curved faces; honor the owning snapshot's current placement.
  /// Return a finite nonnegative value or an error; reject foreign/invalid/non-face handles.
  /// Preserve all inputs; handles remain usable after their producing facade ends.
  [[nodiscard]] virtual std::expected<double, KernelError> face_distance(const EntityPtr&,
                                                                         math::Vec3) = 0;
  [[nodiscard]] virtual std::expected<OperationResult, KernelError> extrude(const Extrusion&) = 0;
  /// Exact solid revolution with side provenance and, for partial turns, start/end caps.
  [[nodiscard]] virtual std::expected<OperationResult, KernelError> revolve(const Revolution&) = 0;
  [[nodiscard]] virtual std::expected<OperationResult, KernelError> transform(
      const ShapePtr&, const RigidTransform&) = 0;
  /// Reflect exact geometry across a finite orthonormal plane, preserving solid orientation.
  /// Returns the reflected copy only; inputs stay immutable. History maps every input face
  /// and edge to an entity with its output orientation; no new local faces are generated.
  [[nodiscard]] virtual std::expected<OperationResult, KernelError> mirror(const ShapePtr&,
                                                                           const Plane&) = 0;
  /// Combine target with the union of nonempty tools: union, target minus tools, or common.
  /// Empty/non-solid results fail. History covers every input face/edge, including unchanged
  /// survivors; new intersection edges have the local role "intersection". Inputs stay intact.
  [[nodiscard]] virtual std::expected<OperationResult, KernelError> boolean_operation(
      const ShapePtr& target, const std::vector<ShapePtr>& tools, BooleanOp) = 0;
};

/// Pinned dynamically linked OCCT backend. Tolerances must be finite, positive and no smaller
/// than OCCT's precision floor. Entity handles remain valid after this facade is destroyed.
[[nodiscard]] std::expected<std::unique_ptr<IKernel>, KernelError> make_occt_kernel(
    math::Tolerance tolerance = math::Tolerance::standard());
}  // namespace nf::kernel
