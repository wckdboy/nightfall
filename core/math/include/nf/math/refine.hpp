// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// Shape-preserving refinement of NURBS curves (The NURBS Book ch. 5): the results trace
// exactly the same curve (up to rounding) with more knots, a higher degree, or in two
// pieces. All work on homogeneous points, so rational curves stay exact.

#pragma once

#include <expected>
#include <string>
#include <utility>

#include "nf/math/nurbs_curve.hpp"

namespace nf::math {

/// Inserts the knot u `times` times (A5.1). u must lie strictly inside the domain, and its
/// resulting multiplicity must not exceed the degree.
template <typename Point>
[[nodiscard]] std::expected<NurbsCurve<Point>, std::string> insert_knot(
    const NurbsCurve<Point>& curve, double u, int times = 1);

/// Raises the degree by `by` ≥ 1 (A5.9); interior knot multiplicities grow by `by`, so the
/// continuity is unchanged. The new degree must not exceed KnotVector::kMaxDegree.
template <typename Point>
[[nodiscard]] std::expected<NurbsCurve<Point>, std::string> elevate_degree(
    const NurbsCurve<Point>& curve, int by = 1);

/// Splits at u, strictly inside the domain, into [start, u] and [u, end]; both pieces
/// keep their original parameters, and they share the control point C(u).
template <typename Point>
[[nodiscard]] std::expected<std::pair<NurbsCurve<Point>, NurbsCurve<Point>>, std::string> split(
    const NurbsCurve<Point>& curve, double u);

/// The same curve traversed the other way on the same domain: C'(u) = C(a + b − u).
template <typename Point>
[[nodiscard]] NurbsCurve<Point> reverse(const NurbsCurve<Point>& curve);

}  // namespace nf::math
