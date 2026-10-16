# Phase 0 — Foundations + kernel bake-off

- **Weeks:** 1–4
- **Milestone:** `Phase 0` on GitHub
- **Status:** In progress

## Gate

All of these, with evidence linked:

- [ ] RFC 0001 accepted with bake-off numbers and a kernel decision
- [x] A box appears in a Metal view, built through an XPC service from a JSON IR ([evidence](https://github.com/wckdboy/nightfall/blob/spike/8-9-xpc-viewport/docs/phases/evidence/phase0-xpc-metal-spike.png))
- [x] CI builds and tests on macOS on every PR; nightly sanitizer build green
- [x] `main` protected per WORKFLOW.md §2 (required: `hygiene`, `build-and-test (macOS arm64)`)

## Work items

Tracked as GitHub issues in the [`Phase 0` milestone](https://github.com/wckdboy/nightfall/milestone/1).

| Issue | Item | Scope | Status |
| --- | --- | --- | --- |
| [#1](https://github.com/wckdboy/nightfall/issues/1) | Repository, workflow, CI, governance files | process | Done (#12) |
| [#2](https://github.com/wckdboy/nightfall/issues/2) | CMake build, presets, `libnf-math` seed with tests | build | Done (#13) |
| [#3](https://github.com/wckdboy/nightfall/issues/3) | Branch protection, labels, milestones, CODEOWNERS on GitHub | process | Done |
| [#4](https://github.com/wckdboy/nightfall/issues/4) | Build SMLib on Apple Silicon (own CMake; no premake needed) | third_party | Done (#14) |
| [#5](https://github.com/wckdboy/nightfall/issues/5) | Build OCCT 7.9.3 for arm64 as a cached CI artifact | third_party | Done (#19) |
| [#6](https://github.com/wckdboy/nightfall/issues/6) | Bake-off harness: kernel interface, JSON runner, reference parts | kernel | Done (#20) |
| [#21](https://github.com/wckdboy/nightfall/issues/21) | Bake-off OCCT driver and `nf_bakeoff` CLI | kernel | Done (#22) |
| [#16](https://github.com/wckdboy/nightfall/issues/16) | Bake-off SMLib driver | kernel | Done (#23) |
| [#17](https://github.com/wckdboy/nightfall/issues/17) | Bake-off reference corpus: 14 analytic parts | kernel | Done (#29) |
| [#28](https://github.com/wckdboy/nightfall/issues/28) | Corpus: profile, revolve, sweep, variable-radius parts (to 20) | kernel | Done (#30) |
| [#18](https://github.com/wckdboy/nightfall/issues/18) | ABC dataset sample manifest (200 STEP models) | evals | Done (#24) |
| [#25](https://github.com/wckdboy/nightfall/issues/25) | Share the OCCT CI cache via `main`; cancel superseded runs | ci | Done (#26) |
| [#36](https://github.com/wckdboy/nightfall/issues/36) | Bake-off runner: per-part processes, timing, sanitizer pass, scoring | kernel | In review |
| [#7](https://github.com/wckdboy/nightfall/issues/7) | Run the bake-off, write results into RFC 0001 | kernel | Corpus done; ABC waits on RFC 0002 (#33) |
| [#8](https://github.com/wckdboy/nightfall/issues/8) | XPC service spike: JSON IR → kernel → mesh | kernel | Done (spike branch; RFC 0003 #35) |
| [#9](https://github.com/wckdboy/nightfall/issues/9) | Metal viewport spike showing the mesh | viewport | Done (spike branch; RFC 0003 #35) |
| [#10](https://github.com/wckdboy/nightfall/issues/10) | Nightly ASan/UBSan workflow | ci | Done (first run green) |
| [#11](https://github.com/wckdboy/nightfall/issues/11) | Layering check: no AppKit below `app/` | ci | Done (#27) |

## Log

- 2026-10-16 — Phase started. Workflow, RFC process and build skeleton proposed (0.1, 0.2).
- 2026-10-16 — GitHub configured (#3): labels, milestone, issues #1–#11, `main` protected
  (`hygiene` required).
- 2026-10-16 — #12 and #13 merged after an independent review pass and Codex review threads;
  `build-and-test (macOS arm64)` added as a required check.
- 2026-10-16 — SMLib builds on macOS arm64 with plain CMake in 30 s (10 cores) and passes
  upstream's SM_API suite (115/115, 25 s). Under ASan it hits a heap-use-after-free in
  booleans on closed-edge solids (RFC 0001 F3, written up as SMLIB-001, deferred).
- 2026-10-16 — OCCT 7.9.3 builds for arm64 in 6.5 min (10 cores), 54 MB of libraries; smoke
  tests pass (#5). Bake-off harness runs all 6 analytic parts on OCCT at 1e-6 relative (#6).
  #6 split into #21 (OCCT driver + CLI), #16 (SMLib driver), #17 (20-part corpus), #18 (ABC manifest).
- 2026-10-16 — SMLib driver (#16): 4/6 analytic parts pass vs OCCT 6/6; both SMLib failures
  are area accuracy (SM_API area misses 1e-6 on curved faces), volumes all within 4.3e-9
  (RFC 0001 F5–F9).
- 2026-10-16 — Bake-off stack merged after review: harness (#20), OCCT driver (#22), SMLib
  driver (#23), ABC manifest (#24). OCCT CI cache shared via `main` (#26): occt job 28m52s ->
  45 s. Remaining for the gate: corpus (#17), bake-off run (#7), XPC + viewport spikes (#8, #9),
  nightly green (#10), layering check (#11).
- 2026-10-17 — XPC + Metal spike done (#8, #9); RFC 0003 (process architecture, #35) and RFC
  0002 (OpenUSD, #33; 197/200 ABC models translate into SMLib) in review until 2026-10-22.
  Corpus bake-off with both kernels instrumented (#36): OCCT 97.0, SMLib 71.3 (corpus only).
