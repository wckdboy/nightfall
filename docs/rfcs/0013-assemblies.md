# RFC 0013: Assemblies in the feature IR

- **Status:** Accepted
- **Author(s):** The Nightfall Authors (agent-authored proposal)
- **Phase:** 3, Track 3 of RFC 0011
- **Created:** 2026-10-22
- **Issue:** [#262](https://github.com/wckdboy/nightfall/issues/262)
- **Decided:** 2026-10-22 by the maintainer, who waived the WORKFLOW §3 review period.

**Depends on:** RFC 0005 (document, selectors, hashing), RFC 0007 (kernel contract, entity
history), RFC 0008 (Rust crates), RFC 0011 (program and gate G3).

## Summary

Schema version 2 adds three optional sections to the part document: `components` (part
definitions, inline or by content-addressed reference), `occurrences` (placed instances) and
`joints` (one per pair of frames, carrying its degrees of freedom). A frame is derived from a
selector by a fixed rule, following Onshape's mate connectors, so the model and the agent
name *where* parts meet, never coordinates of where they happen to be. A new Rust crate,
`nightfall-assembly`, solves occurrence placements over SE(3) with residual, rank and
degree-of-freedom diagnostics. Interference, clearance and mass properties become queries;
STEP with assembly structure and URDF become exports, so a mechanism can be tested in
MuJoCo or Chrono. A v1 document is a v2 document with none of the new sections: the
migration only changes the version number.

## Motivation

Gate G3 and the robotic-arm milestone (#169) need parts that move against each other.
Today a document holds bodies placed by rigid `transform` features: there is no notion of
two instances of one part, of a joint axis, or of a mechanism's freedom. The research behind
RFC 0011 is specific about what goes wrong otherwise: generated assemblies without mates
leave the hard part to the human (Leo AI), assemblies that execute can still have wrong
interfaces (MUSE), and low-DOF mates with separate dimensional constraints are brittle under
edits (Onshape's critique of SolidWorks' ~2.5 mates per part pair). Onshape's model, one
frame-based mate per pair that carries the allowed motion, is the pattern to copy, and the
same frames later carry loads and supports into studies.

## Design

### Document

```json
{
  "format": "nightfall.part",
  "version": 2,
  "params": [ { "name": "link_len", "value": "120 mm" }, { "name": "elbow", "value": "35 deg" } ],
  "features": [ … the root component's features, exactly as in v1 … ],
  "components": [
    { "id": "link", "features": [ … ] },
    { "id": "motor", "reference": "motor", "params": { "shaft_d": "8 mm" } }
  ],
  "occurrences": [
    { "id": "base", "component": "root", "grounded": true },
    { "id": "upper", "component": "link", "params": { "length": "link_len" } },
    { "id": "lower", "component": "link", "params": { "length": "link_len * 0.8" } }
  ],
  "joints": [
    { "id": "shoulder", "kind": "revolute",
      "a": { "occurrence": "base",  "frame": { "faces_of": "body", "where": { "type": "cylinder", "radius": 6 }, "expect": "one" } },
      "b": { "occurrence": "upper", "frame": { "created_by": "pivot_bore", "role": "wall", "expect": "one" }, "rule": "cylinder_axis" },
      "value": "0 deg", "limits": ["-90 deg", "90 deg"] },
    { "id": "elbow", "kind": "revolute", "a": { … }, "b": { … }, "value": "elbow" }
  ]
}
```

- **components:** each has an id and either inline `features` (same schema as the root
  list) or a `reference` id from `references` naming another `.nightfall` file by SHA-256
  (RFC 0005). A referenced component's parameters are its document's `params`; an inline
  component declares its own in `params` (name to value or expression), as the root does.
  An occurrence may override them with values or expressions in the assembly's parameters. The root component is the
  document's own `features` and has the reserved id `root`.
- **occurrences:** an id, a component, optional parameter overrides, `grounded`, and an
  optional authored `placement` (translation, rotation) that is the solver's starting point.
  An occurrence of a referenced document that is itself an assembly nests: its occurrences
  are addressed by path, `arm/upper`. Paths are the identities used by queries and exports.
- **joints:** an id, a kind, two frame ends, optional `value` (one per DOF, expressions) and
  `limits`. A joint with a value drives that DOF; without one, the DOF is free. A one-DOF
  joint takes a single value and a `[min, max]` limit. A multi-DOF joint takes arrays in its
  kind's order, `null` for a free DOF or an absent limit: `cylindrical` angle, slide along
  z; `pin_slot` angle, slide along x; `planar` x, y, angle. `ball` takes no value.

Unknown keys keep failing validation; `x-` keys stay allowed. Every new reference is checked
for order and existence like v1's: a joint may name only declared occurrences, an occurrence
only declared components.

### Frames

A frame is an origin and right-handed axes `x`, `y`, `z` in the occurrence's local
coordinates, derived from exactly one entity resolved by an RFC 0005 selector
(`expect: one` is implied and enforced). The `rule` defaults by entity type:

| Entity | Rule | Origin | z axis | x axis |
| --- | --- | --- | --- | --- |
| planar face | `plane_centroid` | area centroid of the trimmed face | outward normal | the face's located parameter U (as `face_plane`) |
| cylindrical face | `cylinder_axis` | axis point closest to the face's centroid | cylinder axis, oriented by the face | perpendicular, from the axis toward the face's U origin |
| circular edge | `circle_center` | circle centre | circle normal | toward the edge start |
| linear edge | `edge_midpoint` | midpoint | edge direction | perpendicular in the plane of an adjacent face named by a second selector (reserved: not evaluated until that selector exists) |
| vertex or sketch point | `point` | the point | the owning sketch normal or world z | world x projected |

An optional `offset` (translation and rotation, expressions) adjusts the derived frame, and
`flip: true` reverses `z` (and `x`, to stay right-handed). Frames re-derive on every rebuild,
so a joint follows its face when an upstream parameter moves it. A selector that resolves
to zero or several entities is a joint error naming the selector's JSON Pointer; there is no
nearest-geometry fallback (RFC 0007).

### Joint kinds

Each kind fixes some of the six relative DOF between frames `a` and `b` (expressed in `a`):

| Kind | Free DOF | `value` |
| --- | --- | --- |
| `rigid` | none | — |
| `revolute` | rotation about z | angle |
| `slider` | translation along z | length |
| `cylindrical` | rotation about z, translation along z | angle, length |
| `pin_slot` | rotation about z, translation along x | angle, length |
| `planar` | translation along x and y, rotation about z | length, length, angle |
| `ball` | three rotations | none in v2 (free only) |

### Solver: `nightfall-assembly`

A Rust crate in `core/assembly/rust`, `forbid(unsafe_code)`, no new dependencies. Unknowns
are a 6-vector twist per non-grounded occurrence, applied as an exponential-map increment to
its current placement. Each joint contributes the residuals of its fixed DOF (position
components in `a`'s frame, and orientation components from the logarithm of the relative
rotation) plus one residual per driven value. Jacobians are analytic. Levenberg–Marquardt
with a trust region iterates from the previous placements (authored placement on the first
solve), which keeps the solution on its branch through value edits and drags.

The report, per assembly: `status`, `dof` (6 × free occurrences minus the rank of the
Jacobian at the solution, computed by a rank-revealing QR with an explicit tolerance; drive
rows count, so a mechanism with its one DOF driven reports 0),
`residuals` per joint in mm and rad, `redundant` joints (rows that do not raise the rank and
are satisfied), `conflicting` joints (unsatisfied rows on failure), `loops` (independent
cycles in the joint graph) and `iterations`. Limits are inequality constraints active only
when violated; a driven value outside its limits is an error, not a clamp.

Closed loops (four-bar, slider-crank) are ordinary: the loop adds rows, and the rank analysis
reports the remaining mobility. A loop that cannot close at the driven value is a conflict
naming its joints, and the previous placements stay published.

### Evaluation

Each distinct (component, parameter overrides) pair evaluates once, with RFC 0005 hashing:
two occurrences of the same link share the same exact solid and differ only by placement.
The assembly hash combines the component hashes, joint inputs and solver identity, so a
changed joint value re-solves without rebuilding any geometry. Bodies in queries and exports
are addressed `occurrence_path/body_id`. Solve failures are document errors with JSON
Pointers into `/joints/…`, and like feature failures they do not stop independent work.

### Queries

- **Interference:** broad phase on placed bounding boxes, then the exact common volume of
  each candidate pair through the kernel's Boolean intersection. Reported per pair in mm³,
  with touching (zero volume, shared boundary) distinguished from overlapping.
- **Clearance:** minimum distance between two occurrences' solids, with the closest points.
- **Mass properties:** per occurrence and for the assembly: mass, centre of mass and the
  inertia tensor about it in the assembly frame, from exact volume integrals and a
  `material` per component or occurrence. Materials come from a curated CC0 dataset with
  a source per value (RFC 0011 Track 4); until it lands, `density` may be given directly.
- **Motion sweep:** a joint value over N steps, reporting placements and interference at
  each step; this is the "motion study" an agent uses to check a mechanism's range.

### Exports

- **STEP AP242 with assembly structure:** one product per component, one placed instance
  per occurrence, so CAD tools see the assembly tree rather than a flat body list.
- **URDF:** a link per occurrence (or per rigid group), with its mesh from `export_mesh`,
  mass and inertia from mass properties, and joints mapped to `revolute`, `prismatic`,
  `fixed`; `cylindrical`, `pin_slot`, `planar` and `ball` become short chains of massless
  links. URDF is a tree: a closed loop exports its spanning tree, and the loop-closing
  joints it cuts are named in the reply. Units are converted to metres and kilograms. Effort and velocity limits are
  required by URDF; they come from optional joint fields, or a default with a warning.

### Agent tools

`patch_part` already edits every new section. New `nf mcp` tools, all read-only except
where noted: `inspect_assembly` (occurrences with placements, joints with resolved frames,
the solver report), `drive_joints {values}` (sets joint values and re-solves; mutating),
`check_interference {occurrences?}`, `measure_clearance {a, b}`, `mass_properties
{occurrence?}`, `sweep_joint {joint, from, to, steps}`, `export_urdf {path}`. The evaluation
suite gains assembly tasks: build a four-bar from a description, drive it, and pass when the
solved placements and the coupler curve match analytic values.

## Alternatives considered

- **Separate assembly document type.** Cleaner separation, but agents and humans then manage
  two formats and cross-file references for every multi-part design. One document with
  optional sections keeps a single part simple and lets assemblies grow from it; referenced
  components still give file-level reuse.
- **Low-level mates (coincident, concentric, distance) as in SolidWorks.** More rows per
  pair, brittle under edits, and harder for an agent to reason about than one joint that
  names the motion it allows.
- **OndselSolver (LGPL, archived) or SolveSpace (GPL).** Licensing aside, the kinematic core
  is small enough to own, and its diagnostics must match RFC 0012's vocabulary.
- **Use the physics engine as the solver.** MuJoCo or Chrono resolve constraints dynamically;
  CAD needs exact static placement, rank-based DOF and conflict reporting. They remain the
  consumers of the URDF export, not the CAD solver.

## Compatibility and migration

Version 1 → 2 is a pure migration that changes `version` only: every v1 document is a valid
v2 document with no components, occurrences or joints, and evaluates identically, with the
same feature and body hashes. v1 readers refuse v2 files as RFC 0005 requires. The fixture
migration test keeps every v1 corpus document and asserts equal hashes and geometry after
migration. Studies are not part of v2; they come with their own RFC and version.

## Testing and measurement

Gate G3 from RFC 0011, with explicit tolerances:

- **Closure:** a four-bar (Grashof crank-rocker, analytic coupler positions) and a
  slider-crank close with every joint residual ≤ 1e-9 mm and ≤ 1e-9 rad across a full crank
  sweep of 72 steps; reported DOF is 1 for both with the crank undriven.
- **Diagnostics:** a redundant parallel joint is named redundant; an over-constrained loop
  that cannot close is named as conflicting with the previous placements kept; a
  chain with one free joint reports DOF 1; a ball joint reports 3.
- **Queries:** interference of two overlapping boxes equals the analytic overlap within
  1e-6 relative; touching boxes report zero volume and contact; the mass and inertia of a
  hollow cylinder and of a two-occurrence assembly match analytic values within 1e-9
  relative.
- **Export:** a six-joint arm's URDF loads in MuJoCo and in `urdfdom`'s parser; driving each
  joint there to the same values reproduces the solver's end-effector position within
  1e-6 m.
- **Edits:** changing a component parameter moves dependent frames and the assembly
  re-solves on the same branch; renaming an occurrence through `patch_part` rewrites joints.
- **Agents:** the evaluation suite's assembly tasks run through `nf mcp` with measured
  pass criteria.

## Risks and open questions

- Frame rules for faces that are neither planar nor cylindrical (cones, tori, NURBS) are
  undefined in v2 and refused; they need explicit rules or a point-and-axis selector later.
- Rank decisions depend on a tolerance; near-singular configurations (a four-bar at its dead
  centre) need corpus cases and an explicit reported condition number.
- Nested assemblies multiply occurrences; large assemblies need instancing in queries and
  exports and measured memory before they are advertised.
- Joint `value` units must be checked per DOF (angle versus length) with RFC 0005's
  dimension rules; mixing them is a validation error.
