// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// SPIKE (#8, #9): app entry. Headless modes for evidence and CI:
//   --snapshot <file.png>   build the default part in the kernel service, render it offscreen
//   --crash-test            build, crash the kernel service, build again, report
// Both print one result line and exit non-zero on failure.

import AppKit
import SwiftUI

@main
struct NightfallApp: App {
  @NSApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate

  var body: some Scene {
    WindowGroup("Nightfall (spike)") { ContentView() }
  }
}

@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate {
  func applicationDidFinishLaunching(_ notification: Notification) {
    let args = CommandLine.arguments
    if let i = args.firstIndex(of: "--snapshot"), i + 1 < args.count {
      Task { exit(await Headless.snapshot(to: URL(fileURLWithPath: args[i + 1]))) }
    } else if args.contains("--crash-test") {
      Task { exit(await Headless.crashTest()) }
    }
  }
}

@MainActor
enum Headless {
  static let part = PartIR(features: [
    .init(type: "box", size: [40, 30, 12]),
    .init(type: "fillet_all", radius: 3),
    .init(type: "hole", radius: 5, at: [20, 15]),
  ])

  static func snapshot(to url: URL) async -> Int32 {
    let client = KernelClient()
    guard let result = await client.build(part) else {
      print("snapshot: FAILED \(client.status)")
      return 1
    }
    guard let device = MTLCreateSystemDefaultDevice(), let renderer = Renderer(device: device) else {
      print("snapshot: FAILED no Metal device")
      return 1
    }
    renderer.setMesh(result.mesh)
    let ok = renderer.writePNG(to: url, width: 1200, height: 800)
    print("snapshot: \(ok ? "OK" : "FAILED") \(client.status) -> \(url.path)")
    return ok ? 0 : 1
  }

  static func crashTest() async -> Int32 {
    let client = KernelClient()
    guard await client.build(part) != nil else { print("crash-test: FAILED first build"); return 1 }
    let pidBefore = kernelPID()
    client.crashKernel()
    try? await Task.sleep(for: .milliseconds(500))
    guard let again = await client.build(part) else {
      print("crash-test: FAILED rebuild after crash: \(client.status)")
      return 1
    }
    let pidAfter = kernelPID()
    print("crash-test: OK interruptions=\(client.interruptions) kernel pid \(pidBefore) -> \(pidAfter); "
      + String(format: "rebuild round trip %.1f ms", again.milliseconds))
    return client.interruptions >= 1 ? 0 : 1
  }

  /// PID of the running kernel service, for the report (0 if not found).
  static func kernelPID() -> Int32 {
    let pipe = Pipe()
    let p = Process()
    p.executableURL = URL(fileURLWithPath: "/usr/bin/pgrep")
    p.arguments = ["-n", "NightfallKernel"]
    p.standardOutput = pipe
    try? p.run()
    p.waitUntilExit()
    let s = String(data: pipe.fileHandleForReading.readDataToEndOfFile(), encoding: .utf8) ?? ""
    return Int32(s.trimmingCharacters(in: .whitespacesAndNewlines)) ?? 0
  }
}
