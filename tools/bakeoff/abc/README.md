# ABC sample for the kernel bake-off

200 real-world CAD models for RFC 0001, drawn from chunk 0000 of the
[ABC dataset](https://deep-geometry.github.io/abc-dataset/) (Koch et al., CVPR 2019).

## Terms

- The ABC dataset is distributed under the MIT licence, but **the copyright of each CAD model
  is owned by its creator**; see
  [Onshape Terms of Use 1.g.ii](https://www.onshape.com/legal/terms-of-use#your_content).
- Nightfall therefore commits **only the manifest** (model IDs, file names, face counts and
  SHA-256 hashes). The models themselves are downloaded locally by `fetch-abc.sh` into the
  git-ignored `third_party/src/abc/` and never redistributed.
- The ABC authors give no warranties regarding the dataset.

## The sample

`manifest-v1.csv` lists 200 **single-part** models (`#parts == 1` in the ABC stats),
stratified into five bins by B-rep face count (`#surfs`), 40 models per bin:

| Bin | Faces | Models |
| --- | --- | --- |
| 0 | 1–7 | 40 |
| 1 | 8–16 | 40 |
| 2 | 17–31 | 40 |
| 3 | 32–67 | 40 |
| 4 | 68+ | 40 |

The bin edges are the face-count quintiles of chunk 0000's 5,111 single-part models. Within a
bin, models are ordered by `sha256("nightfall-abc-v1:<model id>")` and the first 40 are taken,
so `make_manifest.py` regenerates the same manifest byte for byte.

## Usage

```sh
tools/bakeoff/abc/fetch-abc.sh          # download chunk 0000 (1.6 GB), extract + verify the 200
tools/bakeoff/abc/make_manifest.py \    # only to regenerate the manifest (needs full extraction)
  --stat-dir third_party/src/abc/stat --step-dir third_party/src/abc/step --out manifest-v1.csv
```

Changing the sample (seed, bins, chunk) creates `manifest-v2.csv`; v1 stays so earlier
bake-off results remain reproducible.
