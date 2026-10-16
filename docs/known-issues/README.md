# Known issues

Defects we have found (in our code or in dependencies) and chosen not to fix yet. Each
entry is a decision on record: what is broken, how to reproduce it, what we know about
the cause, and what will make us revisit it.

Statuses: **Deferred** (known, intentionally not acted on yet) · **Mitigated** (worked
around in Nightfall, upstream unchanged) · **Fixed** (kept for history).

| ID | Component | Summary | Status | Revisit |
| --- | --- | --- | --- | --- |
| [SMLIB-001](SMLIB-001-proto-topology-use-after-free.md) | SMLib 1.0.0 | Use-after-free in booleans on solids with closed edges | Deferred | Before RFC 0001 is accepted |
