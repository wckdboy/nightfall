// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// SPIKE (#9): Metal renderer for the kernel's flat-shaded mesh: orbit camera fitted to the
// part, depth testing, and an offscreen path that writes a PNG (used for headless evidence).

import AppKit
import ImageIO
import Metal
import MetalKit
import UniformTypeIdentifiers
import simd

struct Uniforms {
  var modelViewProjection: simd_float4x4
  var modelView: simd_float4x4
}

@MainActor
final class Renderer: NSObject, MTKViewDelegate {
  let device: MTLDevice
  private let queue: MTLCommandQueue
  private let pipeline: MTLRenderPipelineState
  private let depthState: MTLDepthStencilState
  private var vertexBuffer: MTLBuffer?
  private var vertexCount = 0
  private var center = SIMD3<Float>(0, 0, 0)
  private var radius: Float = 1
  var yaw: Float = 0.7
  var pitch: Float = 0.5

  static let colorFormat = MTLPixelFormat.bgra8Unorm
  static let depthFormat = MTLPixelFormat.depth32Float

  init?(device: MTLDevice) {
    guard let queue = device.makeCommandQueue(), let library = device.makeDefaultLibrary() else {
      return nil
    }
    let descriptor = MTLRenderPipelineDescriptor()
    descriptor.vertexFunction = library.makeFunction(name: "mesh_vertex")
    descriptor.fragmentFunction = library.makeFunction(name: "mesh_fragment")
    descriptor.colorAttachments[0].pixelFormat = Self.colorFormat
    descriptor.depthAttachmentPixelFormat = Self.depthFormat
    let depth = MTLDepthStencilDescriptor()
    depth.depthCompareFunction = .less
    depth.isDepthWriteEnabled = true
    guard let pipeline = try? device.makeRenderPipelineState(descriptor: descriptor),
      let depthState = device.makeDepthStencilState(descriptor: depth)
    else { return nil }
    self.device = device
    self.queue = queue
    self.pipeline = pipeline
    self.depthState = depthState
  }

  /// Replaces the mesh (6 × Float32 per vertex) and refits the camera.
  func setMesh(_ data: Data) {
    vertexCount = data.count / (6 * MemoryLayout<Float>.size)
    vertexBuffer = data.withUnsafeBytes { device.makeBuffer(bytes: $0.baseAddress!, length: data.count) }
    var lo = SIMD3<Float>(repeating: .greatestFiniteMagnitude), hi = -lo
    data.withUnsafeBytes { raw in
      let f = raw.bindMemory(to: Float.self)
      for i in stride(from: 0, to: f.count, by: 6) {
        let p = SIMD3<Float>(f[i], f[i + 1], f[i + 2])
        lo = simd_min(lo, p)
        hi = simd_max(hi, p)
      }
    }
    center = (lo + hi) / 2
    radius = max(simd_length(hi - lo) / 2, 1e-3)
  }

  private func uniforms(aspect: Float) -> Uniforms {
    let eye = center + radius * 2.8 * SIMD3<Float>(cos(pitch) * cos(yaw), cos(pitch) * sin(yaw), sin(pitch))
    let view = lookAt(eye: eye, target: center, up: SIMD3<Float>(0, 0, 1))
    let projection = perspective(fovY: .pi / 4, aspect: aspect, near: radius * 0.1, far: radius * 10)
    return Uniforms(modelViewProjection: projection * view, modelView: view)
  }

  private func encode(into pass: MTLRenderPassDescriptor, aspect: Float, buffer: MTLCommandBuffer) {
    pass.colorAttachments[0].clearColor = MTLClearColor(red: 0.11, green: 0.12, blue: 0.14, alpha: 1)
    pass.colorAttachments[0].loadAction = .clear
    pass.depthAttachment.clearDepth = 1
    pass.depthAttachment.loadAction = .clear
    guard let encoder = buffer.makeRenderCommandEncoder(descriptor: pass) else { return }
    if let vertexBuffer, vertexCount > 0 {
      var u = uniforms(aspect: aspect)
      encoder.setRenderPipelineState(pipeline)
      encoder.setDepthStencilState(depthState)
      encoder.setVertexBuffer(vertexBuffer, offset: 0, index: 0)
      encoder.setVertexBytes(&u, length: MemoryLayout<Uniforms>.stride, index: 1)
      encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: vertexCount)
    }
    encoder.endEncoding()
  }

  // MARK: MTKViewDelegate

  nonisolated func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {}

  nonisolated func draw(in view: MTKView) {
    MainActor.assumeIsolated {
      guard let pass = view.currentRenderPassDescriptor, let drawable = view.currentDrawable,
        let buffer = queue.makeCommandBuffer()
      else { return }
      let size = view.drawableSize
      encode(into: pass, aspect: Float(size.width / max(size.height, 1)), buffer: buffer)
      buffer.present(drawable)
      buffer.commit()
    }
  }

  // MARK: Offscreen

  /// Renders one frame offscreen and writes it as a PNG.
  func writePNG(to url: URL, width: Int, height: Int) -> Bool {
    let color = MTLTextureDescriptor.texture2DDescriptor(
      pixelFormat: Self.colorFormat, width: width, height: height, mipmapped: false)
    color.usage = [.renderTarget, .shaderRead]
    color.storageMode = .shared
    let depth = MTLTextureDescriptor.texture2DDescriptor(
      pixelFormat: Self.depthFormat, width: width, height: height, mipmapped: false)
    depth.usage = .renderTarget
    depth.storageMode = .private
    guard let colorTexture = device.makeTexture(descriptor: color),
      let depthTexture = device.makeTexture(descriptor: depth),
      let buffer = queue.makeCommandBuffer()
    else { return false }
    let pass = MTLRenderPassDescriptor()
    pass.colorAttachments[0].texture = colorTexture
    pass.colorAttachments[0].storeAction = .store
    pass.depthAttachment.texture = depthTexture
    encode(into: pass, aspect: Float(width) / Float(height), buffer: buffer)
    buffer.commit()
    buffer.waitUntilCompleted()

    var bytes = [UInt8](repeating: 0, count: width * height * 4)
    colorTexture.getBytes(&bytes, bytesPerRow: width * 4,
                          from: MTLRegionMake2D(0, 0, width, height), mipmapLevel: 0)
    let info = CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedFirst.rawValue
      | CGBitmapInfo.byteOrder32Little.rawValue)
    guard let provider = CGDataProvider(data: Data(bytes) as CFData),
      let image = CGImage(width: width, height: height, bitsPerComponent: 8, bitsPerPixel: 32,
                          bytesPerRow: width * 4, space: CGColorSpaceCreateDeviceRGB(),
                          bitmapInfo: info, provider: provider, decode: nil,
                          shouldInterpolate: false, intent: .defaultIntent),
      let destination = CGImageDestinationCreateWithURL(url as CFURL, UTType.png.identifier as CFString, 1, nil)
    else { return false }
    CGImageDestinationAddImage(destination, image, nil)
    return CGImageDestinationFinalize(destination)
  }
}

// MARK: Matrices (right-handed, Metal clip-space depth 0…1)

func perspective(fovY: Float, aspect: Float, near: Float, far: Float) -> simd_float4x4 {
  let y = 1 / tan(fovY / 2), x = y / aspect, z = far / (near - far)
  return simd_float4x4(columns: (
    SIMD4<Float>(x, 0, 0, 0), SIMD4<Float>(0, y, 0, 0),
    SIMD4<Float>(0, 0, z, -1), SIMD4<Float>(0, 0, z * near, 0)))
}

func lookAt(eye: SIMD3<Float>, target: SIMD3<Float>, up: SIMD3<Float>) -> simd_float4x4 {
  let f = simd_normalize(target - eye)
  let s = simd_normalize(simd_cross(f, up))
  let u = simd_cross(s, f)
  return simd_float4x4(columns: (
    SIMD4<Float>(s.x, u.x, -f.x, 0), SIMD4<Float>(s.y, u.y, -f.y, 0),
    SIMD4<Float>(s.z, u.z, -f.z, 0),
    SIMD4<Float>(-simd_dot(s, eye), -simd_dot(u, eye), simd_dot(f, eye), 1)))
}
