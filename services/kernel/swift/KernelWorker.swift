// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
import Darwin
import Foundation
import NightfallKernelBridge
import Synchronization

// Shared by all workers, including native destruction; never block Swift's cooperative pool.
let kernelWorkQueue = DispatchQueue(label: "dev.nightfall.kernel.worker", qos: .userInitiated)

struct KernelPayload: Sendable {
  let manifest: Data
  var vertices = Data(), indices = Data(), faces = Data()
  func deliver(_ reply: KernelReply) {
    reply(manifest, vertices, indices, faces, ProcessInfo.processInfo.processIdentifier)
  }
  static func failure(_ code: String, _ message: String, feature: Int64 = -1) -> Self {
    var error: [String: Any] = ["code": code, "message": message, "pointer": ""]
    if feature >= 0 {
      error["feature_index"] = feature
      error["pointer"] = "/features/\(feature)"
    }
    let value: [String: Any] = [
      "errors": [error], "features": [], "bodies": [],
      "face_names": [], "cache": [:],
    ]
    return Self(
      manifest: (try? JSONSerialization.data(withJSONObject: value, options: .sortedKeys))
        ?? Data(
          "{\"errors\":[{\"code\":\"kernel\",\"message\":\"diagnostic encoding failed\",\"pointer\":\"\"}]}"
            .utf8))
  }
}
protocol KernelEngine: AnyObject, Sendable {
  func build(_ source: Data) -> KernelPayload
  var activeFeature: Int64 { get }
}
// The pointer is used only on kernelWorkQueue, except the C ABI's atomic progress accessor.
// Watchdog readers retain this wrapper. Final destruction is transferred back to that queue.
private final class NativeEngine: KernelEngine, @unchecked Sendable {
  private let handle: OpaquePointer
  init?() {
    dispatchPrecondition(condition: .onQueue(kernelWorkQueue))
    guard let handle = nf_session_create() else { return nil }
    self.handle = handle
  }
  deinit {
    let address = UInt(bitPattern: handle)
    kernelWorkQueue.async { nf_session_destroy(OpaquePointer(bitPattern: address)) }
  }
  var activeFeature: Int64 { nf_session_active_feature(handle) }
  func build(_ source: Data) -> KernelPayload {
    dispatchPrecondition(condition: .onQueue(kernelWorkQueue))
    let result = source.withUnsafeBytes {
      nf_session_build(handle, $0.bindMemory(to: CChar.self).baseAddress, source.count, nil)
    }
    guard let result else { return .failure("kernel", "native result allocation failed") }
    defer { nf_result_free(result) }
    func copy<T>(_ pointer: UnsafePointer<T>?, _ count: Int) -> Data {
      guard let pointer, count > 0 else { return Data() }
      return Data(bytes: pointer, count: count * MemoryLayout<T>.stride)
    }
    var count = 0
    let json = nf_result_json(result, &count)
    let manifest = copy(json, count)
    let vertices = nf_result_vertices(result, &count)
    let vertexData = copy(vertices, count)
    let indices = nf_result_indices(result, &count)
    let indexData = copy(indices, count)
    let faces = nf_result_faces(result, &count)
    return KernelPayload(
      manifest: manifest, vertices: vertexData, indices: indexData,
      faces: copy(faces, count))
  }
}
final class PendingReply: Sendable {
  private let state: Mutex<(reply: KernelReply?, engine: (any KernelEngine)?)>
  init(_ reply: @escaping KernelReply) { state = Mutex((reply, nil)) }
  func attach(_ engine: any KernelEngine) -> Bool {
    state.withLock {
      guard $0.reply != nil else { return false }
      $0.engine = engine
      return true
    }
  }
  func claim(timeout: Bool = false) -> (KernelReply, Int64)? {
    state.withLock {
      guard let reply = $0.reply else { return nil }
      let index = timeout ? ($0.engine?.activeFeature ?? -1) : -1
      $0.reply = nil
      $0.engine = nil
      return (reply, index)
    }
  }
}
/// One shared instance per service process. All stored mutable state is protected by Mutex.
/// Four LRU document caches are retained; native estimates/readers are not a process RSS cap.
final class KernelWorker: Sendable {
  private enum Admission { case idle, building, timedOut }
  private let admission = Mutex(Admission.idle)
  private let sessions = Mutex<[(String, any KernelEngine)]>([])
  private let deadline: TimeInterval
  private let makeEngine: @Sendable () -> (any KernelEngine)?
  private let terminate: @Sendable (Int32) -> Void
  init(
    deadline: TimeInterval = 5,
    makeEngine: @escaping @Sendable () -> (any KernelEngine)? = { NativeEngine() },
    terminate: @escaping @Sendable (Int32) -> Void = { _exit($0) }
  ) {
    precondition(deadline.isFinite && deadline > 0)
    self.deadline = deadline
    self.makeEngine = makeEngine
    self.terminate = terminate
  }
  func build(_ documentID: String, source: Data, reply: @escaping KernelReply) {
    guard documentID.utf8.count <= 64, let id = UUID(uuidString: documentID)?.uuidString else {
      KernelPayload.failure("schema", "documentID must be a UUID").deliver(reply)
      return
    }
    guard source.count <= 16 * 1024 * 1024 else {
      KernelPayload.failure("bounds", "document exceeds 16 MiB").deliver(reply)
      return
    }
    let rejection: KernelPayload? = admission.withLock {
      switch $0 {
      case .building: return .failure("busy", "kernel is already rebuilding")
      case .timedOut: return .failure("kernel_unavailable", "kernel timed out; reconnect")
      case .idle:
        $0 = .building
        return nil
      }
    }
    if let rejection {
      rejection.deliver(reply)
      return
    }
    let pending = PendingReply(reply)
    let timer = DispatchSource.makeTimerSource(queue: .global(qos: .userInitiated))
    timer.schedule(deadline: .now() + deadline, leeway: .milliseconds(1))
    timer.setEventHandler { [self] in
      guard let (callback, index) = pending.claim(timeout: true) else { return }
      admission.withLock { $0 = .timedOut }
      KernelPayload.failure(
        "timeout",
        index >= 0
          ? "kernel timed out at feature \(index)"
          : "kernel request timed out", feature: index
      ).deliver(callback)
      terminate(124)
    }
    timer.resume()
    kernelWorkQueue.async { [self] in
      let engine = sessions.withLock { entries -> (any KernelEngine)? in
        if let i = entries.firstIndex(where: { $0.0 == id }) {
          let entry = entries.remove(at: i)
          entries.append(entry)
          return entry.1
        }
        guard let engine = makeEngine() else { return nil }
        if entries.count == 4 { entries.removeFirst() }
        entries.append((id, engine))
        return engine
      }
      let payload: KernelPayload
      if let engine {
        guard pending.attach(engine) else {
          timer.cancel()
          return
        }
        payload = engine.build(source)
      } else {
        payload = .failure("kernel_unavailable", "native session creation failed")
      }
      guard let (callback, _) = pending.claim() else {
        timer.cancel()
        return
      }
      timer.cancel()
      admission.withLock { $0 = .idle }
      payload.deliver(callback)
    }
  }
  func closeDocument(_ documentID: String) {
    guard documentID.utf8.count <= 64, let id = UUID(uuidString: documentID)?.uuidString else {
      return
    }
    kernelWorkQueue.async { [self] in sessions.withLock { $0.removeAll { $0.0 == id } } }
  }
}
