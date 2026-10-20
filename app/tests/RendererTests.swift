// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
import AppKit
import Foundation
import ImageIO
import MetalKit
import UniformTypeIdentifiers

func meshValidation() throws {
  let vertices: [Double] = [0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 1]
  let indices: [UInt32] = [0, 1, 2]
  func response(_ v: [Double], _ i: [UInt32]) -> KernelResponse {
    KernelResponse(
      manifest: Data("{\"errors\":[],\"bodies\":[{\"id\":\"part\"}]}".utf8),
      vertices: v.withUnsafeBytes { Data($0) }, indices: i.withUnsafeBytes { Data($0) },
      faces: [UInt32(0), 0, UInt32(i.count)].withUnsafeBytes { Data($0) })
  }
  let mesh = try DisplayMesh(response(vertices, indices))
  try check(mesh.triangleCount == 1 && mesh.vertices.count == 18 * 4, "display vertex/index layout")
  try check(mesh.edges.count == 6 * 4, "triangle boundary edges")
  let square = vertices + [1, 1, 0, 0, 0, 1]
  let quad = try DisplayMesh(response(square, [0, 1, 2, 1, 3, 2]))
  try check(quad.edges.count == 8 * 4, "internal triangulation diagonal was drawn")
  let positions = mesh.vertices.withUnsafeBytes { Array($0.bindMemory(to: Float.self)) }
  try check(positions.allSatisfy(\.isFinite) && abs(positions[0]) <= 1, "normalized finite mesh")
  for invalid in [
    response([], []), response(vertices, [0, 1, UInt32.max]),
    response(vertices, [0, 1]), response([Double.nan] + vertices.dropFirst(), indices),
    response(Array(vertices.dropLast()), indices),
  ] {
    do {
      _ = try DisplayMesh(invalid)
      throw Failure(description: "invalid mesh accepted")
    } catch is DisplayMesh.Invalid {}
  }
}
@MainActor func renderPNG(
  _ renderer: MetalRenderer, to url: URL, allowEmpty: Bool = false, minimumCoverage: Double = 0.01
) throws
  -> [UInt8]
{
  let width = 960
  let height = 720
  let color = MTLTextureDescriptor.texture2DDescriptor(
    pixelFormat: .bgra8Unorm, width: width, height: height, mipmapped: false)
  color.storageMode = .shared
  color.usage = [.renderTarget, .shaderRead]
  let depth = MTLTextureDescriptor.texture2DDescriptor(
    pixelFormat: .depth32Float, width: width, height: height, mipmapped: false)
  depth.storageMode = .private
  depth.usage = .renderTarget
  guard let c = renderer.device.makeTexture(descriptor: color),
    let d = renderer.device.makeTexture(descriptor: depth),
    let command = renderer.queue.makeCommandBuffer()
  else { throw Failure(description: "offscreen allocation") }
  let pass = MTLRenderPassDescriptor()
  pass.colorAttachments[0].texture = c
  pass.colorAttachments[0].storeAction = .store
  pass.depthAttachment.texture = d
  renderer.encode(pass, aspect: Float(width) / Float(height), command: command)
  command.commit()
  command.waitUntilCompleted()
  try check(command.status == .completed && command.error == nil, "Metal execution")
  var bytes = [UInt8](repeating: 0, count: width * height * 4)
  c.getBytes(
    &bytes, bytesPerRow: width * 4,
    from: MTLRegionMake2D(0, 0, width, height), mipmapLevel: 0)
  let lit = stride(from: 0, to: bytes.count, by: 4).filter {
    bytes[$0] != bytes[0] || bytes[$0 + 1] != bytes[1] || bytes[$0 + 2] != bytes[2]
  }.count
  try check(
    (allowEmpty || Double(lit) > Double(width * height) * minimumCoverage)
      && lit < width * height * 4 / 5,
    "blank or clipped render: \(lit)/\(width * height) foreground pixels")
  let info = CGBitmapInfo(
    rawValue: CGImageAlphaInfo.premultipliedFirst.rawValue | CGBitmapInfo.byteOrder32Little.rawValue
  )
  guard let provider = CGDataProvider(data: Data(bytes) as CFData),
    let image = CGImage(
      width: width, height: height, bitsPerComponent: 8, bitsPerPixel: 32,
      bytesPerRow: width * 4, space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: info,
      provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent),
    let destination = CGImageDestinationCreateWithURL(
      url as CFURL, UTType.png.identifier as CFString, 1, nil)
  else { throw Failure(description: "PNG setup") }
  CGImageDestinationAddImage(destination, image, nil)
  try check(CGImageDestinationFinalize(destination), "PNG write")
  return bytes
}
@MainActor func rendererIntegration(_ directory: String, output: String) async throws {
  try meshValidation()
  guard let device = MTLCreateSystemDefaultDevice() else {
    print("Metal device unavailable; GPU validation requires a physical Mac")
    exit(77)
  }
  let renderer = try MetalRenderer(device: device)
  let client = KernelClient()
  defer { client.shutdown() }
  let folder = URL(fileURLWithPath: output)
  try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
  let parts = try FileManager.default.contentsOfDirectory(atPath: directory)
    .filter { $0.hasSuffix(".nightfall") }.sorted()
  try check(parts.count == 20, "render reference count")
  for part in parts {
    let response = await client.build(
      UUID(),
      source: try Data(contentsOf: URL(fileURLWithPath: directory).appendingPathComponent(part))
    )
    try geometry(packet(response))
    let mesh = try await Task.detached { try DisplayMesh(response) }.value
    if part == parts[0] {
      try viewportUploadFailures(response, device: device, output: folder)
    }
    try renderer.setMesh(mesh)
    _ = try renderPNG(renderer, to: folder.appendingPathComponent(part + ".png"))
    print("\(part): \(mesh.triangleCount) indexed triangles rendered")
  }
  let iso = try renderPNG(renderer, to: folder.appendingPathComponent("iso.png"))
  renderer.camera.preset(.top)
  let top = try renderPNG(renderer, to: folder.appendingPathComponent("top.png"))
  try check(iso != top, "camera preset did not change the image")
  renderer.camera.preset(.iso)
  renderer.camera.zoom = 0.7
  let zoom = try renderPNG(renderer, to: folder.appendingPathComponent("zoom.png"))
  try check(zoom != iso, "camera zoom did not change the image")
}

@MainActor func viewportUploadFailures(_ response: KernelResponse, device: MTLDevice, output: URL)
  throws
{
  let mesh = try DisplayMesh(response)
  let replacement = try DisplayMesh(response)
  var allocations = 0
  var failAt: Int?
  let renderer = try MetalRenderer(device: device) { bytes, length in
    allocations += 1
    if allocations == failAt { return nil }
    return device.makeBuffer(bytes: bytes, length: length, options: .storageModeShared)
  }
  let view = OrbitView(renderer: renderer)
  let empty = try renderPNG(
    renderer, to: output.appendingPathComponent("viewport-empty.png"), allowEmpty: true)
  for allocation in 1...3 {
    view.updateMesh(mesh)
    try check(view.meshID == mesh.id && view.uploadError == nil, "successful viewport upload")
    allocations = 0
    failAt = allocation
    view.updateMesh(replacement)
    try check(
      view.meshID == nil && view.uploadError != nil, "failed upload published mesh identity")
    let failed = try renderPNG(
      renderer, to: output.appendingPathComponent("upload-failed-\(allocation).png"),
      allowEmpty: true)
    try check(failed == empty, "failed upload displayed the previous geometry")
    failAt = nil
    view.updateMesh(replacement)
    try check(
      view.meshID == replacement.id && view.uploadError == nil, "same source did not recover")
    view.updateMesh(nil)
  }
  view.updateMesh(nil)
  try check(view.meshID == nil && view.uploadError == nil, "empty viewport retained upload status")
  print("viewport: vertex/index/edge allocation failures publish no ID; same source recovers")
}
