// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// SPIKE (#8): talks to NightfallKernel.xpc. If the service crashes, the connection is
// interrupted (not invalidated): the next request relaunches it.

import Foundation

@MainActor
final class KernelClient: ObservableObject {
  struct Result {
    var mesh: Data
    var volume: Double
    var milliseconds: Double
  }

  @Published private(set) var status = "idle"
  @Published private(set) var last: Result?
  private(set) var interruptions = 0
  private var connection: NSXPCConnection?

  private func proxy(onError: @escaping @Sendable (Error) -> Void) -> KernelServiceProtocol? {
    if connection == nil {
      let c = NSXPCConnection(serviceName: kernelServiceName)
      c.remoteObjectInterface = NSXPCInterface(with: KernelServiceProtocol.self)
      c.interruptionHandler = { [weak self] in
        Task { @MainActor in
          self?.interruptions += 1
          self?.status = "kernel crashed; it will relaunch on the next request"
        }
      }
      c.invalidationHandler = { [weak self] in
        Task { @MainActor in self?.connection = nil }
      }
      c.resume()
      connection = c
    }
    return connection?.remoteObjectProxyWithErrorHandler(onError) as? KernelServiceProtocol
  }

  /// Builds `ir` in the kernel service; returns nil and sets `status` on failure.
  func build(_ ir: PartIR) async -> Result? {
    guard let data = try? JSONEncoder().encode(ir) else { return nil }
    let start = ContinuousClock.now
    let reply: (Data?, Double, String?) = await withCheckedContinuation { cont in
      let proxy = self.proxy { error in cont.resume(returning: (nil, 0, "XPC: \(error)")) }
      proxy?.build(data) { mesh, volume, error in cont.resume(returning: (mesh, volume, error)) }
    }
    let ms = (ContinuousClock.now - start) / .milliseconds(1)
    guard let mesh = reply.0 else {
      status = "build failed: \(reply.2 ?? "unknown")"
      return nil
    }
    let result = Result(mesh: mesh, volume: reply.1, milliseconds: ms)
    last = result
    status = String(format: "%d triangles, V = %.3f mm³, round trip %.1f ms",
                    mesh.count / (18 * MemoryLayout<Float>.size), reply.1, ms)
    return result
  }

  func crashKernel() {
    proxy { _ in }?.crash()
  }
}
