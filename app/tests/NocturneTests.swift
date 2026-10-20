// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
import Foundation
import MetalKit

@MainActor func nocturneIntegration(_ directory: String, output: String) async throws {
  guard let device = MTLCreateSystemDefaultDevice() else { exit(77) }
  let url = URL(fileURLWithPath: directory).appendingPathComponent("nocturne.nightfall")
  let original = try Data(contentsOf: url)
  let model = PreviewDocument()
  defer { model.shutdown() }
  let renderer = try MetalRenderer(device: device)
  renderer.camera.yaw = -0.35
  renderer.camera.pitch = -1.2
  renderer.camera.zoom = 0.8
  let folder = URL(fileURLWithPath: output)
  try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
  func verify() throws {
    try check(
      model.errors.isEmpty && !model.stale && model.notice.isEmpty && model.bodies.count == 21,
      "Nocturne native build failed: \(model.errors), \(model.notice)")
    try check((model.mesh?.triangleCount ?? 0) > 200_000, "Nocturne display missing geometry")
    try check(
      abs(model.bodies.first { $0.id == "pedestal" }!.volume - 196_404 * Double.pi) < 1e-5,
      "Nocturne independent pedestal mass")
  }
  let start = ContinuousClock.now
  model.load(url)
  try await waitForDocument(model)
  try verify()
  print("Nocturne cold native build: \((ContinuousClock.now - start) / .milliseconds(1)) ms")
  try renderer.setMesh(model.mesh!)
  let baseline = try renderPNG(renderer, to: folder.appendingPathComponent("nocturne.png"))
  let volumes = Dictionary(uniqueKeysWithValues: model.bodies.map { ($0.id, $0.volume) })
  print(
    "Nocturne: \(model.bodies.count) bodies, \(model.mesh!.triangleCount) triangles, "
      + "V=\(model.bodies.reduce(0) { $0 + $1.volume }) mm3, "
      + "A=\(model.bodies.reduce(0) { $0 + $1.area }) mm2")
  model.drafts["link_waist"] = "0.95"
  model.drafts["jaw_gap"] = "34 mm"
  model.rebuild()
  try await waitForDocument(model)
  try verify()
  try check(model.hasUnsavedChanges, "Nocturne edit was not authored")
  try renderer.setMesh(model.mesh!)
  let variant = try renderPNG(renderer, to: folder.appendingPathComponent("nocturne-variant.png"))
  try check(variant != baseline, "Nocturne parameter edit did not change the model")
  model.undo()
  try await waitForDocument(model)
  try verify()
  for body in model.bodies {
    try check(abs(body.volume - volumes[body.id]!) < 1e-5, "Nocturne undo mass changed")
  }
  try check(
    !model.hasUnsavedChanges && model.program?.source == original, "Nocturne clean undo source")
  try check(try Data(contentsOf: url) == original, "Nocturne example changed")
  print("Nocturne: real XPC/Metal build, batch parameter edit and clean Undo passed")
}
