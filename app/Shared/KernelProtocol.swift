// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// SPIKE (#8): the XPC interface between the app and NightfallKernel.xpc, plus the tiny JSON
// part IR it carries. Compiled into both targets.

import Foundation

/// Mach service name of the embedded kernel XPC service.
public let kernelServiceName = "dev.nightfall.Nightfall.Kernel"

@objc public protocol KernelServiceProtocol {
  /// Builds the part described by `ir` (JSON `PartIR`). Replies with the flat-shaded mesh
  /// (6 × Float32 per vertex: position, normal) and the exact B-rep volume, or an error.
  func build(_ ir: Data, reply: @escaping @Sendable (Data?, Double, String?) -> Void)

  /// Deliberately terminates the service: used to prove the app survives kernel crashes.
  func crash()
}

/// Minimal feature-list IR for the spike: a box, then optional fillet and holes.
public struct PartIR: Codable, Sendable {
  public struct Feature: Codable, Sendable {
    public var type: String  // "box" | "fillet_all" | "hole"
    public var size: [Double]? = nil  // box: [dx, dy, dz] mm
    public var radius: Double? = nil  // fillet_all, hole: mm
    public var at: [Double]? = nil  // hole: [x, y] mm
  }

  public var version = 1
  public var features: [Feature]
}
