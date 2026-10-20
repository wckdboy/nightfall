// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
import SwiftUI

@main @MainActor struct NightfallApp: App {
  @State private var document = PreviewDocument()
  @NSApplicationDelegateAdaptor(DocumentLifecycle.self) private var lifecycle
  var body: some Scene {
    Window("Nightfall", id: "nightfall") {
      PreviewView(model: document)
        .background(DocumentWindowBinding(lifecycle: lifecycle, document: document))
    }
    .defaultSize(width: 1380, height: 860)
  }
}
