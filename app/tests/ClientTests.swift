// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
import Foundation

@MainActor final class FakeTransport: KernelTransport {
  struct Packet: Sendable {
    let id: UUID, source: Data
    let reply: @Sendable (KernelResponse) -> Void
    let failure: @Sendable (String) -> Void
  }
  let event: @Sendable (String) -> Void
  var packets: [Packet] = []
  var closed: [UUID] = []
  var invalidations = 0
  init(_ event: @escaping @Sendable (String) -> Void) { self.event = event }
  func send(
    _ id: UUID, source: Data, reply: @escaping @Sendable (KernelResponse) -> Void,
    failure: @escaping @Sendable (String) -> Void
  ) {
    packets.append(Packet(id: id, source: source, reply: reply, failure: failure))
  }
  func close(_ id: UUID) { closed.append(id) }
  func invalidate() { invalidations += 1 }
}
@MainActor final class Factory {
  var transports: [FakeTransport] = []
  func make(_ event: @escaping @Sendable (String) -> Void) -> any KernelTransport {
    let transport = FakeTransport(event)
    transports.append(transport)
    return transport
  }
}
func success(_ marker: Int32 = 123) -> KernelResponse {
  KernelResponse(manifest: Data("{\"errors\":[]}".utf8), processID: marker)
}
func errorCode(_ response: KernelResponse) -> String? { response.diagnostics.first?.code }
@MainActor func until(_ condition: () -> Bool) async throws {
  let limit = ContinuousClock.now + .seconds(3)
  while !condition() {
    try check(ContinuousClock.now < limit, "client condition deadline")
    try await Task.sleep(for: .milliseconds(1))
  }
}
@MainActor func clientAdmission() async throws {
  let factory = Factory()
  let id = UUID()
  let source = plate()
  let client = KernelClient(makeTransport: factory.make)
  let first = Task { await client.build(id, source: source) }
  try await until { factory.transports.first?.packets.count == 1 }
  let transport = factory.transports[0]
  let busy = await client.build(id, source: source)
  try check(errorCode(busy) == "busy" && transport.packets.count == 1, "single admission")
  let packet = transport.packets[0]
  try check(packet.id == id && packet.source == source, "complete IR/UUID forwarding")
  packet.reply(success())
  let completed = await first.value
  try check(completed.processID == 123, "successful completion")
  client.close(id)
  try check(transport.closed == [id], "explicit document closure")
  let again = Task { await client.build(id, source: source) }
  try await until { transport.packets.count == 2 }
  transport.packets[1].reply(success(456))
  let repeated = await again.value
  try check(repeated.processID == 456 && factory.transports.count == 1, "warm connection")
  client.shutdown()
  try check(transport.invalidations == 1, "shutdown invalidates")
}
@MainActor func clientDeadline() async throws {
  let factory = Factory()
  let id = UUID()
  let client = KernelClient(deadline: .milliseconds(100), makeTransport: factory.make)
  let result = await client.build(id, source: plate())
  try check(errorCode(result) == "kernel_unresponsive", "missing callback backstop")
  let stale = factory.transports[0]
  try check(stale.invalidations == 1, "deadline retires connection")
  let next = Task { await client.build(id, source: plate()) }
  try await until { factory.transports.count == 2 && factory.transports[1].packets.count == 1 }
  stale.packets[0].reply(success(999))
  stale.packets[0].failure("late error")
  stale.event("kernel_restarted")
  factory.transports[1].packets[0].reply(success(456))
  let received = await next.value
  try check(received.processID == 456, "stale event completed a newer request")
  client.shutdown()
}
@MainActor func clientCancellation() async throws {
  let factory = Factory()
  let id = UUID()
  let client = KernelClient(makeTransport: factory.make)
  let early = Task { await client.build(id, source: plate()) }
  early.cancel()
  let cancelledEarly = await early.value
  try check(
    errorCode(cancelledEarly) == "cancelled" && factory.transports.isEmpty,
    "pre-admission cancellation")
  let task = Task { await client.build(id, source: plate()) }
  try await until { factory.transports.first?.packets.count == 1 }
  task.cancel()
  let cancelled = await task.value
  try check(errorCode(cancelled) == "cancelled", "in-flight cancellation")
  try check(factory.transports[0].invalidations == 1, "cancel retires transport")
  factory.transports[0].packets[0].reply(success())
  let final = Task { await client.build(id, source: plate()) }
  try await until { factory.transports.count == 2 && factory.transports[1].packets.count == 1 }
  client.shutdown()
  let stopped = await final.value
  try check(
    errorCode(stopped) == "cancelled", "shutdown must settle pending continuation")
}
@MainActor func clientRecovery(successful: Bool = false) async throws {
  let factory = Factory()
  let id = UUID()
  let original = plate()
  let client = KernelClient(makeTransport: factory.make)
  let task = Task { await client.build(id, source: original) }
  try await until { factory.transports.first?.packets.count == 1 }
  let timeout = KernelResponse.failure("timeout", "feature hung", feature: 1)
  factory.transports[0].packets[0].reply(timeout)
  try await until { factory.transports.count == 2 && factory.transports[1].packets.count == 1 }
  let retry = factory.transports[1].packets[0]
  let edited = try JSONSerialization.jsonObject(with: retry.source) as! [String: Any]
  let features = edited["features"] as! [[String: Any]]
  try check(features[1]["suppressed"] as? Bool == true, "culprit was not suppressed")
  try check(features[0]["suppressed"] == nil, "unrelated feature changed")
  try check(retry.id == id && original == plate(), "authored document mutated")
  try check(factory.transports[0].packets[0].source == original, "original request changed")
  factory.transports[1].packets[0].reply(successful ? success(456) : timeout)
  let result = await task.value
  try check(
    (successful ? result.processID == 456 : errorCode(result) == "timeout")
      && result.recovery?.featureIndex == 1,
    "recovery diagnostic was lost")
  try check(
    factory.transports.count == 2 && factory.transports.allSatisfy { $0.packets.count == 1 },
    "timeout retried more than once")
  client.shutdown()
}
@MainActor func clientInvalidRecovery() async throws {
  for index in [nil, -1, 2, Int.max] as [Int?] {
    let factory = Factory()
    let client = KernelClient(makeTransport: factory.make)
    let task = Task { await client.build(UUID(), source: plate()) }
    try await until { factory.transports.first?.packets.count == 1 }
    factory.transports[0].packets[0].reply(.failure("timeout", "unknown culprit", feature: index))
    let result = await task.value
    try check(errorCode(result) == "timeout", "invalid culprit error")
    try check(factory.transports.count == 1, "invalid culprit triggered replay")
    client.shutdown()
  }
  let factory = Factory()
  let client = KernelClient(makeTransport: factory.make)
  let large = await client.build(UUID(), source: Data(count: 16 * 1024 * 1024 + 1))
  try check(errorCode(large) == "bounds" && factory.transports.isEmpty, "oversized input sent")
}
@MainActor func clientValidation() async throws {
  let cases = [
    KernelResponse(manifest: Data("{}".utf8)),
    KernelResponse(manifest: Data("{\"errors\":[]}".utf8), vertices: Data(count: 1)),
    KernelResponse(manifest: Data("{\"errors\":[]}".utf8), indices: Data(count: 4)),
    KernelResponse(manifest: Data("{\"errors\":[]}".utf8), faces: Data(count: 4)),
    KernelResponse(
      manifest: KernelResponse.failure("kernel", "failed").manifest, vertices: Data(count: 48)),
    KernelResponse(
      manifest: Data(
        "{\"errors\":[{\"code\":\"timeout\",\"message\":\"bad index\",\"pointer\":\"\",\"feature_index\":true}]}"
          .utf8)),
  ]
  for malformed in cases {
    let factory = Factory()
    let client = KernelClient(makeTransport: factory.make)
    let task = Task { await client.build(UUID(), source: plate()) }
    try await until { factory.transports.first?.packets.count == 1 }
    factory.transports[0].packets[0].reply(malformed)
    let result = await task.value
    try check(errorCode(result) == "protocol", "malformed reply accepted")
    try check(
      result.vertices.isEmpty && result.indices.isEmpty && result.faces.isEmpty,
      "partial error buffers")
    try check(factory.transports[0].invalidations == 1, "malformed peer was reused")
    client.shutdown()
  }
}
@MainActor func clientRaces() async throws {
  let factory = Factory()
  let tested = KernelClient(makeTransport: factory.make)
  for marker in 1...100 {
    let task = Task { await tested.build(UUID(), source: plate()) }
    try await until {
      factory.transports.count == marker && factory.transports.last?.packets.count == 1
    }
    let transport = factory.transports.last!
    let packet = transport.packets[0]
    DispatchQueue.concurrentPerform(iterations: 4) { i in
      if i % 2 == 0 {
        packet.reply(success(Int32(marker)))
      } else {
        packet.failure("concurrent disconnect")
      }
    }
    let result = await task.value
    try check(
      result.processID == marker || errorCode(result) == "kernel_unavailable", "race result")
    // Retire even successful transports so the next iteration has one unambiguous packet.
    tested.shutdown()
  }
  try check(factory.transports.count == 100, "race generations")
}
@MainActor func clientReplyOrdering() async throws {
  // A bounded large packet makes off-actor validation overlap the following disconnect.
  let padding = String(repeating: "x", count: 8 * 1024 * 1024)
  for timeout in [false, true] {
    for proxyFailure in [false, true] {
      let factory = Factory()
      let client = KernelClient(makeTransport: factory.make)
      defer { client.shutdown() }
      var completed = false
      let task = Task {
        let result = await client.build(UUID(), source: plate())
        completed = true
        return result
      }
      try await until { factory.transports.first?.packets.count == 1 }
      let old = factory.transports[0]
      let response =
        timeout
        ? KernelResponse.failure("timeout", padding, feature: 1)
        : KernelResponse(
          manifest: Data("{\"errors\":[],\"padding\":\"\(padding)\"}".utf8), processID: 123)
      old.packets[0].reply(response)
      if proxyFailure {
        old.packets[0].failure("service exited after replying")
      } else {
        old.event("kernel_restarted")
      }
      if timeout {
        try await until { factory.transports.count == 2 || completed }
        try check(
          factory.transports.count == 2, "disconnect discarded the received timeout culprit")
        let retry = factory.transports[1].packets[0]
        let document = try JSONSerialization.jsonObject(with: retry.source) as! [String: Any]
        let features = document["features"] as! [[String: Any]]
        try check(features[1]["suppressed"] as? Bool == true, "wrong recovery culprit")
        try check(features[0]["suppressed"] == nil, "unrelated recovery edit")
        retry.reply(success(456))
      }
      let result = await task.value
      try check(result.processID == (timeout ? 456 : 123), "received reply lost to disconnect")
      try check(result.recovery?.featureIndex == (timeout ? 1 : nil), "received culprit lost")
      try check(old.packets[0].source == plate(), "authored source changed during recovery")
      // Even a preserved success must retire a connection known to have failed.
      let next = Task { await client.build(UUID(), source: plate()) }
      let packets = timeout ? 2 : 1
      try await until {
        factory.transports.count == 2 && factory.transports[1].packets.count == packets
      }
      factory.transports[1].packets[packets - 1].reply(success(789))
      let subsequent = await next.value
      try check(subsequent.processID == 789, "disconnected transport was reused")
    }
  }
}
@main enum ClientTests {
  @MainActor static func main() async {
    do {
      switch CommandLine.arguments[1] {
      case "admission": try await clientAdmission()
      case "deadline": try await clientDeadline()
      case "cancellation": try await clientCancellation()
      case "recovery": try await clientRecovery()
      case "recovery-success": try await clientRecovery(successful: true)
      case "invalid-recovery": try await clientInvalidRecovery()
      case "validation": try await clientValidation()
      case "races": try await clientRaces()
      case "reply-ordering": try await clientReplyOrdering()
      default: throw Failure(description: "unknown client case")
      }
      print("client \(CommandLine.arguments[1]): passed")
    } catch {
      FileHandle.standardError.write(Data("client test: \(error)\n".utf8))
      exit(1)
    }
  }
}
