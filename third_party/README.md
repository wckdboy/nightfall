# Third-party dependencies

Policy: WORKFLOW.md §8. Every dependency is pinned, fetched reproducibly, and listed here.
Adding a row needs an accepted RFC (build-only test tools excepted).

## In use

| Name | Version | License | Used by | Why | Fetched by |
| --- | --- | --- | --- | --- | --- |
| Catch2 | v3.16.1 (`5010924c`) | BSL-1.0 | tests only | C++ unit and property tests | CMake FetchContent, pinned commit |
| usd-brep (SMLib + SM_API) | 1.0.0 (`c9c5979f`) | Apache-2.0 | `NF_WITH_SMLIB=ON` only | Kernel candidate under evaluation (RFC 0001) | `third_party/fetch-smlib.sh`, built by `third_party/smlib/CMakeLists.txt` |

## Planned (pending RFCs)

| Name | License | Purpose | RFC |
| --- | --- | --- | --- |
| OpenCASCADE 7.8 | LGPL-2.1 | Comparison/fallback kernel, STEP translator | 0001 |
| OpenUSD | Apache-2.0 (modified) | Scene and exact-geometry storage | — |
| planegcs | LGPL-2.1 | 2D sketch constraint solver | — |
| sucrase | MIT | TypeScript type stripping for Nightfall Script | — |
| Sparkle | MIT | Direct-download updates | — |

## Notes

- **usd-brep** is built with our own CMake instead of upstream's repo_man/premake/Packman
  toolchain (its macOS premake package is not publicly downloadable). Only the kernel
  (`SMLib`) and its C++ API (`SM_API`) are built: no USD, Python or TBB. Upstream's
  SM_API test suite runs unchanged as `smlib.SM_API_test`.
