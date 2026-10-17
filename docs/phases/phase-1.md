# Phase 1 — libnf-math: exact NURBS curves, SVG and surfaces

- **Weeks:** 5–10
- **Milestone:** `Phase 1` on GitHub
- **Status:** In progress
- **Design:** [RFC 0004](../rfcs/0004-libnf-math.md)

## Gate

All of these, with evidence linked:

- [x] A 200-file SVG corpus round-trips exactly (import → NURBS → export → import, control
      points equal within 1e-12 relative): 200 generated documents covering every command,
      shape, transform function, unit and aspect mode; lines and Béziers bit for bit, arcs
      within 1e-12 (`core/svg/tests/corpus_tests.cpp`, #84)
- [x] Property tests pass (fixed seed): partition of unity, derivatives vs central differences,
      knot insertion and degree elevation leave the curve unchanged to 1e-12; ASan/UBSan clean
      (`tools/check.sh --preset asan`, every PR)
- [x] Cubic NURBS point evaluation ≤ 50 ns on Apple Silicon: 39.9 ns non-rational, 28.7 ns
      rational, M-series (`nf_math_bench`, #84)
- [x] libnf-math curves and surfaces convert to OCCT `Geom` and back without loss (#79)
- [ ] Swift reads and evaluates a curve through the facade

## Work items (build order)

Tracked as GitHub issues in the [`Phase 1` milestone](https://github.com/wckdboy/nightfall/milestone/2).
Evaluation and exact constructors come first; fitting and intersection after (RFC 0004, Risks).

| Issue | Item | Scope | Status |
| --- | --- | --- | --- |
| [#42](https://github.com/wckdboy/nightfall/issues/42) | RFC 0004: libnf-math scope | math | Done (#43) |
| [#44](https://github.com/wckdboy/nightfall/issues/44) | 1.1 Knot vectors and B-spline basis functions | math | Done (#57) |
| [#45](https://github.com/wckdboy/nightfall/issues/45) | 1.2 NURBS curves: evaluation and derivatives | math | Done (#58) |
| [#46](https://github.com/wckdboy/nightfall/issues/46) | 1.3 Exact constructors: lines, arcs, circles, polylines, Bézier | math | Done (#59) |
| [#47](https://github.com/wckdboy/nightfall/issues/47) | 1.4 Knot insertion, degree elevation, splitting | math | Done (#60) |
| [#48](https://github.com/wckdboy/nightfall/issues/48) | 1.5 SVG path data as exact NURBS | svg | Done (#62) |
| [#61](https://github.com/wckdboy/nightfall/issues/61) | 1.5b SVG documents: shapes, transforms, units, viewBox | svg | Done (#65–#67) |
| [#49](https://github.com/wckdboy/nightfall/issues/49) | 1.6 Planar regions from SVG (fill rules) | math | Done (#76, #77) |
| [#74](https://github.com/wckdboy/nightfall/issues/74) | 1.6b Coincident edges in planar arrangements | math | |
| [#50](https://github.com/wckdboy/nightfall/issues/50) | 1.7 SVG export | svg | Done (#64) |
| [#63](https://github.com/wckdboy/nightfall/issues/63) | 1.7b DXF R12 export | svg | Done (#83) |
| [#51](https://github.com/wckdboy/nightfall/issues/51) | 1.8 Equation curves | math | Done (#81, #82) |
| [#52](https://github.com/wckdboy/nightfall/issues/52) | 1.9 Curve fitting with error bounds | math | Done (#73) |
| [#53](https://github.com/wckdboy/nightfall/issues/53) | 1.10 Curve queries: closest point, intersections, polylines | math | Done (#71, #72) |
| [#54](https://github.com/wckdboy/nightfall/issues/54) | 1.11 NURBS surfaces: evaluation and constructors | math | Done (#78) |
| [#55](https://github.com/wckdboy/nightfall/issues/55) | 1.12 Lossless conversion to and from OCCT `Geom` | kernel | Done (#79) |
| [#56](https://github.com/wckdboy/nightfall/issues/56) | 1.13 Swift facade for libnf-math | app | Next |
| [#84](https://github.com/wckdboy/nightfall/issues/84) | 1.14 Gate: SVG corpus round trip, evaluation benchmark | svg | In review |

## Log

- 2026-10-17 — Phase started alongside the Phase 0 gate (RFC 0001, #41). RFC 0004 proposed:
  libnf-math is kernel-free and runs in both processes.
- 2026-10-17 — Basis (#57), curves (#58) and exact constructors (#59) up for review. Rational
  cubic point evaluation measures 29.6 ns (target ≤ 50 ns).
- 2026-10-17 — RFC 0004 accepted after RFC 0001's decision merged (#41). Refinement (#60) and
  SVG path data (#62) up for review; SVG documents split out as #61.
- 2026-10-17 — Phase 0 closed. Basis, curves, constructors, refinement and SVG path data
  merged after Codex review (11 findings fixed, incl. overflow safety and exact weight
  normalisation). SVG export (#64), the XML reader (#65), shared number grammar (#66) and SVG documents (#67) up for
  review; DXF split out as #63. Import convention: the root svg's user-space origin is the
  model origin, so Nightfall's own SVG round-trips unchanged.
- 2026-10-17 — SVG documents, export, closest point, polylines, intersections and fitting
  merged, each after Codex review (27 more findings fixed: rigorous approximation bounds,
  piece-local tolerances, overflow safety, XML strictness). Planar arrangement (#76) and
  regions from fill rules (#77) up for review; coincident edges deferred to #74.
- 2026-10-17 — Regions, surfaces, OCCT conversion, equation curves and DXF merged after
  review. The gate's SVG corpus caught two exactness bugs in the reader (root sizes detoured
  through pixels; clamped rounded rects leaving zero-length sides); fixed with it (#84). Four
  of five gate items met; the Swift facade (#56) remains.
