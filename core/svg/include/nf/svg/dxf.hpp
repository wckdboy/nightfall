// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// NURBS → DXF R12 (AC1009), the 2D exchange format every CAM and laser tool reads.

#pragma once

#include <expected>
#include <span>
#include <string>

#include "nf/svg/export.hpp"

namespace nf::svg {

/// A DXF R12 drawing of model-space curves (millimetres, y up, on layer "0"), per Bézier span:
/// - lines → LINE, circular arcs → ARC (counter-clockwise, as DXF requires), and a closed
///   curve that is one whole circle → CIRCLE; numbers written to read back unchanged. A span
///   counts as circular only if its measured distance from the circle is within the
///   tolerance, and that distance (rounding-level for true circles) counts in `max_error`;
/// - anything else (ellipses, Béziers, other rational spans: R12 has no ELLIPSE or SPLINE) →
///   a POLYLINE within the tolerance (at least 1e-12 of the curve's size, the polyline's
///   floor), which counts in `max_error`.
/// A closed subpath's approximated runs stay separate entities; the drawing stays closed
/// because consecutive entities share their end points exactly.
[[nodiscard]] std::expected<Exported, std::string> write_dxf(std::span<const Subpath> model_paths,
                                                             const ExportOptions& options = {});

}  // namespace nf::svg
