# Phase 2 — Feature IR + Script

- **Weeks:** 8–16 (build plan)
- **Milestone:** `Phase 2` on GitHub
- **Status:** In progress

## Gate

All of these, with evidence linked:

- [ ] 20 reference parts (gear, bracket, enclosure, …) build headlessly in CI
- [ ] Changing one parameter rebuilds a 30-feature part in < 100 ms
- [ ] IR documents validate against schema v1 and migrate forward; ASan/UBSan clean

## Work items (build order)

Tracked as GitHub issues in the [`Phase 2` milestone](https://github.com/wckdboy/nightfall/milestone/3).

| Issue | Item | Scope | Status |
| --- | --- | --- | --- |
| [#89](https://github.com/wckdboy/nightfall/issues/89) | 2.0 RFC 0005: feature IR schema v1 | ir | Done (RFC 0005) |
| [#90](https://github.com/wckdboy/nightfall/issues/90) | 2.1 IR schema v1, validation and migrations | ir | Schema, document I/O and reference validation implemented; review pending |
| [#91](https://github.com/wckdboy/nightfall/issues/91) | 2.2 Expression engine with units, bounds and dependency graph | ir | Expressions, dependency graph and bounds implemented; review pending |
| [#92](https://github.com/wckdboy/nightfall/issues/92) | 2.3 Evaluator: feature hashing, incremental rebuild, cache | kernel | Typed inputs, content hashes and bounded cache implemented; production evaluator pending |
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
