// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// Phase 1 gate: Swift reads and evaluates libnf-math curves through the facade.

import Foundation
import Testing

@testable import NightfallMath

@Test func circleEvaluatesOnItsRadius() throws {
  let c = try #require(Curve2D.circle(center: SIMD2(1, 2), radius: 3))
  #expect(c.isRational)
  #expect(c.degree == 2)
  for i in 0...40 {
    let u = c.domain.lowerBound + (c.domain.upperBound - c.domain.lowerBound) * Double(i) / 40
    let p = c.point(at: u)
    #expect(abs(((p - SIMD2(1, 2)) * (p - SIMD2(1, 2))).sum().squareRoot() - 3) < 1e-12)
  }
}

@Test func bezierMatchesDeCasteljau() throws {
  let pts: [SIMD2<Double>] = [SIMD2(0, 0), SIMD2(1, 3), SIMD2(4, -2), SIMD2(5, 1)]
  let c = try #require(Curve2D.bezier(pts))
  #expect(c.degree == 3)
  #expect(c.controlPoints == pts)
  for i in 0...20 {
    let t = Double(i) / 20
    var q = pts
    for r in 1..<q.count { for k in 0..<(q.count - r) { q[k] = q[k] * (1 - t) + q[k + 1] * t } }
    #expect(((c.point(at: t) - q[0]) * (c.point(at: t) - q[0])).sum().squareRoot() < 1e-12)
  }
}

@Test func polylineAndClosestPoint() throws {
  let c = try #require(Curve2D.arc(center: .zero, radii: SIMD2(4, 2), from: 0, to: 2))
  let pl = c.polyline(tolerance: 1e-3)
  #expect(pl.count > 10)
  #expect(pl.first == c.point(at: c.domain.lowerBound))
  let u = c.domain.lowerBound + 0.3 * (c.domain.upperBound - c.domain.lowerBound)
  let p = c.point(at: u)
  #expect(abs(c.closestParameter(to: p) - u) < 1e-9)
  #expect(c.polyline(tolerance: 0).isEmpty)
}

@Test func invalidInputGivesNil() {
  #expect(Curve2D.circle(center: .zero, radius: 0) == nil)
  #expect(Curve2D.circle(center: .zero, radius: .nan) == nil)
  #expect(Curve2D.bezier([SIMD2(0, 0)]) == nil)
  #expect(Curve2D.arc(center: .zero, radii: SIMD2(1, 1), from: 1, to: 0) == nil)
  #expect(Curve3D.circle(center: .zero, normal: .zero, radius: 1) == nil)
}

@Test func spaceCurves() throws {
  let c = try #require(Curve3D.circle(center: SIMD3(0, 0, 5), normal: SIMD3(0, 0, 1), radius: 2))
  let p = c.point(at: c.domain.lowerBound)
  #expect(abs(p.z - 5) < 1e-12)
  #expect(abs((SIMD2(p.x, p.y) * SIMD2(p.x, p.y)).sum().squareRoot() - 2) < 1e-12)
  let line = try #require(Curve3D.line(from: .zero, to: SIMD3(1, 2, 3)))
  #expect(line.degree == 1)
  #expect(line.point(at: 1) == SIMD3(1, 2, 3))
  // Space curves have derivatives and control points too (review finding).
  #expect(line.derivative(at: 0.5) == SIMD3(1, 2, 3))
  #expect(line.controlPoints == [SIMD3(0, 0, 0), SIMD3(1, 2, 3)])
  let tangent = c.derivative(at: c.domain.lowerBound)
  #expect(abs(tangent.z) < 1e-12)
  #expect(abs((tangent * (p - SIMD3(0, 0, 5))).sum()) < 1e-9)  // perpendicular to the radius
}
