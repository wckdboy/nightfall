// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
import AppKit

@MainActor func draftsIntegration(_ directory: String) async throws {
  let input = URL(fileURLWithPath: directory).appendingPathComponent("mounting_plate.nightfall")
  var root = try JSONSerialization.jsonObject(with: Data(contentsOf: input)) as! [String: Any]
  var params = root["params"] as! [[String: Any]]
  params.append(["name": "span", "value": "60 mm"])
  params.append(["name": "unused", "value": 2])
  root["params"] = params
  var features = root["features"] as! [[String: Any]]
  var inputs = features[0]["inputs"] as! [String: Any]
  var entities = inputs["entities"] as! [[String: Any]]
  entities[0]["size"] = ["span", "40 mm"]
  inputs["entities"] = entities
  features[0]["inputs"] = inputs
  root["features"] = features
  let source = try JSONSerialization.data(withJSONObject: root, options: [.sortedKeys])
  let program = try PartProgram(source)
  try check(try program.setting([:]).source == source, "empty batch reformatted source")
  let single = try program.setting(["thickness": "8 mm"])
  let singleRoot = try JSONSerialization.jsonObject(with: single.source) as! [String: Any]
  let singleParams = singleRoot["params"] as! [[String: Any]]
  try check(
    singleParams[2]["value"] is NSNumber
      && NSDictionary(dictionary: singleRoot["meta"] as! [String: Any]).isEqual(
        to: root["meta"] as! [String: Any]),
    "batch changed an untouched value or metadata")
  do {
    _ = try program.setting(["thickness": "8 mm", "missing": "1 mm"])
    throw Failure(description: "unknown batch parameter accepted")
  } catch is CocoaError {}
  try check(program.source == source, "failed batch mutated its input")
  let folder = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
  try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
  defer { try? FileManager.default.removeItem(at: folder) }
  let url = folder.appendingPathComponent("Input.nightfall")
  try source.write(to: url)
  let model = PreviewDocument()
  defer { model.shutdown() }
  model.load(url)
  try await waitForDocument(model)
  try check(model.errors.isEmpty && model.bodies.count == 1, "initial two-parameter part")
  let originalVolume = model.bodies[0].volume
  try check(!model.hasUnsavedChanges, "loaded source is dirty")
  model.drafts["span"] = "80 mm"
  model.drafts["thickness"] = "span / 10"
  try check(
    model.hasPendingParameters && model.hasUnsavedChanges && model.program?.source == source,
    "pending text was not tracked separately from the applied program")
  model.rebuild()
  try await waitForDocument(model)
  let expected = (80 * 40 - 4 * Double.pi * 3 * 3) * 8
  try check(
    model.errors.isEmpty && abs((model.bodies.first?.volume ?? 0) - expected) < 1e-5,
    "Rebuild discarded visible parameter edits")
  try check(model.past.count == 1, "parameter batch did not create exactly one undo entry")
  try check(!model.hasPendingParameters && model.hasUnsavedChanges, "applied batch dirty state")
  model.undo()
  try await waitForDocument(model)
  try check(
    model.program?.source == source && !model.hasUnsavedChanges
      && abs((model.bodies.first?.volume ?? 0) - originalVolume) < 1e-5,
    "one-step batch undo did not restore source and geometry")
  model.redo()
  try await waitForDocument(model)
  try check(
    abs((model.bodies.first?.volume ?? 0) - expected) < 1e-5 && model.hasUnsavedChanges,
    "batch redo did not restore dependent expressions")
  let applied = model.program!.source
  model.rebuild()
  try await waitForDocument(model)
  try check(
    model.past.count == 1 && model.program?.source == applied, "no-op rebuild added history")

  let saved = folder.appendingPathComponent("Saved.nightfall")
  model.drafts["thickness"] = "10 mm"
  try check(model.saveCopy(to: saved), "save without Return failed")
  try await waitForDocument(model)
  let savedSource = try Data(contentsOf: saved)
  try check(
    savedSource == model.program?.source
      && (try PartProgram(savedSource)).parameters[0].value == "10 mm"
      && !model.hasUnsavedChanges && !model.hasPendingParameters
      && abs((model.bodies.first?.volume ?? 0) - expected * 1.25) < 1e-5,
    "save omitted pending text or advanced the wrong baseline")
  model.undo()
  try await waitForDocument(model)
  try check(model.hasUnsavedChanges && model.program?.source == applied, "undo saved edit is clean")
  model.redo()
  try await waitForDocument(model)
  try check(!model.hasUnsavedChanges && model.program?.source == savedSource, "redo saved baseline")

  let historyCount = model.past.count
  let missing = folder.appendingPathComponent("missing/Failed.nightfall")
  model.drafts["span"] = "90 mm"
  try check(!model.saveCopy(to: missing), "save to missing parent succeeded")
  try check(
    model.program?.source == savedSource && model.past.count == historyCount
      && model.drafts["span"] == "90 mm" && model.hasPendingParameters
      && model.hasUnsavedChanges && !model.notice.isEmpty
      && (try Data(contentsOf: saved)) == savedSource,
    "failed write lost drafts, changed saved source or advanced history")
  model.redo()
  try check(model.drafts["span"] == "90 mm", "redo discarded pending text")
  model.undo()
  try await waitForDocument(model)
  try check(
    model.program?.source == savedSource && !model.hasUnsavedChanges,
    "undo pending text reverted a previously applied edit")
  model.redo()
  try await waitForDocument(model)
  try check(model.drafts["span"] == "90 mm" && model.hasUnsavedChanges, "pending text redo lost")
  model.undo()
  try await waitForDocument(model)

  model.drafts["span"] = "95 mm"
  model.drafts["thickness"] = "6 mm"
  model.setSuppressed("plate", to: true)
  try await waitForDocument(model)
  try check(
    model.errors.isEmpty && model.mesh == nil && model.bodies.isEmpty
      && model.drafts["span"] == "95 mm" && model.drafts["thickness"] == "6 mm"
      && model.past.count == historyCount + 1, "history action discarded drafts or split undo")
  model.undo()
  try await waitForDocument(model)
  try check(model.program?.source == savedSource, "history and parameters did not undo together")
  model.drafts["thickness"] = "12 mm"
  model.endHistory(at: "outline")
  try await waitForDocument(model)
  try check(
    model.mesh == nil && model.errors.isEmpty && model.drafts["thickness"] == "12 mm",
    "rollback discarded pending text")
  model.undo()
  try await waitForDocument(model)
  try check(model.program?.source == savedSource, "rollback batch undo")

  model.drafts["missing"] = "1 mm"
  model.drafts["thickness"] = "7 mm"
  model.rebuild()
  try check(
    model.program?.source == savedSource && model.past.count == historyCount
      && model.drafts["thickness"] == "7 mm" && model.hasPendingParameters,
    "failed batch partially applied or discarded text")
  let rejected = folder.appendingPathComponent("Rejected.nightfall")
  try check(
    !model.saveCopy(to: rejected) && !FileManager.default.fileExists(atPath: rejected.path),
    "invalid batch wrote a partial document")
  model.drafts.removeValue(forKey: "missing")
  model.drafts["thickness"] = "0 mm"
  let invalid = folder.appendingPathComponent("Invalid.nightfall")
  try check(model.saveCopy(to: invalid), "authored invalid geometry could not be saved")
  try await waitForDocument(model)
  try check(
    !model.errors.isEmpty && model.stale && model.mesh != nil && !model.hasUnsavedChanges
      && (try PartProgram(Data(contentsOf: invalid))).parameters[0].value == "0 mm",
    "invalid geometry source was lost or last-valid result was unmarked")
  try check(try Data(contentsOf: url) == source, "draft editing overwrote its original input")
  var choices = 0
  func choose(_ choice: CloseChoice) -> CloseChoice {
    choices += 1
    return choice
  }
  try check(
    model.canClose(choose: { choose(.cancel) }, destination: { nil }), "clean close blocked")
  try check(choices == 0, "clean document prompted")
  model.drafts["thickness"] = "11 mm"
  let pending = model.drafts
  try check(
    !model.canClose(choose: { choose(.cancel) }, destination: { nil }), "cancel closed document")
  try check(model.drafts == pending && model.hasUnsavedChanges, "cancel lost edits")
  try check(
    model.canClose(choose: { choose(.discard) }, destination: { nil }), "discard blocked close")
  try check(model.drafts == pending && model.hasUnsavedChanges, "discard check mutated document")
  try check(
    !model.canClose(choose: { .save }, destination: { nil }), "cancelled save closed document")
  try check(
    !model.canClose(choose: { .save }, destination: { missing }), "failed save closed document")
  try check(model.drafts == pending && model.hasUnsavedChanges, "failed close save lost edits")
  let closing = folder.appendingPathComponent("Closing.nightfall")
  try check(
    model.canClose(choose: { .save }, destination: { closing }), "successful save blocked close")
  try check(
    !model.hasUnsavedChanges
      && (try PartProgram(Data(contentsOf: closing))).parameters[0].value == "11 mm",
    "close save omitted pending text")
  let lifecycle = DocumentLifecycle()
  lifecycle.document = model
  var approvals = 0
  lifecycle.approveClose = { _ in
    approvals += 1
    return false
  }
  try check(
    lifecycle.applicationShouldTerminate(NSApplication.shared) == .terminateCancel,
    "quit bypassed close approval")
  let window = NSWindow()
  try check(!lifecycle.windowShouldClose(window), "window close bypassed approval")
  lifecycle.approveClose = { _ in
    approvals += 1
    return true
  }
  try check(
    lifecycle.applicationShouldTerminate(NSApplication.shared) == .terminateNow,
    "approved quit blocked")
  try check(lifecycle.windowShouldClose(window) && approvals == 4, "approved window close blocked")
  print(
    "drafts: dependent batch volume \(expected) mm³; atomic save/failure, baseline, history and pending undo/redo passed"
  )
}
