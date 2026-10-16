# Phase 0 — Foundations + kernel bake-off

- **Weeks:** 1–4
- **Milestone:** `Phase 0` on GitHub
- **Status:** In progress

## Gate

All of these, with evidence linked:

- [ ] RFC 0001 accepted with bake-off numbers and a kernel decision
- [ ] A box appears in a Metal view, built through an XPC service from a JSON IR
- [ ] CI builds and tests on macOS on every PR; nightly sanitizer build green
- [ ] `main` protected per WORKFLOW.md §2

## Work items

Tracked as GitHub issues in the [`Phase 0` milestone](https://github.com/wckdboy/nightfall/milestone/1).

| Issue | Item | Scope | Status |
| --- | --- | --- | --- |
| [#1](https://github.com/wckdboy/nightfall/issues/1) | Repository, workflow, CI, governance files | process | In review |
| [#2](https://github.com/wckdboy/nightfall/issues/2) | CMake build, presets, `libnf-math` seed with tests | build | In review |
| [#3](https://github.com/wckdboy/nightfall/issues/3) | Branch protection, labels, milestones, CODEOWNERS on GitHub | process | Not started |
| [#4](https://github.com/wckdboy/nightfall/issues/4) | Fork usd-brep; build SMLib on Apple Silicon with a self-built premake | third_party | Not started |
| [#5](https://github.com/wckdboy/nightfall/issues/5) | Build OCCT 7.8 for arm64 as a cached CI artifact | third_party | Not started |
| [#6](https://github.com/wckdboy/nightfall/issues/6) | Bake-off harness and corpus manifest (RFC 0001) | kernel | Not started |
| [#7](https://github.com/wckdboy/nightfall/issues/7) | Run the bake-off, write results into RFC 0001 | kernel | Not started |
| [#8](https://github.com/wckdboy/nightfall/issues/8) | XPC service spike: JSON IR → kernel → mesh | kernel | Not started |
| [#9](https://github.com/wckdboy/nightfall/issues/9) | Metal viewport spike showing the mesh | viewport | Not started |
| [#10](https://github.com/wckdboy/nightfall/issues/10) | Nightly ASan/UBSan workflow | ci | Not started |
| [#11](https://github.com/wckdboy/nightfall/issues/11) | Layering check: no AppKit below `app/` | ci | Not started |

## Log

- 2026-10-16 — Phase started. Workflow, RFC process and build skeleton proposed (0.1, 0.2).
