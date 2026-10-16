// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cmath>
#include <optional>

#include "nf/math/tolerance.hpp"

namespace nf::math {

/// A 2D vector or point in sketch space (mm).
struct Vec2 {
  double x = 0.0;
  double y = 0.0;

  friend constexpr bool operator==(const Vec2&, const Vec2&) = default;
};

/// A 3D vector or point in model space (mm).
struct Vec3 {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;

  friend constexpr bool operator==(const Vec3&, const Vec3&) = default;
};

// --- Vec2 -------------------------------------------------------------------------------

[[nodiscard]] constexpr Vec2 operator+(Vec2 a, Vec2 b) noexcept { return {a.x + b.x, a.y + b.y}; }
[[nodiscard]] constexpr Vec2 operator-(Vec2 a, Vec2 b) noexcept { return {a.x - b.x, a.y - b.y}; }
[[nodiscard]] constexpr Vec2 operator-(Vec2 a) noexcept { return {-a.x, -a.y}; }
[[nodiscard]] constexpr Vec2 operator*(Vec2 a, double s) noexcept { return {a.x * s, a.y * s}; }
[[nodiscard]] constexpr Vec2 operator*(double s, Vec2 a) noexcept { return a * s; }
[[nodiscard]] constexpr Vec2 operator/(Vec2 a, double s) noexcept { return {a.x / s, a.y / s}; }

[[nodiscard]] constexpr double dot(Vec2 a, Vec2 b) noexcept { return a.x * b.x + a.y * b.y; }
/// z-component of the 3D cross product; positive when `b` is counter-clockwise of `a`.
[[nodiscard]] constexpr double cross(Vec2 a, Vec2 b) noexcept { return a.x * b.y - a.y * b.x; }
[[nodiscard]] constexpr double length_squared(Vec2 a) noexcept { return dot(a, a); }
[[nodiscard]] inline double length(Vec2 a) noexcept { return std::hypot(a.x, a.y); }

// --- Vec3 -------------------------------------------------------------------------------

[[nodiscard]] constexpr Vec3 operator+(Vec3 a, Vec3 b) noexcept {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
[[nodiscard]] constexpr Vec3 operator-(Vec3 a, Vec3 b) noexcept {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
[[nodiscard]] constexpr Vec3 operator-(Vec3 a) noexcept { return {-a.x, -a.y, -a.z}; }
[[nodiscard]] constexpr Vec3 operator*(Vec3 a, double s) noexcept {
  return {a.x * s, a.y * s, a.z * s};
}
[[nodiscard]] constexpr Vec3 operator*(double s, Vec3 a) noexcept { return a * s; }
[[nodiscard]] constexpr Vec3 operator/(Vec3 a, double s) noexcept {
  return {a.x / s, a.y / s, a.z / s};
}

[[nodiscard]] constexpr double dot(Vec3 a, Vec3 b) noexcept {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
[[nodiscard]] constexpr Vec3 cross(Vec3 a, Vec3 b) noexcept {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
[[nodiscard]] constexpr double length_squared(Vec3 a) noexcept { return dot(a, a); }
[[nodiscard]] inline double length(Vec3 a) noexcept { return std::hypot(a.x, a.y, a.z); }

// --- Comparison and normalization -------------------------------------------------------

/// True when the points are within `tol.linear` of each other.
[[nodiscard]] inline bool near(Vec2 a, Vec2 b, Tolerance tol = Tolerance::standard()) noexcept {
  return length(a - b) <= tol.linear;
}
[[nodiscard]] inline bool near(Vec3 a, Vec3 b, Tolerance tol = Tolerance::standard()) noexcept {
  return length(a - b) <= tol.linear;
}

/// Unit vector in the direction of `a`, or nothing if `a` is shorter than `tol.linear`
/// (its direction is not meaningful).
[[nodiscard]] inline std::optional<Vec2> normalized(
    Vec2 a, Tolerance tol = Tolerance::standard()) noexcept {
  const double len = length(a);
  if (len <= tol.linear) return std::nullopt;
  return a / len;
}
[[nodiscard]] inline std::optional<Vec3> normalized(
    Vec3 a, Tolerance tol = Tolerance::standard()) noexcept {
  const double len = length(a);
  if (len <= tol.linear) return std::nullopt;
  return a / len;
}

}  // namespace nf::math
