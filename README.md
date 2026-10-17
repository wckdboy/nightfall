# Nightfall

Open-source, AI-native parametric CAD for macOS.

Nightfall pairs fluid direct modeling with a full parametric history, on exact NURBS
geometry. Every part is a program: defined by math, code and SVG, editable by hand or
by any AI agent.

> **Status:** Phase 2 — parametric document foundations. The NURBS math and SVG phase is
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

## Contributing

Read [CONTRIBUTING.md](CONTRIBUTING.md) and [the development workflow](docs/process/WORKFLOW.md)
before opening a pull request. Architectural changes start as an [RFC](docs/rfcs/README.md).

## License

[Apache-2.0](LICENSE). Third-party components keep their own licenses; see
[third_party/README.md](third_party/README.md).
