// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
import Foundation

struct Reply: Sendable {
  let manifest: Data
  var vertices = Data(), indices = Data(), faces = Data()
  var pid: Int32 = 0
}

struct Failure: Error, CustomStringConvertible { let description: String }
func check(_ condition: @autoclosure () throws -> Bool, _ message: String) throws {
  if try !condition() { throw Failure(description: message) }
}
let document = "93A03342-B5B6-468F-AFD7-8BA2570AB681"
func plate(_ depth: Int = 30) -> Data {
  Data(
    """
    {"format":"nightfall.part","version":1,"units":{"length":"mm","angle":"deg"},
     "meta":{},"params":[{"name":"depth","value":"\(depth) mm"}],"references":[],
     "features":[{"id":"outline","type":"sketch","inputs":{"plane":{"datum":"xy"},"entities":[
       {"id":"r","type":"rect","corner":[0,0],"size":[10,20]}],"constraints":[]}},
       {"id":"base","type":"extrude","inputs":{"profile":{"sketch":"outline"},
       "distance":"depth","result":{"new":"plate","body_type":"brep"}}}],
     "history_end":null,"ui":{}}
    """.utf8)
}
func object(_ p: Reply) throws -> [String: Any] {
  guard let value = try JSONSerialization.jsonObject(with: p.manifest) as? [String: Any] else {
    throw Failure(description: "manifest is not an object")
  }
  return value
}
func failed(_ p: Reply, code: String) throws -> [String: Any] {
  let value = try object(p)
  let errors = value["errors"] as? [[String: Any]] ?? []
  try check(errors.contains { $0["code"] as? String == code }, "missing error \(code): \(value)")
  try check(p.vertices.isEmpty && p.indices.isEmpty && p.faces.isEmpty, "partial failure buffers")
  return errors[0]
}
func integer(_ value: Any?) -> Int { (value as? NSNumber)?.intValue ?? -999 }
func cacheHits(_ p: Reply) throws -> Int {
  integer((try object(p)["cache"] as? [String: Any])?["hits"])
}
struct Vec {
  var x: Double, y: Double, z: Double
  static func - (a: Vec, b: Vec) -> Vec { Vec(x: a.x - b.x, y: a.y - b.y, z: a.z - b.z) }
  static func + (a: Vec, b: Vec) -> Vec { Vec(x: a.x + b.x, y: a.y + b.y, z: a.z + b.z) }
  func dot(_ b: Vec) -> Double { x * b.x + y * b.y + z * b.z }
  func cross(_ b: Vec) -> Vec {
    Vec(x: y * b.z - z * b.y, y: z * b.x - x * b.z, z: x * b.y - y * b.x)
  }
}
func scalar<T>(_ data: Data, _ offset: Int, _: T.Type) -> T {
  data.withUnsafeBytes { $0.loadUnaligned(fromByteOffset: offset, as: T.self) }
}
func vector(_ data: Data, _ index: Int, normal: Bool = false) -> Vec {
  let offset = index * 48 + (normal ? 24 : 0)
  return Vec(
    x: scalar(data, offset, Double.self), y: scalar(data, offset + 8, Double.self),
    z: scalar(data, offset + 16, Double.self))
}
func geometry(_ p: Reply, volume: Double? = nil, area: Double? = nil) throws {
  let value = try object(p)
  try check((value["errors"] as? [Any])?.isEmpty == true, "geometry has errors: \(value)")
  guard let bodies = value["bodies"] as? [[String: Any]], bodies.count == 1,
    let exactV = bodies[0]["volume"] as? Double, let exactA = bodies[0]["area"] as? Double
  else {
    throw Failure(description: "missing exact body")
  }
  if let volume { try check(abs(exactV - volume) <= 1e-7, "exact volume") }
  if let area { try check(abs(exactA - area) <= 1e-7, "exact area") }
  let nv = p.vertices.count / 48
  let ni = p.indices.count / 4
  let nf = p.faces.count / 12
  try check(nv > 0 && p.vertices.count % 48 == 0, "vertex layout")
  try check(ni > 0 && p.indices.count % 12 == 0, "triangle layout")
  try check(nf > 0 && p.faces.count % 12 == 0, "face layout")
  try check(nv <= 3_000_000 && ni <= 3_000_000, "returned-data budget")
  try check((value["face_names"] as? [Any])?.count == nf, "face names/ranges")
  var next = 0
  for i in 0..<nf {
    try check(scalar(p.faces, i * 12, UInt32.self) == 0, "body association")
    try check(Int(scalar(p.faces, i * 12 + 4, UInt32.self)) == next, "range offset")
    let count = Int(scalar(p.faces, i * 12 + 8, UInt32.self))
    try check(count > 0 && count % 3 == 0, "range size")
    next += count
  }
  try check(next == ni, "face coverage")
  for i in 0..<nv {
    let position = vector(p.vertices, i)
    let n = vector(p.vertices, i, normal: true)
    try check(
      [position.x, position.y, position.z, n.x, n.y, n.z].allSatisfy(\.isFinite), "nonfinite mesh")
    try check(abs(n.dot(n).squareRoot() - 1) <= 1e-6, "unit surface normal")
  }
  var meshV = 0.0
  var meshA = 0.0
  let origin = vector(p.vertices, 0)
  for i in stride(from: 0, to: ni, by: 3) {
    let a = Int(scalar(p.indices, i * 4, UInt32.self))
    let b = Int(scalar(p.indices, (i + 1) * 4, UInt32.self))
    let c = Int(scalar(p.indices, (i + 2) * 4, UInt32.self))
    try check(a < nv && b < nv && c < nv, "index bounds")
    let pa = vector(p.vertices, a)
    let pb = vector(p.vertices, b)
    let pc = vector(p.vertices, c)
    let cross = (pb - pa).cross(pc - pa)
    let normal =
      vector(p.vertices, a, normal: true) + vector(p.vertices, b, normal: true)
      + vector(p.vertices, c, normal: true)
    try check(cross.dot(normal) > 0, "outward winding")
    meshA += cross.dot(cross).squareRoot() / 2
    meshV += (pa - origin).dot((pb - origin).cross(pc - origin)) / 6
  }
  try check(abs(meshV - exactV) <= exactV * 0.01, "mesh volume differs by over 1%")
  try check(abs(meshA - exactA) <= exactA * 0.01, "mesh area differs by over 1%")
}
