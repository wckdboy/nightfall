# Headless kernel stress references

These fixtures exercise the current C++ evaluator and dynamically linked OCCT solver.
They are geometric studies, with no claim of constrained assemblies, collision freedom,
engineering simulation or an independent Rust solid kernel. See the intermediate goals
in [#232](https://github.com/wckdboy/nightfall/issues/232).

## Eclipse workcell (#225)

Regenerate the complete fixture with `python3 tools/make_eclipse.py`. It retains the
Nocturne component family and adds four manipulators around a turned rotor, twelve
twisted loft vanes, drilled flanges, chamfered hardware, swept services and a connected
spoke foundation: **113 features, 32 live bodies, 529 solids** at default parameters.

Build the private probes with the OCCT preset and run:

```sh
cmake --build --preset occt --target nf_eclipse_probe nf_loft_reference_probe
python3 services/kernel/tests/eclipse_reference.py \
  build/occt/services/kernel/nf_eclipse_probe \
  build/occt/services/kernel/nf_loft_reference_probe . out/eclipse
```

The optional output directory must be new. A successful run writes `verification.json`,
the independent native-patch/Rust integration evidence under `loft-reference/`, and
source, STEP, actual C-bridge mesh buffers/manifest and `receipt.json` under `model/`.
The receipt includes
input/executable/artifact digests, configured kernel identity, units in field names,
timings, actual topology/mesh counts and checked/unchecked scope. Hashes identify these
artifacts; they do not certify numerical uncertainty.

The driver checks exact equality of the minimal vane's section inputs, parameters and
units with the full source, and its twelve-copy pattern contract. Rust independently
integrates the native-generated B-spline control nets, with knot-split Gauss–Legendre-8
refinement through 1/2/4/8/16 subcells and a 1e-6 convergence check. The default and edited
reference values are bound to source/executable digests and the current kernel identity.
This verifies actual generated surfaces; fixed authored-point correspondence is unconstrained.

The private probe checks independent per-body volume (0.02 mm³ absolute): analytic
formulas, converged Simpson arc lengths for swept cables, and Rust seed volume ×12 for vanes.
It also checks
fourfold centroid symmetry (1e-6 mm), analytic foundation area (0.01 mm²), parameter
edit/restore, cache-clear rebuild, unchanged retained-body B-rep digests and current name ownership.
STEP reload uses closed-solid admission plus separate BRepGProp integration (0.05 mm³
and mm² aggregate comparison); it is the same vendor, not independent-kernel validation.
Every output face/index range and outward triangle is checked; each body's mesh V/A
must be within 1% of exact-surface measures. The existing million-triangle budget stays.
The probe runs directly headlessly; it does not establish the five-second service gate.

### Failures retained before acceptance

The first authored model expected 112 features. One operation patterned all 21 robot
bodies four times and correctly failed the 64-instance per-feature limit. Two batches
preserve the complete geometry and raise the authored count to 113. No limit changed.

The next run failed the analytic deck-bolt volume: 21764.035312 mm³ observed versus
21762.943285 mm³ expected. A minimal translated annulus/chamfer reproduction also differs
after independent STEP integration. The isolated correction in
[PR #235](https://github.com/wckdboy/nightfall/pull/235) restores the analytic result
without changing the roundoff bound or existing mesh goldens. Its complete local gate
passes 890/890; focused ASan/UBSan checks pass 1582 assertions across 15 cases.

The next historical Eclipse run passes the deck-bolt check and fails the twisted-vane oracle:
1086259.4812360606 mm³ observed versus 1090379.9332788421 mm³ predicted. That oracle
assumes fixed correspondence between the three ellipse parameterizations; the loft's
automatic compatibility matching can split/reparameterize the sections. Investigation
[#236](https://github.com/wckdboy/nightfall/issues/236) verifies the actual native surface
data with owned Rust mathematics and rejects the fixed-parameter assumption. The original
formula's default/edited predictions remain in each full receipt as diagnostics. The old full cold
evaluation takes 70323.303 ms on an Apple M3 Pro, Release, uncontrolled host load.

[#239](https://github.com/wckdboy/nightfall/issues/239) subsequently conditions the common
shell integration reference outside the bounding box, correcting excessive near-zero-flux
Gauss–Kronrod refinement. Its paired traced full evaluations fall from 59.859 s to 11.305 s.
The complete fixture, 0.02 mm³ budget, 120-second headless test bound and service deadlines
remain unchanged. Headless acceptance uses `ctest --preset occt -R '^evaluator.eclipse'`;
the probe emits source/executable/kernel-bound JSON on failure and publishes a success
receipt only after all checks pass. Timing samples have uncontrolled host load.

With the valid Rust oracle integrated, the first complete run passes the numerical,
edit/restore/cache and STEP checks, then exposes
[#242](https://github.com/wckdboy/nightfall/issues/242): the bridge rejects vane tessellation
because mesh normals disagree with triangle winding. The source/executable-bound failure
and independent vane evidence are retained in `out/kernel-evidence-gh225-verified/`.
Full boundary-mesh acceptance remains pending that isolated kernel correction.
