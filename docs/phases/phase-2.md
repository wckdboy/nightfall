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
| [#90](https://github.com/wckdboy/nightfall/issues/90) | 2.1 IR schema v1, validation and migrations | ir | In progress: JSON I/O and document envelope |
| [#91](https://github.com/wckdboy/nightfall/issues/91) | 2.2 Expression engine with units, bounds and dependency graph | ir |  |
| [#92](https://github.com/wckdboy/nightfall/issues/92) | 2.3 Evaluator: feature hashing, incremental rebuild, cache | kernel |  |
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
