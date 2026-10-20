# Third-party dependencies

Policy: WORKFLOW.md §8. Every dependency is pinned, fetched reproducibly, and listed here.
Adding a row needs an accepted RFC (build-only test tools excepted).

## In use

| Name | Version | License | Used by | Why | Fetched by |
| --- | --- | --- | --- | --- | --- |
| Rust toolchain + standard library | 1.99.1 (`rust-toolchain.toml`) | MIT OR Apache-2.0; bundled component notices in the toolchain's `COPYRIGHT.html` / `COPYRIGHT-library.html` | Rust math (RFC 0008); rustfmt/Clippy build-only | Safe numerical geometry; no external Cargo crates in the first slice | rustup, exact toolchain pin; Cargo.lock committed |
| Catch2 | v3.16.1 (`5010924c`) | BSL-1.0 | tests only | C++ unit and property tests | CMake FetchContent, pinned commit |
| jsonschema | 4.26.0 (transitives pinned in `tools/schema-requirements.txt`) | MIT (including attrs, referencing, jsonschema-specifications, rpds-py) | tests only | Independent JSON Schema 2020-12 check of the IR corpus (RFC 0005) | pip in an isolated test environment; never linked or shipped |
| OpenCASCADE | 7.9.3 (`a016080b`) | LGPL-2.1 (dynamic linking only) | `NF_WITH_OCCT=ON` only | Comparison/fallback kernel and STEP translator (RFC 0001) | `third_party/build-occt.sh`, used via `third_party/occt/CMakeLists.txt` |
| OpenUSD | 25.11 (`363a7c8d`), C++-only | TOST-1.0 (Apache-2.0 with a trademark section; RFC 0002) | `NF_WITH_USD=ON` only | Only route for external geometry into SMLib; later exact-solid storage | `third_party/build-usd.sh` (`build_usd.py`; also fetches oneTBB, Apache-2.0) |
| usd-brep USD modules | 1.0.0 (`c9c5979f`) | Apache-2.0 | `NF_WITH_USD=ON` only | `.brep` → USD → SMLib (`BREP_USD_DATA`, `BREP_STAGING`, `OCCT_BREP_IMPORT`, `BREP_SM_USD`, `SM_API_USD`) | `third_party/usd-brep-usd/CMakeLists.txt` |
| usd-brep (SMLib + SM_API) | 1.0.0 (`c9c5979f`) | Apache-2.0 | `NF_WITH_SMLIB=ON` only | Kernel candidate under evaluation (RFC 0001) | `third_party/fetch-smlib.sh`, built by `third_party/smlib/CMakeLists.txt` |

## Planned (pending RFCs)

| Name | License | Purpose | RFC |
| --- | --- | --- | --- |
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
  git-ignored `third_party/install/`. CI caches that install keyed on the script and patch
  hashes. Nightfall's `occt/patches/0001-align-incremental-allocations.patch` fixes mixed-size
  incremental allocation alignment in the pinned 7.9.3 source (#174), with size-overflow
  rejection and no public ABI change. This modification retains OCCT's LGPL-2.1 license
  and exception; the complete patch is distributed here for reproducible rebuilds.
  The install receipt includes its SHA256, verified at configure time and included in the
  backend's cache identity. A checkout must be clean upstream or contain exactly this patch;
  unrelated source edits are rejected. The script preserves those edits on failure.
