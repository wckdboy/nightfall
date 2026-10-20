# Phase 2 — Feature IR + Script

- **Weeks:** 8–16 (build plan)
- **Milestone:** `Phase 2` on GitHub
- **Status:** In progress

## Gate

All of these, with evidence linked:

- [ ] 20 reference parts (gear, bracket, enclosure, …) build headlessly in CI
- [x] Changing one parameter rebuilds a 30-feature part in < 100 ms ([baseline evidence](#rebuild-benchmark-evidence); [slanted-profile fix](#polynomial-mass-integration-rebuild-evidence); [CLI recheck and loaded-run miss](#headless-cli-export-and-rebuild-evidence))
- [ ] IR documents validate against schema v1 and migrate forward; ASan/UBSan clean

## Rust kernel and engine migration decision (#188)

The maintainer requests Rust for Nightfall's own kernel/engine implementation.
[RFC 0008](../rfcs/0008-rust-kernel.md) proposes Rust numerical geometry, safe kernel
ownership/policy, IR/evaluation/naming/cache code and a narrow native geometry C adapter.
Swift remains the macOS client/XPC host; OCCT remains the selected solver. The first
executable slice after acceptance is std-only checked NURBS evaluation/derivatives with
analytic/property/C++ differential tests. It is not a wrapper around the complete C++
evaluator. The proposal preserves existing geometry/IR/provenance/deadline contracts and
keeps #185's actual-app timeout open. The maintainer accepts the concrete design and
explicitly waives the review wait on 2026-10-20: “waived, lets go.” RFC 0008 is Accepted;
implementation can begin. No Rust production cutover or completed port is claimed.
The documentation revision passes the full native gate **827/827 in 211.91 s**; existing
Rust 1.99.1 tooling is verified and the missing formatter/Clippy components are installed.

## Rust math foundation (#190)

Accepted RFC 0008 now has a pinned Rust 1.99.1, std-only Cargo workspace and a safe
`nightfall-math` crate. Checked immutable clamped knots and stack-only basis evaluation /
derivatives cover degrees 1–9; nonfinite inputs and unrepresentable arithmetic return
structured errors. The library forbids unsafe code; the test allocator alone uses unsafe
forwarding to the system allocator. No production engine or IR switch is made.

Nine analytic/property cases first fail against empty implementations, then pass along
with a zero-allocation observer in debug/release. Seeded differential tests compare 3,456
cases / 380,160 values with C++; 109 cases additionally use an independent 80-digit
Cox-de Boor recurrence. Maximum row-scaled difference is 7.62e-16. Cancellation-sensitive
derivatives use an explicit ulp row-norm floor. fmt/Clippy pass; the full native gate passes
**828/828 in 235.72 s**, plus all ten Rust tests in Debug and Release, schema and hygiene.
CI and the local gate now include the pinned, locked/offline Rust checks.
Rational curves and production integration follow separately; #185 remains open.

## Parallel exact integration and copied-geometry tessellation (#185)

The overnight OCCT CI run for #184 passed 822/824 tests, with `bundle.scalability` and
`bundle.nocturne` reporting the unchanged five-second service timeout without an active
feature index. That index alone does not locate the stalled phase. Local sampling and
separate rebuild/mesh/bridge measurements identified substantial serial face-discretization
work in `BRepMesh_IncrementalMesh`.

The existing deep-copy meshing path now enables OCCT's per-request `InParallel` option.
Source geometry, existing triangulations, original-face binding, deflections, normal/winding
checks and return budgets are unchanged. Backend identity records the option change;
service entry remains serialized. No new dependency, interface, file format or recovery
retry is introduced. Both large native tests retain their full models and edit/Undo paths.

On an Apple M3 Pro (12 logical CPUs, 36 GiB), arm64 Release, macOS 27.2, the quiet native
before/after Nocturne cold build measures **3279.449 / 2410.806 ms**. Asterion's 1→8→4→8
sequence measures **2480.834→1985.141**, **1095.647→836.041**, **2471.871→1979.270 ms**
for the three edits. Both complete clean Undo and real XPC/Metal rendering. These are local
observations, not a guarantee for other hardware or loaded CI runners. The five-second
service watchdog and seven-second client backstop remain unchanged.

The three-sample headless Nocturne meshing range changes from 1258–1313 ms to 295–301 ms
(the baseline run includes a sampled interval and is not wholly quiet). Separate cold/warm
bridge samples change from 3225.729/1237.750 to 2219.310/304.917 ms. All 201808 triangles,
131485 vertices, 763 original-face ranges and exact summed component measures are identical;
mesh volume/area errors remain 0.16390%/0.06380%.

A new compound regression exercises ten separated solids, curved seams/poles, a cavity and
a reflection. Repeated calls preserve exact vertex/index/face bindings and serialized source,
reject a smaller output budget and leave all inputs unmeshed. The focused mesh suite passes
387698 assertions / 9 cases. The initial meshing-only commit `8cb9754` passes the full local
gate **825/825 in 202.16 s**, including schema/hygiene, all native cases and client TSAN.
Candidate **0.0.11 (build 11)** includes all 25 examples, but subsequent actual GUI QA finds
a Nocturne timeout after Asterion 1→8→4→8/Undo. That archive is retained as evidence, not
marked visually validated. Profiling a further failed cold request shows substantial
`BRepGProp_VinertGK` work, including B-spline faces, on the serial worker.

That initial commit's ASan/UBSan with halt-on-error passes all 125 kernel cases / 430563
assertions, the three
Nocturne/Asterion evaluator cases / 15494 assertions, and all 12 bridge cases / 65400
assertions, with no sanitizer diagnostics. The dynamically linked vendor OCCT build is
Release; Nightfall code and compiled header callbacks are instrumented. This is the scoped
evaluator sanitizer run, not a claim that its entire suite was rerun.

The follow-up gives each face private mass integrators/adaptors, executes independent
contributions through OCCT's existing pool, then sums in the original traversal order.
The shell-corner reference, polynomial/Gauss–Kronrod algorithms, 1e-9 requested accuracy,
validation and five/seven-second policy stay unchanged. Backend identity records this
execution option. A new repeated placed-surface regression verifies analytic volume and
asymmetric-hole centroid, deterministic measurements and unchanged source geometry. All
126 Release kernel cases / 430601 assertions pass.

Three quiet headless Nocturne samples now measure cold rebuild 1616–1703 ms and mesh
304–314 ms; separate bridge cold/warm is 1945.701/314.279 ms, with identical component
measures, topology, mesh counts and errors. The development app loads Nocturne, applies
waist/jaw edits (202032 triangles), then restores the clean 201808-triangle source with
Undo. The native scalability regression now loads/renders Nocturne after all large-model
edits and three Undo operations. The focused three native cases pass in 24.34 s. Replacement
preview **0.0.12 (build 12)** passes the full local gate **826/826 in 201.26 s**, including
schema/hygiene and client TSAN. Its quiet native Nocturne cold build takes **2098.604 ms**;
the Asterion edit sequence takes **1936.871/814.309/1920.008 ms**, then Nocturne loads in
**1983.361 ms** after clean Undo. The baseline Metal render remains byte-identical to the
checked-in Nocturne PNG (SHA-256 `d058cbe70424f87b70cd04cb57a03033b4738b0d8f5d48cd532adf82a80dc743`).
Final ASan/UBSan with halt-on-error passes all **126 kernel cases / 430600 assertions**,
all **368 evaluator cases / 450420 assertions**, and all **12 bridge cases / 65400
assertions**, with no diagnostics. A separate Debug C++ ThreadSanitizer build passes the
ten mass/mesh cases / 387735 assertions with halt-on-error and no reported races. Vendor
OCCT remains Release and is not sanitizer-instrumented; Nightfall code/header callbacks
are instrumented. The actual 0.0.12 archive subsequently times out on Undo back to eight
Asterion modules; it is retained as evidence, not marked visually validated. A scoped
Foundation user-initiated activity experiment reproduces the timeout and is removed.
App Nap is not established as a cause.

The next revision batches every shell's face tasks into one pool launch per shape, retaining
each shell's corner and the original face-then-shell reduction order. A new regression uses
eight placed instances sharing curved cavity surfaces, checking signed analytic volume,
area, asymmetric centroid, deterministic measurements and unchanged serialized input. All
**127 Release kernel cases / 430642 assertions** pass. Three quiet Nocturne samples measure
cold rebuild **1506–1561 ms**, mesh **296–300 ms**, and separate bridge cold/warm
**1905.054/302.953 ms**, with identical exact measures, topology, mesh counts and errors.
Three cold eight-module Asterion samples measure kernel **1088–1130 ms**, mesh **445–452 ms**;
separate bridge cold/warm is **2005.321/1788.382 ms**, with all 640 solids and 217142 triangles.

Development **0.0.13 (build 13)** passes the full local gate **827/827 in 197.97 s**, including
schema/hygiene and client TSAN. Quiet native Asterion edits take **1867.156/787.988/1868.508 ms**,
Nocturne after clean Undo **1935.234 ms**, and cold Nocturne **1972.736 ms**. Actual GUI QA
completes 1→8→4→8 and Undo→4, then times out on Undo→8. A separate sampled diagnostic
rebuild times out at the final pattern and temporarily suppresses that feature for recovery;
it is not counted as a successful full model. The profile still shows substantial exact
Boolean snapshot validation/bounds/mass work. No 0.0.13 archive is published or declared
validated. Full batched-revision ASan/UBSan with halt-on-error passes **127 kernel cases /
430641 assertions**, **368 evaluator cases / 450420 assertions**, and **12 bridge cases /
65400 assertions**, with no diagnostics. The separate Debug C++ TSAN build passes **11
mass/mesh cases / 387776 assertions** with halt-on-error and no reported races, subject to
the same vendor-instrumentation limitation above. Remote CI, review, the native reliability
fix and full phase sign-off remain pending.

## Nocturne mixed-tool geometry showcase (#183)

The user-requested final design/build test combines existing operations in
`examples/modeling/nocturne.nightfall`: 71 features, 21 body families, 106 closed solids
and 201808 display triangles. Turned/drilled pedestal, rounded radial fins, mirrored joint
hardware, oblique elliptic lofts, a planar cubic-spline harness and hooked gripper share
one editable v1 document and the existing headless/native engine. No production engine,
client, dependency, public API or schema change is introduced.

All component volumes are independently checked from primitives/frustums, integrated
quadratic ellipse sections and positive cubic-Bezier length quadrature, at 1e-5 mm³
absolute tolerance (1e-3 for the swept harness). Parameter/cache/retained-reader checks,
complete indexed-mesh face coverage, <1% mesh-volume error and exact STEP reload preserve
all 106 solids. The native test builds and renders, batches waist/jaw edits, verifies a
changed image, then restores original component measures and byte-identical clean source.
Default summed component V=2408145.026752167 mm³, A=390313.091946063 mm². The real native
render is retained at `docs/images/nocturne.png` and has been visually inspected.

The fixed shoulder/elbow/wrist centers are (0,0,110)/(150,0,310)/(318,0,184) mm. This is a
posed geometric concept toward #169, with no assembly joints, kinematics, collisions,
actuator/load or physics validation. Existing preview 0.0.10 is preserved; the new source
and exact STEP are separate artifacts usable with that client.

The final local gate passes **824/824 in 215.53 s**, with schema/hygiene green. The quiet
native cold build takes 3272.356 ms; its complete build/edit/Undo/render test passes with
the original five/seven-second policy. The image is the real XPC/Metal output.
The focused ASan/UBSan model regression passes 6119 assertions with halt-on-error and no
sanitizer diagnostics, including parameter/cache/retained-reader, meshing and STEP paths.

One final quiet arm64 Release sample measures kernel cold/warm 1941.403/5.257 ms, meshing
1198.574 ms, and C-bridge cold/warm 3158.810/1234.817 ms. All 71 feature entries hit the
warm cache (39406608 estimated bytes, not RSS). Mesh volume/area differ from exact summed
component measures by 0.16390%/0.06380%. These are single local observations, not capacity
or frame-rate guarantees. The actual preserved 0.0.10 app opens the finished model, changes
waist/jaw together (202032 triangles), then restores the clean 201808-triangle source with
Undo, without errors or recovery notices. It remains open, fitted for visual inspection.
Local deliverables are `out/Nightfall-Nocturne-showcase.zip`, the editable JSON/part source,
`out/Nightfall-Nocturne.step` and a validation receipt with source/geometry/file hashes.

## Asterion capability and limit evidence (#161)

The new `examples/modeling/asterion.nightfall` builds through the real evaluator/CLI and
was opened and visually inspected in the native Metal client. Its 28 features produce
eight live bodies / 80 closed solids / 745 faces / 27,172 display triangles. Independent
primitive and frustum integration gives V = 359949.6576334083 mm³ and A = 265552.9240952252 mm²;
all component volume/area checks and exported STEP reload pass at 1e-5 absolute tolerance.
The indexed display mesh differs in volume by 0.0541% and area by 0.0165%.

Three local Release samples on arm64: single-module median cold rebuild 378.636 ms,
warm rebuild 20.497 ms, mesh 105.861 ms; cold/warm C bridge 482.989/128.348 ms (one each).
Eight modules build headlessly: 640 solids / 5,768 faces / 217,142 triangles,
V = 2879597.261067266 mm³. Cold/warm bridge 4943.289/4738.721 ms leaves little native
deadline margin. The actual client displayed two modules (160 solids / 54,280 triangles),
but four- and eight-module attempts disconnected and retained marked last-valid geometry.
These observations are bounded stress evidence, not a universal maximum or a frame-rate claim.

The 64-instance and 64-total-target-instance limits apply to each pattern operation,
not the number of leaf solids in nested patterns. Large final pattern results can exceed
the 64 MiB estimated admission budget: 27 rather than 28 cache hits, with repeated copy/
union work on warm builds. The private `nf_showcase_bench` makes these costs reproducible.
Kernel performance and client inspection improvements will be tracked separately.

## Separated-union correctness and publication cost (#162)

New regressions exposed input-root `Free` flag changes in separated OCCT unions despite
non-destructive mode (4 failures / 15,044 assertions before the change). Boolean operands
are now copied before any builder can attach them. Unions with pairwise-separated exact
bounds enlarged by shape and kernel tolerances avoid intersection work; touching, overlapping
and nested bounds keep the general boolean path. Both paths retain same-domain seam
unification, full closed-solid validation, mass integration and complete current history.
The backend identity changes so old geometry cannot bypass the corrected behavior.

Naming publication groups histories by immutable owner and deduplicates within semantic
producer/role/source groups. Output order and matching semantics are unchanged. All 45
focused evaluator cases pass (15,410 assertions); the two new kernel cases pass 15,044
assertions, including circular seams, 64 inputs, duplicate handles, near contact, nesting,
retained readers, downstream transforms and mesh coverage. ASan/UBSan checks also pass
for the two kernel cases and showcase; the dynamically linked OCCT library is Release.

Indicative three-sample eight-module median cold/warm rebuild changed from 3758.349 /
3356.905 ms to 2724.110 / 2345.510 ms. Cold/warm C bridge changed from 4943.289 / 4738.721
ms to 4045.950 / 3721.084 ms (one sample each). Measurements are local and subject to load;
volume, area, 640 solids, 5,768 faces and 217,142 triangles remain identical. The cache still
rejects the oversized final entry. SolidCraft `compound_disjoint_extrusions` in `ops.rs`
at 4c30d944 informed the separated-tool approach; this is an independent OCCT implementation
using exact bounds and copied-input history, with no new dependency or mesh-derived proof.

Final #162 local gate after naming optimization: **773/773 passed in 167.08 seconds**,
schema and hygiene green. The updated native client subsequently displayed eight modules
(640 solids / 512 fins / 217,142 triangles), with successful live isolation/show-all and
Undo to the original one-module geometry. The earlier four/eight disconnections remain
recorded as pre-change observations under load, not a universal native-size limit.

Additional bounded headless stress: 4×4 builds through the C bridge (1,280 solids /
434,246 triangles). For 6×6, evaluator and indexed mesher succeed with 2,880 solids /
25,956 faces / 976,966 triangles; V = 12958187.67480281 mm³. One sample measured cold
18.130 s, mesh 4.936 s and warm 17.686 s. The benchmark's 45-second total timeout fired
before a C-bridge result, so no bridge or native success is claimed for that grid. The
7×6 input is rejected by the total mesh triangle/vertex budget. Inputs and JSONL logs
remain under `out/Nightfall-Asterion-*` and `/tmp/nightfall-162-grid-*` respectively.

## Native body inspection (#163)

Preview 0.0.6 opens the Asterion showcase by default. Each body offers hide/show and
isolate; show-all also recovers an entirely hidden viewport. Derived meshes coalesce
triangle and boundary-edge draw ranges by request-local body ID. Invalid IDs, duplicate
IDs and gaps/overlaps in face coverage are rejected before upload. Visibility changes
neither IR nor dirty/Undo state, survives parameter rebuild for current IDs, stays coherent
with last-valid geometry after failure, and resets when another document opens.

The actual XPC/Metal `bundle.visibility` test verifies eight-body coverage, isolated
pixels, fully hidden surfaces AND edges, byte-identical show-all rendering, unchanged IR,
parameter/failure/Undo recovery and document switching. Focused native render/preview/
fillet/visibility checks: **4/4 passed in 3.92 seconds**; strict Swift 6 format is clean.
Rendered default and isolated-fin PNGs were opened and visually inspected, and live
macOS screenshots of full and isolated eight-module models were inspected through CUA.
The 6×6 model also exported successfully through the production CLI as exact STEP AP242:
`out/Nightfall-Asterion-6x6.step` (90,628,475 bytes), independently of the native deadline.
SolidCraft `ui.hide` / `ui.isolate` / `ui.showAll` at 4c30d944 informed the interaction;
this implementation uses SwiftUI/Metal and existing transport, with no persisted change.

Final #163 gate, including strict complete-body face coverage: **774/774 passed in
171.81 seconds**, schema and hygiene green. Native mesh fixtures use the real body/face
transport; missing tables, unrepresented bodies and overlapping ranges fail closed.

## Native rebuild latency and surface integration (#167)

The private XPC benchmark separates the service round trip from manifest/display
conversion. On a quiet local arm64 Release build, the pre-change eight-module run took
3879.886 ms cold and 3462.816 / 3458.069 ms warm; conversion took 44–45 ms. A three-second
worker profile placed 1275/2444 worker samples in exact surface-area integration inside
snapshot validation. IPC or client conversion did not dominate that sampled interval.

Plane/cylinder faces bounded by analytic line/circle/ellipse pcurves now use Green's
theorem with a constant surface Jacobian. Forward-face orientation is used consistently
for periodic seam lookup; a failing mirrored, offset annulus regression and all Asterion
component mass checks protect inward bores. Rectangular linear-extrusion faces use a
positive 1D adaptive metric integral, fixing the independently checked elliptic annulus
area (the former generic calculation returned 313.373047 mm² versus 311.718533 mm²).
All other areas keep adaptive boundary integration at the requested 1e-9 relative accuracy.
Closed-solid validation, volume integration, geometry, provenance and mesh limits remain.
The backend identity changes with the integration algorithm.

The 1024-fin analytic regression initially took 3.086 s and failed its broad runtime
guard; the corrected integration took about 1.01 s locally. Four new surface cases pass
52 assertions, including placed/scaled sectors, elliptic holes, and mirrored/unified
periodic surfaces. The independent showcase checks pass 9375 assertions with unchanged
component volume/area expectations at 1e-5 absolute tolerance.

Native `bundle.scalability` exercises three eight-module parameter rebuilds through the
actual client/service/Metal path, compares exact component mass scaling, renders the full
row and isolated fins, and undoes to byte-identical clean source. The existing visibility
and reference render cases also pass. Native deadlines remain five seconds in the service
and seven seconds in the client; these are measured cases, not a universal size or latency
guarantee. The full local gate passes **779/779 in 169.67 seconds**, including schema,
hygiene and actual recovery/render/client cases. Strict Swift format also passes. ASan/
UBSan passes 104 kernel cases / 152450 assertions, 12 bridge cases / 65400 assertions,
and two showcase cases / 9375 assertions, without diagnostics; OCCT stays a Release dylib.
The 2.5-second fin timing assertion applies only to uninstrumented Release; all geometry
assertions also run in Debug/sanitizer builds (the initial instrumented run took 2.963 s).

The actual native Metal array renders are retained as
[`asterion-array.png`](../images/asterion-array.png) and
[`asterion-array-fins.png`](../images/asterion-array-fins.png), and visually inspected.
Dark boundary pixels count as foreground, including fins narrower than a pixel in this
fully framed eight-module row. No shading or geometry is synthesized for these images.

Final corrected three-sample eight-module median cold/warm rebuild is 1180.683 /
1010.736 ms, with median mesh 834.780 ms. Cold/warm C bridge is 2396.644 / 2218.284 ms
(one each). The final real XPC run is 2562.285 ms cold and 2378.819 / 2340.042 ms warm,
plus 42–52 ms for manifest/display conversion. Exact V = 2879597.261067266 mm³ and
A = 2124423.392761792 mm², 640 solids / 5768 faces / 217142 triangles remain unchanged.
The oversized last cache entry remains unadmitted (27 hits / 77 warm kernel calls).
Logs: `/tmp/nightfall-167-{bench-corrected,xpc-final}.jsonl`,
`/tmp/nightfall-167-check.log` and `/tmp/nightfall-167-asan-*.log`.

The final 0.0.7 extracted archive passes strict/deep signing, macOS 15.0/RPATH/dependency
closure checks for 26 embedded OCCT dylibs and contains 22 examples. Actual CUA inspection
builds 1→8 and 8→4 successfully, then 4→8 disconnects and preserves the marked last-valid
four-module result. The first Undo/reconnect also fails; subsequent Undo restores clean,
original one-module geometry with 27172 triangles and no dirty state. This remaining GUI
variant/restart sensitivity is tracked in [#171](https://github.com/wckdboy/nightfall/issues/171).
The local archive/receipt live at `out/Nightfall-0.0.7-preview-arm64-macos.{zip,json}`;
the receipt identifies the built source separately from this documentation follow-up.

An additional oblique-cylinder volume-integration stall is tracked separately in
[#168](https://github.com/wckdboy/nightfall/issues/168), with a profile and reproduction.
This surface-area change does not resolve that existing volume path.

## GUI sequence profiling and reply ordering (#171)

The actual GUI-owned service was sampled during the previously failing conduit return
and an eight-module Asterion rebuild. The conduit spent 2491/4162 worker samples in two
exact `AddOptimal` bound searches; Asterion spent 1402/3947 in the full compound validity
analyzer's construction. Process CPU advanced throughout both requests; these profiles
identify geometry work, without attributing the difference from probes to App Nap or QoS.

Single-shell snapshots now reuse their already computed exact bound as the local mass
reference. Multiple shells, cavities and separate components retain per-shell references.
Compound snapshots use OCCT's parallel geometric analyzer with all validity checks enabled.
The backend identity records both options. Analytic translated/rotated torus bounds and
unequal cavity/component measures remain checked; a malformed solid at the beginning,
middle or end of a 64-solid compound is rejected. The kernel suite passes 124 cases /
158552 assertions, retaining 1e-9 relative integration accuracy and immutable snapshots.

A separate client regression failed with `received reply lost to disconnect`: a subsequent
connection event could beat off-actor packet validation and discard a received timeout
culprit. A thread-safe receipt now claims a packet on its callback queue. Disconnects still
retire the transport; packet validation, cancellation and the original seven-second
backstop still apply. The eight-MiB regression covers both connection/proxy errors, success
and timeout recovery, the original source and fresh connection reuse. All 18 client/TSAN
cases pass. No additional retry, deadline increase or protocol/schema change is introduced.

The native document test executes Asterion then two conduit/edit/Undo/loft/edit/Undo rounds
and a final conduit reload, checking exact measures, source bytes and real Metal rendering.
It improved from 14.71 s to 11.23 s; the final conduit changed from 2210.832 to 1637.484 ms.
The large native test now exercises 1→8→4→8 and every Undo step, with exact per-body measures
at 1e-5 absolute tolerance and 27172/217142/108582/217142 display triangles. Its focused run
passes in 9.99 s. Both sequences, including clean Undo, were also completed in the live app.
These are bounded local regressions, not a universal complexity or latency guarantee.
The parent sweep PR's [OCCT run](https://github.com/wckdboy/nightfall/actions/runs/39340711080)
also reproduces #171: 817/818 pass, with the eight-module test disconnecting after 5.66 s.
That older run does not contain these fixes.

The final required local gate passes **822/822 in 200.57 s**, including schema/hygiene,
real XPC/Metal sequences and client TSAN. ASan/UBSan with halt-on-error also passes the
complete kernel (124 cases / 158551 assertions), evaluator (367 / 444301) and C bridge
(12 / 65400) suites without sanitizer diagnostics. Preview 0.0.10 contains these fixes;
its packaging and extracted-GUI verification are recorded separately below.

Preview archive: `out/Nightfall-0.0.10-preview-arm64-macos.zip` (17401190 bytes), source
`60279fee433d99d41362ae3d22920d2bba8c4afc`, SHA-256
`1895e58689a723a6f4732082ef1e6490ab6b22c1f740488b3ca40e9c12b0868d`.
The receipt pins that source independently of this documentation follow-up. The extracted
app is byte-identical to the tested build; strict signatures, deployment target, dependency
closure, 26 service-local OCCT dylibs and all 24 example sources pass verification.
The actual extracted app completes 1→8→4→8 and all three Undo steps, followed by
conduit/edit/Undo/loft/edit/Undo/conduit without errors or recovery notices. Both the
217142-triangle Asterion and final 23494-triangle conduit were visually inspected.
The app remains open on the clean conduit. This is an ad-hoc arm64/macOS 15+ development
preview, without notarization or a tagged-main/MVP release; earlier previews are preserved.

## Sweep document and native integration (#179)

The existing v1 sweep operation now binds the current profile and named open sketch path,
prepares exact curves through the shared adapter, and calls the typed kernel from #178.
Path/profile hashes participate in dependency keys; current source IDs rebind on publication.
The existing new/join/cut/intersect, suppression, rollback and bounded-cache paths apply.
Selector paths and closed/point entities fail at their input pointers. The evaluator identity
is `evaluator/29;roles/6`; there is no schema, dependency or public AI-tool expansion.

`swept_conduit.nightfall` adds a five-feature cubic spline with an inflection, outer sweep
and swept through-bore cut. The default independent arc length is 212.2747402664 mm,
V=50016.0573422607 mm³ and A=33815.2771262122 mm². Positive independent centerline quadrature
checks volume/area within 1e-3 at two reach/lift settings. Seven focused evaluator cases /
255 assertions also cover straight/arc mass, current ancestry, cache eviction, path/profile
edits and renames, planar cap selectors, all result modes, precise errors and recovery.
The complete evaluator ASan/UBSan suite passes 367 cases / 444301 assertions; the bridge
passes 12 cases / 65400 assertions, with halt-on-error and zero sanitizer diagnostics.

The actual `bundle.sweeps` test passes in 5.82 s through XPC/client/Metal, rendering baseline
and edited conduit, checking exact mass, clean Undo, bore suppression, invalid-edit
last-valid recovery and unchanged example bytes. Its real render is retained at
`docs/images/swept-conduit.png` and has been visually inspected. In-place GUI testing after
switching among older app copies exposed two kernel-service disconnects without a returned
hung-feature packet; no root cause or universal GUI reliability is inferred from probe
success. The complete local gate passes 818/818 in 187.70 s (bundle.sweeps 5.44 s).
The extracted 0.0.9 app successfully displays the conduit (23494 triangles), a batch
reach/lift edit (26622 triangles) and clean Undo, then the loft/waist edit/clean Undo.
Returning from loft to conduit later disconnects again; reliable document-switch sequences
remain #171, rather than a claimed fix. The archive is byte-identical to the validated
built app, with strict signatures/dependency closure, 26 OCCT dylibs and 24 examples.
This is a visual development preview, not a production/MVP sign-off. Existing watchdog/
deadline limits remain unchanged; no retry masking is introduced.

## Planar path sweep kernel (#178)

Accepted RFCs 0005/0007 cover the typed v1 sweep boundary. `IKernel::sweep` accepts a
closed profile and an open planar path. A fixed path-plane binormal preserves authored
profile roll, including through inflections; the profile origin/normal must match the
path start/forward tangent. There is no automatic contact placement, frame correction,
corner transition, scaling or mesh fallback. Construction errors above the configured
linear tolerance are rejected. The backend identity records the new frame semantics.

Initial limits are one profile without holes (128 curves / 1024 spans), 32 path curves /
64 spans, connected G1 joins, and strict control-pole progress along every exact Bezier
span's chord. Positive rational weights make this a conservative regularity certificate,
not a sampled cusp test. Closed paths, spatial paths, guide rails, corners and uncertified
spans are explicitly unsupported. Input path/profile and output solids receive validity
and self-interference checks. Every output face and authored profile curve must have
complete cap/side provenance before the immutable snapshot is published.

Nine tests / 1982 assertions pass in Release and ASan/UBSan: independent straight mass
(240 mm³ / 376 mm²), rectangular bend (80π mm³ / 16+120π mm²), rational circular tube,
nonrational spline profile, quadratic and inflected cubic paths, tangent line/arc joins,
reversed traversal, authored profile roll, million-millimetre placement, cap orientation,
all-face coverage, STEP round trip, display mesh, retained readers and atomic rejection.
Spline-path mass oracles use independent positive arc-length quadrature (absolute 1e-4);
analytic bend mass/area tolerance is 1e-5. The first bound fixture accidentally aliased a
vector element during reallocation; ASan identified the test-only use-after-free, and the
fixture now retains a value copy before resizing. No sanitizer suppression is used.
The complete local gate passes 810/810 in 178.57 s; the full kernel ASan/UBSan suite
passes 123 cases / 158518 assertions with halt-on-error and zero diagnostics.
This backend slice does not yet connect v1 sweep documents or the native client; the
0.0.8 archive remains the previously validated loft development preview.

## Solid loft kernel (#172)

Accepted RFCs 0005/0007 cover a typed solid loft under the existing v1 operation.
The first backend slice joins 2..32 ordered profiles on parallel planes: two sections
interpolate linearly, while three or more use smooth interpolation with uniform section
parameters and maximum longitudinal degree three. There is no ruled fallback. Initial
bounds are one outer loop per section, 128 authored curves per loop and 1024 total knot
spans. Holes, rails, point sections and nonparallel planes remain unsupported.

Section and output checks include OCCT self-interference diagnostics as well as complete
closed-solid validation. Cap roles and side ancestry cover every current output face;
side records retain the section/region/loop/authored-curve address from every input
section, including decomposed multi-span curves and reversed/cyclic nonconvex profiles.
No names are inferred by proximity or persisted topology indices. Inputs are immutable.

Independent checks cover a rectangular frustum (5600 mm³), a smooth three-section bulge
(4320 mm³), placed circular sections (210π mm³), tapered ellipses (420π mm³), quadratic
spline profiles (240/560 mm³), and a smooth rational circular bulge (486π mm³). Profile
samples are checked against their attributed side faces; mesh winding/area/volume, STEP
reload, retained owners, failure atomicity and explicit input bounds are also checked.

The spline prism regression exposed generic boundary quadrature returning area
304.5222705950 mm² versus the independent 304.5216528199 mm². Proven rectangular UV
patches on B-spline/Bezier surfaces now use positive nested Gauss–Kronrod metric integration,
split at surface knots, with bounded refinement and convergence checks. Trimmed/nonrectangular
faces retain the existing path. This does not fix the separate oblique volume stall #168.
Focused evidence: ten loft cases / 4086 assertions pass, including a smooth interpolation
that collapses between positive sections, reversed plane normals, cap placement and
near-coincident planes. A final red regression catches an authored zero-length curve
silently skipped by wire construction; loft now rejects such collapsed curves to preserve
complete ancestry. The final full Release gate passes 789/789 in 189.87 s, with schema,
hygiene and format checks. This backend is pushed in draft PR #175 (c6fb64c). Initial ASan/UBSan runs aborted in
OCCT's incremental allocator during the existing sphere mesher and loft preservation
mesh. Mixed-size allocation alignment is tracked as the release blocker
[#174](https://github.com/wckdboy/nightfall/issues/174); the following patch clears that local blocker.
Evaluator/cache/selector integration and a native editable example follow in
[#173](https://github.com/wckdboy/nightfall/issues/173); this backend increment alone
does not expose loft in the client.

## OCCT incremental allocation alignment (#174)

Loft validation exposed a misaligned integer-list deleter callback from OCCT's existing
sphere mesher into instrumented Nightfall code. A direct mixed-allocation regression
also fails: after one byte, the next fundamentally aligned object is returned at an odd
address. The pinned 7.9.3 incremental allocator advances by raw requested sizes, without
padding. Passing Release geometry tests did not prove those accesses were defined.

A minimal distributed LGPL patch rounds allocations to `alignof(std::max_align_t)`,
checks size/rounding overflow, gives zero-size requests progress, and verifies block-header
alignment without changing public ABI. Source checkout checks accept clean upstream or
exactly the pinned patch and preserve unrelated edits on failure. Installation receipts,
configure verification, backend cache identity and CI cache keys include its SHA256:
`d7887821ad973f2b2928fd4dcfabed5a4a4920d61a177634f865f9a8175054e4`.
An existing local build is reused only when its cache confirms the identical source path.

The allocator regressions pass 6539 assertions across two cases, including payload
preservation, aligned object accesses, block growth, reset/reuse/release and overflow.
The previously failing ASan/UBSan sphere test now passes 94895 assertions against the
patched Release dylib, with the same instrumented binary and no suppression. Full kernel
sanitizers pass 114 cases / 156536 assertions, including all ten loft cases; the C bridge
passes 12 / 65400, showcase 2 / 9375 and allocator 2 / 6539. Nightfall and header callbacks
are instrumented; the external OCCT dylib remains Release. The final full application gate
passes 791/791 in 178.87 s, including schema/hygiene/format and actual XPC/Metal/scalability
checks. Pushed in draft PR #176 (4bac72c / 3dcf4aa); remote CI/review remain pending.
The unchanged build script reuses the matching
receipt without recompilation. Logs: `/tmp/nightfall-174-{allocator-red,allocator,asan-*}.log`.

## Loft documents and native example (#173)

The evaluator prepares every ordered v1 loft profile through existing exact region/fill
selection and binds each section's sketch dependency. New/join/cut/intersect results use
the existing atomic publication and current history path. Cached sources retain section-
local addresses and authored entity IDs; publication binds current feature/sketch IDs.
Cache identity changes with the added operation and naming scheme, and accounting includes
every section's retained source map. Referenced ID edits conservatively miss, as permitted
by RFC 0007; renaming the current loft producer reuses geometry and rebinds names.

Nine evaluator cases pass 639 assertions, covering each section's edits, warm reuse,
eviction/retained readers, source selectors, region picks, result modes, rollback,
suppression, failure recovery, unsupported modes and precise profile diagnostics.
`lofted_link.nightfall` adds a ten-feature three-ellipse housing, flange and five holes.
Independent polynomial section integration and positive surface-metric quadrature give
V = 160612.8772172270 mm³, A = 35752.4962832179 mm² and centroid z = 59.6972498598 mm.
Mass checks use 1e-5 absolute tolerance, centroid 1e-7 and derived mesh mass within 1%.
STEP reload is measured with Nightfall's validated snapshot integrator; generic OCCT
boundary quadrature returned volume 0.000677 mm³ low at a requested 1e-9 relative accuracy.

The real `bundle.lofts` test passes in 1.94 s through XPC/client/Metal, exercising batch
height/waist edits, clean byte-identical Undo, loft suppression, failure/last-valid recovery
and unchanged example bytes. Its actual render is retained in `docs/images/lofted-link.png`
and visually inspected. Live CUA inspection opens **Examples → Lofted Link**, fits the
model, changes waist_scale 1→1.1 and undoes to the clean original: 8934 triangles, with
correct measures and no errors. Preview version is 0.0.8 with 23 embedded examples.
The final full gate passes **801/801 in 177.82 s**, with 202 schema cases, twenty reference
schemas, hygiene/format, actual XPC recovery, Metal and scalability checks. ASan/UBSan
passes all 360 evaluator cases / 444046 assertions and twelve bridge cases / 65400,
without diagnostics. Strict Swift format is clean. Logs: `/tmp/nightfall-173-check.log`,
`/tmp/nightfall-173-asan-{evaluator,bridge}.log` and `/tmp/nightfall-173-native.log`.

The final archive is byte-identical to the validated app bundle, passes strict/deep signatures,
arm64/macOS 15.0/RPATH/dependency closure checks, and contains 26 OCCT dylibs / 23 examples.
Actual CUA inspection from the extracted archive opens Lofted Link, changes waist_scale
1→1.1 (8886 triangles / updated exact measures), and undoes to the clean 8934-triangle model.
It is left open for visual testing. This is an ad-hoc development preview; no formal
main-branch release, notarization or complete CAD/robotics MVP is claimed.

Parent PR #176's primary OCCT CI (including C/C++ sanitizers and Swift TSAN), standard CI,
SMLib and hygiene pass. Its optional dual-kernel/USD run fails only `bundle.scalability`
(798/799 passed): the first eight-module rebuild disconnects after 5.70 s with
`kernel_restarted`. This is recorded in #171 with the log/run link; no precise cause,
universal eight-module capacity or full remote-green result is claimed. Native deadlines
remain unchanged. Sweeps, guide rails, nonparallel lofts and interactive spline authoring
remain unsupported.

## Work items (build order)

Tracked as GitHub issues in the [`Phase 2` milestone](https://github.com/wckdboy/nightfall/milestone/3).

| Issue | Item | Scope | Status |
| --- | --- | --- | --- |
| [#89](https://github.com/wckdboy/nightfall/issues/89) | 2.0 RFC 0005: feature IR schema v1 | ir | Done (RFC 0005) |
| [#90](https://github.com/wckdboy/nightfall/issues/90) | 2.1 IR schema v1, validation and migrations | ir | Schema, document I/O and reference validation merged in #103; v1 is the first format |
| [#91](https://github.com/wckdboy/nightfall/issues/91) | 2.2 Expression engine with units, bounds and dependency graph | ir | Expressions, dependency graph and bounds merged in #103 |
| [#92](https://github.com/wckdboy/nightfall/issues/92) | 2.3 Evaluator: feature hashing, incremental rebuild, cache | kernel | Done: evaluator/cache and local benchmark merged in #103; further operation/selector coverage remains open |
| [#104](https://github.com/wckdboy/nightfall/issues/104) | Exact curved sketch profiles: arcs, ellipses and NURBS splines | kernel | Implemented with regression tests and local benchmark evidence |
| [#106](https://github.com/wckdboy/nightfall/issues/106) | Shared straight boundaries in sketch profiles | math | Implemented with arrangement, region and evaluator regressions; local benchmark evidence |
| [#108](https://github.com/wckdboy/nightfall/issues/108) | Analytic circular profiles and fast exact mass integration | kernel | Implemented with analytic-surface regressions; local verification and rebuild evidence below |
| [#109](https://github.com/wckdboy/nightfall/issues/109) | Certified shared curved spans in sketch profiles | math | Implemented with rational-curve, fill-rule and production evaluator regressions |
| [#110](https://github.com/wckdboy/nightfall/issues/110) | Verify line and radius constraints in evaluated sketches | kernel | Implemented with tolerance, unit, dependency and cache regressions; local evidence below |
| [#111](https://github.com/wckdboy/nightfall/issues/111) | Resolve point references and verify point constraints | kernel | Implemented with reference-safety, point-retention, constraint and solid regressions |
| [#112](https://github.com/wckdboy/nightfall/issues/112) | Verify analytic tangent constraints in evaluated sketches | kernel | Implemented with finite-contact, unit, tolerance and solid/cache regressions; local verification below |
| [#113](https://github.com/wckdboy/nightfall/issues/113) | Verify angle dimensions and fixed sketch references | kernel | Implemented with direction, tolerance, reference and solid/cache regressions; local evidence below |
| [#114](https://github.com/wckdboy/nightfall/issues/114) | Bound polynomial mass integration for planar solids | kernel | Implemented with unconstrained latency, analytic geometry, placement, cavity and curved-trim regressions; local evidence below |
| [#115](https://github.com/wckdboy/nightfall/issues/115) | Resolve sketch-line axes for revolutions and rigid transforms | kernel | Implemented with directed-line, frame, dependency, cache and ownership regressions; local evidence below |
| [#116](https://github.com/wckdboy/nightfall/issues/116) | Open reference sketches with closed solid profiles | kernel | Implemented with standalone-axis, profile-boundary, exact-curve, validation and cache regressions; local evidence below |
| [#117](https://github.com/wckdboy/nightfall/issues/117) | Parametric linear body patterns with complete provenance | kernel | Implemented with fused/separated arrays, input bounds, dependency, naming and atomic-failure regressions; local evidence below |
| [#118](https://github.com/wckdboy/nightfall/issues/118) | Circular body patterns with sketch-line axes | kernel | Implemented with signed sweeps, angular bounds, nested arrays, axis dependencies, naming and failure regressions; local evidence below |
| [#119](https://github.com/wckdboy/nightfall/issues/119) | Exact body mirrors with complete reflection history | kernel | Implemented with typed reflection, oriented history, immutable inputs, plane edits and atomic-publication regressions; local evidence below |
| [#120](https://github.com/wckdboy/nightfall/issues/120) | Sketches attached to generated planar faces | kernel | Implemented with oriented face queries, current provenance, boolean transfers, exact cardinality and cache-binding regressions; local evidence below |
| [#121](https://github.com/wckdboy/nightfall/issues/121) | Body-face selectors and planar direction filters | kernel | Implemented with immutable face enumeration, signed world directions, exact cardinality and cache/failure regressions; local evidence below |
| [#122](https://github.com/wckdboy/nightfall/issues/122) | Nearest attachment faces by trimmed B-rep distance | kernel | Implemented with exact-geometry distance queries, global candidates, tolerance ties and cache/failure regressions; local evidence below |
| [#123](https://github.com/wckdboy/nightfall/issues/123) | Recursive face-selector unions and exclusions | kernel | Implemented with per-node cardinality, precise nested errors, complete dependencies and shared query descriptions; local evidence below |
| [#124](https://github.com/wckdboy/nightfall/issues/124) | Mirror bodies across selected current planar faces | kernel | Implemented with shared face resolution, separate source/target bindings, current dependencies and cache ownership regressions; local evidence below |
| [#125](https://github.com/wckdboy/nightfall/issues/125) | CLI parameter-expression overrides for variant exports | cli | Implemented with batch validation, exact STEP variants and immutable source files; local evidence below |
| [#126](https://github.com/wckdboy/nightfall/issues/126) | Generated side-face selectors qualified by sketch entity | kernel | Implemented with exact curve ancestry, current naming, cache and atomic-failure regressions; local evidence below |
| [#127](https://github.com/wckdboy/nightfall/issues/127) | Cylindrical face selectors as modeling axes | kernel | Implemented with exact cylinder descriptions, radius filters, three axis consumers and cache/failure regressions; local evidence below |
| [#128](https://github.com/wckdboy/nightfall/issues/128) | Extrusions terminating on selected parallel supporting planes | kernel | Implemented with supporting-plane resolution, exact displacement, result/cache/failure regressions and local evidence below |
| [#129](https://github.com/wckdboy/nightfall/issues/129) | Equal-distance chamfers on selected face-boundary edges | kernel | Implemented with immutable exact solids, complete history, recursive edge sets and atomic cache publication; local evidence below |
| [#130](https://github.com/wckdboy/nightfall/issues/130) | Immutable indexed viewport meshes with current face associations | kernel | Implemented under RFCs 0003/0007 for Phase 2 native integration; mesh/ownership/reference regressions and local evidence below |
| [#135](https://github.com/wckdboy/nightfall/issues/135) | Relocatable embedded XPC service and process recovery | kernel | Production service, headless host and real crash/hang/replay/relocation checks implemented; local evidence below |
| [#137](https://github.com/wckdboy/nightfall/issues/137) | Bounded native app kernel client | app | Implemented with cancellation, recovery and real XPC replay; PR #139 CI green, review pending |
| [#138](https://github.com/wckdboy/nightfall/issues/138) | Production Metal viewport | app | Twenty reference meshes render on Metal; PR #141 CI green, review pending |
| [#142](https://github.com/wckdboy/nightfall/issues/142) | Editable native document model | app | Immutable parameter edits, revision ordering, undo/redo and last-valid geometry implemented; PR #143 review pending |
| [#140](https://github.com/wckdboy/nightfall/issues/140) | First runnable native visual MVP | app | Development app built and visually tested with production XPC/OCCT; final local gate 743/743, evidence below |
| [#145](https://github.com/wckdboy/nightfall/issues/145) | Native suppression and inclusive history rollback | app | History edits, undo/redo and valid empty construction states implemented; validation below |
| [#149](https://github.com/wckdboy/nightfall/issues/149) | Atomic pending parameter application and saving | app | Implemented and packaged in preview 0.0.3; native sheet validation completed by #153 below |
| [#151](https://github.com/wckdboy/nightfall/issues/151) | Seeded direct-engine edit/failure/replay validation | kernel | Analytic, immutable ownership and exact STEP/mesh checks pass; evidence below |
| [#153](https://github.com/wckdboy/nightfall/issues/153) | Retained asynchronous native Open/Save sheets | app | Save, reopen, cancellation and Keep editing verified; preview 0.0.4 and local gate below |
| [#155](https://github.com/wckdboy/nightfall/issues/155) | Typed constant-radius fillets with complete immutable provenance | kernel | Exact OCCT rounds, complete topology-based ancestry and immutability implemented; evidence below |
| [#157](https://github.com/wckdboy/nightfall/issues/157) | Execute v1 document fillets and native rounded-part preview | kernel | Evaluator, CLI and native edits/save/reopen implemented and visually verified; evidence below |
| [#159](https://github.com/wckdboy/nightfall/issues/159) | Chained, concave and hole-adjacent fillet validation | kernel | Analytic combination/history regressions pass; no runtime change, evidence below |
| [#93](https://github.com/wckdboy/nightfall/issues/93) | 2.4 Nightfall Script: JavaScriptCore host and nf API bindings | script |  |
| [#94](https://github.com/wckdboy/nightfall/issues/94) | 2.5 CLI: `nf build part.nightfall --out part.step` | cli | Implemented with exact AP242 export, bounded input, structured errors and atomic output; all twenty reference exports reload locally, CI/review pending |
| [#95](https://github.com/wckdboy/nightfall/issues/95) | 2.6 20 reference parts and the Phase 2 gate | evals | Twenty editable reference documents and headless regressions added; CI confirmation and full gate sign-off pending |
| [#74](https://github.com/wckdboy/nightfall/issues/74) | Coincident edges in planar arrangements (from Phase 1) | math | Straight spans in #106 and certified curved spans in #109; other parameterizations remain open |

## Log

- 2026-10-17 — Phase started after the Phase 1 gate. The IR schema RFC (0005) comes first;
  expressions build on libnf-math's expression language (#80), the evaluator on the OCCT kernel
  layer (#55) inside the kernel service (RFC 0003).
- 2026-10-17 — RFC 0005 accepted (review period waived by the maintainer): one JSON document,
  sketches inside the feature history, selectors instead of indices, content hashes of
  evaluated inputs, a new kernel-free module `core/ir`.
- 2026-10-17 — #90 document envelope: typed parameters, references and feature history;
  version checks, unique IDs, rollback validation, bounded load/save and preserved extensions.
  This is structural validation only. The normative JSON Schema, per-feature inputs,
  selectors, body/entity references and schema corpus remain in #90; expressions, units
  and parameter dependency evaluation remain in #91. No Phase 2 gate is claimed yet.
- 2026-10-17 — #90 publishes the JSON Schema 2020-12 definitions for all 16 feature types,
  sketch entities, constraints and semantic selectors. A shared valid/invalid corpus is
  independently checked with pinned Python test tooling in CI; native enforcement follows.
- 2026-10-17 — #90 native schema enforcement: load/save uses the embedded published schema,
  including recursive selectors and mutually exclusive operation forms. Native and Python
  checks share the corpus; reference annotations are collected for the semantic checker.
- 2026-10-17 — #90 semantic checks enforce body/entity namespaces and typed backward references
  across sketches, selectors, operations and external inputs, including rolled-back history.
  The shared corpus has 184 cases. v1 is the first format: older/newer versions are refused;
  there are no legacy files to migrate. Unit/expression evaluation remains #91, geometry #92.
  `nf_ir_bench` measures complete document load/save for 30- and 300-feature histories.
  Local Release medians (5 × 100, warm schema): 30 features load in 0.235 ms and edit/save
  in 0.370 ms; 300 features in 3.015 ms and 3.690 ms. These do not measure geometry rebuilds.
  `tools/check.sh`: 170/170 passed. ASan/UBSan: 169/170, no sanitizer diagnostics; the
  unchanged closest-point timing test exceeded 2 s (4.805 s), then passed alone in 1.87 s.
- 2026-10-17 — #91 reuses the math expression grammar for parameter names and numeric unit
  suffixes, retaining source locations for diagnostics. Both parser modes bound source size,
  node count and recursion; equation curves retain their numeric semantics.
- 2026-10-17 — #91 dimensional expressions normalize lengths to mm and angles to radians,
  check arithmetic/functions and reject non-finite results. Bare JSON field numbers use
  document units; parameter numbers and bare numbers inside expressions are dimensionless.
  Compiled expressions are reusable. Declared parameter names take precedence over pi/e;
  function syntax and numeric unit suffixes remain unambiguous.
- 2026-10-17 — #91 resolves parameter values in dependency order and checks inclusive bounds
  afterward. Load/save rejects cycles, unknown names, unit mismatches and non-finite results,
  while preserving authored expressions. Iterative traversal handles a 10,000-parameter chain;
  source/instruction/diagnostic limits bound generated inputs. Feature evaluation remains #92.
  Development checks pass 189/189 tests; the shared corpus now has 202 cases, and all five
  Swift integration tests pass. Release medians (5 × 100): edit/compile/evaluate/bounds for
  30 chained parameters takes 20.1 µs, for 300 takes 201.6 µs. No geometry is evaluated.
  Full ASan/UBSan checks also pass 189/189 tests (160.11 s), with no sanitizer diagnostics.
- 2026-10-17 — #92 adds kernel-free SHA-256 identities for evaluated features and chained
  body states, using RFC 8785 canonical JSON and CommonCrypto. Known-answer tests cover
  SHA-256, canonical envelopes, external digest normalization and upstream ordering.
  Geometry execution, incremental rebuild timings and the Phase 2 gate remain pending.
- 2026-10-17 — #92 compiles feature inputs once using dimension annotations in the existing
  schema (no format or validation-rule change). Evaluations normalize lengths, angles and
  scalars, check integer counts/degrees, preserve field diagnostics and exclude opaque
  extensions from executable inputs. Equivalent evaluated values share hashes; unused
  parameters do not invalidate them. Script and equation-curve binding remain explicit
  unsupported-evaluation errors. The benchmark now measures input/hash preparation separately
  from geometry. Local checks: 203/204 passed; the unchanged closest-point timing assertion
  exceeded 2 s under concurrent external compiler load (4.87 s). All new tests passed.
- 2026-10-17 — #92 adds the service's LRU cache with explicit entry/estimated-byte limits,
  safe replacement/eviction and retained reader ownership. A 2,000-operation reference-model
  test checks ordering/accounting; OCCT integration verifies a translated cached solid retains
  volume 6,000 mm³, area 2,200 mm², six faces and twelve edges, without changing its source.
  The rerun passes 210/210 development tests (68.68 s); OCCT checks pass 226/226 (12.78 s).
  Full ASan/UBSan checks pass 210/210 (297.64 s), with no sanitizer diagnostics.
  The cache is a tested service component; feature execution and provenance integration remain
  pending, and no production geometry-rebuild gate is claimed.
  Release medians (5 × 100, prepared feature programs): parameter edit, input evaluation and
  chained hashing take 86.8 µs for 30 features and 1,425.0 µs for 300. These exclude geometry
  execution. Full document load/edit-save takes 282.3/344.8 µs for 30 features and
  2,895.9/4,137.0 µs for 300; chained parameter evaluation takes 18.8/334.1 µs respectively.
- 2026-10-17 — Draft [RFC 0007](../rfcs/0007-kernel-evaluator-contract.md) supplies the
  production `IKernel` contract explicitly deferred by RFC 0001: immutable ownership, typed
  operations, current-body dependencies, safe cached-provenance binding and a staged
  executable evaluator. This is a proposal; acceptance is required before that interface
  is implemented. RFC 0006 remains reserved for Nightfall Script (#93).
- 2026-10-17 — RFC 0007 accepted by the maintainer in the project conversation, explicitly
  authorizing implementation and waiving the review wait. The geometry evaluator proceeds
  in small #92 branches; this does not waive implementation checks or sign off the phase gate.
- 2026-10-17 — #92 introduces kernel-neutral immutable shape/entity handles and OCCT solid
  snapshot validation. Analytic tests cover a 6,000 mm³ box, two solids totaling 12,000 mm³,
  and a 1,968 mm³ enclosed cavity; empty/stray geometry is rejected. Entity ownership survives
  the request. OCCT checks pass 230/230 (17.48 s); production modeling operations follow.
- 2026-10-17 — #92 adds typed production extrusion and rigid transforms, including circular
  holes, multiple solids, placed planes and immutable face/edge history. Exact knot-span
  decomposition and span-aware volume integration fix measurements of rational profiles.
  Annulus plus island: volume 553π mm³ and area 396π mm², both within 1e-5 after rotation.
  OCCT checks pass 236/236 (12.43 s); service-side IR execution remains the next slice.
- 2026-10-17 — #92 service adapters construct exact rectangle/circle/polygon/closed-line
  profiles from normalized inputs, select regions by interior points, place datum/explicit
  planes and prepare symmetric/directed extrusions and rigid transforms. Open/ambiguous
  chains and unsupported constraints/selectors/modes return field diagnostics. OCCT checks
  pass 240/240 (15.86 s); incremental document execution follows these tested adapters.
- 2026-10-17 — #92 executes complete IR documents through the production backend with
  current-body hashes, bounded geometry caching, rollback/suppression, atomic multi-body
  publication and explicit downstream failure states. Operation-local names rebind on cache
  reuse; retained history is rejected when an evicted upstream snapshot has been rebuilt.
  Tests exercise 30-feature histories and verify unused/equivalent-unit edits do no kernel
  work. OCCT checks pass 252/252 (14.51 s); measured rebuild latency follows separately.
- 2026-10-17 — #92 measures real geometry and optimizes rigid transforms: shared immutable
  B-reps, checked location/orientation/isometry invariants, preserved volume/area and translated
  bounds. New geometry still receives full OCCT validation; rotations recompute bounds.
  Tests independently remeasure moved B-reps and their unchanged sources. The benchmark below
  meets the local 30-feature latency criterion; the full Phase 2 gate remains open.
- 2026-10-17 — #92 adds exact solid union, subtraction and intersection, with tools treated
  as a union. OCCT runs in non-destructive mode and validates the complete result. History
  covers every input face/edge, binds cutting faces with their output orientation and records
  new intersection edges. Tests check unchanged source B-rep serialization, circular holes,
  cavities, split solids, disjoint and empty results. The evaluator hashes current target/tool
  states, publishes naming before consuming tools, supports `keep_tools`, and rejects stale
  histories after eviction. Fourteen new regression cases cover geometry, tool lifecycle,
  failures, cache rebinding, rollback and suppression; remaining operations/selectors are pending.

- 2026-10-18 — #92 implements extrusion `join`, `cut` and `intersect` results with current-body
  dependencies and atomic publication. Temporary swept solids stay private and retained only
  as cache/history inputs. Target and tool face names survive the boolean and rebind to renamed
  features on cache hits. Seven new regressions cover analytic box combinations (9,000/3,000/
  3,000 mm³), a symmetric circular through-cut (6,000 − 120π mm³, tolerance 1e-5), target motion,
  suppression, rollback, failed combinations and input-snapshot eviction.
  Full OCCT checks pass 273/273 (16.16 s); ASan/UBSan evaluator tests pass 32/32 (28.69 s),
  with no sanitizer diagnostics. The independent schema corpus remains 202/202.

- 2026-10-18 — #92 adds exact solid revolutions about explicit axes in the sketch plane,
  including signed partial/full turns, new/join/cut/intersect results and face provenance.
  Tests cover hollow cylinders, toruses, cavities, placed axes, axis-touching profiles,
  selected regions, dimensional angles, incremental edits, cache rebinding, eviction,
  rollback, suppression and failure propagation. Profile control hulls must stay on one side
  of the axis; conservative rejection prevents overlapping sweeps without approximating curves.
  Exact linear spans now produce analytic planes/cylinders/cones in OCCT; the original profile
  provenance is retained while edge parameterization changes. Rational curves remain exact.
  Sketch-line/selector axes and the other remaining v1 operations still report errors.
  Full OCCT checks pass 287/287 (19.06 s); full ASan/UBSan checks pass 287/287 (663.87 s),
  with no sanitizer diagnostics or retries. The latter ran under substantial background build
  load. Hygiene checks and the independent 202-case schema cross-check pass. Nightfall code is
  instrumented; OCCT remains the pinned Release shared-library build.

- 2026-10-18 — The feature-IR/evaluator stack is integrated through [PR #103](https://github.com/wckdboy/nightfall/pull/103),
  main commit `4fe1319`. Its tree matches the validated `369938b` exactly. All five GitHub check
  groups passed; the maintainer explicitly authorized the merge in the project conversation.
  Superseded stacked PRs #97/#100/#101/#102 are closed with their source branches retained.
  Issues #90/#91/#92/#98/#99 and their Plane counterparts are complete. The full Phase 2 gate
  remains open; subsequent capability work uses dedicated issues.

- 2026-10-18 — #104 adds exact signed arcs, rotated ellipses and clamped rational/non-rational
  NURBS splines to the existing sketch schema. Mixed line/arc/spline chains may be unordered
  or reversed; both endpoints are checked for open/ambiguous junctions. Exact span decomposition
  prevents region probes from landing on spline corners. Knot/pole/weight errors carry field
  pointers; initial sketch-curve, spline-pole and profile-span work limits are each 4,096.
  Sixteen regressions cover semicylinders (200π mm³), placed ellipses (72π mm³), a rational
  circle, a closed polyline spline, a sphere (4000π/3 mm³), holes/region picks, parameter edits,
  equivalent-unit cache reuse, failed dependencies and malformed/bounded inputs. Review found
  accepted near-coincident joins could remain disconnected in region detection; explicit bridges
  now preserve the original curves and connect those endpoints. Bridges below OCCT's vertex
  precision use vertex welding, avoiding degenerate edges. Regressions cover line/line,
  line/arc and arc/arc gaps of 5e-7 and 5e-8 mm, including the closing junction. Volume
  tolerances are 1e-8–1e-5 mm³; centroid tolerances are 1e-7 mm. No schema/API/dependency change.
  Full Release `tools/check.sh --preset occt` passes 303/303 (17.91 s), with hygiene and the
  independent 202-case schema cross-check passing. The CI ASan/UBSan kernel/evaluator/cache
  selection passes 89/89 (27.77 s), with no diagnostics or retries. Nightfall code is instrumented;
  OCCT remains the pinned Release shared-library build. The prior integrated tree also passed
  the full 287-test sanitizer suite; this slice reruns the affected geometry/service selection.

- 2026-10-18 — #106 supports coincident degree-1 boundaries in planar arrangements. Overlap
  endpoints split both inputs using their original rational parameterization; one half-edge pair
  retains the signed sum of traversals. Zero-count seams remain for topology and disappear from
  filled outlines. Nonzero/even-odd regions now handle adjacent rectangles, partial overlaps,
  repeated/reversed outlines and notches. Empty profiles fail at `/inputs/profile` before kernel
  work; general curved coincidence remains an explicit #74 error. Evaluator identity is bumped.
  Sixteen additional tests include a fixed-seed 128-case analytic rectangle corpus, rational
  parameters, multi-span polylines, crossing seams, scale/rotation/order, separated parallel
  edges and actual OCCT extrusion/cache behavior. The original ten math and four evaluator
  reproductions failed before implementation. Full Release `tools/check.sh --preset occt`
  passes 319/319 (23.50 s), hygiene and the independent 202-case schema corpus pass.
  Targeted local ASan/UBSan passes 40 math cases (20,936 assertions across arrangement,
  region, intersection and refinement tests) plus 93 kernel/evaluator/cache cases (74.58 s),
  with no diagnostics or retries. Nightfall is instrumented; OCCT uses pinned Release libraries.

- 2026-10-18 — #95 adds [20 editable reference parts](../../examples/parts/README.md) with
  53 authored features: brackets, an enclosure, turned parts, curved profiles and solid
  booleans. Twenty geometry tests plus a catalogue check run through the production evaluator.
  Each part has three analytic parameter cases, cold/incremental comparisons, equivalent-unit
  reuse, cache restoration and eviction with retained-snapshot checks. Volumes use 1e-5 mm³,
  centroids 1e-6 mm and bounds 1e-5 mm absolute tolerances. Nineteen parts also check analytic
  surface area; the elliptic duct checks finite positive area. The spline wedge retains a
  separate 2e-4 mm² area budget for OCCT numerical integration (observed error up to 1.73e-4);
  other areas use 1e-5 mm². No runtime API, schema or dependency change. The independent schema
  check now includes all 20 files, and example edits trigger Release and ASan/UBSan geometry CI.
  Full Release `tools/check.sh --preset occt` passes 340/340 (504.46 s), with hygiene and both
  schema corpora (202 cases plus 20 documents) passing. This run had substantial background
  build load; the torus exposes expensive OCCT volume/centroid integration over NURBS surfaces
  of revolution, confirmed by a local stack sample. No new latency claim is made.
  The ASan/UBSan reference selection passes 21/21 (391.07 s), with no diagnostics or retries.
  Nightfall code is instrumented; OCCT remains the pinned Release shared-library build.
  Remote CI confirmation, maintainer review and full Phase 2 sign-off remain pending.

- 2026-10-18 — #108 preserves circular rational-quadratic spans as analytic OCCT arcs.
  Recognition checks every homogeneous circle-polynomial coefficient at roundoff scale;
  uncertain curves and nearly orthonormal sketch placements retain their authored NURBS.
  Extrusions/revolutions now produce analytic cylinders, spheres and tori while retaining
  source-curve provenance. Mass integration keeps the same exact surfaces and 1e-9 requested
  relative accuracy, with a local bounding-box corner reference for each shell. First moments
  accumulate without intermediate normalization so cancellation of signed face volumes cannot
  erase a centroid contribution. The backend identity covers both changes. Ten new regressions
  cover scales/angles, reversed and split arcs, unequal weights, perturbed curves, partial
  revolutions, placed geometry, distant unequal solids and internal cavities. The existing
  triangular gusset caught and verifies the signed-moment cancellation case.
  Full Release `tools/check.sh --preset occt` passes 350/350 (20.69 s), including hygiene
  and both schema corpora (202 cases plus 20 documents). The exact CI ASan/UBSan geometry,
  evaluator and cache selection passes 124/124 (66.78 s), with no diagnostics or retries.
  Nightfall is instrumented; OCCT remains the pinned Release shared-library build.
  Remote CI, maintainer review and full Phase 2 sign-off remain pending.

- 2026-10-18 — #109 supports shared curved sketch boundaries through exact Bezier-span
  decomposition, overlap-endpoint splitting and signed traversal accumulation. Whole rational
  polynomial coefficients certify a common trace, including reversed/partial spans, degree
  elevation and Mobius parameter changes. The bound is local (1e-12 of control extent plus
  coordinate roundoff), independent of drawing-wide vertex welding. Stored curves keep their
  original knot parameters. Parent overlap certification prevents accidental merging of tiny
  pieces at near-tangencies; ordinary intersections retain the original curve domains, and
  control-hull bounds identify exact line contacts. Uncertifiable parameterizations still
  report #74. No schema, dependency or public signature change; evaluator identity is bumped.
  Eight math regressions include 48 fixed-seed ellipse/cap placements, duplicate open/closed
  splines, distinct near-curves, third-path crossings and internal/external tangencies.
  Three evaluator regressions verify duplicate circles/ellipses, a 60° shared-arc notch,
  empty-fill errors, dependent failure, parameter edits, cold equivalence, equivalent-unit
  cache restoration and retained names after eviction. Solid volumes/areas use 1e-5 absolute
  tolerances and centroids 1e-7 mm. The prior implementation failed six new math reproductions.
  Full Release `tools/check.sh --preset occt` passes 361/361 (19.54 s), including hygiene
  and the 202-case plus 20-document schema checks. Targeted ASan/UBSan passes 48 math cases
  (21,484 assertions across shared curves, arrangements, regions, intersections and refinement)
  plus all 127 kernel/evaluator/cache cases (32.68 s), with no diagnostics or retries.
  Nightfall code is instrumented; OCCT remains the pinned Release shared-library build.
  Remote CI, maintainer review and full Phase 2 sign-off remain pending.

- 2026-10-18 — #110 verifies horizontal/vertical lines and parallel/perpendicular line pairs
  within 1e-9 rad, equal line lengths or circle/arc radii within 1e-6 mm, and circle/arc radius
  dimensions after unit/expression evaluation. Reversed lines retain the same orientation
  constraints; equal arcs compare radii, not sweep lengths. Coordinates and exact profile
  geometry are preserved. Violations report the constraint's JSON Pointer, measured residual
  and tolerance; invalid dimensions and incompatible entities identify their specific fields.
  Constraint work is limited to 4096 relations per sketch; degenerate and non-finite derived
  measurements fail explicitly. Other constraint kinds remain unsupported; no solver, schema,
  public kernel interface or dependency change. Evaluator identity is bumped for the semantics.
  Ten new regressions failed before implementation and now pass (343 assertions), including
  48 fixed-seed rotated/translated rectangles, angular bounds across three scales, absolute
  dimensional bounds, partial arcs and equivalent units. Production solids verify a 6000 mm³
  constrained plate (1e-8 tolerance) and radius-edit cylinders (volume/area 1e-5, centroid
  1e-7 mm), failure propagation with independent branches, cache recovery, suppression,
  rollback and retained naming after cache eviction. Full Release `tools/check.sh --preset occt`
  passes 371/371 (19.94 s), including hygiene, 202 schema corpus cases and 20 reference documents.
  The exact CI ASan/UBSan kernel/evaluator/cache selection passes 137/137 (37.08 s), with no
  diagnostics or retries. Nightfall is instrumented; OCCT remains the pinned Release shared
  library. Rebuild evidence follows; remote CI/review and full phase sign-off remain pending.

- 2026-10-18 — #111 resolves schema-v1 point references safely: named points, line/arc
  endpoints and arc/circle/ellipse centers. Point-qualified operands for entire-line/radius
  constraints report a field error. A tests-first ASan/UBSan reproduction confirmed the old
  evaluator bound a string reference to null and aborted on a schema-valid object operand;
  its regression covers all six existing constraint kinds. Standalone points are retained
  separately from closed profile curves and included in cache memory estimates. Point-only
  sketches remain empty profiles for solid operations. Coincidence, point-point distance,
  midpoint (point/line in either order) and two-point symmetry about a third line verify
  evaluated coordinates within 1e-6 mm. Midpoint arithmetic avoids overflowing coordinate
  sums; non-finite derived results and unsupported point roles fail explicitly. No coordinates
  are solved or moved, and no schema, dependency or public kernel-interface change is made.
  Evaluator identity is bumped; the combined curve/point work limit stays 4096.
  Eleven new regressions cover mixed reference forms, unit conversion, negative arc sweeps,
  wrapped angles, tolerance bounds, 48 fixed-seed rigid placements at two scales, reversed
  axes, large coordinates, point retention and the work limit. Production solids verify
  width edits from 10 to 12 mm: volumes 6000/7200 mm³ and areas 2200/2400 mm² within 1e-8,
  plus centroids, dependency failures, equivalent-unit cache recovery and retained names.
  The combined constraint selection passes 21 cases / 610 assertions. Full Release
  `tools/check.sh --preset occt` passes 382/382 (19.47 s), including hygiene and the 202-case
  plus 20-reference-document schema checks. The exact CI ASan/UBSan kernel/evaluator/cache
  selection passes 148/148 (40.57 s), including the former null-reference reproduction,
  without diagnostics or retries. Nightfall is instrumented; OCCT uses the pinned Release
  shared libraries. Benchmark evidence follows; remote CI/review and phase sign-off remain open.

- 2026-10-18 — #112 verifies analytic tangent constraints between line segments and circular
  curves, or between two circles/arcs. Internal and external contact use a 1e-6 mm radial-gap
  bound; contact must lie on the finite segment (1e-6 mm) and each arc (1e-9 rad). Centers
  within linear tolerance, degenerate geometry, point-qualified references and unsupported
  curve kinds fail explicitly. Authored coordinates remain unchanged; no solver, schema,
  public kernel API or dependency is added. The evaluator cache identity is bumped.
  Ten tests-first regressions originally failed 123 assertions. They cover unit normalization,
  signed/wrapped/full-turn arcs, operand order, finite extents, tolerance boundaries, overflow,
  and 48 fixed-seed placements at two scales with reversed curves. Rounded extrusions check
  radius edits from 5 to 6 mm: volumes 6000+750π / 7200+1080π mm³ and areas
  1600+350π / 1680+432π mm² within 1e-5, plus centroids within 1e-7 mm. Invalid edits block
  both dependents without kernel work; restoring equivalent units reuses all three cached
  outputs. Cold comparison and retained names after cache eviction also pass.
  The combined constraint selection passes 31 cases / 1110 assertions. Full Release
  `tools/check.sh --preset occt` passes 392/392 (19.69 s), including hygiene, 202 schema corpus
  cases and 20 reference documents. The exact CI ASan/UBSan kernel/evaluator/cache selection
  passes 158/158 (38.50 s), with no diagnostics. Nightfall is instrumented; OCCT remains the
  pinned Release shared library. Benchmark evidence follows; remote CI/review and full
  phase sign-off remain pending.

- 2026-10-18 — #113 adds angle dimensions and fixed references, the two remaining v1
  constraint kinds. Angle dimensions use the smaller angle between start-to-end directions
  of two nonzero lines, in [0, π], within 1e-9 rad. Swapping operands or reversing both lines
  preserves the angle; reversing one takes its supplement. Fixed whole entities and supported
  point references preserve their evaluated authored coordinates, while all entity and other
  constraint checks still run. They do not freeze parameter edits or compare against a prior
  document. No solver, schema, public kernel API or dependency is added; the cache identity
  is bumped.
  Nine tests-first regressions originally failed 90 assertions; all nine now pass 725 assertions.
  Coverage includes acute/obtuse/0°/180° angles, mm/cm and deg/rad, dimensional expressions,
  invalid references and dimensions, finite arithmetic, tolerance bounds at three scales,
  48 fixed-seed rigid placements, every supported fixed entity and point role, and unchanged
  exact curves. A slanted extrusion with angle θ = 45°/60°/120° checks volume 6000 mm³,
  surface area `1000 + 1200/sin(θ)` mm² and centroid (3,0,15) mm within 1e-7. Constraint-only
  edits fail both dependents without kernel work; equivalent-unit restoration hits all three
  cached outputs. Subsequent angle edits update even fixed coordinates; cold comparisons
  and retained naming after cache eviction pass.
  The new solid regression exposed a mass-integration performance problem: one targeted
  Release run took 32.610 s in that case, while all eight other new cases took 0.000–0.003 s.
  A process sample locates the work in the existing OCCT `BRepGProp_VinertGK::Perform` /
  nested Kronrod integration during snapshot measurement. This remains a separate kernel
  performance follow-up; the correctness regression is retained. The fix plan is to isolate
  an unconstrained planar-solid reproduction, investigate integration/reference conditioning
  without loosening mass tolerances, and rerun the slanted-profile and existing cavity,
  multi-solid and curved-surface checks.
  Full Release `tools/check.sh --preset occt` passes 401/401 (54.40 s), including hygiene,
  202 schema corpus cases and 20 reference documents. The exact CI ASan/UBSan
  kernel/evaluator/cache selection passes 167/167 (86.63 s), with no diagnostics. Nightfall
  is instrumented; OCCT uses the pinned Release shared libraries. The solid regression
  takes 33.27 s / 46.11 s in these Release / sanitizer runs. The 30-feature benchmark
  follows; remote CI/review and full Phase 2 sign-off remain pending.

- 2026-10-18 — #114 resolves the slanted-solid mass-integration bottleneck from #113.
  Planar faces qualify for polynomial Gaussian quadrature only when every pcurve is an
  analytic line with finite parameter bounds. These affine parameterizations make the
  volume/first-moment integrands polynomial; OCCT's fixed Gaussian rule integrates them
  exactly up to floating-point error. Other faces retain exact-surface adaptive
  Gauss–Kronrod integration at 1e-9. The per-shell corner reference, signed cavity terms
  and unnormalized first-moment accumulation are preserved. The backend cache identity
  includes the new integration policy. No dependency, public API or format changes.
  Five new regressions cover an unconstrained production extrusion, 16 fixed-seed
  rotated/translated/scaled prisms, a concave profile with a straight hole, a slanted cavity
  with a distant unequal solid, and curved trim boundaries. The tests-first unconstrained
  extrusion failed only its 2 s runtime bound: 20.063 s, with all 24 geometry/naming
  assertions passing. The same case now takes about 0.001 s; volume 6000 mm³ and area
  3400 mm² remain within 1e-7. The existing angle-driven solid regression takes 0.008 s
  in the targeted run, compared with 33.27 s in #113's Release gate. The focused planar,
  mass-reference and analytic-circle selection passes 15 cases / 864 assertions; the
  angle/fixed selection passes 9 cases / 725 assertions.
  Full Release `tools/check.sh --preset occt` passes 406/406 (21.38 s), including hygiene,
  202 schema corpus cases and 20 reference documents. The exact CI ASan/UBSan
  kernel/evaluator/cache selection passes 172/172 (42.61 s), with no diagnostics.
  Nightfall is instrumented; OCCT remains the pinned Release shared library. The unchanged
  rebuild benchmark below restores the local latency gate for the slanted profile.
  Remote CI/review and full Phase 2 sign-off remain pending.

- 2026-10-18 — #115 resolves schema-v1 sketch-line axes for revolutions and rigid rotations.
  Evaluated sketches retain named, authored line endpoints separately from profile traversal;
  axes map through datum or explicitly placed frames. Direction comes from the local delta,
  avoiding cancellation when subtracting endpoints after large world placement. Axis-sketch
  hashes join the ordered feature dependencies; failed/suppressed sketches invalidate affected
  bodies before backend work. Cache estimates include line metadata and evaluator identity is
  version 13. The schema, public kernel API and dependencies are unchanged (RFCs 0005/0007).
  Selector axes and open/construction curve chains remain unsupported; named lines in existing
  closed sketches are supported. Revolutions retain the backend's planar-axis/one-sided-hull
  restrictions. Eight tests-first regressions all failed on the previous unsupported axes;
  the final focused suite passes 11 cases / 613 assertions. Coverage includes reversed path
  traversal, signed partial turns, all datum frames and explicit placement, independent axis
  edits, equivalent units, producer renaming, suppression/failure/rollback, out-of-plane axes,
  overflow and large placement, typed references, eviction and kernel-facade teardown.
  Analytic volume/area agree within 1e-7 and centroid components within 1e-8 mm. Full Release
  `tools/check.sh --preset occt` passes 417/417 (23.30 s), including hygiene, 202 schema corpus
  cases and 20 reference documents. The CI ASan/UBSan kernel/evaluator/cache selection passes
  183/183 (45.35 s), with no diagnostics. Nightfall is instrumented; OCCT remains the pinned
  Release shared library. Rebuild evidence follows; remote CI/review and full Phase 2 sign-off
  remain pending.

- 2026-10-18 — #116 allows standalone centerlines and open line/arc/spline reference sketches.
  Entity/constraint validation stays in sketch preparation; exact unjoined curves are retained
  separately from already-closed primitives. Filled-profile consumers assemble closed,
  unambiguous chains without mutating the cached sketch. Open/ambiguous chains fail at that
  consumer's `/inputs/profile`, including mixed closed/open sketches with region picks.
  The sketch remains available as an axis even when a profile consumer fails. Axis direction,
  parametric dependencies, suppression/rollback, body invalidation and naming retain their
  existing behavior. Cache estimates now include raw segments; evaluator identity is version 14.
  No schema, persisted format, public kernel API or dependency change (RFCs 0005/0007).
  All eight tests-first regressions failed against the prior sketch-level closure check. The
  final open-sketch suite passes 8 cases / 296 assertions: independent axis edits and equivalent
  units, directed rotations, sleeve-to-cylinder revolution, profile failure isolation, branched
  reference networks, entity/constraint errors, exact arc/rational-spline retention, rollback,
  failure/suppression recovery, byte-budget eviction and retained solid readers after teardown.
  Analytic volume/area agree within 1e-7 and centroid components within 1e-8 mm. Existing curve
  and constraint tests compare the retained unjoined segments; their solid golden checks remain.
  Full Release `tools/check.sh --preset occt` passes 425/425 (22.47 s), including hygiene,
  202 schema corpus cases and 20 reference documents. CI's ASan/UBSan kernel/evaluator/cache
  selection passes 191/191 (47.40 s), with no diagnostics. Nightfall is instrumented; OCCT remains
  the pinned Release shared library. README documents the reference/profile distinction and
  sketch-line axes. Selector axes, open-wire solids and construction flags remain unsupported.
  Rebuild evidence follows; remote CI/review and full Phase 2 sign-off remain pending.

- 2026-10-18 — #117 implements schema-v1 linear body patterns using existing rigid transforms
  and solid union. Count includes each seed; copies translate from that original current body
  by integer multiples of signed spacing along a normalized direction. Touching/overlapping
  copies fuse, separated copies remain valid solid components in the same body, and count 1
  preserves geometry through an identity transform. Unique body targets and finite offsets
  are checked before copying; work is bounded to 64 total instances across all seeds. Larger
  counts require spacing magnitude above 1e-6 mm. Feature-target/circular patterns remain
  unsupported. No schema, persisted format, public kernel API or dependency change
  (RFCs 0005/0007).
  Intermediate copies/history remain private and are charged to cache estimates. Copied faces
  retain inherited producer names and gain the pattern's local `instance` role. Both histories
  rebind names to current IDs/snapshots on cache reuse; all target bodies publish atomically
  after geometry and naming succeed. Evaluator/naming identities advance to 15/4.
  All 13 tests-first cases failed on the prior unsupported-pattern path. The final suite passes
  15 cases / 1878 assertions, covering fused/separated arrays, signed/normalized directions,
  count/spacing errors, exact and aggregate work bounds, current-body dependencies, equivalent
  units, producer renaming, binding misses, suppression/rollback, unavailable seeds, incomplete
  histories, injected copy/union failures, eviction and kernel-facade teardown. Annular seeds
  preserve periodic faces and through-holes: three instances have volume 1890π mm³, area
  1386π mm² and 12 faces. All volume/area checks use 1e-7; centroids use 1e-8 mm.
  Full Release `tools/check.sh --preset occt` passes 440/440 (24.87 s), including hygiene,
  202 schema corpus cases and 20 reference documents. CI's ASan/UBSan kernel/evaluator/cache
  selection passes 206/206 (49.85 s), with no diagnostics. Nightfall is instrumented; OCCT
  remains the pinned Release shared library. README and adapter/evaluator contracts document
  pattern behavior and limits. Rebuild evidence follows; remote CI/review and full Phase 2
  sign-off remain pending.
- 2026-10-18 — #118 implements schema-v1 circular body patterns with existing rigid rotations
  and solid union (RFCs 0005/0007). Count includes the current seed. Signed partial sweeps
  include both endpoints; sweeps within 1e-9 rad of a full turn distribute unique instances.
  Count >1 requires angular steps above 1e-9 rad in magnitude; count 1 allows zero sweep.
  Explicit and evaluated sketch-line axes support world placement and authored direction.
  Independent axis edits invalidate dependent arrays. Body-target admission, the limit of 64
  total instances and copy/union execution are shared with linear patterns. Original current
  seeds may contain multiple solids or earlier patterns; copied faces retain inherited names
  and the new pattern's local `instance` role. Atomic publication, private history estimates,
  cache rebinding and ownership are preserved. Evaluator identity advances to 16; naming
  remains 4. Feature-target patterns and selector axes remain unsupported. No schema, persisted
  format, public kernel API or dependency change.
  All 16 tests-first cases fail against the former unsupported-feature path. A separate
  boundary regression catches an initial subtraction-rounding error at full-turn tolerance
  (three instances instead of four); interval comparisons canonicalize both signs correctly.
  The final 19 new circular cases and 15 existing linear cases pass together: 34 cases /
  5450 assertions, seed 20261018. Coverage includes full/partial/signed sweeps, exact angular
  bounds, count/angle units, explicit/placed/reversed open-sketch axes, independent edits,
  touching/coincident/separated copies, through-holes, nested patterns, exact/aggregate work
  admission, producer renaming, binding misses, suppression/rollback, failed seeds/axes,
  incomplete histories, injected copy/union failures, eviction and kernel-facade teardown.
  Four separated 2×2×30 mm boxes have volume 480 mm³, area 992 mm² and 24 faces. Four
  touching boxes fuse to volume 480 mm³ / area 512 mm² / six faces; coincident boxes keep
  seed volume 120 mm³ / area 248 mm². Four annuli retain holes and periodic faces: volume
  360π mm³ / area 744π mm² / 16 faces. Volume/area tolerances are 1e-7; centroids 1e-8 mm.
  Full Release `tools/check.sh --preset occt` passes 459/459 (25.03 s), including hygiene,
  format, 202 schema corpus cases and 20 reference documents. CI's ASan/UBSan
  kernel/evaluator/cache selection passes 225/225 (52.24 s), with no diagnostics. Nightfall
  is instrumented; OCCT remains the pinned Release shared library. README and private
  adapter/evaluator headers document semantics and limits. Rebuild evidence follows; remote
  CI/review and full Phase 2 sign-off remain pending.
- 2026-10-18 — #119 adds exact body mirrors under RFCs 0005/0007. Typed `IKernel::mirror`
  returns a reflected copy across a finite orthonormal plane; its capability and backend
  identity are explicit. OCCT duplicates geometry, checks finite transformation coefficients
  and fully validates reflected solids. The direct-isometry shortcut is not used. Every input
  face/edge maps to the actual output entity/orientation; source B-reps remain unchanged.
  The evaluator unions each reflection with its original current seed, sharing private copy
  histories, local `instance` roles, cache estimates and atomic publication with patterns.
  Datum and normalized explicit planes share sketch validation; parameter edits invalidate
  dependent outputs. The 64-total-instance bound admits 32 mirror targets. Feature targets
  and selector planes remain unsupported. Evaluator identity advances to 17; naming stays 4.
  No schema, persisted-format or dependency change; the typed kernel extension follows
  RFC 0007's provision for remaining v1 operations. Backend implementers must add `mirror`
  and initialize its capability flag; the commit/PR mark this source/ABI change with `!`.
  Tests first: the new API fails compilation before declaration; five geometry cases fail
  against a temporary unsupported kernel call and all 13 initial evaluator cases fail on the
  unsupported-feature path. Final kernel selection passes 6 cases / 7679 assertions: 32
  fixed-seed arbitrary planes, double reflection, a chiral four-component arrangement,
  analytic periodic surfaces, holes, enclosed cavities, complete oriented history, source
  serialization equality, invalid/foreign inputs, overflow and facade teardown. A 2×3×5 mm
  box retains volume 30 mm³ / area 62 mm²; the cavity retains 114 mm³ / 170 mm².
  Final mirrors/patterns selection passes 49 cases / 6973 assertions, including 15 new mirror
  cases. Two separated 2×2×30 mm boxes have volume 240 mm³ / area 496 mm² / 12 faces;
  touching copies fuse to 240 mm³ / 376 mm² / six faces, and coincident copies retain
  120 mm³ / 248 mm². Two annuli preserve through-holes and periodic faces at 180π mm³ /
  372π mm² / eight faces. Volume/area tolerances are 1e-7; centroid components 1e-8 mm.
  Coverage includes plane/depth/unit edits, all datums, tilted planes, overlap, nested seeds,
  exact/aggregate work bounds, cache reuse/rebinding/eviction, suppression/rollback, coordinate
  overflow, injected reflection/union failures, incomplete histories and retained readers.
  Full Release `tools/check.sh --preset occt` passes 480/480 (27.48 s), including hygiene,
  format, 202 schema corpus cases and 20 reference documents. CI's ASan/UBSan
  kernel/evaluator/cache selection passes 246/246 (59.65 s), with no diagnostics. Nightfall
  is instrumented; OCCT remains the pinned Release shared library. API, adapter/evaluator
  headers and README document behavior and limits. Rebuild evidence follows; remote CI/review
  and full Phase 2 sign-off remain pending.
- 2026-10-18 — #120 attaches sketches to generated analytic planar faces under RFCs
  0005/0007. A sketch plane supports `created_by` and optional local `role`, requiring one
  current face even with `expect: any/some`. Missing/multiple matches report `selector_count`;
  filters, entity qualifiers, selector sets and selector planes outside sketches remain
  unsupported. Typed `IKernel::face_plane` returns the located parameter origin/U axis and
  outward normal, restoring a right-handed frame for reversed/reflected faces. It validates
  backend/snapshot membership and rejects curved/non-face handles without changing geometry.
  Body candidates follow naming history through modifiers and boolean transfers, including
  successive tool retirements and retained copies. Every candidate binds before a result is
  selected; a role-absent body's edit still changes the dependency hash. Unavailable current
  bodies fail explicitly. Sketch cache entries retain source snapshot bindings, charge their
  geometry estimates and conservatively rebuild after recreation. The face query runs before
  cache reuse and is included in kernel-call counts.
  A tests-first ID-swap regression catches stale sketch reuse on the exact same body snapshot:
  swapping two producer IDs must move the boss from (5,10,31) to (25,10,41) mm. Depending on
  both producer feature and current body hashes fixes this case, including downstream rebuilds.
  Evaluator identity advances to 18, naming stays 4; backend identity adds the oriented face
  query convention. Backends must implement the new typed query; commit/PR mark this source/
  ABI change with `!`. No schema, persisted-format or dependency change.
  Tests first: kernel tests fail compilation before the API, all five kernel cases fail on
  a temporary unsupported query, and all fourteen initial evaluator cases fail on selector
  planes. Final targeted tests pass 5 kernel cases / 59 assertions and 16 evaluator cases /
  727 assertions (seed 20261018). Coverage includes signed caps, rigid/reflected placement,
  cutting-face orientation, source serialization equality, invalid/foreign/curved handles,
  parameter/unit edits, merged/separated names, missing/ambiguous faces, consumed/retained
  tools, successive transfers, deleted roles, all candidate dependencies, suppression/
  rollback/failure, query failure/retry, ID edits, binding misses, byte budgets and teardown.
  A radius-2 mm, height-2 mm attached boss has volume 8π mm³ / area 16π mm², with centroid
  z = D + 1 mm on the end cap and z = -1 mm on the outward start cap. The cutting-tool
  attachment follows the reversed face at z = 5 mm into the cavity. Volume/area tolerances
  are 1e-7; centroid components 1e-8 mm; queried normal/axis checks 1e-10.
  Full Release `tools/check.sh --preset occt` passes 501/501 (29.50 s), including hygiene,
  format, 202 schema corpus cases and 20 reference documents. CI's ASan/UBSan
  kernel/evaluator/cache selection passes 267/267 (64.39 s), with no diagnostics. Nightfall
  is instrumented; OCCT remains the pinned Release shared library. API/adapter/evaluator
  contracts and README document supported selectors, limits and migration. Rebuild evidence
  follows; remote CI/review and full Phase 2 sign-off remain pending.

- 2026-10-18 — #121 extends sketch-plane selectors under RFCs 0005/0007 with `faces_of`
  and planar `where.type`, signed `normal`, `parallel_to` and `perpendicular_to` on both
  body and generated-face selectors. Parallel/perpendicular describe the plane relative to
  the direction; conditions combine. Scalar expression directions normalize above length
  1e-6; angular matching uses sine tolerance 1e-9. Curved faces do not match planar filters.
  Empty/ambiguous attachment sets remain `selector_count`, even with `expect: any/some`.
  Typed `IKernel::faces` returns all unique snapshot-owning face handles, including curved
  surfaces and separated solids, with optional located outward planar frames. Enumeration
  preserves geometry, rejects null/foreign snapshots and runs before cache reuse, counted
  as one backend query per queried body. Names are checked against current query membership.
  Producer feature hashes and every candidate body's hash/binding remain dependencies,
  including bodies with no surviving selected name. Retired body IDs fail explicitly.
  Evaluator identity advances to 19, naming stays 4; backend identity records enumeration.
  All backends must implement `faces(const ShapePtr&)`; the commit/PR mark this source/ABI
  change with `!`. No schema, persisted-format or dependency change. Other surface types,
  nearest/radius/convexity filters, entity qualifiers, selector sets and selector planes
  outside sketches remain unsupported.
  Tests first: new kernel tests fail compilation before the API; five query cases fail
  against a temporary unsupported query (the invalid-input case already rejects it).
  All fifteen initial evaluator cases fail on unsupported selectors/filters. Final targeted
  runs pass 11 kernel cases / 189 assertions and 32 evaluator cases / 1599 assertions,
  seed 20261018. Coverage includes all/unique planar and curved faces, signed and tilted/
  reflected frames, reversed cutting-tool faces, unchanged source serialization, malformed
  query handles and missing provenance, independent source edits, empty/ambiguous sets,
  suppression/rollback/failure, query failure/retry, producer ID swaps on identical body
  snapshots, conservative rebinding, retained-byte budgets, eviction and facade teardown.
  A radius-2 mm, height-2 mm attached boss retains volume 8π mm³ / area 16π mm² at
  (5,10,D+1) mm on the top cap, (5,-10,-1) mm on the outward start cap and (11,5,10) mm on
  the +x side. Volume/area tolerance is 1e-7; centroid components 1e-8 mm; frame checks 1e-10.
  Full Release `tools/check.sh --preset occt` passes 523/523 (33.06 s), including hygiene,
  format, 202 schema corpus cases and 20 reference documents. CI's ASan/UBSan kernel/
  evaluator/cache selection passes 289/289 (100.24 s), with no diagnostics. Nightfall is
  instrumented; OCCT remains the pinned Release shared library. Public headers and README
  document supported selectors, direction semantics, limits and backend API migration.
  Rebuild measurements follow; remote CI/review and full Phase 2 sign-off remain pending.

- 2026-10-18 — #122 implements sketch-plane `where.near` under RFCs 0005/0007. The world
  point accepts evaluated length expressions. After other supported filters, it queries
  every remaining trimmed face, including boundaries/holes, across all current candidate
  bodies. Faces within 1e-6 mm of the global minimum all survive; ambiguity remains
  `selector_count`, including with `expect: any`. Near alone does not imply planar type:
  a uniquely nearest curved face fails the sketch-plane consumer explicitly.
  Typed `IKernel::face_distance` computes finite nonnegative point-to-face distance from
  exact B-rep geometry. OCCT uses serial extrema with Precision::Confusion() (1e-7 mm);
  snapshot membership checks preserve owner placement and source geometry. Invalid handles,
  nonfinite points and unrepresentable results fail. The service rejects failed/nonfinite/
  negative distances at `/inputs/plane/where/near` before any cached sketch/downstream reuse.
  Enumeration and each distance execute/count before cache lookup. All candidate bodies
  and producer features remain dependencies even when not nearest; source bindings and
  retained-byte accounting remain conservative. Evaluator identity advances to 20, naming
  stays 4; backend identity records trimmed-face distance. Backend implementers must add
  `face_distance(const EntityPtr&, math::Vec3)`; commit/PR mark this source/ABI change
  with `!`. No schema, persisted-format or dependency change.
  Tests first: kernel tests fail compilation before the API, six of eight fail against
  the temporary unsupported query (invalid-input cases already reject it), and all twelve
  initial evaluator cases fail on unsupported `near`. Final targeted runs pass 19 kernel
  cases / 234 assertions and 44 evaluator cases / 2346 assertions, seed 20261018.
  Coverage includes interior/edge/corner/hole/curved-trim distances, source serialization
  equality, current rigid/reflected/reversed faces, global retained-tool candidates, source/
  point/unit edits, repeat spacing, exact/near ties, unavailable sources, query errors and
  malformed results, cache hits/rebinding/budgets/eviction, producer-ID swaps and teardown.
  A three-copy 10×20×30 mm array measures 18000 mm³ / 6600 mm² and selects the expected
  separated cap. A ring with a plug measures 363π mm³ / 392π mm²: the plug cap 2 mm away
  wins over a ring cap sqrt(5) mm away through its hole. Radius-2 mm, height-2 mm bosses
  retain 8π mm³ / 16π mm². Volume/area and distance tolerances are 1e-7; centroid 1e-8 mm.
  Full Release `tools/check.sh --preset occt` passes 543/543 (32.76 s), including hygiene,
  format, 202 schema corpus cases and 20 reference documents. CI's ASan/UBSan kernel/
  evaluator/cache selection passes 309/309 (70.33 s), with no diagnostics. Nightfall is
  instrumented; OCCT remains the pinned Release shared library. Public headers and README
  document nearest-point semantics, tolerance ties, limits and backend API migration.
  Rebuild measurements follow; remote CI/review and full Phase 2 sign-off remain pending.

- 2026-10-18 — #123 composes sketch-plane face selectors with recursive `any` and
  `except` under RFCs 0005/0007. Unions deduplicate by snapshot/entity identity.
  Exclusions run before this node's geometric/nearest filters, then each node validates
  `expect`: exactly one, some (default, at least one), or any (permits empty). The final
  sketch consumer still requires one analytic planar face. Optional empty branches need
  `expect: any`; unavailable references remain errors. Nested failures retain the complete
  JSON Pointer, including child/filter locations.
  Every referenced producer and candidate body remains a dependency, even for excluded or
  empty sets. Source bindings and retained-byte accounting remain conservative. Current
  face descriptions are shared within one feature evaluation, across children, exclusions
  and parent filters; actual queries still execute before cache reuse. Descriptions validate
  all returned snapshot handles and current name membership. The evaluator identity advances
  to 21; naming stays 4. No backend API, schema, persisted-format or dependency change.
  Entity qualifiers, edge selectors and selector planes outside sketches remain unsupported.
  Tests first: all thirteen initial composition cases fail on unsupported `any`/`except`.
  Final targeted attachment/filter/nearest/composition selection passes 58 cases / 3218
  assertions, seed 20261018, including fourteen new cases. Coverage includes recursive
  union/difference, duplicate/overlapping names, optional empty sets, per-node counts,
  exclusions before nearest, global candidates, coincident geometry in distinct snapshots,
  nested unsupported fields/units/query failures, empty/excluded dependency edits and
  unavailable sources. A 24-level composition remains inside document nesting bounds.
  Regression sections also verify consumed cutting-tool orientation, producer-ID swaps on
  identical body snapshots, conservative rebinding, byte budgets, eviction and teardown.
  Radius-2 mm, height-2 mm bosses retain 8π mm³ / 16π mm². Volume/area tolerance is 1e-7;
  centroid components 1e-8 mm. Excluding the closest of two separated caps selects the
  remaining cap without an index fallback or an empty nearest result.
  Full Release `tools/check.sh --preset occt` passes 557/557 (32.81 s), including hygiene,
  format, 202 schema corpus cases and 20 reference documents. CI's ASan/UBSan kernel/
  evaluator/cache selection passes 323/323 (73.64 s), with no diagnostics. Nightfall is
  instrumented; OCCT remains the pinned Release shared library. Public evaluator contracts
  and README document composition, filter/cardinality order and remaining limits.
  Rebuild measurements follow; remote CI/review and full Phase 2 sign-off remain pending.

- 2026-10-18 — #124 extends body mirrors to selected planar faces under RFCs 0005/0007.
  Sketches and mirrors share generated-role/body-face resolution, planar direction filters,
  trimmed-face nearest queries and recursive `any`/`except` composition. The final plane
  requires exactly one analytic planar face, including with `expect: any/some`; ambiguity,
  empty sets, curved faces and unsupported fields retain structured JSON Pointer errors.
  Every referenced producer/current body remains a dependency and is queried before cache
  reuse. Targets bind first; selector-only bindings retain source snapshots for compatibility
  and conservative byte accounting without adding reflection targets. Reference bodies remain
  unchanged unless explicitly targeted. Pattern publication remains atomic across targets.
  Evaluator identity advances to 22, naming stays 4. No backend API, schema, persisted-format
  or dependency change; feature-target mirrors and selector axes remain unsupported.
  Tests first: all twelve initial mirror-selector cases fail on unsupported selector planes.
  The final targeted mirror/attachment/filter/nearest/composition run passes 87 cases /
  6184 assertions, seed 20261018, including fourteen new cases. Coverage includes shared
  selector composition, source/target/unit edits, current translated/tilted faces, self-plane
  fusion, consumed producer faces and retired body references, empty/ambiguous/tied/curved
  selections, nested errors, suppression/rollback, query failures before cache reuse,
  multi-target atomic failure/recovery, producer-ID swaps on identical source snapshots,
  conservative binding misses, retained-source admission budgets, eviction and teardown.
  A 2×2×30 mm seed mirrored across a reference cap at z = 40 mm retains two solids,
  240 mm³ / 496 mm² and centroid (13,0,40) mm after a 3 mm x translation; the reference
  stays 160 mm³ / 328 mm². Mirroring across the seed's own top cap fuses to one solid,
  240 mm³ / 488 mm². Volume/area tolerance is 1e-7; centroid components 1e-8 mm.
  Full Release `tools/check.sh --preset occt` passes 571/571 (34.78 s), including hygiene,
  format, 202 schema corpus cases and 20 reference documents. CI's ASan/UBSan kernel/
  evaluator/cache selection passes 337/337 (77.05 s), with no diagnostics. Nightfall is
  instrumented; OCCT remains the pinned Release shared library. Public evaluator contracts,
  the input adapter and README document selected mirror planes and retained sources.
  Rebuild measurements follow; remote CI/review and full Phase 2 sign-off remain pending.

- 2026-10-18 — #94 implements `nf build part.nightfall --out part.step` under accepted
  RFCs 0005/0007, using RFC 0001's pinned OCCT translator. The headless executable links
  the production evaluator; the app still uses the service boundary. It reads regular files
  within the existing 16 MiB document limit, evaluates the complete active history and
  refuses partial exports after any model error. Every live body is exported in stable ID
  order, including coincident bodies; consumed tools are excluded. Suppression and inclusive
  rollback keep their evaluator semantics. Success emits one JSON record with output path
  and body IDs; errors retain codes, pointers and available offsets. Exit statuses are
  0 (success), 1 (build/I/O failure) and 2 (usage).
  Only the CLI opens files. `IKernel::export_step` encodes exact manifold STEP AP242 B-reps
  in millimetres from deep geometry copies, with no tessellation. A per-writer messenger
  avoids changing global output routing. Each input body remains distinct even when its
  snapshot is shared with another body. The CLI fsyncs/closes a sibling temporary file before
  rename, cleans it on write/publication errors, and refuses output aliases of the input.
  Existing artifacts survive failed builds. STEP carries geometry, not editable IR history.
  The new pure virtual method changes the backend source/ABI contract: implement
  `export_step(const std::vector<ShapePtr>&)` and rebuild consumers. Backend identity records
  `step=ap242-mm-v1`; evaluator/naming stay 22/4. No schema, IR version or dependency change.
  Tests first: kernel tests fail compilation on the missing method, then five of six cases
  fail against an unsupported stub (invalid-input rejection already passes). All ten initial
  CLI cases fail before the executable exists. Final targeted runs pass 6 kernel cases /
  144 assertions and 10 CLI cases / 1037 assertions, seed 20261018. Coverage includes boxes,
  annular cavities, analytic cylinders, current rigid/reflected/multi-solid snapshots,
  coincident bodies, unchanged B-rep serialization/face handles and retained snapshots;
  CLI diagnostics, file bounds, aliases, failed/empty models, replacement/cleanup,
  consumed tools, suppression/rollback and shell-sensitive filenames. A 3×4×5 mm box
  reloads as 60 mm³ / 94 mm² with centroid (3.5,1,9.5) mm, six faces and twelve edges;
  two coincident boxes remain two solids, 120 mm³ / 188 mm². Kernel STEP tolerances are
  1e-6 for volume/area and 1e-7 mm for centroid components.
  All twenty reference documents build through the CLI and reload as valid closed solids;
  comparisons to their independently tested evaluator results use volume/area tolerance
  1e-5 and centroid 1e-6 mm. The CLI also configures/builds with `NF_BUILD_TESTS=OFF` and
  exports a washer successfully. The OCCT workflow includes CLI path triggers, targets and
  sanitizer tests. Final Release `tools/check.sh --preset occt` passes 587/587 (39.88 s),
  including hygiene, format, 202 schema corpus cases and 20 reference documents, without
  warnings. The expanded CI-equivalent ASan/UBSan kernel/cache/evaluator/CLI selection
  passes 353/353 (88.15 s), with no diagnostics. Nightfall is instrumented; OCCT remains
  the pinned Release shared library. Rebuild measurements follow; remote CI/review and
  full Phase 2 sign-off remain pending.

- 2026-10-18 — #125 adds repeatable `--set name=expression` options to `nf build` under
  RFCs 0005/0007. `--out` and overrides may appear in either flag order after the input.
  Overrides replace existing parameter definitions in an in-memory copy of a valid source;
  the complete batch is applied before revalidating the final dependency graph and bounds.
  Expressions use existing IR semantics: bare values are dimensionless, dimensional values
  need units, references see the final batch, and self-references are cycles. Unknown names
  fail at `/params`; expression/unit/bounds/cycle failures retain their field pointers and
  expression offsets. Malformed names/assignments, duplicate overrides/outputs and unknown
  flags return usage status 2 before file access; invalid parameter values return status 1.
  Builds without overrides retain the original source-loading path. Source bytes, exact
  STEP body semantics, alias protection and failure-safe output replacement remain intact.
  No IR schema, persisted-format, backend/evaluator API, identity or dependency change.
  Tests first: nine of ten new cases fail before implementation; malformed/duplicate usage
  rejection already passes. The final targeted CLI run passes 20 cases / 2308 assertions,
  seed 20261018, including ten new cases. A two-override batch that would create a cycle
  after only the first edit builds correctly in either flag order once both definitions
  are replaced. Coverage also includes equivalent units, dimensionless pattern counts,
  inclusive bounds, domain/unit/parse/unknown/cycle errors, original-value recovery,
  source/output preservation, valid identifier limits, valid-source requirements and aliases.
  A 10×20 mm plate at 45 mm depth exports 9000 mm³ / 3100 mm² with centroid (8,10,22.5) mm.
  The dependent batch exports 8000 mm³ / 2800 mm² with centroid (9,10,20) mm. A three-copy
  scalar-count variant retains three solids, 18000 mm³ / 6600 mm². STEP comparisons use
  volume/area tolerance 1e-5 and centroid components 1e-6 mm. Every reference part also
  exports a parameter edit with matching reloaded geometry and byte-identical source;
  edited values have independent analytic coverage in the existing reference suite.
  The `NF_BUILD_TESTS=OFF` CLI rebuilds and exports a 6 mm washer variant successfully.
  Release `tools/check.sh --preset occt` passes 597/597 (48.50 s), including hygiene,
  format, 202 schema corpus cases and 20 reference documents, with no warnings.
  The CI-equivalent ASan/UBSan kernel/cache/evaluator/CLI selection passes 363/363
  (99.07 s), with no diagnostics. Nightfall is instrumented; OCCT remains the pinned
  Release shared library. Command/rebuild measurements follow; remote CI/review and full
  Phase 2 sign-off remain pending.

- 2026-10-18 — #126 implements the existing `created_by` / `from: sketch/entity`
  qualifier for sketch and mirror planes under RFCs 0004/0005/0007. Arrangement half-edges
  retain exact input path/curve lineage through decomposition, splits, certified shared-span
  merges and reversed boundaries. Region picks preserve that lineage while reindexing output
  edges. Service profile preparation maps it to local authored entity IDs; kernel-generated
  profile-edge roles bind those IDs to the current producer/sketch during publication.
  Modified/split/merged faces retain qualifiers through transforms, booleans, patterns and
  mirrors. Multiple names on one face deduplicate by snapshot/entity when selectors union.
  Caps, named points and synthetic connectors invent no authored curve source; primitive
  entities can yield multiple side faces and still obey exact cardinality. A qualifier from
  an unrelated coincident sketch matches nothing. Referenced sketches must be available,
  including in empty/excluded branches, and contribute dependency hashes before reuse.
  Source bindings remain separate from operation targets. Cache entries retain local IDs,
  never an authored producer/sketch prefix; retained metadata is included in byte estimates.
  Invalid backend profile-edge provenance fails publication atomically. Existing directly
  constructed sketches may omit metadata and retain unqualified geometry behavior.
  Tests first: all eleven initial evaluator cases fail on the unsupported qualifier; the
  four math cases initially cannot compile without the new source metadata. The first
  full gate exposes a direct-sketch span-limit regression (omitted metadata); normalizing
  optional metadata and explicit closed/segment regressions fix it. Expanded coverage also
  exercises signed normals, nested unions/exclusions, consumed tools, reversed chains and
  holes, removed seams, coincident primitives, revolved annuli, producer/sketch renames,
  cache eviction, suppression, foreign sketch identities and malformed backend provenance.
  Rebuild C++ clients: transient `math::Arrangement::HalfEdge` / `math::Loop` layouts and
  region values passed to `IKernel` gain metadata. Direct loops may leave sources empty.
  No persisted schema, virtual kernel operation or dependency change; evaluator identity
  advances to `evaluator/23;roles/5`. Local verification and timings follow; remote CI,
  maintainer review and full Phase 2 sign-off remain pending.

- 2026-10-18 — #127 resolves existing selector axes from one current analytic cylindrical
  face for rigid transforms, circular patterns and revolutions (RFCs 0005/0007). Immutable
  face descriptions gain exact radius and a located, directed parameter-Z axis; planar
  descriptions retain their outward-frame convention. Face orientation reversals for bores
  do not reverse parameter axes. Fixtures explicitly cover stored -Z prism axes, rigidly
  placed/reflected axes, reversed bore orientation, unchanged source B-reps and handle life.
  Cylindrical `type` and positive `radius` filters compose with provenance, recursive
  unions/exclusions and trimmed nearest distances; radius matches within 1e-6 mm and alone
  implies a cylinder. Direction filters still imply a plane; contradictory filters match
  nothing. Coaxial patches remain distinct faces and cannot bypass exact cardinality.
  The existing resolver binds every source dependency and validates cylinder metadata before
  cache reuse. Selector-only snapshots are retained/charged separately; transforms iterate
  only authored targets, and new revolutions publish their new body without treating a source
  binding as an operation target. All bodies still publish atomically. Consumed tools follow
  current survivor history; source-only edits and producer-ID rebinding invalidate/rebind
  correctly. Invalid/missing metadata, unavailable producers and query failures prevent reuse.
  Tests first: all seven initial evaluator cases fail on unsupported selector axes; three
  kernel cases initially cannot compile without cylinder metadata. Final focused runs pass
  18 evaluator cases / 898 assertions and 3 kernel cases / 112 assertions, seed 20261018.
  Coverage includes signed/partial sweeps, result joins with a shared axis source/target,
  single-instance validation, coplanarity, multiple-target failure/retry, units/tolerances,
  malformed metadata on cache hits, suppression, independent branches and cache budgets.
  C++ clients rebuild for the `FaceInfo` layout extension; backends populate exact analytic
  cylinder descriptions. No virtual operation, persisted schema or dependency changes.
  Backend identity becomes `faces=analytic-v2;cylinder-axis=parameter-z-v1`; evaluator
  identity advances to `evaluator/24;roles/5`. Verification and timing evidence follows;
  remote CI/review and full Phase 2 sign-off remain pending.

- 2026-10-18 — #128 implements the existing extrusion `to` selector for one current
  parallel planar face's infinite supporting plane (RFCs 0005/0007). The selected trim is
  not a clipping mask: profiles may extend beyond it. A finite positive ray-plane distance
  drives the existing exact extrusion backend, preserving new/join/cut/intersect history.
  Direction defaults to the sketch normal; opposite target normals work, but targets behind
  the requested ray, separation within 1e-6 mm and rays with either plane-normal alignment
  <=1e-9 fail explicitly. Tilted/curved targets, symmetric `to` and nonzero draft remain
  unsupported. Parallelism uses sine tolerance 1e-9; positive-weight profile control hulls
  additionally certify the full cap within 1e-6 mm of the target, preventing angular tolerance
  amplification over large profiles. A final failing regression proves an almost in-plane
  ray cannot be admitted by a tolerance-level target tilt; both normal alignments are checked.
  Existing semantic selectors, recursive unions/exclusions, filters, nearest distances and
  per-node cardinality retain precise pointers rooted at `/inputs/to`. Source snapshots and
  hashes remain separate from operation targets and are retained/charged to cache estimates.
  New bodies, result targets that also supply the stop plane, current placements, consumed
  producer aliases and retained-tool ambiguity are covered. Described and directly queried
  plane frames are validated as finite/orthonormal before filtering or cache reuse.
  Tests first: seven of eight initial evaluator cases fail on unsupported `to`; the generic
  rejection case already passes. The expanded focused suite passes 22 cases / 976 assertions,
  seed 20261018, including oblique rays, reversed normals, sketch/world frames, curved holes,
  all result modes, source/output ID rebinding, invalid metadata on cache hits, independent
  branches, suppression/rollback, bounded retention and finite-distance overflow checks.
  No persisted schema, kernel API, public value-layout, backend identity or dependency change;
  evaluator identity advances to `evaluator/25;roles/5`. Verification and timings follow;
  remote CI/review and full Phase 2 sign-off remain pending.

- 2026-10-18 — #129 implements equal-distance chamfers and face-boundary edge queries
  under RFCs 0005/0007. Deep copies preserve input geometry; validated solids require complete
  face/edge history and provenance for every new face. Recursive edge unions/exclusions use
  current face names and existing face filters; only selected owners become operation targets.
  Queries validate handles before cache reuse, and multi-body geometry/naming publish atomically.
  Six of seven initial evaluator cases fail before implementation. The expanded suite and CLI
  STEP reloads pass the local gate; sanitizer/timing evidence follows below. Backend implementers
  add the two virtual methods and capability flag, rebuilding consumers. No persisted change;
  evaluator identity advances to `evaluator/26;roles/5`. CI/review and phase sign-off stay pending.

- 2026-10-18 — #130 starts the native visual-testing integration already authorized for
  Phase 2 by RFC 0003. A typed `IKernel::tessellate` query returns indexed world positions,
  outward surface normals and one range per original snapshot face. Deep-copy meshing keeps
  cached exact geometry and existing triangulations unchanged. Output budgets bound returned
  triangles/vertices, not OCCT's internal time/allocation; native admission/watchdog work follows.
  Tests first fail to compile without the query. Geometry regressions then catch lost copied-face
  orientations and a collapsed pole triangle with distinct node IDs; both receive explicit fixes.
  Eight backend cases and two evaluator cases pass, including all twenty current references,
  cavities, mirrors, chamfers, cache reuse, eviction and retained picking handles. The backend
  identity adds `mesh=indexed-faces-copy-v1`; implementers add the virtual method and rebuild
  consumers. No persisted format, dependency or phase-gate change. The C bridge, XPC/Metal
  connection and portable app bundle follow as bounded increments under the accepted RFC.

## Rebuild benchmark evidence

2026-10-17, Apple M3 Pro (12 cores, 36 GiB), Release, dynamically linked OCCT 7.9.3.
Run `build/occt/services/kernel/nf_evaluator_bench` after building the `occt` preset.
Each part has one sketch, one extrusion and 28 transforms. The bored plate has two circular
holes represented by exact rational NURBS. Each row has 30 samples with unique parameter
values; every sample checks analytic volume/centroid and the expected kernel/cache counts.
Timers include JSON parsing, validation, expressions, hashing, geometry, validity checks and
name propagation. Serialization, IPC and tessellation are excluded.

| Part | Rebuild | Median ms | p95 ms | Max ms | Kernel calls / cache hits |
| --- | --- | ---: | ---: | ---: | ---: |
| Plate | Cold | 3.073 | 4.888 | 5.980 | 29 / 0 |
| Plate | Depth edit | 3.081 | 6.497 | 9.765 | 29 / 1 |
| Plate | Last transform edit | 0.698 | 1.148 | 1.603 | 1 / 29 |
| Plate | Unused parameter edit | 0.684 | 1.452 | 1.550 | 0 / 30 |
| Bored plate | Cold | 15.588 | 19.352 | 20.570 | 29 / 0 |
| Bored plate | Depth edit | 14.263 | 22.587 | 28.131 | 29 / 1 |
| Bored plate | Last transform edit | 1.099 | 3.220 | 3.876 | 1 / 29 |
| Bored plate | Unused parameter edit | 1.169 | 2.326 | 2.402 | 0 / 30 |

Before rigid-invariant reuse, depth edits measured 114.290/382.885 ms median for the two
parts. The benchmark cache has an explicit 96-entry/64 MiB estimated budget; after 30 depth
edits it retains 96 entries, estimated at 10.25/26.24 MiB respectively. This is not a process
RSS guarantee or a service-wide default. These are local measurements for the implemented
feature subset; 20 reference parts in CI and full phase sign-off remain pending (#95).

Final local verification: `tools/check.sh --preset occt` passes 252/252 (15.90 s), including
the 202-case schema cross-check and hygiene checks. After the optimization, the exact CI
ASan/UBSan kernel/evaluator/cache selection passes 38/38 (15.02 s). The earlier full sanitizer
run passed 251/252 (228.96 s), with no sanitizer diagnostics; the unchanged closest-point
2-second timing assertion took 5.136 s, then passed alone in 1.94 s. OCCT itself uses the
pinned release build; Nightfall code is instrumented. CI now runs the geometry sanitizer
selection on OCCT pull requests/nightly builds. Remote CI and maintainer review remain pending.

### Solid boolean rebuild evidence

The same machine/configuration and 30-sample methodology now also measure a 30-feature part
with two sketches, two extrusions, one circular through-cut and 25 transforms. The cutter
extends beyond the plate throughout the depth range. Every sample checks the exact volume
`(200 - 4π) × depth` mm³, centroid and kernel/cache counts. The tool is consumed by the cut.

| Boolean-cut plate | Median ms | p95 ms | Max ms | Kernel calls / cache hits |
| --- | ---: | ---: | ---: | ---: |
| Cold | 53.098 | 56.046 | 56.488 | 28 / 0 |
| Depth edit | 49.975 | 53.884 | 57.060 | 27 / 3 |
| Last transform edit | 0.692 | 0.714 | 0.721 | 1 / 29 |
| Unused parameter edit | 0.657 | 0.691 | 0.702 | 0 / 30 |

The 96-entry/64 MiB estimated cache retains 18,700,280 estimated bytes after the depth edits;
this remains an ownership estimate, not RSS. The same run measured plain/bored depth-edit
medians of 2.081/9.556 ms. Timing variation across local runs is not itself an optimization claim.
`tools/check.sh --preset occt` passes 266/266 tests (13.26 s), with all hygiene checks and the
202-case independent schema cross-check passing.
The full `tools/check.sh --preset asan` run also passes 266/266 (188.30 s), including all
new geometry/evaluator tests, with no sanitizer diagnostics or retries. Nightfall code is
ASan/UBSan-instrumented; the OCCT shared libraries remain the pinned Release build. Remote
CI and maintainer review are pending; this extends the local latency evidence only.

### Revolution rebuild evidence

2026-10-18, same machine/configuration and 30-sample methodology. The turned sleeve has one
sketch, one revolution and 28 transforms. Every unique height edit rebuilds the sketch and
all 29 kernel operations; analytic volume `75π × height` mm³, centroid and execution counts
are checked for every sample. Exact line spans retain analytic surfaces in OCCT. Before this
representation change, sleeve height edits measured 1,756.782 ms median in an earlier run.

| Turned sleeve | Median ms | p95 ms | Max ms | Kernel calls / cache hits |
| --- | ---: | ---: | ---: | ---: |
| Cold | 8.828 | 29.102 | 37.186 | 29 / 0 |
| Height edit | 6.934 | 8.842 | 9.595 | 29 / 0 |
| Last transform edit | 0.613 | 0.978 | 1.026 | 1 / 29 |
| Unused parameter edit | 0.596 | 0.633 | 0.845 | 0 / 30 |

The sleeve cache retains 96 entries / 6,422,472 estimated bytes after the height edits.
This final benchmark ran after Nightfall's builds/tests finished. Same-run depth-edit results
for the existing cases are recorded below rather than assuming earlier timings still apply.

| Existing part, depth edit | Median ms | p95 ms | Max ms |
| --- | ---: | ---: | ---: |
| Plate | 2.137 | 2.337 | 2.783 |
| Bored plate | 18.263 | 28.047 | 33.780 |
| Boolean-cut plate | 72.176 | 126.785 | 129.510 |

The sleeve meets the local 100 ms criterion in every sample. The boolean case's tail exceeds
100 ms in this run; these measurements do not establish a universal latency bound. Broader
reference-part coverage, remote CI/review of this slice and full Phase 2 sign-off remain open.

### Curved profile rebuild evidence

2026-10-18, same Apple M3 Pro / Release OCCT 7.9.3 and 30-sample methodology. The new elliptic
tube has two nested ellipses rotated 35°, an extrusion and 28 transforms. Changing `depth`
changes both ellipse major radii and extrusion distance: outer radii `(depth/3, 5)` mm, inner
radii `(depth/6, 2.5)` mm. Every sample verifies volume `1.25π × depth²` mm³, centroid and
execution counts. Profile and all downstream geometry rebuild on each unique depth edit.

| Elliptic tube | Median ms | p95 ms | Max ms | Kernel calls / cache hits |
| --- | ---: | ---: | ---: | ---: |
| Cold | 9.366 | 9.906 | 10.113 | 29 / 0 |
| Profile + depth edit | 9.467 | 10.593 | 11.907 | 29 / 0 |
| Last transform edit | 0.656 | 0.707 | 0.731 | 1 / 29 |
| Unused parameter edit | 0.609 | 0.688 | 0.760 | 0 / 30 |

The cache retains 96 entries / 18,732,720 estimated bytes after profile/depth edits (not RSS).
Final benchmark after the review fix: same-run depth-edit median/p95/max is
plate 1.966/2.518/4.987 ms, bored plate 9.781/11.660/18.681 ms,
boolean-cut plate 47.112/50.712/51.059 ms, sleeve 6.318/7.494/7.524 ms. Every sample is under
100 ms in this run; earlier slower tails remain documented above. Timing variation is not
an optimization claim or a universal bound. Includes parsing, validation, geometry and naming;
excludes serialization, IPC and tessellation. Full phase sign-off remains open.

### Shared straight-boundary rebuild evidence

2026-10-18, Apple M3 Pro / 12 cores / 36 GiB, Release OCCT 7.9.3, 30 samples per case.
The 30-feature part has two adjacent rectangles, one extrusion and 28 transforms. Each unique
`depth` edit changes both rectangle widths and the extrusion distance. The union has width
`2 × depth/3` mm and height 20 mm; every sample checks volume `(40/3) × depth²` mm³, centroid
and execution counts. The shared internal edge must disappear from the filled outline.

| Shared-edge plate | Median ms | p95 ms | Max ms | Kernel calls / cache hits |
| --- | ---: | ---: | ---: | ---: |
| Cold | 3.238 | 6.298 | 7.173 | 29 / 0 |
| Profile + depth edit | 7.934 | 22.958 | 106.810 | 29 / 0 |
| Last transform edit | 1.841 | 15.023 | 27.758 | 1 / 29 |
| Unused parameter edit | 1.132 | 15.357 | 17.751 | 0 / 30 |

The cache retains 96 entries / 14,631,984 estimated bytes after profile/depth edits (not RSS).
Same-run depth-edit median/p95/max: plate 2.357/4.381/5.129 ms,
bored plate 17.057/26.177/36.863 ms, boolean-cut plate 79.371/124.210/256.712 ms,
sleeve 13.588/157.142/191.908 ms, elliptic tube 15.563/27.029/30.351 ms.
The shared-edge maximum and boolean/sleeve tails exceed 100 ms in this run. These are local
measurements with visible timing variation; no universal latency bound or performance
improvement is claimed. Includes parsing, validation, geometry and naming; excludes
serialization, IPC and tessellation. Full Phase 2 sign-off remains open.

### Analytic circular-profile rebuild evidence

2026-10-18, Apple M3 Pro / 12 cores / 36 GiB, Release OCCT 7.9.3. The new 30-feature
toroidal seal has one circular sketch, one revolution and 28 transforms. Its major radius
is 20 mm; each unique `depth` edit changes the section radius to `depth/10` mm (3.1–6 mm).
Every sample checks volume `0.4π² × depth²` mm³ within 1e-5, centroid within 1e-6 mm and
the expected kernel/cache counts. Run `build/occt/services/kernel/nf_evaluator_bench`.

| Toroidal seal, 30 samples | Median ms | p95 ms | Max ms | Kernel calls / cache hits |
| --- | ---: | ---: | ---: | ---: |
| Cold | 88.984 | 120.664 | 126.083 | 29 / 0 |
| Section radius edit | 80.776 | 92.599 | 95.948 | 29 / 0 |
| Last transform edit | 0.626 | 0.724 | 0.726 | 1 / 29 |
| Unused parameter edit | 0.610 | 0.992 | 1.150 | 0 / 30 |

The cache retains 96 entries / 7,212,480 estimated bytes after radius edits (not RSS).
All radius-edit samples are below 100 ms; the cold-build tail exceeds it. This run follows
the builds and sanitizer checks. It includes parsing, validation, expressions, hashing,
exact geometry and naming; serialization, IPC and tessellation remain excluded.

Same-run depth-edit median/p95/max: plate 2.343/5.005/5.409 ms,
bored plate 7.960/8.267/8.944 ms, boolean-cut plate 9.633/12.385/14.630 ms,
sleeve 3.315/4.220/5.490 ms, elliptic tube 10.908/16.012/16.828 ms,
shared-edge plate 2.870/4.907/5.574 ms. These are local observations, not universal bounds.

The focused full-torus regression (R=20 mm, r=3 mm) took 8.423 s on the original NURBS
backend and 0.076 s after analytic arcs plus conditioned mass integration. Both checked
volume 360π² mm³ and area 240π² mm² within 1e-5, and centroid within 1e-7 mm; the original
backend failed the new analytic-surface-type assertion. These are single correctness-run
timings, separate from the 30-sample rebuild benchmark. The complete reference selection
passes 21/21 in 2.47 s; all original analytic and cache assertions remain intact.

### Shared curved-boundary rebuild evidence

2026-10-18, Apple M3 Pro / 12 cores / 36 GiB, Release OCCT 7.9.3, 30 samples per case.
The 30-feature shared-arc part has a circle of radius 5 mm with a 90° cap removed under
even-odd fill, one extrusion and 28 transforms. Every sample checks cross-section area
`A = 25 × (3π/4 + 1/2)` through volume `A × depth` mm³, centroid x `−125/(6A)` before
translations, centroid z `depth/2`, and kernel/cache counts. Depth edits reuse the sketch
and rebuild all 29 solid operations. Run `build/occt/services/kernel/nf_evaluator_bench`.

| Shared-arc notch | Median ms | p95 ms | Max ms | Kernel calls / cache hits |
| --- | ---: | ---: | ---: | ---: |
| Cold | 3.160 | 3.284 | 3.344 | 29 / 0 |
| Depth edit | 3.335 | 3.479 | 3.484 | 29 / 1 |
| Last transform edit | 0.604 | 0.629 | 0.662 | 1 / 29 |
| Unused parameter edit | 0.579 | 0.605 | 0.606 | 0 / 30 |

The cache retains 96 entries / 10,753,680 estimated bytes after depth edits (not RSS).
Same-run depth-edit median/p95/max: plate 1.916/2.007/2.062 ms,
bored plate 7.424/7.672/7.990 ms, boolean-cut plate 7.857/8.089/8.117 ms,
sleeve 2.935/3.006/3.027 ms, elliptic tube 9.812/10.015/10.139 ms,
shared-edge plate 2.466/2.564/2.595 ms, toroidal seal 76.437/78.148/78.855 ms.
All samples are below 100 ms in this run; earlier slower tails remain documented above.
These local observations do not establish a universal bound or claim an optimization.
This run follows all builds/tests and includes parsing, validation, expressions, hashing,
exact geometry, validity and naming; serialization, IPC and tessellation remain excluded.
Remote CI, review and full Phase 2 sign-off remain pending.

### Constrained sketch rebuild evidence

2026-10-18, Apple M3 Pro / 12 cores / 36 GiB, Release OCCT 7.9.3, 30 samples per case.
The 30-feature part has four authored lines, six orientation/equal-length constraints,
one extrusion and 28 transforms. Each unique `depth` edit changes the centered rectangle's
width to `depth/3` mm, retains its 20 mm height, and changes the extrusion distance. Every
sample verifies volume `(20/3) × depth²` mm³ within 1e-5, centroid x/z within 1e-6 mm and
execution counts. Profile edits recheck every constraint and rebuild all 29 solid operations.
Run `build/occt/services/kernel/nf_evaluator_bench`.

| Constrained plate | Median ms | p95 ms | Max ms | Kernel calls / cache hits |
| --- | ---: | ---: | ---: | ---: |
| Cold | 1.817 | 2.009 | 2.054 | 29 / 0 |
| Profile + depth edit | 1.950 | 2.119 | 2.122 | 29 / 0 |
| Last transform edit | 0.655 | 0.674 | 0.678 | 1 / 29 |
| Unused parameter edit | 0.622 | 0.663 | 0.679 | 0 / 30 |

The 96-entry/64 MiB cache budget retains 96 entries / 10,525,848 estimated bytes after profile
edits (not RSS). Same-run depth-edit median/p95/max: plate 1.902/2.003/2.023 ms,
bored plate 7.242/7.448/7.479 ms, boolean-cut plate 8.179/8.513/8.960 ms,
sleeve 6.088/9.782/11.483 ms, elliptic tube 10.236/10.555/10.578 ms,
shared-edge plate 2.568/2.691/2.691 ms, shared-arc notch 3.490/3.632/3.659 ms,
toroidal seal 76.747/80.182/80.604 ms. All parameter-edit samples are below 100 ms in this
run; elliptic-tube cold builds have a 19.084/109.299/173.630 ms median/p95/max. These are local
observations, not universal bounds or an optimization claim. This run follows all builds/tests
and includes parsing, validation, expressions, constraint verification, hashing, exact geometry,
validity and naming; serialization, IPC and tessellation remain excluded. Remote CI/review
and full Phase 2 sign-off remain pending.

### Point-reference constraint rebuild evidence

2026-10-18, Apple M3 Pro / 12 cores / 36 GiB, Release OCCT 7.9.3, 30 samples per case.
The 30-feature part extends the constrained plate with three named points and four relations:
endpoint coincidence, endpoint distance, midpoint and reflection symmetry. Each unique depth
edit changes profile width, point positions and the distance dimension. All ten constraints
are verified before rebuilding the extrusion and 28 transforms. Every sample checks volume
`(20/3) × depth²` mm³ within 1e-5, centroid x/z within 1e-6 mm and execution/cache counts.
Run `build/occt/services/kernel/nf_evaluator_bench`.

| Point-reference plate | Median ms | p95 ms | Max ms | Kernel calls / cache hits |
| --- | ---: | ---: | ---: | ---: |
| Cold | 3.725 | 5.280 | 5.569 | 29 / 0 |
| Profile + point + depth edit | 2.548 | 5.969 | 7.003 | 29 / 0 |
| Last transform edit | 0.830 | 1.832 | 2.072 | 1 / 29 |
| Unused parameter edit | 0.877 | 2.012 | 2.103 | 0 / 30 |

The 96-entry/64 MiB cache budget retains 96 entries / 10,529,169 estimated bytes after profile
edits, including named points (not RSS). Same-run depth-edit median/p95/max:
plate 4.800/14.323/20.848 ms, bored plate 11.953/15.890/17.751 ms,
boolean-cut plate 11.860/17.691/19.629 ms, sleeve 5.184/8.136/9.023 ms,
elliptic tube 20.813/24.498/24.800 ms, shared-edge plate 5.312/6.653/7.398 ms,
shared-arc notch 5.533/9.410/11.429 ms, constrained plate 2.793/5.331/5.854 ms,
toroidal seal 103.771/149.924/153.931 ms. The toroidal median/tail exceeds 100 ms in this
run; elliptic cold builds reach 404.300 ms and even fully cached sleeve edits reach 276.516 ms.
The point-reference case stays below 100 ms in every sample. These observations include
substantial timing variation and do not establish a universal bound or optimization claim.
The run follows all builds/tests and includes parsing, validation, expressions, point/constraint
verification, hashing, exact geometry, validity and naming. Serialization, IPC and tessellation
remain excluded. Remote CI/review and full Phase 2 sign-off remain pending.

### Tangent-constrained rounded profile rebuild evidence

2026-10-18, Apple M3 Pro / 12 cores / 36 GiB, Release OCCT 7.9.3, 30 samples per case.
The 30-feature rounded part has two semicircles, two line segments, four tangent constraints,
one extrusion and 28 transforms. The radius is `5 mm × sqrt(depth / (30 mm))`; each line is
four radii long. Unique depth edits change the whole profile and extrusion, recheck all four
contacts and rebuild 29 solid operations. Every sample checks volume
`(25/30) × (8+π) × D²` mm³ for depth D in mm within 1e-5, centroid x/z within 1e-6 mm,
and kernel/cache counts. Run `build/occt/services/kernel/nf_evaluator_bench`.

| Tangent-constrained rounded part | Median ms | p95 ms | Max ms | Kernel calls / cache hits |
| --- | ---: | ---: | ---: | ---: |
| Cold | 3.441 | 3.509 | 3.529 | 29 / 0 |
| Profile + depth edit | 3.642 | 3.841 | 3.940 | 29 / 0 |
| Last transform edit | 0.624 | 0.670 | 0.699 | 1 / 29 |
| Unused parameter edit | 0.593 | 0.631 | 0.643 | 0 / 30 |

The 96-entry/64 MiB cache budget retains 96 entries / 14,632,896 estimated bytes after profile
edits (not RSS). Same-run depth-edit median/p95/max: plate 1.727/1.803/1.877 ms,
bored plate 6.660/6.761/6.769 ms, boolean-cut plate 7.279/7.457/7.489 ms,
sleeve 2.666/2.706/2.771 ms, elliptic tube 9.297/9.490/9.812 ms,
shared-edge plate 2.476/2.578/2.690 ms, shared-arc notch 3.089/3.179/3.247 ms,
constrained plate 1.801/1.866/1.881 ms, point-reference plate 1.865/1.974/2.009 ms,
toroidal seal 70.343/74.703/74.914 ms. All samples remain below 100 ms in this run; earlier
slower tails remain recorded above. These are local observations, not universal bounds or
an optimization claim. The run follows all builds/tests and includes parsing, validation,
expressions, constraint verification, hashing, exact geometry, validity and naming.
Serialization, IPC and tessellation remain excluded. Remote CI/review and full Phase 2
sign-off remain pending.

### Angle and fixed constraint rebuild evidence

2026-10-18, Apple M3 Pro / 12 cores / 36 GiB, Release OCCT 7.9.3, 30 samples per case.
The 30-feature part is a centered parallelogram (width 10 mm, height 20 mm), one extrusion
and 28 transforms. Angle θ is `depth / (1 mm) × 1 deg`; each depth edit changes the slant,
angle dimension and fixed line/endpoint coordinates. The profile area stays 200 mm².
Every sample checks volume `200 × D` mm³ and surface area `400 + D × (20 + 40/sin(θ))`
mm² within 1e-5, centroids x/z within 1e-6 mm, and kernel/cache counts. D is depth in mm.
Run `build/occt/services/kernel/nf_evaluator_bench`. All 30 samples of every mode passed
the analytic and execution-count checks.

| Angle/fixed slanted part | Median ms | p95 ms | Max ms | Kernel calls / cache hits |
| --- | ---: | ---: | ---: | ---: |
| Cold | 17334.659 | 17738.942 | 20483.019 | 29 / 0 |
| Angle + profile + depth edit | 37.344 | 17102.398 | 17682.842 | 29 / 0 |
| Last transform edit | 0.627 | 0.661 | 0.661 | 1 / 29 |
| Unused parameter edit | 0.630 | 0.656 | 0.659 | 0 / 30 |

The cold builds and parameter-edit tail exceed 100 ms by orders of magnitude. The local
performance gate is reopened for this profile; prior fast baseline measurements remain valid.
The kernel mass-integration follow-up and fix plan are recorded in the #113 log entry above.
A one-second process sample during an untimed warm-up corroborates the same OCCT nested
Kronrod integration path as the targeted regression. Cached changes avoid rebuilding that solid.

The 96-entry/64 MiB cache budget retains 96 entries / 10,528,224 estimated bytes after profile
edits (not RSS). Same-run depth-edit median/p95/max: plate 1.862/1.924/1.937 ms,
bored plate 7.013/7.171/7.176 ms, boolean-cut plate 7.788/7.957/8.053 ms,
sleeve 2.836/2.891/2.899 ms, elliptic tube 9.633/10.112/10.176 ms,
shared-edge plate 2.340/2.458/2.530 ms, shared-arc notch 3.140/3.224/3.253 ms,
constrained plate 1.845/1.909/1.918 ms, point-reference plate 1.907/1.992/2.037 ms,
tangent-constrained rounded part 3.925/4.038/4.085 ms, toroidal seal 75.710/76.513/76.671 ms.
These are local observations, not universal bounds or an optimization claim. The run follows
all builds/tests and includes parsing, validation, expressions, constraint verification,
hashing, exact geometry, validity and naming. Serialization, IPC and tessellation remain
excluded. Remote CI/review and full Phase 2 sign-off remain pending.

### Polynomial mass integration rebuild evidence

2026-10-18, Apple M3 Pro / 12 cores / 36 GiB, Release OCCT 7.9.3, 30 samples per case.
Run `build/occt/services/kernel/nf_evaluator_bench`. The benchmark source and analytic
expectations are unchanged from #113: a slanted 30-feature part, angle/profile/depth
edits, volume and area within 1e-5, centroid x/z within 1e-6 mm, plus kernel/cache counts.
All samples of every mode pass those checks.

| Slanted part after polynomial integration | Median ms | p95 ms | Max ms | Kernel calls / cache hits |
| --- | ---: | ---: | ---: | ---: |
| Cold | 1.858 | 1.924 | 1.971 | 29 / 0 |
| Angle + profile + depth edit | 2.010 | 2.070 | 2.087 | 29 / 0 |
| Last transform edit | 0.635 | 0.666 | 0.669 | 1 / 29 |
| Unused parameter edit | 0.615 | 0.633 | 0.635 | 0 / 30 |

#113's cold median was 17334.659 ms and parameter-edit p95/max were
17102.398/17682.842 ms. The local slanted-profile latency gap is resolved in this run;
all parameter-edit samples across the catalogue stay below 100 ms. Earlier measurements
remain recorded above; these are local observations, not universal bounds.

The 96-entry/64 MiB cache budget retains 96 entries / 10,528,224 estimated bytes after
slanted-profile edits (not RSS). Same-run depth-edit median/p95/max:
plate 1.853/1.995/2.104 ms, bored plate 7.221/7.413/7.443 ms,
boolean-cut plate 7.936/8.285/8.306 ms, sleeve 2.933/2.985/3.092 ms,
elliptic tube 9.724/9.999/10.002 ms, shared-edge plate 2.540/2.689/2.843 ms,
shared-arc notch 3.260/3.456/3.489 ms, constrained plate 2.225/3.310/3.920 ms,
point-reference plate 2.019/2.117/2.118 ms, tangent-constrained rounded part
3.853/3.992/4.020 ms, toroidal seal 75.474/77.356/77.800 ms.
The run follows all builds/tests and includes parsing, validation, expressions,
constraint verification, hashing, exact geometry, validity and naming. Serialization,
IPC and tessellation remain excluded. Remote CI/review and full Phase 2 sign-off remain pending.

### Sketch-line axis rebuild evidence

2026-10-18, Apple M3 Pro / 12 cores / 36 GiB, Release OCCT 7.9.3, 30 samples per mode.
The new 30-feature part has a four-line rectangular sketch in the zx plane, a full revolution
about the named bottom edge, and 28 transforms. Each unique depth edit changes both the
profile and the axis line's endpoint. The result is a radius-5 mm cylinder with height D
and axis at world x = 5 mm. Every sample checks volume `25πD` mm³ within 1e-5, centroid
x (including translations) and z within 1e-6 mm, and kernel/cache counts. Run
`build/occt/services/kernel/nf_evaluator_bench`. All 30 samples of every mode pass.

| Sketch-axis cylinder | Median ms | p95 ms | Max ms | Kernel calls / cache hits |
| --- | ---: | ---: | ---: | ---: |
| Cold | 1.991 | 2.292 | 2.299 | 29 / 0 |
| Profile + axis endpoint + depth edit | 2.190 | 2.550 | 2.679 | 29 / 0 |
| Last transform edit | 0.629 | 0.718 | 0.732 | 1 / 29 |
| Unused parameter edit | 0.624 | 0.905 | 1.130 | 0 / 30 |

The 96-entry/64 MiB cache budget retains 96 entries / 4,376,988 estimated bytes after depth
edits (not RSS). Same-run depth-edit median/p95/max: plate 2.046/2.114/2.126 ms,
bored plate 8.165/9.098/10.548 ms, boolean-cut plate 9.320/10.478/11.571 ms,
sleeve 3.418/3.649/3.829 ms, elliptic tube 10.531/11.135/11.380 ms,
shared-edge plate 2.831/3.477/4.293 ms, shared-arc notch 3.599/3.658/3.666 ms,
constrained plate 2.228/2.319/2.371 ms, point-reference plate 2.293/2.459/2.504 ms,
tangent-constrained rounded part 4.357/4.488/4.517 ms, angle/fixed slanted part
2.305/2.455/2.521 ms and toroidal seal 77.410/78.681/80.260 ms. All parameter-edit
samples across the thirteen-part catalogue stay below 100 ms in this run. These are local
observations, not universal bounds or an optimization claim. The run follows all builds/tests
and includes parsing, validation, expressions, constraint verification, reference resolution,
hashing, exact geometry, validity and naming. Serialization, IPC and tessellation remain
excluded. Remote CI/review and full Phase 2 sign-off remain pending.

### Open reference sketch rebuild evidence

2026-10-18, Apple M3 Pro / 12 cores / 36 GiB, Release OCCT 7.9.3, 30 samples per mode.
The new 30-feature part has a closed rectangular profile, a separate one-line axis sketch,
a full revolution and 27 transforms. Both sketches use the zx plane. Each unique depth edit
changes the profile width and centerline endpoint; the horizontal constraint is verified before
axis resolution. The result is a sleeve with radii 5/10 mm and height D. Every sample checks
volume `75πD` mm³ within 1e-5, centroid x (including translations) and z within 1e-6 mm,
and kernel/cache counts. Run `build/occt/services/kernel/nf_evaluator_bench`. All 30 samples
of every mode pass.

| Standalone-axis sleeve | Median ms | p95 ms | Max ms | Kernel calls / cache hits |
| --- | ---: | ---: | ---: | ---: |
| Cold | 2.873 | 2.947 | 2.979 | 28 / 0 |
| Profile + centerline endpoint + depth edit | 3.038 | 3.119 | 3.145 | 28 / 0 |
| Last transform edit | 0.594 | 0.617 | 0.628 | 1 / 29 |
| Unused parameter edit | 0.580 | 0.604 | 0.609 | 0 / 30 |

The 96-entry/64 MiB cache budget retains 96 entries / 6,221,388 estimated bytes after depth
edits (not RSS). Same-run depth-edit median/p95/max: plate 2.069/2.135/2.136 ms,
bored plate 8.039/9.056/9.150 ms, boolean-cut plate 8.933/18.198/27.872 ms,
sleeve 3.064/3.156/3.173 ms, closed-sketch-axis cylinder 1.904/1.955/2.026 ms,
elliptic tube 10.252/12.823/14.994 ms, shared-edge plate 2.644/2.716/2.729 ms,
shared-arc notch 3.471/3.635/3.665 ms, constrained plate 1.943/2.004/2.038 ms,
point-reference plate 2.007/2.148/2.162 ms, tangent-constrained rounded part
3.889/4.069/4.080 ms, angle/fixed slanted part 2.007/2.136/2.143 ms and toroidal seal
75.407/76.477/78.021 ms. All parameter-edit samples across the fourteen-part catalogue stay
below 100 ms in this run. These are local observations, not universal bounds or an optimization
claim. The run follows all builds/tests and includes parsing, validation, expressions,
constraint verification, reference resolution, hashing, exact geometry, validity and naming.
Serialization, IPC and tessellation remain excluded. Remote CI/review and full Phase 2
sign-off remain pending.

### Linear body pattern rebuild evidence

2026-10-18, Apple M3 Pro / 12 cores / 36 GiB, Release OCCT 7.9.3, 30 samples per mode.
The new 30-feature part has one sketch, one extrusion, a three-instance linear body pattern
at 15 mm spacing along x, and 27 transforms. Every depth edit rebuilds the extrusion, two
private copies, their union and all transforms. Each sample checks volume `600D` mm³ and
surface area `1200 + 180D` mm² within 1e-5, centroid x (including translations) and z within
1e-6 mm, and kernel/cache counts. D is depth in mm. Run
`build/occt/services/kernel/nf_evaluator_bench`. All 30 samples of every mode pass.

| Three-instance linear pattern | Median ms | p95 ms | Max ms | Kernel calls / cache hits |
| --- | ---: | ---: | ---: | ---: |
| Cold | 5.810 | 5.983 | 5.984 | 31 / 0 |
| Seed depth edit | 6.304 | 6.429 | 6.474 | 31 / 1 |
| Last transform edit | 0.805 | 0.834 | 0.884 | 1 / 29 |
| Unused parameter edit | 0.745 | 0.771 | 0.805 | 0 / 30 |

The 96-entry/64 MiB cache budget retains 96 entries / 31,101,608 estimated bytes after depth
edits, including private copies/history (not RSS). Same-run depth-edit median/p95/max:
plate 1.925/2.022/2.134 ms, bored plate 7.374/7.568/7.588 ms,
boolean-cut plate 7.841/8.073/8.095 ms, sleeve 2.844/2.927/2.932 ms,
closed-sketch-axis cylinder 1.820/1.890/1.894 ms, standalone-axis sleeve 2.830/2.998/3.069 ms,
elliptic tube 9.833/10.136/10.147 ms, shared-edge plate 2.488/2.586/2.655 ms,
shared-arc notch 3.340/3.401/3.413 ms, constrained plate 1.978/2.059/2.091 ms,
point-reference plate 2.005/2.111/2.139 ms, tangent-constrained rounded part
3.987/4.082/4.205 ms, angle/fixed slanted part 2.028/2.155/2.190 ms and toroidal seal
75.553/76.605/76.797 ms. All parameter-edit samples across the fifteen-part catalogue stay
below 100 ms in this run. These are local observations, not universal bounds or an optimization
claim. The run follows all builds/tests and includes parsing, validation, expressions,
constraint verification, reference resolution, hashing, exact geometry, validity and naming.
Serialization, IPC and tessellation remain excluded. Remote CI/review and full Phase 2
sign-off remain pending.

### Circular body pattern rebuild evidence

2026-10-18, Apple M3 Pro / 12 cores / 36 GiB, Release OCCT 7.9.3, 30 samples per mode.
The new 30-feature part has one sketch, one extrusion, a four-instance full-circle body
pattern around z, and 27 transforms. The seed is a 2×2 mm square centered 10 mm from the
axis. Every depth edit rebuilds the extrusion, three private rotations, their union and
all transforms. Each sample checks volume `16D` mm³ and area `32 + 32D` mm² within 1e-5,
centroid x (including translations) and z within 1e-6 mm, and exact kernel/cache counts.
D is depth in mm. Run `build/occt/services/kernel/nf_evaluator_bench` after builds/tests.
All 30 samples of every mode pass on all sixteen parts (1920 measured rebuilds).

| Four-instance circular pattern | Median ms | p95 ms | Max ms | Kernel calls / cache hits |
| --- | ---: | ---: | ---: | ---: |
| Cold | 7.398 | 7.474 | 7.560 | 32 / 0 |
| Seed depth edit | 8.001 | 8.198 | 8.278 | 32 / 1 |
| Last transform edit | 0.968 | 0.999 | 1.008 | 1 / 29 |
| Unused parameter edit | 0.873 | 0.917 | 0.956 | 0 / 30 |

The 96-entry/64 MiB budget retains 96 entries / 41,269,312 estimated bytes after depth
edits, including private copies/history (not RSS). Same-run depth-edit median/p95/max:
linear pattern 6.134/6.320/6.340 ms, plain plate 1.857/1.939/2.052 ms,
bored plate 7.401/7.829/7.918 ms, boolean-cut plate 7.887/8.047/8.125 ms,
sleeve 2.946/3.069/3.075 ms, closed-sketch-axis cylinder 1.794/1.883/1.888 ms,
standalone-axis sleeve 2.788/2.964/2.970 ms, elliptic tube 9.580/9.800/9.869 ms,
shared-edge plate 2.453/2.519/2.534 ms, shared-arc notch 3.248/3.333/3.362 ms,
constrained plate 1.920/1.994/2.015 ms, point-reference plate 1.972/2.054/2.060 ms,
tangent-constrained rounded part 3.785/3.873/3.937 ms, angle/fixed slanted part
2.003/2.089/2.116 ms and toroidal seal 74.958/76.519/76.646 ms. All parameter-edit
samples remain below 100 ms in this local run. These are observations, not universal bounds
or an optimization claim. Timing includes parsing, schema/parameter/input evaluation,
reference resolution, hashing, geometry, validity and naming; serialization, IPC and
tessellation remain excluded. Remote CI/review and full Phase 2 sign-off remain pending.

### Body mirror rebuild evidence

2026-10-18, Apple M3 Pro / 12 cores / 36 GiB, Release OCCT 7.9.3, 30 samples per mode.
The new 30-feature part has one sketch, one extrusion, one body mirror across yz and 27
transforms. Its 2×2 mm seed is centered at x = 10 mm. Each depth edit rebuilds the extrusion,
the fully validated exact reflection, the seed/copy union and all transforms. Every sample
checks volume `8D` mm³ / area `16 + 16D` mm² within 1e-5, centroid x (including translations)
and z within 1e-6 mm, and exact kernel/cache counts. D is depth in mm. Run
`build/occt/services/kernel/nf_evaluator_bench` after builds/tests. All 30 samples of every
mode pass on all seventeen parts (2040 measured rebuilds).

| Body mirror | Median ms | p95 ms | Max ms | Kernel calls / cache hits |
| --- | ---: | ---: | ---: | ---: |
| Cold | 5.071 | 5.169 | 5.177 | 30 / 0 |
| Seed depth edit | 5.534 | 6.013 | 6.026 | 30 / 1 |
| Last transform edit | 0.690 | 0.743 | 0.769 | 1 / 29 |
| Unused parameter edit | 0.652 | 0.677 | 0.693 | 0 / 30 |

The 96-entry/64 MiB budget retains 96 entries / 20,933,904 estimated bytes after depth
edits, including private reflected geometry/history (not RSS). Same-run depth edits for the
linear pattern are 6.318/6.428/6.491 ms median/p95/max; circular pattern 8.202/8.415/8.452 ms.
All parameter-edit samples across the seventeen-part catalogue stay below 100 ms in this
local run; the largest is the toroidal seal at 77.652 ms (median 75.580 / p95 77.267).
These are observations, not universal bounds or an optimization claim. Timing includes
parsing, schema/parameter/input evaluation, references, hashing, geometry, validity and
naming; serialization, IPC and tessellation remain excluded. Remote CI/review and full
Phase 2 sign-off remain pending.

The first remote #135 build exposed a cached-install regression: the packager read OCCT
notices from the untracked source checkout, absent when CI restores only the install.
It now reads the notices already installed in `share/doc/opencascade`. A source-free
packager regression fails first with the same missing-file error, then repackages and
executes both relocated hosts successfully. An isolated checkout using the cached OCCT
install passes the full gate again: 724/724 (133.97 s), schema and hygiene included.

### #137 — bounded app client and immutable suppression recovery (2026-10-19)

The Foundation-only `KernelClient` compiles alongside the shared protocol, linking no
native worker/kernel. Main-actor request and connection tokens arbitrate replies, transport
failures, interruptions, invalidations, cancellation and deadlines. One logical rebuild is
admitted; late/duplicate callbacks cannot settle a newer continuation. Cancellation,
shutdown, protocol errors and the seven-second backstop retire the connection. The next
request sends complete IR. Reply manifest/mesh byte limits, buffer strides and zero partial
error geometry are checked off the main actor before publication.

A timeout naming a valid authored feature retries exactly once on a fresh connection,
suppressing only that feature in an ephemeral copy. Source bytes remain unchanged, and
the returned response retains the original culprit diagnostic even when recovery succeeds.
Invalid/absent/Boolean indices do not cause arbitrary edits or repeated retries.

Tests first fail on missing client/transport types. Sixteen controlled checks (eight normal,
eight Swift TSAN; first focused run 17.78 s) cover admission, warm reuse, closure, missing
callbacks, deadline retirement, cancellation before/during requests, shutdown, stale events,
single successful/failed suppression replay, invalid indices, malformed/partial replies
and 100 four-way concurrent reply/error races. A real embedded service test covers exact
geometry, owned buffers, cold/warm caches and complete-IR replay after SIGKILL.

That real-process test initially traps in Foundation's background error callback: the
class's imported callback lacks Sendable and Swift infers main-actor isolation. Explicit
`@Sendable` types on every XPC event/error callback fix the executor assertion. The corrected
integration test passes in 10.60 s; the final gate's same test passes in 10.27 s, including
observed relaunch delay. Individual failed requests still respect the seven-second client
backstop; the test allows later complete-IR attempts rather than extending that deadline.

`tools/check.sh --preset occt` passes 741/741 (159.60 s), plus 202 schema cases, twenty
reference schemas and license/layering/format checks. Swift 6 complete strict concurrency,
warnings as errors and strict swift-format lint pass; no sanitizer diagnostics. Logs:
`/tmp/nightfall-137-{red-build,build,client,integration,check}.log`. This includes the #135
cached-install fix and source-free packaging regression. Remote CI/review remain pending;
the production Metal viewport/native interface and large-mesh shared memory follow. The
kernel/client infrastructure alone is not a visual MVP or a full Phase 2 sign-off.

### #138 — production Metal viewport (2026-10-19)

`DisplayMesh` bounds and validates indexed buffers, finite positions/normals and indices,
normalizing display positions in Double before packing Float32 off the main actor. Exact
measurements remain in the manifest. The kernel-free Metal renderer uses indexed drawing,
depth testing, outward-normal lighting and orthographic orbit/pan/zoom/fit/presets.
`OrbitView` draws on changes; the app shell follows in #140 under Accepted RFCs 0003/0007.

Tests first fail for missing renderer types. Malformed meshes are rejected; twenty real
service-built reference parts pass existing exact/mesh checks, convert and render through
the same Metal pipeline. GPU completion, nonblank coverage and changed preset/zoom pixels
are checked. The focused render test passes in 1.42 s. Actual 960x720 PNGs of the mounting
plate and enclosure are visually inspected: correct through-holes, open cavity, shading
and fitted geometry. Artifacts: `build/occt/renders/`. GPU checks explicitly skip with code
77 on hosts with no Metal device; this Mac's complete rendering run passes without a skip.

The first full gate exposes a restart-test assumption: an abandoned request can reach the
relaunched service and warm its cache, and the next request can legitimately return busy.
The test now permits bounded explicit attempts and uses document closure for an observable
cold boundary, retaining the seven-second backstop. The corrected focused replay passes
in 10.73 s. No production client or kernel policy is weakened.

The final `tools/check.sh --preset occt` passes 742/742 (155.19 s), 202 schema cases, twenty
reference schemas and hygiene. Swift 6 complete strict concurrency/warnings and strict
swift-format lint pass; existing client TSAN checks remain green. Logs:
`/tmp/nightfall-138-{red-build,build,render,replay,check}.log`. CI/review remain pending.
The renderer/PNGs alone are not the requested visual MVP; the native interface and actual
window validation follow immediately in #140. Phase gates and distribution policy stay open.

### #142 — editable native document model (2026-10-19)

Split from #140 at the document/window boundary to keep both PRs below 400 non-test lines.
The kernel-free model preserves authored source through immutable named-expression edits,
undo/redo and explicit Save As. Opening another document warns before discarding edits.
Revision tokens and ordered cancellation prevent older rebuilds from replacing newer edits;
a busy service gets one bounded retry. Failed builds retain clearly marked last-valid
geometry and exact measures. Duplicate malformed parameter names do not trap the UI.

Actual XPC tests verify 4→8 mm doubles plate volume within 1e-5, 0 mm errors retain the
same mesh, undo/redo restore geometry, rapid 6→10 mm edits publish only the newest state,
edited source round-trips and the input file stays byte-identical. Focused check: 0.60 s.
`tools/check.sh --preset occt` passes 743/743 (163.57 s), schema and hygiene included;
strict Swift 6 concurrency/warnings and swift-format lint pass. Logs:
`/tmp/nightfall-{140-preview,142-check}.log`. The native #140 window is already built and
being visually checked: actual parameter/error/Undo interactions match these assertions.
This document-only increment is not the MVP announcement; window packaging/QA follows.

### #140 — first native visual MVP development preview (2026-10-19)

`build/occt/app/Nightfall.app` now provides the SwiftUI window over the production XPC
kernel/client and Metal viewport, under Accepted RFCs 0003/0007. It includes twenty example
parts, native Open/Save copy, named expression edits, undo/redo, feature history, exact
measurements and readable diagnostics. View presets and orbit/pan/zoom/fit expose the real
solid. Face-boundary lines make cavities and holes readable without triangulation diagonals;
they are transient display data derived off the main actor. A new document clears old GPU
geometry, while an invalid edit retains explicitly marked last-valid geometry/measures.

The actual window is opened and visually inspected, including the final outlined plate.
Native controls verify thickness 4→8 mm changes exact volume 9147.61→18295.22 mm³; 0 mm
reports the parameter bound while retaining geometry, and Undo restores the valid part.
Save copy writes the edited 8 mm source, which the CLI exports as exact STEP. Native Open
loads the enclosure (16788 mm³ / 11792 mm²); the Top preset changes the real viewport.
Manual pointer dragging could not be driven by the UI automation server (noWindowsAvailable);
camera preset/zoom pixel-change checks pass on Metal, without claiming a manual drag test.

The app bundle contains the isolated service and 26 service-local OCCT dylibs, with ad-hoc
signatures verified deeply/strictly. It targets arm64/macOS 15.0. The 17 MiB development ZIP
`out/Nightfall-0.0.1-preview-arm64-macos.zip` extracts with valid signatures; it is not a tagged release.
The missing app target and missing edge-buffer tests fail first; final focused GPU/document
checks pass 2/2 (1.69 s). The complete `tools/check.sh --preset occt` passes 743/743 (154.44 s),
202 schema cases, twenty reference schemas and license/layering/format checks. Strict Swift 6
concurrency/warnings and swift-format lint pass. Logs: `/tmp/nightfall-140-{build,visual,check}.log`.
The bundle/client/renderer PRs #136/#139/#141 have green remote CI; human review remains pending.

This is the first runnable visual MVP for existing part programs and parameter variants.
Interactive sketch/feature authoring, in-app AI tools, assemblies, collaboration, physics
simulation and shared-memory transfer remain open. The full Phase 2 gate is not signed off;
formal releases still require reviewed work on main and the distribution release process.

### #145 — native feature suppression and inclusive rollback (2026-10-19)

The history now offers per-feature enable/suppression and rollback through a selected step,
plus Restore for the complete history. Suppressed and deferred rows are marked. These are
immutable edits to existing RFC 0005 fields, using the same revision-guarded rebuild,
undo/redo and Save copy path as parameter edits. Accepted RFCs 0003/0007 cover integration.

The real history test first fails because a valid no-solid construction state is returned
as a kernel error. The C bridge required a live solid even when viewing construction history;
removing that guard allows valid empty, sketch-only, suppressed and rolled-back documents
to return their feature/cache manifest and empty buffers. The header documents this result.
Feature/dependency failures still return diagnostics and zero geometry; solid operation
validity rules are unchanged. The document model recognizes a successful empty result and
clears old mesh/measures. Its window explains the empty state and exposes history actions
as individual accessible buttons in a scrolling stack.

Tests fail first on missing history APIs, then on actual XPC/C-ABI empty-state behavior.
Focused bridge + native document checks pass 16/16 (2.04 s). Tests cover immutable edits,
unknown IDs, inclusive endpoints/restoration, suppression, empty states, dependency-error
retention, undo/redo, latest-only rapid toggles and unchanged input bytes. Local C-bridge
ASan/UBSan checks pass 11 cases / 65,392 assertions, with no diagnostics; Nightfall is
instrumented while pinned OCCT remains a Release shared-library build. The final complete
gate passes 745/745 (154.00 s), 202 schema cases, twenty reference schemas and hygiene;
strict Swift 6 concurrency/warnings and swift-format lint pass. Log: `/tmp/nightfall-145-check.log`.

Actual native buttons verify suppression clears mesh/measures, Undo restores the plate,
rollback through the sketch shows its marker and no solids, Restore returns 9147.61 mm³,
and rollback through the extrusion includes it. Suppressing the required sketch reports
its dependency error and retains marked geometry. Undo returns to the initial source/state.
The final window and empty state are visually inspected. Development build 0.0.2 is packaged
as `out/Nightfall-0.0.2-preview-arm64-macos.zip`; the original 0.0.1 archive is preserved.
The preceding MVP PR #144 now has all three remote CI groups green. This increment's CI,
all human reviews, formal distribution and full phase sign-off remain pending.

### #149 — atomic pending parameters and Save copy (2026-10-19)

Return and Apply parameters now apply all changed inspector expressions as one immutable
revision and undo step. Suppression/rollback include those edits; Undo preserves unapplied
text for Redo, and Redo cannot discard a newer draft. Unchanged values retain their JSON
types, no-op rebuilds preserve source bytes/history, and unknown names reject the whole batch.
Save copy writes the candidate atomically before changing history or its saved baseline;
failed writes retain source, pending text and dirty state. Authored invalid geometry remains
saveable, with diagnostics and marked last-valid geometry. Opening another part also guards
unsubmitted text. The window distinguishes unapplied parameters from applied geometry.

The real XPC regression first fails with `Rebuild discarded visible parameter edits`.
Focused draft/preview/history checks pass 3/3 (0.94 s), covering dependent length expressions,
exact volume within 1e-5 mm³, single-step undo/redo, saved-baseline round trips, failed writes,
invalid authored source, history actions and byte-identical inputs. The full gate passes
746/746 (153.90 s), 202 schema cases, twenty references and hygiene; strict Swift 6
concurrency/warnings, strict swift-format and client TSAN checks pass. Logs:
`/tmp/nightfall-149-{red-test,focused,check}.log`.

Native window checks verify the pending badge, Apply without Return (9147.61→18295.22 mm³),
Undo, and an unsaved-open warning whose Keep editing action preserves raw text. The extracted
archive launches and renders the real solid. Screenshots are inspected in the native UI
session. Save-dialog completion/cancellation observation remains incomplete: the automation
surface shows a disabled Save action and times out after cancellation; direct file save and
failure paths pass integration tests. No successful native Save-dialog claim is made here.
Preview 0.0.3 is packaged in `out/Nightfall-0.0.3-preview-arm64-macos.zip` (arm64/macOS 15+),
with strict/deep ad-hoc signatures, twenty examples and 26 service-local OCCT dylibs verified
after extraction. Prior previews are preserved. PR #146 now has all three remote CI groups
green; this increment's CI, human review, formal distribution and phase sign-off remain open.

### #151 — seeded headless engine edit/failure/replay validation (2026-10-19)

The kernel/evaluator is exercised directly, independently of the native client. Twelve fixed
seeds produce 72 annular extrusion/pattern/mirror/placement edit states, 72 equivalent mm/cm
variants and 144 reverse/forward source replays. Every state checks analytic volume/area,
centroid/bounds, solid/face counts and complete current face-name ownership through warm
cache, invalid documents/features, recovery, eviction and a fresh evaluator. All earlier
results remain usable and unchanged. Suppression and inclusive rollback are interleaved.
Mass tolerance is 1e-6 mm³/mm²; centroid/bounds tolerance is 1e-6 mm. The initial oracle
incorrectly assumed four faces for the un-unified extrusion; it now distinguishes its ten
analytic span faces from the four unified faces per annulus after a union. No runtime defect
or kernel fix is claimed from that test-oracle correction.

The scenario contract draws on SolidCraft's
[`atomic_commands.rs` at 4c30d944](https://github.com/tangency-labs/solidcraft/blob/4c30d944f54113b75c3e2f06d343146a01118015/crates/engine/tests/atomic_commands.rs)
(MIT OR Apache-2.0). Fixtures and analytic C++ oracles are original; no Rust dependency,
source port or new transaction/API surface is introduced. This adapts the reference to
Nightfall's immutable IR/evaluation contract under Accepted RFCs 0003/0005/0007.

A second headless check retains final geometry after evaluator/backend teardown, exports
exact AP242 STEP, reloads valid solids with cylinder surfaces and verifies volume/area within
1e-5. It checks centroid within 1e-6 mm, current mesh face owners and outward mesh volume
within 1% of the analytic solid. Focused Release checks pass 2/2 (7.79 s). ASan/UBSan passes
2 cases / 164,210 assertions without diagnostics; Nightfall is instrumented, pinned OCCT
remains a Release shared library. The full gate passes 748/748 (164.11 s), 202 schema cases,
twenty reference schemas and hygiene. Remote CI/human review remain pending.
Logs: `/tmp/nightfall-151-{focused,asan,check}.log`. This adds production-engine validation;
production readiness, SMLib #15 deferral, USD interchange work and phase sign-off remain open.
The visual preview 0.0.3 archive/source is preserved unchanged.

### #153 — retained native document sheets (2026-10-19)

Open, Save copy and unsaved-change confirmation now use asynchronous sheets attached to
the visible document window. The document retains its sheet until completion; a token and
document ID reject duplicate, replaced-document and late callbacks. Shutdown clears that
state before cancelling the owned sheet, including reentrant completion. File actions are
disabled during presentation. The window is activated before presenting, including actions
invoked through accessibility while no main window is registered. No nested runModal loop
remains. Existing atomic file writing, authored source and kernel boundaries are unchanged.

The old host sample shows NSSavePanel.runModal nested inside a SwiftUI accessibility action.
Initially disabled Save/New Folder indications also reproduce in stock TextEdit, so that
observation alone does not establish a Nightfall-specific file-writing failure. Final native
checks in the new app successfully save an unsubmitted 8 mm expression to a 1960-byte
`.nightfall` file, return to a clean window, and reopen it with exact volume 18295.22 mm³
and area 6776.99 mm². The original example remains 4 mm. A subsequent raw 10 mm edit survives
Save cancellation and the unsaved-change sheet's Keep editing action; applied 8 mm geometry
remains unchanged. Undo then restores the clean saved baseline. Native screenshots are
visually inspected in the computer-use session.

Focused sheet/draft/preview/history checks pass 4/4 (1.03 s), including stale/duplicate
completion, document replacement, reentrant cancellation and idempotent shutdown. The full
gate passes 749/749 (164.50 s), 202 schema corpus cases, twenty reference schemas and
license/layering/format checks. Strict Swift 6 concurrency, warnings as errors, strict
swift-format lint and client ThreadSanitizer checks pass. Logs:
`/tmp/nightfall-153-{focused,check}.log`, `/tmp/nightfall-153-save-open-sample.txt`.
The app version advances to development preview 0.0.4; prior previews remain preserved.
Remote CI, human review, formal distribution and the full Phase 2 sign-off remain pending.

### #155 — production constant-radius fillet backend (2026-10-19)

Accepted RFC 0007's remaining v1 operation coverage now supplies typed `IKernel::fillet`,
its capability and naming/configuration identity. The OCCT backend deep-copies input geometry,
deduplicates target-owned edges, propagates tangent contours and validates complete closed
solids. Invalid radii/handles, failed construction and incomplete provenance return errors.
There is no radius reduction, partial result or mesh approximation. The service evaluator's
v1 fillet execution remains a separate follow-up; this increment exposes the engine operation.

OCCT's generated blend faces and shared input vertices establish ancestry for spherical
junctions. Each junction descends from its incident contour edges; new boundary rails inherit
those generated faces' ancestry. Explicit modified history and exact surviving subshape
identity preserve existing edges. Every output face and edge must be covered. An unblended
input edge that disappears without modified history is rejected conservatively. There is
no nearest-point name assignment. SolidCraft's
[`blend.rs`](https://github.com/tangency-labs/solidcraft/blob/4c30d944f54113b75c3e2f06d343146a01118015/crates/kernel/src/blend.rs)
and [`provenance.rs`](https://github.com/tangency-labs/solidcraft/blob/4c30d944f54113b75c3e2f06d343146a01118015/crates/kernel/src/provenance.rs)
inform the blend/origin contract; this is original OCCT integration, with no Rust source port
or dependency. The reference's mesh-nearest blend attribution is not used for Nightfall names.

Tests first fail for the missing typed API. Six focused cases pass (0.94 s): single/four/all
box edges, radii 0.125/1/2/4 mm, rigid placement, complete generated/modified/deleted ownership,
partial annular-rim picks propagating to whole tangent contours, invalid/forged handles,
failed-operation immutability and retention/export after facade/source teardown. Volume/area
tolerances are 1e-6 mm³/mm² (1e-5 for spherical/toroidal cases), centroid tolerance 1e-8 mm.
The single r2 box measures 5974.24777961 mm³ / 2172.53096491 mm²; four r2 rounds measure
5896.99111843 mm³ / 2090.12385966 mm². The all-edge r1 box measures 5949.83479350 mm³ /
2095.85837720 mm², with 26 faces, 48 ordinary and eight degenerate spherical-pole edges.
The initial 48-total-edge test oracle is corrected by explicitly counting those pole edges;
no geometry failure or relaxed mass tolerance is claimed. Each spherical patch has exactly
three incident input-edge ancestors. Annular rims measure 7048.35772398 mm³ / 3258.22079307 mm².
ASan/UBSan passes six cases / 3,943 assertions without diagnostics; Nightfall code is
instrumented while the pinned OCCT libraries remain Release. The full gate passes 755/755
(170.70 s), 202 schema corpus cases, twenty reference schemas and hygiene. Strict Swift
concurrency/warnings and existing native worker/client concurrency tests also pass.
Logs: `/tmp/nightfall-155-{red-build,initial-focused,focused,asan,check}.log`.
Remote CI, human review, full production/phase sign-off and evaluator/UI integration remain
open; the packaged 0.0.4 preview remains preserved at its original source commit.

### #157 — document fillets and native preview 0.0.5 (2026-10-19)

Existing v1 fillet features execute through the production evaluator and typed backend.
Fillets share chamfer's recursive edge unions/exclusions, qualified source ancestry, exact
cardinality, current body/dependency binding, atomic multi-body publication and immutable
cache naming. Radius validation reports the authored field; backend failure retires intended
targets while independent work continues. Evaluator identity advances to 27. Accepted RFCs
0005/0007 cover this existing operation; no schema/dependency/process change. The unsupported
modifier test now uses shell, which remains explicitly unimplemented.

New tests first fail at `feature is not implemented: fillet`. Nine evaluator cases cover
analytic radii, warm/cold/evicted caches, renamed producers, mm/cm equivalence, unmodified
excluded-body dependencies, invalid radii/selectors, injected missing/foreign history,
suppression/rollback, multi-body atomic failure/recovery and downstream placed cap attachment.
Retained solids survive facade teardown, reload from exact AP242 STEP within 1e-5 mm³/mm²,
and produce outward indexed meshes within 1% of analytic volume, with current face owners.
Geometry checks use 1e-6 mm³/mm² and 1e-8 mm centroid tolerances. Focused evaluator/CLI/XPC
checks pass 11/11 (1.58 s). ASan/UBSan passes nine evaluator cases / 585 assertions and one
CLI case / 59 assertions without diagnostics; OCCT shared libraries remain Release.

`examples/modeling/rounded_plate.nightfall` adds a 60×40 mm four-hole plate with editable
thickness and exact corner radius. The twenty established reference baselines are unchanged;
the native bundle contains this additional example (21 total), selected as Rounded Plate.
Default thickness 8 mm/radius 6 mm measures 18048 mm³ / (5728 + 288π) mm². Radius 3 mm
measures (18912 - 216π) mm³ / (6136 + 186π) mm². CLI tests export/reload both variants,
preserve an existing output on invalid override and prove byte-identical source input.
The real XPC/client test checks edits, Undo, suppression, rollback, marked last-valid geometry
after a failed edit and recovery. The native window visually confirms rounded corners,
6→3 mm Apply (18048.00→18233.42 mm³), Undo, sharp corners after suppression/rollback and
restoration. Native Save accepts an unapplied 3 mm draft and reopens its 2701-byte document
with all three original features and identical exact measures; the example remains 6 mm.
Screenshots of both radii are displayed and inspected in the native UI session.

The development app advances to 0.0.5; prior preview archives remain preserved. The full gate
passes 766/766 (165.67 s), 202 schema cases, twenty reference schemas and hygiene. Strict Swift
format/concurrency/warnings and existing native/client ThreadSanitizer checks pass. Preview
`out/Nightfall-0.0.5-preview-arm64-macos.zip` is extracted into a path containing spaces;
strict/deep signatures, runtime dependencies, 26 service-local OCCT dylibs and 21 examples
are verified. The extracted app launches and renders the rounded plate with the same measures.
Logs: `/tmp/nightfall-157-{red-test,focused,native,asan,cli-asan,check}.log`; focused CTest's
11/11 result is recorded in the tool session. PRs #154 and #156 now have all remote CI groups
green, including both-kernel/USD and SMLib checks. Human review, formal distribution,
assemblies, collaboration, physics and full production/phase sign-off remain open.

### #159 — chained, concave and hole-adjacent fillet validation (2026-10-19)

Three additional production-kernel regressions exercise difficult combinations under Accepted
RFC 0007. This is validation of the existing implementation; no runtime defect/fix, API,
dependency, schema or process change is claimed. Every stage reuses complete output-oriented
face/edge coverage and immutable input checks from #155, including all retained prior stages.

Sequential r2 vertical corners and r1 top/bottom rims on a 10×20×30 mm prism match independent
inner-parallel/Steiner formulas. For cross-section A = 200 - 4(4 - π) and perimeter P = 44 + 4π,
each rim removes volume P(1 - π/4) - 2π(5/6 - π/4) and changes area by
(π/2 - 2)P + 3π - π². The top stage measures 5885.15305696 mm³ / 2065.40053917 mm²;
both rims measure 5873.31499549 mm³ / 2040.67721868 mm² with centroid (5,10,15) mm.
The intermediate/final solids have 18/26 faces, with complete current ancestry at each step.

A reflex corner in a 64 mm² L-profile adds area (1 - π/4)r² and has independently integrated
centroid/first moments. Radii 0.125/1/2/4 mm pass; r1 at 10 mm extrusion measures
642.14601837 mm³ / 524.13716694 mm². A holed 40×20×10 mm block, with a radius3 hole at
(20,14.5), adapts the geometry of SolidCraft's
[`issue_324_tests.rs` at 4c30d944](https://github.com/tangency-labs/solidcraft/blob/4c30d944f54113b75c3e2f06d343146a01118015/crates/kernel/src/issue_324_tests.rs)
(MIT OR Apache-2.0), using original exact C++ oracles. Edge radii 1/2/2.4 mm preserve all four
analytic hole-wall spans; the 2.4 mm case leaves 0.1 mm clearance and measures
7667.81239802 mm³ / 2888.27112567 mm². Failed r100 preserves input and allows subsequent
valid use. No automatic radius reduction, nearest/mesh naming or Rust dependency is added.

Chained/toroidal and hole-adjacent mass tolerances are 1e-5 mm³/mm²; concave mass tolerance
is 1e-6, centroid 1e-8 mm. The expanded focused suite passes nine cases / 6,871 assertions;
Release CTest passes 9/9 (1.48 s). ASan/UBSan passes all nine cases / 6,871 assertions without
diagnostics; Nightfall is instrumented while pinned OCCT shared libraries remain Release.
The full gate passes 769/769 (169.82 s), 202 schema cases, twenty reference schemas and
hygiene, including strict Swift/native/client concurrency tests. Preview0.0.5 remains
preserved at source06a5cc4; this test-only branch does not replace its archive.
Logs: `/tmp/nightfall-159-{ctest,focused,asan,check}.log`. CI, human review and full
production/phase sign-off remain pending.

### Face-attached sketch rebuild evidence

2026-10-18, Apple M3 Pro / 12 cores / 36 GiB, Release OCCT 7.9.3, 30 samples per mode.
The new 30-feature part has two sketches, two extrusions and 26 transforms. The second
sketch selects the first extrusion's current `cap_end` face; its extrusion creates the
measured 10×20 mm plate from z = D to 2D. Every sample checks volume `200D` mm³ / area
`400 + 60D` mm² within 1e-5, centroid x (including translations) and z = 1.5D within
1e-6 mm, and exact kernel/cache counts. D is depth in mm. Run
`build/occt/services/kernel/nf_evaluator_bench` after builds/tests. All 30 samples of every
mode pass on all eighteen parts (2160 measured rebuilds).

| Face-attached plate | Median ms | p95 ms | Max ms | Kernel calls / cache hits |
| --- | ---: | ---: | ---: | ---: |
| Cold | 2.712 | 2.795 | 3.005 | 29 / 0 |
| Host depth + attached depth edit | 2.911 | 2.953 | 3.021 | 29 / 1 |
| Last transform edit | 0.642 | 0.661 | 0.668 | 2 / 29 |
| Unused parameter edit | 0.616 | 0.627 | 0.639 | 1 / 30 |

Every evaluation performs one plane query, including cache-hit cases. Cold/depth samples
also perform 28 modeling calls; the last-transform edit performs one modeling call, and
the unused edit performs none. The 96-entry/64 MiB budget retains 96 entries / 10,412,368
estimated bytes after depth edits, including source snapshots retained by attached sketches
(not RSS). Same-run depth edits: linear pattern 6.785/6.975/7.119 ms median/p95/max,
circular pattern 8.809/9.102/9.140 ms and mirror 5.867/6.260/6.989 ms. All parameter-edit
samples across the eighteen-part catalogue stay below 100 ms in this local run; the largest
is the toroidal seal at 98.014 ms (median 79.130 / p95 87.080). These are observations,
not universal bounds or an optimization claim. Timing includes parsing, schema/parameter/
input evaluation, current provenance and plane queries, hashing, geometry, validity and
naming; serialization, IPC and tessellation remain excluded. Remote CI/review and full
Phase 2 sign-off remain pending.

### Filtered body-face rebuild evidence

2026-10-18, Apple M3 Pro / 12 cores / 36 GiB, Release OCCT 7.9.3, 30 samples per mode.
The new 30-feature part has two sketches, two extrusions and 26 transforms. Its attachment
selects `faces_of: support` with planar type, signed +z normal, parallel-to-x and
perpendicular-to-z filters. The measured 10×20 mm plate spans z = D to 2D. Every sample
checks volume `200D` mm³ / area `400 + 60D` mm² within 1e-5, centroid x (including
translations) and z = 1.5D within 1e-6 mm, plus exact kernel/cache counts. D is depth in mm.
Run `build/occt/services/kernel/nf_evaluator_bench` after builds/tests. All 30 samples of
every mode on all nineteen parts pass (2280 measured rebuilds).

- Cold: median 2.572 ms / p95 2.596 / max 2.613; 29 kernel calls / 0 cache hits.
- Host depth + attached depth edit: 2.910 / 3.029 / 3.161 ms; 29 calls / 1 hit.
- Last transform edit: 0.658 / 0.700 / 0.716 ms; 2 calls / 29 hits.
- Unused parameter edit: 0.627 / 0.684 / 0.701 ms; 1 call / 30 hits.

Every evaluation performs one actual all-face enumeration, including cache-hit cases.
Cold/depth samples also perform 28 modeling calls; last-transform edits perform one, and
unused edits none. The 96-entry/64 MiB budget retains 96 entries / 10,412,368 estimated
bytes after depth edits, including source snapshots retained by attached sketches, not RSS.
All catalogue parameter-edit samples stay below 100 ms in this local run; the largest is
the toroidal seal at 84.102 ms (median 76.414 / p95 79.709). These are observations, not
universal bounds or an optimization claim. Timing includes parsing, schema/parameter/input
evaluation, current references/provenance, face enumeration/filtering, hashing, exact
geometry, validity and naming; serialization, IPC and tessellation remain excluded.
Remote CI/review and full Phase 2 sign-off remain pending.

### Nearest-face attachment rebuild evidence

2026-10-18, Apple M3 Pro / 12 cores / 36 GiB, Release OCCT 7.9.3, 30 samples per mode.
The new 30-feature part has two sketches, two extrusions, a two-instance linear pattern and
25 transforms. The support contains two separated 10×20 mm caps, spaced 20 mm along x;
the attached sketch filters +z normals and chooses the cap nearest (25,10,D+1) mm.
Its measured 10×20 mm plate spans z = D to 2D. Every sample checks volume `200D` mm³ /
area `400 + 60D` mm² within 1e-5, centroid x (including translations) and z = 1.5D
within 1e-6 mm, plus exact kernel/cache counts. D is depth in mm. Run
`build/occt/services/kernel/nf_evaluator_bench` after builds/tests. All 30 samples of
every mode on all twenty benchmark parts pass (2400 measured rebuilds).

- Cold: median 4.882 ms / p95 4.992 / max 5.038; 32 kernel calls / 0 cache hits.
- Host depth + attached depth edit: 5.028 / 5.164 / 5.167 ms; 32 calls / 1 hit.
- Last transform edit: 0.677 / 0.711 / 0.721 ms; 4 calls / 29 hits.
- Unused parameter edit: 0.652 / 0.683 / 0.696 ms; 3 calls / 30 hits.

Every evaluation performs one actual enumeration and two trimmed-face distance queries,
including complete cache hits. Cold/depth samples also perform 29 modeling calls;
last-transform edits perform one modeling call, and unused edits none. The 96-entry/
64 MiB budget retains 96 entries / 11,251,936 estimated bytes after depth edits, including
source snapshots retained by attached sketches, not RSS. All catalogue parameter-edit
samples stay below 100 ms in this local run; largest is the toroidal seal at 87.868 ms
(median 76.356 / p95 87.277). These are observations, not universal bounds or an optimization
claim. Timing includes parsing, schema/parameter/input evaluation, current references/
provenance, enumeration/filtering/distance queries, hashing, exact geometry, validity and
naming; serialization, IPC and tessellation remain excluded. Remote CI/review and full
Phase 2 sign-off remain pending.

### Composed face-selector rebuild evidence

2026-10-18, Apple M3 Pro / 12 cores / 36 GiB, Release OCCT 7.9.3, 30 samples per mode.
The new 30-feature part has two sketches, two extrusions, a two-instance linear pattern
and 25 transforms. The attachment unions generated caps with filtered support faces,
excludes the cap nearest (5,10,D+1) mm, then chooses the nearest remaining +z cap. This
selects the right-hand cap even though the point is closest to the excluded left cap.
The measured 10×20 mm plate spans z = D to 2D. Every sample checks volume `200D` mm³ /
area `400 + 60D` mm² within 1e-5, centroid x (including translations) and z = 1.5D
within 1e-6 mm, plus exact kernel/cache counts. D is depth in mm. Run
`build/occt/services/kernel/nf_evaluator_bench` after builds/tests. All 30 samples of
every mode on all twenty-one benchmark parts pass (2520 measured rebuilds).

- Cold: median 4.866 ms / p95 4.976 / max 5.010; 33 kernel calls / 0 cache hits.
- Host depth + attached depth edit: 5.066 / 5.169 / 5.253 ms; 33 calls / 1 hit.
- Last transform edit: 0.713 / 0.744 / 0.745 ms; 5 calls / 29 hits.
- Unused parameter edit: 0.695 / 0.733 / 0.842 ms; 4 calls / 30 hits.

Every evaluation performs one shared all-face enumeration and three actual trimmed-face
distance queries (two for exclusion, one for the remaining face), including complete
cache hits. Cold/depth samples add 29 modeling calls; last-transform edits add one, and
unused edits none. The 96-entry/64 MiB budget retains 96 entries / 11,891,008 estimated
bytes after depth edits, including all referenced source bindings (shared geometry may
be charged multiple times), not RSS. All catalogue parameter-edit samples stay below
100 ms in this local run; largest is the toroidal seal at 76.262 ms (median 75.190 /
p95 76.076). These are observations, not universal bounds or an optimization claim.
Timing includes parsing, schema/parameter/input evaluation, recursive set/cardinality
resolution, current references/provenance, enumeration/filtering/distance queries, hashing,
exact geometry, validity and naming; serialization, IPC and tessellation remain excluded.
Remote CI/review and full Phase 2 sign-off remain pending.

### Selected mirror-plane rebuild evidence

2026-10-18, Apple M3 Pro / 12 cores / 36 GiB, Release OCCT 7.9.3, 30 samples per mode.
The new 30-feature part has one sketch, two extrusions, a mirror and 26 transforms. A
2×2×D mm seed mirrors across the cap at z = 2D of a separate reference body. The selector
unions generated caps with filtered body faces, excludes the starting cap, then selects the
nearest remaining face. Reference bindings never add reflection targets. Each sample checks
the mirrored pair's volume `8D` mm³ / area `16 + 16D` mm² within 1e-5 and centroid x
(including translations) and z = 2D within 1e-6 mm, plus exact kernel/cache counts. D is
depth in mm. Run `build/occt/services/kernel/nf_evaluator_bench` after builds/tests.
All 30 samples of every mode on all twenty-two parts pass (2640 measured rebuilds).

- Cold: median 6.058 ms / p95 7.891 / max 9.434; 32 kernel calls / 0 cache hits.
- Seed + reference depth edit: 6.677 / 7.390 / 7.550 ms; 32 calls / 1 hit.
- Last transform edit: 0.806 / 0.931 / 0.965 ms; 3 calls / 29 hits.
- Unused parameter edit: 0.734 / 0.865 / 0.887 ms; 2 calls / 30 hits.

Every evaluation performs one shared face enumeration and one trimmed-face distance query,
including complete cache hits. Cold/depth samples add 30 modeling calls; last-transform
edits add one, and unused edits none. The 96-entry/64 MiB cache budget retains 96 entries /
20,936,832 estimated bytes after depth edits. Estimates include selector-source bindings;
shared snapshots may be charged repeatedly, and this is not RSS. All catalogue parameter-edit
samples stay below 100 ms in this local run; largest is the toroidal seal at 78.340 ms
(median 76.306 / p95 77.952). These are observations, not universal bounds or an optimization
claim. Timing includes parsing, schema/parameter/input evaluation, current references,
recursive selector/cardinality resolution, enumeration/distance queries, hashing, exact
reflection/union, validation and naming. Serialization, IPC and tessellation remain excluded.
Remote CI/review and full Phase 2 sign-off remain pending.

### Headless CLI export and rebuild evidence

2026-10-18, Apple M3 Pro / 12 cores / 36 GiB, Release OCCT 7.9.3. The CLI tests build
and reload all twenty reference STEP files, check validity and compare volume/area within
1e-5 and centroid within 1e-6 mm to the independently verified evaluator results. A separate
`NF_BUILD_TESTS=OFF` build also exports the washer: one exact solid, with a 36,179-byte STEP
artifact at `/tmp/nightfall-94-washer.step`. STEP includes geometry, without Nightfall history.

After all builds/tests, two executions of `nf_evaluator_bench` each cover 22 parts × 4 modes
× 30 samples (5280 measured rebuilds total). Both pass every geometry and execution-count
assertion. The benchmark measures IR decoding/evaluation, selectors, geometry and naming;
it excludes CLI startup, file I/O and STEP encoding, as well as serialization/IPC/tessellation.
The per-evaluator budget remains 96 entries / 64 MiB estimated retained bytes, not RSS.

The first run misses the 100 ms target on toroidal depth edits: median 152.992 ms /
p95 203.727 / max 214.026. Toroidal cold builds measure 77.102 / 129.961 / 146.454 ms.
All other parameter-edit modes/parts stay below 100 ms. A post-run process snapshot shows
concurrent simulator and cloud-sync activity; this is context, not proof of the cause.
The immediate repeat, with unchanged sources, measures toroidal depth edits at
75.340 / 77.719 / 78.030 ms and cold builds at 74.987 / 77.264 / 77.641 ms. All catalogue
parameter-edit samples stay below 100 ms on that repeat. The plate depth edit measures
1.994 / 2.128 / 2.162 ms in the first run; these are local observations, not latency bounds.

The successful repeat does not erase the observed miss. Follow-up under #95: capture
contemporaneous system load/thermal data on a quiet-host run and profile toroidal mass
integration if the overrun reproduces. No modeling algorithm was changed for this CLI issue.
The baseline local criterion retains its measured pass; load sensitivity, remote CI/review
and full Phase 2 sign-off remain pending.

### CLI parameter-variant command and rebuild evidence

2026-10-18, Apple M3 Pro / 12 cores / 36 GiB, Release OCCT 7.9.3. After builds/tests,
30 fresh CLI processes export washer thicknesses from 1 to 30 mm through `--set`. Complete
command time is median 71.104 ms / p95 73.259 / max 133.638, including process startup,
file reading, batch validation, evaluator/modeling, STEP encoding, fsync/rename, JSON result
and process exit. OS caches are not reset. Every sample checks successful exit, clean stderr,
the JSON body/output record, STEP signature/AP242/B-rep markers, absence of tessellation,
and byte-identical source. Geometry round-trip comparisons are covered separately by the
20-case CLI suite, including edits to all twenty analytically tested reference parts.
Command timing is not the evaluator's <100 ms rebuild measurement. The standalone helper
and per-sample data are `/tmp/nightfall-125-cli-bench.py` and
`/tmp/nightfall-125-cli-bench.json`; the 6 mm sample is
`/tmp/nightfall-125-washer-6mm.step` (36,179 bytes).

The unchanged production rebuild catalogue also completes 22 parts × 4 modes × 30 samples
(2640 measured rebuilds), passing every analytic geometry and execution-count assertion.
All parameter-edit samples stay below 100 ms in this run; the largest is toroidal depth
editing at median 75.210 ms / p95 75.991 / max 76.227. Its cold build measures
74.050 / 76.480 / 76.637 ms. The 96-entry/64 MiB estimated cache budget remains unchanged
(not RSS); toroidal depth edits retain 96 entries / 7,221,912 estimated bytes.
This benchmark includes IR decoding, validation/evaluation, selectors, hashing, geometry
and naming, and excludes CLI startup, file I/O, STEP encoding, serialization, IPC and
tessellation. These are local observations, not latency guarantees; the prior loaded-run
miss and #95 load-sensitivity follow-up remain recorded. Remote CI/review and full
Phase 2 sign-off remain pending.

### Sketch-entity face provenance and rebuild evidence

2026-10-18, Apple M3 Pro / 12 cores / 36 GiB, Release, dynamically linked OCCT 7.9.3.
The final focused runs pass 20 evaluator/adapter cases / 462 assertions and 4 math cases /
160 assertions (seed 20261018). Analytic attachments have volume 8π mm³ / area 16π mm²
with centroid x = 11 mm; moving the seed shifts x to 14, and the selected patterned wall
places it at 31. A mirror through the qualified wall has volume 12000 mm³ / area 3200 mm²,
centroid (10,10,15) mm. A reversed rectangular hole retains volume 5040 mm³ / area 2856 mm²
and centroid (5,1744/168,15) mm. The revolved annular source has volume 6000π mm³ /
area 1800π mm², centroid (0,10,0) mm. Comparisons use volume/area tolerance 1e-7 and
centroid component tolerance 1e-8 mm. Other regressions cover exact source sets and failure
pointers/counts, rather than inferring ancestry from geometric proximity.

`tools/check.sh --preset occt` passes 621/621 (45.42 s), including hygiene, format,
202 schema cases and 20 reference documents, with no warnings. The ASan/UBSan selection
now includes math as well as kernel/cache/evaluator/CLI: 504/504 pass (295.73 s), without
diagnostics. Nightfall is instrumented; OCCT remains the pinned Release shared library.
The actual CLI builds `/tmp/nightfall-126-qualified-wall.nightfall` with
`--set 'depth=45 mm'` into `/tmp/nightfall-126-qualified-wall.step` (30,216 bytes), containing
two exact AP242 manifold solids, no tessellation. The source remains at 30 mm. This example
checks successful export/format; existing CLI tests separately prove STEP geometry reloads.

After all builds/tests, the catalogue covers 23 parts × 4 modes × 30 samples: 2760 measured
rebuilds, every analytic geometry and execution-count check passing. The new source-face
part has two sketches, two extrusions and 26 transforms. Its attached rectangular plate
selects `outline/bottom`, has volume 200D mm³ / area 400 + 60D mm² and centroid z = D/2,
with the expected translated x (D is depth in mm). Volume/area tolerance is 1e-5 and
centroid tolerance 1e-6 mm. One current face-frame query executes on every rebuild.

- Cold: median 2.420 ms / p95 2.515 / max 2.525; 29 kernel calls / 0 cache hits.
- Depth edit: 2.563 / 2.677 / 2.745 ms; 29 calls / 1 hit.
- Last transform edit: 0.613 / 0.652 / 0.680 ms; 2 calls / 29 hits.
- Unused parameter edit: 0.590 / 0.625 / 0.626 ms; 1 call / 30 hits.

The unchanged 96-entry/64 MiB estimated cache budget retains 96 entries / 10,423,762 bytes
after source-face depth edits, including retained provenance (not RSS). Every catalogue
parameter-edit sample stays below 100 ms in this run; the largest is toroidal depth editing
at median 72.901 / p95 75.114 / max 75.930 ms. Timing includes parsing, validation,
expressions, current references, selectors, hashing, exact geometry, validation and naming;
it excludes authoring serialization, CLI startup, file I/O, STEP encoding, IPC and tessellation.
These are local observations, not latency guarantees. The earlier loaded-run miss and #95
follow-up remain recorded. Logs: `/tmp/nightfall-126-check.log`,
`/tmp/nightfall-126-asan.log`, `/tmp/nightfall-126-bench.log`; per-mode numbers are in
`/tmp/nightfall-126-bench.json`. Remote CI/review and full Phase 2 sign-off remain pending.

### Selected cylindrical-axis rebuild evidence

2026-10-18, Apple M3 Pro / 12 cores / 36 GiB, Release, dynamically linked OCCT 7.9.3.
Focused runs pass 18 evaluator cases / 898 assertions and 3 kernel cases / 112 assertions
(seed 20261018). A 2×2×30 mm seed rotates about the selected fixture's stored -Z axis to
centroid (3,-10,15) mm, preserving volume 120 mm³ / area 248 mm². The reference remains
volume 160π mm³ / area 168π mm² with centroid (0,0,20). Four circular copies yield four
solids, volume 480 mm³ / area 992 mm², centroid (0,0,15). A full annular revolution has
volume 2250π mm³ / area 1050π mm². Signed quarter sweeps have volume 562.5π mm³ /
area 300+262.5π mm² and centroid (140/(9π), ±140/(9π),15) mm. Volume/area tolerance is
1e-6, centroid components 1e-8 mm. Kernel cylinder radius/direction use 1e-10, axis-line
distance 1e-8 mm; source B-reps are byte-identical after queries/placements. Bore-face
orientation reverses relative to the cutting tool while the parameter axis stays -Z.

`tools/check.sh --preset occt` passes 642/642 (56.32 s), all hygiene/format checks,
202 schema cases and 20 reference documents, without warnings. The ASan/UBSan
kernel/cache/evaluator/CLI selection passes 404/404 (106.13 s), without diagnostics.
Nightfall is instrumented; the pinned OCCT shared library remains Release. The actual CLI
exports `/tmp/nightfall-127-cylinder-pattern.nightfall` with `--set 'depth=45 mm'` into
`/tmp/nightfall-127-cylinder-pattern.step`: 75,042 bytes, five exact AP242 manifold solids
(four patterned blocks and the reference), no tessellation. Source depth remains 30 mm.
This example checks export/format; existing CLI tests independently verify reloaded geometry.

After builds/tests, the benchmark covers 24 parts × 4 modes × 30 samples: 2880 measured
rebuilds, all analytic geometry and execution-count checks passing. The new cylindrical-axis
part has two sketches, two extrusions, one four-copy pattern and 25 transforms. Its target
has volume 16D mm³ / area 32+32D mm² and centroid z = D/2, with the expected translated x
(D is depth in mm). Volume/area tolerance is 1e-5, centroid components 1e-6 mm. One face
enumeration and four trimmed patch-distance queries execute every evaluation, including hits.

- Cold: median 20.069 ms / p95 28.219 / max 54.035; 36 kernel calls / 0 cache hits.
- Depth edit: 13.632 / 20.336 / 21.110 ms; 35 calls / 3 hits.
- Last transform edit: 1.592 / 4.666 / 6.943 ms; 6 calls / 29 hits.
- Unused parameter edit: 1.096 / 1.179 / 1.271 ms; 5 calls / 30 hits.

The 96-entry/64 MiB estimated cache budget retains 96 entries / 40,632,645 bytes after
cylindrical-axis depth edits, including separately charged selector-source snapshots (not RSS).
Every catalogue parameter-edit sample stays below 100 ms in this run; largest is toroidal
depth editing at median 79.770 / p95 82.928 / max 83.113 ms. Timing includes IR parsing,
schema/parameter/input evaluation, current references, recursive selectors, face queries,
hashing, exact modeling, validation and naming. It excludes authoring serialization, CLI
startup, file I/O, STEP encoding, IPC and tessellation. These are local observations, not
latency guarantees; the prior loaded-run miss and #95 follow-up remain recorded.
Logs: `/tmp/nightfall-127-check.log`, `/tmp/nightfall-127-asan.log`,
`/tmp/nightfall-127-bench.log`; per-mode data: `/tmp/nightfall-127-bench.json`.
Remote CI/review and full Phase 2 sign-off remain pending.

### Selected-plane extrusion rebuild evidence

2026-10-18, Apple M3 Pro / 12 cores / 36 GiB, Release, dynamically linked OCCT 7.9.3.
The final focused run passes 22 cases / 976 assertions, seed 20261018. A 10×20 mm profile
terminated at 40 mm has volume 8000 mm³ / area 2800 mm², centroid (5,10,20) mm. Its separate
reference remains volume 160 mm³ / area 328 mm², centroid (101,101,20). The profile lies
outside the reference face's finite trim, explicitly testing supporting-plane extent.
An oblique [1,0,1] ray preserves volume 8000 mm³ with area 1200+1600√2 mm², centroid
(25,10,20). Reversing the ray reaches -20 mm with volume 4000 / area 1600. Annular profile
termination gives volume 3000π / area 1350π. Result union gives volume 11000 / area 3300;
cut/intersection each give volume 3000 / area 1700 with independently checked centroids.
Volume/area tolerance is 1e-6, centroid components 1e-8 mm. Cache/source retention, invalid
frames before filtering/reuse, exact failure pointers and profile-wide tolerance guards
have explicit regressions, including the initially failing near-parallel ray boundary case.

Final `tools/check.sh --preset occt` passes 664/664 (46.18 s), with hygiene, format,
202 schema cases and 20 reference documents, without warnings. The initial pre-boundary-test
gate also passes 663/663 (46.95 s); final evidence includes the added direction regression.
ASan/UBSan kernel/cache/evaluator/CLI passes 426/426 (113.41 s), without diagnostics.
Nightfall is instrumented; the pinned OCCT shared library remains Release. The actual CLI
exports `/tmp/nightfall-128-extrude-to.nightfall` with `--set 'stop_height=55 mm'` into
`/tmp/nightfall-128-extrude-to.step`: 29,354 bytes, two exact AP242 manifold solids,
no tessellation. Source height remains 40 mm. This example checks export/format; existing
CLI tests independently prove reloaded STEP geometry.

After builds/tests, the catalogue covers 25 parts × 4 modes × 30 samples: 3000 measured
rebuilds, every analytic geometry and execution-count check passing. The new stopped plate
has two sketches, two extrusions and 26 transforms. Reference depth D drives the stop plane;
the target has volume 200D mm³ / area 400+60D mm², centroid z = D/2 and the expected
translated x. Volume/area tolerance is 1e-5, centroid components 1e-6 mm. One current
face-frame query executes every evaluation, including complete cache hits.

- Cold: median 2.565 ms / p95 2.664 / max 2.676; 29 kernel calls / 0 cache hits.
- Depth edit: 2.753 / 2.902 / 2.924 ms; 29 calls / 2 hits.
- Last transform edit: 0.627 / 0.656 / 0.658 ms; 2 calls / 29 hits.
- Unused parameter edit: 0.609 / 0.641 / 0.659 ms; 1 call / 30 hits.

The 96-entry/64 MiB estimated cache budget retains 96 entries / 10,650,304 bytes after
stopped-plate depth edits, including separately charged selector sources (not RSS).
Every catalogue parameter-edit sample stays below 100 ms in this run; largest is toroidal
depth editing at median 75.713 / p95 77.755 / max 77.987 ms. Timing includes IR parsing,
schema/parameter/input evaluation, current references, recursive selectors, frame queries,
hashing, exact modeling, validation and naming. It excludes authoring serialization, CLI
startup, file I/O, STEP encoding, IPC and tessellation. These are local observations, not
latency guarantees; the earlier loaded-run miss and #95 follow-up remain recorded.
Logs: `/tmp/nightfall-128-check.log`, `/tmp/nightfall-128-asan.log`,
`/tmp/nightfall-128-bench.log`; per-mode data: `/tmp/nightfall-128-bench.json`.
Remote CI/review and full Phase 2 sign-off remain pending.

### Selected-edge chamfer rebuild evidence

2026-10-18, #129, Apple M3 Pro / 12 cores / 36 GiB, Release, dynamically linked OCCT 7.9.3.
A 10×20×30 mm box with its four top edges chamfered by 1 mm has volume
6000−86/3 mm³, area 2084+56√2 mm², centroid (5,10,(90000−860+29/3)/(6000−86/3)) mm,
ten faces and twenty edges. Volume/area tolerance is 1e-6, centroid components 1e-8 mm.
Chamfering both upper rims of a 30 mm annulus with radii 10/5 mm gives volume 2235π mm³,
area (990+30√2)π mm² within 1e-5. Annular boundaries, rigid placement, complete
face/edge history, generated-face coverage and unchanged serialized inputs are checked.
The five backend and thirteen evaluator cases cover edge-set composition, cardinality,
current names, excluded dependencies, atomic multi-body failures, invalid handles before
cache reuse, renamed producers, suppression, rollback, eviction and retained readers.
Unsupported angle/edge-filter forms and incomplete provenance remain explicit errors.

`tools/check.sh --preset occt` passes 683/683 (53.44 s), including hygiene/format,
202 schema cases and twenty reference documents, without warnings. The initial run passed
681/682; an existing unsupported-filter regression caught queries running before validation.
Restoring early face-filter validation fixes it; both full-run logs are retained. The CLI
regression independently reloads 1/2 mm chamfer variants as valid closed STEP solids with
analytic volume/area tolerance 1e-5 and centroid tolerance 1e-6 mm. Source files stay intact.
The actual CLI exports `/tmp/nightfall-129-chamfer.nightfall` with `--set 'bevel=2 mm'`
to `/tmp/nightfall-129-chamfer.step`: 25,589 bytes, one exact AP242 manifold solid,
no tessellation. Public backend migration adds `edges`, `chamfer` and the capability flag;
rebuild vtable/value-layout consumers. No schema, document or dependency migration.

ASan/UBSan kernel/cache/evaluator/CLI passes 445/445 (117.07 s) without diagnostics.
Nightfall is instrumented; the pinned OCCT shared library remains Release. Logs:
`/tmp/nightfall-129-check.log`, `/tmp/nightfall-129-initial-check.log`,
`/tmp/nightfall-129-asan.log`; initial red tests: `/tmp/nightfall-129-red.log`.

After all builds/tests, the catalogue covers 26 parts × 4 modes × 30 samples: 3120 measured
rebuilds, every analytic geometry and execution-count check passing. The new chamfered plate
has one sketch, one extrusion, one chamfer and 27 transforms. It selects the four vertical
edges by subtracting both cap boundaries from side boundaries. At depth D, its volume is
198D mm³, area 396+(52+4√2)D mm², centroid z = D/2 and the expected translated x.
Volume/area tolerance is 1e-5, centroid components 1e-6 mm. Six boundary queries execute
on every evaluation, including complete cache hits.

- Cold: median 4.699 ms / p95 4.901 / max 4.982; 35 kernel calls / 0 cache hits.
- Depth edit: 4.940 / 5.103 / 5.138 ms; 35 calls / 1 hit.
- Last transform edit: 0.690 / 0.734 / 0.736 ms; 7 calls / 29 hits.
- Unused parameter edit: 0.655 / 0.685 / 0.694 ms; 6 calls / 30 hits.

The 96-entry/64 MiB estimated cache budget retains 96 entries / 19,389,688 bytes after
chamfer depth edits, including separately retained selector sources (not RSS). Every catalogue
parameter-edit sample stays below 100 ms in this run; largest is toroidal depth editing at
median 75.727 / p95 79.120 / max 79.429 ms. Timing includes IR parsing, schema/parameter/input
evaluation, current references, recursive selectors, queries, hashing, exact modeling,
validation and naming. It excludes authoring serialization, CLI startup, file I/O, STEP
encoding, IPC and tessellation. These are local observations, not latency guarantees;
the earlier loaded-run miss and #95 follow-up remain recorded. Logs/data:
`/tmp/nightfall-129-bench.log`, `/tmp/nightfall-129-bench.json`.
Remote CI/review and full Phase 2 sign-off remain pending.

### Indexed viewport mesh evidence

2026-10-18, #130, Apple M3 Pro / 12 cores / 36 GiB, Release, dynamically linked OCCT 7.9.3.
The focused backend run passes eight cases / 115,686 assertions; evaluator integration passes
two initial fine-only cases / 175,755 assertions, seed 20261018; the final suite additionally
checks the default mesh on every reference. A 10×20×30 mm box has 24 face-local vertices,
12 triangles and six contiguous face ranges. Outward signed mesh volume 6000 mm³ and area
2200 mm² match analytic values within 1e-7, including after rigid placement/reflection.
Cavity volume 5976 / area 2252 and separated solid components exercise inner-shell winding.
Fine annular meshes converge toward exact volume 2250π / area 1050π within 0.2%; the sphere
is within 0.5% of volume 4000π/3 / area 400π. Normals are finite and unit within 1e-6,
including seams, poles and inward bore normals. Every range owns an original snapshot face;
indices, winding, face coverage, geometry bounds and retained lifetime are verified.

All twenty current reference parts produce default (0.1 mm / 0.2 rad) and fine
(0.02 mm / 0.15 rad) meshes, with approximate volume/area within 1% of their exact solids. Tessellation preserves evaluator hashes, cached
shape pointers, original serialized B-reps and existing triangulations; retained face handles
remain usable after facade teardown and cache eviction. A chamfered, placed, mirrored
part exercises naming/mesh ownership across geometry reconstruction. Degenerate pole nodes
with equal positions are removed before cross products; genuine triangles require consistent
normal/winding. Copy-history orientations are recovered from the actual copied solid.
Invalid options/foreign snapshots and returned-data limits fail without changing inputs.

`tools/check.sh --preset occt` passes 693/693 (50.62 s), including hygiene/format,
202 schema cases and twenty reference documents, without warnings. Initial interface tests
fail to compile; the first geometry run passes 2/8, then 7/8 after the orientation fix.
The final pole regression also fails before its fix. Logs: `/tmp/nightfall-130-red-build.log`,
`/tmp/nightfall-130-kernel-initial.log`, `/tmp/nightfall-130-pole-red.log`,
`/tmp/nightfall-130-kernel.log`, `/tmp/nightfall-130-evaluator.log`,
`/tmp/nightfall-130-check.log`. The pre-default-refinement gate also passes 693/693 (53.16 s),
retained in `/tmp/nightfall-130-initial-check.log`. The initial meshing benchmark exposed
1.3898% volume / 0.7454% area error for the spherical knob at 0.1 mm / 0.3 rad. A failing
regression confirms the default miss; tightening the default angle to 0.2 rad fixes it.
Both partial runs are retained: `/tmp/nightfall-130-mesh-bench-initial.log` and
`/tmp/nightfall-130-mesh-bench-diagnostic.log`; the red test is
`/tmp/nightfall-130-default-red.log`. Final sanitizer/meshing evidence follows.

After the final Release gate, `nf_mesh_bench` measures twenty reference parts at default
0.1 mm / 0.2 rad and fine 0.02 mm / 0.15 rad: one untimed warm-up plus thirty samples
per part/quality, 1200 measured mesh queries. Every sample checks complete face coverage,
deterministic vertex/triangle counts and approximate volume/area within 1% of exact measures.
Default median times span 0.154–17.576 ms; fine medians span 0.153–104.057 ms. The slowest
case in both modes is the toroidal seal:

- Default: median 17.576 / p95 18.258 / max 18.438 ms; 4352 vertices, 8064 triangles,
  four face ranges, 305,760 buffer bytes, volume error 0.325990%, area error 0.143671%.
- Fine: 104.057 / 112.425 / 114.348 ms; 16,067 vertices, 31,160 triangles, four ranges,
  1,145,232 buffer bytes, volume error 0.119084%, area error 0.034162%.

The largest approximation error is the spherical knob: default volume 0.654812% /
area 0.354961%, fine volume 0.285691% / area 0.154450%. Its default query takes
6.310 / 6.469 / 6.538 ms, fine 19.524 / 19.741 / 19.750 ms. All other reference errors
are smaller. Buffer bytes count vertices, indices and range structs, excluding retained
face-handle objects/source solids and allocator overhead; they are not RSS measurements.
Timing includes deep copy, meshing, surface normals, mesh checks and face associations;
it excludes document rebuild, caller-side verification, transport, GPU upload and rendering.
Fine meshing can exceed 100 ms and must stay on the service worker. This is separate from
the parameter-rebuild gate and does not establish viewport frame time. Logs/data:
`/tmp/nightfall-130-mesh-bench.log`, `/tmp/nightfall-130-mesh-bench.json`.
The C bridge, XPC/Metal connection and portable development app bundle remain the next steps;
there is no production visual-testing app artifact from this backend-only increment.
Remote CI/review and full Phase 2 sign-off remain pending.

Final ASan/UBSan kernel/cache/evaluator/CLI passes 455/455 (116.82 s), without diagnostics.
The initial pre-default-refinement run also passes 455/455 (117.23 s); both are retained in
`/tmp/nightfall-130-asan.log` and `/tmp/nightfall-130-initial-asan.log`. Nightfall code is
instrumented; the pinned OCCT shared library remains Release. The final public query/defaults
and both mesh-quality modes are covered; development app integration remains separate.

### #131 — production plain-C evaluator/mesh bridge (2026-10-18)

Under Accepted RFCs 0003 and 0007, `nf_kernel_bridge` exposes per-document sessions and
independently owned results through `kernel_bridge.h`, with no C++/OCCT types in its C17
or Swift 6 import surface. A session owns the existing evaluator/cache (96 entries,
64 MiB estimated); results retain immutable JSON and indexed vertex/index/face buffers
after later builds and session teardown. The manifest carries exact body measures/hashes,
feature states/diagnostics, cache statistics and current semantic names per transient face.
Names can remain ambiguous; body/face/vertex indices are never persisted selectors.

Requests and result JSON are capped at 16 MiB; returned meshes share a request-wide budget
of at most one million triangles / three million vertices. Any feature, meshing or budget
error returns zero geometry, including independently successful branches. Exceptions stay
behind the ABI; allocation/diagnostic failure can return null. These bounds do not cap OCCT
working memory/runtime. Build calls stay serialized on the service worker. An atomic signed
active-feature index reports the executing authored feature, including cache hits, and -1
during validation, meshing and idle, preparing the later XPC watchdog.

Tests first fail to compile for the missing bridge/progress interface. Swift importing then
catches the unsigned size_t sentinel mismatch and unsupported macro expansion; the final
signed 64-bit index and literal sentinel import together. Thirteen focused checks pass
(1.28 s), including actual C17 compile/link and successful geometry, Swift 6 complete strict
concurrency typechecking, cache isolation/rebinding/recovery, suppression/rollback/consumed
bodies, sorted multi-body ranges, retained ownership and zero partial-success buffers.
The exact 16 MiB input boundary succeeds; an 8 MiB unknown key whose RFC 6901 pointer
expands past the output limit yields a small structured bounds error. All 20 reference
baselines cross the C ABI with outward indexed meshes within 1% of exact volume and area;
the analytic plate measures 6000 mm³ / 2200 mm² within 1e-7, with mesh relative tolerance 1e-9.

`tools/check.sh --preset occt` passes 706/706 (52.16 s), plus 202 schema corpus cases and
20 reference schemas, with license/layering/format checks and no new warnings. Targeted
ASan/UBSan kernel/cache/evaluator/CLI/bridge passes 468/468 (120.92 s), without diagnostics;
Nightfall code is instrumented, the pinned OCCT shared libraries remain Release. Logs:
`/tmp/nightfall-131-red-build.log`, `/tmp/nightfall-131-swift-sentinel-red.log`,
`/tmp/nightfall-131-bridge.log`, `/tmp/nightfall-131-check.log`, `/tmp/nightfall-131-asan.log`.
CI now includes the bridge sanitizer targets; hygiene checks include the new C source.
The Swift XPC worker/watchdog/restart policy, shared-memory transport, Metal viewer and
portable app packaging remain separate increments. No production `.app` exists from this
change; remote CI, human review and the full Phase 2 sign-off remain pending.

### #133 — bounded Swift XPC worker and watchdog (2026-10-19)

Accepted RFCs 0003/0007 now have a reusable `nf_native_worker` Swift library and a shared
Foundation-only `KernelServiceProtocol`. The listener shares one worker across connections.
The worker accepts UUID document IDs and complete v1 JSON, rejects requests over 16 MiB,
admits one rebuild without a waiting build queue, and retains four document caches by LRU.
Explicit closure releases a document cache; native destruction returns to the same serial
queue as all kernel work. A narrowly documented unchecked-Sendable wrapper owns the opaque
C session; only its C atomic active-feature accessor runs concurrently. Result JSON and
indexed buffers are copied to owned Data before freeing the C result.

A separate five-second watchdog starts at admission, covering cache/session creation,
evaluation, meshing and payload copying. A Mutex-protected reply claim arbitrates completion
and timeout. Timeout diagnostics name the active authored feature when available, reply once,
then call _exit(124). A timed-out worker stays unavailable if termination is replaced in tests;
late completion cannot publish a second result or make it reusable. Cache creation failures
release normal admission. The cache count/estimated budgets do not cap process RSS or OCCT
working memory; shared-memory transfer and service admission beyond this initial policy remain open.

Tests first fail to import the missing module. Twelve focused cases pass (2.37 s): bounds,
busy admission, cache isolation/LRU/closure/recovery, copied-buffer ownership, factory failure,
watchdog during factory or feature work, 1000 concurrent completion/deadline claims, all twenty
reference baselines, the actual NSXPC codec, and child-process timeout reply/exit. Timeout tests
use injected 200 ms deadlines and blocking test engines, not new persisted IR features.
All twenty meshes have complete face ranges, unit/outward normals, valid indices and volume/area
within 1% of exact measures. Analytic plate quantities remain 6000 mm³ / 2200 mm² within 1e-7.
Four real 30-feature plate documents stay warm with 30 cache hits each; a fifth evicts the LRU.
An uncontended Release residency run peaks at 30,523,392 bytes RSS (29.1 MiB), including the
framework/bridge process and transient eviction; this is a small-part baseline, not a memory cap.

Swift builds in version 6 with complete strict concurrency and warnings as errors; vtool confirms
minimum macOS 15.0 in the test executable. `tools/check.sh --preset occt` passes 718/718 (50.36 s),
202 schema cases and 20 reference schemas, with license/layering/format checks. Swift-format lint
also passes. A separate Swift ThreadSanitizer build passes 12/12 (30.32 s), without diagnostics;
the C/C++ libraries stay Release in that build. CI now runs this concurrency check separately
from the existing C/C++ ASan/UBSan preset. Logs: `/tmp/nightfall-133-red-build.log`,
`/tmp/nightfall-133-{native,check,tsan,residency}.log` (the initial import failure is intentional).

The anonymous-listener test follows [Apple TN3113](https://developer.apple.com/documentation/technotes/tn3113-testing-xpc-code-with-an-anonymous-listener)
and runs client/server in one process. The child watchdog test proves reply/termination with
injected engines. Portable .xpc/.app packaging, actual launchd crash/hang/restart/replay, the
app-side backstop/retry, shared memory and Metal UI remain separate work before app use.
No production visual-testing .app is produced here. Remote CI/human review and full Phase 2
sign-off remain pending. The preceding bridge PR #132 now has all five remote CI checks green;
its human review is still pending.

### #135 — embedded service bundle and real process recovery (2026-10-19)

The production worker/listener now runs in an Application-lifetime `NightfallKernel.xpc`
inside a headless Foundation-only test host. Accepted RFCs 0003/0007 cover this integration.
The host compiles only the shared protocol and has no native kernel linkage. A reusable
development packager collects the 26 required OCCT dylibs in the service's Frameworks,
copies their LGPL/exception notices, rewrites install names/runtime paths and ad-hoc signs
libraries, service and outer app. Swift dependency metadata is removed from code-only
MacOS folders. The resulting host bundle occupies 46 MiB on disk. Distribution signing,
notarization and release obligations remain separate; this is a test/development artifact.

Tests first fail for the missing bundle target. Six focused bundle checks pass (65.25 s):
real process separation, complete IR/XPC transport, cache isolation/closure/recovery,
owned result retention, all twenty reference meshes, service termination/replay, controlled
crash and watchdog hang, and copied-bundle execution from a path containing spaces. The
relocation check independently verifies every dependency, runtime path, macOS 15.0 target
and ad-hoc signature. Kernel dylibs occur only in the embedded service. Mesh volume/area
remain within 1% of exact values; analytic plate quantities are 6000 mm³ / 2200 mm² within
1e-7. Indexed ranges, normals and winding are checked after transport.

Hang/crash controls exist only in a separate test service/protocol. A real injected blocked
engine produces a feature-7 timeout reply at a 300 ms test deadline, exits, and is replaced
by a fresh service that rebuilds complete IR with cold then warm caches. Production keeps
its default five-second watchdog and has no test operation or persisted fault feature.
Forced-exit/replay takes roughly 10.2–10.6 s on this host, including observed service
relaunch delay; this is not kernel execution latency. The app client must surface failures
and enforce RFC 0003's seven-second backstop rather than assume immediate relaunch.

`tools/check.sh --preset occt` passes 724/724 (123.59 s), 202 schema corpus cases and twenty
reference schemas, with license/layering/format checks and no new warnings. Swift 6 complete
strict concurrency, warnings as errors and strict swift-format lint pass. The first sandboxed
XPC lookup is rejected (error 159); real process tests and the complete gate pass with
launchd access. Logs: `/tmp/nightfall-135-{red-build,build,bundle,check}.log`. The existing
worker PR #134 now has all three remote CI checks green, including Swift TSAN and C/C++
sanitizers; its human review remains pending. No C/C++ ownership or worker algorithm changes
are made here. App-side timeout/suppression retry, shared memory and Metal UI remain next;
there is still no production visual-testing app. This increment's CI/review and the full
Phase 2 sign-off remain pending.
