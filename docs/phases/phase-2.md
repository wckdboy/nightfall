# Phase 2 — Feature IR + Script

- **Weeks:** 8–16 (build plan)
- **Milestone:** `Phase 2` on GitHub
- **Status:** In progress

## Gate

All of these, with evidence linked:

- [ ] 20 reference parts (gear, bracket, enclosure, …) build headlessly in CI
- [x] Changing one parameter rebuilds a 30-feature part in < 100 ms ([local evidence](#rebuild-benchmark-evidence))
- [ ] IR documents validate against schema v1 and migrate forward; ASan/UBSan clean

## Work items (build order)

Tracked as GitHub issues in the [`Phase 2` milestone](https://github.com/wckdboy/nightfall/milestone/3).

| Issue | Item | Scope | Status |
| --- | --- | --- | --- |
| [#89](https://github.com/wckdboy/nightfall/issues/89) | 2.0 RFC 0005: feature IR schema v1 | ir | Done (RFC 0005) |
| [#90](https://github.com/wckdboy/nightfall/issues/90) | 2.1 IR schema v1, validation and migrations | ir | Schema, document I/O and reference validation implemented; review pending |
| [#91](https://github.com/wckdboy/nightfall/issues/91) | 2.2 Expression engine with units, bounds and dependency graph | ir | Expressions, dependency graph and bounds implemented; review pending |
| [#92](https://github.com/wckdboy/nightfall/issues/92) | 2.3 Evaluator: feature hashing, incremental rebuild, cache | kernel | Sketch/extrude/transform/boolean evaluator and rebuild benchmark implemented; remaining v1 operations/selectors and review pending |
| [#93](https://github.com/wckdboy/nightfall/issues/93) | 2.4 Nightfall Script: JavaScriptCore host and nf API bindings | script |  |
| [#94](https://github.com/wckdboy/nightfall/issues/94) | 2.5 CLI: `nf build part.nightfall --out part.step` | cli |  |
| [#95](https://github.com/wckdboy/nightfall/issues/95) | 2.6 20 reference parts and the Phase 2 gate | evals |  |
| [#74](https://github.com/wckdboy/nightfall/issues/74) | Coincident edges in planar arrangements (from Phase 1) | math |  |

## Log

- 2026-10-17 — Phase started after the Phase 1 gate. The IR schema RFC (0005) comes first;
  expressions build on libnf-math's expression language (#80), the evaluator on the OCCT kernel
  layer (#55) inside the kernel service (RFC 0003).
- 2026-10-17 — RFC 0005 accepted (review period waived by the maintainer): one JSON document,
  sketches inside the feature history, selectors instead of indices, content hashes of
  evaluated inputs, a new kernel-free module `core/ir`.
- 2026-10-17 — #90 document envelope: typed parameters, references and feature history;
  version checks, unique IDs, rollback validation, bounded load/save and preserved extensions.
  This is structural validation only. The normative JSON Schema, per-feature inputs,
  selectors, body/entity references and schema corpus remain in #90; expressions, units
  and parameter dependency evaluation remain in #91. No Phase 2 gate is claimed yet.
- 2026-10-17 — #90 publishes the JSON Schema 2020-12 definitions for all 16 feature types,
  sketch entities, constraints and semantic selectors. A shared valid/invalid corpus is
  independently checked with pinned Python test tooling in CI; native enforcement follows.
- 2026-10-17 — #90 native schema enforcement: load/save uses the embedded published schema,
  including recursive selectors and mutually exclusive operation forms. Native and Python
  checks share the corpus; reference annotations are collected for the semantic checker.
- 2026-10-17 — #90 semantic checks enforce body/entity namespaces and typed backward references
  across sketches, selectors, operations and external inputs, including rolled-back history.
  The shared corpus has 184 cases. v1 is the first format: older/newer versions are refused;
  there are no legacy files to migrate. Unit/expression evaluation remains #91, geometry #92.
  `nf_ir_bench` measures complete document load/save for 30- and 300-feature histories.
  Local Release medians (5 × 100, warm schema): 30 features load in 0.235 ms and edit/save
  in 0.370 ms; 300 features in 3.015 ms and 3.690 ms. These do not measure geometry rebuilds.
  `tools/check.sh`: 170/170 passed. ASan/UBSan: 169/170, no sanitizer diagnostics; the
  unchanged closest-point timing test exceeded 2 s (4.805 s), then passed alone in 1.87 s.
- 2026-10-17 — #91 reuses the math expression grammar for parameter names and numeric unit
  suffixes, retaining source locations for diagnostics. Both parser modes bound source size,
  node count and recursion; equation curves retain their numeric semantics.
- 2026-10-17 — #91 dimensional expressions normalize lengths to mm and angles to radians,
  check arithmetic/functions and reject non-finite results. Bare JSON field numbers use
  document units; parameter numbers and bare numbers inside expressions are dimensionless.
  Compiled expressions are reusable. Declared parameter names take precedence over pi/e;
  function syntax and numeric unit suffixes remain unambiguous.
- 2026-10-17 — #91 resolves parameter values in dependency order and checks inclusive bounds
  afterward. Load/save rejects cycles, unknown names, unit mismatches and non-finite results,
  while preserving authored expressions. Iterative traversal handles a 10,000-parameter chain;
  source/instruction/diagnostic limits bound generated inputs. Feature evaluation remains #92.
  Development checks pass 189/189 tests; the shared corpus now has 202 cases, and all five
  Swift integration tests pass. Release medians (5 × 100): edit/compile/evaluate/bounds for
  30 chained parameters takes 20.1 µs, for 300 takes 201.6 µs. No geometry is evaluated.
  Full ASan/UBSan checks also pass 189/189 tests (160.11 s), with no sanitizer diagnostics.
- 2026-10-17 — #92 adds kernel-free SHA-256 identities for evaluated features and chained
  body states, using RFC 8785 canonical JSON and CommonCrypto. Known-answer tests cover
  SHA-256, canonical envelopes, external digest normalization and upstream ordering.
  Geometry execution, incremental rebuild timings and the Phase 2 gate remain pending.
- 2026-10-17 — #92 compiles feature inputs once using dimension annotations in the existing
  schema (no format or validation-rule change). Evaluations normalize lengths, angles and
  scalars, check integer counts/degrees, preserve field diagnostics and exclude opaque
  extensions from executable inputs. Equivalent evaluated values share hashes; unused
  parameters do not invalidate them. Script and equation-curve binding remain explicit
  unsupported-evaluation errors. The benchmark now measures input/hash preparation separately
  from geometry. Local checks: 203/204 passed; the unchanged closest-point timing assertion
  exceeded 2 s under concurrent external compiler load (4.87 s). All new tests passed.
- 2026-10-17 — #92 adds the service's LRU cache with explicit entry/estimated-byte limits,
  safe replacement/eviction and retained reader ownership. A 2,000-operation reference-model
  test checks ordering/accounting; OCCT integration verifies a translated cached solid retains
  volume 6,000 mm³, area 2,200 mm², six faces and twelve edges, without changing its source.
  The rerun passes 210/210 development tests (68.68 s); OCCT checks pass 226/226 (12.78 s).
  Full ASan/UBSan checks pass 210/210 (297.64 s), with no sanitizer diagnostics.
  The cache is a tested service component; feature execution and provenance integration remain
  pending, and no production geometry-rebuild gate is claimed.
  Release medians (5 × 100, prepared feature programs): parameter edit, input evaluation and
  chained hashing take 86.8 µs for 30 features and 1,425.0 µs for 300. These exclude geometry
  execution. Full document load/edit-save takes 282.3/344.8 µs for 30 features and
  2,895.9/4,137.0 µs for 300; chained parameter evaluation takes 18.8/334.1 µs respectively.
- 2026-10-17 — Draft [RFC 0007](../rfcs/0007-kernel-evaluator-contract.md) supplies the
  production `IKernel` contract explicitly deferred by RFC 0001: immutable ownership, typed
  operations, current-body dependencies, safe cached-provenance binding and a staged
  executable evaluator. This is a proposal; acceptance is required before that interface
  is implemented. RFC 0006 remains reserved for Nightfall Script (#93).
- 2026-10-17 — RFC 0007 accepted by the maintainer in the project conversation, explicitly
  authorizing implementation and waiving the review wait. The geometry evaluator proceeds
  in small #92 branches; this does not waive implementation checks or sign off the phase gate.
- 2026-10-17 — #92 introduces kernel-neutral immutable shape/entity handles and OCCT solid
  snapshot validation. Analytic tests cover a 6,000 mm³ box, two solids totaling 12,000 mm³,
  and a 1,968 mm³ enclosed cavity; empty/stray geometry is rejected. Entity ownership survives
  the request. OCCT checks pass 230/230 (17.48 s); production modeling operations follow.
- 2026-10-17 — #92 adds typed production extrusion and rigid transforms, including circular
  holes, multiple solids, placed planes and immutable face/edge history. Exact knot-span
  decomposition and span-aware volume integration fix measurements of rational profiles.
  Annulus plus island: volume 553π mm³ and area 396π mm², both within 1e-5 after rotation.
  OCCT checks pass 236/236 (12.43 s); service-side IR execution remains the next slice.
- 2026-10-17 — #92 service adapters construct exact rectangle/circle/polygon/closed-line
  profiles from normalized inputs, select regions by interior points, place datum/explicit
  planes and prepare symmetric/directed extrusions and rigid transforms. Open/ambiguous
  chains and unsupported constraints/selectors/modes return field diagnostics. OCCT checks
  pass 240/240 (15.86 s); incremental document execution follows these tested adapters.
- 2026-10-17 — #92 executes complete IR documents through the production backend with
  current-body hashes, bounded geometry caching, rollback/suppression, atomic multi-body
  publication and explicit downstream failure states. Operation-local names rebind on cache
  reuse; retained history is rejected when an evicted upstream snapshot has been rebuilt.
  Tests exercise 30-feature histories and verify unused/equivalent-unit edits do no kernel
  work. OCCT checks pass 252/252 (14.51 s); measured rebuild latency follows separately.
- 2026-10-17 — #92 measures real geometry and optimizes rigid transforms: shared immutable
  B-reps, checked location/orientation/isometry invariants, preserved volume/area and translated
  bounds. New geometry still receives full OCCT validation; rotations recompute bounds.
  Tests independently remeasure moved B-reps and their unchanged sources. The benchmark below
  meets the local 30-feature latency criterion; the full Phase 2 gate remains open.
- 2026-10-17 — #92 adds exact solid union, subtraction and intersection, with tools treated
  as a union. OCCT runs in non-destructive mode and validates the complete result. History
  covers every input face/edge, binds cutting faces with their output orientation and records
  new intersection edges. Tests check unchanged source B-rep serialization, circular holes,
  cavities, split solids, disjoint and empty results. The evaluator hashes current target/tool
  states, publishes naming before consuming tools, supports `keep_tools`, and rejects stale
  histories after eviction. Fourteen new regression cases cover geometry, tool lifecycle,
  failures, cache rebinding, rollback and suppression; remaining operations/selectors are pending.

## Rebuild benchmark evidence

2026-10-17, Apple M3 Pro (12 cores, 36 GiB), Release, dynamically linked OCCT 7.9.3.
Run `build/occt/services/kernel/nf_evaluator_bench` after building the `occt` preset.
Each part has one sketch, one extrusion and 28 transforms. The bored plate has two circular
holes represented by exact rational NURBS. Each row has 30 samples with unique parameter
values; every sample checks analytic volume/centroid and the expected kernel/cache counts.
Timers include JSON parsing, validation, expressions, hashing, geometry, validity checks and
name propagation. Serialization, IPC and tessellation are excluded.

| Part | Rebuild | Median ms | p95 ms | Max ms | Kernel calls / cache hits |
| --- | --- | ---: | ---: | ---: | ---: |
| Plate | Cold | 3.073 | 4.888 | 5.980 | 29 / 0 |
| Plate | Depth edit | 3.081 | 6.497 | 9.765 | 29 / 1 |
| Plate | Last transform edit | 0.698 | 1.148 | 1.603 | 1 / 29 |
| Plate | Unused parameter edit | 0.684 | 1.452 | 1.550 | 0 / 30 |
| Bored plate | Cold | 15.588 | 19.352 | 20.570 | 29 / 0 |
| Bored plate | Depth edit | 14.263 | 22.587 | 28.131 | 29 / 1 |
| Bored plate | Last transform edit | 1.099 | 3.220 | 3.876 | 1 / 29 |
| Bored plate | Unused parameter edit | 1.169 | 2.326 | 2.402 | 0 / 30 |

Before rigid-invariant reuse, depth edits measured 114.290/382.885 ms median for the two
parts. The benchmark cache has an explicit 96-entry/64 MiB estimated budget; after 30 depth
edits it retains 96 entries, estimated at 10.25/26.24 MiB respectively. This is not a process
RSS guarantee or a service-wide default. These are local measurements for the implemented
feature subset; 20 reference parts in CI and full phase sign-off remain pending (#95).

Final local verification: `tools/check.sh --preset occt` passes 252/252 (15.90 s), including
the 202-case schema cross-check and hygiene checks. After the optimization, the exact CI
ASan/UBSan kernel/evaluator/cache selection passes 38/38 (15.02 s). The earlier full sanitizer
run passed 251/252 (228.96 s), with no sanitizer diagnostics; the unchanged closest-point
2-second timing assertion took 5.136 s, then passed alone in 1.94 s. OCCT itself uses the
pinned release build; Nightfall code is instrumented. CI now runs the geometry sanitizer
selection on OCCT pull requests/nightly builds. Remote CI and maintainer review remain pending.

### Solid boolean rebuild evidence

The same machine/configuration and 30-sample methodology now also measure a 30-feature part
with two sketches, two extrusions, one circular through-cut and 25 transforms. The cutter
extends beyond the plate throughout the depth range. Every sample checks the exact volume
`(200 - 4π) × depth` mm³, centroid and kernel/cache counts. The tool is consumed by the cut.

| Boolean-cut plate | Median ms | p95 ms | Max ms | Kernel calls / cache hits |
| --- | ---: | ---: | ---: | ---: |
| Cold | 53.098 | 56.046 | 56.488 | 28 / 0 |
| Depth edit | 49.975 | 53.884 | 57.060 | 27 / 3 |
| Last transform edit | 0.692 | 0.714 | 0.721 | 1 / 29 |
| Unused parameter edit | 0.657 | 0.691 | 0.702 | 0 / 30 |

The 96-entry/64 MiB estimated cache retains 18,700,280 estimated bytes after the depth edits;
this remains an ownership estimate, not RSS. The same run measured plain/bored depth-edit
medians of 2.081/9.556 ms. Timing variation across local runs is not itself an optimization claim.
`tools/check.sh --preset occt` passes 266/266 tests (13.26 s), with all hygiene checks and the
202-case independent schema cross-check passing.
The full `tools/check.sh --preset asan` run also passes 266/266 (188.30 s), including all
new geometry/evaluator tests, with no sanitizer diagnostics or retries. Nightfall code is
ASan/UBSan-instrumented; the OCCT shared libraries remain the pinned Release build. Remote
CI and maintainer review are pending; this extends the local latency evidence only.
