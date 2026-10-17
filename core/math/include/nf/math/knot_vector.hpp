// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace nf::math {

/// Clamped knot vector of a degree-p B-spline (The NURBS Book §2.2). Immutable and cheap to
/// copy (shared storage). Invariants, checked by make(): 1 ≤ p ≤ kMaxDegree, finite and
/// non-decreasing knots, both ends of multiplicity exactly p + 1, interior multiplicities
/// ≤ p, and a non-empty domain.
class KnotVector {
 public:
  static constexpr int kMaxDegree = 9;

  /// Validated construction; the error says which invariant failed.
  [[nodiscard]] static std::expected<KnotVector, std::string> make(std::vector<double> knots,
                                                                   int degree);

  /// Open uniform knots on [0, 1] for `control_points` control points (≥ degree + 1).
  [[nodiscard]] static KnotVector uniform_clamped(int degree, int control_points);

  [[nodiscard]] int degree() const noexcept { return degree_; }
  [[nodiscard]] std::span<const double> knots() const noexcept { return *knots_; }
  [[nodiscard]] std::size_t size() const noexcept { return knots_->size(); }
  [[nodiscard]] double operator[](std::size_t i) const noexcept { return (*knots_)[i]; }
  [[nodiscard]] int control_point_count() const noexcept {
    return static_cast<int>(size()) - degree_ - 1;
  }
  [[nodiscard]] double domain_start() const noexcept {
    return (*knots_)[static_cast<std::size_t>(degree_)];
  }
  [[nodiscard]] double domain_end() const noexcept {
    return (*knots_)[size() - static_cast<std::size_t>(degree_) - 1];
  }

  /// Index i of the knot span [u_i, u_{i+1}) containing u (A2.1); u is clamped to the
  /// domain, and the domain's end belongs to the last non-empty span.
  [[nodiscard]] int find_span(double u) const noexcept;

 private:
  KnotVector(std::shared_ptr<const std::vector<double>> knots, int degree)
      : knots_(std::move(knots)), degree_(degree) {}

  std::shared_ptr<const std::vector<double>> knots_;
  int degree_;
};

}  // namespace nf::math
