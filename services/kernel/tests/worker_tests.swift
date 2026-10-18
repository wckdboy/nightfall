// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
import Darwin
import Foundation
import Synchronization

@testable import NightfallKernelWorker

struct Failure: Error, CustomStringConvertible { let description: String }
func check(_ condition: @autoclosure () throws -> Bool, _ message: String) throws {
  if try !condition() { throw Failure(description: message) }
}
final class Inbox: Sendable {
  let messages = Mutex<[KernelPayload]>([])
  let processIDs = Mutex<[Int32]>([])
  let signal = DispatchSemaphore(value: 0)
  func receive(_ manifest: Data, _ vertices: Data, _ indices: Data, _ faces: Data, _ pid: Int32) {
    processIDs.withLock { $0.append(pid) }
    messages.withLock {
      $0.append(
        KernelPayload(
          manifest: manifest, vertices: vertices,
          indices: indices, faces: faces))
    }
    signal.signal()
  }
  func wait() throws -> KernelPayload {
    try check(signal.wait(timeout: .now() + 10) == .success, "missing reply")
    return messages.withLock { $0.last! }
  }
}
final class Probe: Sendable {
  let count = Mutex(0)
  let entered = DispatchSemaphore(value: 0)
  let release = DispatchSemaphore(value: 0)
  let terminated = DispatchSemaphore(value: 0)
  let status = Mutex<Int32?>(nil)
  func next() -> Int {
    count.withLock {
      $0 += 1
      return $0
    }
  }
}
final class FakeEngine: KernelEngine {
  let id: Int
  let blocked: Probe?
  let calls = Mutex(0)
  let activeFeature: Int64 = 7
  init(_ id: Int = 0, blocked: Probe? = nil) {
    self.id = id
    self.blocked = blocked
  }
  func build(_ source: Data) -> KernelPayload {
    if let blocked,
      calls.withLock({
        $0 += 1
        return $0
      }) == 1
    {
      blocked.entered.signal()
      blocked.release.wait()
    }
    return KernelPayload(manifest: Data("{\"errors\":[],\"tag\":\(id)}".utf8))
  }
}
let document = "93A03342-B5B6-468F-AFD7-8BA2570AB681"
func plate(_ depth: Int = 30) -> Data {
  Data(
    """
    {"format":"nightfall.part","version":1,"units":{"length":"mm","angle":"deg"},
     "meta":{},"params":[{"name":"depth","value":"\(depth) mm"}],"references":[],
     "features":[{"id":"outline","type":"sketch","inputs":{"plane":{"datum":"xy"},"entities":[
       {"id":"r","type":"rect","corner":[0,0],"size":[10,20]}],"constraints":[]}},
       {"id":"base","type":"extrude","inputs":{"profile":{"sketch":"outline"},
       "distance":"depth","result":{"new":"plate","body_type":"brep"}}}],
     "history_end":null,"ui":{}}
    """.utf8)
}
func request(_ worker: KernelWorker, id: String = document, source: Data = plate()) throws
  -> KernelPayload
{
  let inbox = Inbox()
  worker.build(id, source: source, reply: inbox.receive)
  return try inbox.wait()
}
func object(_ p: KernelPayload) throws -> [String: Any] {
  guard let value = try JSONSerialization.jsonObject(with: p.manifest) as? [String: Any] else {
    throw Failure(description: "manifest is not an object")
  }
  return value
}
func failed(_ p: KernelPayload, code: String) throws -> [String: Any] {
  let value = try object(p)
  let errors = value["errors"] as? [[String: Any]] ?? []
  try check(errors.contains { $0["code"] as? String == code }, "missing error \(code): \(value)")
  try check(p.vertices.isEmpty && p.indices.isEmpty && p.faces.isEmpty, "partial failure buffers")
  return errors[0]
}
func integer(_ value: Any?) -> Int { (value as? NSNumber)?.intValue ?? -999 }
func cacheHits(_ p: KernelPayload) throws -> Int {
  integer((try object(p)["cache"] as? [String: Any])?["hits"])
}
struct Vec {
  var x: Double, y: Double, z: Double
  static func - (a: Vec, b: Vec) -> Vec { Vec(x: a.x - b.x, y: a.y - b.y, z: a.z - b.z) }
  static func + (a: Vec, b: Vec) -> Vec { Vec(x: a.x + b.x, y: a.y + b.y, z: a.z + b.z) }
  func dot(_ b: Vec) -> Double { x * b.x + y * b.y + z * b.z }
  func cross(_ b: Vec) -> Vec {
    Vec(x: y * b.z - z * b.y, y: z * b.x - x * b.z, z: x * b.y - y * b.x)
  }
}
func scalar<T>(_ data: Data, _ offset: Int, _: T.Type) -> T {
  data.withUnsafeBytes { $0.loadUnaligned(fromByteOffset: offset, as: T.self) }
}
func vector(_ data: Data, _ index: Int, normal: Bool = false) -> Vec {
  let offset = index * 48 + (normal ? 24 : 0)
  return Vec(
    x: scalar(data, offset, Double.self), y: scalar(data, offset + 8, Double.self),
    z: scalar(data, offset + 16, Double.self))
}
func geometry(_ p: KernelPayload, volume: Double? = nil, area: Double? = nil) throws {
  let value = try object(p)
  try check((value["errors"] as? [Any])?.isEmpty == true, "geometry has errors: \(value)")
  guard let bodies = value["bodies"] as? [[String: Any]], bodies.count == 1,
    let exactV = bodies[0]["volume"] as? Double, let exactA = bodies[0]["area"] as? Double
  else {
    throw Failure(description: "missing exact body")
  }
  if let volume { try check(abs(exactV - volume) <= 1e-7, "exact volume") }
  if let area { try check(abs(exactA - area) <= 1e-7, "exact area") }
  let nv = p.vertices.count / 48
  let ni = p.indices.count / 4
  let nf = p.faces.count / 12
  try check(nv > 0 && p.vertices.count % 48 == 0, "vertex layout")
  try check(ni > 0 && p.indices.count % 12 == 0, "triangle layout")
  try check(nf > 0 && p.faces.count % 12 == 0, "face layout")
  try check(nv <= 3_000_000 && ni <= 3_000_000, "returned-data budget")
  try check((value["face_names"] as? [Any])?.count == nf, "face names/ranges")
  var next = 0
  for i in 0..<nf {
    try check(scalar(p.faces, i * 12, UInt32.self) == 0, "body association")
    try check(Int(scalar(p.faces, i * 12 + 4, UInt32.self)) == next, "range offset")
    let count = Int(scalar(p.faces, i * 12 + 8, UInt32.self))
    try check(count > 0 && count % 3 == 0, "range size")
    next += count
  }
  try check(next == ni, "face coverage")
  for i in 0..<nv {
    let position = vector(p.vertices, i)
    let n = vector(p.vertices, i, normal: true)
    try check(
      [position.x, position.y, position.z, n.x, n.y, n.z].allSatisfy(\.isFinite), "nonfinite mesh")
    try check(abs(n.dot(n).squareRoot() - 1) <= 1e-6, "unit surface normal")
  }
  var meshV = 0.0
  var meshA = 0.0
  let origin = vector(p.vertices, 0)
  for i in stride(from: 0, to: ni, by: 3) {
    let a = Int(scalar(p.indices, i * 4, UInt32.self))
    let b = Int(scalar(p.indices, (i + 1) * 4, UInt32.self))
    let c = Int(scalar(p.indices, (i + 2) * 4, UInt32.self))
    try check(a < nv && b < nv && c < nv, "index bounds")
    let pa = vector(p.vertices, a)
    let pb = vector(p.vertices, b)
    let pc = vector(p.vertices, c)
    let cross = (pb - pa).cross(pc - pa)
    let normal =
      vector(p.vertices, a, normal: true) + vector(p.vertices, b, normal: true)
      + vector(p.vertices, c, normal: true)
    try check(cross.dot(normal) > 0, "outward winding")
    meshA += cross.dot(cross).squareRoot() / 2
    meshV += (pa - origin).dot((pb - origin).cross(pc - origin)) / 6
  }
  try check(abs(meshV - exactV) <= exactV * 0.01, "mesh volume differs by over 1%")
  try check(abs(meshA - exactA) <= exactA * 0.01, "mesh area differs by over 1%")
}

func limits() throws {
  let probe = Probe()
  let worker = KernelWorker(makeEngine: { FakeEngine(probe.next()) })
  _ = try failed(request(worker, id: ""), code: "schema")
  _ = try failed(request(worker, id: String(repeating: "x", count: 1000)), code: "schema")
  _ = try failed(request(worker, source: Data(count: 16 * 1024 * 1024 + 1)), code: "bounds")
  try check(probe.count.withLock { $0 } == 0, "invalid input created a session")
}
func admission() throws {
  let probe = Probe()
  let inbox = Inbox()
  let worker = KernelWorker(deadline: 30, makeEngine: { FakeEngine(blocked: probe) })
  worker.build(document, source: plate(), reply: inbox.receive)
  defer { probe.release.signal() }
  try check(probe.entered.wait(timeout: .now() + 5) == .success, "worker never entered")
  _ = try failed(request(worker), code: "busy")
  probe.release.signal()
  _ = try inbox.wait()
  _ = try request(worker)  // completion must release admission before invoking its reply
}
func lru() throws {
  let probe = Probe()
  let worker = KernelWorker(makeEngine: { FakeEngine(probe.next()) })
  let ids = (0..<5).map { _ in UUID().uuidString }
  for i in 0..<4 {
    try check(integer(try object(request(worker, id: ids[i]))["tag"]) == i + 1, "cold tag")
  }
  try check(integer(try object(request(worker, id: ids[0]))["tag"]) == 1, "LRU touch lost session")
  try check(integer(try object(request(worker, id: ids[4]))["tag"]) == 5, "new session")
  try check(integer(try object(request(worker, id: ids[0]))["tag"]) == 1, "retained MRU")
  try check(integer(try object(request(worker, id: ids[1]))["tag"]) == 6, "LRU was not evicted")
  worker.closeDocument(ids[0])
  try check(integer(try object(request(worker, id: ids[0]))["tag"]) == 7, "closed cache reused")
}
func nativeCache() throws {
  let worker = KernelWorker()
  let first = try request(worker)
  try geometry(first, volume: 6000, area: 2200)
  try check(try cacheHits(first) == 0, "cold native cache")
  try check(try cacheHits(request(worker)) == 2, "warm native cache")
  try check(try cacheHits(request(worker, id: UUID().uuidString)) == 0, "document cache leaked")
  try geometry(request(worker, source: plate(15)), volume: 3000, area: 1300)
  _ = try failed(request(worker, source: plate(0)), code: "kernel")
  try check(try cacheHits(request(worker)) == 2, "cache recovery")
  worker.closeDocument(document.lowercased())
  try check(try cacheHits(request(worker)) == 0, "closed native cache survived")
  try geometry(first, volume: 6000, area: 2200)  // copied buffers survive closure and later builds
}
func residency() throws {
  var value = try JSONSerialization.jsonObject(with: plate()) as! [String: Any]
  var features = value["features"] as! [[String: Any]]
  for i in 2..<30 {
    features.append([
      "id": "move_\(i)", "type": "transform",
      "inputs": ["bodies": ["plate"], "translation": [1, 0, 0]],
    ])
  }
  value["features"] = features
  let source = try JSONSerialization.data(withJSONObject: value)
  let worker = KernelWorker()
  let ids = (0..<5).map { _ in UUID().uuidString }
  for id in ids.prefix(4) {
    let payload = try request(worker, id: id, source: source)
    try geometry(payload, volume: 6000, area: 2200)
    try check(try cacheHits(payload) == 0, "new thirty-feature document reused cache")
  }
  for id in ids.prefix(4) {
    let payload = try request(worker, id: id, source: source)
    try check(try cacheHits(payload) == 30, "four resident document caches did not stay warm")
    let usage = try object(payload)["cache"] as! [String: Any]
    try check(integer(usage["entries"]) <= 96, "entry budget")
    try check(integer(usage["estimated_bytes"]) <= 64 * 1024 * 1024, "estimated-byte budget")
  }
  _ = try request(worker, id: ids[4], source: source)
  try check(try cacheHits(request(worker, id: ids[0], source: source)) == 0, "native LRU eviction")
}
func factoryFailure() throws {
  let probe = Probe()
  let worker = KernelWorker(makeEngine: { probe.next() == 1 ? nil : FakeEngine() })
  _ = try failed(request(worker), code: "kernel_unavailable")
  let p = try request(worker)
  try check(integer(try object(p)["tag"]) == 0, "failed creation poisoned worker")
}
func watchdog(factory: Bool) throws {
  let probe = Probe()
  let inbox = Inbox()
  let worker = KernelWorker(
    deadline: 0.2,
    makeEngine: {
      if factory {
        probe.entered.signal()
        probe.release.wait()
      }
      return FakeEngine(blocked: factory ? nil : probe)
    },
    terminate: { status in
      probe.status.withLock { $0 = status }
      probe.terminated.signal()
    })
  worker.build(document, source: plate(), reply: inbox.receive)
  defer { probe.release.signal() }
  try check(probe.entered.wait(timeout: .now() + 5) == .success, "blocking engine not entered")
  let error = try failed(inbox.wait(), code: "timeout")
  try check(integer(error["feature_index"]) == (factory ? -999 : 7), "wrong timeout feature")
  try check(probe.terminated.wait(timeout: .now() + 5) == .success, "watchdog did not terminate")
  try check(probe.status.withLock { $0 } == 124, "watchdog exit status")
  _ = try failed(request(worker), code: "kernel_unavailable")
  probe.release.signal()
  kernelWorkQueue.sync {}
  try check(inbox.messages.withLock { $0.count } == 1, "late completion replied twice")
  _ = try failed(request(worker), code: "kernel_unavailable")
}
func races() throws {
  for _ in 0..<1000 {
    let probe = Probe()
    let gate = PendingReply { _, _, _, _, _ in _ = probe.next() }
    _ = gate.attach(FakeEngine())
    DispatchQueue.concurrentPerform(iterations: 2) { i in
      if let (reply, _) = gate.claim(timeout: i == 1) {
        KernelPayload.failure("timeout", "race").deliver(reply)
      }
    }
    try check(probe.count.withLock { $0 } == 1, "completion/deadline race replied twice")
  }
}
func references(_ directory: String) throws {
  let worker = KernelWorker()
  let urls = try FileManager.default.contentsOfDirectory(
    at: URL(fileURLWithPath: directory),
    includingPropertiesForKeys: nil
  )
  .filter { $0.pathExtension == "nightfall" }.sorted { $0.path < $1.path }
  try check(urls.count == 20, "reference catalogue changed")
  for url in urls {
    let p = try request(worker, id: UUID().uuidString, source: Data(contentsOf: url))
    do { try geometry(p) } catch {
      throw Failure(description: "\(url.lastPathComponent): \(error)")
    }
  }
}
func xpcRoundtrip() throws {
  let delegate = KernelListener()
  let listener = NSXPCListener.anonymous()
  listener.delegate = delegate
  listener.resume()
  defer { listener.invalidate() }
  let connection = NSXPCConnection(listenerEndpoint: listener.endpoint)
  connection.remoteObjectInterface = NSXPCInterface(with: KernelServiceProtocol.self)
  connection.resume()
  defer { connection.invalidate() }
  let errors = Probe()
  guard
    let proxy = connection.remoteObjectProxyWithErrorHandler({ _ in _ = errors.next() })
      as? KernelServiceProtocol
  else { throw Failure(description: "missing XPC proxy") }
  func send(_ source: Data) throws -> KernelPayload {
    let inbox = Inbox()
    proxy.build(document, source: source, reply: inbox.receive)
    let payload = try inbox.wait()
    try check(
      inbox.processIDs.withLock { $0 } == [ProcessInfo.processInfo.processIdentifier],
      "XPC PID scalar did not round trip")
    return payload
  }
  let first = try send(plate())
  try geometry(first, volume: 6000, area: 2200)
  try check(try cacheHits(send(plate())) == 2, "XPC warm rebuild")
  proxy.closeDocument(document)
  try check(try cacheHits(send(plate())) == 0, "XPC document closure")
  _ = try failed(send(Data("{}".utf8)), code: "schema")
  try geometry(first, volume: 6000, area: 2200)
  try check(errors.count.withLock { $0 } == 0, "XPC transport failed")
  withExtendedLifetime(delegate) {}
}
func terminateChild(factory: Bool) -> Never {
  let probe = Probe()
  let worker = KernelWorker(
    deadline: 0.2,
    makeEngine: {
      if factory { probe.release.wait() }
      return FakeEngine(blocked: probe)
    })
  worker.build(document, source: plate()) { manifest, vertices, indices, faces, _ in
    if !vertices.isEmpty || !indices.isEmpty || !faces.isEmpty { _exit(122) }
    FileHandle.standardOutput.write(manifest + Data([10]))
  }
  dispatchMain()
}
func termination() throws {
  for mode in ["timeout-child", "factory-timeout-child"] {
    let process = Process()
    let pipe = Pipe()
    process.executableURL = URL(fileURLWithPath: CommandLine.arguments[0])
    process.arguments = [mode]
    process.standardOutput = pipe
    let exited = DispatchSemaphore(value: 0)
    process.terminationHandler = { _ in exited.signal() }
    try process.run()
    if exited.wait(timeout: .now() + 5) != .success {
      process.terminate()
      throw Failure(description: "watchdog child did not exit")
    }
    try check(process.terminationReason == .exit && process.terminationStatus == 124, "child exit")
    let manifest = pipe.fileHandleForReading.readDataToEndOfFile()
    let error = try failed(KernelPayload(manifest: manifest), code: "timeout")
    try check(
      integer(error["feature_index"]) == (mode == "timeout-child" ? 7 : -999),
      "child timeout feature")
  }
}
@main struct WorkerTests {
  static func main() {
    let mode = CommandLine.arguments.dropFirst().first ?? ""
    if mode == "timeout-child" { terminateChild(factory: false) }
    if mode == "factory-timeout-child" { terminateChild(factory: true) }
    do {
      switch mode {
      case "limits": try limits()
      case "admission": try admission()
      case "lru": try lru()
      case "cache": try nativeCache()
      case "residency": try residency()
      case "factory": try factoryFailure()
      case "watchdog": try watchdog(factory: false)
      case "factory-deadline": try watchdog(factory: true)
      case "races": try races()
      case "references": try references(CommandLine.arguments[2])
      case "xpc": try xpcRoundtrip()
      case "termination": try termination()
      default: throw Failure(description: "unknown test \(mode)")
      }
      print("\(mode): passed")
    } catch {
      FileHandle.standardError.write(Data("\(mode): \(error)\n".utf8))
      exit(1)
    }
  }
}
