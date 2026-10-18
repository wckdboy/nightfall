# Phase 2 — Feature IR + Script

- **Weeks:** 8–16 (build plan)
- **Milestone:** `Phase 2` on GitHub
- **Status:** In progress

## Gate

All of these, with evidence linked:

- [ ] 20 reference parts (gear, bracket, enclosure, …) build headlessly in CI
- [x] Changing one parameter rebuilds a 30-feature part in < 100 ms ([baseline evidence](#rebuild-benchmark-evidence); [slanted-profile fix](#polynomial-mass-integration-rebuild-evidence))
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
| [#108](https://github.com/wckdboy/nightfall/issues/108) | Analytic circular profiles and fast exact mass integration | kernel | Implemented with analytic-surface regressions; local verification and rebuild evidence below |
| [#109](https://github.com/wckdboy/nightfall/issues/109) | Certified shared curved spans in sketch profiles | math | Implemented with rational-curve, fill-rule and production evaluator regressions |
| [#110](https://github.com/wckdboy/nightfall/issues/110) | Verify line and radius constraints in evaluated sketches | kernel | Implemented with tolerance, unit, dependency and cache regressions; local evidence below |
| [#111](https://github.com/wckdboy/nightfall/issues/111) | Resolve point references and verify point constraints | kernel | Implemented with reference-safety, point-retention, constraint and solid regressions |
| [#112](https://github.com/wckdboy/nightfall/issues/112) | Verify analytic tangent constraints in evaluated sketches | kernel | Implemented with finite-contact, unit, tolerance and solid/cache regressions; local verification below |
| [#113](https://github.com/wckdboy/nightfall/issues/113) | Verify angle dimensions and fixed sketch references | kernel | Implemented with direction, tolerance, reference and solid/cache regressions; local evidence below |
| [#114](https://github.com/wckdboy/nightfall/issues/114) | Bound polynomial mass integration for planar solids | kernel | Implemented with unconstrained latency, analytic geometry, placement, cavity and curved-trim regressions; local evidence below |
| [#115](https://github.com/wckdboy/nightfall/issues/115) | Resolve sketch-line axes for revolutions and rigid transforms | kernel | Implemented with directed-line, frame, dependency, cache and ownership regressions; local evidence below |
| [#116](https://github.com/wckdboy/nightfall/issues/116) | Open reference sketches with closed solid profiles | kernel | Implemented with standalone-axis, profile-boundary, exact-curve, validation and cache regressions; local evidence below |
| [#117](https://github.com/wckdboy/nightfall/issues/117) | Parametric linear body patterns with complete provenance | kernel | Implemented with fused/separated arrays, input bounds, dependency, naming and atomic-failure regressions; local evidence below |
| [#93](https://github.com/wckdboy/nightfall/issues/93) | 2.4 Nightfall Script: JavaScriptCore host and nf API bindings | script |  |
| [#94](https://github.com/wckdboy/nightfall/issues/94) | 2.5 CLI: `nf build part.nightfall --out part.step` | cli |  |
| [#95](https://github.com/wckdboy/nightfall/issues/95) | 2.6 20 reference parts and the Phase 2 gate | evals | Twenty editable reference documents and headless regressions added; CI confirmation and full gate sign-off pending |
| [#74](https://github.com/wckdboy/nightfall/issues/74) | Coincident edges in planar arrangements (from Phase 1) | math | Straight spans in #106 and certified curved spans in #109; other parameterizations remain open |

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

- 2026-10-18 — #108 preserves circular rational-quadratic spans as analytic OCCT arcs.
  Recognition checks every homogeneous circle-polynomial coefficient at roundoff scale;
  uncertain curves and nearly orthonormal sketch placements retain their authored NURBS.
  Extrusions/revolutions now produce analytic cylinders, spheres and tori while retaining
  source-curve provenance. Mass integration keeps the same exact surfaces and 1e-9 requested
  relative accuracy, with a local bounding-box corner reference for each shell. First moments
  accumulate without intermediate normalization so cancellation of signed face volumes cannot
  erase a centroid contribution. The backend identity covers both changes. Ten new regressions
  cover scales/angles, reversed and split arcs, unequal weights, perturbed curves, partial
  revolutions, placed geometry, distant unequal solids and internal cavities. The existing
  triangular gusset caught and verifies the signed-moment cancellation case.
  Full Release `tools/check.sh --preset occt` passes 350/350 (20.69 s), including hygiene
  and both schema corpora (202 cases plus 20 documents). The exact CI ASan/UBSan geometry,
  evaluator and cache selection passes 124/124 (66.78 s), with no diagnostics or retries.
  Nightfall is instrumented; OCCT remains the pinned Release shared-library build.
  Remote CI, maintainer review and full Phase 2 sign-off remain pending.

- 2026-10-18 — #109 supports shared curved sketch boundaries through exact Bezier-span
  decomposition, overlap-endpoint splitting and signed traversal accumulation. Whole rational
  polynomial coefficients certify a common trace, including reversed/partial spans, degree
  elevation and Mobius parameter changes. The bound is local (1e-12 of control extent plus
  coordinate roundoff), independent of drawing-wide vertex welding. Stored curves keep their
  original knot parameters. Parent overlap certification prevents accidental merging of tiny
  pieces at near-tangencies; ordinary intersections retain the original curve domains, and
  control-hull bounds identify exact line contacts. Uncertifiable parameterizations still
  report #74. No schema, dependency or public signature change; evaluator identity is bumped.
  Eight math regressions include 48 fixed-seed ellipse/cap placements, duplicate open/closed
  splines, distinct near-curves, third-path crossings and internal/external tangencies.
  Three evaluator regressions verify duplicate circles/ellipses, a 60° shared-arc notch,
  empty-fill errors, dependent failure, parameter edits, cold equivalence, equivalent-unit
  cache restoration and retained names after eviction. Solid volumes/areas use 1e-5 absolute
  tolerances and centroids 1e-7 mm. The prior implementation failed six new math reproductions.
  Full Release `tools/check.sh --preset occt` passes 361/361 (19.54 s), including hygiene
  and the 202-case plus 20-document schema checks. Targeted ASan/UBSan passes 48 math cases
  (21,484 assertions across shared curves, arrangements, regions, intersections and refinement)
  plus all 127 kernel/evaluator/cache cases (32.68 s), with no diagnostics or retries.
  Nightfall code is instrumented; OCCT remains the pinned Release shared-library build.
  Remote CI, maintainer review and full Phase 2 sign-off remain pending.

- 2026-10-18 — #110 verifies horizontal/vertical lines and parallel/perpendicular line pairs
  within 1e-9 rad, equal line lengths or circle/arc radii within 1e-6 mm, and circle/arc radius
  dimensions after unit/expression evaluation. Reversed lines retain the same orientation
  constraints; equal arcs compare radii, not sweep lengths. Coordinates and exact profile
  geometry are preserved. Violations report the constraint's JSON Pointer, measured residual
  and tolerance; invalid dimensions and incompatible entities identify their specific fields.
  Constraint work is limited to 4096 relations per sketch; degenerate and non-finite derived
  measurements fail explicitly. Other constraint kinds remain unsupported; no solver, schema,
  public kernel interface or dependency change. Evaluator identity is bumped for the semantics.
  Ten new regressions failed before implementation and now pass (343 assertions), including
  48 fixed-seed rotated/translated rectangles, angular bounds across three scales, absolute
  dimensional bounds, partial arcs and equivalent units. Production solids verify a 6000 mm³
  constrained plate (1e-8 tolerance) and radius-edit cylinders (volume/area 1e-5, centroid
  1e-7 mm), failure propagation with independent branches, cache recovery, suppression,
  rollback and retained naming after cache eviction. Full Release `tools/check.sh --preset occt`
  passes 371/371 (19.94 s), including hygiene, 202 schema corpus cases and 20 reference documents.
  The exact CI ASan/UBSan kernel/evaluator/cache selection passes 137/137 (37.08 s), with no
  diagnostics or retries. Nightfall is instrumented; OCCT remains the pinned Release shared
  library. Rebuild evidence follows; remote CI/review and full phase sign-off remain pending.

- 2026-10-18 — #111 resolves schema-v1 point references safely: named points, line/arc
  endpoints and arc/circle/ellipse centers. Point-qualified operands for entire-line/radius
  constraints report a field error. A tests-first ASan/UBSan reproduction confirmed the old
  evaluator bound a string reference to null and aborted on a schema-valid object operand;
  its regression covers all six existing constraint kinds. Standalone points are retained
  separately from closed profile curves and included in cache memory estimates. Point-only
  sketches remain empty profiles for solid operations. Coincidence, point-point distance,
  midpoint (point/line in either order) and two-point symmetry about a third line verify
  evaluated coordinates within 1e-6 mm. Midpoint arithmetic avoids overflowing coordinate
  sums; non-finite derived results and unsupported point roles fail explicitly. No coordinates
  are solved or moved, and no schema, dependency or public kernel-interface change is made.
  Evaluator identity is bumped; the combined curve/point work limit stays 4096.
  Eleven new regressions cover mixed reference forms, unit conversion, negative arc sweeps,
  wrapped angles, tolerance bounds, 48 fixed-seed rigid placements at two scales, reversed
  axes, large coordinates, point retention and the work limit. Production solids verify
  width edits from 10 to 12 mm: volumes 6000/7200 mm³ and areas 2200/2400 mm² within 1e-8,
  plus centroids, dependency failures, equivalent-unit cache recovery and retained names.
  The combined constraint selection passes 21 cases / 610 assertions. Full Release
  `tools/check.sh --preset occt` passes 382/382 (19.47 s), including hygiene and the 202-case
  plus 20-reference-document schema checks. The exact CI ASan/UBSan kernel/evaluator/cache
  selection passes 148/148 (40.57 s), including the former null-reference reproduction,
  without diagnostics or retries. Nightfall is instrumented; OCCT uses the pinned Release
  shared libraries. Benchmark evidence follows; remote CI/review and phase sign-off remain open.

- 2026-10-18 — #112 verifies analytic tangent constraints between line segments and circular
  curves, or between two circles/arcs. Internal and external contact use a 1e-6 mm radial-gap
  bound; contact must lie on the finite segment (1e-6 mm) and each arc (1e-9 rad). Centers
  within linear tolerance, degenerate geometry, point-qualified references and unsupported
  curve kinds fail explicitly. Authored coordinates remain unchanged; no solver, schema,
  public kernel API or dependency is added. The evaluator cache identity is bumped.
  Ten tests-first regressions originally failed 123 assertions. They cover unit normalization,
  signed/wrapped/full-turn arcs, operand order, finite extents, tolerance boundaries, overflow,
  and 48 fixed-seed placements at two scales with reversed curves. Rounded extrusions check
  radius edits from 5 to 6 mm: volumes 6000+750π / 7200+1080π mm³ and areas
  1600+350π / 1680+432π mm² within 1e-5, plus centroids within 1e-7 mm. Invalid edits block
  both dependents without kernel work; restoring equivalent units reuses all three cached
  outputs. Cold comparison and retained names after cache eviction also pass.
  The combined constraint selection passes 31 cases / 1110 assertions. Full Release
  `tools/check.sh --preset occt` passes 392/392 (19.69 s), including hygiene, 202 schema corpus
  cases and 20 reference documents. The exact CI ASan/UBSan kernel/evaluator/cache selection
  passes 158/158 (38.50 s), with no diagnostics. Nightfall is instrumented; OCCT remains the
  pinned Release shared library. Benchmark evidence follows; remote CI/review and full
  phase sign-off remain pending.

- 2026-10-18 — #113 adds angle dimensions and fixed references, the two remaining v1
  constraint kinds. Angle dimensions use the smaller angle between start-to-end directions
  of two nonzero lines, in [0, π], within 1e-9 rad. Swapping operands or reversing both lines
  preserves the angle; reversing one takes its supplement. Fixed whole entities and supported
  point references preserve their evaluated authored coordinates, while all entity and other
  constraint checks still run. They do not freeze parameter edits or compare against a prior
  document. No solver, schema, public kernel API or dependency is added; the cache identity
  is bumped.
  Nine tests-first regressions originally failed 90 assertions; all nine now pass 725 assertions.
  Coverage includes acute/obtuse/0°/180° angles, mm/cm and deg/rad, dimensional expressions,
  invalid references and dimensions, finite arithmetic, tolerance bounds at three scales,
  48 fixed-seed rigid placements, every supported fixed entity and point role, and unchanged
  exact curves. A slanted extrusion with angle θ = 45°/60°/120° checks volume 6000 mm³,
  surface area `1000 + 1200/sin(θ)` mm² and centroid (3,0,15) mm within 1e-7. Constraint-only
  edits fail both dependents without kernel work; equivalent-unit restoration hits all three
  cached outputs. Subsequent angle edits update even fixed coordinates; cold comparisons
  and retained naming after cache eviction pass.
  The new solid regression exposed a mass-integration performance problem: one targeted
  Release run took 32.610 s in that case, while all eight other new cases took 0.000–0.003 s.
  A process sample locates the work in the existing OCCT `BRepGProp_VinertGK::Perform` /
  nested Kronrod integration during snapshot measurement. This remains a separate kernel
  performance follow-up; the correctness regression is retained. The fix plan is to isolate
  an unconstrained planar-solid reproduction, investigate integration/reference conditioning
  without loosening mass tolerances, and rerun the slanted-profile and existing cavity,
  multi-solid and curved-surface checks.
  Full Release `tools/check.sh --preset occt` passes 401/401 (54.40 s), including hygiene,
  202 schema corpus cases and 20 reference documents. The exact CI ASan/UBSan
  kernel/evaluator/cache selection passes 167/167 (86.63 s), with no diagnostics. Nightfall
  is instrumented; OCCT uses the pinned Release shared libraries. The solid regression
  takes 33.27 s / 46.11 s in these Release / sanitizer runs. The 30-feature benchmark
  follows; remote CI/review and full Phase 2 sign-off remain pending.

- 2026-10-18 — #114 resolves the slanted-solid mass-integration bottleneck from #113.
  Planar faces qualify for polynomial Gaussian quadrature only when every pcurve is an
  analytic line with finite parameter bounds. These affine parameterizations make the
  volume/first-moment integrands polynomial; OCCT's fixed Gaussian rule integrates them
  exactly up to floating-point error. Other faces retain exact-surface adaptive
  Gauss–Kronrod integration at 1e-9. The per-shell corner reference, signed cavity terms
  and unnormalized first-moment accumulation are preserved. The backend cache identity
  includes the new integration policy. No dependency, public API or format changes.
  Five new regressions cover an unconstrained production extrusion, 16 fixed-seed
  rotated/translated/scaled prisms, a concave profile with a straight hole, a slanted cavity
  with a distant unequal solid, and curved trim boundaries. The tests-first unconstrained
  extrusion failed only its 2 s runtime bound: 20.063 s, with all 24 geometry/naming
  assertions passing. The same case now takes about 0.001 s; volume 6000 mm³ and area
  3400 mm² remain within 1e-7. The existing angle-driven solid regression takes 0.008 s
  in the targeted run, compared with 33.27 s in #113's Release gate. The focused planar,
  mass-reference and analytic-circle selection passes 15 cases / 864 assertions; the
  angle/fixed selection passes 9 cases / 725 assertions.
  Full Release `tools/check.sh --preset occt` passes 406/406 (21.38 s), including hygiene,
  202 schema corpus cases and 20 reference documents. The exact CI ASan/UBSan
  kernel/evaluator/cache selection passes 172/172 (42.61 s), with no diagnostics.
  Nightfall is instrumented; OCCT remains the pinned Release shared library. The unchanged
  rebuild benchmark below restores the local latency gate for the slanted profile.
  Remote CI/review and full Phase 2 sign-off remain pending.

- 2026-10-18 — #115 resolves schema-v1 sketch-line axes for revolutions and rigid rotations.
  Evaluated sketches retain named, authored line endpoints separately from profile traversal;
  axes map through datum or explicitly placed frames. Direction comes from the local delta,
  avoiding cancellation when subtracting endpoints after large world placement. Axis-sketch
  hashes join the ordered feature dependencies; failed/suppressed sketches invalidate affected
  bodies before backend work. Cache estimates include line metadata and evaluator identity is
  version 13. The schema, public kernel API and dependencies are unchanged (RFCs 0005/0007).
  Selector axes and open/construction curve chains remain unsupported; named lines in existing
  closed sketches are supported. Revolutions retain the backend's planar-axis/one-sided-hull
  restrictions. Eight tests-first regressions all failed on the previous unsupported axes;
  the final focused suite passes 11 cases / 613 assertions. Coverage includes reversed path
  traversal, signed partial turns, all datum frames and explicit placement, independent axis
  edits, equivalent units, producer renaming, suppression/failure/rollback, out-of-plane axes,
  overflow and large placement, typed references, eviction and kernel-facade teardown.
  Analytic volume/area agree within 1e-7 and centroid components within 1e-8 mm. Full Release
  `tools/check.sh --preset occt` passes 417/417 (23.30 s), including hygiene, 202 schema corpus
  cases and 20 reference documents. The CI ASan/UBSan kernel/evaluator/cache selection passes
  183/183 (45.35 s), with no diagnostics. Nightfall is instrumented; OCCT remains the pinned
  Release shared library. Rebuild evidence follows; remote CI/review and full Phase 2 sign-off
  remain pending.

- 2026-10-18 — #116 allows standalone centerlines and open line/arc/spline reference sketches.
  Entity/constraint validation stays in sketch preparation; exact unjoined curves are retained
  separately from already-closed primitives. Filled-profile consumers assemble closed,
  unambiguous chains without mutating the cached sketch. Open/ambiguous chains fail at that
  consumer's `/inputs/profile`, including mixed closed/open sketches with region picks.
  The sketch remains available as an axis even when a profile consumer fails. Axis direction,
  parametric dependencies, suppression/rollback, body invalidation and naming retain their
  existing behavior. Cache estimates now include raw segments; evaluator identity is version 14.
  No schema, persisted format, public kernel API or dependency change (RFCs 0005/0007).
  All eight tests-first regressions failed against the prior sketch-level closure check. The
  final open-sketch suite passes 8 cases / 296 assertions: independent axis edits and equivalent
  units, directed rotations, sleeve-to-cylinder revolution, profile failure isolation, branched
  reference networks, entity/constraint errors, exact arc/rational-spline retention, rollback,
  failure/suppression recovery, byte-budget eviction and retained solid readers after teardown.
  Analytic volume/area agree within 1e-7 and centroid components within 1e-8 mm. Existing curve
  and constraint tests compare the retained unjoined segments; their solid golden checks remain.
  Full Release `tools/check.sh --preset occt` passes 425/425 (22.47 s), including hygiene,
  202 schema corpus cases and 20 reference documents. CI's ASan/UBSan kernel/evaluator/cache
  selection passes 191/191 (47.40 s), with no diagnostics. Nightfall is instrumented; OCCT remains
  the pinned Release shared library. README documents the reference/profile distinction and
  sketch-line axes. Selector axes, open-wire solids and construction flags remain unsupported.
  Rebuild evidence follows; remote CI/review and full Phase 2 sign-off remain pending.

- 2026-10-18 — #117 implements schema-v1 linear body patterns using existing rigid transforms
  and solid union. Count includes each seed; copies translate from that original current body
  by integer multiples of signed spacing along a normalized direction. Touching/overlapping
  copies fuse, separated copies remain valid solid components in the same body, and count 1
  preserves geometry through an identity transform. Unique body targets and finite offsets
  are checked before copying; work is bounded to 64 total instances across all seeds. Larger
  counts require spacing magnitude above 1e-6 mm. Feature-target/circular patterns remain
  unsupported. No schema, persisted format, public kernel API or dependency change
  (RFCs 0005/0007).
  Intermediate copies/history remain private and are charged to cache estimates. Copied faces
  retain inherited producer names and gain the pattern's local `instance` role. Both histories
  rebind names to current IDs/snapshots on cache reuse; all target bodies publish atomically
  after geometry and naming succeed. Evaluator/naming identities advance to 15/4.
  All 13 tests-first cases failed on the prior unsupported-pattern path. The final suite passes
  15 cases / 1878 assertions, covering fused/separated arrays, signed/normalized directions,
  count/spacing errors, exact and aggregate work bounds, current-body dependencies, equivalent
  units, producer renaming, binding misses, suppression/rollback, unavailable seeds, incomplete
  histories, injected copy/union failures, eviction and kernel-facade teardown. Annular seeds
  preserve periodic faces and through-holes: three instances have volume 1890π mm³, area
  1386π mm² and 12 faces. All volume/area checks use 1e-7; centroids use 1e-8 mm.
  Full Release `tools/check.sh --preset occt` passes 440/440 (24.87 s), including hygiene,
  202 schema corpus cases and 20 reference documents. CI's ASan/UBSan kernel/evaluator/cache
  selection passes 206/206 (49.85 s), with no diagnostics. Nightfall is instrumented; OCCT
  remains the pinned Release shared library. README and adapter/evaluator contracts document
  pattern behavior and limits. Rebuild evidence follows; remote CI/review and full Phase 2
  sign-off remain pending.

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

### Analytic circular-profile rebuild evidence

2026-10-18, Apple M3 Pro / 12 cores / 36 GiB, Release OCCT 7.9.3. The new 30-feature
toroidal seal has one circular sketch, one revolution and 28 transforms. Its major radius
is 20 mm; each unique `depth` edit changes the section radius to `depth/10` mm (3.1–6 mm).
Every sample checks volume `0.4π² × depth²` mm³ within 1e-5, centroid within 1e-6 mm and
the expected kernel/cache counts. Run `build/occt/services/kernel/nf_evaluator_bench`.

| Toroidal seal, 30 samples | Median ms | p95 ms | Max ms | Kernel calls / cache hits |
| --- | ---: | ---: | ---: | ---: |
| Cold | 88.984 | 120.664 | 126.083 | 29 / 0 |
| Section radius edit | 80.776 | 92.599 | 95.948 | 29 / 0 |
| Last transform edit | 0.626 | 0.724 | 0.726 | 1 / 29 |
| Unused parameter edit | 0.610 | 0.992 | 1.150 | 0 / 30 |

The cache retains 96 entries / 7,212,480 estimated bytes after radius edits (not RSS).
All radius-edit samples are below 100 ms; the cold-build tail exceeds it. This run follows
the builds and sanitizer checks. It includes parsing, validation, expressions, hashing,
exact geometry and naming; serialization, IPC and tessellation remain excluded.

Same-run depth-edit median/p95/max: plate 2.343/5.005/5.409 ms,
bored plate 7.960/8.267/8.944 ms, boolean-cut plate 9.633/12.385/14.630 ms,
sleeve 3.315/4.220/5.490 ms, elliptic tube 10.908/16.012/16.828 ms,
shared-edge plate 2.870/4.907/5.574 ms. These are local observations, not universal bounds.

The focused full-torus regression (R=20 mm, r=3 mm) took 8.423 s on the original NURBS
backend and 0.076 s after analytic arcs plus conditioned mass integration. Both checked
volume 360π² mm³ and area 240π² mm² within 1e-5, and centroid within 1e-7 mm; the original
backend failed the new analytic-surface-type assertion. These are single correctness-run
timings, separate from the 30-sample rebuild benchmark. The complete reference selection
passes 21/21 in 2.47 s; all original analytic and cache assertions remain intact.

### Shared curved-boundary rebuild evidence

2026-10-18, Apple M3 Pro / 12 cores / 36 GiB, Release OCCT 7.9.3, 30 samples per case.
The 30-feature shared-arc part has a circle of radius 5 mm with a 90° cap removed under
even-odd fill, one extrusion and 28 transforms. Every sample checks cross-section area
`A = 25 × (3π/4 + 1/2)` through volume `A × depth` mm³, centroid x `−125/(6A)` before
translations, centroid z `depth/2`, and kernel/cache counts. Depth edits reuse the sketch
and rebuild all 29 solid operations. Run `build/occt/services/kernel/nf_evaluator_bench`.

| Shared-arc notch | Median ms | p95 ms | Max ms | Kernel calls / cache hits |
| --- | ---: | ---: | ---: | ---: |
| Cold | 3.160 | 3.284 | 3.344 | 29 / 0 |
| Depth edit | 3.335 | 3.479 | 3.484 | 29 / 1 |
| Last transform edit | 0.604 | 0.629 | 0.662 | 1 / 29 |
| Unused parameter edit | 0.579 | 0.605 | 0.606 | 0 / 30 |

The cache retains 96 entries / 10,753,680 estimated bytes after depth edits (not RSS).
Same-run depth-edit median/p95/max: plate 1.916/2.007/2.062 ms,
bored plate 7.424/7.672/7.990 ms, boolean-cut plate 7.857/8.089/8.117 ms,
sleeve 2.935/3.006/3.027 ms, elliptic tube 9.812/10.015/10.139 ms,
shared-edge plate 2.466/2.564/2.595 ms, toroidal seal 76.437/78.148/78.855 ms.
All samples are below 100 ms in this run; earlier slower tails remain documented above.
These local observations do not establish a universal bound or claim an optimization.
This run follows all builds/tests and includes parsing, validation, expressions, hashing,
exact geometry, validity and naming; serialization, IPC and tessellation remain excluded.
Remote CI, review and full Phase 2 sign-off remain pending.

### Constrained sketch rebuild evidence

2026-10-18, Apple M3 Pro / 12 cores / 36 GiB, Release OCCT 7.9.3, 30 samples per case.
The 30-feature part has four authored lines, six orientation/equal-length constraints,
one extrusion and 28 transforms. Each unique `depth` edit changes the centered rectangle's
width to `depth/3` mm, retains its 20 mm height, and changes the extrusion distance. Every
sample verifies volume `(20/3) × depth²` mm³ within 1e-5, centroid x/z within 1e-6 mm and
execution counts. Profile edits recheck every constraint and rebuild all 29 solid operations.
Run `build/occt/services/kernel/nf_evaluator_bench`.

| Constrained plate | Median ms | p95 ms | Max ms | Kernel calls / cache hits |
| --- | ---: | ---: | ---: | ---: |
| Cold | 1.817 | 2.009 | 2.054 | 29 / 0 |
| Profile + depth edit | 1.950 | 2.119 | 2.122 | 29 / 0 |
| Last transform edit | 0.655 | 0.674 | 0.678 | 1 / 29 |
| Unused parameter edit | 0.622 | 0.663 | 0.679 | 0 / 30 |

The 96-entry/64 MiB cache budget retains 96 entries / 10,525,848 estimated bytes after profile
edits (not RSS). Same-run depth-edit median/p95/max: plate 1.902/2.003/2.023 ms,
bored plate 7.242/7.448/7.479 ms, boolean-cut plate 8.179/8.513/8.960 ms,
sleeve 6.088/9.782/11.483 ms, elliptic tube 10.236/10.555/10.578 ms,
shared-edge plate 2.568/2.691/2.691 ms, shared-arc notch 3.490/3.632/3.659 ms,
toroidal seal 76.747/80.182/80.604 ms. All parameter-edit samples are below 100 ms in this
run; elliptic-tube cold builds have a 19.084/109.299/173.630 ms median/p95/max. These are local
observations, not universal bounds or an optimization claim. This run follows all builds/tests
and includes parsing, validation, expressions, constraint verification, hashing, exact geometry,
validity and naming; serialization, IPC and tessellation remain excluded. Remote CI/review
and full Phase 2 sign-off remain pending.

### Point-reference constraint rebuild evidence

2026-10-18, Apple M3 Pro / 12 cores / 36 GiB, Release OCCT 7.9.3, 30 samples per case.
The 30-feature part extends the constrained plate with three named points and four relations:
endpoint coincidence, endpoint distance, midpoint and reflection symmetry. Each unique depth
edit changes profile width, point positions and the distance dimension. All ten constraints
are verified before rebuilding the extrusion and 28 transforms. Every sample checks volume
`(20/3) × depth²` mm³ within 1e-5, centroid x/z within 1e-6 mm and execution/cache counts.
Run `build/occt/services/kernel/nf_evaluator_bench`.

| Point-reference plate | Median ms | p95 ms | Max ms | Kernel calls / cache hits |
| --- | ---: | ---: | ---: | ---: |
| Cold | 3.725 | 5.280 | 5.569 | 29 / 0 |
| Profile + point + depth edit | 2.548 | 5.969 | 7.003 | 29 / 0 |
| Last transform edit | 0.830 | 1.832 | 2.072 | 1 / 29 |
| Unused parameter edit | 0.877 | 2.012 | 2.103 | 0 / 30 |

The 96-entry/64 MiB cache budget retains 96 entries / 10,529,169 estimated bytes after profile
edits, including named points (not RSS). Same-run depth-edit median/p95/max:
plate 4.800/14.323/20.848 ms, bored plate 11.953/15.890/17.751 ms,
boolean-cut plate 11.860/17.691/19.629 ms, sleeve 5.184/8.136/9.023 ms,
elliptic tube 20.813/24.498/24.800 ms, shared-edge plate 5.312/6.653/7.398 ms,
shared-arc notch 5.533/9.410/11.429 ms, constrained plate 2.793/5.331/5.854 ms,
toroidal seal 103.771/149.924/153.931 ms. The toroidal median/tail exceeds 100 ms in this
run; elliptic cold builds reach 404.300 ms and even fully cached sleeve edits reach 276.516 ms.
The point-reference case stays below 100 ms in every sample. These observations include
substantial timing variation and do not establish a universal bound or optimization claim.
The run follows all builds/tests and includes parsing, validation, expressions, point/constraint
verification, hashing, exact geometry, validity and naming. Serialization, IPC and tessellation
remain excluded. Remote CI/review and full Phase 2 sign-off remain pending.

### Tangent-constrained rounded profile rebuild evidence

2026-10-18, Apple M3 Pro / 12 cores / 36 GiB, Release OCCT 7.9.3, 30 samples per case.
The 30-feature rounded part has two semicircles, two line segments, four tangent constraints,
one extrusion and 28 transforms. The radius is `5 mm × sqrt(depth / (30 mm))`; each line is
four radii long. Unique depth edits change the whole profile and extrusion, recheck all four
contacts and rebuild 29 solid operations. Every sample checks volume
`(25/30) × (8+π) × D²` mm³ for depth D in mm within 1e-5, centroid x/z within 1e-6 mm,
and kernel/cache counts. Run `build/occt/services/kernel/nf_evaluator_bench`.

| Tangent-constrained rounded part | Median ms | p95 ms | Max ms | Kernel calls / cache hits |
| --- | ---: | ---: | ---: | ---: |
| Cold | 3.441 | 3.509 | 3.529 | 29 / 0 |
| Profile + depth edit | 3.642 | 3.841 | 3.940 | 29 / 0 |
| Last transform edit | 0.624 | 0.670 | 0.699 | 1 / 29 |
| Unused parameter edit | 0.593 | 0.631 | 0.643 | 0 / 30 |

The 96-entry/64 MiB cache budget retains 96 entries / 14,632,896 estimated bytes after profile
edits (not RSS). Same-run depth-edit median/p95/max: plate 1.727/1.803/1.877 ms,
bored plate 6.660/6.761/6.769 ms, boolean-cut plate 7.279/7.457/7.489 ms,
sleeve 2.666/2.706/2.771 ms, elliptic tube 9.297/9.490/9.812 ms,
shared-edge plate 2.476/2.578/2.690 ms, shared-arc notch 3.089/3.179/3.247 ms,
constrained plate 1.801/1.866/1.881 ms, point-reference plate 1.865/1.974/2.009 ms,
toroidal seal 70.343/74.703/74.914 ms. All samples remain below 100 ms in this run; earlier
slower tails remain recorded above. These are local observations, not universal bounds or
an optimization claim. The run follows all builds/tests and includes parsing, validation,
expressions, constraint verification, hashing, exact geometry, validity and naming.
Serialization, IPC and tessellation remain excluded. Remote CI/review and full Phase 2
sign-off remain pending.

### Angle and fixed constraint rebuild evidence

2026-10-18, Apple M3 Pro / 12 cores / 36 GiB, Release OCCT 7.9.3, 30 samples per case.
The 30-feature part is a centered parallelogram (width 10 mm, height 20 mm), one extrusion
and 28 transforms. Angle θ is `depth / (1 mm) × 1 deg`; each depth edit changes the slant,
angle dimension and fixed line/endpoint coordinates. The profile area stays 200 mm².
Every sample checks volume `200 × D` mm³ and surface area `400 + D × (20 + 40/sin(θ))`
mm² within 1e-5, centroids x/z within 1e-6 mm, and kernel/cache counts. D is depth in mm.
Run `build/occt/services/kernel/nf_evaluator_bench`. All 30 samples of every mode passed
the analytic and execution-count checks.

| Angle/fixed slanted part | Median ms | p95 ms | Max ms | Kernel calls / cache hits |
| --- | ---: | ---: | ---: | ---: |
| Cold | 17334.659 | 17738.942 | 20483.019 | 29 / 0 |
| Angle + profile + depth edit | 37.344 | 17102.398 | 17682.842 | 29 / 0 |
| Last transform edit | 0.627 | 0.661 | 0.661 | 1 / 29 |
| Unused parameter edit | 0.630 | 0.656 | 0.659 | 0 / 30 |

The cold builds and parameter-edit tail exceed 100 ms by orders of magnitude. The local
performance gate is reopened for this profile; prior fast baseline measurements remain valid.
The kernel mass-integration follow-up and fix plan are recorded in the #113 log entry above.
A one-second process sample during an untimed warm-up corroborates the same OCCT nested
Kronrod integration path as the targeted regression. Cached changes avoid rebuilding that solid.

The 96-entry/64 MiB cache budget retains 96 entries / 10,528,224 estimated bytes after profile
edits (not RSS). Same-run depth-edit median/p95/max: plate 1.862/1.924/1.937 ms,
bored plate 7.013/7.171/7.176 ms, boolean-cut plate 7.788/7.957/8.053 ms,
sleeve 2.836/2.891/2.899 ms, elliptic tube 9.633/10.112/10.176 ms,
shared-edge plate 2.340/2.458/2.530 ms, shared-arc notch 3.140/3.224/3.253 ms,
constrained plate 1.845/1.909/1.918 ms, point-reference plate 1.907/1.992/2.037 ms,
tangent-constrained rounded part 3.925/4.038/4.085 ms, toroidal seal 75.710/76.513/76.671 ms.
These are local observations, not universal bounds or an optimization claim. The run follows
all builds/tests and includes parsing, validation, expressions, constraint verification,
hashing, exact geometry, validity and naming. Serialization, IPC and tessellation remain
excluded. Remote CI/review and full Phase 2 sign-off remain pending.

### Polynomial mass integration rebuild evidence

2026-10-18, Apple M3 Pro / 12 cores / 36 GiB, Release OCCT 7.9.3, 30 samples per case.
Run `build/occt/services/kernel/nf_evaluator_bench`. The benchmark source and analytic
expectations are unchanged from #113: a slanted 30-feature part, angle/profile/depth
edits, volume and area within 1e-5, centroid x/z within 1e-6 mm, plus kernel/cache counts.
All samples of every mode pass those checks.

| Slanted part after polynomial integration | Median ms | p95 ms | Max ms | Kernel calls / cache hits |
| --- | ---: | ---: | ---: | ---: |
| Cold | 1.858 | 1.924 | 1.971 | 29 / 0 |
| Angle + profile + depth edit | 2.010 | 2.070 | 2.087 | 29 / 0 |
| Last transform edit | 0.635 | 0.666 | 0.669 | 1 / 29 |
| Unused parameter edit | 0.615 | 0.633 | 0.635 | 0 / 30 |

#113's cold median was 17334.659 ms and parameter-edit p95/max were
17102.398/17682.842 ms. The local slanted-profile latency gap is resolved in this run;
all parameter-edit samples across the catalogue stay below 100 ms. Earlier measurements
remain recorded above; these are local observations, not universal bounds.

The 96-entry/64 MiB cache budget retains 96 entries / 10,528,224 estimated bytes after
slanted-profile edits (not RSS). Same-run depth-edit median/p95/max:
plate 1.853/1.995/2.104 ms, bored plate 7.221/7.413/7.443 ms,
boolean-cut plate 7.936/8.285/8.306 ms, sleeve 2.933/2.985/3.092 ms,
elliptic tube 9.724/9.999/10.002 ms, shared-edge plate 2.540/2.689/2.843 ms,
shared-arc notch 3.260/3.456/3.489 ms, constrained plate 2.225/3.310/3.920 ms,
point-reference plate 2.019/2.117/2.118 ms, tangent-constrained rounded part
3.853/3.992/4.020 ms, toroidal seal 75.474/77.356/77.800 ms.
The run follows all builds/tests and includes parsing, validation, expressions,
constraint verification, hashing, exact geometry, validity and naming. Serialization,
IPC and tessellation remain excluded. Remote CI/review and full Phase 2 sign-off remain pending.

### Sketch-line axis rebuild evidence

2026-10-18, Apple M3 Pro / 12 cores / 36 GiB, Release OCCT 7.9.3, 30 samples per mode.
The new 30-feature part has a four-line rectangular sketch in the zx plane, a full revolution
about the named bottom edge, and 28 transforms. Each unique depth edit changes both the
profile and the axis line's endpoint. The result is a radius-5 mm cylinder with height D
and axis at world x = 5 mm. Every sample checks volume `25πD` mm³ within 1e-5, centroid
x (including translations) and z within 1e-6 mm, and kernel/cache counts. Run
`build/occt/services/kernel/nf_evaluator_bench`. All 30 samples of every mode pass.

| Sketch-axis cylinder | Median ms | p95 ms | Max ms | Kernel calls / cache hits |
| --- | ---: | ---: | ---: | ---: |
| Cold | 1.991 | 2.292 | 2.299 | 29 / 0 |
| Profile + axis endpoint + depth edit | 2.190 | 2.550 | 2.679 | 29 / 0 |
| Last transform edit | 0.629 | 0.718 | 0.732 | 1 / 29 |
| Unused parameter edit | 0.624 | 0.905 | 1.130 | 0 / 30 |

The 96-entry/64 MiB cache budget retains 96 entries / 4,376,988 estimated bytes after depth
edits (not RSS). Same-run depth-edit median/p95/max: plate 2.046/2.114/2.126 ms,
bored plate 8.165/9.098/10.548 ms, boolean-cut plate 9.320/10.478/11.571 ms,
sleeve 3.418/3.649/3.829 ms, elliptic tube 10.531/11.135/11.380 ms,
shared-edge plate 2.831/3.477/4.293 ms, shared-arc notch 3.599/3.658/3.666 ms,
constrained plate 2.228/2.319/2.371 ms, point-reference plate 2.293/2.459/2.504 ms,
tangent-constrained rounded part 4.357/4.488/4.517 ms, angle/fixed slanted part
2.305/2.455/2.521 ms and toroidal seal 77.410/78.681/80.260 ms. All parameter-edit
samples across the thirteen-part catalogue stay below 100 ms in this run. These are local
observations, not universal bounds or an optimization claim. The run follows all builds/tests
and includes parsing, validation, expressions, constraint verification, reference resolution,
hashing, exact geometry, validity and naming. Serialization, IPC and tessellation remain
excluded. Remote CI/review and full Phase 2 sign-off remain pending.

### Open reference sketch rebuild evidence

2026-10-18, Apple M3 Pro / 12 cores / 36 GiB, Release OCCT 7.9.3, 30 samples per mode.
The new 30-feature part has a closed rectangular profile, a separate one-line axis sketch,
a full revolution and 27 transforms. Both sketches use the zx plane. Each unique depth edit
changes the profile width and centerline endpoint; the horizontal constraint is verified before
axis resolution. The result is a sleeve with radii 5/10 mm and height D. Every sample checks
volume `75πD` mm³ within 1e-5, centroid x (including translations) and z within 1e-6 mm,
and kernel/cache counts. Run `build/occt/services/kernel/nf_evaluator_bench`. All 30 samples
of every mode pass.

| Standalone-axis sleeve | Median ms | p95 ms | Max ms | Kernel calls / cache hits |
| --- | ---: | ---: | ---: | ---: |
| Cold | 2.873 | 2.947 | 2.979 | 28 / 0 |
| Profile + centerline endpoint + depth edit | 3.038 | 3.119 | 3.145 | 28 / 0 |
| Last transform edit | 0.594 | 0.617 | 0.628 | 1 / 29 |
| Unused parameter edit | 0.580 | 0.604 | 0.609 | 0 / 30 |

The 96-entry/64 MiB cache budget retains 96 entries / 6,221,388 estimated bytes after depth
edits (not RSS). Same-run depth-edit median/p95/max: plate 2.069/2.135/2.136 ms,
bored plate 8.039/9.056/9.150 ms, boolean-cut plate 8.933/18.198/27.872 ms,
sleeve 3.064/3.156/3.173 ms, closed-sketch-axis cylinder 1.904/1.955/2.026 ms,
elliptic tube 10.252/12.823/14.994 ms, shared-edge plate 2.644/2.716/2.729 ms,
shared-arc notch 3.471/3.635/3.665 ms, constrained plate 1.943/2.004/2.038 ms,
point-reference plate 2.007/2.148/2.162 ms, tangent-constrained rounded part
3.889/4.069/4.080 ms, angle/fixed slanted part 2.007/2.136/2.143 ms and toroidal seal
75.407/76.477/78.021 ms. All parameter-edit samples across the fourteen-part catalogue stay
below 100 ms in this run. These are local observations, not universal bounds or an optimization
claim. The run follows all builds/tests and includes parsing, validation, expressions,
constraint verification, reference resolution, hashing, exact geometry, validity and naming.
Serialization, IPC and tessellation remain excluded. Remote CI/review and full Phase 2
sign-off remain pending.

### Linear body pattern rebuild evidence

2026-10-18, Apple M3 Pro / 12 cores / 36 GiB, Release OCCT 7.9.3, 30 samples per mode.
The new 30-feature part has one sketch, one extrusion, a three-instance linear body pattern
at 15 mm spacing along x, and 27 transforms. Every depth edit rebuilds the extrusion, two
private copies, their union and all transforms. Each sample checks volume `600D` mm³ and
surface area `1200 + 180D` mm² within 1e-5, centroid x (including translations) and z within
1e-6 mm, and kernel/cache counts. D is depth in mm. Run
`build/occt/services/kernel/nf_evaluator_bench`. All 30 samples of every mode pass.

| Three-instance linear pattern | Median ms | p95 ms | Max ms | Kernel calls / cache hits |
| --- | ---: | ---: | ---: | ---: |
| Cold | 5.810 | 5.983 | 5.984 | 31 / 0 |
| Seed depth edit | 6.304 | 6.429 | 6.474 | 31 / 1 |
| Last transform edit | 0.805 | 0.834 | 0.884 | 1 / 29 |
| Unused parameter edit | 0.745 | 0.771 | 0.805 | 0 / 30 |

The 96-entry/64 MiB cache budget retains 96 entries / 31,101,608 estimated bytes after depth
edits, including private copies/history (not RSS). Same-run depth-edit median/p95/max:
plate 1.925/2.022/2.134 ms, bored plate 7.374/7.568/7.588 ms,
boolean-cut plate 7.841/8.073/8.095 ms, sleeve 2.844/2.927/2.932 ms,
closed-sketch-axis cylinder 1.820/1.890/1.894 ms, standalone-axis sleeve 2.830/2.998/3.069 ms,
elliptic tube 9.833/10.136/10.147 ms, shared-edge plate 2.488/2.586/2.655 ms,
shared-arc notch 3.340/3.401/3.413 ms, constrained plate 1.978/2.059/2.091 ms,
point-reference plate 2.005/2.111/2.139 ms, tangent-constrained rounded part
3.987/4.082/4.205 ms, angle/fixed slanted part 2.028/2.155/2.190 ms and toroidal seal
75.553/76.605/76.797 ms. All parameter-edit samples across the fifteen-part catalogue stay
below 100 ms in this run. These are local observations, not universal bounds or an optimization
claim. The run follows all builds/tests and includes parsing, validation, expressions,
constraint verification, reference resolution, hashing, exact geometry, validity and naming.
Serialization, IPC and tessellation remain excluded. Remote CI/review and full Phase 2
sign-off remain pending.
