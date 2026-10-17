// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// NURBS → SVG. Exact wherever SVG can express the curve; everything else within a stated
// tolerance, with the measured error reported (RFC 0004 exactness contract).

#pragma once

#include <expected>
#include <span>
#include <string>

#include "nf/svg/path.hpp"

namespace nf::svg {

struct ExportOptions {
  /// Largest allowed deviation, in user units (millimetres for documents), for curves SVG
  /// cannot express exactly.
  double tolerance = 0.01;
};

struct Exported {
  std::string text;
  /// Largest deviation of any approximated piece: a rigorous upper bound on the distance
  /// between each written cubic and the curve at the same parameter (so also on the distance
  /// between the curves); 0 when every segment was written exactly.
  double max_error = 0.0;
};

/// Path data (a `d` attribute) for the subpaths, per Bézier span:
/// - degree 1 → L; non-rational degree 2 → Q, degree 3 → C (exact);
/// - rational degree 2 → Q when parabolic, A when elliptical (exact up to rounding of the
///   ellipse's centre and axes, ≤ 1e-12 relative);
/// - anything else (hyperbolic spans, degree > 3) → cubic Béziers within the tolerance.
/// A closed subpath whose last segment is the closing line ends with Z instead. Numbers are
/// written in their shortest form that reads back to the same double.
/// Errors: a tolerance that is not positive and finite, or one the geometry cannot reach
/// (below the floating-point resolution of the curve).
[[nodiscard]] std::expected<Exported, std::string> write_path(std::span<const Subpath> paths,
                                                              const ExportOptions& options = {});

/// A standalone SVG document of model-space curves (millimetres, y up). y is negated into
/// SVG's y-down user space; the viewBox is the control points' bounding box and width and
/// height give it in millimetres, so one user unit is one millimetre.
/// A degenerate box (one straight horizontal or vertical line) is padded to 1 mm so the
/// viewport is never empty.
[[nodiscard]] std::expected<Exported, std::string> write_document(
    std::span<const Subpath> model_paths, const ExportOptions& options = {});

}  // namespace nf::svg
