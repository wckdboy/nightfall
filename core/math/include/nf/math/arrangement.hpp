// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// The planar arrangement of closed paths: every curve split where it meets another, joined
// at shared vertices, and the faces between them, each with its winding number. Regions
// (fill rules → outer loops and holes, #49) are built on it, and so is sketch profile
// detection.

#pragma once

#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "nf/math/nurbs_curve.hpp"

namespace nf::math {

struct Arrangement {
  struct HalfEdge {
    NurbsCurve2 curve;  // from origin to target; the twin runs the same edge backwards
    int origin, target, twin, next, face;
    int direction;  // +1 along its input path, −1 against
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
/// Not supported yet (an error): curves that share a stretch (coincident edges, #74).
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
