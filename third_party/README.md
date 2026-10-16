# Third-party dependencies

Policy: WORKFLOW.md §8. Every dependency is pinned, fetched reproducibly, and listed here.
Adding a row needs an accepted RFC (build-only test tools excepted).

## In use

| Name | Version | License | Used by | Why | Fetched by |
| --- | --- | --- | --- | --- | --- |
| Catch2 | v3.16.1 (`5010924c`) | BSL-1.0 | tests only | C++ unit and property tests | CMake FetchContent, pinned commit |
| OpenCASCADE | 7.9.3 (`a016080b`) | LGPL-2.1 (dynamic linking only) | `NF_WITH_OCCT=ON` only | Comparison/fallback kernel and STEP translator (RFC 0001) | `third_party/build-occt.sh`, used via `third_party/occt/CMakeLists.txt` |
| usd-brep (SMLib + SM_API) | 1.0.0 (`c9c5979f`) | Apache-2.0 | `NF_WITH_SMLIB=ON` only | Kernel candidate under evaluation (RFC 0001) | `third_party/fetch-smlib.sh`, built by `third_party/smlib/CMakeLists.txt` |

## Planned (pending RFCs)

| Name | License | Purpose | RFC |
| --- | --- | --- | --- |
| OpenUSD | Apache-2.0 (modified) | Scene and exact-geometry storage | — |
| planegcs | LGPL-2.1 | 2D sketch constraint solver | — |
| sucrase | MIT | TypeScript type stripping for Nightfall Script | — |
| Sparkle | MIT | Direct-download updates | — |

## Notes

- **usd-brep** is built with our own CMake instead of upstream's repo_man/premake/Packman
  toolchain (its macOS premake package is not publicly downloadable). Only the kernel
  (`SMLib`) and its C++ API (`SM_API`) are built: no USD, Python or TBB. Upstream's
  SM_API test suite runs unchanged as `smlib.SM_API_test`.
- **OpenCASCADE** is built by `third_party/build-occt.sh` with only the modeling and
  data-exchange toolkits (no Draw, Tcl/Tk, FreeType or OpenGL) and installed into the
  git-ignored `third_party/install/`. CI caches that install keyed on the script's hash.
