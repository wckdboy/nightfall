# Development workflow

Nightfall is a multi-year project with a geometry kernel, a native app, a file format
and an AI agent that all depend on each other. This workflow exists so that every
change is small, reviewed, tested, traceable to a decision, and reversible.

If a rule here blocks you, propose a change to this file in a PR. Do not work around it.

## 1. The chain of record

Every line of code traces back through this chain:

```
Build plan (phases + gates)
  └─ RFC            why + what, for anything architectural          docs/rfcs/NNNN-*.md
       └─ Issue     one reviewable unit of work, in a phase milestone   GitHub Issues
            └─ Branch + PR   the change, its tests, its evidence          GitHub PRs
                 └─ Squash commit on main                                 git history
```

- **Phase:** a milestone (`Phase 0`, `Phase 1`, …). Every issue belongs to exactly one.
- **RFC:** required before any work in the "needs an RFC" list (§3).
- **Issue:** required for every PR except typo-level docs fixes.
- **PR:** closes exactly one issue (`Closes #N`).

## 2. Branches and commits

- `main` is protected: no direct pushes, linear history, squash-merge only, required
  checks green, at least one approving review from a CODEOWNER.
- Branch names: `<type>/<issue>-<slug>`, e.g. `feat/42-svg-arc-import`.
- Commit messages and PR titles follow [Conventional Commits](https://www.conventionalcommits.org):
  `<type>(<scope>): <summary>`.
  - **types:** `feat` `fix` `perf` `refactor` `test` `docs` `build` `ci` `chore` `rfc`
  - **scopes:** `math` `svg` `ir` `kernel` `usd` `script` `app` `viewport` `sketch`
    `agent` `cli` `evals` `third_party` `process`
  - Breaking change to the IR, file format or a public API: `!` after the scope, plus a
    `BREAKING CHANGE:` footer with the migration.
- Every commit carries a DCO sign-off (`git commit -s`).
- Branches live for days, not weeks. Rebase on `main` and do not merge `main` into a branch.

## 3. What needs an RFC

Write an RFC (template: `docs/rfcs/0000-template.md`) before starting any of:

- A new module, service or process boundary, or a change to one
- The feature IR schema, the `.nightfall` format, or any persisted data
- The `IKernel`, `nf` script API, agent tool schemas or MCP surface
- Adding, replacing or upgrading a major version of a third-party dependency
- Anything that changes a phase gate, the roadmap or a principle

RFC lifecycle: `Draft` → `Review` (PR open, at least 3 working days) → `Accepted` or
`Rejected` → later `Superseded by NNNN`. An accepted RFC is changed only by a new RFC.

## 4. Pull request rules

| Rule | Limit |
| --- | --- |
| Size | ≤ 400 changed lines excluding tests, fixtures and generated files. Split larger work into stacked PRs. |
| Scope | One issue, one concern. A drive-by refactor gets its own PR. |
| Tests | Every behavior change ships with tests that fail without it. |
| Evidence | Geometry changes attach numbers (volumes, tolerances, timings); UI changes attach a screenshot or recording. |
| Checks | CI green: build, tests, format, license headers, DCO, PR title. |
| Review | One CODEOWNER approval; two for anything under §3. |

The PR template's checklist is the definition of done (§7). Unticked boxes block merge.

**While there is a single maintainer**, GitHub cannot require an approving review (authors
can't approve their own PRs). Until a second maintainer joins: required status checks
stay enforced, the maintainer reviews every PR in the GitHub diff view before merging
(never from the terminal), agent-authored PRs get a written review comment, and §3
changes wait the full RFC review period. Required approvals switch on the day a second
maintainer is added.

## 5. Phase gates

Each phase in the build plan ends with a gate: measurable exit criteria.

- The gate lives in `docs/phases/phase-N.md` with its criteria, the evidence and a sign-off.
- Work that depends on a later phase does not start until the earlier gate passes,
  unless the build plan explicitly overlaps the phases.
- A failed gate produces a short write-up in the phase file and either a fix plan or an
  RFC that changes the plan. Gates are never quietly skipped.

## 6. Quality bars by area

| Area | Required |
| --- | --- |
| `core/math` | Unit + property tests; tolerances explicit in every assertion; no allocation in evaluation hot paths. |
| `core/kernel` | Runs against every `IKernel` backend; golden results (volume, area, topology counts) with stated tolerance; validity check after every operation. |
| `core/ir` | Schema version bump + migration + round-trip test for any persisted change. |
| `app` | No AppKit outside `app/`; main-thread work < 16 ms per frame while dragging. |
| `agent` | Eval suite run attached to the PR for any prompt, tool or provider change; pass rate may not drop. |
| `third_party` | License on the allowlist (§8), version pinned, entry in `third_party/README.md`. |

Sanitizer builds (ASan + UBSan) run nightly. A sanitizer failure is a release blocker.

## 7. Definition of done

A change is done when all of these hold:

- [ ] Linked to an issue in the current phase milestone
- [ ] Tests added or updated and passing locally (`tools/check.sh`)
- [ ] Public APIs documented in their headers
- [ ] Docs, RFC status or phase tracker updated if behavior or plans changed
- [ ] No new warnings; no TODO without an issue number (`TODO(#123)`)
- [ ] CI green and review approved

## 8. Dependencies

- **Allowed:** Apache-2.0, MIT, BSD-2/3, ISC, Zlib, BSL-1.0, Unicode, CC0, CC-BY (data/docs).
- **Allowed with an RFC:** LGPL, only when dynamically linked and optional (OCCT, planegcs).
- **Not allowed:** GPL, AGPL, SSPL, BUSL, "non-commercial" licenses, unlicensed code.
- Every dependency is pinned to an exact version or commit, fetched reproducibly, and
  recorded in `third_party/README.md` with its license and why it is here.

## 9. Releases and versioning

- SemVer. `0.x` until the public beta; minor versions may break APIs before 1.0.
- The IR schema and `.nightfall` format carry their own version numbers, independent of
  the app, and every version has a migration from the previous one.
- Releases are cut from tags on `main`. Changelogs are generated from Conventional Commits.

## 10. Working with AI agents

AI agents (Claude Code and others) write much of this codebase. They follow the same
workflow as humans, plus the rules in [`CLAUDE.md`](../../CLAUDE.md):

- One issue per session and per branch. Agents never push to `main` or merge PRs.
- The agent states its plan in the PR description before or with the code.
- Agents do not add dependencies, change persisted formats or touch §3 areas without an
  accepted RFC.
- Humans review agent PRs with the same bar as human PRs. An agent PR is not merged on
  green CI alone.
