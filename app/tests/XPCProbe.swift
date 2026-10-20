// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
import Darwin
import Foundation
import Synchronization

final class Outcome: Sendable {
  private let result = Mutex<Result<Reply, Failure>?>(nil)
  private let signal = DispatchSemaphore(value: 0)
  func complete(_ value: Result<Reply, Failure>) {
    let won = result.withLock {
      guard $0 == nil else { return false }
      $0 = value
      return true
    }
    if won { signal.signal() }
  }
  func receive(_ m: Data, _ v: Data, _ i: Data, _ f: Data, _ pid: Int32) {
    complete(.success(Reply(manifest: m, vertices: v, indices: i, faces: f, pid: pid)))
  }
  func wait() throws -> Reply {
    try check(signal.wait(timeout: .now() + 12) == .success, "XPC reply deadline")
    return try result.withLock { try $0!.get() }
  }
}
final class Interruptions: Sendable {
  let count = Mutex(0)
  let signal = DispatchSemaphore(value: 0)
}
final class Host {
  let interruptions = Interruptions()
  let connection: NSXPCConnection
  init(fault: Bool = false) {
    connection = NSXPCConnection(serviceName: kernelServiceName)
    connection.remoteObjectInterface = NSXPCInterface(
      with: fault ? FaultProtocol.self : KernelServiceProtocol.self)
    let count = interruptions
    connection.interruptionHandler = {
      count.count.withLock { $0 += 1 }
      count.signal.signal()
    }
    connection.resume()
  }
  deinit { connection.invalidate() }
  func request(_ source: Data = plate(), id: String = document, fault: String? = nil) throws
    -> Reply
  {
    let box = Outcome()
    let proxy = connection.remoteObjectProxyWithErrorHandler {
      box.complete(.failure(Failure(description: "transport: \($0)")))
    }
    if let fault {
      guard let p = proxy as? FaultProtocol else { throw Failure(description: "fault proxy") }
      p.fault(fault, reply: box.receive)
    } else {
      guard let p = proxy as? KernelServiceProtocol else {
        throw Failure(description: "kernel proxy")
      }
      p.build(id, source: source, reply: box.receive)
    }
    return try box.wait()
  }
  func close() throws {
    guard let p = connection.remoteObjectProxy as? KernelServiceProtocol else {
      throw Failure(description: "close proxy")
    }
    p.closeDocument(document)
  }
  func waitForExit() throws {
    try check(
      interruptions.signal.wait(timeout: .now() + 5) == .success, "service did not interrupt")
  }
}
func smoke() throws {
  let host = Host()
  let first = try host.request()
  try geometry(first, volume: 6000, area: 2200)
  try check(first.pid > 0 && first.pid != getpid(), "service is not a separate process")
  try check(try cacheHits(first) == 0, "first build is not cold")
  let warm = try host.request()
  try check(warm.pid == first.pid && cacheHits(warm) == 2, "warm cache/process")
  try check(try cacheHits(host.request(id: UUID().uuidString)) == 0, "document isolation")
  try host.close()
  try check(try cacheHits(host.request()) == 0, "document closure")
  _ = try failed(host.request(Data("{}".utf8)), code: "schema")
  _ = try failed(host.request(id: "invalid"), code: "schema")
  try geometry(first, volume: 6000, area: 2200)
  try geometry(host.request(plate(15)), volume: 3000, area: 1300)
  try check(host.interruptions.count.withLock { $0 } == 0, "unexpected interruption")
  print("service pid=\(first.pid), app pid=\(getpid()); copied geometry, caches and errors passed")
}
func references(_ directory: String) throws {
  let host = Host()
  let files = try FileManager.default.contentsOfDirectory(
    at: URL(fileURLWithPath: directory), includingPropertiesForKeys: nil
  ).filter { $0.pathExtension == "nightfall" }.sorted { $0.path < $1.path }
  try check(files.count == 20, "reference corpus size")
  for file in files {
    let data = try Data(contentsOf: file)
    let reply = try host.request(data, id: UUID().uuidString)
    try check(reply.pid != getpid(), "in-process reference build")
    try geometry(reply)
    print("\(file.lastPathComponent): mesh volume/area within 1%")
  }
}
func restart(_ mode: String) throws {
  let fault = mode != "restart"
  let host = Host(fault: fault)
  let first = try host.request()
  try geometry(first, volume: 6000, area: 2200)
  let start = ContinuousClock.now
  if mode == "restart" {
    try check(kill(first.pid, SIGKILL) == 0, "cannot terminate owned kernel service")
  } else if mode == "crash" {
    do {
      _ = try host.request(fault: "crash")
      throw Failure(description: "crashed request succeeded")
    } catch let error as Failure {
      try check(
        error.description.hasPrefix("transport:"), "crash did not fail the request: \(error)")
    }
  } else {
    let timeout = try host.request(fault: "hang")
    let error = try failed(timeout, code: "timeout")
    try check(integer(error["feature_index"]) == 7, "hang feature diagnostic")
    try check(timeout.pid == first.pid, "watchdog reply process")
    try check(ContinuousClock.now - start < .seconds(3), "watchdog deadline")
  }
  try host.waitForExit()
  host.connection.invalidate()
  let fresh = Host(fault: fault)
  let rebuilt = try fresh.request()
  try check(rebuilt.pid != first.pid && rebuilt.pid != getpid(), "service was not relaunched")
  try geometry(rebuilt, volume: 6000, area: 2200)
  try check(try cacheHits(rebuilt) == 0, "restarted service retained geometry cache")
  try check(try cacheHits(fresh.request()) == 2, "replayed service did not warm")
  try geometry(first, volume: 6000, area: 2200)
  let ms = (ContinuousClock.now - start) / .milliseconds(1)
  print("\(mode): pid \(first.pid) → \(rebuilt.pid), complete IR replay in \(ms) ms")
}
/// Private developer measurement; timings are observations, never CI latency assertions.
func nativeBenchmark(_ path: String, samples: Int) throws {
  try check((1...10).contains(samples), "benchmark samples must be 1...10")
  let source = try Data(contentsOf: URL(fileURLWithPath: path))
  let host = Host()
  let ready = try host.request(id: UUID().uuidString)
  func emit(_ value: [String: Any]) throws {
    var line = try JSONSerialization.data(withJSONObject: value, options: [.sortedKeys])
    line.append(10)
    FileHandle.standardOutput.write(line)
  }
  try emit(["kind": "ready", "service_pid": ready.pid, "host_pid": getpid()])
  for sample in 0..<samples {
    let start = ContinuousClock.now
    do {
      let reply = try host.request(source)
      let received = ContinuousClock.now
      let value = try object(reply)
      let errors = value["errors"] as? [[String: Any]] ?? []
      let converted: ContinuousClock.Instant
      if errors.isEmpty {
        _ = try DisplayMesh(
          KernelResponse(
            manifest: reply.manifest, vertices: reply.vertices, indices: reply.indices,
            faces: reply.faces))
        converted = .now
      } else {
        converted = received
      }
      try emit([
        "kind": "sample", "sample": sample, "service_pid": reply.pid,
        "roundtrip_ms": (received - start) / .milliseconds(1),
        "display_ms": (converted - received) / .milliseconds(1),
        "triangles": reply.indices.count / 12, "manifest_bytes": reply.manifest.count,
        "mesh_bytes": reply.vertices.count + reply.indices.count + reply.faces.count,
        "cache": value["cache"] ?? [:], "errors": errors,
      ])
      if !errors.isEmpty { break }
    } catch {
      try emit([
        "kind": "failure", "sample": sample,
        "roundtrip_ms": (ContinuousClock.now - start) / .milliseconds(1),
        "error": String(describing: error),
      ])
      throw error
    }
  }
}
@main enum XPCProbe {
  @MainActor static func main() async {
    do {
      let mode = CommandLine.arguments.dropFirst().first ?? "smoke"
      switch mode {
      case "benchmark":
        try nativeBenchmark(
          CommandLine.arguments[2], samples: Int(CommandLine.arguments[3]) ?? 0)
      case "smoke": try smoke()
      case "client": try await clientIntegration()
      case "preview": try await previewIntegration(CommandLine.arguments[2])
      case "history": try await historyIntegration(CommandLine.arguments[2])
      case "drafts": try await draftsIntegration(CommandLine.arguments[2])
      case "document-sequences":
        try await documentSequenceIntegration(
          CommandLine.arguments[2], output: CommandLine.arguments[3])
      case "nocturne":
        try await nocturneIntegration(CommandLine.arguments[2], output: CommandLine.arguments[3])
      case "sheets": try sheetLifecycle()
      case "fillets": try await filletIntegration(CommandLine.arguments[2])
      case "sweeps":
        try await sweepIntegration(CommandLine.arguments[2], output: CommandLine.arguments[3])
      case "lofts":
        try await loftIntegration(CommandLine.arguments[2], output: CommandLine.arguments[3])
      case "visibility":
        try await visibilityIntegration(CommandLine.arguments[2], output: CommandLine.arguments[3])
      case "scalability":
        try await scalabilityIntegration(CommandLine.arguments[2], output: CommandLine.arguments[3])
      case "render":
        try await rendererIntegration(CommandLine.arguments[2], output: CommandLine.arguments[3])
      case "references": try references(CommandLine.arguments[2])
      case "restart", "crash", "hang": try restart(mode)
      default: throw Failure(description: "unknown mode")
      }
    } catch {
      FileHandle.standardError.write(Data("XPC probe: \(error)\n".utf8))
      exit(1)
    }
  }
}
