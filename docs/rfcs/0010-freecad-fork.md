# RFC 0010: FreeCAD as the Nightfall product foundation

- **Status:** Review
- **Author(s):** The Nightfall Authors
- **Phase:** 2 transition; the new fork tracks its foundation milestone separately
- **Created:** 2026-10-21
- **Issue:** [#246](https://github.com/wckdboy/nightfall/issues/246)

## Summary

The maintainer explicitly changes Nightfall's direction: "nightfall now is a fork of
freecad, made more AI native, better verification, more fusion360 like UX for human
input". The maintainer restores UI/UX to scope and identifies speed, simplicity,
stability, AI nativeness, flexible sketching and intelligent constraint handling as
priorities. Establish a stable FreeCAD fork and build those improvements on its native
documents, geometry, Sketcher, transactions and workspaces. Preserve the existing
Nightfall engine as reference work instead of deleting or claiming to complete it.

The fork decision is a direct user instruction. This RFC records that instruction and
proposes the implementation boundaries and checks below; its Review status is not a
claim of acceptance for every future tool schema, add-on or distribution change.

## Motivation

The existing OCCT-based engine provides useful geometry, typed Rust native ownership,
owned spline math and verification receipts. It still lacks assemblies and simulation,
and its Eclipse acceptance remains failed. Continuing the independent engine as the
primary product delays access to existing engineering workflows and their accumulated
behavior. A large test count is not evidence of commercial-kernel parity.

The isolated [#245 evaluation](https://github.com/wckdboy/nightfall/issues/245) executes
FreeCAD 1.1.4 at source `4fd3bf320d9566a27e60069fc8387448aaa3a094`. A downstream
attachment survives parameter edit/revert, an upstream topology change from Face6 to
Face7, and save/reload. An actual native fixed joint corrects approximately 30 mm / 37
degrees of deliberately introduced error between linked occurrences. These finite
cases support reuse; they establish neither general naming correctness nor physics.
The analysis experiment only creates a domain and associates a fixed boundary.

## Design

### Source and product boundary

- The fork is [wckdboy/nightfall-freecad](https://github.com/wckdboy/nightfall-freecad),
  with FreeCAD upstream history and credits preserved. Begin from tag **1.1.4**, exact
  commit above; avoid moving to development main simply to acquire more features.
- Keep `wckdboy/nightfall` and its unmerged/failed work intact as reference. Do not
  rewrite its main branch, rename its repository or silently import unfinished changes.
- FreeCAD's C++/Python/Qt stack becomes the product foundation. Rust is a tool where it
  improves a measured subsystem, not a requirement to duplicate FreeCAD's engine.
- Start with a small native Nightfall workbench using the existing extension mechanism.
  Keep the patch set reviewable; prefer upstream fixes and compatible extension points.
  No separate web UI, second document evaluator or new solver process is introduced.
- Development may load fork Python files into the exact official 1.1.4 runtime. Receipts
  must distinguish this from building and packaging a modified C++ application binary.

### Documents, commands and verification

Use native FCStd documents and native transactions/undo as the fork's starting model.
The legacy `.nightfall` feature IR is not transparently interpreted as FCStd. Any later
importer must report supported operations, losses and unsupported semantics explicitly.
Retained reference test parts and analytic oracles remain valuable; do not transfer old
capability claims or benchmark results to the fork without running them there.

Human controls and future AI tools invoke the same bounded modeling operations. An AI
edit must identify its document/revision, validate inputs and references, group changes
into an undoable transaction, recompute and inspect native errors, then report complete
success or roll back. Tool schemas, revision identity and provider/MCP transport need a
concrete reviewed implementation before exposure; arbitrary Python execution is not a
substitute for an AI modeling contract.

Begin with read-only sketch feedback shared by a native panel and a Python helper. Report
native degrees of freedom, conflicting/redundant/malformed constraint numbers, object
identity and whether solver facts are stale. Reading feedback must not invoke a solve,
change geometry, commit user transactions or delete constraints. A cached native solve
is not an independent certificate of geometric intent, closed profiles or physical
correctness. Unknown and unchecked fields remain explicit in compact AI feedback.

Keep semantic references and cardinality checks in later AI modeling tools. FreeCAD's
native attachment remapping is useful evidence, but it does not authorize nearest-face
fallback or an assumption that every native name survives split/merge/deletion. Re-run
adversarial edit/revert/save/reload cases before advertising stronger guarantees.

### Sketching and human workflow

Sketches may remain underconstrained while a person explores. Distinguish freedom to
move, missing dimensions, conflicts, redundancy and actual solver failure. Do not turn
"fully constrained" into a mandatory modal hurdle or indiscriminately fix all geometry.
Offer dimensions and relations that express intent; preserve intended motion. Future
constraint suggestions must preview changes and their effects and support one-step undo.
Automatic repairs need explicit behavior and tests rather than silent deletion.

The first slice creates a sketch on XY/XZ/YZ using a body's native origin plane, enters
the existing Sketcher, and shows current constraint feedback. Preserve an existing active
body and pending user edits. Invalid plane/body requests leave the document unchanged.
Later work improves selection, contextual dimensions, plane discovery, navigation,
previews and workspace transitions through observed human tasks and measured latency.

### Workspaces and dependencies

Keep upstream Part Design, Sketcher, Assembly and Draft available. Improve their shared
selection, object/occurrence hierarchy and task flow instead of creating duplicate data
models. Use the built-in Assembly as the initial solver and take interaction inspiration
from assembly workspaces without claiming solver/DOF/collision coverage not exercised.

PCB/KiCad and Rendering are requested product workspaces. They are not delivered merely
because an external add-on exists. Evaluate compatible releases, import/export behavior,
native versions, optional dependencies and licenses, then pin the chosen integrations in
separate issues. Do not automatically copy Assembly3, bundle every FreeCAD add-on or
install tools from a development snapshot. Preserve the full upstream license/notice
requirements; the old Apache-2.0 project license does not relicense the FreeCAD fork.

Speed and stability favor the native runtime, small patches, explicit updates, bounded
reports and no background re-solving loop for inspection. Establish cold/warm startup,
sketch/edit latency, representative model recompute, memory and crash/recovery baselines.
Existing Nightfall's five/seven-second service deadlines cannot be asserted for this
different runtime; a new responsiveness contract requires measured fork evidence.

## Alternatives considered

- Continue the standalone Rust migration as the primary product: superseded by the
  maintainer's explicit fork direction. Keep its independent math and oracles available.
- Treat FreeCAD only as an optional worker: gives less access to its native document and
  human workflows and does not implement the newly requested product foundation.
- Fork SolidCraft instead: valuable Rust document/command/MCP code, but its own kernel
  work is paused while a replacement is evaluated. It remains a reference, not this base.
- Rewrite the whole interface immediately: expands risk before a runnable, measurable
  fork exists. Improve complete human tasks incrementally on the native framework.

## Compatibility and migration

This records a product change directed by the maintainer. It supersedes standalone Rust
ownership and UI deferral as scheduling requirements; it does not mark RFC 0008's engine
cutover complete or rewrite Accepted RFC text. Keep all prior failures and evidence.
The fork starts with native FCStd compatibility. No existing files are bulk converted,
overwritten or deleted. The legacy project keeps its current build and clients.

## Testing and measurement

Foundation checks: exact source/runtime pin, successful native startup, native transaction
and undo tests, free/dimensioned/fully constrained/conflicting/redundant sketch cases,
read-only/stale-state checks, and actual GUI plane-to-sketch inspection. Retain source
hashes, native logs, numerical tolerances, screenshots and checked/unchecked scope.
Report a Python-module runtime check separately from a full FreeCAD C++ build/test gate.

Next milestones are shared AI/human edit transactions with rollback; sketch interaction
and constraint suggestions with usability evidence; integrated assembly workflows with
residual/DOF/loop/collision evidence; and pinned PCB/render workflows. Structural/thermal
simulation and CFD remain later engineering work, requiring analytic/convergence and
conservation checks. No milestone is satisfied by a workspace menu label or test count.

## Risks and open questions

Upstream upgrades, UI complexity, persistent-name limitations and optional-workspace
compatibility remain real costs. A full fork does not by itself produce industrial
accuracy or a good experience. Measure those properties against declared tasks and
models, keep upstream synchronization practical, and return explicit failures to AI.
