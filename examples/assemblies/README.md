# Assemblies

RFC 0013 assembly documents: components, occurrences and joints in one `.nightfall` file. The
headless engine and `nf mcp` evaluate them; the macOS app does not open assemblies yet.

## Strandbeest

[strandbeest.nightfall](strandbeest.nightfall) is Theo Jansen's walking linkage: four legs on a
two-throw crankshaft, the front pair half a turn ahead of the back pair.

| | |
| --- | --- |
| Components | 5: frame, crankshaft, link, rocker, foot |
| Occurrences | 26, one grounded |
| Joints | 37 revolute, 12 closed loops, 0 degrees of freedom |
| Parameters | Jansen's holy numbers `a` to `m`, the crank angle `crank`, and part sizes |
| Materials | Aluminium plates (2700 kg/m³), steel crankshaft (7850), nylon feet (1150): 378 g |

Every link outline is drawn roughly and solved from its constraints: the two-hole links are
two arcs and two tangent lines around the pin holes, and the rocker and foot are rounded
triangles whose sides are distance constraints driven by `b`, `d`, `e` and `g`, `h`, `i`.
Mirrored legs reuse the same components with `hand` set to -1. Joint frames are the pin
holes' axes, stacked 5 mm apart by joint offsets: the rocker, lower link, thigh and foot sit
nearest the frame so the hip pins stop short of the crank links. A `sweep_joint` motion
study found that the first stacking drove each lower crank link through a hip pin for about
40 degrees of every turn; with this one, no two parts overlap or touch over a full turn. The back half mirrors the front about the frame's
mid-plane, so `mass_properties` puts the centre of mass on that plane, z = 2 mm, at every pose. The parts also use `sqrt` expressions,
booleans, a circular body pattern and fillets.

`crank` drives the walk. Over a full turn every toe lands on Jansen's closed-form path within
1e-9 mm (`services/kernel/tests/strandbeest_tests.cpp`). Change a holy number and the outlines,
frames and solve all follow.

Drive it with an agent:

```sh
claude mcp add strandbeest -- build/occt/tools/cli/nf mcp --in examples/assemblies/strandbeest.nightfall
```

Then ask, for example, to set `crank` to 120 deg and measure `foot_fl/plate`, or to export
the assembly with `export_mesh`.
