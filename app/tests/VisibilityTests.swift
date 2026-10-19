// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
import Foundation
import MetalKit

@MainActor func visibilityIntegration(_ directory: String, output: String) async throws {
  guard let device = MTLCreateSystemDefaultDevice() else { exit(77) }
  let url = URL(fileURLWithPath: directory).appendingPathComponent("asterion.nightfall")
  let original = try Data(contentsOf: url)
  let model = PreviewDocument()
  defer { model.shutdown() }
  model.load(url)
  try await waitForDocument(model)
  try check(model.errors.isEmpty && model.bodies.count == 8, "showcase build failed")
  let source = model.program?.source
  guard let mesh = model.mesh else { throw Failure(description: "showcase mesh missing") }
  let renderer = try MetalRenderer(device: device)
  try renderer.setMesh(mesh)
  let folder = URL(fileURLWithPath: output)
  try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
  let all = try renderPNG(renderer, to: folder.appendingPathComponent("asterion.png"))
  try check(Set(mesh.draws.map(\.body)) == Set(model.bodies.map(\.id)), "body draw coverage")
  try check(mesh.draws.count == 8, "face ranges were not coalesced by body")

  model.setBodyVisible("guard", to: false)
  try check(model.hiddenBodies == ["guard"], "hide guard")
  model.isolateBody("fins")
  try check(model.hiddenBodies.count == 7 && !model.hiddenBodies.contains("fins"), "isolate fins")
  renderer.hiddenBodies = model.hiddenBodies
  let isolated = try renderPNG(renderer, to: folder.appendingPathComponent("asterion-fins.png"))
  try check(all != isolated, "isolation did not change Metal pixels")
  try check(
    model.mesh?.id == mesh.id && model.program?.source == source && !model.hasUnsavedChanges,
    "view-only inspection edited geometry, IR or document dirty state")
  for body in model.bodies { model.setBodyVisible(body.id, to: false) }
  try check(model.allBodiesHidden, "all-hidden state missing")
  renderer.hiddenBodies = model.hiddenBodies
  let blank = try renderPNG(
    renderer, to: folder.appendingPathComponent("asterion-hidden.png"), allowEmpty: true)
  try check(
    stride(from: 0, to: blank.count, by: 4).allSatisfy {
      Array(blank[$0..<($0 + 4)]) == Array(blank[0..<4])
    }, "hidden triangles or boundary edges remained visible")
  model.showAllBodies()
  renderer.hiddenBodies = model.hiddenBodies
  let restored = try renderPNG(renderer, to: folder.appendingPathComponent("asterion-restored.png"))
  try check(restored == all && !model.allBodiesHidden, "show-all did not restore identical pixels")
  model.setBodyVisible("unknown", to: false)
  model.isolateBody("unknown")
  try check(model.hiddenBodies.isEmpty, "unknown body changed visibility")

  model.setBodyVisible("guard", to: false)
  model.drafts["fin_height"] = "18 mm"
  model.rebuild()
  try await waitForDocument(model)
  try check(model.errors.isEmpty && model.hiddenBodies == ["guard"], "rebuild lost visibility")
  let valid = model.mesh?.id
  model.drafts["fin_height"] = "100 mm"
  model.rebuild()
  try await waitForDocument(model)
  try check(
    !model.errors.isEmpty && model.stale && model.mesh?.id == valid
      && model.hiddenBodies == ["guard"], "failure changed last-valid inspection state")
  model.undo()
  try await waitForDocument(model)
  model.undo()
  try await waitForDocument(model)
  try check(!model.hasUnsavedChanges, "undo did not restore original document")
  model.load(URL(fileURLWithPath: directory).appendingPathComponent("rounded_plate.nightfall"))
  try await waitForDocument(model)
  try check(
    model.hiddenBodies.isEmpty && model.bodies.count == 1, "document switch kept hidden IDs")
  try check(try Data(contentsOf: url) == original, "inspection changed the example file")

  let client = KernelClient()
  defer { client.shutdown() }
  let response = await client.build(UUID(), source: original)
  func rejects(_ invalid: KernelResponse) throws {
    do {
      _ = try DisplayMesh(invalid)
      throw Failure(description: "invalid body/face association accepted")
    } catch is DisplayMesh.Invalid {}
  }
  var invalidBody = response
  invalidBody.faces.withUnsafeMutableBytes { $0.storeBytes(of: UInt32.max, as: UInt32.self) }
  try rejects(invalidBody)
  var gap = response
  gap.faces.withUnsafeMutableBytes {
    $0.storeBytes(of: UInt32(3), toByteOffset: 4, as: UInt32.self)
  }
  try rejects(gap)
  var missingFaces = response
  missingFaces.faces = Data()
  try rejects(missingFaces)
  var orphaned = response
  orphaned.faces.withUnsafeMutableBytes { raw in
    for offset in stride(from: 0, to: raw.count, by: 12) {
      raw.storeBytes(of: UInt32(0), toByteOffset: offset, as: UInt32.self)
    }
  }
  try rejects(orphaned)
  var overlap = response
  overlap.faces.withUnsafeMutableBytes {
    $0.storeBytes(of: UInt32(0), toByteOffset: 16, as: UInt32.self)
  }
  try rejects(overlap)
  var value = try JSONSerialization.jsonObject(with: response.manifest) as! [String: Any]
  var bodies = value["bodies"] as! [[String: Any]]
  bodies[1]["id"] = bodies[0]["id"]
  value["bodies"] = bodies
  try rejects(
    KernelResponse(
      manifest: JSONSerialization.data(withJSONObject: value), vertices: response.vertices,
      indices: response.indices, faces: response.faces))
  print("visibility: actual XPC inspection, immutable IR, failure recovery and Metal pixels passed")
}
