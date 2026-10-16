#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0
"""Run the RFC 0001 bake-off on the reference corpus and score it.

Every part runs in its own nf_bakeoff process, so a kernel crash or hang is a recorded
result instead of the end of the run. Each part is timed over --repeat runs; with
--asan-build-dir, each part also runs once in a sanitizer build and memory-safety reports
are counted.

Usage:
  run_bakeoff.py --build-dir build/bakeoff [--asan-build-dir build/bakeoff-asan]
                 [--repeat 5] [--integration integration.json] --out results.json

Standard library only. The ABC corpus is added in #7 (needs RFC 0002); until then the score
covers the reference corpus and says so.
"""

import argparse
import datetime
import json
import os
import re
import pathlib
import statistics
import subprocess
import sys
import tempfile

KERNELS = ["occt", "smlib"]
TIMEOUT_S = 120
WEIGHTS = {  # RFC 0001 "Metrics and weights"
    "operation_success": 0.30,
    "correctness": 0.20,
    "operation_history": 0.20,
    "fillet_quality": 0.10,
    "performance": 0.10,
    "integration_cost": 0.10,
}
HISTORY_POINTS = {"yes": 100.0, "partial": 50.0, "no": 0.0}


def run_part(binary, kernel, part, env=None):
    """One part in one process: (status, part JSON or None, stderr)."""
    with tempfile.TemporaryDirectory() as tmp:
        out = pathlib.Path(tmp) / "r.json"
        try:
            proc = subprocess.run(
                [binary, "--kernel", kernel, "--part", part, "--out", str(out)],
                capture_output=True, text=True, timeout=TIMEOUT_S, env=env)
        except subprocess.TimeoutExpired:
            return "timeout", None, ""
        if proc.returncode < 0:
            return f"signal {-proc.returncode}", None, proc.stderr
        if proc.returncode != 0 or not out.exists():
            return f"exit {proc.returncode}", None, proc.stderr
        return "ok", json.loads(out.read_text())["parts"][0], proc.stderr


def scrub(line):
    """Drop machine-specific path prefixes (home directory, Xcode SDK) from a report line."""
    line = re.sub(r"/Applications/Xcode[^ ]*?\.sdk/usr/include/", "<sdk>/", line)
    line = re.sub(r"/Users/[^/ ]+/[^ ]*?/(third_party|tools|core)/", r"\1/", line)
    return line.replace(os.path.expanduser("~"), "~")


def sanitizer_findings(stderr):
    """Distinct ASan/UBSan reports in a run's stderr (first line of each, paths scrubbed)."""
    lines = [l for l in stderr.splitlines()
             if "ERROR: AddressSanitizer" in l or "runtime error:" in l
             or l.startswith("SUMMARY: ")]
    return sorted(set(scrub(l.strip())[:200] for l in lines if not l.startswith("SUMMARY")))


def list_parts(binary):
    """Part name -> category, in corpus order."""
    out = subprocess.run([binary, "--list"], capture_output=True, text=True, check=True).stdout
    return dict(line.split()[:2] for line in out.splitlines() if line.strip())


def score(results, integration):
    """RFC 0001 normalized scores (0–100) per kernel, reference corpus only."""
    per = {}
    medians = {k: {} for k in KERNELS}
    for k in KERNELS:
        for name, r in results[k].items():
            if r["status"] == "ok" and r["part"]["success"] and r["timings_ms"]:
                medians[k][name] = statistics.median(r["timings_ms"])
    shared = set.intersection(*(set(m) for m in medians.values()))
    for k in KERNELS:
        rs = list(results[k].values())
        ok = [r for r in rs if r["status"] == "ok"]
        valid = [r for r in ok if r["part"]["built"] and r["part"]["valid"]]
        checks = [r["part"][c] for r in ok for c in ("volume_ok", "area_ok")
                  if r["part"][c] is not None]
        fillets = [r for r in rs if r["category"] == "fillet"]
        fillet_ok = [r for r in fillets if r["status"] == "ok" and r["part"]["valid"]]
        median_ms = statistics.median(medians[k][n] for n in shared) if shared else None
        history = ok[0]["part_history"] if ok else "no"
        per[k] = {
            "operation_success": 100.0 * len(valid) / len(rs),
            "correctness": 100.0 * sum(checks) / len(checks) if checks else 0.0,
            "operation_history": HISTORY_POINTS[history],
            "fillet_quality": 100.0 * len(fillet_ok) / len(fillets) if fillets else 0.0,
            "_median_ms": median_ms,
        }
        i = integration.get(k, {})
        per[k]["integration_cost"] = (40 * bool(i.get("builds_from_unmodified_upstream"))
                                      + 30 * bool(i.get("ci_build_minutes", 1e9) <= 15)
                                      + 30 * bool(i.get("shipped_megabytes", 1e9) <= 50))
    fastest = min((v["_median_ms"] for v in per.values() if v["_median_ms"]), default=None)
    for k, v in per.items():
        v["performance"] = 100.0 * fastest / v["_median_ms"] if fastest and v["_median_ms"] else 0.0
        v["weighted"] = sum(WEIGHTS[m] * v[m] for m in WEIGHTS)
    return per, sorted(shared)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--build-dir", required=True, type=pathlib.Path)
    ap.add_argument("--asan-build-dir", type=pathlib.Path)
    ap.add_argument("--repeat", type=int, default=5)
    ap.add_argument("--integration", type=pathlib.Path)
    ap.add_argument("--out", required=True, type=pathlib.Path)
    args = ap.parse_args()

    binary = str(args.build_dir / "tools/bakeoff/nf_bakeoff")
    asan_binary = str(args.asan_build_dir / "tools/bakeoff/nf_bakeoff") if args.asan_build_dir else None
    # ASan must keep going after a report so every part gets a verdict; UBSan prints and continues.
    asan_env = dict(os.environ, ASAN_OPTIONS="halt_on_error=0:detect_leaks=0",
                    UBSAN_OPTIONS="print_stacktrace=0")
    parts = list_parts(binary)
    integration = json.loads(args.integration.read_text()) if args.integration else {}

    results = {k: {} for k in KERNELS}
    for k in KERNELS:
        for name in parts:
            status, part, _ = run_part(binary, k, name)
            timings = [part["build_ms"]] if part else []
            for _ in range(args.repeat - 1 if part else 0):
                s, p, _ = run_part(binary, k, name)
                if s == "ok":
                    timings.append(p["build_ms"])
            entry = {"status": status, "part": part, "category": parts[name],
                     "part_history": None, "timings_ms": timings}
            if asan_binary:
                s, _, err = run_part(asan_binary, k, name, env=asan_env)
                entry["sanitizer"] = {"status": s, "findings": sanitizer_findings(err)}
            results[k][name] = entry
            print(f"{k:5} {name:18} {status:10} "
                  f"{'pass' if part and part['success'] else 'FAIL'}"
                  + (f"  san={len(entry['sanitizer']['findings'])}" if asan_binary else ""),
                  file=sys.stderr)
        # Declared history support comes from the report header.
        with tempfile.TemporaryDirectory() as tmp:
            out = pathlib.Path(tmp) / "h.json"
            subprocess.run([binary, "--kernel", k, "--part", next(iter(parts)), "--out", str(out)],
                           capture_output=True, check=False)
            declared = json.loads(out.read_text())["history_support_declared"] if out.exists() else "no"
        for e in results[k].values():
            e["part_history"] = declared

    scores, shared = score(results, integration)
    report = {
        "date": datetime.date.today().isoformat(),
        "corpus": "reference parts only (ABC pending RFC 0002, #7)",
        "repeat": args.repeat,
        "sanitizer_pass": bool(asan_binary),
        "integration_inputs": integration,
        "performance_shared_parts": shared,
        "scores": scores,
        "results": results,
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=1, sort_keys=True) + "\n")
    for k, v in scores.items():
        print(f"{k:5} weighted {v['weighted']:6.2f}  " + "  ".join(
            f"{m}={v[m]:.1f}" for m in WEIGHTS), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
