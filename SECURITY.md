# Security policy

Nightfall opens untrusted files (STEP, USD, SVG) and runs user scripts, so parser and
sandbox bugs matter.

Report vulnerabilities privately through GitHub's
[security advisories](https://github.com/wckdboy/nightfall/security/advisories/new), not
in public issues. We aim to acknowledge reports within 3 working days.

In scope: crashes or memory-safety bugs from crafted files, script sandbox escapes,
XPC service boundary bypasses, and leaks of API keys stored in the Keychain.

Bugs in upstream dependencies (SMLib/usd-brep, OpenCASCADE, OpenUSD) should also go to
their maintainers; we will help coordinate.
