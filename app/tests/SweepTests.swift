// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
import Foundation
import MetalKit

@MainActor func sweepIntegration(_ directory: String, output: String) async throws {
  guard let device = MTLCreateSystemDefaultDevice() else { exit(77) }
  let url = URL(fileURLWithPath: directory).appendingPathComponent("swept_conduit.nightfall")
  let original = try Data(contentsOf: url)
  let model = PreviewDocument()
  defer { model.shutdown() }
  let renderer = try MetalRenderer(device: device)
  let folder = URL(fileURLWithPath: output)
  try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
  func arcLength(_ reach: Double, _ lift: Double) -> Double {
    let n = 8192
    var sum = 0.0
    for i in 0...n {
      let t = Double(i) / Double(n)
      let u = 1 - t
      let dx = reach * (4 * u * u + 2 * u * t + 4 * t * t) / 3
      let dy = 6 * lift * u * t
      sum += Double(i == 0 || i == n ? 1 : i % 2 == 0 ? 2 : 4) * hypot(dx, dy)
    }
    return sum / Double(3 * n)
  }
  func checkModel(_ reach: Double, _ lift: Double, hollow: Bool = true) throws {
    try check(
      model.errors.isEmpty && !model.stale && model.notice.isEmpty && model.bodies.count == 1,
      "sweep client failed: \(model.errors), \(model.notice)")
    let length = arcLength(reach, lift)
    let volume = Double.pi * (hollow ? 75 : 196) * length
    let area = Double.pi * (hollow ? 50 : 28) * length + Double.pi * (hollow ? 150 : 392)
    try check(abs(model.bodies[0].volume - volume) < 1e-3, "sweep independent volume mismatch")
    try check(abs(model.bodies[0].area - area) < 1e-3, "sweep independent area mismatch")
    try check(model.mesh != nil, "sweep display missing")
  }
  model.load(url)
  try await waitForDocument(model)
  try checkModel(180, 100)
  try check(!model.hasUnsavedChanges && model.program?.source == original, "sweep initial source")
  try renderer.setMesh(model.mesh!)
  let baseline = try renderPNG(renderer, to: folder.appendingPathComponent("swept-conduit.png"))
  model.drafts["reach"] = "220 mm"
  model.drafts["lift"] = "120 mm"
  model.rebuild()
  try await waitForDocument(model)
  try checkModel(220, 120)
  try renderer.setMesh(model.mesh!)
  let variant = try renderPNG(
    renderer, to: folder.appendingPathComponent("swept-conduit-variant.png"))
  try check(variant != baseline && model.hasUnsavedChanges, "sweep variant display unchanged")
  model.undo()
  try await waitForDocument(model)
  try checkModel(180, 100)
  try check(
    !model.hasUnsavedChanges && model.program?.source == original, "sweep clean undo failed")
  model.setSuppressed("bore_sweep", to: true)
  try await waitForDocument(model)
  try checkModel(180, 100, hollow: false)
  model.undo()
  try await waitForDocument(model)
  try checkModel(180, 100)
  let valid = model.mesh?.id
  model.drafts["radius"] = "1 mm"
  model.rebuild()
  try await waitForDocument(model)
  try check(
    !model.errors.isEmpty && model.stale && model.mesh?.id == valid, "sweep last-valid loss")
  model.undo()
  try await waitForDocument(model)
  try checkModel(180, 100)
  try check(
    !model.hasUnsavedChanges && model.program?.source == original, "sweep failure undo failed")
  try check(try Data(contentsOf: url) == original, "native sweep test modified the example")
  print(
    "sweeps: actual XPC/Metal, spline edits, exact mass, undo, suppression and failure recovery passed"
  )
}
