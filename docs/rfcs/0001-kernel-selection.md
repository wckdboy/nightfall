# RFC 0001: Geometry kernel selection and bake-off

- **Status:** Draft
- **Author(s):** wckdboy
- **Phase:** 0
- **Created:** 2026-10-16
- **Decided:** —

## Summary

Choose Nightfall's primary B-rep kernel by measurement, not reputation. SMLib (released
as [usd-brep](https://github.com/NVIDIA-Omniverse/usd-brep) 1.0.0 under Apache-2.0 on
2026-10-08) is the lead candidate; OpenCASCADE 7.8 (LGPL-2.1) is the comparison and
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
| SMLib (usd-brep) | 1.0.0, pinned commit | Apache-2.0 | NURBS-native (NLib); macOS build needs a self-built premake |
| OpenCASCADE | 7.8.x | LGPL-2.1 | Mature; STEP/IGES translators |

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
   translation, unless SMLib turns out to have its own STEP reader.
3. **Stress cases** from SMLib's `KNOWN_ISSUES.md` (closed-surface seams, fillets near
   caps), to measure how often they hit in practice.

### Metrics and weights

| Metric | Weight | Measured as |
| --- | --- | --- |
| Operation success | 30% | Share of operations producing a valid, manifold solid |
| Correctness | 20% | Volume/area within 1e-6 relative of analytic or consensus values |
| Operation history | 20% | Can we tell, for every output face/edge, which input or operation produced it? (yes / partial / no) |
| Fillet and blend quality | 10% | Success on the fillet set; continuity checks on results |
| Performance | 10% | Median and p95 time per operation on an M1 |
| Integration cost | 10% | Build friction on macOS, API clarity, binary size |

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

## Risks and open questions

- Does SMLib expose operation history? If not, provenance needs tag-and-match.
- Does SMLib include a STEP reader, or only the USD path?
- ABC dataset licensing for redistribution of the sampled models: commit only the
  manifest (IDs and hashes), not the files, until confirmed.
