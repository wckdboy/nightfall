// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
import Foundation

/// Retain with NSXPCListener.service() in the embedded service; anonymous listeners are testable.
public final class KernelListener: NSObject, NSXPCListenerDelegate {
  private let worker = KernelWorker()
  public func listener(
    _ listener: NSXPCListener, shouldAcceptNewConnection connection: NSXPCConnection
  ) -> Bool {
    connection.exportedInterface = NSXPCInterface(with: KernelServiceProtocol.self)
    connection.exportedObject = Endpoint(worker)
    connection.resume()
    return true
  }
  private final class Endpoint: NSObject, KernelServiceProtocol {
    let worker: KernelWorker
    init(_ worker: KernelWorker) { self.worker = worker }
    func build(_ documentID: String, source: Data, reply: @escaping KernelReply) {
      worker.build(documentID, source: source, reply: reply)
    }
    func closeDocument(_ documentID: String) { worker.closeDocument(documentID) }
  }
}
