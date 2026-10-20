// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
import Foundation
import MetalKit

/// Actual document lifecycle: discard old service caches, open new documents, edit and undo.
/// Timings are evidence, not an extra latency threshold or a retry policy.
@MainActor func documentSequenceIntegration(_ directory: String, output: String) async throws {
  guard let device = MTLCreateSystemDefaultDevice() else { exit(77) }
  let root = URL(fileURLWithPath: directory)
  let model = PreviewDocument()
  defer { model.shutdown() }
  let renderer = try MetalRenderer(device: device)
  let folder = URL(fileURLWithPath: output)
  try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
  let conduit = root.appendingPathComponent("swept_conduit.nightfall")
  let loft = root.appendingPathComponent("lofted_link.nightfall")
  let conduitSource = try Data(contentsOf: conduit)
  let loftSource = try Data(contentsOf: loft)
  func emit(_ values: [String: Any]) throws {
    var line = try JSONSerialization.data(withJSONObject: values, options: [.sortedKeys])
    line.append(10)
    FileHandle.standardOutput.write(line)
  }
  func step(_ label: String, triangles: Int, action: () -> Void) async throws {
    try emit(["kind": "begin", "stage": label])
    let start = ContinuousClock.now
    action()
    try await waitForDocument(model)
    try emit([
      "kind": "end", "stage": label,
      "milliseconds": (ContinuousClock.now - start) / .milliseconds(1),
      "triangles": model.mesh?.triangleCount ?? 0,
      "errors": model.errors.map { "\($0.code): \($0.message)" }, "notice": model.notice,
    ])
    try check(
      model.errors.isEmpty && !model.stale && model.notice.isEmpty,
      "document sequence \(label) failed: \(model.errors), \(model.notice)")
    try check(model.mesh?.triangleCount == triangles, "sequence \(label) mesh coverage")
    try renderer.setMesh(model.mesh!)
  }
  try await step("asterion", triangles: 27_172) {
    model.load(root.appendingPathComponent("asterion.nightfall"))
  }
  for round in 0..<2 {
    try await step("conduit-\(round)", triangles: 23_494) { model.load(conduit) }
    try check(abs(model.bodies[0].volume - 50_016.05734226065) < 1e-3, "conduit mass")
    try await step("conduit-edit-\(round)", triangles: 26_622) {
      model.drafts["reach"] = "220 mm"
      model.drafts["lift"] = "120 mm"
      model.rebuild()
    }
    try await step("conduit-undo-\(round)", triangles: 23_494) { model.undo() }
    try check(
      !model.hasUnsavedChanges && model.program?.source == conduitSource, "conduit clean undo")
    try await step("loft-\(round)", triangles: 8934) { model.load(loft) }
    try check(abs(model.bodies[0].volume - 160_612.87721722698) < 1e-5, "loft mass")
    try await step("loft-edit-\(round)", triangles: 8886) {
      model.drafts["waist_scale"] = "1.1"
      model.rebuild()
    }
    try await step("loft-undo-\(round)", triangles: 8934) { model.undo() }
    try check(!model.hasUnsavedChanges && model.program?.source == loftSource, "loft clean undo")
  }
  try await step("conduit-final", triangles: 23_494) { model.load(conduit) }
  _ = try renderPNG(renderer, to: folder.appendingPathComponent("document-sequence.png"))
  try check(try Data(contentsOf: conduit) == conduitSource, "sequence changed conduit source")
  try check(try Data(contentsOf: loft) == loftSource, "sequence changed loft source")
}
