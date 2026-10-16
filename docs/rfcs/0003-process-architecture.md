# RFC 0003: Process architecture: the kernel in an XPC service

- **Status:** Review (opened 2026-10-17; earliest decision 2026-10-22 per WORKFLOW §3)
- **Author(s):** wckdboy
- **Phase:** 0
- **Created:** 2026-10-17
- **Decided:** —

## Summary

The geometry kernel runs in `NightfallKernel.xpc`, an XPC service embedded in the app
bundle. The app sends the versioned feature IR and gets back meshes, measurements and
structured errors. Kernel code sits behind a plain-C bridge, so no kernel header reaches
Swift, and a kernel crash or hang costs one rebuild, never the user's document.

## Motivation

- **Kernels crash and hang.** RFC 0001 F3 shows a use-after-free in SMLib's booleans, and
  OCCT is known to loop on bad fillets. In-process, either takes the user's unsaved work with
  it.
- **The build plan already assumes it** ("Engine boundary": XPC, restart, rebuild from the
  last good feature).
- **The #8/#9 spike proved it works** with today's toolchain (Swift 6 complete concurrency,
  xcodegen, OCCT), and the costs are measured below.

## Design

### Processes

```
Nightfall.app (SwiftUI/AppKit, Metal)          NightfallKernel.xpc (one per app instance)
  document, IR, UI, viewport      ──IR (JSON)──▶  decode IR → kernel bridge (C) → kernel (C++)
                                  ◀─mesh, measures, errors──  tessellate, measure
```

- **One service process per app instance** (`XPCService.ServiceType = Application`, launchd
  manages its lifetime). Documents share it; every request names its document, so caches
  can be per-document.
- **The app is stateless toward the kernel:** every request carries the IR (or the IR
  delta plus the base hash, once incremental rebuild exists). After a crash, the app replays
  the IR and needs nothing else.

### Interfaces

| Boundary | Choice | Why |
| --- | --- | --- |
| App ↔ service | `NSXPCConnection` with an `@objc` protocol; `@Sendable` replies | Typed, sandbox-friendly, works under Swift 6 strict concurrency (spike) |
| Payload | Versioned IR as JSON (`version` field), until the IR RFC picks the format | Debuggable, schema-checkable; size is small next to meshes |
| Service ↔ kernel | Plain C API (`kernel_bridge.h`), opaque handles | Kernel headers (OCCT's `Handle` macro, SMLib's macros) never reach Swift; swapping kernels doesn't touch the service; one ABI to test |
| Kernel ↔ kernel | `IKernel` (C++), per RFC 0001's decision | Out of scope here |

### Meshes

The spike returns flat-shaded, non-indexed triangles as one `Data` blob: 72 bytes per
triangle, so 2,112 triangles are 152 KB, copied once by XPC. Before Phase 4 that changes to:

- **indexed** meshes with per-face ranges, so picking can map triangles to face IDs, and
- **shared memory** (`IOSurface` or `xpc_shmem`) for meshes above a threshold (tentatively
  4 MB), avoiding a copy of every large rebuild.

### Failure policy

| Event | Detection | Response |
| --- | --- | --- |
| Service crash | `interruptionHandler` | Mark the in-flight request failed, show "kernel restarted", replay the IR on the next request (spike: relaunch + rebuild in 55.5 ms) |
| Hang | Per-request deadline in the app (default 5 s, per the build plan) | Invalidate the connection (launchd tears down the service), report the feature as the culprit, retry once without it |
| Kernel error | Structured error from the bridge (feature index, code, message) | Show it on the feature; the agent receives the same structure |

### Packaging

- Kernel dylibs (OCCT/SMLib and later USD) are embedded in the **service** bundle's
  `Frameworks` folder, not the app's, so only the service links them. The spike's absolute
  rpath is spike-only.
- Development builds are ad-hoc signed. Distribution signing, notarization and the LGPL
  relink obligations for OCCT are a release-engineering RFC.

## Alternatives considered

- **In-process kernel:** fastest, but one SMLib use-after-free or OCCT hang loses the user's
  work. Rejected.
- **Helper launched by the app over a raw socket or pipe:** more code than XPC, worse
  sandbox story, no launchd lifecycle.
- **Swift-C++ interop instead of a C bridge:** less boilerplate, but kernel headers would
  leak into Swift compilation (the spike hit OCCT's `Handle` macro breaking OpenUSD headers;
  Swift's Clang importer would face the same). Revisit once the kernel facade is our own
  clean C++ (`IKernel`).

## Compatibility and migration

Nothing shipped. The spike branch is the reference implementation and is not merged; the
production `app/` and `services/` code lands in Phase 2/4 under this RFC.

## Testing and measurement

Spike (`spike/8-9-xpc-viewport`), Debug build, M-series Mac:

| Measure | Result |
| --- | --- |
| Box 40×30×12, all edges filleted r=3, r=5 hole: IR → service → OCCT → mesh → app | 98.2 ms round trip, first request (includes service launch and meshing) |
| Kill service mid-session (`--crash-test`) | Interrupted once; relaunched (pid 80532 → 80536); rebuild 55.5 ms |
| Correctness | Mesh volume source = exact B-rep: 12,860.177 mm³ = 11,880 + 312π |
| Toolchain | Swift 6 `SWIFT_STRICT_CONCURRENCY=complete`; xcodegen 2.46; builds in 8 s |

Production will add headless tests built on the spike's `--snapshot` / `--crash-test` modes,
plus a hang test once the deadline exists.

## Risks and open questions

- XPC message size limits and copy cost for large meshes. Shared memory is planned above.
- Whether per-document caches in a single service process grow too large. Fallback: one
  service per document via `NSXPCConnection` to an app-launched helper.
- Debuggability across processes. The service logs with `os_log` under a shared subsystem.
