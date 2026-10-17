# Phase 1 — libnf-math: exact NURBS curves, SVG and surfaces

- **Weeks:** 5–10
- **Milestone:** `Phase 1` on GitHub
- **Status:** In progress
- **Design:** [RFC 0004](../rfcs/0004-libnf-math.md)

## Gate

All of these, with evidence linked:

- [ ] A 200-file SVG corpus round-trips exactly (import → NURBS → export → import, control
      points equal within 1e-12 relative)
- [ ] Property tests pass (fixed seed): partition of unity, derivatives vs central differences,
      knot insertion and degree elevation leave the curve unchanged to 1e-12; ASan/UBSan clean
- [ ] Cubic NURBS point evaluation ≤ 50 ns on Apple Silicon
- [ ] libnf-math curves and surfaces convert to OCCT `Geom` and back without loss
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
| [#61](https://github.com/wckdboy/nightfall/issues/61) | 1.5b SVG documents: shapes, transforms, units, viewBox | svg | In review (#65–#67) |
| [#49](https://github.com/wckdboy/nightfall/issues/49) | 1.6 Planar regions from SVG (fill rules) | svg | |
| [#50](https://github.com/wckdboy/nightfall/issues/50) | 1.7 SVG export | svg | In review (#64) |
| [#63](https://github.com/wckdboy/nightfall/issues/63) | 1.7b DXF R12 export | svg | |
| [#51](https://github.com/wckdboy/nightfall/issues/51) | 1.8 Equation curves | math | |
| [#52](https://github.com/wckdboy/nightfall/issues/52) | 1.9 Curve fitting with error bounds | math | |
| [#53](https://github.com/wckdboy/nightfall/issues/53) | 1.10 Curve queries: closest point, intersections, polylines | math | |
| [#54](https://github.com/wckdboy/nightfall/issues/54) | 1.11 NURBS surfaces: evaluation and constructors | math | |
| [#55](https://github.com/wckdboy/nightfall/issues/55) | 1.12 Lossless conversion to and from OCCT `Geom` | kernel | |
| [#56](https://github.com/wckdboy/nightfall/issues/56) | 1.13 Swift facade for libnf-math | app | |

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
