# Nightfall

Open-source, AI-native parametric CAD for macOS.

Nightfall pairs fluid direct modeling with a full parametric history, on exact NURBS
geometry. Every part is a program: defined by math, code and SVG, editable by hand or
by any AI agent.

> **Status:** The first native macOS visual development preview is runnable: exact solids,
> editable parameters, feature history and a Metal viewport. Phase 2 and the complete
> modeling workflow remain in progress. See the [phase tracker](docs/phases/phase-2.md).

## Principles

1. **Open by default.** Apache-2.0, permissive dependencies, open file formats.
2. **Math is the source of truth.** Exact NURBS; meshes are always derived.
3. **Every part is a program.** A feature list with named parameters; geometry is a cache.
4. **Three ways in, one model.** Equations, Nightfall Script and SVG compile to one feature IR.
5. **AI edits the program, never the geometry.** Typed tools, rebuild, validate.
6. **Built for making real things.** Millimetres, explicit tolerances, STEP/3MF/SVG/USD out.

## Repository layout

```
core/        C++/Rust libraries (math, svg, ir, kernel, usd, script)
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

Requirements: macOS 15+ on Apple Silicon, Xcode 26+, CMake 3.28+, Ninja, rustup.
`rustup show` installs the exact Rust toolchain and components in `rust-toolchain.toml`.
The std-only `nightfall-math` crate starts the [Rust migration](docs/rfcs/0008-rust-kernel.md)
with checked B-spline bases and rational 2D/3D curves, including allocation-free points
and translation-conditioned derivatives. Run `cargo doc --locked --offline --no-deps --open` for the Rust API
and a headless conic example. The production client still uses the
existing C++ engine while parity gates are built. `tools/check.sh` checks both languages;
Rust dependency resolution is locked/offline and artifacts go to `build/rust/`.
The std-only `nightfall-native` crate adds checked OCCT contexts, exact typed extrusion,
retained immutable solids/entities, owned measures and generated face/source lineage.
Contexts and native snapshots are neither `Send` nor `Sync`; native calls and matching
releases are serialized. The headless `nf-rust-extrude` example builds a plate with two
exact rational circular holes and checks it after input/context teardown. The configured
native CTest checks run this example and print its exact measurements in verbose mode.
`nightfall_native::transform` adds exact rigid placement and complete owned face/edge
history. A retained before/modified/generated entity keeps both native snapshots alive,
including after source, intermediate result and context teardown. Foreign contexts reject
before native access; axis normalization and geometry validation remain native.
CMake builds the private dynamic adapter before CTest runs Rust Debug/Release and Clippy
against both OCCT-enabled and unavailable configurations. Math-only Cargo commands remain
independent; native Cargo consumers must set an absolute `NF_NATIVE_LIB_DIR` and a runtime rpath.
For the configured native checks, run:

```sh
cmake --preset occt
cmake --build --preset occt --target nf_native_adapter_tests nf_native_adapter_c_smoke nf_native_adapter_step_probe nf_native_unavailable_tests nf_native_unavailable_c_smoke
ctest --preset occt -R '^native-abi\.'
```

`nightfall-kernel` is the headless typed entry point, with unsafe code forbidden. It checks
finite inputs, nonempty regions/loops and exact endpoint connectivity at the configured
mm tolerance, returning field coordinates or region/loop/curve locations. Its
`validate_extrusion` checks input structure; `extrude` also invokes native orthonormal-frame,
geometry and full solid validation. It never samples or rewrites the NURBS. Retained readers
use the safe native ownership layer; no raw pointer, client, XPC or model provider is needed.
`Kernel::info` distinguishes the Rust implementation from the solver and its configuration;
native solver capabilities do not imply Rust operation parity. Extrusion, rigid
placement, solid booleans, mesh readback and exact STEP export are exposed in the current Rust surface.
CTest also runs `nf-kernel-extrude`, which reports a disconnected-profile diagnostic and
builds the exact two-hole plate through `Kernel`. To see the examples' receipts/measures:

```sh
ctest --preset occt -R '^native-abi.*\.rust$' -V
```

`Kernel::transform` places exact solids by rotating about an arbitrary axis/origin, then
translating, with complete retained face/edge history. `validate_transform` reports precise
nonfinite field coordinates and a zero axis; native normalization and configured axis
precision floors still apply. `nf-kernel-placement` uses this typed API to build six placed
two-hole parts, checks all world centroids/measures and 300 history mappings, then verifies
retained readers after input/source/Kernel/results are dropped. These are placed solids;
joint constraints and assembly simulation remain separate work.

`Kernel::tessellate` returns retained immutable world-space vertices, outward normals,
indexed triangles and original-face ranges. `validate_meshing` checks finite mm/radian
deflections against configured precision floors and the native triangle limit. Structured
`KernelError::MeshInput` diagnostics identify invalid fields. Mesh clones borrow the same
native buffers; a cloned picking face stays valid after every mesh/source/facade drops.
Offsets in derived meshes are transient and never persisted selectors.
Rust consumers matching `KernelError` exhaustively must handle the new `MeshInput`
variant; diagnostic receipt consumers update to `policy=extrusion-rigid-mesh-step-boolean-v1`.
No persisted document or cache migration is needed.

`nf-kernel-mesh` validates six exact rational two-hole plates and exports a derived OBJ,
receipt and offline viewer with orbit, zoom, wireframe and face selection. After the linked
native CTest above has built the examples, generate into a new destination:

```sh
mkdir -p out
build/occt/rust-nf_native_adapter/aarch64-apple-darwin/release/examples/nf-kernel-mesh --output-dir out/rust-mesh-preview
open out/rust-mesh-preview/preview.html
```

The [inspected fixture](docs/images/rust-kernel-mesh.png) has 6 solids, 84 original faces
and 4,152 triangles, with mesh volume within 0.0086% of exact geometry. This viewer is a
validation example; production macOS client integration remains a separate migration gate.

The [private native adapter](core/kernel/native/include/nf_native.h) also exposes exact
STEP export: `nf_native_export_step` encodes every supplied same-context snapshot as AP242
millimetre bytes, including coincident bodies. Its independent immutable byte owner survives
source/context teardown, with paired `nf_native_step_query` / `nf_native_step_release`.
The adapter opens no files. `nightfall_native::Context::export_step(&[Shape])` returns a
cloneable `step::Step` with borrowed immutable `bytes()`. It rejects empty/foreign-context
lists before native access and checks returned pointer/count metadata. Clones share the
allocation after all sources/context drop and remain thread-affine; copied `Vec<u8>` can
cross threads.

`Kernel::export_step(&[Shape])` exposes the same exact export through the forbid-unsafe
typed facade and reexports `Step`. Empty/foreign-context lists return
`KernelError::Native(NativeError::InvalidArgument)`; solver errors keep their native
diagnostics. The operation performs no file I/O. Retained bytes survive every geometry
owner and Kernel; STEP contains geometry without editable history or selectors.
`nf-kernel-step` builds the six placed two-hole plates and writes a new exact STEP file:

```sh
build/occt/rust-nf_native_adapter/aarch64-apple-darwin/release/examples/nf-kernel-step --output out/rust-plates.step
```

The example refuses existing destinations. The native Rust gate independently reloads the
actual file and validates six closed solids, 84 faces, 216 edges, twelve analytic holes,
world bounds/centroids and exact volume/area. Diagnostic receipt consumers migrate from
`policy=extrusion-rigid-mesh-v1` to `policy=extrusion-rigid-mesh-step-boolean-v1`; persisted documents
and cache formats are unchanged.

`Kernel::boolean_operation(target, tools, BooleanOp::{Union, Subtract, Intersect})` exposes
exact solid booleans through the safe Rust facade. Tools form a union; repeated snapshots
are deduplicated for geometry and history. Sources remain immutable, and complete retained
face/edge history, output shapes and generated intersection entities survive input/facade
teardown. Empty/foreign-context operands return `Native(InvalidArgument)` before native
access. Native full closed-solid validation rejects empty or non-solid results with
`Native(NativeFailure)` and publishes no partial result. The same operation is available
through `nightfall_native::Context`; only its private adapter owns unsafe calls.
No feature JSON, file I/O or approximate geometry is involved. Diagnostic receipts migrate
from `policy=extrusion-rigid-mesh-step-v1` to `policy=extrusion-rigid-mesh-step-boolean-v1`.

Mathematical curves come from `nightfall-math`; exact solid construction/validation remain
delegated to C++/OCCT. Rust IR/evaluation and production client cutover remain pending.

Build the native visual preview with the production, dynamically linked OCCT kernel:

```sh
third_party/build-occt.sh --jobs 4
cmake --preset occt
cmake --build --preset occt --target Nightfall
open build/occt/app/Nightfall.app
```

The app starts with the Asterion multi-body showcase and includes twenty reference parts
plus five modeling showcases in **Examples**. Choose **Nocturne** for the posed robotic
manipulator combining lofts, a spline harness, joint hardware and a two-finger gripper;
development build 0.0.13 bundles it and parallelizes copied-geometry meshing and exact face
integrals. Large-pattern Undo still has an intermittent native timeout under investigation
in [#185](https://github.com/wckdboy/nightfall/issues/185); this build is not a validated release.
The saved model also
opens in preview 0.0.10 through **Open…**. Choose **Lofted Link** for a smooth
three-section elliptic housing with a flange and five drilled holes; change `waist_scale`
or `height` to explore its shape. Choose **Swept Conduit** for a hollow cubic-spline
sweep; edit `reach`, `lift`, `radius` or `wall` to reshape it.
Quit older Nightfall copies before opening a newer development build.
Edit a parameter expression, including its units, and press Return to rebuild. The window
shows feature history, exact volume/area and diagnostics; failed edits retain clearly marked
last-valid geometry. Use Undo/Redo, Open and Save copy to try variants. Drag to orbit,
Shift-drag or right-drag to pan, scroll/pinch to zoom, or use the view presets and Fit.
History rows offer feature enable/suppression and inclusive rollback. **Restore** evaluates
the full history again; these edits also support undo/redo and Save copy. A valid sketch-only
or fully suppressed construction state clears the viewport and exact measures.

This is an ad-hoc signed development preview for Apple Silicon/macOS 15+, without Developer
ID signing or notarization. It supports existing part programs and named parameter edits;
interactive sketch/feature authoring, in-app AI tools, assemblies, collaboration and physics
simulation are still ahead. Shared-memory mesh transport and full phase sign-off are pending.

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

Documents are version 2 ([RFC 0013](https://github.com/wckdboy/nightfall/pull/263)): the version-1 layout plus
optional `components`, `occurrences` and `joints`. Version-1 files open unchanged and are saved
as version 2 by changing only their version number. The published part schemas,
[version 2](core/ir/schema/part-v2.schema.json) and [version 1](core/ir/schema/part-v1.schema.json), are generated from
`core/ir/schema/build_schema.py`; `tools/check.sh` rejects a stale generated copy.

Part load/save also validates parameter expressions, dependencies and bounds. Values such as
`"2 * bolt_pitch + 10 mm"` resolve in dependency order, with lengths normalized to mm and
angles to radians. Parameter numbers are dimensionless; bare numbers in feature fields use
the document units. Errors include the field's JSON Pointer and expression offset when available.
See [the expression API](core/ir/include/nf/ir/expression.hpp) and
[parameter evaluation](core/ir/include/nf/ir/parameters.hpp).

The service's [document evaluator](services/kernel/src/evaluator.hpp) executes sketches,
B-rep extrusions, revolutions and lofts, rigid transforms and solid booleans using dynamically linked OCCT 7.9.3.
It supports rectangles, circles, signed arcs, rotated ellipses, NURBS splines, polygons and mixed
closed profiles, holes, interior-point selection, datum/explicit planes and symmetric extrusion.
Content hashes and a bounded cache preserve unchanged geometry. Rollback, suppression and
failures have explicit states; unsupported
constraints, selectors and modeling operations report errors. This headless kernel path
also powers the native preview; the remaining v1 operations are still in progress.
Lofts support 2..32 ordered closed profiles on parallel planes, with smooth interpolation
through intermediate sections and provenance from every section. Holes within loft sections,
guide rails and nonparallel planes are unsupported. Sweeps and interactive spline authoring
remain in progress; exact NURBS spline profiles already work through the shared IR/kernel.

Viewport consumers can call `IKernel::tessellate(shape, {0.1, 0.2})` for an indexed display
mesh (absolute mm/radian deflections). Positions are world-space doubles; normals point
outward. Each face range retains its original snapshot handle for transient picking.
Meshing deep-copies geometry and preserves existing triangulations and cached solid names.
The default returned-data limit is one million triangles and three million vertices;
it does not bound OCCT's internal working memory or runtime. The native worker handles
service admission and the watchdog. Backends must implement the new virtual query,
and vtable consumers must rebuild.

Native service code can now use the [plain-C bridge](services/kernel/bridge/kernel_bridge.h)
(`nf_kernel_bridge` target; Swift module `NightfallKernelBridge`). Create a per-document
session, pass the complete v1 JSON to `nf_session_build`, inspect its immutable manifest and
indexed buffers, then free the result and session. Results survive later builds and session
teardown; any error returns zero geometry. Mesh budgets apply across all live bodies.
Valid documents with no live solids return an empty successful manifest; check its errors
to distinguish a sketch-only, suppressed or rolled-back state from a failed build.
The signed active-feature index is safe to poll from a watchdog and is -1 outside feature
evaluation. Serialize builds on the service worker. C17 linking and Swift 6 strict importing
are tested. The [Swift worker](services/kernel/swift/KernelWorker.swift) now serializes native
access, admits one rebuild, retains four document caches by LRU, and returns owned buffers
through the [shared XPC protocol](services/kernel/swift/KernelProtocol.swift). Its watchdog
claims a single timeout reply with the active feature, then terminates the service.
`nf_native_worker_tests` covers the codec and watchdog ownership. The `occt` preset also
builds the production `NightfallKernel.xpc` inside a headless test host, with only the
required OCCT dylibs in the service's Frameworks folder. The build removes machine-specific
runtime search paths and ad-hoc signs nested code. Real XPC tests check all twenty parts,
cache ownership and restart/replay after process exit, including a separate test-only
hang/crash service and relocation to a path with spaces:

```sh
cmake --preset occt
cmake --build --preset occt --target nf_xpc_probe nf_xpc_fault_probe
ctest --preset occt -R '^bundle\.' --output-on-failure
```

Tests require launchd access; restricted process sandboxes can reject the service lookup.
The test host imports only the Foundation protocol and links no kernel library. Set
`NF_BUILD_XPC_BUNDLE=OFF` for a standalone Swift sanitizer build. The native app uses the
same production service and packager. Shared-memory transfer remains pending under RFC 0003;
both the test host and visual app are development artifacts, without a notarized release.

The Foundation-only [app client](app/KernelClient.swift) keeps one rebuild in flight,
settles each request once and enforces a seven-second backstop. Connection/request tokens
discard stale callbacks; cancellation and shutdown retire the connection. A timeout with
a valid feature index retries once with that feature suppressed in a temporary IR copy.
The response retains the recovery diagnostic, while authored source bytes stay unchanged.
Reply bounds/layout/diagnostics are checked off the main actor. Controlled tests also run
under Swift ThreadSanitizer; the real embedded-service test exercises cache closure and
complete-IR replay through the client after a crash, including bounded relaunch failures.

The [Metal viewport](app/MetalRenderer.swift) consumes finite, bounded Float32 display
data derived off the main actor from the service's indexed meshes. It supports depth-tested
normal lighting, face-boundary outlines and fitted orthographic orbit/pan/zoom/presets.
`bundle.render` exercises all twenty real parts and saves inspected PNGs in `build/occt/renders`; it requires a
physical Metal device (GPU-only checks skip with code 77 when none is available). This
renderer powers the native preview window. `bundle.preview` tests actual service rebuilds,
parameter edits, retained valid geometry, revision ordering and undo/redo through its document model.

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
Agents drive the same headless engine through `nf mcp`, a stdio Model Context Protocol
server ([RFC 0011](docs/rfcs/0011-ai-design-loop.md), Review) with few general tools:
`inspect_part`, `get_part`, `get_schema`, `body_topology`, `measure`, `set_parameters`, `patch_part` (RFC 6902),
`replace_part`, `undo`, `redo`,
`open_part`, `save_part`, `export_step` and `export_mesh` (derived OBJ). The authoritative state is the IR text; every
mutating tool validates, rebuilds and replies with the build evidence above plus the bodies
whose geometry changed, appeared or vanished. A failed feature rolls the change back unless
`rollback` is false, and every error carries its JSON Pointer. Sketch constraints solve (RFC 0012): every sketch of points, lines, circles and arcs reports
its degrees of freedom, under-constrained entities and redundant constraints in each
feature's `sketch` entry, and a rebuild that solved a sketch writes the solved coordinates
back into the document's plain numbers, in its own units, listing them under
`written_back`; coordinates authored as expressions stay as written. Files are reachable only in
the `--in` folder, a private scratch folder and each `--allow-path`. Evaluation runs in
process without a deadline; the client's tool timeout bounds it. Register it with Claude Code:

```sh
claude mcp add nightfall -- build/occt/tools/cli/nf mcp --in examples/parts/mounting_plate.nightfall
```

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
