// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
import Foundation
import MetalKit

@MainActor func scalabilityIntegration(_ directory: String, output: String) async throws {
  guard let device = MTLCreateSystemDefaultDevice() else { exit(77) }
  let url = URL(fileURLWithPath: directory).appendingPathComponent("asterion.nightfall")
  let original = try Data(contentsOf: url)
  let model = PreviewDocument()
  defer { model.shutdown() }
  model.load(url)
  try await waitForDocument(model)
  try check(model.errors.isEmpty && !model.stale && model.notice.isEmpty, "base showcase failed")
  let baseline = Dictionary(uniqueKeysWithValues: model.bodies.map { ($0.id, $0) })
  try check(baseline.count == 8 && model.mesh?.triangleCount == 27_172, "base showcase coverage")
  let renderer = try MetalRenderer(device: device)
  renderer.camera.preset(.top)
  renderer.camera.zoom = 0.45
  let folder = URL(fileURLWithPath: output)
  try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
  for (pass, variant) in [(8, 217_142), (4, 108_582), (8, 217_142)].enumerated() {
    let (count, triangles) = variant
    model.drafts["module_count"] = "\(count)"
    let start = ContinuousClock.now
    model.rebuild()
    try await waitForDocument(model)
    try check(
      model.errors.isEmpty && !model.stale && model.notice.isEmpty,
      "\(count)-module rebuild failed: \(model.errors), \(model.notice)")
    try check(model.bodies.count == 8, "large rebuild body coverage")
    for body in model.bodies {
      guard let base = baseline[body.id] else { throw Failure(description: "unknown body") }
      try check(
        abs(body.volume - base.volume * Double(count)) <= 1e-5, "pattern exact volume changed")
      try check(abs(body.area - base.area * Double(count)) <= 1e-5, "pattern exact area changed")
    }
    guard let mesh = model.mesh else { throw Failure(description: "large rebuild mesh missing") }
    try check(mesh.triangleCount == triangles && mesh.draws.count == 8, "large mesh coverage")
    try renderer.setMesh(mesh)
    _ = try renderPNG(renderer, to: folder.appendingPathComponent("asterion-eight-\(pass).png"))
    print("scalability \(count) modules: \((ContinuousClock.now - start) / .milliseconds(1)) ms")
  }
  model.isolateBody("fins")
  renderer.hiddenBodies = model.hiddenBodies
  // Subpixel fin widths occupy less than 1% of this long, fully framed model.
  _ = try renderPNG(
    renderer, to: folder.appendingPathComponent("asterion-eight-fins.png"), minimumCoverage: 0.001)
  model.showAllBodies()
  renderer.hiddenBodies = model.hiddenBodies
  for triangles in [108_582, 217_142, 27_172] {
    model.undo()
    try await waitForDocument(model)
    try check(
      model.errors.isEmpty && !model.stale && model.notice.isEmpty
        && model.mesh?.triangleCount == triangles, "large-model variant undo failed")
  }
  try check(
    model.errors.isEmpty && !model.stale && !model.hasUnsavedChanges
      && model.mesh?.triangleCount == 27_172, "large-model undo did not restore the clean source")
  try check(model.program?.source == original, "large-model undo changed source bytes")
  try check(try Data(contentsOf: url) == original, "large-model test changed the example")
  let nocturne = URL(fileURLWithPath: directory).appendingPathComponent("nocturne.nightfall")
  let start = ContinuousClock.now
  model.load(nocturne)
  try await waitForDocument(model)
  try check(
    model.errors.isEmpty && !model.stale && model.notice.isEmpty && model.bodies.count == 21
      && model.mesh?.triangleCount == 201_808 && !model.hasUnsavedChanges,
    "Nocturne switch after large-model undo failed: \(model.errors), \(model.notice)")
  try check(model.program?.source == Data(contentsOf: nocturne), "Nocturne switch changed source")
  try renderer.setMesh(model.mesh!)
  _ = try renderPNG(renderer, to: folder.appendingPathComponent("nocturne-after-asterion.png"))
  print("Nocturne after Asterion: \((ContinuousClock.now - start) / .milliseconds(1)) ms")
  print("scalability: three real XPC/client/Metal rebuilds, clean undo and document switch passed")
}
