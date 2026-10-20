// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
import Foundation

@MainActor func sheetLifecycle() throws {
  let sheet = DocumentSheet()
  let document = UUID()
  var cancelled = 0
  let first = sheet.begin(for: document) { cancelled += 1 }!
  try check(sheet.begin(for: document) { cancelled += 100 } == nil, "duplicate presentation")
  try check(sheet.complete(first, for: document), "current completion rejected")
  try check(!sheet.complete(first, for: document), "completion executed twice")
  try check(sheet.active == nil && cancelled == 0, "successful completion invoked cancellation")

  let replaced = sheet.begin(for: document) { cancelled += 1 }!
  try check(!sheet.complete(replaced, for: UUID()), "completion applied to replacement document")
  try check(sheet.active == nil, "stale document kept presentation active")

  let old = sheet.begin(for: document) { cancelled += 1 }!
  sheet.cancel()
  try check(cancelled == 1, "owned sheet was not cancelled once")
  let next = sheet.begin(for: document) { cancelled += 1 }!
  try check(!sheet.complete(old, for: document), "late cancelled completion was accepted")
  try check(sheet.active == next, "late cancelled completion cleared newer presentation")
  try check(sheet.complete(next, for: document), "newer presentation could not complete")

  var closing: UUID?
  var replacement: UUID?
  var reentrantAccepted = false
  closing = sheet.begin(for: document) {
    cancelled += 1
    reentrantAccepted = sheet.complete(closing!, for: document)
    replacement = sheet.begin(for: document) { cancelled += 1 }
  }
  sheet.cancel()
  try check(!reentrantAccepted, "reentrant cancelled completion accepted")
  try check(replacement != nil && sheet.active == replacement, "cancel lost reentrant new request")
  sheet.cancel()
  sheet.cancel()
  try check(cancelled == 3 && sheet.active == nil, "shutdown cancellation was not idempotent")
  print("sheets: duplicate, stale, replaced-document, cancellation and reentrant completion passed")
}
