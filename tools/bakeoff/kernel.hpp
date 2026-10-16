// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// The bake-off's kernel interface (RFC 0001). Temporary scaffolding: the real IKernel comes
// from a later RFC informed by what the bake-off teaches us.

#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

#include "nf/math/vec.hpp"

namespace nf::bakeoff {

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
  bool valid = false;   ///< Closed, manifold, passes the kernel's own checker.
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

  virtual Body boolean(BooleanOp op, Body a, Body b) = 0;
  /// Fillets every edge of `body` whose bounding box lies inside `edges`.
  virtual Body fillet(Body body, Region edges, double radius, FilletKind kind) = 0;
  /// Hollows `body` inward by `thickness`, opening every face inside `open_faces`.
  virtual Body shell(Body body, Region open_faces, double thickness) = 0;

  /// Mass properties at a relative accuracy of 1e-9 or better.
  virtual Measures measure(Body body) = 0;
};

}  // namespace nf::bakeoff
