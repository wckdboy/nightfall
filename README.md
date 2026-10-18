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

Circular body patterns use `bodies`, `count`, signed `angle` and an explicit or sketch-line
`axis`. Partial sweeps include the seed at zero and the last copy at the specified angle;
full turns distribute unique instances without repeating the seed endpoint. Angles within
1e-9 rad of a full turn use that full-turn placement. Sweeps are limited to one turn (plus
that tolerance), and count >1 requires an angular step above 1e-9 rad in magnitude. Count 1
allows zero sweep. Circular patterns share linear patterns' union, naming, atomic publication
and 64-total-instance behavior. Each rotation starts from the original current seed, and
axis-sketch edits rebuild dependent arrays. Feature-target patterns and selector axes remain
unimplemented.

Body mirrors use `bodies` and a datum or explicit `plane`. Each current seed and one exact
reflected copy union into that body, fusing shared material or retaining separate solid
components. Reflection preserves closed-solid orientation, analytic surfaces and complete
face/edge history; source snapshots remain unchanged. Mirrored faces retain inherited names
and gain the mirror feature's `instance` role. Plane parameters drive dependent rebuilds.
The shared 64-total-instance limit admits at most 32 mirror targets. All bodies publish
together after geometry and naming succeed. Feature targets and selector planes remain
unimplemented.

Sketch planes can select a generated analytic planar face, for example
`{"created_by":"base","role":"cap_end","expect":"one"}`. Selection follows current face names
through transforms, patterns, mirrors and booleans, including consumed or retained tools.
The face supplies its located parameter origin/U axis and outward normal; the resulting
right-handed frame places the sketch's authored entities. Every plane requires exactly one
face, including with `expect: any/some`; zero or multiple matches report `selector_count`.
The producer feature and all candidate bodies are dependencies before role filtering.
Cached evaluations query the current face frame, and execution counts include that query.
Geometric filters, `from` qualifiers, selector sets and selector planes outside sketches
remain unimplemented.

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
