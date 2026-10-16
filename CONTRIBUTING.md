# Contributing to Nightfall

Thanks for helping build open CAD. Nightfall is large and its parts depend on each other,
so every contribution follows one workflow: [docs/process/WORKFLOW.md](docs/process/WORKFLOW.md).

## Quick path

1. **Find or open an issue.** Each PR closes exactly one issue in the current phase.
2. **Big change?** Anything architectural, any file-format or API change, any new
   dependency starts as an [RFC](docs/rfcs/README.md).
3. **Branch:** `<type>/<issue>-<slug>`, e.g. `fix/57-arc-weight-sign`.
4. **Build and test:** `tools/check.sh` must pass locally.
5. **Commit:** Conventional Commits, signed off: `git commit -s -m "fix(svg): …"`.
6. **Open a PR** and fill in the template, including the evidence section.

## Developer Certificate of Origin

We use the [DCO](https://developercertificate.org/) instead of a CLA: you keep your
copyright and certify that you have the right to contribute the code. Sign every commit
with `git commit -s`, which adds:

```
Signed-off-by: Your Name <you@example.com>
```

CI rejects PRs with unsigned commits.

## Source file headers

Every source file starts with an SPDX header:

```cpp
// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
```

## Code of conduct

Be kind, assume good faith, and keep discussion about the work. Maintainers may remove
comments or contributors that make the project hostile to others.
