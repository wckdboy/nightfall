# Assemblies

RFC 0013 assembly documents: components, occurrences and joints in one `.nightfall` file. The
headless engine and `nf mcp` evaluate them; the macOS app does not open assemblies yet.

## Strandbeest

[strandbeest.nightfall](strandbeest.nightfall) is Theo Jansen's walking linkage: four legs on a
two-throw crankshaft, the front pair half a turn ahead of the back pair. It stands with z up and
walks in the x-z plane, so the app's viewport shows it upright.

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
mid-plane, so `mass_properties` puts the centre of mass on that plane, y = -2 mm, at every pose. The parts also use `sqrt` expressions,
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

## Six-joint arm

[arm.nightfall](arm.nightfall) is a serial arm of seven parts and six revolute joints: base
yaw, shoulder, elbow, wrist roll, wrist pitch and tool roll, driven by parameters `q1` to `q6`.
Each joint mates two boss faces, so its axis is their normal through their centroid
(`plane_centroid`); the shoulder is limited to ±120°. It is the URDF export's reference: at
several poses, forward kinematics from the exported URDF alone reproduces every part's solved
placement within 4e-15 m (`tools/cli/tests/urdf_tests.py`), and MuJoCo 3.16 loading the same
file agrees.

```sh
build/occt/tools/cli/nf mcp --in examples/assemblies/arm.nightfall
# then: export_urdf {"path": "arm.urdf"}
```

