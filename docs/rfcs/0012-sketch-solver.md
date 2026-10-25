# RFC 0012: A permissive Rust 2D sketch constraint solver

- **Status:** Accepted — adopt SolidCraft's `sketch` crate under the conditions in Result
- **Author(s):** The Nightfall Authors (agent-authored proposal)
- **Phase:** 3, Track 2 of RFC 0011
- **Created:** 2026-10-22
- **Issue:** [#257](https://github.com/wckdboy/nightfall/issues/257)
- **Decided:** 2026-10-22 by the maintainer, who waived the WORKFLOW §3 review period.

**Depends on:** RFC 0005 (v1 sketch constraints are in the schema; "once the solver lands,
stored coordinates become its starting point") and RFC 0011 (the solver owns constraint
satisfaction; the agent proposes relations).

## Summary

Adopt one permissive-license Rust 2D constraint solver behind a Nightfall crate,
`nightfall-sketch` in `core/sketch/rust`, and have the evaluator solve every sketch before
profile construction. Two candidates qualify: **ezpz** (MIT, KittyCAD, shipped in Zoo Design
Studio 1.2) and **SolidCraft's `sketch` crate** (Apache-2.0 OR MIT, tangency-labs). This RFC
fixes the decision rule, the spike corpus and the metrics before the spike runs, as RFC 0001
did for kernels; the measured result is appended and decides. No dependency is added by this
RFC.

## Motivation

RFC 0005 stored constraints in v1 so the format would not change when a solver arrived; the
evaluator today only verifies them against authored coordinates and reports a violated
constraint as an error. An agent therefore has to compute coordinates itself, which is the
failure mode the research in RFC 0011 shows most clearly: the high-validity results
(ReliCAD, CAD-Assistant, Fusion AutoConstrain) all come from a deterministic solver owning
constraint satisfaction while the model proposes relations. Gate G2 of Phase 3 needs it,
interactive sketching in the client needs it, and assemblies (Track 3) reuse its diagnostics
vocabulary (degrees of freedom, conflicting and redundant sets).

Writing our own solver is the alternative SolidCraft took; it cost them a 23,000-line crate.
Two permissive solvers now exist, so the question is which one, under what rule.

## Design

### Requirements

1. **Coverage** of the RFC 0005 v1 set: coincident, horizontal, vertical, parallel,
   perpendicular, tangent, equal, symmetric, midpoint, fixed, and the dimensional distance,
   angle and radius. A kind may be mapped onto the solver's primitives (parallel as "lines at
   angle 0") when the mapping is exact.
2. **Diagnostics** an agent and a person can act on: the sketch's degrees of freedom, which
   entities are under-constrained, which constraints conflict, which are redundant.
3. **Continuity**: editing a dimension keeps the solution branch (no mirrored or flipped
   sketch) and a drag re-solves from the current coordinates.
4. **Robustness**: no panic on hostile input; failure is a value. Determinism across runs.
5. **Footprint**: the dependency closure is small enough to vendor, review and pin under the
   locked/offline Cargo policy (RFC 0008); `forbid(unsafe_code)` in our crate.
6. **Licensing hygiene**: a complete license text and notices for every transitive crate.

### The candidates, as surveyed on 2026-10-22 (read-only; nothing built)

| | ezpz 0.2.29 (commit `915882c`, 2026-08-05) | SolidCraft `sketch` (commit `6f5d003`, 2026-10-22) |
| --- | --- | --- |
| License | MIT; the LICENSE file is the unfilled template (`[year] [fullname]`), to be raised upstream before adoption | Apache-2.0 OR MIT (workspace) |
| Size | ~18,500 lines in the core crate | ~23,000 lines including tests |
| Constraint kinds | 25: coincident, horizontal, vertical, fixed, distance (also H/V, point–line), lines at angle, points at angle, equal length, line–circle and circle–circle tangent, circle and arc radius, arc length and angle, midpoint, symmetric, point on arc, scalar equality | 32: coincident, point on curve, horizontal, vertical, parallel, perpendicular, collinear, tangent, equal, concentric, midpoint, symmetric, fix, distance (also X/Y, point–line, line–circle), length, line angle, radius, diameter, angle, arc sweep and length, smooth, slot length, offset distance, pattern kinds |
| v1 coverage | 13/13 by mapping (parallel/perpendicular via lines-at-angle) | 13/13 directly |
| Method | sparse least squares on `faer`, priorities per constraint | Levenberg–Marquardt per connected component, finite-difference Jacobians, rank analysis by Gaussian elimination, constructive seeding that keeps the prior branch |
| Diagnostics | `solve_analysis` → under-constrained variables; conflicts as a failure outcome | `SolveReport` with failing (conflict) and redundant constraint ids, freedom analysis, `solve_after_edit` continuity |
| Dependencies | `faer` (sparse linear algebra), `indexmap`, `libm`, `thiserror`, `winnow`, `mutants`, plus transitive | `solidcraft-geom` (internal, must be extracted), `serde`, `thiserror`, `ttf-parser` (sketch text) |
| Safety | not surveyed | `forbid(unsafe_code)`, `deny(unwrap_used, panic, …)` |
| Maintenance | releases on crates.io, used in production by Zoo | moves daily; no crates.io release; extraction from the workspace needed |
| Tooling | CLI with a text problem format, benchmarks, fuzz target | in-repo tests per topic |

Neither is disqualified. ezpz is the smaller, released, independently usable library with
a heavier numerical dependency; SolidCraft's crate has richer diagnostics and continuity
semantics but is coupled to its workspace and moving fast.

### Decision rule (fixed before the spike)

The spike runs both solvers on the same corpus through thin adapters in
`tools/sketch-spike/` (temporary scaffolding, like `tools/bakeoff/`). Scores:

| Metric | Weight | Measurement |
| --- | --- | --- |
| Correctness | 30% | Residual of every satisfied constraint ≤ 1e-9 mm or rad on the corpus; share of cases solved |
| Diagnostics | 25% | Degrees of freedom equal the analytic count; conflicting subsets and redundant constraints named exactly (score per case) |
| Continuity | 15% | Dimension-edit sequences keep the branch (no reflection or arc flip); drags re-solve from current coordinates |
| Robustness | 10% | Fuzzing of the adapter input for 10 minutes: zero panics, zero hangs beyond 1 s |
| Performance | 10% | Median solve of a 200-constraint sketch on Apple Silicon, release build; 100 × fastest ÷ this |
| Footprint and hygiene | 10% | Transitive crate count and lines to vendor, license completeness, `unsafe` count |

Corpus: the sketches of the 20 reference parts with their constraints made explicit; the
constraint kinds of RFC 0005 each in a minimal and an adversarial case; over-constrained,
redundant and inconsistent sets; four-bar and slot sketches for branch continuity. The
corpus is committed with analytic expectations before results exist. The higher weighted
score is adopted; within 5 points, the smaller dependency closure wins.

### Integration

- Crate `nightfall-sketch` (`core/sketch/rust`, `forbid(unsafe_code)`): input is the RFC
  0005 sketch (entities with ids, constraints with ids, authored coordinates as the start);
  output is solved coordinates plus a report `{dof, underconstrained: [entity ids],
  conflicting: [constraint ids], redundant: [constraint ids], iterations}`. The chosen
  solver is an implementation detail behind this API; the adapter is the only place its
  types appear.
- The evaluator calls it before profile construction and builds geometry from the solved
  coordinates. A sketch with conflicting constraints is a feature error naming them, as
  today; an under-constrained sketch still builds and reports its freedom.
- Authored coordinates are the starting point (RFC 0005). On commit, `nf mcp` and the client
  write the solved coordinates back, so a saved document is self-consistent and `nf build`
  of it reproduces the same solve from a converged start. This writes only numbers the
  solver produced; a document is never rewritten on read. A sketch with any coordinate
  authored as an expression is not rewritten: writing its numbers alone would move the next
  solve's start and the source would drift on every rebuild. It is solved from the same
  authored start each time instead, and its expression entities are reported.
- `inspect_part` adds per-sketch `dof`, `underconstrained`, `conflicting` and `redundant`,
  which is what an agent proposing a relation needs to see next.
- Dependencies: the chosen crate at an exact version with its transitive closure vendored
  under `third_party/` with every license text, recorded in `third_party/README.md`;
  `cargo` stays locked and offline. If ezpz is chosen, its license template is fixed
  upstream first or the correct MIT text is obtained from the maintainers in writing.

## Alternatives considered

- **planegcs** (LGPL-2.1, FreeCAD): mature, but LGPL and C++; a dynamically linked optional
  solver would sit in the kernel service, against the Rust direction of RFC 0008.
- **libslvs** (GPL-3.0, SolveSpace): excluded by WORKFLOW §8.
- **Own solver**: SolidCraft's 23,000 lines say what it costs; not before the two permissive
  options have been measured.
- **No solver, agents compute coordinates**: the measured failure mode (RFC 0011 sources).

## Compatibility and migration

The document format does not change: v1 already stores constraints. The evaluation of
sketches with constraints changes from "verify or error" to "solve", which RFC 0005 called
out as the one v1 feature whose evaluation changes later. Documents without constraints
evaluate exactly as before. Written-back coordinates differ from authored ones only where
the authored sketch was not already satisfied.

## Testing and measurement

Gate G2 (RFC 0011): the corpus solves with reported degrees of freedom; conflicting and
redundant sets are named; no panic under fuzzing. Plus differential checks between the two
candidates where both solve, and the spike results appended to this RFC with raw JSON under
`tools/sketch-spike/results/`.

## Result (2026-10-22)

The spike ran on Apple M3 Pro, macOS 27.2, rustc 1.99.1, release builds, with both
candidates at the pinned commits above. Scaffolding and raw results:
[`tools/sketch-spike/`](../../tools/sketch-spike/).

| Metric (weight) | ezpz | SolidCraft `sketch` |
| --- | --- | --- |
| Correctness (30) | 100: all 13 cases solved to 1e-6 mm | 100 |
| Diagnostics (25) | 46: flags under-constrained variables but gives no DOF count; reports a conflict as a least-squares compromise with 4 unsatisfied equations; no redundancy report | 100: exact DOF, refuses the conflict and names 2 failing and 1 redundant constraint, names the redundant parallel |
| Continuity (15) | 100: the 3-4-5 branch survives the dimension edit | 100 |
| Robustness (10) | 100: 400 randomized runs, no crash or hang | 100 |
| Performance (10) | 100: 0.106 ms median for 203 constraints | 26: 0.410 ms |
| Footprint and hygiene (10) | 21: 54 crates, ~114,000 lines with `faer`; LICENSE is the unfilled MIT template | 100: 12 crates, ~38,000 lines; Apache-2.0 OR MIT; `forbid(unsafe_code)` |
| **Weighted** | **78.7** | **92.6** |

**Decision by the rule: SolidCraft's `sketch` crate.** The margin is 13.9 points, outside
the 5-point tie band. It does not depend on the footprint sub-scores: without that metric
the totals are 76.6 and 82.6, still beyond the band. ezpz is about four times faster, but
both solve 203 constraints in well under a millisecond, far inside an interactive frame.
What decides is diagnostics: SolidCraft refuses a contradiction and names it, while ezpz
silently returns a compromise, which is exactly the behavior an agent cannot act on.

Deviations from the protocol, stated so the result can be weighed:

- The footprint sub-weights (50 for crate count, 30 for vendored lines, 20 for license
  completeness) were set at scoring time; the protocol named the inputs but not the split.
  The decision holds without the metric, as shown above.
- Robustness used 400 randomized variants per solver instead of 10 minutes of fuzzing.
- Continuity is one dimension-edit pair; drags and arc flips are not yet in the corpus.
- The corpus is the 13 committed synthetic cases. The constrained sketches of the 20
  reference parts and the four-bar and slot sequences were not in it.
- Correctness compared solved coordinates to the expectations at 1e-6 mm; it did not check
  each constraint's residual against 1e-9.
- Diagnostics compared the counts of failing and redundant constraints, not their ids.

The last two matter for the decision only if SolidCraft would have lost points on them.
`nightfall-sketch` closes them in its own tests: every residual within 1e-9 mm or rad, and
redundant and conflicting constraints reported by id (G2).

Conditions for adoption, carried into the dependency entry:

1. Vendor `sketch` and `geom` at the pinned commit under `third_party/` with their license
   texts, as `nightfall-sketch`'s only solver dependency; pin `serde` and `thiserror`.
2. Exclude sketch text: its embedded fonts are OFL-1.1, which WORKFLOW §8 does not list.
   The solver does not use the `text` or `fonts` modules; the vendored copy drops them and
   `ttf-parser`, leaving 10 crates. Sketch text, if wanted later, needs its own decision on
   OFL fonts.
3. Track upstream by pinned commit and carry Nightfall changes as patches, as with OCCT.

## Risks and open questions

- ezpz's `faer` dependency is large; the vendored closure may be bigger than the solver.
- SolidCraft's crate changes daily; pinning a commit and extracting it from the workspace
  is work, and upstreaming fixes may not match their process.
- Writing solved coordinates back changes file bytes on save; the diff is numeric only.
- Rank analysis with finite-difference Jacobians can misreport degrees of freedom near
  singular configurations; the corpus includes such cases deliberately.
