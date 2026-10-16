# RFC 0001: Geometry kernel selection and bake-off

- **Status:** Draft
- **Author(s):** wckdboy
- **Phase:** 0
- **Created:** 2026-10-16
- **Decided:** —

## Summary

Choose Nightfall's primary B-rep kernel by measurement, not reputation. SMLib (released
as [usd-brep](https://github.com/NVIDIA-Omniverse/usd-brep) 1.0.0 under Apache-2.0 on
2026-10-08) is the lead candidate; OpenCASCADE 7.9.3 (LGPL-2.1) is the comparison and
fallback. Both run the same corpus through a thin harness; this RFC fixes the corpus,
metrics and decision rule before any results exist, so the result can't be argued after
the fact.

## Motivation

The kernel decides model quality (fillets, booleans, healing), licensing (App Store,
Apache-2.0 distribution) and how stable face references can be. Changing it after
Phase 3 means redoing every feature, so it must be decided by the Phase 0 gate.

## Design

### Candidates

| Kernel | Version | License | Notes |
| --- | --- | --- | --- |
| SMLib (usd-brep) | 1.0.0, pinned commit | Apache-2.0 | NURBS-native (NLib); built with our own CMake (F1) |
| OpenCASCADE | 7.9.3, pinned commit | LGPL-2.1 | Mature; STEP/IGES translators. Latest release of the 7.x line; 8.0.x exists and can be evaluated separately |

### Harness

`tools/bakeoff/` holds a small C++ driver per kernel behind one interface: load or build a
model, run an operation script, then report validity, volume, area, topology counts,
timings and history information as JSON. The harness is temporary scaffolding; the real
`IKernel` comes in a later RFC informed by what we learn here.

### Corpus

1. **20 reference parts** built from operation scripts: brackets, enclosures with shells,
   filleted blocks (constant radius, varying radius, vertex blends), gears from
   involute curves, sweeps along helices, booleans with coincident faces.
2. **200 STEP models** sampled from the ABC dataset, stratified by face count. Both
   kernels import through OCCT's STEP reader, so the comparison is about modeling, not
   translation. usd-brep has no STEP reader (F4): models reach SMLib as OCCT shapes
   through its `.brep` bridge.
3. **Stress cases** from SMLib's `KNOWN_ISSUES.md` (closed-surface seams, fillets near
   caps), to measure how often they hit in practice.

### Metrics and weights

Every metric is normalized to a 0–100 score with the formula in its row, so the weighted
score is reproducible from the raw JSON results:

| Metric | Weight | Raw measurement | Normalized score (0–100) |
| --- | --- | --- | --- |
| Operation success | 30% | Operations producing a valid, manifold solid | 100 × successes ÷ operations attempted |
| Correctness | 20% | Volume and area vs analytic or consensus values | 100 × results within 1e-6 relative ÷ results measured |
| Operation history | 20% | Can every output face/edge be traced to the input or operation that produced it? | yes = 100, partial (faces only, or some operations) = 50, no = 0 |
| Fillet and blend quality | 10% | Fillet/chamfer subset of the corpus | 100 × valid fillet results ÷ fillet operations attempted |
| Performance | 10% | Median time per operation over the shared successful operations (M-series Mac, release build) | 100 × fastest kernel's median ÷ this kernel's median |
| Integration cost | 10% | Three pass/fail checks | 40 if it builds from unmodified upstream sources + 30 if the CI build takes ≤ 15 min + 30 if shipped binaries total ≤ 50 MB |

Weighted score = Σ (weight × normalized score). p95 timings, memory-safety errors and
binary sizes are reported alongside but are not part of the score.

Hard requirements, regardless of score: builds reproducibly on Apple Silicon in CI, and
has a license compatible with Apache-2.0 distribution.

### Decision rule

SMLib becomes the primary kernel if it meets the hard requirements and its weighted
score is at least 90% of OCCT's. The 10% margin is the price we accept for Apache-2.0
licensing and native NURBS/USD. Otherwise OCCT is primary and SMLib is re-evaluated at
the next NVIDIA release.

## Alternatives considered

- **Parasolid:** best-in-class, but a commercial license conflicts with "open by default."
  Revisit only if both open kernels fail the fillet bar.
- **solidcore** (Rust port of SMLib, CAD subset): too young to evaluate as primary; we
  track it.
- **Own kernel:** years of work; not before the MVP.

## Compatibility and migration

Nothing persisted exists yet. The `IKernel` boundary (later RFC) keeps a second backend
possible.

## Testing and measurement

The harness, corpus manifest and raw JSON results are committed under `tools/bakeoff/`.
Results and the final decision are appended to this RFC and summarized in
`docs/phases/phase-0.md`.

## Findings so far

Recorded as they come in; scoring waits for the full bake-off (#7).

| # | Date | Finding | Affects |
| --- | --- | --- | --- |
| F1 | 2026-10-16 | Measured on the unmodified pinned commit `c9c5979f`. SMLib + SM_API build on macOS arm64 with plain CMake (C++17), no premake, Packman, TBB or USD. 30 s on 10 cores; `libSMLib.dylib` 9.7 MB, `libSM_API.dylib` 0.2 MB. | Integration cost (positive) |
| F2 | 2026-10-16 | Upstream SM_API suite passes unchanged: 115/115 in 25 s (release, M-series). | Operation success (baseline) |
| F3 | 2026-10-16 | Heap-use-after-free in every boolean (difference, union, intersection) whose operands have closed edges, such as a box minus a cylinder: a closed edge is listed twice on its vertex but unlinked once. Box-only booleans are clean. Root cause, repro and candidate fix: [SMLIB-001](../known-issues/SMLIB-001-proto-topology-use-after-free.md). Not in upstream KNOWN_ISSUES.md; not reported upstream (maintainer decision), deferred. | Correctness, robustness |
| F4 | 2026-10-16 | usd-brep has no STEP or IGES reader. `OCCT_BREP_IMPORT` / `OCCT_BREP_EXPORT` bridge OpenCASCADE's native `.brep` format (`BRepTools::Read`, `BinTools::Read`). STEP into SMLib therefore means OCCT reads STEP, then the shape crosses via `.brep`: OCCT stays in our stack either way. | Integration cost, licensing |
| F5 | 2026-10-16 | SM_API's fillet supports a blend-curve cross-section with G2/G3 continuity (`SmApiFilletEdges`, `lXSectType = 2`). OCCT has no curvature-continuous blend; its driver refuses `kG2Blend`. To be exercised by the corpus (#17). | Fillet and blend quality |
| F6 | 2026-10-16 | SM_API clamps requested area accuracy to [1e-4, 1e-1] relative (volume: [1e-8, 1e-1]). First harness run (6 analytic parts): every SMLib volume within 4.3e-9 of the analytic value, but area errors of 1.6e-4 (sphere) and 1.2e-6 (plate with hole) fail the 1e-6 bar, so SMLib passes 4/6 vs OCCT 6/6. The scoring rule is unchanged; this is recorded so the result is read correctly. | Correctness |
| F7 | 2026-10-16 | SM_API exposes no operation history (which output face came from which input or operation). Provenance exists only inside the fillet, sweep and offset modules. History support: **no** at the API level, pending a look at whether those internals can be surfaced. | Operation history |
| F8 | 2026-10-16 | SMLib merges coplanar faces after a union (two coincident cubes: 6 faces); OCCT leaves them split (10 faces) unless unified explicitly. | Integration cost (positive) |
| F9 | 2026-10-16 | `SmApiBrepIsManifoldSolid` reports two disjoint boxes as one manifold solid. SMLib's topology is region-based (each closed surface has a shell on both sides: a box has 2 shells), so "one solid" must be checked as one bounded region via `SmBrep::GetRegions`, outside SM_API. The harness applies the same rule to both kernels. | Integration cost |

Because of F3, the harness also runs both kernels under ASan/UBSan, and memory-safety
errors per 1,000 operations are reported next to the weighted score.

## Risks and open questions

- Does SMLib expose operation history? If not, provenance needs tag-and-match.
- ~~Does SMLib include a STEP reader, or only the USD path?~~ Neither: STEP must go through OCCT and the `.brep` bridge (F4).
- ABC dataset licensing for redistribution of the sampled models: commit only the
  manifest (IDs and hashes), not the files, until confirmed.
