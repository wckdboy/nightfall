// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// SVG files → model-space NURBS (millimetres, y up). The geometry stays exact: transforms
// are affine and are applied to control points.

#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "nf/svg/path.hpp"

namespace nf::svg {

struct Drawing {
  std::vector<Subpath> paths;         // model space: millimetres, y up
  std::vector<std::string> warnings;  // content that was skipped, one line each
};

/// Reads the shapes of an SVG document: path, rect (with rounded corners), circle, ellipse,
/// line, polyline and polygon, inside g and nested svg elements, with every `transform`.
///
/// Placement: the root svg's user-space origin is the model origin, and y is negated. The
/// root's width/height (mm, cm, in, pt, pc, Q, px) and viewBox only set the scale, uniform
/// unless preserveAspectRatio="none"; without a viewBox, or without a size, a user unit is
/// a CSS pixel (25.4/96 mm). write_document's output therefore reads back unchanged.
///
/// Text, images, `use` references and elements in other namespaces are skipped with a
/// warning; definitions (defs, symbol, clipPath, mask, pattern, marker, gradients,
/// metadata, style) are skipped silently, as are elements with display="none". Malformed
/// XML, path data or numbers are errors.
[[nodiscard]] std::expected<Drawing, ParseError> read_document(std::string_view svg);

}  // namespace nf::svg
