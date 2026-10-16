// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// SPIKE (#8, #9): parameters drive the JSON IR; every change rebuilds in the kernel service
// and re-renders. Drag to orbit.

import MetalKit
import SwiftUI

struct ContentView: View {
  @StateObject private var kernel = KernelClient()
  @State private var renderer = MTLCreateSystemDefaultDevice().flatMap { Renderer(device: $0) }
  @State private var width = 40.0
  @State private var fillet = 3.0
  @State private var hole = 5.0
  @State private var revision = 0

  var body: some View {
    VStack(spacing: 8) {
      if let renderer {
        MeshView(renderer: renderer, revision: revision)
          .gesture(DragGesture().onChanged { g in
            renderer.yaw -= Float(g.velocity.width) * 0.00004
            renderer.pitch = min(1.5, max(-1.5, renderer.pitch + Float(g.velocity.height) * 0.00004))
          })
      } else {
        Text("Metal unavailable")
      }
      HStack {
        slider("Width", $width, 20...80)
        slider("Fillet", $fillet, 0...5)
        slider("Hole", $hole, 1...9)
        Button("Crash kernel") { kernel.crashKernel() }
      }
      Text(kernel.status).font(.caption.monospaced()).frame(maxWidth: .infinity, alignment: .leading)
    }
    .padding()
    .frame(minWidth: 900, minHeight: 640)
    .task(id: [width, fillet, hole]) { await rebuild() }
  }

  private func slider(_ label: String, _ value: Binding<Double>, _ range: ClosedRange<Double>) -> some View {
    HStack {
      Text("\(label) \(value.wrappedValue, specifier: "%.1f")").frame(width: 90, alignment: .leading)
      Slider(value: value, in: range)
    }
  }

  private func rebuild() async {
    let ir = PartIR(features: [
      .init(type: "box", size: [width, 30, 12]),
      .init(type: "fillet_all", radius: fillet),
      .init(type: "hole", radius: hole, at: [width / 2, 15]),
    ])
    if let result = await kernel.build(ir) {
      renderer?.setMesh(result.mesh)
      revision += 1
    }
  }
}

struct MeshView: NSViewRepresentable {
  let renderer: Renderer
  let revision: Int

  func makeNSView(context: Context) -> MTKView {
    let view = MTKView(frame: .zero, device: renderer.device)
    view.colorPixelFormat = Renderer.colorFormat
    view.depthStencilPixelFormat = Renderer.depthFormat
    view.delegate = renderer
    return view
  }

  func updateNSView(_ view: MTKView, context: Context) { view.setNeedsDisplay(view.bounds) }
}
