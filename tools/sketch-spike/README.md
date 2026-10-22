# Sketch solver spike (RFC 0012)

Temporary scaffolding, like `tools/bakeoff/`: two adapters solve the same corpus and
`score.py` applies RFC 0012's decision rule. Nothing here is built by CMake or shipped.

- `corpus/`: one problem per case in ezpz's text format (`*.md`) with its analytic
  expectation (`*.json`): all 13 RFC 0005 v1 constraint kinds, an under-constrained, an
  inconsistent and a redundant set, a dimension-edit branch pair, and a 203-constraint strip
  of 40 squares for timing.
- `ezpz-adapter/`: a binary in a checkout of KittyCAD/ezpz at the pinned commit; prints one
  JSON line per problem (solve with freedom analysis, median of 21 timed solves).
- `solidcraft-adapter/`: a binary next to tangency-labs/solidcraft's `sketch` and `geom`
  crates at the pinned commit; reads the same files, maps each statement onto SolidCraft's
  API (`p = (x, y)` becomes a coincidence with a fixed anchor, so contradictory fixes
  surface as a conflict) and prints the same JSON.
- `score.py <spike-dir>`: runs both on the corpus and on 400 randomized variants
  (perturbed and extreme values, dropped and duplicated lines) and writes the per-metric
  scores; `results/2026-10-22.json` is the recorded run.

Pinned sources: ezpz `915882cc731da31042ce494c89b10334ee57837a`, SolidCraft
`6f5d0038abffbd3e1fc26069b1b96ca89984058f`. Both are read-only checkouts outside this
repository; neither is a Nightfall dependency until RFC 0012 is Accepted.
