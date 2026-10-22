# RFC 0011: The AI design loop program

- **Status:** Accepted
- **Author(s):** The Nightfall Authors (agent-authored proposal at the maintainer's request)
- **Phase:** 3 (proposed; this RFC defines its gates)
- **Created:** 2026-10-22
- **Issue:** [#248](https://github.com/wckdboy/nightfall/issues/248)
- **Decided:** 2026-10-22 by the maintainer ("accepted, waive the wait, keep building"),
  who waived the WORKFLOW §3 review period.

## Summary

Keep the accepted foundation (OCCT behind XPC, the JSON feature IR with selectors, the Rust
policy layer) and put the **agent loop** on the critical path. Five tracks, in dependency
order: (1) `nf mcp`, a headless agent surface over the existing evaluator with few general
tools and factual evidence; (2) a permissive Rust 2D sketch solver; (3) assemblies in IR v2
with frame-based joints, a small SE(3) solver, interference and URDF export; (4) engineering
studies as IR objects over out-of-process permissive solvers with analytic gates and a
curated materials dataset; (5) kernel and client work that follows, never blocks, the loop.
The FreeCAD fork is retired as a product foundation and kept as an external oracle. This
RFC supersedes the direction of the Review proposals RFC 0009 (#229) and RFC 0010 (#247).

## Motivation

The maintainer's goal is AI that designs and engineers parts, systems and assemblies and
runs structural, fluid and material analysis, natively and fast. After 55 commits and
~53,000 lines, Nightfall has a strong headless kernel and no loop: no agent surface, no
sketch solver, no assemblies, no studies, and a client that only edits parameters of
pre-authored parts. The direction changed three times in three days (RFC 0008, the
kernel-first RFC 0009, the FreeCAD-fork RFC 0010). A survey of the field on 2026-10-22
settles the open questions:

- **Kernel.** No pure-Rust B-rep kernel has production fillets: Fornjot is archived
  (2026-06), solidcore is about a third ported with no booleans, truck needs vendored fixes.
  usd-brep logged 24 bugs in two weeks and accepts no contributions. OCCT 8.0.1 (2026-07)
  adds typed-ID `BRepGraph`, faster booleans and thread-safe fillets. RFC 0008's split
  (Rust owns policy, OCCT solves) is the shape every credible project converges on [S1–S4].
- **Forks.** Hard FreeCAD forks end as patch donors (LinkStage3) or shut down (Ondsel);
  upstream rejects large AI-written rewrites. A fork costs Apache-2.0, pins OCCT 7.8.1 and
  a Qt stack opposed to "speed and simplicity", and its AI surface would be the FreeCAD
  MCP that already exists. GUI-driving agents reach 17.5% on CADWorld [S5–S7].
- **What works for agents.** The vendors with real agent traction (Zoo/KCL, Onshape
  FeatureScript MCP) let the agent edit the textual source of truth. Benchmarks show that
  structured programs beat token sequences, structured error feedback lifts executability
  from 0.70 to 0.97 while screenshots add 5–7 points, and execution success is not
  correctness: 64% of "successful" edits in BenchCAD silently corrupt unrelated features.
  Verification must compare measured geometry [S8–S12].
- **Reference implementation.** SolidCraft (Apache-2.0/MIT, pure Rust, created 2026-10-07)
  ships a constraint solver, timeline, joints with motion studies and contact sets,
  interference, URDF, STEP, an MCP server with batch rollback and 83 analytic oracle cases,
  built largely by agents under three rules: everything is a command, never crash, thin
  swappable UI. It lacks a robust kernel; Nightfall lacks everything above one [S13].

Doing nothing keeps kernel evidence growing while the product the maintainer asked for
does not exist.

## Design

### Foundation decisions

| Decision | Choice |
| --- | --- |
| Product foundation | Nightfall's own stack: IR (RFC 0005), XPC kernel service (RFC 0003), Rust policy layer (RFC 0008), Swift/Metal client. |
| FreeCAD | External oracle only: `tools/freecad-oracle/` runs the official binary as a separate process to compute reference values for tests. Never linked, vendored or shipped. `wckdboy/nightfall-freecad` is not the product. |
| Kernel | OCCT stays. An OCCT 8.0.1 upgrade gets its own RFC after Track 1 ships. SMLib stays buildable for re-evaluation. |
| Rust migration | Continues as RFC 0008 slices behind the tracks below; it never blocks them. The agent layer is ported with the engine cutover (RFC 0008 G5). |
| Architecture rules adopted | Everything user-visible or agent-visible is a command with a JSON schema; no panics or uncaught exceptions reach a session (a failed command leaves the document unchanged); the client is a thin consumer of the same commands. |
| Verification | Measured geometry (volume, area, centroid, bounds, topology counts, selector resolution) against analytic or independent-oracle values, never rebuild status alone. |

### Track 1: `nf mcp`, the agent surface

A stdio [Model Context Protocol](https://modelcontextprotocol.io) server (JSON-RPC 2.0, one
message per line, protocol revisions 2025-06-18, 2025-03-26 and 2024-11-05) added to the
existing C++ `nf` CLI as `nf mcp [--in part.nightfall] [--allow-path DIR ...]`. stdout
carries protocol messages only; stderr carries diagnostics.

**Why C++ in `tools/cli`:** the IR evaluator is C++ until RFC 0008 G5, the CLI already links
it, and `core/ir` already has the strict JSON reader/writer. A std-only Rust server would
duplicate that JSON code and link the C++ bridge for no capability gain today.

**Session.** One in-process document per server: the authoritative state is the IR source
text. Every mutating tool validates, rebuilds through `kernel_service::Evaluator` and
replies with the same evidence record `nf build` writes (version 1: digests, kernel
identity, units, tolerances, feature status counts, cache activity, per-body metrics,
timings). A failed tool leaves the document unchanged. An undo stack of source texts gives
`undo`/`redo`.

**Tools (v1).** Few and general; the model edits the document, not one tool per feature.

| Tool | Arguments | Reply | Mutates |
| --- | --- | --- | --- |
| `inspect_part` | — | units, params, features (id, type, name, suppressed, status, errors), bodies with metrics, `document_sha256`, kernel identity | no |
| `get_schema` | — | `part-v1.schema.json` and the feature type list | no |
| `measure` | `body?` | volume mm³, area mm², centroid, bounds, solids/faces/edges, name records | no |
| `set_parameters` | `values: {name: expression}` | rebuild evidence, `changed_bodies` | yes |
| `patch_part` | `patch: [RFC 6902 ops]` | rebuild evidence, `changed_bodies`; on error, the errors with JSON Pointers and expression offsets | yes |
| `replace_part` | `document: string` | as above; whole-document replacement | yes |
| `undo`, `redo` | — | evidence of the restored state | yes |
| `open_part`, `save_part` | `path` | digests | file |
| `export_step` | `path`, `bodies?` | bytes, SHA-256 | file |

`changed_bodies` lists bodies whose state hash differs from the previous build; bodies
that vanished or appeared are listed separately. Errors are returned as tool results with
`isError: true` and the existing `{code, message, pointer, offset}` entries, so the model
can localize and repair. Unknown tools and malformed arguments are JSON-RPC `-32602`.

**Files.** Tool calls read and write only inside the folder of the `--in` document, the
server's private scratch folder, and each `--allow-path`; hidden entries are refused,
paths are resolved before comparison. Outputs are published atomically as `nf build` does.

**Budgets.** The 16 MiB document limit and the evaluator's cache limits apply. v1 evaluates
in-process without a deadline, as `nf build` does; the host's tool timeout bounds it.
Routing through the XPC service and its watchdog is a v2 item once shared-memory transport
lands (RFC 0003).

**Later in this track.** `body_topology` (faces with name, type, centroid, normal, axis and
a picking point; edges with midpoint and length) once the kernel exposes per-face queries
through the C ABI; `screenshot` through the client's renderer; `batch` with rollback.

**Agent evaluation suite** (`agent/evals/`, WORKFLOW §6): tasks that build the 20 reference
parts from text, apply named edits, and repair injected faults; scored on measured geometry
against goldens, selector stability and silent corruption of unrelated features. The pass
rate may not drop on any prompt, tool or provider change.

### Track 2: sketch constraint solver

Adopt a permissive-license Rust 2D solver as the owner of constraint satisfaction; the
model proposes relations, the solver satisfies them. Candidates: **ezpz** (MIT, shipped in
Zoo Design Studio 1.2) by default, SolidCraft's `sketch` crate (Apache-2.0/MIT) as the
alternative. A measured spike on the RFC 0005 constraint set decides; the pick is recorded
in a dependency RFC with a `third_party/README.md` row. Requirements: all v1 constraint
kinds, degrees-of-freedom report, conflicting and redundant subsets named, deterministic
branch under parameter edits, no panics on hostile input. planegcs (LGPL) and libslvs
(GPL) are excluded. Integration: `core/sketch/rust`, called by the evaluator before profile
construction; stored coordinates are the solver's starting point, as RFC 0005 anticipates.

### Track 3: assemblies (IR v2)

Model after Onshape mate connectors: a **frame** is a point plus orthonormal axes derived
from selected geometry by fixed rules (planar face: origin at centroid, Z along the normal;
cylindrical face: origin on the axis, Z along it; vertex, edge midpoint, datum). One
**joint** per pair of frames carries its degrees of freedom: `rigid`, `revolute`,
`slider`, `cylindrical`, `planar`, `ball`, with limits and a driven `value` expression.

- `components`: part definitions, inline feature lists or a reference to another
  `.nightfall` by content hash; `occurrences`: id, component, placement, `grounded`;
  `joints`: two frames (occurrence + selector + frame rule), kind, limits, value.
- Solver: Levenberg–Marquardt over SE(3) in a Rust crate with analytic Jacobians; reports
  residuals, rank and degrees of freedom, closed-loop closure, and refuses silently
  reassigning a lost frame. No new dependency.
- Queries: interference (OCCT common volume), clearance (distance), mass properties with
  materials; motion study as joint values over steps with collision at each step.
- Exports: STEP with assembly structure; URDF with per-link meshes, mass and inertia so
  MuJoCo, Chrono, Isaac Sim or Gazebo can test the mechanism.
- Format: version 1 → 2 with a pure migration (a v1 part becomes one component with one
  grounded occurrence). Studies (Track 4) ride the same bump.

### Track 4: engineering studies

Copy Creo's behavioural modelling: measurements and acceptance checks live in the document
and re-solve on rebuild. A `study` names its domain (bodies or occurrences), materials,
boundary conditions by selector with explicit SI units, mesh settings, requested
quantities and acceptance criteria. `nf study run` executes it out of process under a job
lifecycle (admit, progress, cancel, checkpoint, result provenance) separate from the
five-second interactive watchdog.

Solver adapters are a trait with one implementation per tool; each is its own dependency
RFC. Under WORKFLOW §8 the default stack is: **fTetWild** (MPL-2.0) for volume meshes with
Manifold (Apache-2.0) for cleanup; **FEBio** (MIT) for nonlinear structural with contact,
and MFEM (BSD-3) when the physics should be owned code; **SU2** (LGPL-2.1, dynamic,
optional) for RANS and PyFR (BSD-2, native Metal) for high-order flow; **Chrono** (BSD-3)
and MuJoCo (Apache-2.0) for multibody. GPL tools (Gmsh, CalculiX, OpenFOAM) are never
linked; whether a user-installed binary driven by files is acceptable is a policy decision
recorded in its RFC, not assumed. Materials: no open engineering dataset exists, so
`core/materials/` ships a curated CC0 JSON (density, E, ν, yield, k, Cp) with a source per
value. Gates per regime are analytic first (cantilever, plate, thermal slab, Poiseuille)
across three mesh refinements with conservation and convergence reported.

### Track 5: kernel and client

OCCT 8.0.1 upgrade RFC (BRepGraph typed IDs as a selector substrate, parallel fillets).
The client adopts the command registry and gains interactive sketching once Track 2 lands.
Surrogate models (NVIDIA PhysicsNeMo checkpoints are the only open pretrained CFD ones)
enter only behind a provider interface, like model providers.

### Phase 3 gates

| Gate | Evidence |
| --- | --- |
| G1 Loop | An agent builds all 20 reference parts from text through `nf mcp`; every body within 1e-6 relative of its golden volume and area; the eval suite reports zero silent changes to unrelated features. |
| G2 Sketch | The v1 constraint corpus solves with reported DOF; conflicting and redundant sets named; no panic under fuzzing. |
| G3 Assembly | Four-bar and slider-crank close with residuals < 1e-9 mm/rad; a six-joint arm exports URDF that loads and moves in MuJoCo with matching joint values. |
| G4 Study | A cantilever and a thermal slab within 2% of analytic values on three refinements; Poiseuille pressure drop within 2%; every result carries mesh, solver and source digests. |

## Alternatives considered

- **FreeCAD as foundation (RFC 0010):** fastest to a demo of its FEM and Assembly
  workbenches, but see Motivation: license, stack, fork history, undifferentiated AI surface.
- **Fork SolidCraft:** its kernel (truck) lacks production fillets and its policy forbids
  reading LGPL code, so an OCCT backend cannot go upstream. Reuse its permissive crates and
  its process; do not fork.
- **DSL-first (KCL style):** Nightfall Script (RFC 0006, in review) stays a front end
  compiled to IR; the loop that matters is write, run, read errors, measure, revise, which
  JSON plus JSON Patch provides today.
- **GUI-driving agents:** 17.5% on CADWorld; structured IR access is the differentiator.
- **Wait for the Rust engine cutover:** it does not add any loop capability.

## Compatibility and migration

Track 1 changes no persisted format and adds the `nf mcp` surface. Tracks 3 and 4 bump
the document version to 2 with a pure migration and fixture tests; v1 files keep loading.
RFC 0009 and 0010 are Review proposals in open PRs; this RFC supersedes their direction
and neither becomes Accepted. Accepted RFCs 0001–0008 are unchanged.

## Testing and measurement

Each tool has a schema test and an end-to-end test through a spawned `nf mcp` process over
the reference parts; mutating tools are tested for unchanged-on-failure. The eval suite
runs in CI on `agent/` changes. Tracks 2–4 carry the gates above plus fuzzing of every
command with hostile input. Timings are reported per tool.

## Risks and open questions

- v1 evaluation runs in-process without a deadline; a hung kernel hangs the tool call until
  the host times out. Routing through XPC fixes this and is scheduled, not assumed.
- Models fail most on sweep, loft and shell; the eval suite must weight those.
- SolidCraft is two weeks old and moving fast; pin exact commits when reusing crates.
- The assembly solver is owned code; closed loops and singular configurations need their
  own adversarial corpus.
- GPL-by-process policy is undecided (#194); the default stack avoids needing it.

## Sources

- [S1] OCCT 8.0.1 release: https://github.com/Open-Cascade-SAS/OCCT/releases/tag/V8.0.1
- [S2] Fornjot archived: https://github.com/hannobraun/fornjot
- [S3] solidcore status: https://github.com/tangency-labs/solidcore
- [S4] usd-brep issues: https://github.com/NVIDIA-Omniverse/usd-brep/issues
- [S5] Ondsel goodbye: https://www.ondsel.com/blog/goodbye/
- [S6] FreeCAD PR #32814 (semantic ID, closed): https://github.com/FreeCAD/FreeCAD/pull/32814
- [S7] CADWorld: https://arxiv.org/html/2609.16251
- [S8] Onshape FeatureScript MCP: https://www.onshape.com/en/blog/featurescript-mcp-server-enables-text-code-cad
- [S9] Zookeeper: https://zoo.dev/research/zookeeper
- [S10] Text2CAD-Bench: https://arxiv.org/html/2605.18430v1
- [S11] 3DCodeBench: https://arxiv.org/html/2606.01057v1
- [S12] BenchCAD: https://arxiv.org/html/2605.10865v1
- [S13] SolidCraft: https://github.com/tangency-labs/solidcraft (docs/mcp.md, ROADMAP.md)
- [S14] Onshape mate connectors: https://www.onshape.com/en/blog/build-assemblies-faster-mate-connectors
- [S15] Creo feasibility studies: https://support.ptc.com/help/creo/creo_plus/usascii/model_analysis/bemod/To_Perform_a_Feasibility_Study.html
- [S16] ezpz: https://github.com/KittyCAD/ezpz
- [S17] fTetWild: https://github.com/wildmeshing/fTetWild ; FEBio: https://github.com/febiosoftware/FEBio ; MFEM: https://mfem.org ; SU2: https://su2code.github.io ; PyFR: https://pyfr.org ; Chrono: https://projectchrono.org
