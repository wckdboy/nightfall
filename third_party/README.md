# Third-party dependencies

Policy: WORKFLOW.md §8. Every dependency is pinned, fetched reproducibly, and listed here.
Adding a row needs an accepted RFC (build-only test tools excepted).

## In use

| Name | Version | License | Used by | Why | Fetched by |
| --- | --- | --- | --- | --- | --- |
| Catch2 | v3.16.1 (`5010924c`) | BSL-1.0 | tests only | C++ unit and property tests | CMake FetchContent, pinned commit |

## Planned (pending RFCs)

| Name | License | Purpose | RFC |
| --- | --- | --- | --- |
| usd-brep (SMLib) | Apache-2.0 | Primary kernel candidate | 0001 |
| OpenCASCADE 7.8 | LGPL-2.1 | Comparison/fallback kernel, STEP translator | 0001 |
| OpenUSD | Apache-2.0 (modified) | Scene and exact-geometry storage | — |
| planegcs | LGPL-2.1 | 2D sketch constraint solver | — |
| sucrase | MIT | TypeScript type stripping for Nightfall Script | — |
| Sparkle | MIT | Direct-download updates | — |
