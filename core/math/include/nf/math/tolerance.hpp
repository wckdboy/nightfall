// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace nf::math {

/// Tolerances used by every geometric comparison in Nightfall.
///
/// Lengths are millimetres and angles are radians, everywhere. No comparison in `core/`
/// may use a bare epsilon; it takes a `Tolerance` (or one of its fields) instead.
struct Tolerance {
  /// Two points closer than this are the same point (mm).
  double linear = 1e-6;
  /// Two directions closer than this are the same direction (rad).
  double angular = 1e-9;

  [[nodiscard]] static constexpr Tolerance standard() noexcept { return {}; }
};

/// |value|, usable in constant expressions.
[[nodiscard]] constexpr double abs(double value) noexcept { return value < 0.0 ? -value : value; }

/// True when `a` and `b` differ by at most `tolerance` (absolute).
[[nodiscard]] constexpr bool near(double a, double b, double tolerance) noexcept {
  return abs(a - b) <= tolerance;
}

}  // namespace nf::math
