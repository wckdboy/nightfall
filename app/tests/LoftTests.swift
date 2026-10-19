// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
import Foundation
import MetalKit

@MainActor func loftIntegration(_ directory: String, output: String) async throws {
  guard let device = MTLCreateSystemDefaultDevice() else { exit(77) }
  let url = URL(fileURLWithPath: directory).appendingPathComponent("lofted_link.nightfall")
  let original = try Data(contentsOf: url)
  let model = PreviewDocument()
  defer { model.shutdown() }
  let renderer = try MetalRenderer(device: device)
  let folder = URL(fileURLWithPath: output)
  try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
  func volume(_ height: Double, _ waist: Double, _ bore: Double = 8, _ flange: Double = 6) -> Double
  {
    let a = [32, 88 * waist - 120, 112 - 88 * waist]
    let b = [20, 48 * waist - 76, 72 - 48 * waist]
    var section = 0.0
    for i in 0..<3 {
      for j in 0..<3 { section += a[i] * b[j] / Double(i + j + 1) }
    }
    return Double.pi
      * (height * section + 1200 * flange - bore * bore * (height + flange) - 25 * flange)
  }
  func checkModel(_ expected: Double) throws {
    try check(
      model.errors.isEmpty && !model.stale && model.notice.isEmpty && model.bodies.count == 1,
      "loft client failed: \(model.errors), \(model.notice)")
    try check(abs(model.bodies[0].volume - expected) < 1e-5, "loft independent volume mismatch")
    try check(model.mesh != nil && model.bodies[0].area > 0, "loft display or exact area missing")
  }
  model.load(url)
  try await waitForDocument(model)
  try checkModel(volume(160, 1))
  // Independent positive-metric quadrature in the C++ fixture test, not a measured baseline.
  try check(abs(model.bodies[0].area - 35_752.49628321786) < 1e-5, "loft independent area mismatch")
  try check(!model.hasUnsavedChanges && model.program?.source == original, "loft initial source")
  try renderer.setMesh(model.mesh!)
  let baseline = try renderPNG(renderer, to: folder.appendingPathComponent("lofted-link.png"))
  model.drafts["height"] = "200 mm"
  model.drafts["waist_scale"] = "1.1"
  model.rebuild()
  try await waitForDocument(model)
  try checkModel(volume(200, 1.1))
  try renderer.setMesh(model.mesh!)
  let variant = try renderPNG(
    renderer, to: folder.appendingPathComponent("lofted-link-variant.png"))
  try check(variant != baseline && model.hasUnsavedChanges, "loft variant did not change display")
  model.undo()
  try await waitForDocument(model)
  try checkModel(volume(160, 1))
  try check(!model.hasUnsavedChanges && model.program?.source == original, "loft clean undo failed")
  model.setSuppressed("housing_loft", to: true)
  try await waitForDocument(model)
  try checkModel(Double.pi * (1200 - 64 - 25) * 6)
  model.undo()
  try await waitForDocument(model)
  try checkModel(volume(160, 1))
  let valid = model.mesh?.id
  model.drafts["waist_scale"] = "0.1"
  model.rebuild()
  try await waitForDocument(model)
  try check(
    !model.errors.isEmpty && model.stale && model.mesh?.id == valid,
    "invalid loft parameters discarded last-valid display")
  model.undo()
  try await waitForDocument(model)
  try checkModel(volume(160, 1))
  try check(
    !model.hasUnsavedChanges && model.program?.source == original, "loft failure undo failed")
  try check(try Data(contentsOf: url) == original, "native loft test modified the example")
  print(
    "lofts: actual XPC/Metal rendering, batch edit, undo, suppression and failure recovery passed")
}
