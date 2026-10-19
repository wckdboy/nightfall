// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
import AppKit
import MetalKit

/// Draws only on changes. Drag to orbit, Shift-drag/right-drag to pan, scroll/pinch to zoom.
@MainActor final class OrbitView: MTKView {
  let renderer: MetalRenderer?
  var meshID: UUID?
  init() {
    let device = MTLCreateSystemDefaultDevice()
    renderer = device.flatMap { try? MetalRenderer(device: $0) }
    super.init(frame: .zero, device: device)
    colorPixelFormat = .bgra8Unorm
    depthStencilPixelFormat = .depth32Float
    isPaused = true
    enableSetNeedsDisplay = true
    delegate = renderer
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
