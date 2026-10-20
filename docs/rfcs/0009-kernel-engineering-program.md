# RFC 0009: Kernel-first geometry, assemblies and simulation program

- **Status:** Review
- **Author(s):** The Nightfall Authors (agent-authored proposal)
- **Phase:** 2 (program definition; later implementation gates proposed below)
- **Created:** 2026-10-20
- **Issue:** [#226](https://github.com/wckdboy/nightfall/issues/226)

## Summary

Make Nightfall a headless, open-source engineering engine: industrial-quality exact
geometry, persistent references, assemblies, mechanical simulation and CFD. Prioritize
kernel correctness and numerical evidence; defer new UI/UX and showcase presentation.
Keep the current application as an existing client and test consumer. Rust remains the
implementation language for new Nightfall kernel/engine code under RFC 0008.

This is a proposed program, not a claim of commercial parity or approval of every future
API, format, dependency or worker. Each new boundary needs its own concrete Accepted RFC.
Existing Phase 2 gates remain binding until this proposal is accepted. The maintainer's
immediate instruction to stop new UI work applies while the proposal is reviewed.

## Motivation

The maintainer requests an open-source engine at the level associated with Parasolid,
NX, Dassault's CAD systems and Ansys, explicitly including assemblies, simulation and
CFD. That spans several numerical systems. Parasolid's geometry remit includes solids,
sheets and hybrid representations [S1]; assembly and analysis capabilities need their
own contracts above geometry. A large rendered part does not establish those contracts.

Current evidence at integration candidate `d287f14`:

- C++/OCCT implements exact solid operations, immutable snapshots, operation history,
  semantic selectors, incremental evaluation, derived surface meshes and STEP exchange.
  Loft/sweep and blend support have deliberately bounded domains.
- Rust implements checked B-spline/rational curve math and safe typed extrusion,
  placement, booleans, mesh readers and STEP export over the native solver. Rust
  surfaces, IR/evaluation/naming cutover and an independent Rust B-rep solver are pending.
- RFC 0007 already rejects ambiguous selectors and incomplete naming history. Tests
  cover producer rebinding, retained readers and upstream eviction; this is a foundation
  for persistent topology, not evidence that every topology-changing edit is solved.
- The full local integration gate passes **884/884 in 250.54 s**; scoped native
  ASan/UBSan passes **564/564 in 203.51 s**. Rust and installed OCCT are uninstrumented.
  All integration CI passes; PR #224 is squash-merged as `069b49e`. #185 latency and
  #195 diagnostics stay open.
- Placed component solids exist. Assembly constraints, dynamics, volume analysis meshes,
  structural/thermal solvers and CFD are not implemented. #169 and #194 record ambitions.

## Design

### Shared contracts and dependency order

Use separate, independently testable numerical layers with one revision/provenance chain:

1. Geometry: units, tolerances, curves/surfaces, B-rep, operations and lineage.
2. Model intent: feature graph, persistent selectors and reproducible transactions.
3. Assemblies: definitions, occurrences, joints, constraints, kinematics and contact pairs.
4. Analysis preparation: regions, materials, idealization, BCs and quality-controlled meshes.
5. Solvers: mechanics, heat, fluids and explicit coupling contracts.
6. Execution/evidence: bounded jobs, checkpoints, results, convergence and reproducibility.

Geometry/model intent underpin both assemblies and analysis preparation; those tracks
can advance independently once their prerequisites pass. Rendering is an optional consumer.
Physics results never become authored CAD geometry implicitly. Carry geometry revision,
occurrence path, region reference and study/mesh/solver identities through each transition.

Keep the accepted Rust math/native/kernel/IR/engine boundaries. Native OCCT is initially
both production solver and a differential reference. Own Rust algorithms replace it only
after independent accuracy, topology, history and performance gates. A native operation
wrapped in Rust counts as interface parity, not replacement of that geometric algorithm.

### 1. Topological Naming and persistent semantic references — first priority

Treat three identities separately: immutable snapshot handles; authored feature/role
identity; and a persistent reference expressing intent across revisions. UUIDs alone do
not explain whether one face became two, two merged or the intended region disappeared.
Operation history and recomputation are central to OCCT's naming model too [S2].

Require explicit lineage for unchanged, generated, modified, split, merged and deleted
entities. References combine source feature/sketch roles and occurrence context with
explicit selection/cardinality intent. Adjacency and geometric predicates may constrain
provenance candidates; neither topology order nor nearest geometry silently supplies it.
Symmetric, equally plausible candidates must remain ambiguous. Deletion and missing
history are explicit failures; a fallback cannot attach a mate or load to another face.

Define single-descendant versus region/all-descendant semantics before persisting them.
For example, splitting a pressure wall might preserve a declared surface region, while
splitting a face used for a unique mate can invalidate that mate. Both need inspectable
resolution evidence. Do not infer either policy from what makes the solver run.

Test cuts crossing seams; coplanar face merge/split; holes appearing/disappearing;
fillet/chamfer replacement; symmetric faces; mirrored/patterned instances; suppression;
feature-ID rebinding; reorderings; cold/cache/evicted execution; save/reload and undo.
Check intended ancestry and selected geometry, not just stable counts or equal volume.
Cache reuse must reproduce the same reference result or documented ambiguity/deletion.
Import without authored lineage cannot promise the original semantic references.

### 2. Numerical robustness, units and tolerance accounting

Separate geometric admissibility, angular, parameter-space, intersection, meshing and
solver tolerances. Define model scale and supported coordinate/feature-size ranges.
Track conditioning and error growth rather than enlarging one global epsilon until a
model succeeds. Preserve explicit CAD mm/radian units; convert analysis quantities to a
declared unit system, including density, pressure, stiffness, heat and inertia.

Use filtered/adaptive predicates where sign decisions control topology [S3]. Distinguish
exact predicates from exact constructions: robust orientation does not solve a general
NURBS intersection. Establish analytic and high-precision or interval-based oracles for
ill-conditioned cases. Test far-from-origin models, scale transformations, very thin
walls, near tangencies, clustered knots and extreme weights within declared limits.

### 3. Analytic geometry, NURBS and intersections

Complete checked rational surfaces, derivatives, normals, curvature and bounds; exact
analytic constructions; knot insertion/splitting and degree changes; projection and
closest points; curve/curve, curve/surface and surface/surface intersections. Handle
periodicity, poles, repeated knots, singular parameterizations, overlaps and tangencies.

An intersection needs connected branches, isolated/tangent points, parameter ranges,
3D curves and matching trimming curves on both surfaces, with measured error bounds.
Tracing alone can miss a branch. A general intersection of rational surfaces need not
have a finite exact NURBS representation: retain analytic support where possible and
state approximation/certification bounds where required. Define what “exact CAD” means
without claiming floating-point constructions are symbolic exact arithmetic.

### 4. B-rep data structure, validity and solid classification

Specify vertices, edges, oriented edge uses/coedges, loops, faces, shells and solids;
shared ownership, seam edges and degenerate edges; 3D/UV consistency and orientation.
Separate closed manifold solids from sheets and any future non-manifold representation.
Handle cavities, disconnected solids, nested shells and periodic surfaces correctly.
Classify point/curve/face against trimmed regions even near the boundary.

Validate incidence, loop closure, trim consistency, self-intersection, shell orientation
and admissible tolerances after operations. Euler checks and positive volume alone are
insufficient. Publish complete immutable geometry/history or a structured failure.

### 5. Booleans, arrangements and healing

Build reliable intersection → split/imprint → classify → stitch pipelines, including
coincident faces/edges, tangential contact, tiny slivers, cavities and repeated operands.
Solid regularization, sheet operations and empty results need explicit semantics.
Use exact separation proofs for fast paths; never replace uncertain intersections with
bounding-box guesses. Preserve all operand lineage through classification and stitching.

Repair/import is a separate capability: diagnose gaps and invalid trims, sew or rebuild
within a declared budget, report changed geometry, and retain unrepaired input. Healing
must not silently erase thin features or inflate tolerance. Admit hostile/malformed files
with resource bounds. Verify units and geometry through independent exchange round trips.

### 6. Fillets, offsets, shells and advanced construction

Industrial blending needs rolling-ball/spine construction, variable radii, concave and
convex chains, corner patches, setbacks, disappearing edges and self-intersection handling.
Offsets/shells need singularity detection, opposing-wall collapse and topology changes;
general offsets of rational surfaces are not necessarily rational surfaces.

Broaden loft/sweep domains with explicit section correspondence, guides, frame transport,
twist, continuity and self-intersection contracts. Add draft, face replacement/direct
edits and sheet operations as separate capabilities. Gate each operation/domain by
analytic cases, adversarial topology and lineage before declaring broad support.

### 7. Constraint solving and assemblies

First distinguish verifying a satisfied sketch from actually solving its constraints.
Sketch solving needs dimensions, Jacobians, rank/DOF diagnostics, inconsistent subsets,
underconstraint, multiple solutions and continuity under parameter changes.

Assemblies distinguish reusable part definitions from occurrences and nested occurrence
paths. Define local frames, grounding, configurations, stable occurrence keys, joints
and limits; repeated parts may have different materials or contact roles. Solve rigid
mates and closed kinematic loops with rank-aware diagnostics and controlled branch
selection. Persistent topology supplies joint axes and attachment surfaces.

Add forward/inverse kinematics, singularity handling and reachable-workspace tests;
broad/narrow collision and clearance queries; continuous collision for moving paths.
Large assemblies need shared definition geometry, occurrence-aware bounds, incremental
constraint updates, configurations and measured memory/query scaling. Clearance and
collision results must declare approximation bounds and supported motion assumptions.
Dynamic simulation further requires mass/inertia, constraints, actuation, friction,
contact and time integration. A posed robot or an animation satisfies none of these
mechanical gates by itself. Test pendulum/slider-crank/four-bar mechanisms, redundant and
inconsistent mates, nested repeated parts, analytic inertia and conservation laws.

### 8. CAD-to-analysis domains and meshing

Derive solid/fluid regions and interfaces explicitly. Internal fluids require capping
and extraction; external flows require a declared enclosure/far field. Detect leaks,
overlaps and unresolved small features; record defeaturing and idealization choices.

Surface display triangles are not a volume analysis mesh. Require tet/hex/prism or
other declared cells, positive Jacobians, quality metrics, curvature/feature resolution,
conforming or explicitly coupled interfaces, and preserved boundary-region membership.
CFD additionally needs wall layers and controlled growth/orthogonality. Refinement,
remeshing and curved/high-order elements must preserve geometry and boundary intent.
Track coverage, disconnected regions and material assignment before solver admission.

### 9. Structural, thermal and transient simulation

Specify dimensional material laws, loads/supports, element types, quadrature, assembly,
sparse algebra and preconditioning. Start with linear elasticity and heat conduction;
then modes, buckling, transient response, finite deformation, plasticity and contact.
Beams/shells, composites/anisotropy, fatigue and fracture require separate models and gates.
Handle rigid modes, locking, ill-conditioning, incompatible constraints and nonlinear
failure explicitly. Contact needs search, enforcement, friction and consistent tangents.
Mass/inertia tensors and coordinate transformations require independent checks.

Use patch tests, manufactured solutions, analytic bars/beams/thermal slabs, energy and
reaction balance, mesh/time convergence and documented experimental comparisons. Keep
discretization, iteration and model uncertainty distinct. MFEM and PETSc demonstrate
the scale of discretization and solver infrastructure available for evaluation [S4,S5].

### 10. CFD

Start with conservative incompressible laminar flow and typed wall/inlet/outlet settings.
Handle pressure nullspace and velocity-pressure coupling, stable fluxes/discretization,
non-orthogonal meshes, transient stability and local/global conservation. A decreasing
residual does not prove accurate pressure drop, lift or heat transfer.

Advance separately to turbulence/wall treatment, compressibility/shocks, rotating and
moving meshes, heat transfer, multiphase/free surfaces and reacting flow. Each adds
physical models and validation regimes. For every supported regime, report residuals,
mass/momentum/energy balance, quantities of interest, mesh/time sensitivity and limits.
Use analytic Poiseuille/Couette cases first, then manufactured solutions and documented
experimental cases. Use at least three systematically refined meshes for initial
acceptance, with observed convergence/error estimates rather than a plausible plot [S6].

### 11. Coupling, execution and reproducibility

Thermomechanics, conjugate heat transfer and fluid-structure interaction need explicit
interface fields, conservative transfer, nonmatching mesh mapping, coupling iterations
and time synchronization. Moving meshes require geometric conservation and invalid-cell
handling; partitioned FSI needs stability checks. Adjoint/optimization support follows
validated primal models and sensitivity checks through geometry/topology changes.

Long-running analysis uses a separate proposed job lifecycle; it cannot borrow or extend
the five-second interactive geometry watchdog. Specify admission, cancellation, memory/
CPU limits, progress, checkpoint/restart and failure evidence before adding workers.
Record source/revision, selectors, mesh, materials, BCs, numerical settings, solver build,
hardware and result provenance. Changed inputs invalidate downstream results explicitly.
Sparse parallel assembly, preconditioners, partitioning, adaptive refinement and CPU/GPU
execution need their own accuracy/scaling evidence; adding threads is not a solver design.
AI and any future client consume the same typed, validated headless contracts.

### 12. Precise, compact factual feedback to AI

Every build/query must be usable by an AI to verify a change. Bind feedback to the exact
request, effective source/revision, solver/configuration and result/artifact digests.
Return round-trip numerical precision, explicit units and tolerance/uncertainty scope;
geometric comparison tolerance is not a certified volume/area error bound. Unknown
quantities are explicit, never fabricated zeros or a generic successful validation flag.

Provide measured body geometry, topology, validity/admission basis, resolved reference
cardinality/lineage, feature diagnostics and build/cache/timing evidence. For assemblies
and studies, add DOF/constraint residuals, clearances, mesh quality/coverage, solver residuals,
conservation, quantities of interest and convergence only when actually computed.
Checks identify actual/expected values, units, absolute/relative tolerances and outcome;
distinguish computed facts, checked assertions, unsupported checks and model assumptions.

Keep default feedback compact: shared units/configuration, summary counts and changed
evidence against an identified baseline. Expose detailed topology, query witnesses, logs
and full fields through bounded/paginated retrieval, with exact artifact references and
explicit truncation. Fail stale baselines; never mix last-valid display geometry with a
failed revision's facts. AI and clients share the same contracts. New query/tool schemas
need concrete Accepted RFCs. [#228](https://github.com/wckdboy/nightfall/issues/228) starts
by enriching existing headless build records with already-available geometry evidence.

### 13. Quality corpus and performance

Maintain small analytic cases, adversarial degeneracies and representative real parts.
Add deterministic property/metamorphic tests: rigid invariance, scale-aware behavior,
valid Boolean identities under stated domains, immutable inputs and edit/revert replay.
Compare independent measurements and alternate algorithms, not merely the same solver
through two wrappers. Differential agreement is evidence, not a correctness proof.

Report success/failure by supported domain, false reference bindings (target: zero),
error distributions, median/p95/max latency, peak RSS, retained memory and scalability.
Keep failing seeds and minimize them into permanent regressions. Preserve named failure
categories and denominator; a feature checklist or test count cannot establish parity.

### Implementation sequence and proposed exit gates

- **K0 — Current baseline and persistent-reference corpus.** Complete the integration
  gates; preserve #185/#195. First new kernel work adds split/merge/deletion/ambiguity,
  cache/eviction and edit/revert cases under RFC 0007, starting with
  [#227](https://github.com/wckdboy/nightfall/issues/227), alongside Rust parity under RFC
  0008. Gate: every expected binding/failure agrees across cold/warm/rebuilt execution;
  no silent rebinding; existing geometry/ownership and full-check baselines pass.
- **K1 — Own numerical and topology foundation.** Finish Rust surface math and engine
  migration; specify owned B-rep/intersection/tolerance contracts in dedicated RFCs.
  Gate each replacement by independent accuracy, validity, lineage and resource tests
  across a declared domain. Keep OCCT until replacements pass; do not require a wholesale
  rewrite before assemblies or analysis can use validated existing geometry.
- **K2 — Robust modeling and exchange.** Expand pathological booleans, blends, shells,
  healing and STEP corpora. Gate supported domains by published success/failure counts,
  error budgets, immutability, reference survival and measured latency/RSS. Growth in
  advertised operation coverage requires new evidence, not relaxed tolerances.
- **A1 — Assemblies and motion.** Accept occurrence/joint schemas and solver design;
  pass grounded/under/overconstrained and loop benchmarks, analytic kinematics, collision
  and independent mass/inertia checks before dynamic loads and contact.
- **M1 — Analysis preparation.** Accept study/region/mesh contracts. Gate: complete named
  boundary/material coverage, positive cells, declared quality bounds, retained CAD
  association and explicit invalidation after edits. A1 is not a prerequisite for a
  single-part analysis; both depend on K0 and sufficient validated geometry.
- **S1 — Structural/thermal and F1 — Laminar CFD.** Can advance independently after M1.
  Pin per-case accuracy, equilibrium/conservation and refinement criteria before coding.
  First gates are elastic/thermal patch and analytic tests, and pipe-flow pressure drop/
  velocity over three meshes. Publish units, assumptions, settings and actual cost.
- **C1 — Nonlinear/dynamic, turbulent and coupled analysis.** Admit one physical regime
  at a time after simpler gates pass. Validate contact, turbulence and coupling with
  separate conservation, sensitivity and experimental evidence. No calendar promises
  substitute for gates; this is a multi-year engineering program.

## Alternatives considered

- Continue building the UI around demonstration parts: conflicts with current priority
  and does not close reference, geometry or simulation reliability gaps.
- Wrap more native operations and call the Rust kernel complete: useful parity work,
  but leaves owned geometry algorithms and the Rust evaluator unimplemented.
- Rewrite all geometry and physics at once: removes a working oracle while multiplying
  unverified algorithms. Prefer replaceable layers and small evidence-backed cutovers.
- Put physics into `IKernel`: couples geometry transactions to long-running studies and
  solver-specific state. Prefer shared references with distinct typed numerical layers.

## Compatibility and migration

This proposal changes prioritization, not existing file bytes or public APIs. Preserve
Accepted RFCs, Phase 2 failures, current app/CLI clients and all deadlines/resource limits.
Acceptance would supersede UI-first scheduling where inconsistent, not erase UI code or
waive correctness gates. New assembly/study/mesh/reference persistence requires versioned
schemas, migration and Accepted implementation RFCs. No dependency is added here.

Evaluate reuse as well as owned algorithms. PETSc is BSD-2-Clause, with separately
licensed optional packages [S7]; MFEM is a candidate for finite elements [S4]. SU2 is
LGPL-2.1 [S8]; OpenFOAM Foundation is GPLv3 [S9], currently excluded by WORKFLOW §8 as
recorded in #194. Solver selection must resolve licenses, pins, distribution and process/
file contracts explicitly. Process separation alone is not a licensing decision.

## Testing and measurement

This RFC makes no numerical implementation change. Check links, source-backed capability
claims, RFC/issue traceability and scope against the current tree. The proposal revision
passes **884/884 in 268.48 s** through `tools/check.sh --preset occt`, plus Rust math,
schema and hygiene; all local relative links resolve. New implementation issues attach
their own regressions and full checks. No assembly/FEA/CFD result is claimed.

## Risks and open questions

Industrial robustness comes from algorithms, exceptional cases, support and a growing
corpus over years. Rust memory safety and many passing tests are valuable but insufficient.
Staffing, benchmark licensing/data, and whether each solver is owned or integrated remain
decisions. Choose bounded initial physics domains and quantitative targets in follow-up
RFCs; this charter does not silently select every backend or approve their dependencies.

## Primary references

Checked 2026-10-20. These inform the proposal; no external code was copied or installed.
OCCT's website redirects its older documentation URLs to current guides; the repository
remains pinned to 7.9.3 and its headers/tests determine implemented behavior.

- [S1: Siemens Parasolid geometric modeling scope](https://www.siemens.com/en-gb/products/plm-components/parasolid/3d-modeling-sdk/).
- [S2: OCCT OCAF topological naming and evolution](https://occt3d.com/dev/doc/overview/html/occt_user_guides__ocaf.html).
- [S3: Shewchuk, adaptive precision and robust geometric predicates](https://www.cs.cmu.edu/~quake/robust.html).
- [S4: MFEM finite element infrastructure](https://mfem.org/).
- [S5: PETSc solver/discretization overview](https://petsc.org/release/overview/).
- [S6: NASA NPARC spatial convergence assessment](https://www.grc.nasa.gov/www/wind/valid/tutorial/spatconv.html).
- [S7: PETSc license and external-package scope](https://petsc.org/release/install/license/).
- [S8: SU2 scope and license](https://su2code.github.io/).
- [S9: OpenFOAM Foundation license](https://openfoam.org/licence/).
