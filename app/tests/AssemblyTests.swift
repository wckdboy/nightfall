// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// RFC 0013 assemblies in the app: the real document model and kernel service build the
// Strandbeest's 32 placed bodies, a crank edit moves the walker in the viewport, undo brings
// it back, and the six-joint arm poses from its joint parameters.
import Foundation
import MetalKit

@MainActor func assemblyIntegration(_ directory: String, output: String) async throws {
  guard let device = MTLCreateSystemDefaultDevice() else { exit(77) }
  let renderer = try MetalRenderer(device: device)
  let folder = URL(fileURLWithPath: output)
  try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
  let model = PreviewDocument()
  defer { model.shutdown() }
  func checkBuilt(_ count: Int, _ what: String) throws {
    try check(
      model.errors.isEmpty && !model.stale && model.bodies.count == count && model.mesh != nil,
      "\(what): \(model.errors), \(model.bodies.count) bodies")
  }
  func render(_ name: String) throws -> [UInt8] {
    try renderer.setMesh(model.mesh!)
    return try renderPNG(renderer, to: folder.appendingPathComponent(name))
  }

  model.load(URL(fileURLWithPath: directory).appendingPathComponent("strandbeest.nightfall"))
  try await waitForDocument(model)
  try checkBuilt(32, "Strandbeest")
  // Each placed link keeps its component's exact volume: a 12 mm stadium, 4 mm thick.
  let link = (50 * 12 + 18 * Double.pi) * 4
  let upper = model.bodies.first { $0.id == "j_fl/plate" }
  try check(abs((upper?.volume ?? 0) - link) < 1e-6, "placed link volume")
  let standing = try render("strandbeest-30.png")
  model.drafts["crank"] = "120 deg"
  model.rebuild()
  try await waitForDocument(model)
  try checkBuilt(32, "Strandbeest at 120 deg")
  let stepped = try render("strandbeest-120.png")
  try check(standing != stepped, "a crank edit did not move the walker")
  model.undo()
  try await waitForDocument(model)
  try checkBuilt(32, "Strandbeest after undo")
  try check(try render("strandbeest-undo.png") == standing, "undo did not restore the pose")

  model.load(URL(fileURLWithPath: directory).appendingPathComponent("arm.nightfall"))
  try await waitForDocument(model)
  try checkBuilt(7, "arm")
  let home = try render("arm-home.png")
  for (joint, value) in [
    ("q1", "30 deg"), ("q2", "40 deg"), ("q3", "-60 deg"), ("q4", "20 deg"), ("q5", "50 deg"),
  ] {
    model.drafts[joint] = value
  }
  model.rebuild()
  try await waitForDocument(model)
  try checkBuilt(7, "posed arm")
  try check(try render("arm-posed.png") != home, "joint edits did not pose the arm")
}
