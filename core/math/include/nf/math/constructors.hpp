// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// Exact NURBS constructors (RFC 0004 exactness contract): every curve here reproduces its
// shape mathematically, not approximately.

#pragma once

#include <span>

#include "nf/math/nurbs_curve.hpp"

namespace nf::math {

/// Straight segment a → b, parameter [0, 1]. Exact.
[[nodiscard]] NurbsCurve2 make_line(Vec2 a, Vec2 b);
[[nodiscard]] NurbsCurve3 make_line(Vec3 a, Vec3 b);

/// Degree-1 curve through `points` (≥ 2), parameterised by cumulative chord length on
/// [0, 1]. Exact. Repeated consecutive vertices are dropped. Throws std::invalid_argument for
/// fewer than 2 points or zero length.
[[nodiscard]] NurbsCurve2 make_polyline(std::span<const Vec2> points);
[[nodiscard]] NurbsCurve3 make_polyline(std::span<const Vec3> points);

/// Bézier curve of degree points.size() − 1 (1 … 9), parameter [0, 1]. Exact.
[[nodiscard]] NurbsCurve2 make_bezier(std::span<const Vec2> points);
[[nodiscard]] NurbsCurve3 make_bezier(std::span<const Vec3> points);

/// Elliptical arc C(t) = center + rx cos(t) x_axis + ry sin(t) y_axis for t from `start` to
/// `end` radians (end > start; at most one full turn), as a rational quadratic with one span
/// per ≤ 90° (The NURBS Book A7.1). `x_axis` and `y_axis` must be orthonormal. Exact.
[[nodiscard]] NurbsCurve3 make_arc(Vec3 center, Vec3 x_axis, Vec3 y_axis, double rx, double ry,
                                   double start, double end);

/// The 2D form: the ellipse's x axis is rotated by `rotation` radians from +X.
[[nodiscard]] NurbsCurve2 make_arc(Vec2 center, double rx, double ry, double rotation, double start,
                                   double end);

/// Full circle of radius r in the plane through `center` with unit `normal`; starts on the
/// in-plane axis most aligned with +X (or +Y if the normal is ±X). Exact, 4 spans.
[[nodiscard]] NurbsCurve3 make_circle(Vec3 center, Vec3 normal, double r);
[[nodiscard]] NurbsCurve2 make_circle(Vec2 center, double r);

}  // namespace nf::math
