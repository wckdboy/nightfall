// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
import Foundation

@MainActor func historyIntegration(_ directory: String) async throws {
  let url = URL(fileURLWithPath: directory).appendingPathComponent("mounting_plate.nightfall")
  let source = try Data(contentsOf: url)
  let program = try PartProgram(source)
  let disabled = try program.suppressing("plate", true)
  try check(
    disabled.features[1].suppressed && !program.features[1].suppressed
      && program.source == source, "suppression mutated the original program")
  let rolled = try program.endingHistory(at: "outline")
  try check(
    rolled.historyEnd == "outline" && !rolled.features[0].rolledBack
      && rolled.features[1].rolledBack, "rollback marker is not inclusive")
  let restored = try rolled.endingHistory(at: nil)
  let object = try JSONSerialization.jsonObject(with: restored.source) as? [String: Any]
  try check(object?["history_end"] is NSNull, "restore removed the required history field")
  for operation in [
    { try program.suppressing("missing", true) },
    { try program.endingHistory(at: "missing") },
  ] {
    do {
      _ = try operation()
      throw Failure(description: "unknown feature accepted")
    } catch is CocoaError {}
  }
  let model = PreviewDocument()
  defer { model.shutdown() }
  model.load(url)
  try await waitForDocument(model)
  let volume = model.bodies.first?.volume ?? 0
  try check(volume > 0 && model.errors.isEmpty, "initial history solid")
  model.setSuppressed("plate", to: true)
  try await waitForDocument(model)
  try check(
    model.errors.isEmpty && model.notice.isEmpty && model.mesh == nil && model.bodies.isEmpty
      && !model.stale && model.program?.features[1].suppressed == true,
    "suppressing the only solid kept geometry or reported an invalid mesh")
  model.undo()
  try await waitForDocument(model)
  try check(
    model.errors.isEmpty && abs((model.bodies.first?.volume ?? 0) - volume) < 1e-5,
    "undo suppression did not restore geometry")
  model.redo()
  try await waitForDocument(model)
  try check(model.errors.isEmpty && model.mesh == nil, "redo suppression")
  model.undo()
  try await waitForDocument(model)
  model.endHistory(at: "outline")
  try await waitForDocument(model)
  try check(
    model.errors.isEmpty && model.notice.isEmpty && model.mesh == nil && model.bodies.isEmpty
      && model.program?.historyEnd == "outline", "sketch-only rollback did not clear geometry")
  model.endHistory(at: "plate")
  try await waitForDocument(model)
  try check(
    model.errors.isEmpty && abs((model.bodies.first?.volume ?? 0) - volume) < 1e-5,
    "history endpoint excluded the selected extrusion")
  model.undo()
  try await waitForDocument(model)
  try check(model.mesh == nil && model.errors.isEmpty, "undo rollback endpoint")
  model.redo()
  try await waitForDocument(model)
  model.endHistory(at: nil)
  try await waitForDocument(model)
  let good = model.mesh?.id
  model.setSuppressed("outline", to: true)
  try await waitForDocument(model)
  try check(
    model.errors.contains { $0.code == "input_suppressed" } && model.stale
      && model.mesh?.id == good, "dependent suppression lost marked last-valid geometry")
  let reopened = try PartProgram(model.program!.source)
  try check(
    reopened.features[0].suppressed && reopened.historyEnd == nil,
    "authored history state did not round trip")
  model.undo()
  try await waitForDocument(model)
  model.setSuppressed("plate", to: true)
  model.setSuppressed("plate", to: false)
  try await waitForDocument(model)
  try check(
    model.errors.isEmpty && !model.stale && model.mesh != nil
      && abs((model.bodies.first?.volume ?? 0) - volume) < 1e-5,
    "older empty result replaced the latest solid")
  try check(try Data(contentsOf: url) == source, "history edits overwrote the source file")
  print("history: immutable suppression, inclusive rollback, empty state and undo/redo passed")
}
