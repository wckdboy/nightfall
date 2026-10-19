// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
import Foundation
import simd

/// Derived display data only. Convert off the main actor; exact measures stay in the manifest.
struct DisplayMesh: Sendable {
  struct Invalid: Error, LocalizedError {
    var errorDescription: String? { "The kernel returned an invalid display mesh." }
  }
  let id = UUID()
  let vertices: Data, indices: Data, edges: Data
  var triangleCount: Int { indices.count / 12 }
  init(_ response: KernelResponse) throws {
    let data = response.vertices
    let index = response.indices
    guard response.diagnostics.isEmpty, !data.isEmpty, !index.isEmpty,
      data.count % 48 == 0, index.count % 12 == 0,
      data.count / 48 <= 3_000_000, index.count / 4 <= 3_000_000
    else { throw Invalid() }
    let count = data.count / 48
    var lo = SIMD3<Double>(repeating: .infinity)
    var hi = -lo
    try data.withUnsafeBytes { raw in
      for v in 0..<count {
        let p = SIMD3<Double>(
          raw.loadUnaligned(fromByteOffset: v * 48, as: Double.self),
          raw.loadUnaligned(fromByteOffset: v * 48 + 8, as: Double.self),
          raw.loadUnaligned(fromByteOffset: v * 48 + 16, as: Double.self))
        guard p.x.isFinite && p.y.isFinite && p.z.isFinite else { throw Invalid() }
        lo = simd_min(lo, p)
        hi = simd_max(hi, p)
      }
    }
    let span = hi - lo
    let scale = max(span.x, max(span.y, span.z)) / 2
    guard scale.isFinite && scale > 0 else { throw Invalid() }
    let center = lo + span / 2
    var packed = [Float](repeating: 0, count: count * 6)
    try data.withUnsafeBytes { raw in
      for v in 0..<count {
        var normal = SIMD3<Float>()
        for c in 0..<6 {
          let value = raw.loadUnaligned(fromByteOffset: v * 48 + c * 8, as: Double.self)
          let converted = Float(c < 3 ? (value - center[c]) / scale : value)
          guard converted.isFinite else { throw Invalid() }
          packed[v * 6 + c] = converted
          if c >= 3 { normal[c - 3] = converted }
        }
        guard abs(simd_length(normal) - 1) <= 1e-4 else { throw Invalid() }
      }
    }
    try index.withUnsafeBytes { raw in
      for offset in stride(from: 0, to: index.count, by: 4) {
        guard raw.loadUnaligned(fromByteOffset: offset, as: UInt32.self) < count else {
          throw Invalid()
        }
      }
    }
    vertices = packed.withUnsafeBytes { Data($0) }
    indices = index
    let values = index.withUnsafeBytes { raw in
      stride(from: 0, to: index.count, by: 4).map {
        raw.loadUnaligned(fromByteOffset: $0, as: UInt32.self)
      }
    }
    guard response.faces.count % 12 == 0 else { throw Invalid() }
    let ranges = try response.faces.withUnsafeBytes { raw -> [Range<Int>] in
      if raw.isEmpty { return [0..<values.count] }
      return try stride(from: 0, to: raw.count, by: 12).map { offset in
        let first = Int(raw.loadUnaligned(fromByteOffset: offset + 4, as: UInt32.self))
        let length = Int(raw.loadUnaligned(fromByteOffset: offset + 8, as: UInt32.self))
        guard first % 3 == 0, length % 3 == 0, first + length <= values.count else {
          throw Invalid()
        }
        return first..<(first + length)
      }
    }
    var lines: [UInt32] = []
    for range in ranges {
      var uses: [UInt64: Int] = [:]
      for i in stride(from: range.lowerBound, to: range.upperBound, by: 3) {
        for (a, b) in [
          (values[i], values[i + 1]), (values[i + 1], values[i + 2]), (values[i + 2], values[i]),
        ] {
          let key = UInt64(min(a, b)) << 32 | UInt64(max(a, b))
          uses[key, default: 0] += 1
        }
      }
      for (key, count) in uses where count == 1 {
        lines += [UInt32(key >> 32), UInt32(truncatingIfNeeded: key)]
      }
    }
    edges = lines.withUnsafeBytes { Data($0) }
  }
}
