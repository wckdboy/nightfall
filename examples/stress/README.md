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

Build `nf_eclipse_probe` with the OCCT preset and run:

```sh
build/occt/services/kernel/nf_eclipse_probe examples/stress/eclipse.nightfall out/eclipse
```

The optional output directory must be new. A successful run writes the source, STEP,
actual C-bridge mesh buffers/manifest and a factual `receipt.json`. The receipt includes
input/executable/artifact digests, configured kernel identity, units in field names,
timings, actual topology/mesh counts and checked/unchecked scope. Hashes identify these
artifacts; they do not certify numerical uncertainty.

The private probe checks independent per-body analytic volume (0.02 mm³ absolute),
fourfold centroid symmetry (1e-6 mm), analytic foundation area (0.01 mm²), parameter
edit/restore, cache-clear rebuild, retained snapshots and current name ownership.
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

The next Eclipse run passes the deck-bolt check and fails the twisted-vane oracle:
1086259.4812360606 mm³ observed versus 1090379.9332788421 mm³ predicted. That oracle
assumes fixed correspondence between the three ellipse parameterizations; the loft's
automatic compatibility matching can split/reparameterize the sections. Investigation
[#236](https://github.com/wckdboy/nightfall/issues/236) must distinguish that assumption
from an implementation defect, using independent surface mathematics. The full cold
evaluation takes 70323.303 ms on an Apple M3 Pro, Release, uncontrolled host load.

Eclipse is **not yet a passing capability milestone**. The complete fixture, 0.02 mm³
budget, 120-second headless test bound and service deadlines remain unchanged. The probe
emits source/executable/kernel-bound JSON on failure and publishes no success receipt.
All later results must retain these failed cases and state their resolution explicitly.
