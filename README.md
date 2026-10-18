# Nightfall

Open-source, AI-native parametric CAD for macOS.

Nightfall pairs fluid direct modeling with a full parametric history, on exact NURBS
geometry. Every part is a program: defined by math, code and SVG, editable by hand or
by any AI agent.

> **Status:** Phase 2 — parametric documents, unit-aware expressions and incremental solid rebuilds. The NURBS math and SVG phase is
> complete; the native modeling app is not usable yet. See the [phase tracker](docs/phases/phase-2.md).

## Principles

1. **Open by default.** Apache-2.0, permissive dependencies, open file formats.
2. **Math is the source of truth.** Exact NURBS; meshes are always derived.
3. **Every part is a program.** A feature list with named parameters; geometry is a cache.
4. **Three ways in, one model.** Equations, Nightfall Script and SVG compile to one feature IR.
5. **AI edits the program, never the geometry.** Typed tools, rebuild, validate.
6. **Built for making real things.** Millimetres, explicit tolerances, STEP/3MF/SVG/USD out.

## Repository layout

```
core/        C++ libraries (math, svg, ir, kernel, usd, script)
services/    XPC services (kernel isolation)
app/         macOS app (SwiftUI + Metal)
agent/       AI tools, model providers, eval suite
cli/         nf command-line tool
third_party/ pinned external dependencies
docs/        process, RFCs, phase trackers, format spec
tools/       developer scripts
```

Only directories with real content exist; the rest arrive with their phase.

## Building

Requirements: macOS 15+ on Apple Silicon, Xcode 26+, CMake 3.28+, Ninja.

```sh
tools/check.sh            # configure, build, test, format-check (what CI runs)
cmake --preset dev        # or drive CMake directly
cmake --build --preset dev
ctest --preset dev
```

To run the independent IR schema cross-check locally (Python 3.13+):

```sh
python3 -m venv build/schema-venv
build/schema-venv/bin/pip install -r tools/schema-requirements.txt
NF_SCHEMA_PYTHON=build/schema-venv/bin/python tools/check.sh
```

The published [part schema](core/ir/schema/part-v1.schema.json) is generated from
`core/ir/schema/build_schema.py`; `tools/check.sh` rejects a stale generated copy.

Part load/save also validates parameter expressions, dependencies and bounds. Values such as
`"2 * bolt_pitch + 10 mm"` resolve in dependency order, with lengths normalized to mm and
angles to radians. Parameter numbers are dimensionless; bare numbers in feature fields use
the document units. Errors include the field's JSON Pointer and expression offset when available.
See [the expression API](core/ir/include/nf/ir/expression.hpp) and
[parameter evaluation](core/ir/include/nf/ir/parameters.hpp).

The service's [document evaluator](services/kernel/src/evaluator.hpp) executes sketches,
B-rep extrusions/revolutions, rigid transforms and solid booleans using dynamically linked OCCT 7.9.3.
It supports rectangles, circles, signed arcs, rotated ellipses, NURBS splines, polygons and mixed
closed profiles, holes, interior-point selection, datum/explicit planes and symmetric extrusion.
Content hashes and a bounded cache preserve unchanged geometry. Rollback, suppression and
failures have explicit states; unsupported
constraints, selectors and modeling operations report errors. This is the headless kernel
path; native app integration and the remaining v1 operations are still in progress.

Build a `.nightfall` part into exact STEP AP242 solids (millimetres):

```sh
cmake --preset occt
cmake --build --preset occt --target nf
build/occt/tools/cli/nf build examples/parts/mounting_plate.nightfall --out /tmp/plate.step
```

`nf build` exports every live body, preserving separate and coincident bodies and excluding
consumed tools. It respects suppression and rollback; any model error prevents export.
Input reads are limited to 16 MiB. Success writes one JSON record to stdout with `output`
and sorted `bodies`; errors write an `errors` array to stderr with codes, messages, JSON
Pointers and available expression offsets. Exit codes are 0 for success, 1 for build/I/O
failure, and 2 for usage errors (`nf --help`). An existing output is replaced only after
successful encoding and a complete temporary-file write; input aliases are refused.
The backend encodes in memory and preserves source snapshots. Backend implementers must add
`IKernel::export_step(const std::vector<ShapePtr>&)` and rebuild for the new virtual interface;
no document migration is needed. STEP carries geometry, without Nightfall history or selectors.

Build a parameter variant with repeatable `--set name=expression` options:

```sh
build/occt/tools/cli/nf build examples/parts/washer.nightfall --set "thickness=6 mm" --out /tmp/washer-6mm.step
```

Overrides require a valid source and existing, unique parameter names. They apply together
to an in-memory copy; source bytes remain unchanged. `--set` and `--out` may appear in either
flag order. Expressions see the final parameter graph, so self-references are cycles. Bare
values are dimensionless; lengths and angles require units. Existing unit/bounds/dependency
validation and field diagnostics apply. Malformed/repeated flags return usage errors (2);
unknown parameters or invalid expressions/values return build errors (1). Failed variants
preserve an existing output.

Arc entities use `center`, `radius`, `start_angle` and a signed `sweep_angle` up to one full turn;
ellipses use `center`, `radii` and optional `rotation`. Splines retain their clamped knot vector,
degree (1–9), `control_points` and optional positive `weights`. Coordinates and angles accept
unit-aware expressions. Open lines, arcs and splines are valid reference geometry; their
entities and constraints are still validated. When an operation requests filled profile regions,
unordered segments join at unambiguous endpoints within the modeling tolerance, with explicit
bridges preserving the original curves. Open or ambiguous chains report an error at the
consuming operation's profile, including when selecting a region from a mixed closed/open
sketch. A failed profile consumer leaves the sketch available for axis references.
Bridges below OCCT's vertex precision are handled by vertex welding. Exact knot-span
decomposition handles spline corners before region
detection. Initial work limits are 4,096 sketch curves, 4,096 spline control points per sketch
and 4,096 profile spans; oversized inputs report errors before arrangement work.

Closed outlines may share straight boundaries: adjacent rectangles merge into one filled
region, partial overlaps split at their endpoints, and duplicate outlines follow the selected
`evenodd` (default) or `nonzero` fill rule. Rational degree-1 spans retain their original
parameterization. A profile whose outlines cancel reports an empty-profile error before kernel
work. Shared stretches involving higher-degree curves remain tracked in
[#74](https://github.com/wckdboy/nightfall/issues/74).

Booleans union, subtract or intersect a target with the union of its tool bodies. Tools are
consumed on success unless `keep_tools` is true; failed operations preserve the tools and
invalidate the target for downstream execution. Empty or non-solid results are errors.
Face provenance survives cuts, cache reuse and subsequent transforms.
Extrusions and revolutions can create a new body or use `result: {"join": "body"}`, `{"cut": "body"}`
or `{"intersect": "body"}` to modify an existing body. The intermediate solid stays private;
the target's geometry and face history are published together after both operations succeed.

Extrusions can replace `distance` with a face selector, for example
`"to":{"created_by":"stop","role":"cap_end","expect":"one"}`. The selected analytic
plane must be parallel to the sketch plane and ahead of the chosen `direction` (the sketch
normal by default). It supplies its infinite supporting plane: the extruded profile may
extend beyond the selected face's trim, and the face does not clip the result. Termination
uses the current face after placements and booleans, with the same provenance, recursive
unions/exclusions, filters and nearest-face rules as sketch planes. References remain
unchanged unless they are also the authored result target. Source snapshots are retained
and charged to cache estimates, and plane queries still run on cache hits.
Separation must exceed 1e-6 mm; direction/normal alignment with both planes must exceed 1e-9
in magnitude.
Parallelism uses sine tolerance 1e-9, with a conservative control-hull check keeping the
complete cap within 1e-6 mm of the target plane. Negative ray travel, curved/tilted targets,
nonzero draft and `symmetric: true` with `to` are explicit errors. Select `direction: "-z"`
or another direction to reach a plane behind the default normal. Finite-distance extrusion
semantics are unchanged; no schema, kernel API or document migration is needed.

Equal-distance chamfers bevel selected face boundaries, for example:

```json
{"id":"bevel","type":"chamfer","inputs":{"distance":"1 mm",
 "edges":{"edges_of":{"created_by":"base","role":"cap_end","expect":"one"}}}}
```

`edges_of` includes all trimming loops, including holes, and deduplicates shared edges.
Compose edge sets with `any` and `except`; apply existing face filters inside `edges_of`.
Only bodies retaining selected edges are modified; excluded candidates remain dependencies.
Distance must exceed 1e-6 mm. OCCT propagates along tangent contours. Unsupported geometry,
incomplete history, explicit `angle` and edge-level `where` filters report errors. New faces
have role `chamfer`; surviving face names follow modifications through placements and cache hits.
Queries run before cache reuse, and multiple target bodies publish atomically.
Backend implementers must add `IKernel::edges`, `IKernel::chamfer` and the `chamfer` capability,
and rebuild consumers for the changed vtable/value layout. Documents need no migration.

Revolutions and rigid rotations accept an explicit axis or a named line in an earlier sketch,
such as `{"sketch":"centerline","entity":"axis"}`. A standalone line can define the axis;
its start and start-to-end direction map through the evaluated sketch plane. Axis-sketch edits
rebuild dependent operations. Revolutions require the axis to lie in the profile plane and
accept a signed angle up to one full turn.
They preserve exact profile curves, holes and selected regions. Full turns have no cap faces;
partial turns retain start/end cap provenance. Profile control hulls must stay on the same side
of the axis, touching it if needed; potentially overlapping sweeps are rejected. Selector axes
remain unimplemented.

Linear body patterns accept `bodies`, `count`, signed `spacing` and `direction`. Count includes
the seed; every copy translates from that original seed by an integer multiple of the spacing
along the normalized direction. Copies union into each targeted body: overlapping/touching
copies fuse, while separated copies remain multiple valid solids within that body. Copied
faces retain their original producer names and gain the pattern's `instance` role. All targets
publish together after geometry and naming succeed. The initial work limit is 64 total
instances across all targets, including seeds; count 1 preserves seed geometry. Larger counts
require spacing magnitude above 1e-6 mm and finite offsets.

Circular body patterns use `bodies`, `count`, signed `angle` and an explicit, sketch-line or selected
`axis`. Partial sweeps include the seed at zero and the last copy at the specified angle;
full turns distribute unique instances without repeating the seed endpoint. Angles within
1e-9 rad of a full turn use that full-turn placement. Sweeps are limited to one turn (plus
that tolerance), and count >1 requires an angular step above 1e-9 rad in magnitude. Count 1
allows zero sweep. Circular patterns share linear patterns' union, naming, atomic publication
and 64-total-instance behavior. Each rotation starts from the original current seed, and
axis-source edits rebuild dependent arrays. Feature-target patterns remain unimplemented.

Body mirrors accept a datum, explicit frame or face selector as `plane`. Each body in
`bodies` unions its seed with one exact reflection, fusing material or retaining separate solid
components. Reflection preserves closed-solid orientation, analytic surfaces and complete
face/edge history; source snapshots remain unchanged. Mirrored faces retain inherited names
and gain the mirror feature's `instance` role. Plane parameters drive dependent rebuilds.
The shared 64-total-instance limit admits at most 32 mirror targets. All bodies publish
together after geometry and naming succeed. Mirrors share the face-selector semantics below:
only authored `bodies` are modified; reference bodies remain unchanged unless also targeted.
Source edits invalidate the mirror, and every selector source is retained/charged to its cache
entry. Feature targets remain unimplemented.

Sketch planes can select a generated analytic planar face, for example
`{"created_by":"base","role":"cap_end","expect":"one"}`. Selection follows current face names
through transforms, patterns, mirrors and booleans, including consumed or retained tools.
The face supplies its located parameter origin/U axis and outward normal; the resulting
right-handed frame places the sketch's authored entities. Every plane requires exactly one
face, including with `expect: any/some`; zero or multiple matches report `selector_count`.
The producer feature and all candidate bodies are dependencies before role filtering.
Cached evaluations query the current face frame, and execution counts include that query.
Body-level attachment also accepts, for example,
`{"faces_of":"plate","where":{"type":"plane","normal":"z"},"expect":"one"}`.
Both selector forms support planar `type`, signed `normal`, `parallel_to` and
`perpendicular_to` filters. The latter two describe the plane relative to the direction.
Directions accept axes or scalar expression vectors, normalized above length 1e-6; angular
matching uses sine tolerance 1e-9. Conditions combine; curved faces do not match planar filters.
Body selectors read current geometry and fail for retired tools. All candidate dependencies
remain bound, including bodies removed by filters. Face enumeration also runs on cache hits.
`where.near` accepts a world point in length units, for example
`{"faces_of":"plate","where":{"normal":"z","near":[25,10,"depth + 1 mm"]}}`.
After other filters, it compares distance to every remaining trimmed face, including edges
and holes, across all candidate bodies. Every face within 1e-6 mm of the minimum survives;
ties remain ambiguous. Distance queries also run on cache hits and count as kernel calls.
`near` alone includes curved faces; sketch and mirror planes still require a planar face.
Face selectors also compose recursively, for example:

```json
{"any":[{"created_by":"base","role":"cap_end"},{"created_by":"base","role":"cap_start"}],
 "except":{"created_by":"base","role":"cap_start"}}
```

Each node unions by snapshot/entity identity, subtracts `except`, applies its geometric
and nearest filters, then checks `expect`: `one` means exactly one, `some` (default) means
at least one, and `any` permits empty sets. Optional empty branches need `expect: any`.
Failures identify the nested selector's JSON Pointer; every referenced producer/body is
a dependency, including excluded/empty branches. The final sketch or mirror plane needs one
planar face. Face descriptions are shared within each feature evaluation and refreshed
before cache reuse.
Generated side faces also accept an authored entity qualifier, for example
`{"created_by":"base","role":"side","from":"outline/right","expect":"one"}`.
Curve sources survive decomposition, arrangement splits, shared-span merges, region picks
and later operation history. A rectangle/polygon can produce several matching walls; use
filters to narrow them. Caps, named points and synthetic tolerance connectors have no
authored curve qualifier. A source from an unrelated sketch matches nothing, even if its
geometry coincides. The referenced sketch must be available and is a cache dependency.
Local cached sources bind to the current producer/sketch IDs when names are published.
Rebuild all C++ clients: `math::Arrangement::HalfEdge` and `math::Loop` gained transient
source metadata, also changing the layout of region values passed to `IKernel`.
Directly constructed loops may leave sources empty; no document migration is needed.
Transforms, circular patterns and revolutions also accept a cylindrical face selector as
their `axis`, for example `{"created_by":"bore","role":"side","where":{"type":"cylinder",
"radius":"2 mm","near":[1.5,1.5,10]},"expect":"one"}`. Radius must be positive and matches
within 1e-6 mm; radius alone implies a cylinder filter. Conditions combine with existing
provenance, unions, exclusions and nearest-face rules. Directions retain their planar
semantics, so combining a normal filter with a cylinder filter matches nothing. Select one
face: several coaxial patches remain ambiguous. Sketch/mirror planes still require a plane.
The located cylinder's stored parameter Z supplies the directed axis. It can point opposite
the extrusion direction; positive angles follow that stored direction, and negative angles
reverse rotation. Reversing a face for a bore does not reverse its parameter axis. Source
placements and boolean history are followed, and metadata/distance queries refresh on cache
hits. Reference bodies stay unchanged unless authored targets; retained sources are charged
to cache estimates. Revolution axes must still lie in the profile plane.
Rebuild C++ clients of `kernel::FaceInfo`, which gains optional typed cylinder metadata.
Backends populate finite unit axes and positive exact radii for analytic cylinders; other
curved types have neither a plane nor a cylinder description. No document migration is needed.
Other surface types, convexity filters and edge selectors remain
unimplemented. Backend implementers must add
`IKernel::faces` for snapshot-owned face handles/optional outward frames, and
`IKernel::face_distance` for finite nonnegative point-to-trimmed-face distances.
These are source/ABI changes with no document migration.

To run the real 30-feature geometry benchmark (plain, bored and boolean-cut plates, turned sleeve,
parametric elliptic tube and a plate made from rectangles sharing an edge):

```sh
third_party/build-occt.sh
cmake --preset occt
cmake --build --preset occt --target nf_evaluator_bench
build/occt/services/kernel/nf_evaluator_bench
```

The benchmark reports median/p95/maximum for cold builds and unique parameter edits, with
analytic geometry and execution-count checks on every sample. Timings include JSON parsing,
document validation, feature evaluation and kernel validity checks; they exclude serialization,
IPC and tessellation. Cache byte counts are retained-size estimates, not process RSS.

Twenty editable [reference parts](examples/parts/README.md) exercise the production evaluator:
mounting plates, brackets, an enclosure, turned parts, curved sections and solid booleans.
Each `.nightfall` file has a named, bounded parameter. Headless regression tests check analytic
measurements at three parameter values, incremental versus cold builds, equivalent units,
cache restoration and retained geometry after eviction. Run just this suite with:

```sh
ctest --preset occt -R '^evaluator.reference'
```

## Contributing

Read [CONTRIBUTING.md](CONTRIBUTING.md) and [the development workflow](docs/process/WORKFLOW.md)
before opening a pull request. Architectural changes start as an [RFC](docs/rfcs/README.md).

## License

[Apache-2.0](LICENSE). Third-party components keep their own licenses; see
[third_party/README.md](third_party/README.md).
