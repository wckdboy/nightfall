// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
import Foundation

struct PartProgram {
  struct Parameter: Identifiable { let id: String, value: String }
  struct Feature: Identifiable {
    let id: String, type: String
    let suppressed: Bool, rolledBack: Bool
  }
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
  var historyEnd: String? { object["history_end"] as? String }
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
    let source = object["features"] as? [[String: Any]] ?? []
    let end = historyEnd.flatMap { id in source.firstIndex { $0["id"] as? String == id } }
    return source.enumerated().compactMap { index, feature in
      guard let id = feature["id"] as? String, let type = feature["type"] as? String else {
        return nil
      }
      return Feature(
        id: id, type: type, suppressed: feature["suppressed"] as? Bool ?? false,
        rolledBack: end.map { index > $0 } ?? false)
    }
  }
  func setting(_ name: String, to expression: String) throws -> Self {
    try setting([name: expression])
  }
  func setting(_ expressions: [String: String]) throws -> Self {
    guard !expressions.isEmpty else { return self }
    guard var params = object["params"] as? [[String: Any]] else {
      throw CocoaError(.validationMissingMandatoryProperty)
    }
    for (name, expression) in expressions {
      guard let index = params.firstIndex(where: { $0["name"] as? String == name }) else {
        throw CocoaError(.validationMissingMandatoryProperty)
      }
      params[index]["value"] = expression
    }
    return try replacing("params", with: params)
  }
  func suppressing(_ id: String, _ suppressed: Bool) throws -> Self {
    guard var features = object["features"] as? [[String: Any]],
      let index = features.firstIndex(where: { $0["id"] as? String == id })
    else { throw CocoaError(.validationMissingMandatoryProperty) }
    features[index]["suppressed"] = suppressed
    return try replacing("features", with: features)
  }
  func endingHistory(at id: String?) throws -> Self {
    guard id == nil || features.contains(where: { $0.id == id }) else {
      throw CocoaError(.validationMissingMandatoryProperty)
    }
    if let id { return try replacing("history_end", with: id) }
    return try replacing("history_end", with: NSNull())
  }
  private func replacing(_ key: String, with value: Any) throws -> Self {
    var root = object
    root[key] = value
    return try Self(
      JSONSerialization.data(withJSONObject: root, options: [.prettyPrinted, .sortedKeys]))
  }
}
