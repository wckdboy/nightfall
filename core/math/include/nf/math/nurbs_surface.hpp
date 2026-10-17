// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// Tensor-product NURBS surfaces (The NURBS Book ch. 3–4, 8). Immutable values with shared
// storage; evaluation never allocates.

#pragma once

#include <array>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "nf/math/knot_vector.hpp"
#include "nf/math/nurbs_curve.hpp"
#include "nf/math/vec.hpp"

namespace nf::math {

/// Partial derivatives S_{u^k v^l} for k + l ≤ order (order ≤ kMaxOrder).
struct SurfaceDerivatives {
  static constexpr int kMaxOrder = 3;
  std::array<Vec3, (kMaxOrder + 1) * (kMaxOrder + 1)> d{};
  [[nodiscard]] Vec3 at(int k, int l) const {
    return d[static_cast<std::size_t>(k * (kMaxOrder + 1) + l)];
  }
};

class NurbsSurface {
 public:
  /// Validated construction. `points` has knots_u.control_point_count() rows of
  /// knots_v.control_point_count() points each (row i varies in v): P(i, j) = points[i·m + j].
  /// Weights are empty (non-rational) or one positive finite weight per point.
  [[nodiscard]] static std::expected<NurbsSurface, std::string> make(
      KnotVector knots_u, KnotVector knots_v, std::vector<Vec3> points,
      std::vector<double> weights = {});

  [[nodiscard]] const KnotVector& knots_u() const noexcept { return data_->ku; }
  [[nodiscard]] const KnotVector& knots_v() const noexcept { return data_->kv; }
  [[nodiscard]] int count_u() const noexcept { return data_->ku.control_point_count(); }
  [[nodiscard]] int count_v() const noexcept { return data_->kv.control_point_count(); }
  [[nodiscard]] std::span<const Vec3> points() const noexcept { return data_->points; }
  [[nodiscard]] std::span<const double> weights() const noexcept { return data_->weights; }
  [[nodiscard]] bool rational() const noexcept { return data_->rational; }

  /// S(u, v) (A4.3); parameters are clamped to the domain.
  [[nodiscard]] Vec3 point(double u, double v) const noexcept;

  /// All partial derivatives up to `order` (A3.6, and A4.4 for rational surfaces).
  [[nodiscard]] SurfaceDerivatives derivatives(double u, double v, int order) const noexcept;

  /// Unit normal Su × Sv. Where that vanishes (a pole of a surface of revolution, a collapsed
  /// edge), the normal of a nearby interior point; nullopt if the surface is degenerate there
  /// too.
  [[nodiscard]] std::optional<Vec3> normal(double u, double v) const noexcept;

  /// The exact curve S(u, ·) at fixed u (knots in v), and S(·, v) at fixed v.
  [[nodiscard]] NurbsCurve3 iso_u(double u) const;
  [[nodiscard]] NurbsCurve3 iso_v(double v) const;

 private:
  struct Data {
    KnotVector ku, kv;
    std::vector<Vec3> points;
    std::vector<double> weights;
    bool rational;
  };
  explicit NurbsSurface(std::shared_ptr<const Data> d) : data_(std::move(d)) {}
  std::shared_ptr<const Data> data_;
};

/// The curve swept along `direction`: S(u, v) = C(u) + v·direction, v ∈ [0, 1]. Exact.
[[nodiscard]] NurbsSurface extrude(const NurbsCurve3& profile, Vec3 direction);

/// The profile revolved about the axis through `origin` along `axis` (non-zero) by `angle`
/// radians (0 < angle ≤ 2π, counter-clockwise about the axis): u follows the profile, v the
/// rotation, as rational quadratic arcs of ≤ 90° (A8.1). Exact. Throws std::invalid_argument
/// for a zero axis or an angle out of range.
[[nodiscard]] NurbsSurface revolve(const NurbsCurve3& profile, Vec3 origin, Vec3 axis,
                                   double angle);

}  // namespace nf::math
