# RFC 0007: Production kernel interface and incremental evaluator

- **Status:** Accepted
- **Author(s):** The Nightfall Authors (agent-authored proposal)
- **Phase:** 2
- **Created:** 2026-10-17
- **Issue:** [#92](https://github.com/wckdboy/nightfall/issues/92)
- **Accepted:** 2026-10-17 by the maintainer in the project conversation; the instruction
  “force it, and keep working” authorizes implementation and waives the RFC review wait.

Number 0006 is reserved by RFC 0005 for Nightfall Script (#93). This proposal supplements
RFCs 0001, 0003 and 0005; it does not change their accepted text. Implementation of the new
`IKernel` interface proceeds under this accepted contract.

## Summary

Introduce a kernel-neutral C++23 `IKernel` in `core/kernel`, implemented first by the pinned,
dynamically linked OCCT 7.9.3 backend. The evaluator in `services/kernel` owns feature order,
selectors, body state and cache decisions. Backends own geometry and operation history.
Opaque, immutable snapshots survive cache eviction while active consumers use them. The
first executable slice builds sketches, finite-distance extrusions and rigid transforms;
unsupported operations report feature errors until implemented. The existing Phase 2 gate
remains open until real IR documents run through the production evaluator.

## Motivation

RFC 0001 explicitly reserves production `IKernel` for a later RFC. The current bake-off
interface is temporary: integer handles live in an ever-growing vector, failures throw,
selection uses bounding boxes, and it lacks the naming/ownership contract a document needs.
Production `core/kernel` currently supplies NURBS conversions only. Reusing the bake-off
driver directly would make cache eviction and semantic selectors depend on unspecified
behavior. RFC 0005 supplies feature hashes, but a geometry cache must also preserve provenance.

## Design

### Ownership and module boundaries

- `core/ir` continues to own document validation, quantities, input preparation and hashes.
  It has no kernel dependency. Its normalized inputs are ephemeral; authored IR stays intact.
- `core/kernel/include/nf/kernel/kernel.hpp` defines `IKernel`, typed operation inputs,
  opaque `Shape`/`Entity` handles, measurements, history and structured backend failures.
  Public headers use standard C++ and `nf::math` values, never OCCT or OpenUSD headers.
- The OCCT implementation lives behind those headers. A shape owns the backend state it
  needs; destruction is safe after the producing request and `IKernel` facade have ended.
  Entity handles keep their owning snapshot alive and are checked for backend/snapshot
  compatibility. They are transient handles, never face indices in the IR or protocol.
- Operations do not mutate input geometry. The backend copies where OCCT could alter
  topology, tolerances or cached geometry. Cache and backend access are serialized by the
  service's worker queue initially; immutable handles alone do not make OCCT thread-safe.
- Only the service and headless executables link the backend. The app uses RFC 0003's C
  bridge/XPC boundary. The bridge catches exceptions and translates errors; no C++ object
  or exception crosses it. Production XPC packaging/watchdog work remains separately tested.

### Backend contract

`IKernel` exposes a version/configuration identity, capabilities, typed modeling operations,
geometric queries, measurement/validation and resource estimates. Modeling calls return
`expected<OperationResult, KernelError>`. `OperationResult` contains immutable shape outputs,
operation-local generated roles, input-to-output modified/generated/deleted history, and an
estimated retained size. It cannot contain a partially successful operation.

The first calls accept evaluated planar regions (libnf-math curves plus a local plane), an
extrusion vector/options, and rigid transformations. Later calls implement the remaining v1
operations through typed inputs under this boundary. Backends do not interpret JSON,
parameter expressions, feature order or user-facing selector trees. Unsupported capabilities
return a structured `kernel` error; there is no mesh approximation or alternate operation.

Every successful operation passes backend validity checks before admission to the cache.
A v1 solid output consists of nonempty, closed, valid solids, with finite measurements and
no stray faces/wires. Cavities and multiple solid components are allowed; the bake-off's
temporary single-shell restriction does not become a production restriction. Empty boolean
results are feature errors until an explicit empty-body semantic is specified. Geometry
tolerances/configuration are explicit backend inputs and part of evaluator identity.

### Feature evaluation and dependency state

1. Validate the document and resolve parameters with libnf-ir. Structural or parameter errors
   return before kernel work; individual feature expression/geometry errors are collected.
2. Walk history in order. Suppressed features and everything after `history_end` are skipped
   without executing expressions, references or geometry. The rollback marker is inclusive.
3. Prepare numeric inputs, resolve referenced sketches/bodies/resources and evaluate selectors.
   Use current body state, including modifications after the feature named by `created_by`.
   A selector's candidate bodies are dependencies even when a geometric filter removes them.
4. Form the RFC 0005 feature hash using evaluated inputs and dependency hashes in stable
   JSON-Pointer order (then reference kind/ID to disambiguate multiple bindings at a pointer).
   Body dependencies use their current state hash; sketch/feature dependencies use feature
   hashes. External dependencies use verified content hashes, never path or timestamp alone.
5. Reuse a compatible cached output, or execute and validate the backend operation. Publish
   all body changes and naming changes atomically. Advance each modified body's state hash
   from its previous state and this feature hash. New bodies have no previous state.

Failed producers make their outputs unavailable. A failed modifier marks its intended body
outputs failed, so consumers cannot silently fall back to an older shape. The last good
geometry may be retained for display, explicitly separate from executable state. Independent
branches continue. Suppressing a modifier leaves its input body unchanged, while directly
consuming the suppressed feature reports `input_suppressed`. Consumed boolean tools are
retired unless `keep_tools` is true. Errors retain RFC 0005 codes and document JSON Pointers.

The service resolves external resources through an injected reader and verifies the supplied
SHA-256 before using either imported geometry or a cache hit. File access belongs to the
service/CLI boundary; `core/ir` and the modeling backend never open arbitrary IR paths.

### Selectors, provenance and cache reuse

The evaluator binds operation-local generated roles to the current feature and sketch entity
IDs. It propagates modified/generated/deleted history into each live body's naming graph.
Selector trees resolve through this graph and backend geometric queries, checking `expect`
exactly. Ambiguity is `selector_count`, not permission to choose a topology index.

OCCT's incomplete fillet-edge history remains explicit (RFC 0001): implement and test a
semantic tagging fallback, or return an error where provenance is insufficient. A geometric
match without the required provenance is not silently substituted.

Cache entries must retain geometry **and** enough history to reconstruct the naming graph.
Feature hashes omit the current feature ID, so cached provenance cannot be permanently bound
to the original producer's ID. Store local roles and dependency bindings separately and bind
them to the current evaluation on reuse. If an entry cannot safely rebind its history to the
current input snapshots, treat it as a miss. This may cost work after eviction or ID edits;
it must never return geometry with incorrect semantic names.

Use the service's `GeometryCache` LRU with explicit entry and estimated-byte budgets. Keep
per-document ownership initially and expose cache statistics to headless tests. Eviction
releases cache ownership only; in-flight readers may retain outputs. Estimates include
retained input/history data, conservatively charging shared data when exact attribution is
unavailable. These limits are not a bound on process RSS. Default budgets and total-service
admission limits must be chosen from measured parts before enabling many concurrent documents.

The version string in every key covers evaluator semantics, kernel version, geometry
tolerances, backend options and naming algorithm. No cache is persisted by this proposal.

### First executable slice

Implement datum-plane sketches using libnf-math regions, finite-distance extrusion into a
new B-rep body, and rigid transforms. Exercise rectangles, circles and closed line/polygon
profiles first. Unimplemented entity types, constraints, operation modes and selectors return
explicit feature errors; schema validity never implies implementation support. Implement
constraint verification before claiming constrained sketches work, as RFC 0005 requires.
Equation curves need parameter binding before hashes are safe; script hosting remains #93.
Broaden operation coverage in subsequent small #92 branches using the same contract.

## Alternatives considered

- **Promote the bake-off interface unchanged:** its temporary handle lifetime, selection and
  error contracts cannot support bounded caching and semantic feature references.
- **Put OCCT calls in the feature evaluator:** couples IR semantics to one backend and breaks
  the accepted kernel-header boundary.
- **Cache only shapes:** loses naming history, allowing a visually correct solid to select
  the wrong entity after an edit or a cache hit.
- **Add producer IDs to the feature hash:** changes RFC 0005 identity instead of fixing
  provenance binding. Keep content identity and evaluation bindings separate.

## Compatibility and migration

No persisted-format, schema-version or dependency change. Existing documents and bake-off
drivers stay readable/buildable. The new interface is initially service-private in use;
Swift still consumes the plain-C bridge. New capability errors do not reinterpret v1 inputs.

## Testing and measurement

- Analytic volume/area/topology checks and validity after every operation, with explicit
  tolerances. Verify source geometry is unchanged after success, failure and cache reuse.
- Dependency tests for modified bodies, selectors, multiple bodies, consumed tools,
  suppression, rollback and independent errors. Reverting an edit must recover prior results.
- Cache tests for limits, eviction, retained readers, version changes and producer-ID rebinding.
  In particular, recreate an evicted upstream shape while retaining a downstream cache entry.
- An actual 30-feature `.nightfall` part through the production evaluator: assert exactly
  which features execute after a parameter edit, compare rebuilt geometry to a cold build,
  and report median/p95/maximum time, hardware/configuration and cache-hit counts. Include IR
  decoding, validation, parameter/input preparation and backend validation in the timing;
  report IPC and tessellation separately. Hash-only timings do not satisfy the Phase 2 gate.
- ASan/UBSan on ownership/history paths, plus RFC 0003 crash/hang/replay tests before app use.
  The complete Phase 2 gate still requires #95's 20 reference parts and maintainer sign-off.

## Risks and open questions

- OCCT history gaps may limit which selector combinations are initially supported. Record
  exact failures in reference parts; do not weaken selector cardinality to pass them.
- Rebinding cached provenance after eviction must be proved with adversarial histories before
  broad cache reuse. Conservative misses are acceptable while that proof is incomplete.
- Retained snapshots and kernel-internal caches can exceed estimated resident bytes. Measure
  RSS separately and choose service admission/eviction policies from real parts.
- The first typed operation signatures should be reviewed against libnf-math region output
  and OCCT history requirements before acceptance; this proposal does not approve an opaque
  JSON escape hatch or expand the accepted operation semantics.
