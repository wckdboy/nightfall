# ABC sample for the kernel bake-off

200 real-world CAD models for RFC 0001, drawn from chunk 0000 of the
[ABC dataset](https://deep-geometry.github.io/abc-dataset/) (Koch et al., CVPR 2019).

## Terms

- **The copyright of each CAD model is owned by its creator**; the ABC site points to
  [Onshape Terms of Use 1.g.ii](https://www.onshape.com/legal/terms-of-use#your_content).
- Nightfall therefore commits **only the manifest** (model IDs, file names, face counts and
  SHA-256 hashes). The models themselves are downloaded locally by `fetch-abc.sh` into the
  git-ignored `third_party/src/abc/` and never redistributed.
- The ABC authors give no warranties regarding the dataset.

## The sample

`manifest-v1.csv` lists 200 **single-solid** models, stratified into five bins by B-rep face
count (`#surfs` in the ABC stats), 40 per bin. A model qualifies when the stats say
`#parts == 1` **and** its STEP file has exactly one `MANIFOLD_SOLID_BREP` with no
`BREP_WITH_VOIDS` or `SHELL_BASED_SURFACE_MODEL`: `#parts == 1` alone admits open surface
sheets and solids with voids.

| Bin | Faces (`#surfs`) | Single-part models in chunk 0000 | Sampled |
| --- | --- | --- | --- |
| 0 | 1–7 | 857 | 40 |
| 1 | 8–16 | 1,154 | 40 |
| 2 | 17–31 | 1,046 | 40 |
| 3 | 32–67 | 1,021 | 40 |
| 4 | 68+ | 1,033 | 40 |

The edges are the quintile cut points of the 5,111 single-part models' face counts (bins are
uneven because of ties). Within a bin, models are ordered by
`sha256("nightfall-abc-v1:<model id>")` and the first 40 that pass the solid check are taken,
so `make_manifest.py` regenerates the same manifest byte for byte. `#surfs` comes from the
ABC stats and can differ slightly from the faces a kernel reports after import.

## Usage

```sh
# Download chunk 0000 (1.6 GB, resumable), extract and verify the 200 models (macOS: uses md5).
tools/bakeoff/abc/fetch-abc.sh

# Regenerate the manifest (needs the whole chunk: fetch-abc.sh --all, 13.7 GB extracted).
tools/bakeoff/abc/make_manifest.py --stat-dir third_party/src/abc/stat \
  --step-dir third_party/src/abc/step --out tools/bakeoff/abc/manifest-v1.csv
```

Changing the sample (seed, bins, chunk) creates `manifest-v2.csv`; v1 stays so earlier
bake-off results remain reproducible.
