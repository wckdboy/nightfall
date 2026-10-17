# RFC 0001: Geometry kernel selection and bake-off

- **Status:** Accepted — **OpenCASCADE is the primary kernel**
- **Author(s):** wckdboy
- **Phase:** 0
- **Created:** 2026-10-16
- **Decided:** 2026-10-17, by the decision rule below on the full bake-off; the maintainer
  waived the WORKFLOW §3 review period

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
2. **200 STEP models** sampled from the ABC dataset, stratified by face count: single-solid
   models from chunk 0000, five face-count quintile bins of 40
   (`tools/bakeoff/abc/manifest-v1.csv`). Both
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

## Result and decision

Full bake-off, 2026-10-17 (`tools/bakeoff/results/2026-10-17-full.json`, #40): the 20-part
reference corpus plus the ABC corpus under the F13 protocol (197 models scored, 3 excluded
because they failed translation into SMLib). Scores follow the normalization above;
`run_bakeoff.py --rescore` recomputes them from the raw results.

| Metric (weight) | OCCT 7.9.3 | SMLib 1.0.0 |
| --- | --- | --- |
| Operation success (30 %) | 83.3 | 68.2 |
| Correctness (20 %) | 100 | 71.1 |
| Operation history (20 %) | 50 (faces + edges through booleans, faces only through fillets) | 0 (no history API) |
| Fillet and blend quality (10 %) | 54.2 | 10.9 |
| Performance (10 %) | 100 | 49.8 |
| Integration cost (10 %) | 70 | 100 |
| **Weighted score** | **77.4** | **50.7** |
| Memory-safety errors / 1,000 sanitized operations | 0 | 250 |
| UB reports / 1,000 sanitized operations | 50 | 50 |

ABC operations valid: import 197 / 197 on both; drill (the 167 models it actually cuts) OCCT
162, SMLib 161; round (fillet every
edge at 0.5 % of the diagonal) **OCCT 105, SMLib 18** (OCCT crashed once, isolated by the
per-process runner). Imported volumes agree within 1e-6 on 161 of 197 models.

**What the "round" step measured (found in review).** 170 of SMLib's 179 failed rounds return
`SM_ERR_INVALID_INPUT`: `SmApiFilletEdges` rejects the *whole* call when any selected edge is
smooth (its faces meet within ~2° of tangent, `SmEdge::GetBlendEdgeuse`), and ABC parts are
full of smooth edges (existing fillets, seams). At one fifth of the radius the outcomes are
unchanged on both kernels, so radius is not the cause. The step therefore measures how each
kernel handles an "every edge" request, not fillet geometry; SMLib's fillet strength on
sharp edges is not established by it. **The decision does not depend on it:** crediting
SMLib a valid round on every ABC model still gives 68.9 vs 77.4 = 89.0 %, below 90 %.

**Not run:** the stress cases listed under Corpus (item 3, from SMLib's KNOWN_ISSUES) were not
run as a separate set; the reference corpus covers fillets next to caps (rounded box) and
closed surfaces (sphere, torus, cylinders), and SMLIB-001 is reported separately.

**Decision:** both kernels meet the hard requirements, but SMLib's score is **65.5 %** of
OCCT's, below the 90 % the decision rule requires, and stays below it under the most
SMLib-favourable reading above. **OpenCASCADE 7.9.3 is Nightfall's primary kernel.** SMLib
is re-evaluated at the next usd-brep release. The gaps that would have to close are an
operation-history API, area accuracy on curved faces, speed, SMLIB-001, and the fillet API's
all-or-nothing rejection of smooth edges.

Consequences:

- `IKernel` (a later RFC) is designed against OCCT first; the bake-off interface stays the
  template, and the SMLib driver keeps building so a re-evaluation is one command.
- RFC 0002 still holds: usd-brep's `.brep` → USD bridge reads **OCCT** shapes, so exact-solid
  storage in USD works with OCCT as the kernel.
- OCCT is LGPL-2.1: dynamic linking only, and the Mac App Store question in the build plan
  is open again (release-engineering RFC).
- Operation history is "partial" on OCCT: fillet edge provenance must come from our own
  tagging (as the build plan's tag-and-match fallback anticipated).

## Findings so far

Recorded as they come in; scoring waits for the full bake-off (#7).

| # | Date | Finding | Affects |
| --- | --- | --- | --- |
| F1 | 2026-10-16 | Measured on the unmodified pinned commit `c9c5979f`. SMLib + SM_API build on macOS arm64 with plain CMake (C++17), no premake, Packman, TBB or USD. 30 s on 10 cores; `libSMLib.dylib` 9.7 MB, `libSM_API.dylib` 0.2 MB. | Integration cost (positive) |
| F2 | 2026-10-16 | Upstream SM_API suite passes unchanged: 115/115 in 25 s (release, M-series). | Operation success (baseline) |
| F3 | 2026-10-16 | Heap-use-after-free in every boolean (difference, union, intersection) whose operands have closed edges, such as a box minus a cylinder: a closed edge is listed twice on its vertex but unlinked once. Box-only booleans are clean. Root cause, repro and candidate fix: [SMLIB-001](../known-issues/SMLIB-001-proto-topology-use-after-free.md). Not in upstream KNOWN_ISSUES.md; not reported upstream (maintainer decision), deferred. | Correctness, robustness |
| F4 | 2026-10-16 | usd-brep has no STEP or IGES reader. `OCCT_BREP_IMPORT` / `OCCT_BREP_EXPORT` bridge OpenCASCADE's native `.brep` format (`BRepTools::Read`, `BinTools::Read`). STEP into SMLib therefore means OCCT reads STEP, then the shape crosses via `.brep`: OCCT stays in our stack either way. | Integration cost, licensing |
| F5 | 2026-10-16 | SM_API's fillet supports a blend-curve cross-section with G2/G3 continuity (`SmApiFilletEdges`, `lXSectType = 2`); per the header, three-edge corner boundaries are only guaranteed G1. OCCT has no curvature-continuous blend; its driver refuses `kG2Blend`. To be exercised by the corpus (#17). | Fillet and blend quality |
| F6 | 2026-10-16 | SM_API clamps requested area accuracy to [1e-4, 1e-1] relative (volume: [1e-8, 1e-1]), and on curved faces misses even that: the sphere's area is off by 1.62e-4 when 1e-4 is requested (identical at 1e-5 and 1e-8). `SmApiBrepComputeMassProperties` gives 8.6e-5 on the sphere but 1.27e-6 on the plate with hole, so neither API reaches 1e-6. First harness run (6 analytic parts): every SMLib volume within 4.3e-9, but the sphere and plate-with-hole areas fail the 1e-6 bar, so SMLib passes 4/6 vs OCCT 6/6. The scoring rule is unchanged; recorded so the result is read correctly. | Correctness |
| F7 | 2026-10-16 | SM_API exposes no operation history (which output face came from which input or operation). Provenance exists only inside the fillet, sweep and offset modules. History support: **no** at the API level, pending a look at whether those internals can be surfaced. | Operation history |
| F8 | 2026-10-16 | SMLib merges coplanar faces after a union (two cubes sharing a face: 6 faces); OCCT leaves them split (10 faces) unless unified explicitly. | Integration cost (positive) |
| F9 | 2026-10-16 | `SmApiBrepIsManifoldSolid` reports two disjoint boxes as one manifold solid. `SmApiBrepMaterialCensus` counts material regions and void cavities separately (disjoint boxes: 2 solids; box minus an inner box: 1 solid, 1 void), so the driver requires 1 solid and 0 voids. SMLib's topology is region-based (a box has 2 shells), so shell counts are not a substitute. | Integration cost |
| F10 | 2026-10-17 | 14 analytic parts (#17, first half): OCCT 14/14; SMLib 8/14 with **every volume within 1e-6 and every result valid**. All six SMLib failures are area on parts with cylindrical or spherical faces: sphere 1.6e-4, hemisphere 1.8e-5, hole grid 4.4e-5, counterbore 3.4e-6, plate with hole 1.2e-6, Steinmetz 1.1e-6. The rounded box (cylindrical fillets, spherical corners) passes only marginally (area 9.0e-7, OCCT exact), and Steinmetz fails at 1.08e-6: around the 1e-6 bar SMLib's area pass/fail is effectively noise. Consistent with F6. | Correctness |
| F11 | 2026-10-17 | 20-part corpus complete (#28 adds cone, torus, extruded profile, helix sweep, variable-radius fillet): OCCT 19/19 analytic parts, SMLib 9/19, plus 1 consensus part (both built valid; compared across kernels in #7, not counted as a pass). New SMLib misses: torus area 2.2e-4, knob area 5.4e-5, frustum area 3.6e-6, and the helical spring's **volume** 6.7e-6 (area 2.1e-5). The spring's volume miss needs a path ~0.002 mm short, far more than the helix fit's 1e-6 mm, so the likely cause is the pipe sweep's looser internal surface tolerance (`SmPrimitiveCreation::CreatePipeSweep`); not yet isolated. The 408-sided extruded gear is exact on both. Variable-radius fillet (consensus part): volumes differ by 8.8e-5 (OCCT 5971.571, SMLib 5972.099) because **the radius laws differ**: SMLib is consistent with linear (within 4e-7 of the quarter-circle estimate; a numerically integrated linear rolling-ball fillet gives ≈ 5972.15), while OCCT's two-radius `Add(R1, R2, E)` interpolates with zero end slopes (contact radius 1.39 / 2.00 / 2.61 at the quarter points vs 1.5 / 2.0 / 2.5 linear). A linear OCCT law (`Law_Linear`) needs care: a naive attempt crashed. | Correctness, fillet quality |
| F12 | 2026-10-17 | Reference-corpus run (`tools/bakeoff/results/2026-10-17-corpus.json`, #36). Method: every part in its own process (crashes become results), timing inside that process after one untimed warm-up (median of 5; separate processes timed library loading instead), two processes per part to catch flakiness, one sanitizer run with **both kernels instrumented**. **Operation history is measured, not declared:** a probe traces every result face and edge through `Modified`/`Generated`; OCCT traces all faces and edges through a boolean but only faces through a fillet (11 edges untraced), so it scores **partial (50)**; SMLib exposes no history API (0). **Sanitizers:** parts with an ASan report OCCT 0/20, SMLib 5/20 (heap-use-after-free in every part that cuts a cylinder: SMLIB-001) = 0 vs 250 memory errors per 1,000 operations; UBSan in 1/20 each (variable fillet: OCCT misaligned construction in `NCollection_OccAllocator`, SMLib invalid enum load). Caveats: OCCT-asan is a Release build and pools its allocations, so its counts are a lower bound. **Corpus-only weighted scores (ABC still pending, so not the decision): OCCT 87.0, SMLib 69.1 (79 % of OCCT; the rule needs ≥ 90 %).** Driven by history (50 vs 0), correctness (100 vs 71.1) and performance (100 vs 48.5; per-part geometric-mean time ratio SMLib ≈ 4.2×). Integration cost uses cold CI builds (OCCT 31 min → 70, SMLib → 100). Unit: one reference part = one operation script; the consensus part is excluded from correctness (two kernels cannot settle it). | All |
| F13 | 2026-10-17 | **ABC protocol, fixed before any ABC modeling result exists** (only the RFC 0002 translation spike has run). Per model, three operations on both kernels: (1) *import* — OCCT reads the STEP directly; SMLib gets STEP → OCCT → `.brep` → USD → SMLib (healer off); (2) *drill* — boolean difference with a cylinder along Z through the bounding-box centre, radius 10 % of the smaller of the X/Y extents, spanning the box with 1 mm margin; **amended after the first run (review finding):** drills that would miss the part (OCCT finds no volume removed at plan time) are dropped for both kernels, because a no-op is not an operation; (3) *round* — fillet every edge, radius 0.5 % of the bounding-box diagonal. Parameters are computed **once** from OCCT's STEP import and committed (`tools/bakeoff/abc/ops-v1.csv`), so both kernels get identical inputs. A model that fails *import* on either kernel is excluded from both kernels' modeling scores and the count is published (RFC 0002). ABC results have no analytic values: they count toward operation success and fillet quality, not performance (SMLib's timings would include the STEP → OCCT → USD translation that OCCT does not pay); volume agreement between the kernels is reported but not scored (two kernels cannot say which is right). | Corpus definition |

Because of F3, the harness also runs both kernels under ASan/UBSan, and memory-safety
errors per 1,000 operations are reported next to the weighted score.

## Risks and open questions

- Does SMLib expose operation history? If not, provenance needs tag-and-match.
- ~~Does SMLib include a STEP reader, or only the USD path?~~ Neither: STEP must go through OCCT and the `.brep` bridge (F4).
- ~~ABC dataset licensing for redistribution of the sampled models~~ Resolved: each model's
  copyright stays with its creator (the ABC site points to Onshape Terms of Use 1.g.ii), so
  only the manifest is committed (`tools/bakeoff/abc/`, #18).
