// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// Non-uniform rational B-spline curves (The NURBS Book ch. 4), in 2D and 3D. Immutable
// values with shared storage; evaluation never allocates.

#pragma once

#include <expected>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "nf/math/knot_vector.hpp"
#include "nf/math/vec.hpp"

namespace nf::math {

template <typename Point>
class NurbsCurve {
 public:
  /// Validated construction: points.size() == knots.control_point_count(), weights empty
  /// (non-rational, all 1) or one positive finite weight per point, finite coordinates.
  /// Equal weights are stored as all 1 (non-rational); otherwise the weights are scaled by a
  /// power of two (exact; the curve is unchanged) so the largest lies in [1, 2).
  [[nodiscard]] static std::expected<NurbsCurve, std::string> make(
      KnotVector knots, std::vector<Point> points, std::vector<double> weights = {});

  [[nodiscard]] const KnotVector& knots() const noexcept { return data_->knots; }
  [[nodiscard]] int degree() const noexcept { return data_->knots.degree(); }
  [[nodiscard]] std::span<const Point> points() const noexcept { return data_->points; }
  /// One weight per control point (all 1 for a non-rational curve).
  [[nodiscard]] std::span<const double> weights() const noexcept { return data_->weights; }
  [[nodiscard]] bool rational() const noexcept { return data_->rational; }
  [[nodiscard]] double domain_start() const noexcept { return data_->knots.domain_start(); }
  [[nodiscard]] double domain_end() const noexcept { return data_->knots.domain_end(); }

  /// C(u) (A4.1); u is clamped to the domain.
  [[nodiscard]] Point point(double u) const noexcept;

  /// C(u) and its derivatives: out[k] = d^k C / du^k for k < out.size(), k ≤ n (A4.2 for
  /// rational curves), to any order. Orders above the degree are zero for non-rational
  /// curves but not, in general, for rational ones.
  void derivatives(double u, int n, std::span<Point> out) const noexcept;

 private:
  struct Data {
    KnotVector knots;
    std::vector<Point> points;
    std::vector<double> weights;
    bool rational;
  };
  explicit NurbsCurve(std::shared_ptr<const Data> data) : data_(std::move(data)) {}
  std::shared_ptr<const Data> data_;
};

using NurbsCurve2 = NurbsCurve<Vec2>;
using NurbsCurve3 = NurbsCurve<Vec3>;

extern template class NurbsCurve<Vec2>;
extern template class NurbsCurve<Vec3>;

}  // namespace nf::math
