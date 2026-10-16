# Phase 0 — Foundations + kernel bake-off

- **Weeks:** 1–4
- **Milestone:** `Phase 0` on GitHub
- **Status:** In progress

## Gate

All of these, with evidence linked:

- [ ] RFC 0001 accepted with bake-off numbers and a kernel decision
- [ ] A box appears in a Metal view, built through an XPC service from a JSON IR
- [ ] CI builds and tests on macOS on every PR; nightly sanitizer build green
- [x] `main` protected per WORKFLOW.md §2 (required: `hygiene`, `build-and-test (macOS arm64)`)

## Work items

Tracked as GitHub issues in the [`Phase 0` milestone](https://github.com/wckdboy/nightfall/milestone/1).

| Issue | Item | Scope | Status |
| --- | --- | --- | --- |
| [#1](https://github.com/wckdboy/nightfall/issues/1) | Repository, workflow, CI, governance files | process | Done (#12) |
| [#2](https://github.com/wckdboy/nightfall/issues/2) | CMake build, presets, `libnf-math` seed with tests | build | Done (#13) |
| [#3](https://github.com/wckdboy/nightfall/issues/3) | Branch protection, labels, milestones, CODEOWNERS on GitHub | process | Done |
| [#4](https://github.com/wckdboy/nightfall/issues/4) | Build SMLib on Apple Silicon (own CMake; no premake needed) | third_party | Done (#14) |
| [#5](https://github.com/wckdboy/nightfall/issues/5) | Build OCCT 7.9.3 for arm64 as a cached CI artifact | third_party | In review |
| [#6](https://github.com/wckdboy/nightfall/issues/6) | Bake-off harness: kernel interface, JSON runner, OCCT driver | kernel | In review |
| [#16](https://github.com/wckdboy/nightfall/issues/16) | Bake-off SMLib driver | kernel | Not started |
| [#17](https://github.com/wckdboy/nightfall/issues/17) | Bake-off reference corpus: 20 parts | kernel | Not started |
| [#18](https://github.com/wckdboy/nightfall/issues/18) | ABC dataset sample manifest (200 STEP models) | evals | Not started |
| [#7](https://github.com/wckdboy/nightfall/issues/7) | Run the bake-off, write results into RFC 0001 | kernel | Not started |
| [#8](https://github.com/wckdboy/nightfall/issues/8) | XPC service spike: JSON IR → kernel → mesh | kernel | Not started |
| [#9](https://github.com/wckdboy/nightfall/issues/9) | Metal viewport spike showing the mesh | viewport | Not started |
| [#10](https://github.com/wckdboy/nightfall/issues/10) | Nightly ASan/UBSan workflow | ci | In progress (workflow merged in #13; first green run pending) |
| [#11](https://github.com/wckdboy/nightfall/issues/11) | Layering check: no AppKit below `app/` | ci | Not started |

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
  #6 split into #16 (SMLib driver), #17 (20-part corpus), #18 (ABC manifest).
