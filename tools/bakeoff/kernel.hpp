// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// The bake-off's kernel interface (RFC 0001). Temporary scaffolding: the real IKernel comes
// from a later RFC informed by what the bake-off teaches us.

#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "nf/math/vec.hpp"

namespace nf::bakeoff {

using math::Vec2;
using math::Vec3;

/// Axis-aligned region used to select edges or faces without kernel-specific indices: an
/// entity is selected when its bounding box lies entirely inside the region.
struct Region {
  Vec3 min;
  Vec3 max;
};

enum class BooleanOp { kUnion, kDifference, kIntersection };

enum class FilletKind {
  kCircular,  ///< Constant-radius circular cross-section.
  kG2Blend,   ///< Curvature-continuous blend (kernels without one throw KernelError).
};

/// Can every output face/edge be traced to the input or operation that produced it?
enum class HistorySupport { kNo, kPartial, kYes };

/// Handle to a body owned by a Kernel. Operations never modify their inputs.
struct Body {
  int id = -1;
};

struct Measures {
  /// Exactly one solid bounded by exactly one closed manifold shell, passing the kernel's own
  /// checker. Internal voids (extra shells) count as invalid for every kernel. TODO(#17):
  /// distinguish voids from separate lumps once the corpus has a part with a cavity.
  bool valid = false;
  double volume = 0.0;  ///< mm³
  double area = 0.0;    ///< mm²
  int faces = 0;
  int edges = 0;
};

/// Thrown when a kernel operation fails or is unsupported.
class KernelError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

class Kernel {
 public:
  virtual ~Kernel() = default;

  [[nodiscard]] virtual std::string_view name() const = 0;
  [[nodiscard]] virtual std::string version() const = 0;
  /// Declared capability. Whether it holds is measured separately (#17).
  [[nodiscard]] virtual HistorySupport history_support() const = 0;

  virtual Body box(Vec3 origin, Vec3 size) = 0;
  /// Cylinder along +Z with its base circle centred at `base_center`.
  virtual Body cylinder(Vec3 base_center, double radius, double height) = 0;
  virtual Body sphere(Vec3 center, double radius) = 0;

  /// Copy of `body` rotated by `degrees` (right-handed) about the line through `point`
  /// along `axis`.
  virtual Body rotate(Body body, Vec3 point, Vec3 axis, double degrees) = 0;

  /// Truncated cone along +Z with its base circle centred at `base_center`.
  virtual Body cone(Vec3 base_center, double base_radius, double top_radius, double height) = 0;
  /// Torus around the Z axis, centred at `center`.
  virtual Body torus(Vec3 center, double major_radius, double minor_radius) = 0;
  /// Prism: the closed, counter-clockwise polygon `profile` in the plane z = `base_z`
  /// (last point joins the first), extruded along +Z by `height`.
  virtual Body extrude(const std::vector<Vec2>& profile, double base_z, double height) = 0;
  /// Tube of radius `tube_radius` swept along a right-handed helix of radius `helix_radius`
  /// around the Z axis, from (helix_radius, 0, 0) up to z = `height` in `turns` turns, with
  /// flat end caps.
  virtual Body helix_pipe(double helix_radius, double height, double turns, double tube_radius) = 0;
  /// Fillets every edge inside `edges` with a radius going from `start_radius` at the edge's
  /// first vertex (the kernel's own edge orientation) to `end_radius` at its last. The law in
  /// between is kernel-defined: SMLib's is consistent with linear, OCCT interpolates with zero
  /// end slopes (S-shaped). Results are only comparable by consensus (RFC 0001 F11).
  virtual Body variable_fillet(Body body, Region edges, double start_radius, double end_radius) = 0;

  virtual Body boolean(BooleanOp op, Body a, Body b) = 0;
  /// Fillets every edge of `body` whose bounding box lies inside `edges`.
  virtual Body fillet(Body body, Region edges, double radius, FilletKind kind) = 0;
  /// Hollows `body` inward by `thickness`, opening every face inside `open_faces`.
  virtual Body shell(Body body, Region open_faces, double thickness) = 0;

  /// Mass properties at the kernel's tightest supported accuracy (OCCT: 1e-9; SMLib: volume
  /// 1e-8, area 1e-4 — RFC 0001 F6).
  virtual Measures measure(Body body) = 0;
};

#if NF_BAKEOFF_WITH_OCCT
[[nodiscard]] std::unique_ptr<Kernel> make_occt_kernel();
#endif
#if NF_BAKEOFF_WITH_SMLIB
[[nodiscard]] std::unique_ptr<Kernel> make_smlib_kernel();
#endif

}  // namespace nf::bakeoff
