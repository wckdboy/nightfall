// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
import Foundation

@MainActor func filletIntegration(_ directory: String) async throws {
  let url = URL(fileURLWithPath: directory).appendingPathComponent("rounded_plate.nightfall")
  let source = try Data(contentsOf: url)
  let model = PreviewDocument()
  defer { model.shutdown() }
  func measures(_ radius: Double, _ thickness: Double = 8) throws {
    let crossSection = 2400 - 36 * Double.pi - (4 - Double.pi) * radius * radius
    let perimeter = 200 - 8 * radius + 2 * Double.pi * radius + 24 * Double.pi
    try check(
      model.errors.isEmpty && !model.stale && model.mesh != nil && model.bodies.count == 1,
      "fillet result missing or stale: \(model.errors)")
    try check(
      abs(model.bodies[0].volume - crossSection * thickness) < 1e-5
        && abs(model.bodies[0].area - (2 * crossSection + perimeter * thickness)) < 1e-5,
      "fillet exact measures do not match independent rounded-plate oracle")
  }
  model.load(url)
  try await waitForDocument(model)
  try measures(6)
  model.drafts["corner_radius"] = "3 mm"
  model.rebuild()
  try await waitForDocument(model)
  try measures(3)
  model.undo()
  try await waitForDocument(model)
  try measures(6)
  model.setSuppressed("round_corners", to: true)
  try await waitForDocument(model)
  try measures(0)
  model.undo()
  try await waitForDocument(model)
  try measures(6)
  model.endHistory(at: "plate")
  try await waitForDocument(model)
  try measures(0)
  model.endHistory(at: nil)
  try await waitForDocument(model)
  try measures(6)
  let valid = model.mesh?.id
  model.drafts["corner_radius"] = "100 mm"
  model.rebuild()
  try await waitForDocument(model)
  try check(
    !model.errors.isEmpty && model.stale && model.mesh?.id == valid,
    "failed fillet discarded the marked last-valid display")
  model.undo()
  try await waitForDocument(model)
  try measures(6)
  try check(try Data(contentsOf: url) == source, "fillet edits overwrote the source example")
  print("fillets: actual XPC edit, undo, suppression, rollback and failure recovery passed")
}
