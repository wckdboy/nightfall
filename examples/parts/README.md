# Reference parts

Twenty editable v1 `.nightfall` documents for the Phase 2 reference suite (#95). These are
complete parametric inputs for the production headless evaluator. Native app opening, the
`nf build` CLI and Nightfall Script are still pending; use the test runner below for now.

Each file has a descriptive feature history and a named, bounded parameter in millimetres.
Edit its `params[0].value` to change the model. All inputs are self-contained and use the
existing schema; no external assets, meshes or saved geometry are needed.

| Part | Varied parameter: default → test edits (mm) | Construction |
| --- | --- | --- |
| [Mounting plate](mounting_plate.nightfall) | Thickness: 4 → 2, 7.5 | Plate with four circular through-holes |
| [Washer](washer.nightfall) | Thickness: 2 → 1, 4 | Concentric circular profile |
| [Hex spacer](hex_spacer.nightfall) | Height: 12 → 6, 18 | Regular hexagon and circular bore |
| [Angle bracket](angle_bracket.nightfall) | Width: 30 → 20, 45 | Joined extrusions on orthogonal planes |
| [U-channel](channel.nightfall) | Length: 50 → 25, 75 | Three rectangles with shared boundaries |
| [Enclosure](enclosure.nightfall) | Height: 20 → 12, 28 | Pocket cut with 3 mm walls and floor |
| [Stepped shaft](stepped_shaft.nightfall) | Length: 30 → 20, 45 | Revolved stepped section |
| [Flanged bushing](flanged_bushing.nightfall) | Height: 15 → 10, 22 | Revolved flange, sleeve and bore |
| [Conical nozzle](conical_nozzle.nightfall) | Height: 20 → 12, 30 | Tapered revolution with straight bore |
| [Spherical knob](spherical_knob.nightfall) | Radius: 10 → 7, 12 | Revolved semicircle and translation |
| [Toroidal seal](toroidal_seal.nightfall) | Section radius: 3 → 2, 4 | Circular section revolved about an offset axis |
| [Semicircular saddle](semicircular_saddle.nightfall) | Width: 8 → 4, 12 | Arcs with overlapping straight closures |
| [Slotted link](slotted_link.nightfall) | Thickness: 6 → 3, 9 | Lines, arcs and exact rational quadratic spline |
| [Elliptic duct](elliptic_duct.nightfall) | Length: 20 → 10, 30 | Nested, rotated ellipses |
| [Spline wedge](spline_wedge.nightfall) | Thickness: 5 → 2, 8 | Exact quadratic Bézier roof |
| [Gusset](gusset.nightfall) | Thickness: 4 → 2, 6 | Triangular section on the YZ plane |
| [Offset foot](offset_foot.nightfall) | Thickness: 6 → 3, 9 | Interior-point region selection, rotation and translation |
| [Twin boss plate](twin_boss_plate.nightfall) | Boss height: 8 → 5, 12 | Two circular bosses joined to a plate |
| [Drilled block](drilled_block.nightfall) | Height: 12 → 8, 16 | Boolean subtraction consuming a compound tool |
| [Intersected rail](intersected_rail.nightfall) | Height: 10 → 6, 14 | Intersection of solid extrusions |

Build and run all examples (macOS / dynamically linked OCCT 7.9.3):

```sh
third_party/build-occt.sh
cmake --preset occt
cmake --build --preset occt --target nf_evaluator_tests
ctest --preset occt -R '^evaluator.reference'
```

One part can be checked with, for example:

```sh
ctest --preset occt -R '^evaluator.reference part: enclosure$'
```

The [test suite](../../services/kernel/tests/reference_part_tests.cpp) uses independent
closed-form volumes, centroids, bounds and surface areas, not saved kernel output. For example,
the enclosure volume is `564h + 5508` mm³, the stepped shaft is `38πl` mm³, and the
elliptic duct is `28πl` mm³. Each result must contain one valid solid with current face-name
ownership. The consumed drilling tools must not remain as visible geometry.

Absolute tolerances are 1e-5 mm³ for volume, 1e-6 mm for centroid, 1e-5 mm for bounds and
1e-5 mm² for surface area. The parabolic spline has a separate 2e-4 mm² area budget: OCCT's
numerical surface integral differs from the analytic value by up to 1.73e-4 mm² in these
cases. Its other checks retain the tighter tolerances. The elliptic duct checks finite,
positive area only; ellipse perimeter has no elementary closed form.

Every part is built at its authored value and two distinct edits. Tests compare cold and
incremental results, verify equivalent cm inputs reuse geometry, restore the original from
cache, then rebuild with caching disabled and inspect the still-live original snapshot.
The catalogue check rejects missing or untested files. Python independently validates all
20 documents against the published JSON Schema in `tools/check.sh` when schema tooling is
installed (required in CI).

The OCCT workflow runs the reference tests in Release and with Nightfall's ASan/UBSan
instrumentation, including when only an example changes. OCCT itself remains the pinned
Release shared-library build. These correctness checks do not set a timing threshold; the
separate 30-feature rebuild benchmark and remaining Phase 2 work are tracked in
[the phase tracker](../../docs/phases/phase-2.md).
The toroidal seal is currently much slower than the other examples: a local stack sample
places the work in OCCT's volume/centroid integration over NURBS surfaces of revolution.
It remains in the suite so future performance work retains an analytic correctness check.
