# Phase 3 — The AI design loop

- **Status:** Active since 2026-10-22, when the maintainer accepted
  [RFC 0011](../rfcs/0011-ai-design-loop.md) and waived the review period. Phase 2's open
  gate items and failures stay recorded in [phase-2.md](phase-2.md) and are not transferred.
- **Milestone:** [`Phase 3`](https://github.com/wckdboy/nightfall/milestone/4) on GitHub

## Gate

All of these, with evidence linked:

- [ ] **G1 Loop:** an agent builds all 20 reference parts from text through `nf mcp`; every
      body within 1e-6 relative of its golden volume and area (the golden is the reference
      part built by `nf build`, so the elliptic duct's area needs no closed form); the eval
      suite reports zero silent changes to unrelated features
- [ ] **G2 Sketch:** the v1 constraint corpus solves with reported degrees of freedom;
      conflicting and redundant sets are named; no panic under fuzzing
- [ ] **G3 Assembly:** four-bar and slider-crank close with residuals < 1e-9 mm/rad; a
      six-joint arm exports URDF that loads and moves in MuJoCo with matching joint values
- [ ] **G4 Study:** cantilever and thermal slab within 2% of analytic values on three mesh
      refinements; Poiseuille pressure drop within 2%; every result carries the document,
      geometry, mesh, mesher and solver digests (cases and criteria: RFC 0014)

## Work items (build order)

1. `nf mcp` v1: transport, session, `inspect_part`, `get_schema`, `measure`,
   `set_parameters`, `replace_part`, `undo`/`redo`, `open_part`/`save_part`, `export_step`
2. `patch_part` (RFC 6902) and `changed_bodies`
3. `agent/evals/` suite over the 20 reference parts; CI job
4. Sketch solver: [RFC 0012](../rfcs/0012-sketch-solver.md) (decision rule and spike protocol), the spike, `core/sketch/rust`
5. IR v2: [RFC 0013](../rfcs/0013-assemblies.md) (components, occurrences, frame joints, queries, URDF); studies get their own RFC
6. SE(3) joint solver crate; interference and clearance queries; URDF export
7. Study runner and job lifecycle; Netgen meshing and MFEM physics under [RFC 0014](../rfcs/0014-engineering-studies.md)
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

**G3, 2026-10-22.** Closure: a four-bar and a slider-crank built from real solids close at
their closed-form positions with every joint residual at most 1e-9 mm and 1e-9 rad, the
slider-crank over nine crank angles in a full turn (#277); the Strandbeest showcase, 37
revolute joints in 12 loops, puts every toe on Jansen's closed-form path within 1e-9 mm over
a full turn (#284). Export: the six-joint reference arm (`examples/assemblies/arm.nightfall`)
exports URDF through `nf mcp`; forward kinematics from the URDF alone reproduces every part's
solved placement at four poses within 3.8e-15 m (`cli.urdf_export`), and MuJoCo 3.16 loads the
file and, driven to the same joint values, matches within 3.8e-15 m (local run; MuJoCo is
not a dependency). Interference, clearance, motion sweeps and mass properties are agent
tools (#286, #289); the first motion study found and fixed a pin collision in the
Strandbeest. Branches: #276, #277, #278, #283, #284, #286, #289, #291 and the URDF PR. The box
stays unticked until they merge.

## Log

- 2026-10-22: G3 met on unmerged branches: assemblies evaluate, drive and solve on real
  solids; interference, clearance, motion sweep, mass properties and URDF export are `nf
  mcp` tools; a solver fix (#291) refuses flipped parts and seeds far drives.

- 2026-10-22: RFC 0011 drafted (#248) and accepted the same day; RFCs 0012 (#257) and 0013
  (#262) accepted with it.
- 2026-10-22: RFC 0011 drafted (#248). First implementation slice tracked separately and
  held as a draft PR until acceptance.
