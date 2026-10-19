// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
import Foundation
import Observation

@MainActor @Observable final class DocumentSheet {
  private(set) var active: UUID?
  @ObservationIgnored private var document: UUID?
  @ObservationIgnored private var cancelAction: (() -> Void)?
  func begin(for document: UUID, cancel: @escaping () -> Void) -> UUID? {
    guard active == nil else { return nil }
    let id = UUID()
    self.document = document
    cancelAction = cancel
    active = id
    return id
  }
  func complete(_ id: UUID, for document: UUID) -> Bool {
    guard active == id else { return false }
    let current = self.document == document
    clear()
    return current
  }
  func cancel() {
    let action = cancelAction
    clear()
    action?()
  }
  private func clear() {
    active = nil
    document = nil
    cancelAction = nil
  }
}
