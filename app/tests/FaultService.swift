// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
import Darwin
import Foundation

@testable import NightfallKernelWorker

final class BlockedEngine: KernelEngine {
  let activeFeature: Int64 = 7
  func build(_ source: Data) -> KernelPayload {
    DispatchSemaphore(value: 0).wait()
    return .failure("kernel", "unreachable")
  }
}
final class FaultEndpoint: NSObject, FaultProtocol {
  private let native = KernelWorker()
  private let blocked = KernelWorker(deadline: 0.3, makeEngine: { BlockedEngine() })
  func build(_ id: String, source: Data, reply: @escaping KernelReply) {
    native.build(id, source: source, reply: reply)
  }
  func closeDocument(_ id: String) { native.closeDocument(id) }
  func fault(_ mode: String, reply: @escaping KernelReply) {
    if mode == "crash" { _exit(99) }
    blocked.build(UUID().uuidString, source: Data(), reply: reply)
  }
}
final class FaultListener: NSObject, NSXPCListenerDelegate {
  private let endpoint = FaultEndpoint()
  func listener(_ listener: NSXPCListener, shouldAcceptNewConnection c: NSXPCConnection) -> Bool {
    c.exportedInterface = NSXPCInterface(with: FaultProtocol.self)
    c.exportedObject = endpoint
    c.resume()
    return true
  }
}
@main enum FaultService {
  static func main() {
    let delegate = FaultListener()
    let listener = NSXPCListener.service()
    listener.delegate = delegate
    withExtendedLifetime(delegate) { listener.resume() }
  }
}
