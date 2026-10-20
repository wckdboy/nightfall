// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
import AppKit
import MetalKit

/// Draws only on changes. Drag to orbit, Shift-drag/right-drag to pan, scroll/pinch to zoom.
@MainActor final class OrbitView: MTKView {
  let renderer: MetalRenderer?
  private(set) var meshID: UUID?
  private let uploadStatus = NSTextField(wrappingLabelWithString: "")
  var uploadError: String? { uploadStatus.isHidden ? nil : uploadStatus.stringValue }
  init(renderer supplied: MetalRenderer? = nil) {
    let device = supplied?.device ?? MTLCreateSystemDefaultDevice()
    renderer = supplied ?? device.flatMap { try? MetalRenderer(device: $0) }
    super.init(frame: .zero, device: device)
    colorPixelFormat = .bgra8Unorm
    depthStencilPixelFormat = .depth32Float
    isPaused = true
    enableSetNeedsDisplay = true
    delegate = renderer
    setAccessibilityElement(true)
    setAccessibilityRole(.image)
    setAccessibilityLabel("CAD viewport")
    uploadStatus.isHidden = true
    uploadStatus.textColor = .systemOrange
    uploadStatus.translatesAutoresizingMaskIntoConstraints = false
    addSubview(uploadStatus)
    NSLayoutConstraint.activate([
      uploadStatus.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 16),
      uploadStatus.topAnchor.constraint(equalTo: topAnchor, constant: 16),
      uploadStatus.trailingAnchor.constraint(lessThanOrEqualTo: trailingAnchor, constant: -16),
    ])
  }
  func updateMesh(_ mesh: DisplayMesh?) {
    guard let renderer else {
      uploadStatus.stringValue = "The graphics renderer is unavailable."
      uploadStatus.isHidden = false
      return
    }
    guard let mesh else {
      renderer.clearMesh()
      meshID = nil
      uploadStatus.isHidden = true
      needsDisplay = true
      return
    }
    guard meshID != mesh.id else { return }
    do {
      try renderer.setMesh(mesh)
      meshID = mesh.id
      uploadStatus.isHidden = true
    } catch {
      renderer.clearMesh()
      meshID = nil
      uploadStatus.stringValue = "Unable to display the model. Try Rebuild."
      uploadStatus.isHidden = false
    }
    needsDisplay = true
  }
  required init(coder: NSCoder) { fatalError("init(coder:) is not supported") }
  override var acceptsFirstResponder: Bool { true }
  override func mouseDown(with event: NSEvent) { window?.makeFirstResponder(self) }
  override func mouseDragged(with event: NSEvent) {
    guard let renderer else { return }
    if event.modifierFlags.contains(.shift) {
      renderer.camera.pan +=
        SIMD2(Float(event.deltaX), -Float(event.deltaY)) * renderer.camera.zoom * 0.005
    } else {
      renderer.camera.yaw += Float(event.deltaX) * 0.01
      renderer.camera.pitch = min(
        max(renderer.camera.pitch + Float(event.deltaY) * 0.01, -.pi / 2), .pi / 2)
    }
    needsDisplay = true
  }
  override func rightMouseDragged(with event: NSEvent) {
    renderer?.camera.pan +=
      SIMD2(Float(event.deltaX), -Float(event.deltaY)) * (renderer?.camera.zoom ?? 1) * 0.005
    needsDisplay = true
  }
  override func scrollWheel(with event: NSEvent) {
    renderer?.camera.zoom = min(
      max((renderer?.camera.zoom ?? 1) * exp(-Float(event.scrollingDeltaY) * 0.02), 0.15), 8)
    needsDisplay = true
  }
  override func magnify(with event: NSEvent) {
    renderer?.camera.zoom = min(
      max((renderer?.camera.zoom ?? 1) * (1 - Float(event.magnification)), 0.15), 8)
    needsDisplay = true
  }
}
