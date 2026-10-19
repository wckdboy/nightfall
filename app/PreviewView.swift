// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
import SwiftUI

struct CADViewport: NSViewRepresentable {
  let mesh: DisplayMesh?
  let pose: CADCamera.Preset, fit: Int
  class Coordinator { var pose = CADCamera.Preset.iso, fit = 0 }
  func makeCoordinator() -> Coordinator { Coordinator() }
  func makeNSView(context: Context) -> OrbitView { OrbitView() }
  func updateNSView(_ view: OrbitView, context: Context) {
    if let mesh, view.meshID != mesh.id {
      try? view.renderer?.setMesh(mesh)
      view.meshID = mesh.id
    }
    if mesh == nil, view.meshID != nil {
      view.renderer?.clearMesh()
      view.meshID = nil
    }
    if context.coordinator.pose != pose {
      view.renderer?.camera.preset(pose)
      context.coordinator.pose = pose
    }
    if context.coordinator.fit != fit {
      view.renderer?.camera.fit()
      context.coordinator.fit = fit
    }
    view.needsDisplay = true
  }
}
struct PreviewView: View {
  @Bindable var model: PreviewDocument
  var body: some View {
    VStack(spacing: 0) {
      HStack(spacing: 14) {
        Image(systemName: "moon.stars.fill").foregroundStyle(.cyan)
        Text("NIGHTFALL").font(.system(size: 13, weight: .semibold, design: .rounded))
        Divider().frame(height: 18)
        Button("Open…", systemImage: "folder", action: model.open).keyboardShortcut("o")
          .disabled(model.presentingSheet)
        Menu("Examples") {
          ForEach(model.examples, id: \.self) { url in
            Button(
              url.deletingPathExtension().lastPathComponent.replacingOccurrences(of: "_", with: " ")
                .capitalized
            ) { model.load(url) }
          }
        }
        .disabled(model.presentingSheet)
        Spacer()
        if model.hasUnsavedChanges {
          Text("Unsaved changes").font(.caption).foregroundStyle(.secondary)
        }
        Button(action: model.undo) { Image(systemName: "arrow.uturn.backward") }.disabled(
          model.past.isEmpty && !model.hasPendingParameters
        ).keyboardShortcut("z")
        Button(action: model.redo) { Image(systemName: "arrow.uturn.forward") }.disabled(
          model.future.isEmpty || model.hasPendingParameters
        ).keyboardShortcut("z", modifiers: [.command, .shift])
        Button("Save copy…", systemImage: "square.and.arrow.down", action: model.saveCopy).disabled(
          model.program == nil || model.presentingSheet
        ).keyboardShortcut("s")
      }.buttonStyle(.borderless).padding(14)
      Divider()
      HStack(spacing: 0) {
        VStack(alignment: .leading, spacing: 14) {
          Label("Design history", systemImage: "list.bullet.indent").font(.headline).padding(
            .horizontal)
          Text(model.program?.name ?? "No part open").font(.subheadline).foregroundStyle(.secondary)
            .padding(.horizontal)
          if let end = model.program?.historyEnd {
            HStack {
              Text("Through \(end)").font(.caption).lineLimit(1)
              Spacer()
              Button("Restore") { model.endHistory(at: nil) }.disabled(model.busy)
                .accessibilityLabel("Restore full history")
            }.padding(.horizontal).foregroundStyle(.cyan)
          }
          ScrollView {
            LazyVStack(alignment: .leading, spacing: 8) {
              ForEach(Array((model.program?.features ?? []).enumerated()), id: \.offset) {
                index, feature in
                HStack(spacing: 8) {
                  Image(systemName: feature.type == "sketch" ? "pencil.and.outline" : "cube")
                  VStack(alignment: .leading, spacing: 3) {
                    Text(feature.id).font(.system(size: 12, weight: .medium)).strikethrough(
                      feature.suppressed)
                    Text(feature.type.capitalized).font(.system(size: 10)).foregroundStyle(
                      .secondary)
                  }
                  Spacer()
                  if model.errors.contains(where: { $0.featureIndex == index }) {
                    Image(systemName: "exclamationmark.circle.fill").foregroundStyle(.orange)
                  }
                  Button {
                    model.endHistory(at: feature.id)
                  } label: {
                    Image(
                      systemName: model.program?.historyEnd == feature.id
                        ? "backward.end.fill" : "backward.end")
                  }.help("Roll back through \(feature.id)")
                    .accessibilityLabel("Roll back through \(feature.id)")
                  Button {
                    model.setSuppressed(feature.id, to: !feature.suppressed)
                  } label: {
                    Image(systemName: feature.suppressed ? "circle" : "checkmark.circle.fill")
                  }.help(feature.suppressed ? "Enable \(feature.id)" : "Suppress \(feature.id)")
                    .accessibilityLabel(
                      feature.suppressed ? "Enable \(feature.id)" : "Suppress \(feature.id)")
                }.accessibilityElement(children: .contain).buttonStyle(.borderless).disabled(
                  model.busy
                )
                .opacity(feature.suppressed || feature.rolledBack ? 0.45 : 1).padding(.vertical, 5)
              }
            }.padding(.horizontal, 16)
          }
        }.padding(.top, 20).frame(width: 220)
        Divider()
        VStack(spacing: 0) {
          HStack {
            Picker("View", selection: $model.pose) {
              ForEach(CADCamera.Preset.allCases, id: \.self) { Text($0.rawValue).tag($0) }
            }.labelsHidden().frame(width: 125)
            Spacer()
            if model.busy {
              ProgressView().controlSize(.small)
              Text("Rebuilding…").font(.caption)
            }
            Button("Fit", systemImage: "arrow.up.left.and.arrow.down.right") { model.fit += 1 }
              .keyboardShortcut("f", modifiers: [])
          }.padding(12)
          ZStack(alignment: .topLeading) {
            CADViewport(mesh: model.mesh, pose: model.pose, fit: model.fit).accessibilityLabel(
              "CAD viewport")
            if model.mesh == nil {
              ContentUnavailableView {
                Label(
                  model.program == nil ? "Open a part" : "No solid bodies",
                  systemImage: "cube.transparent")
              } description: {
                Text(
                  model.program == nil
                    ? "Choose an example or open a Nightfall model."
                    : "Restore the history or enable a solid feature to see geometry.")
              }
            }
            if model.stale {
              Label("Last valid geometry", systemImage: "exclamationmark.triangle").font(.caption)
                .padding(10).background(.orange.opacity(0.15), in: Capsule()).padding()
            }
          }
        }.frame(maxWidth: .infinity, maxHeight: .infinity)
        Divider()
        ScrollView {
          VStack(alignment: .leading, spacing: 18) {
            Label("Parameters", systemImage: "slider.horizontal.3").font(.headline)
            if model.hasPendingParameters {
              Label("Unapplied parameters", systemImage: "clock").font(.caption)
                .foregroundStyle(.orange)
            }
            ForEach(model.program?.parameters ?? []) { parameter in
              VStack(alignment: .leading, spacing: 6) {
                Text(parameter.id).font(.caption).foregroundStyle(.secondary)
                TextField(
                  parameter.id,
                  text: Binding(
                    get: { model.drafts[parameter.id] ?? parameter.value },
                    set: { model.drafts[parameter.id] = $0 })
                )
                .textFieldStyle(.roundedBorder).font(.system(.body, design: .monospaced))
                .onSubmit { model.rebuild() }.disabled(model.busy)
                .accessibilityLabel("\(parameter.id) expression").help(
                  "Enter a value with units or a formula. Return applies all parameter edits.")
              }
            }
            Button(
              model.hasPendingParameters ? "Apply parameters" : "Rebuild",
              systemImage: "arrow.trianglehead.2.clockwise", action: model.rebuild
            ).disabled(model.program == nil || model.busy)
            Divider()
            Text(
              model.stale || model.busy
                ? "LAST VALID GEOMETRY"
                : (model.hasPendingParameters ? "APPLIED GEOMETRY" : "EXACT GEOMETRY")
            ).font(
              .system(size: 10, weight: .semibold)
            ).foregroundStyle(
              .secondary)
            ForEach(model.bodies) { body in
              VStack(alignment: .leading, spacing: 6) {
                Text(body.id).font(.subheadline.bold())
                Text("\(body.volume, format: .number.precision(.fractionLength(2))) mm³")
                  .monospacedDigit()
                Text("\(body.area, format: .number.precision(.fractionLength(2))) mm²").font(
                  .caption
                ).foregroundStyle(.secondary).monospacedDigit()
              }
            }
            ForEach(Array(model.errors.enumerated()), id: \.offset) { _, error in
              Label(error.message, systemImage: "exclamationmark.circle").foregroundStyle(.orange)
                .font(.caption).textSelection(.enabled)
            }
            if !model.notice.isEmpty {
              Text(model.notice).font(.caption).foregroundStyle(.secondary).textSelection(.enabled)
            }
          }.padding(20)
        }.frame(width: 260)
      }
      Divider()
      HStack {
        Circle().fill(model.errors.isEmpty ? Color.cyan : .orange).frame(width: 6, height: 6)
        Text(model.busy ? "Building" : "\(model.mesh?.triangleCount ?? 0) triangles · mm")
        Spacer()
        Text("Drag to orbit  ·  Shift-drag to pan  ·  Scroll to zoom  ·  F to fit")
      }.font(.system(size: 10)).foregroundStyle(.secondary).padding(.horizontal, 16).padding(
        .vertical, 8)
    }.frame(minWidth: 1080, minHeight: 680).preferredColorScheme(.dark).tint(.cyan)
      .onAppear { model.start() }.onDisappear { model.shutdown() }
  }
}
