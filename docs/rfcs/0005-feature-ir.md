# RFC 0005: Feature IR schema v1

- **Status:** Accepted
- **Author(s):** wckdboy
- **Phase:** 2
- **Created:** 2026-10-17
- **Decided:** 2026-10-17 (review period waived by the maintainer)

**Depends on:** RFC 0003 (the kernel service takes the IR as JSON) and RFC 0004 (libnf-math
types and its expression language).

## Summary

A Nightfall part is one JSON document, the **feature IR**: named parameters, external
references, and one ordered list of features (sketches included) that build named bodies.
Every reference to geometry is a **selector** (a stable description), never an index. The
document carries a format version from day one; older versions are migrated forward by pure
functions and newer versions are refused. Each feature gets a **content hash** of its
evaluated inputs and upstream results, so a rebuild re-runs only the features whose hash
changed. A new kernel-free module, `core/ir` (libnf-ir), owns parsing, validation,
migration, expressions with units, and hashing; the evaluator in the kernel service (#92)
consumes it.

## Motivation

- **The IR is the source of truth** (build plan, CLAUDE.md). Geometry is a cache. Everything
  later builds on this format: the evaluator, Nightfall Script, the CLI, the agent's typed
  tools, undo, diffs and file compatibility. Changing it after users have files is expensive,
  so the shape has to be agreed before code depends on it (WORKFLOW §3).
- **AI and humans edit the same document.** An agent must be able to read a part, understand
  what each feature does and change one value without breaking unrelated features. That needs
  readable names, stable ids, semantic references and structured errors. Index-based
  references ("face 7") break on any upstream change and are opaque to a model.
- **The Phase 2 gate needs incremental rebuild:** one parameter change on a 30-feature part
  in under 100 ms. That requires knowing exactly which features a change touches.
- **RFC 0003 made the app stateless toward the kernel.** Every request carries the IR, so the
  IR must fully describe the part, deterministically.

## Design

### The document

A `.nightfall` file is a UTF-8 JSON document (RFC 8259), LF line endings, written by Nightfall
with two-space indentation and fields in the order below, so files diff and merge well in git.
Media type `application/vnd.nightfall.part+json`. A later RFC may add a package format
(thumbnails, a persisted geometry cache); the IR inside it stays this document.

```json
{
  "format": "nightfall.part",
  "version": 1,
  "units": { "length": "mm", "angle": "deg" },
  "meta": { "name": "Bracket", "doc": "L-bracket for a 20 mm extrusion" },
  "params": [
    { "name": "width", "value": "40 mm", "min": "10 mm", "max": "200 mm", "doc": "Overall width" },
    { "name": "wall", "value": "3 mm" },
    { "name": "hole_d", "value": "wall * 1.7" }
  ],
  "references": [
    { "id": "logo", "kind": "svg", "path": "logo.svg", "sha256": "9f2c…" }
  ],
  "features": [
    { "id": "base_sketch", "type": "sketch", "inputs": { "plane": { "datum": "xy" },
      "entities": [ { "id": "r", "type": "rect", "corner": [0, 0], "size": ["width", "20 mm"] } ],
      "constraints": [] } },
    { "id": "base", "type": "extrude", "name": "Base plate",
      "inputs": { "profile": { "sketch": "base_sketch" }, "distance": "wall",
                  "result": { "new": "plate", "body_type": "brep" } } },
    { "id": "edge_round", "type": "fillet", "suppressed": false,
      "inputs": { "edges": { "edges_of": { "created_by": "base", "role": "side" },
                             "where": { "parallel_to": "z" } },
                  "radius": "2 mm" } }
  ],
  "history_end": null,
  "ui": {}
}
```

| Field | Meaning |
| --- | --- |
| `format`, `version` | Always `"nightfall.part"` and an integer schema version (this RFC: 1). Required, checked first. |
| `units` | The unit of bare numbers: `length` ∈ mm, cm, m, in, ft; `angle` ∈ deg, rad. Internally everything is mm and radians (RFC 0004). |
| `meta` | Name and free text. Not hashed. |
| `params` | Named values, ordered for display. |
| `references` | External inputs, by content hash. |
| `features` | The ordered history. |
| `history_end` | `null`, or the id of the last feature to evaluate (the rollback bar). |
| `ui` | Free-form app state (camera, visibility, colours). Never hashed, never read by the evaluator. |

Keys starting with `x-` are allowed in any object, preserved on write and ignored. Any other
unknown key is a validation error, so typos do not silently do nothing.

### Ids and names

- Ids (features, params, references, bodies, sketch entities) match `[a-z_][a-z0-9_]{0,63}`,
  are unique within their namespace, and are never reused after deletion within a document's
  life. Parameter names are their ids. An entity of a sketch is addressed `sketch_id/entity_id`.
- The app generates short ids (`extrude_3`); agents and scripts may choose meaningful ones
  (`mounting_holes`). `name` is a free display label, never used for references.
- Renaming an id is an explicit IR operation that rewrites every use. It is never a side
  effect of a display-name change.

### Values, expressions and units

Any numeric input is a **value**: a JSON number (in the document's `units`) or a string
expression. The expression language is RFC 0004's (numbers, `+ − * / ^`, parentheses, `pi`,
`e`, the same functions) with three additions, implemented in #91:

- **identifiers** name parameters (`wall * 2`); the variable `t` is not available here;
- **unit suffixes** on numbers: `mm cm m in ft` for length, `deg rad` for angle;
- **dimensions are checked:** length ± length, length · number, length / length is a number,
  trigonometric functions take angles, and so on. A length field fed an angle is an error
  naming the field. A bare number in a length or angle field takes the document unit; in an
  expression, a bare number is dimensionless (`width / 2`).

Parameters may refer to other parameters; the dependency graph must be acyclic (a cycle is an
error that lists it). `min` and `max` are optional values of the same dimension; a value out
of bounds is an error on the parameter, not a clamp. Expressions evaluate to doubles once per
rebuild; the evaluator never sees an unevaluated expression.

Positions and vectors are arrays of values (`[0, "width / 2"]`). Directions may also be the
words `x`, `y`, `z`, `-x`, `-y`, `-z`.

### Features

Each feature is `{ "id", "type", "name"?, "suppressed"?, "inputs" }`. The feature list is the
history: features evaluate in order, and a feature may refer only to features before it
(validation checks this). Suppressed features are skipped; a feature that consumes a
suppressed feature's output fails with `input_suppressed`, so nothing silently changes shape.
Features after `history_end` are kept but not evaluated.

Each `type` has its own input schema. Version 1 defines:

| Type | Inputs (abridged) |
| --- | --- |
| `sketch` | `plane` (a datum `xy`/`yz`/`zx`, a planar face selector, or origin + normal + x direction), `entities`, `constraints` |
| `extrude` | `profile` (sketch regions), `distance` or `to` (selector), `direction`, `symmetric`, `draft`, `result` |
| `revolve` | `profile`, `axis` (sketch line or selector), `angle`, `result` |
| `sweep`, `loft` | profiles and a path or rails, `result` |
| `fillet`, `chamfer` | `edges` selector, `radius` or `distance`/`angle` |
| `shell` | `body`, `faces` to remove, `thickness` |
| `hole` | `face`, `positions` (sketch points), `diameter`, `depth`, optional counterbore/countersink |
| `boolean` | `op` (union, subtract, intersect), `target` body, `tools` bodies, `keep_tools` |
| `pattern_linear`, `pattern_circular`, `mirror` | `features` or `bodies`, counts, spacing, axis/plane |
| `transform` | `bodies`, translation, rotation |
| `import` | `reference` id, `result` |
| `script` | `source` (Nightfall Script), `args` (values), `result`. Its API is RFC 0006 (#93). |

A sketch entity is a point, line, arc, circle, ellipse, rect, polygon, spline (degree,
knots, control points, weights: RFC 0004 data), equation curve (`x`, `y`, `t0`, `t1`,
tolerance, as `equation_curve`) or SVG (a reference id plus placement). Coordinates are values,
so a sketch can be fully parametric through expressions. Constraints (coincident, horizontal,
vertical, parallel, perpendicular, tangent, equal, symmetric, midpoint, fixed, and dimensional
distance, angle and radius, whose amounts are values) are part of v1's schema. Until the
sketch solver lands, the evaluator checks them against the evaluated coordinates and reports
a violated constraint as an error; once the solver lands, stored coordinates become its
starting point. This is the only v1 feature whose evaluation changes later, and it is called
out under Compatibility.

Profiles are regions of a sketch (libnf-math `regions`, even-odd unless stated): the whole
sketch, or `{ "sketch": "s", "regions": [ <point inside> … ] }` picking regions by a point
they contain, never by region index.

### Bodies

A feature that makes geometry states where it goes in `result`:

- `{ "new": "plate", "body_type": "brep" }` creates a body with that id;
- `{ "join": "plate" }`, `{ "cut": "plate" }`, `{ "intersect": "plate" }` modify an
  existing one.

Body ids are document-wide and stable, so selectors and later features name `plate`, not
"body 1". A document can hold many bodies (multi-body parts, also the base for assemblies).
`body_type` is `brep`, `mesh` or `field` (implicit/SDF). v1's evaluator builds only `brep`;
`mesh` and `field` are valid in the schema and evaluate to `unsupported_body_type`, so the
format does not change when those evaluators arrive.

### Selectors

A selector describes faces, edges or vertices by how they came to be, then narrows them by
geometry. It resolves to a set at evaluation time.

```json
{ "created_by": "base", "role": "end" }
{ "created_by": "base", "role": "side", "from": "base_sketch/r" }
{ "edges_of": { "created_by": "base", "role": "end" } }
{ "faces_of": "plate", "where": { "type": "plane", "normal": "z", "near": [0, 0, "wall"] } }
{ "any": [ <selector>, <selector> ], "except": <selector> }
```

- **Generative names:** each feature names what it creates by role (`start`, `end`, `side`
  for extrude/revolve/sweep; `fillet` for a fillet's faces; `wall`, `bottom` for holes),
  qualified by the sketch entity it came from. Later features that split or merge an entity
  carry its name forward through the kernel's history (OCCT `BRepTools_History`:
  generated/modified/deleted), so "the side face from line `r`" still resolves after an
  unrelated boolean.
- **Filters** (`where`): `type` (plane, cylinder, cone, sphere, torus, nurbs; line, circle,
  nurbs), `normal`/`parallel_to`/`perpendicular_to` a direction, `near` a point (nearest
  match), `radius`, `convex`/`concave` for edges. Values in filters are expressions.
- **Cardinality:** `"expect": "one" | "some" | "any"` (default `some`: at least one).
  A selector that resolves outside its expectation is a feature error naming the selector's
  JSON Pointer and the count found. It never falls back to an index.

The app's picker writes the most semantic selector it can (generative name first, then
filters that make it unique) and shows which entities each one matches.

### Errors and results

Evaluation results are not stored in the document. Each feature evaluates to `ok`,
`skipped` (suppressed or after `history_end`), or `error` with
`{ "code", "message", "pointer" }`. Here `pointer` is an RFC 6901 JSON Pointer into the
document (for example `/features/4/inputs/radius`), so the UI, the CLI and the agent all point
at the same field. Codes are a closed, documented list (`parse`, `schema`, `unit`, `bounds`,
`cycle`, `unknown_id`, `forward_reference`, `selector_count`, `input_suppressed`,
`kernel`, `timeout`, `unsupported_body_type`, …). A failed feature does not stop the rest:
features that do not depend on it still evaluate (Phase 2 shows all errors at once).

### Hashing and incremental rebuild

- **Feature hash** = SHA-256 of the canonical JSON (RFC 8785 JCS) of
  `{ evaluator version, type, evaluated inputs, upstream hashes }`.
  - The *evaluated inputs* have every expression replaced by its double (JCS formats doubles
    exactly, shortest round-trip). A parameter change therefore invalidates only the features
    whose evaluated values change, not every feature that mentions the parameter's name.
  - The *upstream hashes* are those of the features this one consumes: the bodies it reads
    (the hash of the body's state, below), the sketches and selectors' `created_by` features,
    and the `sha256` of referenced files. Display names, `meta` and `ui` are excluded.
- **Body state hash** = H(body's previous state hash, hash of the feature that modified it).
  The kernel service keeps an LRU cache from hash to kernel shape. A rebuild walks the
  features, recomputes hashes (microseconds) and runs the kernel only on a cache miss, so a
  change re-runs exactly the downstream features that changed.
- Hashes are also the protocol unit RFC 0003 anticipated ("IR delta plus the base hash"); the
  evaluator version in each hash makes a kernel upgrade invalidate caches instead of serving
  stale geometry.

### Versioning and migrations

- `version` is a single integer for the whole document. Any change that makes an old reader
  misread a new file, or a new reader misread an old file, bumps it, through an RFC.
- A **migration** is a pure function from version *n* JSON to *n + 1* JSON, in `core/ir`,
  kept forever and chained on load. Each has fixture tests (old file in, expected new file
  out) that stay in the repo forever.
- Nightfall writes only the current version. A file whose version is newer than the reader's
  is refused with "made by a newer Nightfall (version N)"; it is never opened lossily.
- The normative schema is JSON Schema 2020-12, `core/ir/schema/part-v1.schema.json`, published
  for editors, agents and other tools. libnf-ir's decoder enforces the same rules (and the
  cross-references JSON Schema cannot express: ids exist, no forward references, acyclic
  params). CI checks both against the same corpus of valid and invalid documents, using the
  Python `jsonschema` package as test tooling only (nothing shipped).

### Module: `core/ir` (libnf-ir)

A new C++23 library beside libnf-math, kernel-free like it (RFC 0004), used in both
processes: the app (editing, validation, the agent's tools) and the kernel service (the
evaluator, #92, which lives in the service and is the only part that touches OCCT). It
contains:

- a strict JSON reader and canonical writer (like `core/svg`'s XML reader: small, no
  dependency, exact number round trip, depth and size limits);
- the typed document model, validation and migrations;
- expressions with units and the parameter graph (#91), built on libnf-math's `Expression`;
- hashing (SHA-256 via CommonCrypto on Apple platforms, which is part of the OS, not a
  new dependency).

Swift reads the document through the libnf-ir facade (as libnf-math's facade, #87), not
through a second Codable model, so there is one implementation of the rules.

## Alternatives considered

- **Sketches in a separate top-level list** (the build plan's sketch). Sketch planes can sit
  on faces of earlier features, so sketches have a place in history. A separate list would
  need its own ordering rules relative to features. A sketch is a feature here.
- **Index references (face 7).** Simple, and the cause of most "my model broke" reports in
  history-based CAD. Unreadable for agents. Rejected outright.
- **Script as the source of truth** (OpenSCAD, CadQuery). Great for generation; poor for
  direct manipulation, since a drag would have to rewrite code. Here, script is one feature type
  inside a structured IR, and the IR stays editable by both humans and tools.
- **A custom text DSL, YAML or TOML.** A DSL needs its own parser, formatter and editor
  tooling and is harder for every other tool. YAML's implicit typing is a hazard for numbers
  and ids. JSON is the language of agent tool calls, JSON Schema and RFC 0003's protocol.
- **Binary (protobuf, FlatBuffers).** Smaller and faster, but undiffable and opaque. Part
  documents are kilobytes; meshes, the large data, never enter the IR.
- **A JSON library (nlohmann/json, simdjson, Glaze).** Each is a fine dependency, but we need
  our own canonical writer for hashing anyway, the format is small, and `core/svg` showed that
  a strict reader is a few hundred tested lines. No dependency means no RFC, licence or
  upgrade burden. To be revisited if parsing ever shows up in profiles.
- **Hashing source text instead of evaluated values.** Simpler, but a change to an unused
  parameter or to a display name would invalidate everything, and the 100 ms gate depends on
  invalidating only what actually changed.
- **Persisting kernel geometry in the file.** Faster open, but it puts a second, derived
  truth on disk, along with OCCT's format and version. Deferred to the package-format RFC, as
  a cache keyed by feature hash.

## Compatibility and migration

There are no existing part files: this is version 1. From now on, files are a public format:
- every later change follows *Versioning and migrations* above;
- fields defined here are never reinterpreted without a version bump;
- version 1 is supported forever.

The one planned change in evaluation is the sketch solver: v1 sketches without constraints
behave identically; v1 sketches with constraints that were satisfied keep their geometry, and
those that were violated (an error in v1) will then solve. This makes failing files work and
changes no working ones, so it needs no version bump.

## Testing and measurement

- **#90:** a schema corpus of valid and invalid documents (each invalid one labelled with the
  expected error code and pointer), checked by libnf-ir and by `jsonschema` in CI; reader
  fuzzing under ASan/UBSan; write → read round trip is identity; migration fixtures.
- **#91:** unit-checking and dependency-graph tests, including cycles and bounds.
- **#92:** hash stability (the same document hashes the same across runs and machines);
  a one-parameter change re-runs exactly the expected features; the Phase 2 gate's
  30-feature part rebuilds in < 100 ms.
- **#95:** 20 reference parts written in this format build headlessly through `nf build`.

## Risks and open questions

- **Topological naming is hard.** Generative names plus history cover extrude, revolve,
  fillet and booleans well; complex cases (a face split into two by a later cut, both
  matching) will surface as `selector_count` errors, which is the intended honest failure.
  The reference parts in #95 measure how often this happens; a later RFC may add a
  similarity-based recovery.
- **Hash stability across kernels.** Hashes include the evaluator version, so an OCCT
  upgrade invalidates caches by design; the cost is one full rebuild.
- **The feature type list will grow.** Adding a type with a new input schema is a minor
  change if old readers reject it cleanly (unknown type → `schema` error), but it still bumps
  `version`, so older apps refuse rather than misread. Whether additive types should instead
  use a capability list rather than a version bump is open; v1 keeps the single integer.
- **Sketch solver semantics** (above) are the main deferred decision.
