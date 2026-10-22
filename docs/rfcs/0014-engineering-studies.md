# RFC 0014: Engineering studies in the feature IR

- **Status:** Accepted
- **Author(s):** Claude (agent), for the maintainer
- **Phase:** 3 (Track 4 of RFC 0011, gate G4)
- **Created:** 2026-10-22
- **Decided:** 2026-10-22, by the maintainer ("keep going, use open source. build it."): accepted
  with the physics on MFEM instead of an owned core, and Netgen as the mesher.

## Summary

A part or assembly document gains an optional `studies` section. A study names its physics
(linear static structural, steady thermal, or laminar flow), its domain, materials, boundary
conditions by selector with explicit units, mesh settings, the quantities it reports and
acceptance criteria. `nf study run` and the `run_study` agent tool execute a study out of
process under a job lifecycle. Every result carries the digests of the document, geometry,
mesh, mesher and solver it came from. Everything numerical is open source:

- **Netgen** (LGPL-2.1, dynamically linked, optional) meshes OCCT shapes directly.
- **MFEM** (BSD-3) solves the physics with high-order H1 elements: P2 elasticity and heat,
  and Taylor–Hood P2–P1 Stokes.

Larger physics (contact, nonlinear materials, turbulence, transient) comes later through more
solver adapters, each with its own dependency RFC.

## Motivation

The maintainer asked for AI that can "design/engineer parts, systems, cfd, material
analysis". Gates G1–G3 give an agent geometry, constraints and mechanisms. Nothing yet tells
it whether a part is strong enough, cool enough or flows well enough, and without that it
cannot engineer. Creo and Onshape keep analyses next to the model and re-run them when it
changes (RFC 0011 [S15]). That is the loop to copy: edit, rebuild, re-solve, check criteria.

RFC 0011 left one question open. Its default mesher, fTetWild, is MPL-2.0, and WORKFLOW §8
does not list MPL among allowed or RFC-allowed licenses. This RFC answers it by choosing
Netgen and recording fTetWild as an alternative that needs a policy change.

Doing nothing leaves agents to guess margins from geometry alone, or to call external tools
whose results nobody can check against the document.

## Design

### Study objects

`studies` is an optional array in the document, after `joints`. Each study is evaluated
against the document's built geometry: its bodies, or the placed bodies of an assembly.

```json
{
  "id": "beam_tip_load",
  "kind": "static_structural",
  "domain": {"bodies": ["beam"]},
  "materials": [{"bodies": ["beam"], "material": "aluminium_6061_t6"}],
  "supports": [{"id": "root", "type": "fixed",
                "faces": {"created_by": "beam", "role": "cap_start"}}],
  "loads": [{"id": "tip", "type": "force", "value": ["0 N", "0 N", "-10 N"],
             "faces": {"created_by": "beam", "role": "cap_end"}}],
  "mesh": {"size": "2 mm", "order": 2},
  "outputs": ["max_displacement", "max_von_mises", "reactions"],
  "criteria": [{"quantity": "max_von_mises", "below": "120 MPa"},
               {"quantity": "max_displacement", "below": "1.5 mm"}]
}
```

- **Kinds.** The first slice ships three:
  - `static_structural`: small-strain linear elasticity.
  - `steady_thermal`: conduction with fixed temperature, heat flux and convection boundaries.
  - `laminar_flow`: steady Stokes flow with velocity or flow-rate inlets, pressure outlets
    and no-slip walls.
  Each kind lists the support, load and output types it accepts. Anything else fails
  validation with a JSON Pointer.
- **Domain.** `bodies` (live body ids), `occurrences` (assembly occurrence ids), or for flow,
  the fluid region `cavity_of` a body. A cavity is the closed void the body encloses, built
  by a Boolean, so a pipe's bore becomes the fluid domain.
- **Selectors.** Faces are chosen with the existing selector grammar (RFC 0005 and the
  evaluator). In assemblies an entry adds `occurrence`. Each support or load must match
  at least one face. Selectors resolve against the geometry the study is run on, so a study
  follows parameter edits like any feature.
- **Units.** Every physical value is a string with an explicit unit, checked against the
  field's dimension: force, pressure or stress, temperature (K or °C), heat flux, film
  coefficient, velocity, volumetric flow rate, dynamic viscosity and density. Lengths keep
  the document's units. The expression system gains these dimensions. Bare numbers are
  refused, so "10" never silently means newtons.
- **Materials.** Each entry is either a `material` id from `core/materials/`, or explicit
  values such as `"E": "69 GPa"` and `"nu": 0.33`. `core/materials/materials.json` is
  curated CC0 data. Each value carries its source: density, E, ν, yield strength, thermal
  conductivity, specific heat, and viscosity for fluids. RFC 0013's `density` field remains;
  a `material` on a component or occurrence supersedes it.
- **Outputs and criteria.** Each kind names its quantities:
  - structural: `max_displacement`, `max_von_mises`, `reactions`, `safety_factor` (yield
    over peak von Mises);
  - thermal: `max_temperature`, `min_temperature`, `heat_rate` per boundary;
  - flow: `pressure_drop`, `flow_rate` per boundary, `max_velocity`.

  Criteria compare a quantity `below` or `above` a value. A study passes when every
  criterion holds, and the pass/fail result is part of the study's result, never stored in
  the document.

### Running studies

- **Process.** `nf-study` is a separate executable, the only one that links Netgen. It reads
  a request (the document, study id and limits) and writes JSON lines: `admitted`
  (estimated nodes, DOFs and memory), `progress` (stage, iteration, residual), and `result`
  or `error`. It holds no state between runs. `nf study run <document> <study>` and the
  agent tools launch it. The kernel XPC service and its five-second watchdog are not
  involved. Studies have their own budget, given in the request (default 120 s and 4 GiB),
  and the runner refuses work its admission estimate puts over budget.
- **Stages.**
  1. Build: evaluate the document and resolve the selectors.
  2. Mesh: Netgen on the domain's OCCT solids, with the boundary faces carried through as
     mesh face groups.
  3. Assemble and solve: MFEM.
  4. Post-process: derived quantities and criteria.
  5. Write: the result file and an optional VTU field file.
  Cancellation (SIGTERM, or the tool's `cancel_study`) stops between iterations and writes
  nothing.
- **Provenance.** A result records the SHA-256 of the canonical document, of each domain
  body's geometry hash and of the mesh. It also records the mesher and solver identities
  with their versions and options, the element count and the solver's final residual. The
  digest of all of these keys a result cache next to the document's scratch folder. An
  unchanged study returns its cached result; any input change re-runs it.
- **Convergence.** A `refine: [h1, h2, h3]` option runs the study at three mesh sizes and
  reports each quantity per size with its observed order of convergence. This is how gate
  G4 is measured.

### The physics: MFEM

MFEM v4.10 (BSD-3) is built serially (no MPI, no external solvers) as a shared library. The
study runner keeps it in its own translation units. OCCT's `Handle` macro breaks MFEM's
headers, so OCCT/Netgen and MFEM never share a translation unit, the same rule as OCCT and
OpenUSD. The mesher hands the solver a plain tetrahedral mesh: points, tetrahedra, and
boundary triangles tagged with their B-rep face numbers.

- **Elements.** H1 P2 vector spaces with `ElasticityIntegrator` for structural, P2
  `DiffusionIntegrator` for heat, and Taylor–Hood P2–P1 for Stokes.
- **Solvers.** PCG with a Gauss–Seidel smoother for the SPD problems, and MINRES with a
  block-diagonal preconditioner for Stokes. Iteration stops at a relative residual of 1e-12,
  and a solve that misses it is an error, never a silent result. Algebraic multigrid
  (hypre, Apache-2.0/MIT, which needs an MPI build) is the follow-up when meshes outgrow it.
- **Boundary conditions.**
  - Fixed supports and prescribed temperatures or velocities are imposed strongly.
  - Forces, pressures, fluxes and convection are boundary integrals over the selected faces'
    triangles.
  - A flow rate becomes a parabolic profile scaled to the requested flux on a circular
    inlet.
- **Derived quantities.** Reactions come from the assembled residual on constrained DOFs,
  and their balance against the applied loads is reported. Stress and flux come from the
  solution gradient. Pressure drop is the difference of area-averaged boundary pressures.

**Spike, 2026-10-22.** An OCCT 200 × 10 × 10 mm aluminium box, fixed at one end with 10 N at
the tip, meshed by Netgen through nglib and solved by MFEM with P2 elasticity:

| Max element size | Unknowns | Tip deflection error vs PL³/3EI | Solve time |
| --- | --- | --- | --- |
| 5 mm | 15,045 | −0.49% | 4.1 s |
| 3.3 mm | 17,853 | −0.32% | 3.7 s |
| 2.2 mm | 47,820 | −0.29% | 15.5 s |

All three are inside the 2% gate. The −0.29% limit is real physics that beam theory leaves
out, mostly the clamped root resisting lateral contraction.

Integration notes from the spike:
- Netgen's geometry must be constructed inside the library, through nglib's
  `Ng_OCC_Load_BREP` with the shape written by `BRepTools::Write`. Constructing
  `OCCGeometry` in client code fails a `dynamic_cast` across the dylib boundary.
- Releasing nglib's mesh aborted in the spike, so the runner, a short-lived process,
  releases nothing until exit. The cause is a follow-up.
- MFEM reorients Netgen's tetrahedra on finalize.

### Solver adapters

`nf-study` defines a solver interface. It takes the mesh, the materials and the resolved
boundary conditions, and returns fields and quantities. MFEM is its first implementation.
Later adapters each need their own dependency RFC, and are listed here so the interface is
shaped for them:
- FEBio (MIT) for contact and nonlinear solids;
- SU2 (LGPL-2.1, dynamic, optional) for RANS flow;
- PyFR (BSD-3, Metal) for high-order flow.

### Meshing

Netgen (NGSolve/netgen, LGPL-2.1, v6.2.2604) is built without its GUI or Python, against
the pinned OCCT, as a shared library, the same way OCCT is. It meshes the domain's
`TopoDS_Shape` directly, honours face groups, and produces curved second-order tetrahedra.
`nf-study` is the only binary that links it, and Netgen is an optional build component.
Without it, `run_study` returns `mesher_unavailable` and the rest of Nightfall is unchanged.
A `third_party/README.md` row and a build script follow the OCCT pattern.

### Agent tools

- `list_studies` returns each study with its last cached result.
- `run_study {id, refine?, budget_s?}` runs a study and returns the result, or a job id
  when it exceeds the tool's wait.
- `study_status {job}` and `cancel_study {job}` manage running jobs.
- `study_results {id}` returns quantities, criteria and provenance, plus summary statistics
  per face group for the agent to reason about.

`patch_part` edits studies like any other section. The evaluation suite gains study tasks:
- "make this bracket pass a 150 MPa stress criterion without exceeding 40 g";
- "size the fins so the plate stays below 80 °C".

They pass on measured results, not on the agent's claims.

## Alternatives considered

- **fTetWild (MPL-2.0) as the mesher.** It is very robust on dirty input and runs well as a
  separate process. But MPL-2.0 is not on the §8 list. Adopting it needs the maintainer to
  add MPL-2.0 under "allowed with an RFC" for unmodified, separately built tools. It also
  meshes triangle soups rather than B-reps, so curved boundaries become facets, which costs
  accuracy on the Poiseuille gate. It is kept as the first fallback if Netgen's build proves
  impractical.
- **Gmsh, TetGen, CGAL.** These are GPL or AGPL and are not allowed (§8).
- **An owned Rust finite-element core.** The first draft of this RFC proposed one, to stay
  dependency-free. The maintainer chose open source instead: MFEM is mature and verified,
  and the spike shows it meets the gate on the first try. MFEM's serial build takes 67 s on
  ten cores and needs no other dependency.
- **External solvers driven by input files only (CalculiX, OpenFOAM).** These are GPL.
  Whether a user-installed GPL binary driven by files is acceptable is a policy question
  this RFC doesn't answer. Nothing here depends on it.
- **Mesh-free or surrogate methods.** These are fast, but neither can be verified against
  analytic cases to 2%. Surrogates stay behind a provider interface (RFC 0011 Track 5).

## Compatibility and migration

`studies` is an optional section. IR v2 (RFC 0013) has not been released, so studies join
v2 before its first release, and v1 documents migrate unchanged. If v2 has shipped by the
time this lands, studies become v3, with a pure migration that changes only `version`.
Results live in a cache outside the document, so documents stay small and diffable. The
expression system's new dimensions are additive. No existing `IKernel` method changes. The
mesher reads OCCT shapes inside `nf-study`, outside the kernel service.

## Testing and measurement

Gate G4 from RFC 0011, each case at three mesh refinements with its observed convergence
order reported:

- **Cantilever.**
  - Setup: an aluminium beam, 200 × 10 × 10 mm, fixed at one end with a 10 N tip load.
  - Pass: tip deflection within 2% of PL³/3EI. At this slenderness, shear deformation adds
    under 0.3%.
  - Also: root stress within 2% of 6PL/bh² away from the support's singular corners.
  - Also: reactions balance the load within 1e-9 relative.
- **Thermal slab.**
  - Setup: 100 × 100 × 10 mm, 400 K and 300 K on the large faces.
  - Pass: the heat rate kAΔT/t within 2%. P2 elements reproduce the linear field exactly,
    so the test also checks to 1e-9.
  - Also: the energy balance between the two faces within 1e-9 relative.
- **Poiseuille.**
  - Setup: water in a 10 mm bore, 100 mm long, at a given flow rate.
  - Pass: pressure drop within 2% of 8μLQ/πR⁴.
  - Also: the flow rate out equals the flow rate in within 1e-9 relative.
- **Provenance.** Every result carries the document, geometry, mesh, mesher and solver
  digests. Re-running an unchanged study returns the cached result, and editing a
  dimension re-runs it.
- **Failure modes.** These must each give a named error with a pointer:
  - a selector that matches nothing;
  - a load on a face outside the domain;
  - a missing material;
  - a value with the wrong unit;
  - an over-budget study;
  - a solver that does not converge.
- **Agents.** Study tasks in the evaluation suite pass on measured results.

## Risks and open questions

- **Netgen on macOS arm64 against OCCT 7.9.3.** Netgen's OCC layer follows recent OCCT, and
  pinning both may need a patch. The fallback is fTetWild, after a §8 decision.
- **Solver scale.** Serial PCG with Gauss–Seidel took 15.5 s at 48k unknowns in the spike.
  Hypre's algebraic multigrid needs MFEM's MPI build and is the planned next step.
- **Singularities.** Re-entrant corners and point supports make peak stress mesh-dependent.
  `max_von_mises` therefore reports the convergence trend, and criteria can exclude regions
  near supports. Agents must be told this in the tool description.
- **Units in the expression system.** New dimensions touch RFC 0005's expression rules. This
  RFC proposes the table. Their exact grammar is reviewed with the first implementation PR.
- **Materials data.** No open engineering-materials dataset exists, so the curated file
  starts small: common aluminium, steel, stainless, ABS, PLA, nylon, copper, water and air.
  Every value has a cited source, and its extent is a review item.
