# RFC 0008: Rust implementation of the Nightfall kernel and engine

- **Status:** Accepted
- **Author(s):** The Nightfall Authors (agent-authored proposal)
- **Phase:** 2
- **Created:** 2026-10-20
- **Issue:** [#188](https://github.com/wckdboy/nightfall/issues/188)
- **Accepted:** 2026-10-20 by the maintainer in the project conversation: “waived, lets go.”
  This accepts the concrete design in PR #189 and explicitly waives the normal review wait.

The maintainer explicitly requests Rust for the kernel and engine. That establishes the
language direction. This RFC supplies the concrete module, ownership, FFI and cutover
design required by WORKFLOW §3; the maintainer subsequently accepts it and waives the wait.
RFC 0006 remains reserved for Nightfall Script.

## Summary

Implement Nightfall's own numerical geometry, typed kernel interface, snapshot ownership,
feature evaluation, naming and bounded caching in Rust. Keep the native macOS client and
XPC host in Swift. Retain the selected dynamically linked OCCT 7.9.3 solver behind a small
C ABI adapter; native SMLib/OpenUSD integration remains isolated as before. Migrate in
tested slices while the existing C++ engine remains available for differential checks.
The completed Rust engine must build geometry headlessly through its typed Rust API and
the existing `nf_session_*` C ABI. Wrapping the entire C++ `nf_session_build` in Rust does
not constitute this migration.

## Motivation

Nightfall currently implements `core/math`, `core/kernel`, IR preparation and feature
evaluation in C++23. Rust provides checked ownership and explicit errors for the code we
own, while preserving the mature geometry solvers needed for a useful CAD application.
The maintainer's requested direction also makes a reusable Rust engine, independently
callable by AI tools, an explicit deliverable.

Rust does not repair defects inside an FFI library or prove numerical correctness. It
does not by itself resolve #185's interactive large-pattern timeout. The current engine's
827-test gate and the outstanding actual-app failure remain the baseline, rather than
being replaced with a smaller Rust demonstration.

## Design

### Contracts retained and decisions superseded

On acceptance, this RFC supersedes RFC 0004's C++ implementation/exposure choices and
RFC 0007's C++-specific interface/ownership representation as their Rust replacements
become ready. Their numerical, exactness, validation, provenance, immutability and cache
contracts remain binding. Accepted RFC text is not edited in place.

RFC 0001's OCCT selection, RFC 0002's USD route and separation of OCCT/USD translation
units, RFC 0003's Swift/XPC process isolation, and RFC 0005's schema v1 and content-hash
semantics stay in force. Units stay millimetres/radians and numerical values stay `f64`.
The five-second service watchdog, seven-second client backstop and existing resource
limits stay unchanged. This does not approve new modeling semantics or phase gates.

### Modules and build

Add a root Cargo workspace with these staged members inside the existing module layout:

- `core/math/rust`, crate `nightfall-math`: kernel-free vectors/tolerances, checked knot
  vectors, NURBS curves/surfaces, evaluation/derivatives, exact constructions and later
  the remaining RFC 0004 algorithms. Forbid unsafe code. Evaluation uses fixed-size stack
  workspaces bounded by the current degree limit of nine, without hot-path allocation.
- `core/kernel/native`, crate `nightfall-native`: the only Rust crate allowed to contain
  unsafe native calls and corresponding safe ownership wrappers. Handwritten C ABI
  declarations bind a service-private C++ adapter around native geometry operations.
  No feature JSON, parameter evaluation, selectors or document cache in that adapter.
- `core/kernel/rust`, crate `nightfall-kernel`: safe typed operations, input validation,
  backend capabilities/identity, immutable shape/entity handles, operation history,
  measurement and meshing policy. Forbid unsafe code; consume the checked native wrapper.
- `core/ir/rust`, crate `nightfall-ir`: port the current validated document, expression,
  input-preparation and canonical hashing contracts before evaluator cutover. Persisted
  documents and hash bytes are unchanged; shared fixtures check this directly.
- `services/kernel/rust`, crate `nightfall-engine`: history/dependency evaluation,
  semantic selectors, provenance rebinding, bounded cache and the headless engine API.
  The C ABI export module is its explicitly audited unsafe boundary; core evaluation
  modules forbid unsafe code. No UI, model-provider SDK or asynchronous runtime dependency.

Start with Rust **1.99.1**, edition 2024, pinned in `rust-toolchain.toml`, with `rustfmt` and
Clippy components. Local evidence is rustc 1.99.1 (`58cf8f9c3`, 2026-10-13), Cargo 1.99.1,
`aarch64-apple-darwin`. Preflight installed the previously missing rustfmt 1.10.0 and
Clippy 0.1.99 components for that existing toolchain. This is a reproducible toolchain
choice, not a claim about the latest release. Record tooling in
`third_party/README.md` and retain its upstream licenses/notices.

The first math slice uses only Rust's standard library: no runtime crates, bindgen,
cxx, async framework or automatic dependency resolution. Commit the workspace lockfile;
checks use `--locked` and run offline after tooling installation. Any later third-party
crate needs an exact version, license and explicit dependency approval under WORKFLOW §8.

CMake remains the native/XPC packaging entry point. It invokes Cargo into an isolated
build-directory target path with explicit target/profile and `MACOSX_DEPLOYMENT_TARGET=15.0`.
The initial Rust math tests run beside CTest; the production executable is switched only
at the cutover stage. Build dependencies must trigger relinking/re-embedding when Rust
artifacts or native adapters change. OCCT remains dynamically linked in the service,
with the current ad-hoc signing/dependency-closure checks; Rust does not static-link it.

### Native geometry boundary and ownership

Expose low-level native context, shape and entity ownership, exact operations, queries,
history and output-buffer access through a private C header. No C++ standard containers,
classes, references, exceptions or OCCT/USD handles cross it. Use opaque pointers,
fixed-width integers, pointer/count pairs and C-layout value records with explicit units.
Counts, alignment, layout and integer conversions are checked on both sides. Valid pointer
provenance and lifetime are preconditions for raw C callers; arbitrary forged addresses
cannot be made safe by attempting to dereference them for validation. Safe Rust wrappers
enforce these lifetimes and do not expose raw pointers to ordinary kernel callers.

The adapter catches native exceptions and returns complete results or structured error
values. Each allocation has exactly one matching release function in its allocating
module; buffers are copied or borrowed for the documented owner lifetime, never freed by
the other language's allocator. Inputs remain immutable, and failed operations publish
no partial shapes or history. Existing full closed-solid validation and exact-surface
measurement routines remain delegated native computations until an independently tested
Rust equivalent is approved; numerical shortcuts are not part of this migration.

Rust shape storage owns the native shape plus the native context needed to destroy it.
Entity storage owns its originating snapshot and backend identity. Cloning a safe shape
or entity retains that ownership; dropping a facade or evicting a cache entry cannot
invalidate retained readers. Cross-context/snapshot operations fail before native access.
Native-backed objects are initially **neither Send nor Sync**; do not add unsafe marker
implementations to bypass this. Native calls and final destruction stay on the service's
serialized engine worker. A serial GCD queue can use different OS threads, so it cannot by
itself enforce a Rust `!Send` object's affinity. At C ABI cutover, a dedicated Rust OS
thread owns all native-backed engine state for that service process. The Swift worker's
synchronous C calls send owned inputs/session IDs to that thread and wait for owned
results; C-visible session tokens contain channel/atomic progress state, not a movable
native context. Document close/drop also executes there. The watchdog still runs outside
the blocked calculation and counts queueing time. Only owned bytes/plain values cross
executors. Direct Rust callers use the safe kernel on its owning thread; mathematical
values may be shared where their own types permit it.

A process-wide native-call guard also serializes safe Rust adapter entry points, including
context creation and final release. Non-Send handles alone do not prevent independent
contexts on separate caller threads from entering a library with shared global state.
This guard preserves the current serialized-backend contract; tested internal per-request
solver parallelism remains available. Drop/release paths must not panic or unwind.

Extern-C Rust entry points catch unwinding Rust panics and translate them into failure
results, with `panic=unwind` selected explicitly. They do not attempt to catch native
exceptions in Rust. Neither language unwinds across the C ABI. Abort/OOM/native crashes
remain service-process failures handled by XPC isolation, not a claimed recoverable Rust
error. Bounds and pointer-lifetime validation happen before unsafe access.

### Engine and public consumers

Provide a typed Rust kernel/engine library usable without the client, XPC or an AI
provider. It owns model semantics rather than delegating a JSON build back to the old
C++ evaluator. The Swift host continues consuming `nf_session_*` and the existing XPC
protocol during migration; only the implementation supplying that ABI changes at cutover.
Rust API details for each operation mirror RFC 0007 and require header/rustdoc contracts
and conformance fixtures in their implementation PRs.

The native adapter may initially call existing typed C++ operation code to avoid losing
validated geometry and history. Those calls are transitional geometry implementations,
identified honestly in build receipts. Their policy/ownership code moves to Rust by
subsystem; the final remaining C++ is documented as solver-specific adapter code. A Rust
front end over an unchanged C++ evaluator is never labeled a completed Rust engine.

Selectors remain semantic; no persisted face indices or native pointer IDs. Port canonical
IR preparation/hash encoding exactly and include the Rust engine/backend configuration
in transient cache identity so mixed implementation caches cannot be reused accidentally.
No persisted cache exists. Nightfall Script/AI tool schemas remain subject to their own
RFC; changing implementation language does not approve an opaque JSON tool escape hatch.

## Alternatives considered

- Keep all Nightfall implementation in C++: conflicts with the maintainer's Rust decision.
- Rewrite OCCT/SMLib/USD into Rust first: discards working geometry coverage and makes the
  macOS client wait for replacement solid modeling and interchange implementations.
- Rust wrapper around the complete C++ evaluator: useful as a temporary experiment but
  does not deliver Rust-owned math, kernel policy, naming or feature evaluation.
- Rewrite and switch everything at once: prevents useful differential comparisons and
  risks changing exactness, selectors and files together.
- Adopt a new Rust solid-modeling library immediately: needs a separate measured bake-off
  and dependency decision; this RFC retains the current selected geometry solver.

## Compatibility and migration

Implement small, separately tracked PRs after acceptance, each within the normal size limit:

1. **Rust math foundation:** pinned workspace/tooling, checked knot and rational-curve
   evaluation/derivatives, real analytic/property tests and a C++ differential harness.
   This is the first executable deliverable; production still uses C++ at this point.
2. **Rust kernel ownership:** private native C adapter and safe context/snapshot/entity
   ownership, beginning with exact extrusion, measures and rigid transforms. Test lifetime,
   source immutability, failures and foreign handles before broadening operations.
3. **Operation parity:** port policy/history and expose the current booleans, mirror,
   revolution, chamfer/fillet, loft, sweep, queries, mesh and STEP contracts. Preserve all
   current limitations and unsupported errors; do not add approximate fallbacks.
4. **Rust IR/engine:** port validation, quantities, canonical hash inputs, selectors,
   provenance rebinding, suppression/rollback, cache ownership/eviction and replay semantics.
5. **Production cutover:** implement existing C exports with the Rust engine; rerun the
   real client/XPC/Metal suite and actual packaged-app sequences before changing defaults.
   Keep the C++ comparison implementation until parity and maintainer sign-off.

Every implementation issue belongs to the existing phase and cites this RFC. Migration
does not close Phase 2, fix #185 or declare an MVP by itself. No file-schema migration is
required for equivalent implementations. Any discovered semantic incompatibility is a
separate decision; it is not silently introduced as a language-port detail.

## Testing and measurement

- Run Cargo Debug/Release tests, formatting and Clippy with warnings denied; preserve the
  existing `tools/check.sh --preset occt` gate. Add Rust checks to that gate as code lands.
- Math tests: checked construction/nonfinite inputs, clamped/repeated knots and endpoints,
  analytic line/conic points and derivatives, partition of unity, seeded properties and
  independent finite differences. Match existing explicit tolerances and degree limits.
  Instrument hot-path allocations and compare against the existing C++ implementation.
- FFI tests: C/Rust layout and count checks, null/foreign handles and safe-handle lifetime
  checks, allocator pairing,
  retained owners after facade/cache teardown, exception/panic containment and no partial
  result publication. Run native ASan/UBSan; use Rust tooling where supported and state
  coverage limits instead of claiming uninstrumented vendor code is checked.
- Before cutover, preserve all twenty reference parts, Nocturne/Asterion exact measures,
  topology, selectors, source bytes, mesh-face coverage, STEP reload and edit/Undo replay.
  Compare measured cold/warm and native end-to-end latency with the current Release path;
  no unmeasured claim that Rust is faster. Resolve actual GUI failures before handoff.
- Preserve watchdog/crash recovery and serialized native access. Do not increase deadlines,
  reduce stress fixtures, weaken numeric tolerances or add retries to pass migration tests.

## Risks and open questions

The port temporarily duplicates implementations and increases build/FFI maintenance. Native
solvers remain outside Rust's memory-safety guarantees. Calling a fine-grained FFI API can
cost more than typed operations; measure and batch owned numeric data without hiding the
whole evaluator behind one call. Cross-language panic, allocator and thread-affinity mistakes
are release blockers. Later parser/hash/runtime dependencies require concrete proposals.
The outstanding interactive timeout must remain visible throughout the migration.

Implementation is authorized by the maintainer's explicit acceptance and review-wait waiver
recorded above. Normal per-issue scope, tests, sign-offs and PR review still apply.

References: [RFC 0004](0004-libnf-math.md), [RFC 0007](0007-kernel-evaluator-contract.md),
[Rust FFI guidance](https://doc.rust-lang.org/nomicon/ffi.html),
[panic containment](https://doc.rust-lang.org/std/panic/fn.catch_unwind.html),
[Cargo configuration](https://doc.rust-lang.org/cargo/reference/config.html).
