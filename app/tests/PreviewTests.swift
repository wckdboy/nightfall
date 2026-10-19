// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
import Foundation

@MainActor func waitForDocument(_ model: PreviewDocument) async throws {
  let deadline = ContinuousClock.now + .seconds(20)
  while model.busy {
    try check(ContinuousClock.now < deadline, "document rebuild deadline")
    try await Task.sleep(for: .milliseconds(2))
  }
}
@MainActor func previewIntegration(_ directory: String) async throws {
  let url = URL(fileURLWithPath: directory).appendingPathComponent("mounting_plate.nightfall")
  let original = try Data(contentsOf: url)
  let program = try PartProgram(original)
  let edited = try program.setting("thickness", to: "8 mm")
  try check(program.source == original && edited.source != original, "immutable program edit")
  try check(
    program.features.count == edited.features.count && program.parameters[0].value == "4 mm",
    "history or old parameter mutated")
  do {
    _ = try program.setting("missing", to: "1 mm")
    throw Failure(description: "unknown parameter accepted")
  } catch is CocoaError {}
  let model = PreviewDocument()
  defer { model.shutdown() }
  model.load(url)
  try await waitForDocument(model)
  try check(
    model.errors.isEmpty && model.mesh != nil && model.bodies.count == 1, "initial document build")
  let volume = model.bodies[0].volume
  model.drafts["thickness"] = "8 mm"
  model.rebuild()
  try await waitForDocument(model)
  try check(
    model.errors.isEmpty && abs(model.bodies[0].volume - 2 * volume) < 1e-5,
    "live parameter geometry")
  let good = model.mesh?.id
  model.drafts["thickness"] = "0 mm"
  model.rebuild()
  try await waitForDocument(model)
  try check(
    !model.errors.isEmpty && model.stale && model.mesh?.id == good,
    "failed edit lost last valid mesh")
  model.undo()
  try await waitForDocument(model)
  try check(
    model.errors.isEmpty && !model.stale && abs(model.bodies[0].volume - 2 * volume) < 1e-5,
    "undo failed edit")
  model.undo()
  try await waitForDocument(model)
  try check(abs(model.bodies[0].volume - volume) < 1e-5, "undo original thickness")
  model.redo()
  try await waitForDocument(model)
  try check(abs(model.bodies[0].volume - 2 * volume) < 1e-5, "redo thickness")
  model.drafts["thickness"] = "6 mm"
  model.rebuild()
  model.drafts["thickness"] = "10 mm"
  model.rebuild()
  try await waitForDocument(model)
  try check(
    model.errors.isEmpty && abs(model.bodies[0].volume - 2.5 * volume) < 1e-5,
    "latest edit was superseded")
  try check(try Data(contentsOf: url) == original, "opening/editing overwrote source file")
  let reopened = try PartProgram(model.program!.source)
  try check(reopened.parameters[0].value == "10 mm", "edited source does not round trip")
  print(
    "preview: parameter volume \(volume) → \(model.bodies[0].volume); error retention and undo/redo passed"
  )
}
