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
It supports rectangle, circle, polygon and closed-line profiles, holes, interior-point selection,
datum/explicit planes and symmetric extrusion. Content hashes and a bounded cache preserve
unchanged geometry. Rollback, suppression and failures have explicit states; unsupported
constraints, selectors and modeling operations report errors. This is the headless kernel
path; native app integration and the remaining v1 operations are still in progress.

Booleans union, subtract or intersect a target with the union of its tool bodies. Tools are
consumed on success unless `keep_tools` is true; failed operations preserve the tools and
invalidate the target for downstream execution. Empty or non-solid results are errors.
Face provenance survives cuts, cache reuse and subsequent transforms.
Extrusions and revolutions can create a new body or use `result: {"join": "body"}`, `{"cut": "body"}`
or `{"intersect": "body"}` to modify an existing body. The intermediate solid stays private;
the target's geometry and face history are published together after both operations succeed.

Revolutions accept an explicit axis in the sketch plane and a signed angle up to one full turn.
They preserve exact profile curves, holes and selected regions. Full turns have no cap faces;
partial turns retain start/end cap provenance. Profile control hulls must stay on the same side
of the axis, touching it if needed; potentially overlapping sweeps are rejected. Sketch-line
and selector axes remain unimplemented.

To run the real 30-feature geometry benchmark (plain, bored and boolean-cut plates; turned sleeve):

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

## Contributing

Read [CONTRIBUTING.md](CONTRIBUTING.md) and [the development workflow](docs/process/WORKFLOW.md)
before opening a pull request. Architectural changes start as an [RFC](docs/rfcs/README.md).

## License

[Apache-2.0](LICENSE). Third-party components keep their own licenses; see
[third_party/README.md](third_party/README.md).
