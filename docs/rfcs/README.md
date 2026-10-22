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
| [0001](0001-kernel-selection.md) | Geometry kernel selection and bake-off | Accepted (OCCT) |
| [0002](0002-openusd-dependency.md) | Adopt OpenUSD 25.11 (C++-only) as a dependency | Accepted |
| [0003](0003-process-architecture.md) | Process architecture: the kernel in an XPC service | Accepted |
| [0004](0004-libnf-math.md) | libnf-math: a kernel-free NURBS math core | Accepted |
| [0005](0005-feature-ir.md) | Feature IR schema v1 | Accepted |
| [0007](0007-kernel-evaluator-contract.md) | Production kernel interface and incremental evaluator | Accepted |
| [0008](0008-rust-kernel.md) | Rust implementation of the Nightfall kernel and engine | Accepted |
| [0011](0011-ai-design-loop.md) | The AI design loop program | Accepted |
| [0012](0012-sketch-solver.md) | A permissive Rust 2D sketch constraint solver | Accepted (SolidCraft `sketch`) |
| [0013](0013-assemblies.md) | Assemblies in the feature IR | Accepted |
| [0014](0014-engineering-studies.md) | Engineering studies in the feature IR | Accepted |
| [0015](0015-external-kernel-protocol.md) | External kernel protocol (`nf kernel serve`) | Review |

RFC 0006 is reserved for Nightfall Script by RFC 0005 (#93). RFC 0009 (#229) and RFC 0010
(#247) are Review proposals in open PRs; RFC 0011 supersedes their direction.
