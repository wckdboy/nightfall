# Modeling showcases

Choose **Examples → Asterion** in the macOS client to build the radial cooling module.
The source is [asterion.nightfall](asterion.nightfall): 28 features, eight live bodies and
80 closed solids, including 64 individually rounded fins, a turned hub, a drilled rounded
mount, mirrored stands, a guard ring and chamfered fasteners. These are exact B-rep solids;
the model has no assembly joints, materials, thermal analysis or simulation results.

Edit `fin_count`, `fin_height`, `fin_radius` or `module_count`, then press Return. The
single module is the default for visual inspection. Larger arrays deliberately stress
the current engine and can exceed the native service's five-second deadline.

```sh
build/occt/tools/cli/nf build examples/modeling/asterion.nightfall --out out/asterion.step
build/occt/tools/cli/nf build examples/modeling/asterion.nightfall --set module_count=8 --out out/asterion-array.step
build/occt/services/kernel/nf_showcase_bench examples/modeling/asterion.nightfall 5
```

The developer benchmark emits JSON lines with cold rebuild, warm rebuild, indexed-mesh
and C-bridge times. It uses the same 96-entry / 64 MiB estimated geometry cache and default
0.1 mm / 0.2 rad display settings as the service. It does not include XPC, client decoding,
GPU upload or rendering; C-bridge measurements include rebuild, meshing and manifest encoding.
Run it on a quiet machine, retain the output and distinguish medians from worst cases.

Regression tests integrate each component's primitive/frustum mass independently, exercise
parameter edits and cache reuse, reload exported STEP, and compare mesh volume within 1%.
Exact volume and area checks use an absolute tolerance of 1e-5 in mm units. The original
20 reference-part baselines remain unchanged.

[Rounded Plate](rounded_plate.nightfall) is a smaller example for exact corner fillets.
