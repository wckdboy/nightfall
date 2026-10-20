// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
import SwiftUI

enum CloseChoice { case save, discard, cancel }

@MainActor final class DocumentLifecycle: NSObject, NSApplicationDelegate, NSWindowDelegate {
  weak var document: PreviewDocument?
  var approveClose: ((PreviewDocument) -> Bool)?
  // AppKit dispatches window delegate forwarding on the main thread. ObjC's Any return
  // cannot cross assumeIsolated's Sendable boundary, so assert that contract below.
  nonisolated(unsafe) private weak var previous: (any NSWindowDelegate)?
  private func mayClose() -> Bool {
    guard let document else { return true }
    return approveClose?(document) ?? document.confirmClose()
  }
  func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
    mayClose() ? .terminateNow : .terminateCancel
  }
  func windowShouldClose(_ sender: NSWindow) -> Bool {
    mayClose() && (previous?.windowShouldClose?(sender) ?? true)
  }
  func attach(to window: NSWindow, document: PreviewDocument) {
    self.document = document
    if window.delegate !== self {
      previous = window.delegate
      window.delegate = self
    }
  }
  // Preserve SwiftUI's window bookkeeping and any other delegate messages.
  override nonisolated func responds(to selector: Selector!) -> Bool {
    if super.responds(to: selector) { return true }
    return MainActor.assumeIsolated { previous?.responds(to: selector) ?? false }
  }
  override nonisolated func forwardingTarget(for selector: Selector!) -> Any? {
    MainActor.assertIsolated()
    if let previous, previous.responds(to: selector) { return previous }
    return super.forwardingTarget(for: selector)
  }
}

struct DocumentWindowBinding: NSViewRepresentable {
  let lifecycle: DocumentLifecycle
  let document: PreviewDocument
  final class BindingView: NSView {
    var bind: ((NSWindow) -> Void)?
    override func viewDidMoveToWindow() {
      super.viewDidMoveToWindow()
      if let window { bind?(window) }
    }
  }
  func makeNSView(context: Context) -> BindingView {
    let view = BindingView()
    view.bind = { lifecycle.attach(to: $0, document: document) }
    return view
  }
  func updateNSView(_ view: BindingView, context: Context) {
    if let window = view.window { lifecycle.attach(to: window, document: document) }
  }
}
