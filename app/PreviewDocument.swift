// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
import AppKit
import Observation
import UniformTypeIdentifiers

struct BodyMeasure: Decodable, Identifiable, Sendable {
  let id: String
  let volume: Double, area: Double
}
@MainActor @Observable final class PreviewDocument {
  var program: PartProgram?
  var mesh: DisplayMesh?
  var bodies: [BodyMeasure] = []
  var hiddenBodies: Set<String> = []
  var allBodiesHidden: Bool {
    !bodies.isEmpty && bodies.allSatisfy { hiddenBodies.contains($0.id) }
  }
  func setBodyVisible(_ id: String, to visible: Bool) {
    guard bodies.contains(where: { $0.id == id }) else { return }
    if visible { hiddenBodies.remove(id) } else { hiddenBodies.insert(id) }
  }
  func isolateBody(_ id: String) {
    guard bodies.contains(where: { $0.id == id }) else { return }
    hiddenBodies = Set(bodies.map(\.id).filter { $0 != id })
  }
  func showAllBodies() { hiddenBodies.removeAll() }
  var errors: [KernelDiagnostic] = []
  var drafts: [String: String] = [:]
  var notice = ""
  var busy = false
  var stale = false
  var pose = CADCamera.Preset.iso
  var fit = 0
  var past: [PartProgram] = []
  var future: [PartProgram] = []
  @ObservationIgnored private let client = KernelClient()
  private let sheet = DocumentSheet()
  @ObservationIgnored private var task: Task<Void, Never>?
  private var documentID = UUID()
  private var revision = UUID()
  private var savedSource: Data?
  private var pendingParameters: [String: String] {
    let values = Dictionary(
      uniqueKeysWithValues: (program?.parameters ?? []).map { ($0.id, $0.value) })
    return drafts.filter { values[$0.key] != $0.value }
  }
  var hasPendingParameters: Bool { !pendingParameters.isEmpty }
  var hasUnsavedChanges: Bool {
    hasPendingParameters || (program.map { $0.source != savedSource } ?? false)
  }
  var presentingSheet: Bool { sheet.active != nil }
  private var documentWindow: NSWindow? {
    NSApp.mainWindow
      ?? NSApp.windows.first {
        $0.canBecomeMain && $0.isVisible && !($0 is NSPanel)
      }
  }
  var examples: [URL] {
    let folder = Bundle.main.resourceURL?.appendingPathComponent("Examples")
    return
      ((try? FileManager.default.contentsOfDirectory(
        at: folder ?? URL(fileURLWithPath: "/"),
        includingPropertiesForKeys: nil)) ?? []).filter { $0.pathExtension == "nightfall" }.sorted {
        $0.path < $1.path
      }
  }
  func start() {
    if program == nil,
      let part = examples.first(where: { $0.lastPathComponent == "asterion.nightfall" })
        ?? examples.first(where: { $0.lastPathComponent == "mounting_plate.nightfall" })
    {
      load(part)
    }
  }
  func load(_ url: URL) {
    guard !presentingSheet else { return }
    if hasUnsavedChanges {
      guard let window = documentWindow else { return }
      let alert = NSAlert()
      alert.messageText = "Discard unsaved changes?"
      alert.informativeText = "Choose Keep editing to save a copy first."
      alert.addButton(withTitle: "Keep editing")
      alert.addButton(withTitle: "Discard")
      guard
        let id = sheet.begin(
          for: documentID,
          cancel: { [weak window] in
            window?.endSheet(alert.window, returnCode: .cancel)
          })
      else { return }
      NSApp.activate()
      window.makeKeyAndOrderFront(nil)
      alert.beginSheetModal(for: window) { [weak self, weak alert] response in
        alert?.window.orderOut(nil)
        guard let self, self.sheet.complete(id, for: self.documentID) else { return }
        if response == .alertSecondButtonReturn { self.read(url) }
      }
    } else {
      read(url)
    }
  }
  private func read(_ url: URL) {
    do {
      let part = try PartProgram(Data(contentsOf: url, options: .mappedIfSafe))
      client.close(documentID)
      documentID = UUID()
      savedSource = part.source
      bodies = []
      hiddenBodies = []
      past = []
      future = []
      mesh = nil
      fit += 1
      replace(part)
    } catch { notice = error.localizedDescription }
  }
  private func applyingDrafts() throws -> PartProgram {
    guard let program else { throw CocoaError(.fileReadCorruptFile) }
    return try program.setting(pendingParameters)
  }
  func setSuppressed(_ id: String, to value: Bool) {
    guard program != nil else { return }
    do { commit(try applyingDrafts().suppressing(id, value)) } catch {
      notice = error.localizedDescription
    }
  }
  func endHistory(at id: String?) {
    guard program != nil else { return }
    do { commit(try applyingDrafts().endingHistory(at: id)) } catch {
      notice = error.localizedDescription
    }
  }
  private func commit(_ next: PartProgram) {
    guard let program, next.source != program.source else { return }
    past.append(program)
    future = []
    replace(next)
  }
  func undo() {
    do {
      if hasPendingParameters { commit(try applyingDrafts()) }
    } catch {
      notice = error.localizedDescription
      return
    }
    if let previous = past.popLast(), let program {
      future.append(program)
      replace(previous)
    }
  }
  func redo() {
    guard !hasPendingParameters else { return }
    if let next = future.popLast(), let program {
      past.append(program)
      replace(next)
    }
  }
  func rebuild() {
    guard let program else { return }
    do {
      let next = try applyingDrafts()
      if next.source == program.source { replace(next) } else { commit(next) }
    } catch { notice = error.localizedDescription }
  }
  func shutdown() {
    sheet.cancel()
    revision = UUID()
    task?.cancel()
    client.shutdown()
    busy = false
  }
  func open() {
    let panel = NSOpenPanel()
    panel.allowedContentTypes = [UTType(filenameExtension: "nightfall") ?? .json, .json]
    present(panel) { [weak self] in self?.load($0) }
  }
  func saveCopy() {
    guard program != nil else { return }
    let panel = NSSavePanel()
    panel.nameFieldStringValue = "Part.nightfall"
    present(panel) { [weak self] in self?.saveCopy(to: $0) }
  }
  private func present(_ panel: NSSavePanel, selected: @escaping (URL) -> Void) {
    guard let window = documentWindow,
      let id = sheet.begin(for: documentID, cancel: { panel.cancel(nil) })
    else { return }
    NSApp.activate()
    window.makeKeyAndOrderFront(nil)
    panel.beginSheetModal(for: window) { [weak self, weak panel] response in
      guard let panel else { return }
      panel.orderOut(nil)
      guard let self, self.sheet.complete(id, for: self.documentID) else { return }
      if response == .OK, let url = panel.url { selected(url) }
    }
  }
  @discardableResult func saveCopy(to url: URL) -> Bool {
    guard program != nil else { return false }
    do {
      let next = try applyingDrafts()
      try next.source.write(to: url, options: .atomic)
      commit(next)
      savedSource = next.source
      notice = "Saved \(url.lastPathComponent)"
      return true
    } catch {
      notice = error.localizedDescription
      return false
    }
  }
  private func replace(_ next: PartProgram) {
    let previous = task
    previous?.cancel()
    let token = UUID()
    revision = token
    program = next
    drafts = Dictionary(uniqueKeysWithValues: next.parameters.map { ($0.id, $0.value) })
    errors = []
    notice = ""
    busy = true
    task = Task { [weak self] in
      await previous?.value
      guard let self, self.revision == token, !Task.isCancelled else { return }
      var response = await self.client.build(self.documentID, source: next.source)
      if response.diagnostics.first?.code == "busy" {
        try? await Task.sleep(for: .milliseconds(150))
        guard self.revision == token, !Task.isCancelled else { return }
        response = await self.client.build(self.documentID, source: next.source)
      }
      guard self.revision == token, !Task.isCancelled else { return }
      self.errors = response.diagnostics
      if let recovery = response.recovery {
        self.notice = "Recovery: feature \((recovery.featureIndex ?? 0) + 1) temporarily suppressed"
      }
      if self.errors.isEmpty {
        do {
          let result = try await Task.detached { [response] () -> (DisplayMesh?, [BodyMeasure]) in
            struct Measures: Decodable { let bodies: [BodyMeasure] }
            let bodies = try JSONDecoder().decode(Measures.self, from: response.manifest).bodies
            if bodies.isEmpty {
              guard response.vertices.isEmpty && response.indices.isEmpty && response.faces.isEmpty
              else { throw DisplayMesh.Invalid() }
              return (nil, [])
            }
            return (try DisplayMesh(response), bodies)
          }.value
          guard self.revision == token, !Task.isCancelled else { return }
          self.mesh = result.0
          self.bodies = result.1
          self.hiddenBodies.formIntersection(Set(result.1.map(\.id)))
          self.stale = false
        } catch {
          self.notice = error.localizedDescription
          self.stale = self.mesh != nil
        }
      } else {
        self.stale = self.mesh != nil
      }
      self.busy = false
    }
  }
}
