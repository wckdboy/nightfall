# RFC 0004: libnf-math, a kernel-free NURBS math core

- **Status:** Review
- **Author(s):** wckdboy
- **Phase:** 1
- **Created:** 2026-10-17
- **Decided:** —

## Summary

libnf-math is Nightfall's own C++23 NURBS library: exact curve and surface types, their
evaluation and exact constructions, SVG conversion and equation curves. It depends on no
kernel and runs in **both** processes, the app (sketcher, SVG import, previews) and the
kernel service. OCCT (RFC 0001) does what needs topology or heavy numerics (booleans,
fillets, surface intersection, meshing) inside the service, where libnf-math types convert
to `Geom_BSpline*` without loss.

## Motivation

- **The build plan assumed libnf-math would wrap the winning kernel's math** (NLib if SMLib
  won, OCCT's `Geom` otherwise). RFC 0003 rules that out: the kernel lives in an XPC
  service, but the sketcher, SVG import, equation curves and live previews run in the app at
  interactive rates, with no IPC per mouse move.
- **The IR and the AI need geometry as plain data.** A sketch spline in `part.json`, a curve an
  agent writes in Nightfall Script, an SVG path: all of these are NURBS *data*, not kernel
  handles. They need one value type that both processes, Swift and the IR share.
- **Exactness is a product promise** ("math is the source of truth"). Owning the
  constructions (arcs as rational quadratics, SVG paths, polynomial equation curves) lets us
  test exactness ourselves.

## Design

### Scope: owned vs delegated

| Owned by libnf-math (both processes) | Delegated to OCCT (kernel service only) |
| --- | --- |
| Value types: `Vec2/3`, `Tolerance` (exist), `KnotVector`, `NurbsCurve2/3`, `NurbsSurface` (degree, knots, control points, weights) | Booleans, fillets, shells, sweeps (B-rep topology) |
| Evaluation + derivatives (Cox–de Boor; rational via the quotient rule), knot insertion, degree elevation, Bézier splitting, reparameterisation | Surface–surface intersection |
| Exact constructors: line, circular/elliptical arc (rational quadratic, ≤ 90° per span), full circle, polyline, Bézier → NURBS | Tessellation of B-reps (`BRepMesh`) |
| Fitting with error bounds: interpolation and least squares (equation curves, scan data) | STEP/IGES translation |
| Curve queries: closest point, curve–curve and curve–line intersection, adaptive polyline (for the 2D sketcher and SVG export) | Exact mass properties of solids |
| SVG path ↔ NURBS (exact for M/L/H/V/C/S/Q/T/A/Z); equation curves `x(t), y(t), z(t)` | |

A capability moves from the right column to the left only through an RFC.

### Representation

- Doubles, millimetres, radians (`Tolerance` as today). Homogeneous control points stored as
  `(w·x, w·y, w·z, w)`; the API takes and returns Euclidean points plus weights.
- Clamped (open uniform or non-uniform) knot vectors; periodic curves are represented
  clamped. Invariants (`knots.size() == points.size() + degree + 1`, non-decreasing,
  positive weights) are checked at construction; invalid data is a constructor error, never
  undefined behaviour.
- Values are immutable and cheap to copy (shared storage). Operations return new values.
- **Conversion to OCCT** (`Geom_BSplineCurve`/`Surface`, `Geom2d_BSplineCurve`) lives in
  `core/kernel`, not here, and is lossless in both directions (tested round-trip).

### Exactness contract

Every constructor documents whether it is **exact** (the NURBS reproduces the shape
mathematically: lines, conics, polynomials, every SVG command) or **approximate** (fitted:
transcendental equation curves, scan data). Approximate results carry their measured maximum
deviation. Tests check exact constructors analytically, e.g. every arc point at distance r
from the centre within 1e-12 relative.

### Exposure

- C++23 API in `core/math/include/nf/math/`, header-light, no allocation in evaluation hot
  paths.
- Swift sees it through Swift 6 C++ interop on a small, value-typed facade (no templates in
  the interop surface). The Nightfall Script `nf.curve` API binds the same facade.
- The IR serialises NURBS as plain JSON (degree, knots, points, weights), defined by the IR
  RFC.

### Testing standard

Unit tests plus property tests (fixed seed): partition of unity, derivatives against central
finite differences within 1e-8, knot insertion and degree elevation leave the curve
unchanged to 1e-12, a 200-file SVG corpus round-trips exactly. Tests run under ASan/UBSan
nightly. Algorithms follow *The NURBS Book* (Piegl & Tiller), cited by algorithm number in
the code.

## Alternatives considered

- **Wrap OCCT's `Geom` (the build plan's fallback).** Would put OCCT in the app process
  against RFC 0003, and kernel handles are not plain data for the IR or the AI.
- **Use SMLib's NLib (Apache-2.0, open) as the math core** while OCCT does topology. A good
  library, but it drags the SMLib context and macros into the app, and it lost the bake-off's
  robustness tests. Its algorithms remain a reference.
- **Third-party header-only NURBS libraries (e.g. tinynurbs, MIT).** Smaller than what we need
  (no exact SVG, fitting with bounds, or rational intersection), and would still need the
  same tests. We may borrow ideas under their licences.

## Compatibility and migration

Existing `Vec2/3` and `Tolerance` stay. Nothing persisted exists yet.

## Testing and measurement

Phase 1 gate (build plan): the SVG corpus round-trips exactly, and the property tests above
pass. Performance target: evaluating a cubic NURBS point ≤ 50 ns on an M1 (sketcher budget).

## Risks and open questions

- The scope is large; the build order in the Phase 1 tracker puts evaluation and exact
  constructors first, fitting and intersection after.
- Robust curve–curve intersection is hard; we follow Bézier clipping with subdivision and
  test against OCCT's results in the kernel service as an oracle.
