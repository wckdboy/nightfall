# RFC 0015: External kernel protocol

- **Status:** Review
- **Author(s):** Claude (agent), for the maintainer
- **Phase:** 3
- **Created:** 2026-10-23
- **Decided:**

## Summary

`nf kernel serve` exposes Nightfall's kernel to other CAD programs one operation at a time.
The client starts the command as a child process and exchanges one JSON object per line on
stdin and stdout. Each operand travels as STEP text, together with one sample point per face
and per edge. Each result comes back as STEP text, together with every result face's history
in the result's own STEP face order. The client keeps its own document, naming, undo,
rendering and agent tools; Nightfall supplies booleans, fillets and chamfers. To read operands,
`IKernel` gains `import_step(text)` and `edge_distance`.

The first client is a fork of SolidCraft (Rust and truck, MIT OR Apache-2.0). It keeps its own
kernel for everything else and for wasm, and routes the operations truck is weakest at to OCCT.

## Motivation

SolidCraft has an AI-native modeller worth reusing: a typed engine, an MCP server, lineage
naming and an egui/wgpu UI. Its kernel is truck, which is young: booleans fall back to
faceted results on hard cases, and fillets cover fewer configurations than OCCT's. Nightfall
has the opposite profile. Its kernel is OCCT 7.9.3 with complete face and edge history, its
app is earlier in the build plan, and its agent tools, assemblies and studies are ahead.

Porting SolidCraft onto Nightfall's IR would rewrite most of SolidCraft. Linking OCCT into
SolidCraft would break its licence rules (LGPL), its `unsafe`-free rule and its wasm build.
A process boundary avoids all three:

- The fork never links OCCT, so SolidCraft's code and licence stay as they are.
- It keeps truck, so wasm still builds.
- Nightfall stays the only program that loads OCCT, which is how RFC 0003 already runs it.

A spike on 2026-10-23 checked the round trip:

- SolidCraft exported a box and a cylinder as STEP. OCCT read them, cut one from the other
  and wrote the result.
- SolidCraft read the result back. All 7 faces arrived in OCCT's `TopExp` order, and the STEP
  `ADVANCED_FACE` ids ascended in that order.
- SolidCraft's exporter merges the two halves of a split cylinder face (4 kernel faces became
  3 STEP faces). Operand faces therefore cannot be matched by order, which is why the
  protocol carries sample points.
- Timings: OCCT read 36 ms cold, cut 10.5 ms, write 2.4 ms; SolidCraft read 1.6 ms. Truck
  did the same cut in 5.9 ms.

## Design

### `IKernel::import_step`

```cpp
/// Decode STEP text into closed solids in millimetres, one snapshot per solid, in file order.
/// No files are opened. Reject text without a solid, open shells and invalid solids.
/// Backends without the query report it as unsupported.
virtual std::expected<std::vector<ShapePtr>, KernelError> import_step(std::string_view);
```

A second query maps edge sample points, mirroring `face_distance`:

```cpp
/// Minimum distance in mm from a finite world point to a trimmed edge.
virtual std::expected<double, KernelError> edge_distance(const EntityPtr&, math::Vec3);
```

The OCCT backend reads from a stream with `STEPControl_Reader`, converts units to millimetres
and validates each solid as `checked()` does. Imported shapes carry no history; they start a
lineage.

### Process and framing

- The client starts `nf kernel serve` as a child process. Requests go to stdin and responses
  come from stdout, one JSON object per line. Stderr is for logs only.
- Requests are answered in order. Every response repeats the request's `id`.
- The server exits when stdin closes. A request that runs longer than the RFC 0003 watchdog
  limit ends the process with a report on stderr; the client restarts it and falls back to
  its own kernel.
- Lines are capped at 64 MiB. A longer line gets an error response and the server stops
  reading.

### Requests

```json
{"id": 1, "op": "hello"}
{"id": 2, "op": "boolean", "kind": "union|subtract|intersect",
 "operands": [OPERAND, OPERAND, ...]}
{"id": 3, "op": "fillet",  "operands": [OPERAND], "edges": [3, 7], "radius": 2.0}
{"id": 4, "op": "chamfer", "operands": [OPERAND], "edges": [3],    "distance": 1.0}
```

An operand is one solid:

```json
{"step": "ISO-10303-21; ...", "faces": [[x, y, z], ...], "edges": [[x, y, z], ...]}
```

- `faces[i]` is a point on the client's face `i`, and `edges[j]` is a point on the client's
  edge `j`. The client picks interior points, away from boundaries.
- The server maps each point to the nearest face or edge of the imported solid, using
  `face_distance` and `edge_distance`. The match must lie within 10 linear tolerances plus
  the client's stated `sample_tolerance` (default 1e-3 mm).
- An ambiguous or missing match is an error. The server never guesses.
- Several client faces may map to one OCCT face (a seam split on the client's side).
- In a fillet or chamfer request, `edges` holds indices into operand 0's `edges` list.
- For a boolean, operand 0 is the target and the rest are tools, as in `boolean_operation`.

### Responses

```json
{"id": 2, "ok": true, "kernel": "occt-7.9.3 ...", "ms": 12.4,
 "step": "ISO-10303-21; ...",
 "faces": [{"change": "kept|modified|split|merged|generated",
            "from": [{"operand": 0, "face": 4}, {"operand": 1, "edge": 2}],
            "role": "fillet"}]}
{"id": 2, "ok": false, "error": {"code": "unmatched_face", "message": "..."}}
```

- `faces[k]` describes result face `k`. Faces are numbered in the order of the
  `ADVANCED_FACE` entities in `step`, which is the result's `TopExp` order. A test pins this
  order.
- `from` lists the client's own face and edge indices, translated back through the sample
  point map. Faces of the same OCCT input face are all listed.
- `change` is derived from `IKernel` history:
  - `kept`: the result face is an unchanged input face.
  - `modified`: the input face had exactly one image.
  - `split`: the input face had several images.
  - `merged`: the result face has several input faces.
  - `generated`: the result face has no input faces, only edges or a role.
- `ok: false` leaves the client's state unchanged. Error codes: `bad_request`,
  `unsupported`, `step_read`, `unmatched_face`, `unmatched_edge`, `operation_failed`,
  `too_large`.

`hello` returns the protocol version (`1`), the kernel identity and the operations the
server supports.

### Module boundaries

- The server lives in `tools/cli` next to `nf mcp`, behind `IKernel`. It includes no OCCT
  headers.
- The protocol is versioned. A client must send `hello` and refuse a different major
  version.
- The client never sees OCCT types, and Nightfall never sees the client's types.

## Alternatives considered

- **Rebuild SolidCraft on Nightfall's feature IR.** This replaces SolidCraft's document,
  engine and naming: most of the code that makes it worth forking. Rejected.
- **Link OCCT into the fork through FFI.** This needs `unsafe` and LGPL dynamic linking, and
  breaks the wasm build. It also puts a kernel crash into the UI process, which RFC 0003
  exists to prevent. Rejected.
- **BREP instead of STEP on the wire.** BREP is OCCT's private format and has no reader in
  truck. STEP has readers on both sides and the spike shows its cost is small. Rejected.
- **Match operand faces by order.** The spike shows SolidCraft's exporter merging split
  faces, so order does not survive. Sample points do not depend on either exporter.
- **Send result history as sample points too.** This is unnecessary: the result's face order
  survives the trip, and SolidCraft reports the original STEP face of every kernel face
  (`face_origin`).

## Compatibility and migration

- `import_step` is additive. Its default implementation reports it as unsupported, so other
  backends compile unchanged.
- `nf kernel serve` is a new subcommand; nothing existing changes.
- The protocol starts at version 1. Changes that break clients bump the major version.

## Testing and measurement

- Core tests for `import_step`:
  - An export/import round trip keeps volume and face count within explicit tolerances.
  - Text without a solid, and an open shell, are rejected.
  - An imported solid's `faces()` order equals the order of the `ADVANCED_FACE` entities
    after export.
- CLI tests drive `nf kernel serve` over pipes:
  - `hello`.
  - A box minus a cylinder: the result volume within 1e-6 relative, and the history naming
    the right input faces.
  - A fillet whose new faces come from the requested edges, with role `fillet`.
  - An ambiguous sample point, rejected with `unmatched_face`.
  - A malformed line, answered with `bad_request` without exiting.
- Client tests live in the fork: its own boolean and fillet suites run against the
  Nightfall backend.

## Risks and open questions

- **STEP round-trip precision.** Each trip re-approximates nothing (analytic surfaces stay
  analytic), but tolerances can grow. Mitigation: the server validates every imported solid,
  and the client keeps its own kernel as a fallback.
- **Latency.** STEP text grows with the model. If the cost shows up on large parts, a later
  version can cache operands by digest on the server, so the client sends only the digest.
- **Sample-point quality.** It depends on the client choosing interior points. The server
  rejects ambiguous matches instead of guessing.
- **Exposure.** The server reads only stdin and has no network listener.
