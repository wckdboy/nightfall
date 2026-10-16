// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// SPIKE (#8): NightfallKernel.xpc. Decodes the JSON IR, builds the part through the C kernel
// bridge (OCCT), tessellates it and replies with the mesh. Runs out of process so a kernel
// crash or hang cannot take the app down.

import Foundation

final class KernelService: NSObject, KernelServiceProtocol {
  func build(_ ir: Data, reply: @escaping @Sendable (Data?, Double, String?) -> Void) {
    do {
      let part = try JSONDecoder().decode(PartIR.self, from: ir)
      let (mesh, volume) = try Self.make(part)
      reply(mesh, volume, nil)
    } catch {
      reply(nil, 0, String(describing: error))
    }
  }

  func crash() { abort() }

  struct KernelError: Error, CustomStringConvertible { let description: String }

  static func make(_ ir: PartIR) throws -> (Data, Double) {
    guard ir.version == 1 else { throw KernelError(description: "unsupported IR version \(ir.version)") }
    var part: OpaquePointer?
    defer { if let part { nf_part_free(part) } }
    for feature in ir.features {
      switch feature.type {
      case "box":
        guard let s = feature.size, s.count == 3 else { throw KernelError(description: "box needs size[3]") }
        part = nf_part_box(s[0], s[1], s[2])
        if part == nil { throw KernelError(description: "box failed") }
      case "fillet_all":
        guard let part, let r = feature.radius else { throw KernelError(description: "fillet needs a body and radius") }
        if r > 0, nf_part_fillet_all(part, r) != 0 { throw KernelError(description: "fillet r=\(r) failed") }
      case "hole":
        guard let part, let r = feature.radius, let at = feature.at, at.count == 2 else {
          throw KernelError(description: "hole needs a body, radius and at[2]")
        }
        if nf_part_hole(part, at[0], at[1], r) != 0 { throw KernelError(description: "hole failed") }
      default:
        throw KernelError(description: "unknown feature '\(feature.type)'")
      }
    }
    guard let part else { throw KernelError(description: "empty part") }
    var mesh = nf_mesh()
    guard nf_part_mesh(part, 0.02, &mesh) == 0 else { throw KernelError(description: "meshing failed") }
    defer { nf_mesh_free(&mesh) }
    let data = Data(bytes: mesh.vertices, count: mesh.vertex_count * 6 * MemoryLayout<Float>.size)
    return (data, mesh.volume)
  }
}

final class ServiceDelegate: NSObject, NSXPCListenerDelegate {
  func listener(_ listener: NSXPCListener, shouldAcceptNewConnection connection: NSXPCConnection) -> Bool {
    connection.exportedInterface = NSXPCInterface(with: KernelServiceProtocol.self)
    connection.exportedObject = KernelService()
    connection.resume()
    return true
  }
}

let delegate = ServiceDelegate()
let listener = NSXPCListener.service()
listener.delegate = delegate
listener.resume()
