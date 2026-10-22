# Phase 3 — The AI design loop

- **Status:** Proposed by [RFC 0011](../rfcs/0011-ai-design-loop.md) (Review); activates
  on acceptance. Phase 2's open gate items and failures stay recorded in
  [phase-2.md](phase-2.md) and are not transferred.
- **Milestone:** `Phase 3` on GitHub (to be created on acceptance)

## Gate

All of these, with evidence linked:

- [ ] **G1 Loop:** an agent builds all 20 reference parts from text through `nf mcp`; every
      body within 1e-6 relative of its golden volume and area; the eval suite reports zero
      silent changes to unrelated features
- [ ] **G2 Sketch:** the v1 constraint corpus solves with reported degrees of freedom;
      conflicting and redundant sets are named; no panic under fuzzing
- [ ] **G3 Assembly:** four-bar and slider-crank close with residuals < 1e-9 mm/rad; a
      six-joint arm exports URDF that loads and moves in MuJoCo with matching joint values
- [ ] **G4 Study:** cantilever and thermal slab within 2% of analytic values on three mesh
      refinements; Poiseuille pressure drop within 2%; every result carries mesh, solver and
      source digests

## Work items (build order)

1. `nf mcp` v1: transport, session, `inspect_part`, `get_schema`, `measure`,
   `set_parameters`, `replace_part`, `undo`/`redo`, `open_part`/`save_part`, `export_step`
2. `patch_part` (RFC 6902) and `changed_bodies`
3. `agent/evals/` suite over the 20 reference parts; CI job
4. Sketch solver spike (ezpz vs SolidCraft `sketch`), dependency RFC, `core/sketch/rust`
5. IR v2 RFC: components, occurrences, frames, joints, studies; migration
6. SE(3) joint solver crate; interference and clearance queries; URDF export
7. Study runner and job lifecycle; first adapters (fTetWild, FEBio) under their RFCs
8. OCCT 8.0.1 upgrade RFC

## Evidence (unmerged branches)

**G1, 2026-10-22.** Claude Code headless (Opus 5.5, the account default) with `nf mcp` as
its only tool and no access to goldens: **60/60 tasks pass** (build from the description
20/20, edit one parameter 20/20, repair a part that fails to build 20/20). Every saved part
measures within 1e-6 relative of the golden in volume and area with equal topology counts,
and no edit added or removed a body. Median 7 turns and 23 s per task, $25.17 for all 60.
The first pass stopped at the account's session limit after 43 tasks; those 17 sessions
never started and were rerun with `--resume`, which also taught the harness to report
such sessions as not run. The deterministic reference runner passes 60/60 in CTest.
Branches: #253 (`patch_part`), the evals branch (#254), `export_mesh` (#255),
`body_topology` (#256). Report and per-task documents: `out/evals-claude-2026-10-22/`.
The gate box stays unticked until those PRs merge under an Accepted RFC 0011.

## Log

- 2026-10-22: RFC 0011 drafted (#248). First implementation slice tracked separately and
  held as a draft PR until acceptance.
