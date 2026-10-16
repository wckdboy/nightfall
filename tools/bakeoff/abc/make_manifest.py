#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0
"""Build the RFC 0001 ABC sample manifest: 200 single-solid STEP models from chunk 0000,
stratified by B-rep face count. Deterministic: rerunning on the same chunk gives the same
manifest byte for byte.

Usage: make_manifest.py --stat-dir DIR --step-dir DIR --out manifest.csv

Standard library only (no PyYAML): the two stats fields used are top-level scalars.
"""

import argparse
import csv
import hashlib
import itertools
import pathlib
import re
import sys

SEED = "nightfall-abc-v1"
PER_BIN = 40
CHUNK_SIZE = 10_000  # models per ABC chunk
# Quintile edges of #surfs over chunk 0000's 5,111 single-part models (lower bound inclusive).
BINS = [(1, 8), (8, 17), (17, 32), (32, 68), (68, 10**9)]

FIELD = re.compile(r"^'#(surfs|parts)': (\d+)$", re.M)
STAT_NAME = re.compile(r"^(\d{8})_([0-9a-f]+)_stats_\d+\.yml$")
# '#parts == 1' in the stats does not guarantee one solid: some models are open surface
# sheets or solids with voids. Check the STEP entities themselves.
SOLID = re.compile(rb"\bMANIFOLD_SOLID_BREP\s*\(")
NOT_SINGLE_SOLID = re.compile(rb"\b(BREP_WITH_VOIDS|SHELL_BASED_SURFACE_MODEL)\s*\(")


def is_single_solid(step: pathlib.Path) -> bool:
    """Exactly one manifold solid, no voids, no surface-only geometry."""
    text = step.read_bytes()
    return len(SOLID.findall(text)) == 1 and not NOT_SINGLE_SOLID.search(text)


def read_stats(stat_dir: pathlib.Path) -> dict[str, int]:
    """Model id -> face count, for single-part models only."""
    faces = {}
    for path in sorted(stat_dir.glob("*/*_stats_*.yml")):
        name = STAT_NAME.match(path.name)
        if not name:
            continue
        fields = dict(FIELD.findall(path.read_text(errors="replace")))
        if fields.get("parts") == "1" and "surfs" in fields:
            faces[name.group(1)] = int(fields["surfs"])
    return faces


def step_files(step_dir: pathlib.Path) -> dict[str, pathlib.Path]:
    """Model id -> its STEP file (a model directory holds exactly one)."""
    found = {}
    for path in sorted(step_dir.glob("*/*.step")):
        found.setdefault(path.parent.name, path)
    return found


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--stat-dir", type=pathlib.Path, required=True)
    parser.add_argument("--step-dir", type=pathlib.Path, required=True)
    parser.add_argument("--out", type=pathlib.Path, required=True)
    args = parser.parse_args()

    faces = read_stats(args.stat_dir)
    steps = step_files(args.step_dir)
    if len(steps) < CHUNK_SIZE:
        # A partial extraction (e.g. fetch-abc.sh without --all) holds only the manifest's
        # own models, so regenerating from it would just echo the manifest back.
        print(f"found {len(steps)} STEP models, expected the whole chunk ({CHUNK_SIZE}): "
              "run fetch-abc.sh --all first", file=sys.stderr)
        return 1
    candidates = sorted(set(faces) & set(steps))
    order = lambda model: hashlib.sha256(f"{SEED}:{model}".encode()).hexdigest()

    rows = []
    for index, (low, high) in enumerate(BINS):
        in_bin = sorted((m for m in candidates if low <= faces[m] < high), key=order)
        # Lazy: reads STEP files only until the bin is full.
        chosen = list(itertools.islice((m for m in in_bin if is_single_solid(steps[m])), PER_BIN))
        if len(chosen) < PER_BIN:
            print(f"bin {index} [{low}, {high}) has only {len(chosen)} models", file=sys.stderr)
            return 1
        for model in chosen:
            step = steps[model]
            rows.append([model, step.name, faces[model], index, sha256(step)])

    rows.sort()
    with args.out.open("w", newline="") as f:
        writer = csv.writer(f, lineterminator="\n")
        writer.writerow(["model_id", "step_file", "faces", "bin", "sha256"])
        writer.writerows(rows)
    print(f"{len(rows)} models from {len(candidates)} single-part candidates -> {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
