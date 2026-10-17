// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// libnf-math NURBS ↔ OpenCASCADE B-splines (RFC 0004: lossless both ways). libnf-math values
// are the plain data the IR and the app use; OCCT's are what the kernel models with.

#pragma once

#include <expected>
#include <string>

#include <Geom2d_BSplineCurve.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_BSplineSurface.hxx>

#include "nf/math/nurbs_curve.hpp"
#include "nf/math/nurbs_surface.hpp"

namespace nf::kernel {

/// Same knots (as distinct values with multiplicities), poles and weights; rational exactly
/// when the libnf-math value is. An error when OCCT cannot hold the data, e.g. distinct
/// knots closer than OCCT's Epsilon, which libnf-math allows; never an exception.
[[nodiscard]] std::expected<Handle(Geom_BSplineCurve), std::string> to_occt(
    const math::NurbsCurve3& curve);
[[nodiscard]] std::expected<Handle(Geom2d_BSplineCurve), std::string> to_occt(
    const math::NurbsCurve2& curve);
[[nodiscard]] std::expected<Handle(Geom_BSplineSurface), std::string> to_occt(
    const math::NurbsSurface& surface);

/// The same B-spline as libnf-math data: knots expanded from multiplicities, poles, weights
/// (rescaled by a power of two at most, which is exact). Periodic OCCT B-splines are first
/// converted to their clamped form; degrees above KnotVector::kMaxDegree are an error.
[[nodiscard]] std::expected<math::NurbsCurve3, std::string> from_occt(
    const Geom_BSplineCurve& curve);
[[nodiscard]] std::expected<math::NurbsCurve2, std::string> from_occt(
    const Geom2d_BSplineCurve& curve);
[[nodiscard]] std::expected<math::NurbsSurface, std::string> from_occt(
    const Geom_BSplineSurface& surface);

}  // namespace nf::kernel
