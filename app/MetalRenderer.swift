// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
import MetalKit
import simd

struct CADCamera {
  enum Preset: String, CaseIterable {
    case iso = "Isometric"
    case top = "Top"
    case front = "Front"
    case right = "Right"
  }
  var yaw: Float = -0.7, pitch: Float = -0.9, zoom: Float = 1
  var pan = SIMD2<Float>()
  mutating func fit() {
    zoom = 1
    pan = .zero
  }
  mutating func preset(_ value: Preset) {
    yaw = value == .iso ? -0.7 : (value == .right ? -.pi / 2 : 0)
    pitch = value == .iso ? -0.9 : (value == .top ? 0 : -.pi / 2)
    fit()
  }
  func matrix(aspect: Float) -> simd_float4x4 {
    let height = 1.7 * zoom / max(min(aspect, 1), 0.01)
    let width = height * aspect
    let rotation =
      simd_quatf(angle: pitch, axis: SIMD3(1, 0, 0)) * simd_quatf(angle: yaw, axis: SIMD3(0, 0, 1))
    var view = simd_float4x4(rotation)
    view.columns.3 = SIMD4(pan.x, pan.y, -4, 1)
    let projection = simd_float4x4(
      columns: (
        SIMD4(1 / width, 0, 0, 0), SIMD4(0, 1 / height, 0, 0),
        SIMD4(0, 0, -1 / 9.9, 0), SIMD4(0, 0, -0.1 / 9.9, 1)
      ))
    return projection * view
  }
}
@MainActor final class MetalRenderer: NSObject, MTKViewDelegate {
  let device: MTLDevice, queue: MTLCommandQueue
  private let pipeline: MTLRenderPipelineState, depth: MTLDepthStencilState
  private let edgePipeline: MTLRenderPipelineState, edgeDepth: MTLDepthStencilState
  private var vertices: MTLBuffer?, indices: MTLBuffer?, edges: MTLBuffer?
  private var indexCount = 0
  private var edgeCount = 0
  private var draws: [DisplayMesh.Draw] = []
  var hiddenBodies: Set<String> = []
  var camera = CADCamera()
  private let makeBuffer: (UnsafeRawPointer, Int) -> MTLBuffer?
  init(device: MTLDevice, makeBuffer: ((UnsafeRawPointer, Int) -> MTLBuffer?)? = nil) throws {
    self.makeBuffer =
      makeBuffer ?? { bytes, length in
        device.makeBuffer(bytes: bytes, length: length, options: .storageModeShared)
      }
    let library = try device.makeLibrary(source: Self.shader, options: nil)
    guard let queue = device.makeCommandQueue(),
      let vertex = library.makeFunction(name: "vertexMain"),
      let fragment = library.makeFunction(name: "fragmentMain")
    else { throw DisplayMesh.Invalid() }
    self.device = device
    self.queue = queue
    let descriptor = MTLRenderPipelineDescriptor()
    descriptor.vertexFunction = vertex
    descriptor.fragmentFunction = fragment
    descriptor.colorAttachments[0].pixelFormat = .bgra8Unorm
    descriptor.depthAttachmentPixelFormat = .depth32Float
    pipeline = try device.makeRenderPipelineState(descriptor: descriptor)
    let state = MTLDepthStencilDescriptor()
    state.depthCompareFunction = .less
    state.isDepthWriteEnabled = true
    guard let depth = device.makeDepthStencilState(descriptor: state) else {
      throw DisplayMesh.Invalid()
    }
    self.depth = depth
    descriptor.fragmentFunction = library.makeFunction(name: "edgeFragment")
    edgePipeline = try device.makeRenderPipelineState(descriptor: descriptor)
    state.depthCompareFunction = .lessEqual
    state.isDepthWriteEnabled = false
    guard let edgeDepth = device.makeDepthStencilState(descriptor: state) else {
      throw DisplayMesh.Invalid()
    }
    self.edgeDepth = edgeDepth
  }
  func setMesh(_ mesh: DisplayMesh) throws {
    let vertex = mesh.vertices.withUnsafeBytes {
      makeBuffer($0.baseAddress!, $0.count)
    }
    let index = mesh.indices.withUnsafeBytes {
      makeBuffer($0.baseAddress!, $0.count)
    }
    guard let vertex, let index else { throw DisplayMesh.Invalid() }
    let edge =
      mesh.edges.isEmpty
      ? nil
      : mesh.edges.withUnsafeBytes {
        makeBuffer($0.baseAddress!, $0.count)
      }
    guard mesh.edges.isEmpty || edge != nil else { throw DisplayMesh.Invalid() }
    vertices = vertex
    indices = index
    indexCount = mesh.indices.count / 4
    edges = edge
    edgeCount = mesh.edges.count / 4
    draws = mesh.draws
  }
  func clearMesh() {
    vertices = nil
    indices = nil
    edges = nil
    indexCount = 0
    edgeCount = 0
    draws = []
  }
  func encode(_ pass: MTLRenderPassDescriptor, aspect: Float, command: MTLCommandBuffer) {
    pass.colorAttachments[0].clearColor = MTLClearColor(
      red: 0.045, green: 0.055, blue: 0.075, alpha: 1)
    pass.colorAttachments[0].loadAction = .clear
    pass.depthAttachment.clearDepth = 1
    pass.depthAttachment.loadAction = .clear
    guard let encoder = command.makeRenderCommandEncoder(descriptor: pass) else { return }
    if let vertices, let indices, indexCount > 0 {
      var matrix = camera.matrix(aspect: max(aspect, 0.01))
      encoder.setRenderPipelineState(pipeline)
      encoder.setDepthStencilState(depth)
      encoder.setCullMode(.back)
      encoder.setFrontFacing(.counterClockwise)
      encoder.setVertexBuffer(vertices, offset: 0, index: 0)
      encoder.setVertexBytes(&matrix, length: MemoryLayout<simd_float4x4>.stride, index: 1)
      let visible = draws.filter { !hiddenBodies.contains($0.body) }
      for draw in visible {
        encoder.drawIndexedPrimitives(
          type: .triangle, indexCount: draw.triangles.count, indexType: .uint32,
          indexBuffer: indices, indexBufferOffset: draw.triangles.lowerBound * 4)
      }
      if let edges, edgeCount > 0 {
        encoder.setRenderPipelineState(edgePipeline)
        encoder.setDepthStencilState(edgeDepth)
        encoder.setDepthBias(-1, slopeScale: -1, clamp: 0)
        for draw in visible where !draw.edges.isEmpty {
          encoder.drawIndexedPrimitives(
            type: .line, indexCount: draw.edges.count, indexType: .uint32, indexBuffer: edges,
            indexBufferOffset: draw.edges.lowerBound * 4)
        }
      }
    }
    encoder.endEncoding()
  }
  nonisolated func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {}
  nonisolated func draw(in view: MTKView) {
    MainActor.assumeIsolated {
      guard let pass = view.currentRenderPassDescriptor, let drawable = view.currentDrawable,
        let command = queue.makeCommandBuffer()
      else { return }
      encode(
        pass, aspect: Float(view.drawableSize.width / max(view.drawableSize.height, 1)),
        command: command)
      command.present(drawable)
      command.commit()
    }
  }
  private static let shader = """
    #include <metal_stdlib>
    using namespace metal;
    struct Vertex { packed_float3 position; packed_float3 normal; };
    struct Raster { float4 position [[position]]; float3 normal; };
    vertex Raster vertexMain(uint id [[vertex_id]], const device Vertex* mesh [[buffer(0)]],
                             constant float4x4& matrix [[buffer(1)]]) {
      Raster result;
      result.position = matrix * float4(float3(mesh[id].position), 1);
      result.normal = float3(mesh[id].normal);
      return result;
    }
    fragment float4 fragmentMain(Raster in [[stage_in]]) {
      float3 n = normalize(in.normal);
      float light = 0.25 + 0.65 * max(dot(n, normalize(float3(0.4,-0.6,0.8))),0.0)
                         + 0.22 * max(dot(n, normalize(float3(-0.7,0.5,0.2))),0.0);
      return float4(float3(0.64,0.74,0.9) * light, 1);
    }
    fragment float4 edgeFragment(Raster in [[stage_in]]) { return float4(0.08,0.12,0.18,1); }
    """
}
