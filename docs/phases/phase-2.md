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
| [#90](https://github.com/wckdboy/nightfall/issues/90) | 2.1 IR schema v1, validation and migrations | ir | Schema, document I/O and reference validation merged in #103; v1 is the first format |
| [#91](https://github.com/wckdboy/nightfall/issues/91) | 2.2 Expression engine with units, bounds and dependency graph | ir | Expressions, dependency graph and bounds merged in #103 |
| [#92](https://github.com/wckdboy/nightfall/issues/92) | 2.3 Evaluator: feature hashing, incremental rebuild, cache | kernel | Done: evaluator/cache and local benchmark merged in #103; further operation/selector coverage remains open |
| [#104](https://github.com/wckdboy/nightfall/issues/104) | Exact curved sketch profiles: arcs, ellipses and NURBS splines | kernel | Implemented with regression tests and local benchmark evidence |
| [#106](https://github.com/wckdboy/nightfall/issues/106) | Shared straight boundaries in sketch profiles | math | Implemented with arrangement, region and evaluator regressions; local benchmark evidence |
| [#93](https://github.com/wckdboy/nightfall/issues/93) | 2.4 Nightfall Script: JavaScriptCore host and nf API bindings | script |  |
| [#94](https://github.com/wckdboy/nightfall/issues/94) | 2.5 CLI: `nf build part.nightfall --out part.step` | cli |  |
| [#95](https://github.com/wckdboy/nightfall/issues/95) | 2.6 20 reference parts and the Phase 2 gate | evals | Twenty editable reference documents and headless regressions added; CI confirmation and full gate sign-off pending |
| [#74](https://github.com/wckdboy/nightfall/issues/74) | Coincident edges in planar arrangements (from Phase 1) | math | Degree-1 slice in #106; higher-degree shared stretches remain open |

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

- 2026-10-18 — #92 implements extrusion `join`, `cut` and `intersect` results with current-body
  dependencies and atomic publication. Temporary swept solids stay private and retained only
  as cache/history inputs. Target and tool face names survive the boolean and rebind to renamed
  features on cache hits. Seven new regressions cover analytic box combinations (9,000/3,000/
  3,000 mm³), a symmetric circular through-cut (6,000 − 120π mm³, tolerance 1e-5), target motion,
  suppression, rollback, failed combinations and input-snapshot eviction.
  Full OCCT checks pass 273/273 (16.16 s); ASan/UBSan evaluator tests pass 32/32 (28.69 s),
  with no sanitizer diagnostics. The independent schema corpus remains 202/202.

- 2026-10-18 — #92 adds exact solid revolutions about explicit axes in the sketch plane,
  including signed partial/full turns, new/join/cut/intersect results and face provenance.
  Tests cover hollow cylinders, toruses, cavities, placed axes, axis-touching profiles,
  selected regions, dimensional angles, incremental edits, cache rebinding, eviction,
  rollback, suppression and failure propagation. Profile control hulls must stay on one side
  of the axis; conservative rejection prevents overlapping sweeps without approximating curves.
  Exact linear spans now produce analytic planes/cylinders/cones in OCCT; the original profile
  provenance is retained while edge parameterization changes. Rational curves remain exact.
  Sketch-line/selector axes and the other remaining v1 operations still report errors.
  Full OCCT checks pass 287/287 (19.06 s); full ASan/UBSan checks pass 287/287 (663.87 s),
  with no sanitizer diagnostics or retries. The latter ran under substantial background build
  load. Hygiene checks and the independent 202-case schema cross-check pass. Nightfall code is
  instrumented; OCCT remains the pinned Release shared-library build.

- 2026-10-18 — The feature-IR/evaluator stack is integrated through [PR #103](https://github.com/wckdboy/nightfall/pull/103),
  main commit `4fe1319`. Its tree matches the validated `369938b` exactly. All five GitHub check
  groups passed; the maintainer explicitly authorized the merge in the project conversation.
  Superseded stacked PRs #97/#100/#101/#102 are closed with their source branches retained.
  Issues #90/#91/#92/#98/#99 and their Plane counterparts are complete. The full Phase 2 gate
  remains open; subsequent capability work uses dedicated issues.

- 2026-10-18 — #104 adds exact signed arcs, rotated ellipses and clamped rational/non-rational
  NURBS splines to the existing sketch schema. Mixed line/arc/spline chains may be unordered
  or reversed; both endpoints are checked for open/ambiguous junctions. Exact span decomposition
  prevents region probes from landing on spline corners. Knot/pole/weight errors carry field
  pointers; initial sketch-curve, spline-pole and profile-span work limits are each 4,096.
  Sixteen regressions cover semicylinders (200π mm³), placed ellipses (72π mm³), a rational
  circle, a closed polyline spline, a sphere (4000π/3 mm³), holes/region picks, parameter edits,
  equivalent-unit cache reuse, failed dependencies and malformed/bounded inputs. Review found
  accepted near-coincident joins could remain disconnected in region detection; explicit bridges
  now preserve the original curves and connect those endpoints. Bridges below OCCT's vertex
  precision use vertex welding, avoiding degenerate edges. Regressions cover line/line,
  line/arc and arc/arc gaps of 5e-7 and 5e-8 mm, including the closing junction. Volume
  tolerances are 1e-8–1e-5 mm³; centroid tolerances are 1e-7 mm. No schema/API/dependency change.
  Full Release `tools/check.sh --preset occt` passes 303/303 (17.91 s), with hygiene and the
  independent 202-case schema cross-check passing. The CI ASan/UBSan kernel/evaluator/cache
  selection passes 89/89 (27.77 s), with no diagnostics or retries. Nightfall code is instrumented;
  OCCT remains the pinned Release shared-library build. The prior integrated tree also passed
  the full 287-test sanitizer suite; this slice reruns the affected geometry/service selection.

- 2026-10-18 — #106 supports coincident degree-1 boundaries in planar arrangements. Overlap
  endpoints split both inputs using their original rational parameterization; one half-edge pair
  retains the signed sum of traversals. Zero-count seams remain for topology and disappear from
  filled outlines. Nonzero/even-odd regions now handle adjacent rectangles, partial overlaps,
  repeated/reversed outlines and notches. Empty profiles fail at `/inputs/profile` before kernel
  work; general curved coincidence remains an explicit #74 error. Evaluator identity is bumped.
  Sixteen additional tests include a fixed-seed 128-case analytic rectangle corpus, rational
  parameters, multi-span polylines, crossing seams, scale/rotation/order, separated parallel
  edges and actual OCCT extrusion/cache behavior. The original ten math and four evaluator
  reproductions failed before implementation. Full Release `tools/check.sh --preset occt`
  passes 319/319 (23.50 s), hygiene and the independent 202-case schema corpus pass.
  Targeted local ASan/UBSan passes 40 math cases (20,936 assertions across arrangement,
  region, intersection and refinement tests) plus 93 kernel/evaluator/cache cases (74.58 s),
  with no diagnostics or retries. Nightfall is instrumented; OCCT uses pinned Release libraries.

- 2026-10-18 — #95 adds [20 editable reference parts](../../examples/parts/README.md) with
  53 authored features: brackets, an enclosure, turned parts, curved profiles and solid
  booleans. Twenty geometry tests plus a catalogue check run through the production evaluator.
  Each part has three analytic parameter cases, cold/incremental comparisons, equivalent-unit
  reuse, cache restoration and eviction with retained-snapshot checks. Volumes use 1e-5 mm³,
  centroids 1e-6 mm and bounds 1e-5 mm absolute tolerances. Nineteen parts also check analytic
  surface area; the elliptic duct checks finite positive area. The spline wedge retains a
  separate 2e-4 mm² area budget for OCCT numerical integration (observed error up to 1.73e-4);
  other areas use 1e-5 mm². No runtime API, schema or dependency change. The independent schema
  check now includes all 20 files, and example edits trigger Release and ASan/UBSan geometry CI.
  Full Release `tools/check.sh --preset occt` passes 340/340 (504.46 s), with hygiene and both
  schema corpora (202 cases plus 20 documents) passing. This run had substantial background
  build load; the torus exposes expensive OCCT volume/centroid integration over NURBS surfaces
  of revolution, confirmed by a local stack sample. No new latency claim is made.
  The ASan/UBSan reference selection passes 21/21 (391.07 s), with no diagnostics or retries.
  Nightfall code is instrumented; OCCT remains the pinned Release shared-library build.
  Remote CI confirmation, maintainer review and full Phase 2 sign-off remain pending.

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

### Revolution rebuild evidence

2026-10-18, same machine/configuration and 30-sample methodology. The turned sleeve has one
sketch, one revolution and 28 transforms. Every unique height edit rebuilds the sketch and
all 29 kernel operations; analytic volume `75π × height` mm³, centroid and execution counts
are checked for every sample. Exact line spans retain analytic surfaces in OCCT. Before this
representation change, sleeve height edits measured 1,756.782 ms median in an earlier run.

| Turned sleeve | Median ms | p95 ms | Max ms | Kernel calls / cache hits |
| --- | ---: | ---: | ---: | ---: |
| Cold | 8.828 | 29.102 | 37.186 | 29 / 0 |
| Height edit | 6.934 | 8.842 | 9.595 | 29 / 0 |
| Last transform edit | 0.613 | 0.978 | 1.026 | 1 / 29 |
| Unused parameter edit | 0.596 | 0.633 | 0.845 | 0 / 30 |

The sleeve cache retains 96 entries / 6,422,472 estimated bytes after the height edits.
This final benchmark ran after Nightfall's builds/tests finished. Same-run depth-edit results
for the existing cases are recorded below rather than assuming earlier timings still apply.

| Existing part, depth edit | Median ms | p95 ms | Max ms |
| --- | ---: | ---: | ---: |
| Plate | 2.137 | 2.337 | 2.783 |
| Bored plate | 18.263 | 28.047 | 33.780 |
| Boolean-cut plate | 72.176 | 126.785 | 129.510 |

The sleeve meets the local 100 ms criterion in every sample. The boolean case's tail exceeds
100 ms in this run; these measurements do not establish a universal latency bound. Broader
reference-part coverage, remote CI/review of this slice and full Phase 2 sign-off remain open.

### Curved profile rebuild evidence

2026-10-18, same Apple M3 Pro / Release OCCT 7.9.3 and 30-sample methodology. The new elliptic
tube has two nested ellipses rotated 35°, an extrusion and 28 transforms. Changing `depth`
changes both ellipse major radii and extrusion distance: outer radii `(depth/3, 5)` mm, inner
radii `(depth/6, 2.5)` mm. Every sample verifies volume `1.25π × depth²` mm³, centroid and
execution counts. Profile and all downstream geometry rebuild on each unique depth edit.

| Elliptic tube | Median ms | p95 ms | Max ms | Kernel calls / cache hits |
| --- | ---: | ---: | ---: | ---: |
| Cold | 9.366 | 9.906 | 10.113 | 29 / 0 |
| Profile + depth edit | 9.467 | 10.593 | 11.907 | 29 / 0 |
| Last transform edit | 0.656 | 0.707 | 0.731 | 1 / 29 |
| Unused parameter edit | 0.609 | 0.688 | 0.760 | 0 / 30 |

The cache retains 96 entries / 18,732,720 estimated bytes after profile/depth edits (not RSS).
Final benchmark after the review fix: same-run depth-edit median/p95/max is
plate 1.966/2.518/4.987 ms, bored plate 9.781/11.660/18.681 ms,
boolean-cut plate 47.112/50.712/51.059 ms, sleeve 6.318/7.494/7.524 ms. Every sample is under
100 ms in this run; earlier slower tails remain documented above. Timing variation is not
an optimization claim or a universal bound. Includes parsing, validation, geometry and naming;
excludes serialization, IPC and tessellation. Full phase sign-off remains open.

### Shared straight-boundary rebuild evidence

2026-10-18, Apple M3 Pro / 12 cores / 36 GiB, Release OCCT 7.9.3, 30 samples per case.
The 30-feature part has two adjacent rectangles, one extrusion and 28 transforms. Each unique
`depth` edit changes both rectangle widths and the extrusion distance. The union has width
`2 × depth/3` mm and height 20 mm; every sample checks volume `(40/3) × depth²` mm³, centroid
and execution counts. The shared internal edge must disappear from the filled outline.

| Shared-edge plate | Median ms | p95 ms | Max ms | Kernel calls / cache hits |
| --- | ---: | ---: | ---: | ---: |
| Cold | 3.238 | 6.298 | 7.173 | 29 / 0 |
| Profile + depth edit | 7.934 | 22.958 | 106.810 | 29 / 0 |
| Last transform edit | 1.841 | 15.023 | 27.758 | 1 / 29 |
| Unused parameter edit | 1.132 | 15.357 | 17.751 | 0 / 30 |

The cache retains 96 entries / 14,631,984 estimated bytes after profile/depth edits (not RSS).
Same-run depth-edit median/p95/max: plate 2.357/4.381/5.129 ms,
bored plate 17.057/26.177/36.863 ms, boolean-cut plate 79.371/124.210/256.712 ms,
sleeve 13.588/157.142/191.908 ms, elliptic tube 15.563/27.029/30.351 ms.
The shared-edge maximum and boolean/sleeve tails exceed 100 ms in this run. These are local
measurements with visible timing variation; no universal latency bound or performance
improvement is claimed. Includes parsing, validation, geometry and naming; excludes
serialization, IPC and tessellation. Full Phase 2 sign-off remains open.
