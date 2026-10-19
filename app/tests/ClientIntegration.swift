// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
import Darwin
import Foundation

func packet(_ response: KernelResponse) -> Reply {
  Reply(
    manifest: response.manifest, vertices: response.vertices, indices: response.indices,
    faces: response.faces, pid: response.processID)
}
@MainActor func clientIntegration() async throws {
  let client = KernelClient()
  let id = UUID()
  let source = plate()
  defer { client.shutdown() }
  let first = await client.build(id, source: source)
  try geometry(packet(first), volume: 6000, area: 2200)
  try check(first.processID > 0 && first.processID != getpid(), "client process isolation")
  let warm = await client.build(id, source: source)
  try check(try cacheHits(packet(warm)) == 2, "client warm cache")
  client.close(id)
  let closed = await client.build(id, source: source)
  try check(try cacheHits(packet(closed)) == 0, "client document closure")
  let bad = await client.build(id, source: Data("{}".utf8))
  try check(
    bad.diagnostics.first?.code == "schema" && bad.vertices.isEmpty, "structured kernel error")
  try check(kill(first.processID, SIGKILL) == 0, "client cannot terminate owned service")
  var rebuilt: KernelResponse?
  for attempt in 1...3 {
    let start = ContinuousClock.now
    let next = await client.build(id, source: source)
    let elapsed = ContinuousClock.now - start
    try check(elapsed < .seconds(9), "client seven-second backstop exceeded")
    if next.diagnostics.isEmpty {
      rebuilt = next
      break
    }
    try check(
      ["kernel_restarted", "kernel_unavailable", "kernel_unresponsive"].contains(
        next.diagnostics[0].code),
      "unexpected restart error")
    print("client replay attempt \(attempt): \(next.diagnostics[0].code) after \(elapsed)")
  }
  guard let rebuilt else {
    throw Failure(description: "client could not replay after service restart")
  }
  try check(rebuilt.processID != first.processID, "client replay kept old process")
  try geometry(packet(rebuilt), volume: 6000, area: 2200)
  try check(try cacheHits(packet(rebuilt)) == 0, "client replay was not cold")
  let repeated = await client.build(id, source: source)
  try check(try cacheHits(packet(repeated)) == 2, "client replay did not warm")
  try geometry(packet(first), volume: 6000, area: 2200)
  print("client complete IR replay: pid \(first.processID) → \(rebuilt.processID)")
}
