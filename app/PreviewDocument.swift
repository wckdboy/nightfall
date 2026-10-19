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
  @ObservationIgnored private var task: Task<Void, Never>?
  private var documentID = UUID()
  private var revision = UUID()
  private var savedSource: Data?
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
      let part = examples.first(where: { $0.lastPathComponent == "mounting_plate.nightfall" })
    {
      load(part)
    }
  }
  func load(_ url: URL) {
    if let program, program.source != savedSource {
      let alert = NSAlert()
      alert.messageText = "Discard unsaved changes?"
      alert.informativeText = "Choose Keep editing to save a copy first."
      alert.addButton(withTitle: "Keep editing")
      alert.addButton(withTitle: "Discard")
      guard alert.runModal() == .alertSecondButtonReturn else { return }
    }
    do {
      let part = try PartProgram(Data(contentsOf: url, options: .mappedIfSafe))
      client.close(documentID)
      documentID = UUID()
      savedSource = part.source
      bodies = []
      past = []
      future = []
      mesh = nil
      fit += 1
      replace(part)
    } catch { notice = error.localizedDescription }
  }
  func edit(_ name: String) {
    guard let program else { return }
    do {
      let next = try program.setting(name, to: drafts[name] ?? "")
      past.append(program)
      future = []
      replace(next)
    } catch { notice = error.localizedDescription }
  }
  func undo() {
    if let previous = past.popLast(), let program {
      future.append(program)
      replace(previous)
    }
  }
  func redo() {
    if let next = future.popLast(), let program {
      past.append(program)
      replace(next)
    }
  }
  func rebuild() { if let program { replace(program) } }
  func shutdown() {
    revision = UUID()
    task?.cancel()
    client.shutdown()
    busy = false
  }
  func open() {
    let panel = NSOpenPanel()
    panel.allowedContentTypes = [UTType(filenameExtension: "nightfall") ?? .json, .json]
    if panel.runModal() == .OK, let url = panel.url { load(url) }
  }
  func saveCopy() {
    guard let program else { return }
    let panel = NSSavePanel()
    panel.nameFieldStringValue = "Part.nightfall"
    if panel.runModal() == .OK, let url = panel.url {
      do {
        try program.source.write(to: url, options: .atomic)
        savedSource = program.source
        notice = "Saved \(url.lastPathComponent)"
      } catch { notice = error.localizedDescription }
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
          let result = try await Task.detached { [response] () -> (DisplayMesh, [BodyMeasure]) in
            struct Measures: Decodable { let bodies: [BodyMeasure] }
            return (
              try DisplayMesh(response),
              try JSONDecoder().decode(Measures.self, from: response.manifest).bodies
            )
          }.value
          guard self.revision == token, !Task.isCancelled else { return }
          self.mesh = result.0
          self.bodies = result.1
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
