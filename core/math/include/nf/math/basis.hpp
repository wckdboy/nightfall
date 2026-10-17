// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// B-spline basis functions (The NURBS Book §2.5). Fixed-size outputs: evaluation never
// allocates.

#pragma once

#include <array>

#include "nf/math/knot_vector.hpp"

namespace nf::math {

/// N_{span-p,p}(u) … N_{span,p}(u) in entries 0 … p.
using BasisValues = std::array<double, KnotVector::kMaxDegree + 1>;

/// Basis function derivatives: at(k, j) = d^k/du^k N_{span-p+j,p}(u).
struct BasisDerivatives {
  static constexpr int kSide = KnotVector::kMaxDegree + 1;
  std::array<double, kSide * kSide> values{};

  [[nodiscard]] double at(int k, int j) const noexcept { return values[index(k, j)]; }
  double& at(int k, int j) noexcept { return values[index(k, j)]; }

 private:
  static std::size_t index(int k, int j) noexcept {
    return static_cast<std::size_t>(k * kSide + j);
  }
};

/// Non-vanishing basis functions at u (A2.2). `span` must be find_span(u).
void basis_functions(const KnotVector& knots, int span, double u, BasisValues& out) noexcept;

/// Basis functions and their derivatives up to order `n` at u (A2.3); orders above the
/// degree are zero. `span` must be find_span(u); n ≤ KnotVector::kMaxDegree.
void basis_derivatives(const KnotVector& knots, int span, double u, int n,
                       BasisDerivatives& out) noexcept;

}  // namespace nf::math
