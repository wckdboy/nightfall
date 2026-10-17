// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// SVG path data (the `d` attribute, SVG 2 §9.3) as exact NURBS, in the path's own user
// units and y-down axes; mapping into model space belongs to the document reader (#61).

#pragma once

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "nf/math/nurbs_curve.hpp"

namespace nf::svg {

/// One subpath: consecutive segments, each the exact curve of one path command (degree 1
/// for L/H/V/Z, 2 for Q/T, 3 for C/S, rational quadratic spans for A). Each segment starts
/// exactly where the previous one ends.
struct Subpath {
  std::vector<math::NurbsCurve2> segments;
  bool closed = false;  // ended with Z; the closing line is the last segment, if non-empty
};

struct ParseError {
  std::size_t offset;  // byte offset in the path data
  std::string message;
};

/// Parses path data into subpaths. Zero-length segments are dropped (they draw nothing in
/// a model), as are subpaths left with no segments. Arcs follow SVG 2 §9.5 (out-of-range
/// radii are scaled up; a zero radius gives a straight line). Malformed data is an error
/// rather than SVG's render-up-to-the-error rule, so a bad import is never silent.
[[nodiscard]] std::expected<std::vector<Subpath>, ParseError> parse_path(std::string_view d);

}  // namespace nf::svg
