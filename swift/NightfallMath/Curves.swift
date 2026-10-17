// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

import CNightfallMath

/// A planar NURBS curve: libnf-math through its C++ facade. Values; cheap to copy.
public struct Curve2D {
  let raw: nf.facade.Curve2

  init?(_ raw: nf.facade.Curve2) {
    guard raw.valid() else { return nil }
    self.raw = raw
  }

  public static func line(from a: SIMD2<Double>, to b: SIMD2<Double>) -> Curve2D? {
    Curve2D(nf.facade.Curve2.line(point(a), point(b)))
  }

  /// Elliptical arc; angles in radians, counter-clockwise, at most a full turn.
  public static func arc(
    center: SIMD2<Double>, radii: SIMD2<Double>, rotation: Double = 0, from start: Double,
    to end: Double
  ) -> Curve2D? {
    Curve2D(nf.facade.Curve2.arc(point(center), radii.x, radii.y, rotation, start, end))
  }

  public static func circle(center: SIMD2<Double>, radius: Double) -> Curve2D? {
    Curve2D(nf.facade.Curve2.circle(point(center), radius))
  }

  /// Bézier curve of degree `points.count − 1` (2 … 10 points).
  public static func bezier(_ points: [SIMD2<Double>]) -> Curve2D? {
    var v = nf.facade.Points2()
    for p in points { v.push_back(point(p)) }
    return Curve2D(nf.facade.Curve2.bezier(v))
  }

  public var degree: Int { Int(raw.degree()) }
  public var isRational: Bool { raw.rational() }
  public var domain: ClosedRange<Double> { raw.domain_start()...raw.domain_end() }
  public var controlPoints: [SIMD2<Double>] { raw.control_points().map { SIMD2($0.x, $0.y) } }

  public func point(at u: Double) -> SIMD2<Double> {
    let p = raw.point(u)
    return SIMD2(p.x, p.y)
  }

  public func derivative(at u: Double) -> SIMD2<Double> {
    let d = raw.derivative(u)
    return SIMD2(d.x, d.y)
  }

  /// Vertices such that every point of the curve lies within `tolerance` of the polyline.
  public func polyline(tolerance: Double) -> [SIMD2<Double>] {
    raw.polyline(tolerance).map { SIMD2($0.x, $0.y) }
  }

  /// The parameter of the point nearest to `p`.
  public func closestParameter(to p: SIMD2<Double>) -> Double { raw.closest(Curve2D.point(p)) }

  static func point(_ p: SIMD2<Double>) -> nf.facade.Point2 { nf.facade.Point2(x: p.x, y: p.y) }
}

/// A NURBS curve in space; the same contract as `Curve2D`.
public struct Curve3D {
  let raw: nf.facade.Curve3

  init?(_ raw: nf.facade.Curve3) {
    guard raw.valid() else { return nil }
    self.raw = raw
  }

  public static func line(from a: SIMD3<Double>, to b: SIMD3<Double>) -> Curve3D? {
    Curve3D(nf.facade.Curve3.line(point(a), point(b)))
  }

  public static func circle(center: SIMD3<Double>, normal: SIMD3<Double>, radius: Double) -> Curve3D? {
    Curve3D(nf.facade.Curve3.circle(point(center), point(normal), radius))
  }

  public static func bezier(_ points: [SIMD3<Double>]) -> Curve3D? {
    var v = nf.facade.Points3()
    for p in points { v.push_back(point(p)) }
    return Curve3D(nf.facade.Curve3.bezier(v))
  }

  public var degree: Int { Int(raw.degree()) }
  public var isRational: Bool { raw.rational() }
  public var domain: ClosedRange<Double> { raw.domain_start()...raw.domain_end() }
  public var controlPoints: [SIMD3<Double>] { raw.control_points().map { SIMD3($0.x, $0.y, $0.z) } }

  public func point(at u: Double) -> SIMD3<Double> {
    let p = raw.point(u)
    return SIMD3(p.x, p.y, p.z)
  }

  public func derivative(at u: Double) -> SIMD3<Double> {
    let d = raw.derivative(u)
    return SIMD3(d.x, d.y, d.z)
  }

  public func polyline(tolerance: Double) -> [SIMD3<Double>] {
    raw.polyline(tolerance).map { SIMD3($0.x, $0.y, $0.z) }
  }

  public func closestParameter(to p: SIMD3<Double>) -> Double { raw.closest(Curve3D.point(p)) }

  static func point(_ p: SIMD3<Double>) -> nf.facade.Point3 {
    nf.facade.Point3(x: p.x, y: p.y, z: p.z)
  }
}
