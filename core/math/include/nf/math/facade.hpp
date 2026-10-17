// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// The Swift-facing surface of libnf-math (RFC 0004): non-template value types, no
// exceptions, no std::expected. Swift imports this through Swift 6 C++ interop (see
// Package.swift and swift/NightfallMath); the sketcher and Nightfall Script bind it.

#pragma once

#include <memory>
#include <vector>

namespace nf::facade {

struct Point2 {
  double x = 0, y = 0;
};
struct Point3 {
  double x = 0, y = 0, z = 0;
};

// Swift names template instantiations through typedefs.
using Points2 = std::vector<Point2>;
using Points3 = std::vector<Point3>;
using Numbers = std::vector<double>;

/// A planar NURBS curve. Constructors return an invalid curve (valid() == false) instead of
/// failing; queries on an invalid curve return zeros and empty lists.
class Curve2 {
 public:
  Curve2();

  [[nodiscard]] static Curve2 line(Point2 a, Point2 b);
  /// Elliptical arc: rx, ry > 0, x axis rotated by `rotation`, angles in radians, start < end
  /// ≤ start + 2π.
  [[nodiscard]] static Curve2 arc(Point2 center, double rx, double ry, double rotation,
                                  double start, double end);
  [[nodiscard]] static Curve2 circle(Point2 center, double radius);
  /// Degree = points − 1 (2 … 10 points).
  [[nodiscard]] static Curve2 bezier(const Points2& points);
  /// General NURBS: clamped knots, one point per control point, weights empty or one each.
  [[nodiscard]] static Curve2 nurbs(int degree, const Numbers& knots, const Points2& points,
                                    const Numbers& weights);

  [[nodiscard]] bool valid() const;
  [[nodiscard]] int degree() const;
  [[nodiscard]] bool rational() const;
  [[nodiscard]] double domain_start() const;
  [[nodiscard]] double domain_end() const;
  [[nodiscard]] Numbers knots() const;
  [[nodiscard]] Points2 control_points() const;
  [[nodiscard]] Numbers weights() const;

  [[nodiscard]] Point2 point(double u) const;
  [[nodiscard]] Point2 derivative(double u) const;
  /// Every point of the curve lies within `tolerance` of this polyline (ends and knots
  /// included); empty for a tolerance that is not positive and finite.
  [[nodiscard]] Points2 polyline(double tolerance) const;
  /// The parameter of the point of the curve nearest to p.
  [[nodiscard]] double closest(Point2 p) const;

  struct Impl;  // opaque

 private:
  std::shared_ptr<const Impl> impl_;
};

/// A NURBS curve in space; the same contract as Curve2.
class Curve3 {
 public:
  Curve3();

  [[nodiscard]] static Curve3 line(Point3 a, Point3 b);
  /// Circle about `normal` (non-zero) through `center`.
  [[nodiscard]] static Curve3 circle(Point3 center, Point3 normal, double radius);
  [[nodiscard]] static Curve3 bezier(const Points3& points);
  [[nodiscard]] static Curve3 nurbs(int degree, const Numbers& knots, const Points3& points,
                                    const Numbers& weights);

  [[nodiscard]] bool valid() const;
  [[nodiscard]] int degree() const;
  [[nodiscard]] bool rational() const;
  [[nodiscard]] double domain_start() const;
  [[nodiscard]] double domain_end() const;
  [[nodiscard]] Numbers knots() const;
  [[nodiscard]] Points3 control_points() const;
  [[nodiscard]] Numbers weights() const;

  [[nodiscard]] Point3 point(double u) const;
  [[nodiscard]] Point3 derivative(double u) const;
  [[nodiscard]] Points3 polyline(double tolerance) const;
  [[nodiscard]] double closest(Point3 p) const;

  struct Impl;  // opaque

 private:
  std::shared_ptr<const Impl> impl_;
};

}  // namespace nf::facade
