// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
import Foundation

public let kernelServiceName = "dev.nightfall.Nightfall.Kernel"
public typealias KernelReply = @Sendable (Data, Data, Data, Data, Int32) -> Void

/// Compiled into both app and service; imports no native kernel module.
@objc(NFKernelServiceProtocol) public protocol KernelServiceProtocol {
  /// documentID is a UUID; source is complete v1 JSON, at most 16 MiB.
  /// Reply: JSON manifest, vertices, indices, face ranges, service PID. Buffers are copies
  /// in the C bridge layout: six Float64 per vertex, UInt32 indices, three UInt32 per face
  /// (body_index, first_index, index_count). All quantities use mm; indices are transient.
  /// Any error has empty buffers. Busy requests are rejected without queuing; retry from
  /// the app's latest IR. Timeout replies precede service termination (RFC 0003).
  func build(_ documentID: String, source: Data, reply: @escaping KernelReply)
  /// Release this document's cache after any current rebuild. Unknown IDs are harmless.
  func closeDocument(_ documentID: String)
}
