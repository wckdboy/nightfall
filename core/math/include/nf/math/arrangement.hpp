// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// The planar arrangement of closed paths: every curve split where it meets another, joined
// at shared vertices, and the faces between them, each with its winding number. Regions
// (fill rules → outer loops and holes, #49) are built on it, and so is sketch profile
// detection.

#pragma once

#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "nf/math/nurbs_curve.hpp"

namespace nf::math {

/// Original input path/curve, before decomposition or splitting. A merged span may have
/// several sources; synthetic closing edges have none. Indices refer to this call's inputs.
struct CurveSource {
  std::size_t path, curve;
  bool operator==(const CurveSource&) const = default;
};
struct Arrangement {
  struct HalfEdge {
    NurbsCurve2 curve;  // from origin to target; the twin runs the same edge backwards
    int origin, target, twin, next, face;
    int direction;  // signed sum of input traversals; coincident opposite paths can cancel
    std::vector<CurveSource> sources{};
  };
  struct Face {
    std::vector<int> half_edges;  // the boundary cycle, the face on the left of each
    double area;                  // signed: > 0 for a counter-clockwise cycle around a bounded face
    /// Winding number of the input in the face on the cycle's left: inside a bounded face,
    /// or, for the outer cycle of a part nested inside another face, in that face; 0 for
    /// the unbounded face.
    int winding;
  };
  std::vector<Vec2> vertices;
  std::vector<HalfEdge> half_edges;
  std::vector<Face> faces;  // one per boundary cycle; each connected part has one outer
};

/// Arranges closed paths: each path is a chain of curves, each starting where the previous
/// ends; an open chain is closed with a straight line, as SVG fills do. Curves are split at
/// every crossing and touching point, vertices closer than 1e-9 of the drawing's size are
/// joined, and each bounded face gets the winding number of the input at an interior point.
///
/// Coincident degree-1 spans are split at overlap endpoints and merged with summed signed
/// traversal counts, including rational lines and multi-span polylines. Their curves retain
/// the original parameterization; zero-count internal seams remain in the arrangement.
/// Shared curved Bezier spans are certified by homogeneous polynomial coefficients,
/// including reversed/partial spans, degree elevation and Mobius reparameterizations.
/// Their pointwise discrepancy is bounded by 1e-12 of local control extent plus eight
/// machine epsilons of coordinate magnitude, independently of drawing-wide vertex welding.
/// Uncertifiable shared parameterizations remain an explicit error (#74).
/// A single curve's own self-intersections (a cubic with a loop) are not split.
[[nodiscard]] std::expected<Arrangement, std::string> arrange(
    std::span<const std::vector<NurbsCurve2>> paths);

/// Winding number of closed curves around p (signed crossings along a ray, retried in other
/// directions when one grazes a vertex or a tangent); nullopt for p on a curve (within 1e-9
/// of the curves' size), where it is undefined.
[[nodiscard]] std::optional<int> winding_number(std::span<const NurbsCurve2> curves, Vec2 p);

enum class FillRule { kNonZero, kEvenOdd };

/// A closed chain of curves: each starts where the previous ends, the last ends at the first.
struct Loop {
  std::vector<NurbsCurve2> curves;
  /// regions() supplies one source set per curve, including reversed hole boundaries.
  /// Callers constructing loops directly may leave this metadata empty.
  std::vector<std::vector<CurveSource>> sources{};
};

/// A connected filled area: its outer boundary (counter-clockwise) and holes (clockwise).
struct Region {
  Loop outer;
  std::vector<Loop> holes;
};

/// The filled areas of closed paths under an SVG fill rule, ready to extrude: the bounded
/// faces of the arrangement whose winding number the rule fills, merged where they share an
/// edge. Areas that touch only at a point are separate regions; a filled island inside a hole
/// is a region of its own.
[[nodiscard]] std::expected<std::vector<Region>, std::string> regions(
    std::span<const std::vector<NurbsCurve2>> paths, FillRule rule);

}  // namespace nf::math
