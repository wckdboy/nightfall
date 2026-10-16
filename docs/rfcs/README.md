# RFCs

An RFC records a decision before code depends on it. WORKFLOW.md §3 lists what needs one.

## Process

1. Copy `0000-template.md` to `NNNN-short-title.md` with the next free number.
2. Open a PR titled `rfc: NNNN short title` with status `Review`.
3. Review stays open for at least 3 working days. Two CODEOWNER approvals accept it.
4. On merge, set the status to `Accepted` (or `Rejected`, kept for the record).
5. An accepted RFC is never rewritten. Change it with a new RFC that supersedes it.

## Index

| RFC | Title | Status |
| --- | --- | --- |
| [0001](0001-kernel-selection.md) | Geometry kernel selection and bake-off | Draft |
| [0002](0002-openusd-dependency.md) | Adopt OpenUSD 25.11 (C++-only) as a dependency | Accepted |
| [0003](0003-process-architecture.md) | Process architecture: the kernel in an XPC service | Review |
