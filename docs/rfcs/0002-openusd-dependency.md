# RFC 0002: Adopt OpenUSD 25.11 (C++-only) as a dependency

- **Status:** Draft
- **Author(s):** wckdboy
- **Phase:** 0
- **Created:** 2026-10-17
- **Decided:** —

## Summary

Add OpenUSD 25.11, built C++-only (no Python, no imaging), as a pinned, optional
third-party dependency, together with usd-brep's USD modules. In Phase 0 it is used only to
load the 200 ABC STEP models into SMLib for the kernel bake-off (RFC 0001). It is the same
library the build plan relies on later for exact-solid storage (`.nightfall` packages,
usd-brep's `omniSolid` schema).

## Motivation

- **SMLib can only take external geometry through USD.** SM_API reads nothing but its native
  `.smb` format. usd-brep's import path is OCCT `.brep` → USD `BrepArray` → `SM_API_USD` →
  `SmBrep` (RFC 0001 F4). Without USD, the ABC half of the bake-off (#18) runs on OCCT only,
  and RFC 0001's like-for-like comparison breaks.
- **NVIDIA ships no macOS route.** usd-brep's Packman USD packages and its PyPI wheels exist
  for Linux and Windows only; Homebrew has no OpenUSD.
- **The build plan needs it anyway.** Exact solids are stored as USD with the `omniSolid`
  schema (Phase 3). Taking the dependency now moves that integration risk earlier.

The maintainer chose this route on 2026-10-17 over two alternatives (see below).

## Design

### What is built

| Component | Version | Built how | Links into Nightfall |
| --- | --- | --- | --- |
| OpenUSD | v25.11, commit `363a7c8d` (the release usd-brep 1.0.0 pins) | OpenUSD's `build_usd.py`: `--no-python --no-imaging --no-usdview --no-materialx` | Optional (`NF_WITH_USD`) |
| oneTBB | fetched by `build_usd.py` | as part of the OpenUSD build | via OpenUSD |
| usd-brep USD modules | usd-brep 1.0.0 (already pinned, #14) | our CMake, like SMLib | Optional (`NF_WITH_USD`) |

usd-brep modules: `BREP_USD_DATA`, `BREP_STAGING`, `OCCT_BREP_IMPORT`, `BREP_SM_USD`,
`SM_API_USD` (and `USD_BREP_UTILS` if they need it). None reference Python, and
`OCCT_BREP_IMPORT` is a standalone parser of OCCT's `.brep` format: it does not link OCCT.

### How it is used in Phase 0

```
ABC STEP ──OCCT STEPControl_Reader──▶ TopoDS_Shape ──BRepTools::Write──▶ .brep
.brep ──OCCT_BREP_IMPORT──▶ USD stage (BrepArray) ──SM_API_USD──▶ SmBrep
```

The bake-off's OCCT side keeps the `TopoDS_Shape` directly. Both kernels then run the same
operations on the same model, so translation stays out of the comparison, as RFC 0001
intends. Translation failures are recorded separately from modeling failures.

### Build, CI and caching

- `third_party/build-usd.sh`: pinned tag and commit, verified clean tree, install stamp, into
  the git-ignored `third_party/install/usd-25.11`. Same pattern as `build-occt.sh`.
- `NF_WITH_USD=ON` turns on the usd-brep USD modules and the bake-off's STEP path. It is off
  in the default and `dev` presets. A new `usd` workflow builds OpenUSD once on `main` and
  caches it, as `occt.yml` does (#26).
- No Nightfall code outside `third_party/` and `tools/bakeoff/` depends on USD until an RFC
  for the `.nightfall` format says so.

### Licence

OpenUSD is under the **Tomorrow Open Source Technology License 1.0 (TOST 1.0)**: Apache-2.0
with a different Section 6 (trademarks), stating that the licence grants no permission to
use the licensor's trade names or marks. All other terms match Apache-2.0. Bundled
components are MIT/BSD/zlib-style; oneTBB is Apache-2.0.

This RFC adds **TOST-1.0** to the allowed list in WORKFLOW.md §8. It is permissive and
compatible with distributing Nightfall under Apache-2.0. The only obligation it adds is
not using Pixar's or the "OpenUSD" marks beyond attribution.

## Alternatives considered

- **NVIDIA's usd-brep Python wheel on a Linux runner.** Fastest to set up, but the SMLib side
  would run through Python bindings while the OCCT side runs through our C++ harness. A
  different API surface weakens the comparison. Also Linux-only.
- **Defer the ABC corpus to Phase 3.** Decide the kernel on the 20-part corpus, memory
  safety and history support. This would amend RFC 0001's corpus *after* seeing partial
  results, which is exactly what RFC 0001 was written to prevent.
- **Write our own OCCT → SMLib translator.** Large, and it would test our translator as much
  as the kernel.

## Compatibility and migration

Nothing persisted exists. The dependency is opt-in (`NF_WITH_USD`), and the default build
and CI are unchanged apart from the new cached `usd` workflow.

## Testing and measurement

The build spike (`spike/usd-build`, not merged) records:

- OpenUSD build time and install size on an M-series Mac
- the usd-brep USD modules building with our CMake
- one ABC model loaded into SMLib through the pipeline above, with volume compared to OCCT

These results are appended here before the RFC moves to Review.

## Risks and open questions

- **Build cost:** OpenUSD is large. A C++-only build without imaging keeps it smaller, and CI
  caches it on `main`.
- **Translation losses:** `.brep` → USD → SMLib may fail or alter some models. The bake-off
  reports translation failures separately, and a model that fails to translate is excluded
  from both kernels' modeling scores, with the count published.
- **Version drift:** usd-brep pins 25.11. Upgrading OpenUSD waits for usd-brep to move.
