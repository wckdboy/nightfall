// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
import Foundation

struct PartProgram {
  struct Parameter: Identifiable { let id: String, value: String }
  struct Feature: Identifiable { let id: String, type: String }
  let source: Data
  private let object: [String: Any]
  init(_ source: Data) throws {
    guard source.count <= 16 * 1024 * 1024,
      let value = try JSONSerialization.jsonObject(with: source) as? [String: Any]
    else { throw CocoaError(.fileReadCorruptFile) }
    self.source = source
    object = value
  }
  var name: String { (object["meta"] as? [String: Any])?["name"] as? String ?? "Untitled part" }
  var parameters: [Parameter] {
    var seen = Set<String>()
    return (object["params"] as? [[String: Any]] ?? []).compactMap {
      guard let id = $0["name"] as? String, let value = $0["value"], seen.insert(id).inserted else {
        return nil
      }
      return Parameter(id: id, value: (value as? String) ?? String(describing: value))
    }
  }
  var features: [Feature] {
    (object["features"] as? [[String: Any]] ?? []).compactMap {
      guard let id = $0["id"] as? String, let type = $0["type"] as? String else { return nil }
      return Feature(id: id, type: type)
    }
  }
  func setting(_ name: String, to expression: String) throws -> Self {
    var root = object
    guard var params = root["params"] as? [[String: Any]],
      let index = params.firstIndex(where: { $0["name"] as? String == name })
    else { throw CocoaError(.validationMissingMandatoryProperty) }
    params[index]["value"] = expression
    root["params"] = params
    return try Self(
      JSONSerialization.data(withJSONObject: root, options: [.prettyPrinted, .sortedKeys]))
  }
}
