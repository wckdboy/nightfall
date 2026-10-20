// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
import Foundation
import Synchronization

struct KernelDiagnostic: Codable, Sendable, Equatable {
  let code: String, message: String
  var pointer = ""
  var featureIndex: Int?
  enum CodingKeys: String, CodingKey {
    case code, message, pointer
    case featureIndex = "feature_index"
  }
}
/// Owned XPC bytes in KernelServiceProtocol's layout. Recovery never changes authored IR.
struct KernelResponse: Sendable {
  let manifest: Data
  var vertices = Data(), indices = Data(), faces = Data()
  var processID: Int32 = 0
  var recovery: KernelDiagnostic?
  private var parsedDiagnostics: [KernelDiagnostic]?
  init(
    manifest: Data, vertices: Data = Data(), indices: Data = Data(), faces: Data = Data(),
    processID: Int32 = 0
  ) {
    self.manifest = manifest
    self.vertices = vertices
    self.indices = indices
    self.faces = faces
    self.processID = processID
  }
  private struct Envelope: Codable { let errors: [KernelDiagnostic] }
  var diagnostics: [KernelDiagnostic] {
    parsedDiagnostics ?? (try? JSONDecoder().decode(Envelope.self, from: manifest).errors)
      ?? [KernelDiagnostic(code: "protocol", message: "malformed kernel manifest")]
  }
  static func failure(_ code: String, _ message: String, feature: Int? = nil) -> Self {
    let error = KernelDiagnostic(code: code, message: message, featureIndex: feature)
    return Self(manifest: (try? JSONEncoder().encode(Envelope(errors: [error]))) ?? Data())
  }
  fileprivate func validated() -> Self {
    guard manifest.count <= 16 * 1024 * 1024,
      let envelope = try? JSONDecoder().decode(Envelope.self, from: manifest),
      vertices.count <= 3_000_000 * 48, indices.count <= 3_000_000 * 4,
      faces.count <= 1_000_000 * 12,
      vertices.count % 48 == 0, indices.count % 12 == 0, faces.count % 12 == 0,
      envelope.errors.isEmpty || (vertices.isEmpty && indices.isEmpty && faces.isEmpty)
    else { return .failure("protocol", "invalid kernel result or partial error geometry") }
    var result = self
    result.parsedDiagnostics = envelope.errors
    return result
  }
}
@MainActor protocol KernelTransport: AnyObject {
  func send(
    _ id: UUID, source: Data, reply: @escaping @Sendable (KernelResponse) -> Void,
    failure: @escaping @Sendable (String) -> Void)
  func close(_ id: UUID)
  func invalidate()
}
/// Foundation-only adapter; the app compiles the shared protocol, never the native worker.
@MainActor private final class XPCTransport: KernelTransport {
  private let connection: NSXPCConnection
  init(event: @escaping @Sendable (String) -> Void) {
    connection = NSXPCConnection(serviceName: kernelServiceName)
    connection.remoteObjectInterface = NSXPCInterface(with: KernelServiceProtocol.self)
    // Foundation's class declarations do not mark these callbacks Sendable. Give them
    // explicit types so Swift cannot infer main-actor isolation for XPC's private queue.
    let interrupted: @Sendable () -> Void = { event("kernel_restarted") }
    let invalidated: @Sendable () -> Void = { event("kernel_unavailable") }
    connection.interruptionHandler = interrupted
    connection.invalidationHandler = invalidated
    connection.resume()
  }
  func send(
    _ id: UUID, source: Data, reply: @escaping @Sendable (KernelResponse) -> Void,
    failure: @escaping @Sendable (String) -> Void
  ) {
    let onError: @Sendable (Error) -> Void = { failure($0.localizedDescription) }
    guard
      let proxy = connection.remoteObjectProxyWithErrorHandler(onError)
        as? KernelServiceProtocol
    else {
      failure("kernel proxy unavailable")
      return
    }
    proxy.build(id.uuidString, source: source) { m, v, i, f, pid in
      reply(KernelResponse(manifest: m, vertices: v, indices: i, faces: f, processID: pid))
    }
  }
  func close(_ id: UUID) {
    let ignore: @Sendable (Error) -> Void = { _ in }
    (connection.remoteObjectProxyWithErrorHandler(ignore) as? KernelServiceProtocol)?
      .closeDocument(id.uuidString)
  }
  func invalidate() { connection.invalidate() }
}
/// Claim on the transport callback queue, before validation hops to another executor.
private final class ReplyReceipt: Sendable {
  private let state = Mutex(false)
  var received: Bool { state.withLock { $0 } }
  func claim() -> Bool {
    state.withLock {
      guard !$0 else { return false }
      $0 = true
      return true
    }
  }
}
/// One rebuild per client; complete IR is replayed after any connection failure.
/// Call shutdown when the owner closes; close(id) releases one document's service cache.
@MainActor final class KernelClient {
  typealias TransportFactory =
    @MainActor (@escaping @Sendable (String) -> Void) -> any KernelTransport
  private struct Pending {
    let token: UUID
    let continuation: CheckedContinuation<KernelResponse, Never>
    let receipt: ReplyReceipt
  }
  private let deadline: Duration
  private let makeTransport: TransportFactory
  private var transport: (any KernelTransport)?
  private var generation: UUID?
  private var pending: Pending?
  private var timer: Task<Void, Never>?
  private var building = false
  init(
    deadline: Duration = .seconds(7),
    makeTransport: @escaping TransportFactory = { XPCTransport(event: $0) }
  ) {
    precondition(deadline > .zero)
    self.deadline = deadline
    self.makeTransport = makeTransport
  }
  /// A valid timeout culprit is suppressed only in one temporary retry. Check recovery
  /// even on success; no recovery edit is committed to the caller's source bytes.
  func build(_ id: UUID, source: Data) async -> KernelResponse {
    guard !Task.isCancelled else { return .failure("cancelled", "rebuild cancelled") }
    guard !building else { return .failure("busy", "a rebuild is already in progress") }
    guard source.count <= 16 * 1024 * 1024 else {
      return .failure("bounds", "document exceeds 16 MiB")
    }
    building = true
    defer { building = false }
    let first = await request(id, source)
    guard !Task.isCancelled else { return .failure("cancelled", "rebuild cancelled") }
    guard let culprit = first.diagnostics.first(where: { $0.code == "timeout" }),
      let index = culprit.featureIndex,
      var document = (try? JSONSerialization.jsonObject(with: source)) as? [String: Any],
      var features = document["features"] as? [[String: Any]], features.indices.contains(index),
      features[index]["suppressed"] as? Bool != true
    else { return first }
    features[index]["suppressed"] = true
    document["features"] = features
    guard let replay = try? JSONSerialization.data(withJSONObject: document, options: .sortedKeys)
    else { return first }
    var result = await request(id, replay)
    if Task.isCancelled { result = .failure("cancelled", "rebuild cancelled") }
    result.recovery = culprit
    return result
  }
  func close(_ id: UUID) { transport?.close(id) }
  func shutdown() {
    if let token = pending?.token {
      finish(token, .failure("cancelled", "rebuild cancelled"), retire: true)
    } else {
      retire()
    }
  }
  private func retire() {
    generation = nil
    transport?.invalidate()
    transport = nil
  }
  private func finish(_ token: UUID, _ response: KernelResponse, retire: Bool = false) {
    guard let current = pending, current.token == token else { return }
    pending = nil
    timer?.cancel()
    timer = nil
    if retire { self.retire() }
    current.continuation.resume(returning: response)
  }
  private func disconnected(
    _ serial: UUID?, _ code: String,
    message: String = "kernel service disconnected; rebuild to replay the document"
  ) {
    guard generation == serial else { return }
    if let current = pending, !current.receipt.received {
      finish(current.token, .failure(code, message), retire: true)
    } else {
      // A received reply still must pass validation and the existing backstop. A
      // following disconnect retires the connection without discarding that packet.
      retire()
    }
  }
  private func request(_ id: UUID, _ source: Data) async -> KernelResponse {
    let token = UUID()
    return await withTaskCancellationHandler {
      await withCheckedContinuation { continuation in
        guard !Task.isCancelled else {
          continuation.resume(returning: .failure("cancelled", "rebuild cancelled"))
          return
        }
        guard source.count <= 16 * 1024 * 1024 else {
          continuation.resume(returning: .failure("bounds", "document exceeds 16 MiB"))
          return
        }
        let receipt = ReplyReceipt()
        pending = Pending(token: token, continuation: continuation, receipt: receipt)
        if transport == nil {
          let serial = UUID()
          generation = serial
          transport = makeTransport { [weak self] code in
            Task { @MainActor [weak self] in self?.disconnected(serial, code) }
          }
        }
        let serial = generation
        let duration = deadline
        timer = Task { [weak self] in
          do { try await Task.sleep(for: duration) } catch { return }
          self?.finish(
            token, .failure("kernel_unresponsive", "kernel did not reply before the deadline"),
            retire: true)
        }
        transport?.send(id, source: source) { [weak self] response in
          guard receipt.claim() else { return }
          Task.detached { [weak self] in
            let result = response.validated()
            await self?.finish(
              token, result,
              retire: result.diagnostics.contains { $0.code == "timeout" || $0.code == "protocol" })
          }
        } failure: { [weak self] message in
          Task { @MainActor [weak self] in
            self?.disconnected(serial, "kernel_unavailable", message: message)
          }
        }
      }
    } onCancel: { [weak self] in
      Task { @MainActor [weak self] in
        self?.finish(token, .failure("cancelled", "rebuild cancelled"), retire: true)
      }
    }
  }
}
