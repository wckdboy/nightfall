# RFC 0006: Bounded Nightfall Script recipes and document proposals

- **Status:** Review
- **Author(s):** The Nightfall Authors (agent-authored proposal)
- **Phase:** 2
- **Created:** 2026-10-19
- **Issue:** [#147](https://github.com/wckdboy/nightfall/issues/147)
- **Implementation:** [#93](https://github.com/wckdboy/nightfall/issues/93), not started

## Summary

Introduce a versioned `nf` JavaScript API that compiles a recipe into an ordinary v1
part document. Human and AI authoring produce the same editable feature IR and use the
same validation, kernel, measurements and viewport. A recipe returns an isolated candidate;
failed or cancelled work cannot replace the open design. JavaScriptCore runs outside the
app and kernel processes. Runtime isolation, resource control and source diagnostics require
evidence before untrusted execution is enabled. This proposal accepts no implementation yet.

## Motivation

The native preview now exposes parameters, history suppression/rollback, exact measures and
the production viewport (#140/#145). A script host and shared authoring contract are missing.
RFC 0005 reserves this API decision; WORKFLOW §3 requires acceptance before implementation.
Adding unrelated UI, script and agent mutation paths would make undo and failure behavior diverge.

SolidCraft's [MCP documentation](https://github.com/tangency-labs/solidcraft/blob/main/docs/mcp.md)
provides a useful product reference: discover commands, inspect a design, change it, measure
and view the result, with undo and batch failure handling. Nightfall should expose that
workflow over its own IR/evaluator contract. This proposal takes no source or dependency
from SolidCraft and does not adopt its tool schemas.

## Design

### First slice and source of truth

A recipe generates a complete new candidate document. Applying it stores ordinary v1 IR;
subsequent parameter/history edits do not rewrite JavaScript. Source code remains an authoring
input, never the sole representation of the part. Initial execution supports JavaScript only.
TypeScript needs an explicit compiler/dependency decision; the host must reject it rather
than attempt regex-based type removal. Generated declarations can document the JS API.

The existing v1 `script` feature remains explicitly unsupported in this first slice. Embedded
script features need a later decision covering private feature identity, generated provenance,
dependency hashing, result publication and diagnostic mapping. This does not reinterpret that
feature or remove it from the schema. #93 stays open until its agreed implementation scope,
sandbox and diagnostics are delivered. MCP schemas, model providers, assemblies, collaboration
and physics remain separate work; this RFC changes no phase gate.

### API version 1

The immutable global `nf` exposes only these builder operations and frozen JSON arguments:

- `nf.requireApi(1)` selects an exact supported API version before any builder call.
- `nf.part({meta, units})` begins exactly one part; units default to mm/deg and metadata to
  an empty object. `meta` accepts the complete RFC 0005 metadata object, including `name`,
  `doc` and `x-` extensions, copied without dropping descriptions or extension values.
  The host supplies the nine required envelope fields, including empty references/UI and
  null `history_end`.
- `nf.parameter(definition)` appends an RFC 0005 parameter with its authored name/value,
  optional bounds and documentation. No silent replacement of an existing name.
- `nf.feature(definition)` appends an RFC 0005 feature, including optional suppression.
  IDs, inputs, selectors and references follow the published IR schema, with no raw kernel handles.
- `nf.historyEnd(idOrNull)` sets the existing inclusive rollback marker.
- `nf.finish()` seals exactly one result. Missing/duplicate finish, further builder calls,
  duplicate IDs and invalid references fail the complete recipe. Completion is synchronous,
  before evaluation returns; later callbacks cannot finish an earlier request.
- `nf.arguments` is the caller's deeply immutable JSON value. It contains no host objects,
  credentials, file handles or live document reference. Missing arguments are explicit in JS.

Inputs are copied at each call; later JS mutation cannot change an earlier feature.
Reject cycles, functions, BigInt, non-finite numbers and other non-JSON values before encoding,
with bounded traversal. Getters/proxies can execute JS during conversion: copying must remain
inside the worker's watchdog and produce owned JSON, never live JS values. Reject the request
if safe bounded conversion cannot complete. Do not use JSON serialization's conversion
of invalid numbers to null as validation. Unsupported modeling operations retain explicit
kernel capability errors; accepting a feature schema does not imply geometry support.
Import/external-resource and embedded-script generation are unavailable in the initial host.

Example, proposed API (not executable in the current app):

```javascript
nf.requireApi(1);
nf.part({meta: {name: "Scripted plate", doc: "60 × 40 mm parameterized plate."},
         units: {length: "mm", angle: "deg"}});
nf.parameter({name: "thickness", value: nf.arguments.thickness ?? "4 mm",
              min: "0.5 mm", max: "100 mm"});
nf.feature({id: "outline", type: "sketch", inputs: {
  plane: {datum: "xy"},
  entities: [{id: "blank", type: "rect", corner: [0, 0], size: [60, 40]}],
  constraints: []
}});
nf.feature({id: "plate", type: "extrude", inputs: {
  profile: {sketch: "outline"}, distance: "thickness",
  result: {new: "part", body_type: "brep"}
}});
nf.finish();
```

JS arithmetic uses numbers; dimensional expressions remain authored IR strings and are
resolved by libnf-ir. Bare parameter numbers stay dimensionless; feature numbers follow
document units, exactly as in RFC 0005. Unit strings are never stripped or implicitly cast.
The example therefore emits the same exact 60×40×4 mm solid as a hand-authored document.

### Isolation and bounds

Proposed placement: `core/script` links the system JavaScriptCore framework and `core/ir`,
with no OCCT, OpenUSD, AppKit or filesystem operations. A separate application-lifetime
`NightfallScript.xpc` hosts it behind a plain-C boundary; the app compiles only a Foundation
protocol. One serial worker admits one recipe, with a fresh VM/context group per request.
No JS values cross a request or actor boundary. CLI use needs a Foundation-only bundled
headless host, as already demonstrated for kernel XPC tests; its lookup/package/relocation
must be proven before a user-facing CLI command is enabled.

Request: UUID, API version, UTF-8 source, JSON arguments and an opaque source label.
Reply: matching UUID, owned candidate IR bytes or diagnostics, source locations, compiler
identity and measured phase timings. No file paths, native objects or executable callbacks
cross the boundary. Validate sizes/layout on both sides. Request/connection/revision tokens
settle a call once and discard cancelled or stale replies. There is no automatic source edit
or suppression retry for recipe failures; a later explicit request uses a fresh connection.

Proposed initial admission budgets, subject to recorded platform measurements:

- 1 MiB UTF-8 source, 1 MiB argument JSON, 16 MiB generated IR; 32 levels of builder input.
- 256 features, 512 parameters and 1,024 builder calls per recipe. Existing kernel/sketch
  limits still apply. These are compiler admission limits, not persisted-format restrictions.
- Two seconds per admitted compile, including VM work and result conversion, enforced by
  an independent service watchdog; seven seconds at the caller, including startup/IPC.
  Timeout/cancellation retires the process if execution cannot be stopped reliably.
- A 256 MiB resident-memory threshold is a proposed *soft* admission/termination policy,
  not a hard heap or system-memory guarantee. Record baseline, peak and overshoot separately.

The installed SDK's public JavaScriptCore headers expose context creation/evaluation but no
per-context execution-time or heap-limit API. Upstream declares execution-time control in
[a private header](https://github.com/WebKit/WebKit/blob/main/Source/JavaScriptCore/API/JSContextRefPrivate.h).
Do not import private headers or redeclare private symbols. A process watchdog handles
non-cooperative loops; source/output bounds alone do not bound VM allocation. Enabling
untrusted scripts is gated on a reviewed, tested resource policy that satisfies #93's memory
requirement. A polling RSS threshold alone must not be presented as a hard memory sandbox.

Export no Objective-C bridge, module loader, filesystem, network, process or environment API.
Removing host callbacks is only one defense: the service must also demonstrate OS-enforced
file/network isolation. Exact entitlements and resource enforcement are acceptance decisions,
not assumed properties of an XPC bundle. Tests must prove denied access and safe process
termination/recovery before this host is connected to arbitrary files or AI-generated code.

### Candidate transaction and inspection

The compiler returns plain IR, which the caller revalidates independently. The production
kernel rebuilds the candidate under a separate document UUID before publication. A script
exception, schema/unit/reference error, modeling error, budget failure or cancellation
leaves the current authored source, selection, undo/redo and saved-file baseline unchanged.
A valid empty/sketch-only candidate is valid, with no body measures (#145).

Apply is one revision-checked document transaction and one undo entry. If the live document
changes during compilation/rebuild, return a stale-candidate error rather than overwrite it.
Explicit human parameter edits keep their existing editable-error/last-valid-display behavior;
they are not converted into candidate generation. File saving remains a trusted caller action.

Future UI/CLI/AI adapters should share the same document transaction and inspection layer:
parameter definitions, feature states, exact measures, semantic selections, diagnostics,
undo depth and model image. Discovery should derive feature schemas from the published IR,
with supported capabilities reported separately. A later MCP/tool RFC specifies its wire
schemas and authorization. There is no generic native-code `execute` escape hatch here.

### Determinism and diagnostics

Expose no clock, entropy or external resources; wall time and locale must not become hidden
model inputs. Block Date/Math.random and other nondeterministic facilities in the supported
language profile. Repeatability tests are required; arbitrary JS is not assumed deterministic
merely because I/O is absent. Fresh VMs prevent cross-recipe globals. Compile identity covers
API/compiler version, OS/JSC identity, source bytes, argument bytes and profile/budget version.
Identical source still needs canonical IR comparison; no persistent script cache initially.
Geometry caching continues to use the evaluated IR and RFC 0007's evaluator identity.

Diagnostics carry a stable category, message, source label, optional 1-based line/column,
and available feature ID/JSON Pointer. Preserve source bytes; wrapper prefixes must not
shift reported positions. Syntax/runtime location mapping and builder-call provenance need
SDK-level tests, including Unicode and multiline input. Never fabricate an unavailable line.
Review must settle how structural IR errors map to builder calls before claiming #93's
source-location requirement. Return bounded messages without host paths or native stacks.

## Alternatives considered

- **Run JavaScript in the app:** a loop or allocation failure would threaten the UI/document.
- **Run it in the geometry service:** script resource failures would discard kernel caches;
  an isolated compiler keeps the output as ordinary independently validated IR.
- **Use a Node/TypeScript runtime now:** adds dependencies, host capabilities and transpilation
  choices before the initial JavaScriptCore contract and sandbox have been validated.
- **Make script text authoritative or expose B-rep mutation:** conflicts with editable IR,
  semantic selectors and immutable kernel results. Ordinary generated features preserve them.
- **Add a free-form MCP command first:** leaves shared transaction/schema behavior undefined.

## Compatibility and migration

No existing `.nightfall` migration, hash change or accepted RFC rewrite. Recipes are a new
authoring input with an explicit API version; generated output is v1 IR. Existing `script`
features remain readable and explicitly unsupported until their separate semantics land.
JavaScriptCore is the proposed system-framework dependency; no dependency is added by this PR.
The new compiler process/protocol require this RFC's acceptance before implementation.

## Testing and measurement

1. Unit/schema tests: exact envelope/units/IDs/history, immutable argument/input copies,
   invalid JSON values, sizes/depth/call bounds, unsupported language/import/script and locations.
2. Differential recipes for all twenty parts: compare complete canonical IR (including
   metadata descriptions/extensions, parameters, features and history), analytic volume/area/
   centroid/bounds, selectors, parameter variants and exact STEP round trips. No tolerance widening.
3. Real process tests: startup, concurrency/admission, cancellation, stale/duplicate replies,
   missing callbacks, forced exit, infinite loops, budget failure, denied I/O and complete replay.
4. Transaction tests: a failed middle step and a stale candidate leave document/file/undo state
   byte-identical; successful apply/undo/redo uses the same model as human edits.
5. Repeated fresh/long-lived-worker runs: report compiler/startup/IPC/kernel/mesh timings,
   p50/p95/max, process RSS/overshoot and retained resources on stated hardware/OS. The existing
   <100 ms Phase 2 parameter-rebuild gate remains unchanged and excludes no new work silently.
6. ASan/UBSan for the C boundary, Swift strict concurrency/TSAN for transport, native visual
   verification and required `tools/check.sh`. No untrusted runtime before isolation evidence.

## Risks and open questions

- Confirm separate-service entitlements, supported memory enforcement and budget measurements.
  If the required bound cannot be achieved, narrow trusted-prototype scope and return for review.
- Prove source locations and the supported deterministic JS profile on the deployment SDKs.
- Measure standalone CLI host launch/relocation; choose packaging before publishing a command.
- Decide embedded-script provenance/results and TypeScript in later scoped decisions.
- Approve exact initial `nf` spelling and recipe API versioning before clients depend on it.
