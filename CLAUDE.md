# Nightfall — instructions for AI agents

Open-source (Apache-2.0), AI-native parametric CAD for macOS. NURBS geometry on NVIDIA's
SMLib kernel (usd-brep), feature-IR source of truth, direct-modeling UX with history.

The process is in `docs/process/WORKFLOW.md`. It is binding. The short version:

## Before writing code

1. Find the issue you are working on and its phase in `docs/phases/`. No issue, no code:
   ask the human to create one, or propose its text.
2. If the work touches anything in WORKFLOW.md §3 (module boundaries, IR or file format,
   `IKernel`, script API, agent tools, dependencies, roadmap), check that an **Accepted**
   RFC covers it in `docs/rfcs/`. If none does, stop and draft an RFC instead.
3. Work on a branch named `<type>/<issue>-<slug>`. Never commit to `main`, never push
   without being asked, never merge.

## While writing code

- Stay inside the issue's scope. Note unrelated problems for the human; don't fix them.
- Keep the PR under 400 changed lines excluding tests. Propose a split if it won't fit.
- Tests first for `core/` code. Every tolerance in a test is explicit.
- C++23 in `core/`, Swift 6 strict concurrency in `app/`. No AppKit outside `app/`.
- No new dependency without an accepted RFC and a `third_party/README.md` entry.
- Match the surrounding code's style and comment density.

## Before finishing

- Run `tools/check.sh`. It must pass. Report its real output; never claim green without it.
- Commit with `git commit -s` and a Conventional Commit message
  (`feat(math): …`, scopes listed in WORKFLOW.md §2).
- Fill in the PR template, including the evidence section, and tick only what's true.
- Update the phase tracker or RFC status if the change affects them.

## Key decisions already made (see RFCs and the build plan)

- Kernel: SMLib via usd-brep is the lead candidate; OCCT is fallback and STEP translator.
  Final choice comes from the Phase 0 bake-off (RFC 0001).
- Feature IR (JSON, versioned) is the source of truth; geometry is a cache.
- References to faces and edges are selectors, never indices.
- The kernel runs out of process (XPC). Feature code never includes kernel headers.
- AI edits the IR through typed tools; providers are swappable; MCP is exposed via `nf mcp`.
