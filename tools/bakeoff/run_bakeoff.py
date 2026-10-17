#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0
"""Run the RFC 0001 bake-off on the reference corpus and score it.

Every part runs in its own nf_bakeoff process, so a kernel crash or hang is a recorded
result instead of the end of the run. Timing happens inside that process: one untimed
warm-up build, then --repeat timed builds, median reported (separate processes would time
library loading and lazy kernel initialisation instead of the operation). With
--asan-build-dir, each part also runs once in a sanitizer build (both kernels instrumented)
and sanitizer reports are recorded.

Unit: one reference part = one operation script (RFC 0001 "operations attempted").

Usage:
  run_bakeoff.py --build-dir build/bakeoff [--asan-build-dir build/bakeoff-asan]
                 [--repeat 5] [--integration integration.json] --out results.json

Standard library only. The ABC corpus is added in #7 (needs RFC 0002); until then the score
covers the reference corpus and says so.
"""

import argparse
import datetime
import json
import math
import os
import pathlib
import re
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


def run_part(binary, kernel, part, env=None, repeat=0):
    """One part in one process: (status, report or None, stderr). With repeat > 0 the
    process warms up once and reports the median of `repeat` timed builds."""
    with tempfile.TemporaryDirectory() as tmp:
        out = pathlib.Path(tmp) / "r.json"
        extra = ["--repeat", str(repeat)] if repeat > 0 else []
        try:
            proc = subprocess.run(
                [binary, "--kernel", kernel, "--part", part, "--out", str(out)] + extra,
                capture_output=True, text=True, timeout=TIMEOUT_S, env=env)
        except subprocess.TimeoutExpired as e:
            err = e.stderr.decode(errors="replace") if isinstance(e.stderr, bytes) else (e.stderr or "")
            return "timeout", None, err
        if proc.returncode < 0:
            return f"signal {-proc.returncode}", None, proc.stderr
        if proc.returncode != 0 or not out.exists():
            return f"exit {proc.returncode}", None, proc.stderr
        return "ok", json.loads(out.read_text()), proc.stderr


def scrub(line):
    """Drop machine-specific path prefixes (home directory, Xcode SDK) from a report line."""
    line = re.sub(r"/Applications/Xcode[^ ]*?\.sdk/usr/include/", "<sdk>/", line)
    line = re.sub(r"/Users/[^/ ]+/[^ ]*?/(third_party|tools|core)/", r"\1/", line)
    return line.replace(os.path.expanduser("~"), "~")


def normalize(line):
    """Stable identity for a report line: no pids, addresses or registers."""
    line = re.sub(r"==\d+==", "", line)
    line = re.sub(r"0x[0-9a-f]+", "0x?", line)
    return scrub(line).strip()[:220]


def sanitizer_findings(stderr):
    """Distinct ASan/UBSan reports: kind, first line and the first stack frame."""
    lines = stderr.splitlines()
    found = {}
    for i, line in enumerate(lines):
        if "ERROR: AddressSanitizer" in line:
            kind = "asan"
        elif "runtime error:" in line:
            kind = "ubsan"
        else:
            continue
        frame = next((l for l in lines[i + 1:i + 12] if re.match(r"\s+#0 ", l)), "")
        key = normalize(line)
        found.setdefault(key, {"kind": kind, "report": key, "frame": normalize(frame)})
    return sorted(found.values(), key=lambda f: (f["kind"], f["report"]))


def list_parts(binary):
    """Part name -> category, in corpus order."""
    out = subprocess.run([binary, "--list"], capture_output=True, text=True, check=True).stdout
    return dict(line.split()[:2] for line in out.splitlines() if line.strip())


def valid(entry):
    p = entry["part"]
    return entry["status"] == "ok" and p is not None and p["built"] and p["valid"]


def score(results, integration, history):
    """RFC 0001 normalized scores (0–100) per kernel, reference corpus only."""
    # Performance compares the operations that succeeded (valid solid) on every kernel.
    shared = sorted(set.intersection(*({n for n, e in results[k].items() if valid(e) and e["timings_ms"]}
                                       for k in KERNELS)))
    medians = {k: {n: statistics.median(results[k][n]["timings_ms"]) for n in shared} for k in KERNELS}
    per = {}
    for k in KERNELS:
        entries = list(results[k].values())
        analytic = [e for e in entries if e["part"] and not e["part"]["consensus_reason"]]
        checks = [e["part"][c] for e in analytic for c in ("volume_ok", "area_ok")
                  if e["part"][c] is not None]
        fillets = [e for e in entries if e["category"] == "fillet"]
        san = [e.get("sanitizer", {}).get("findings", []) for e in entries]
        per[k] = {
            "operation_success": 100.0 * sum(valid(e) for e in entries) / len(entries),
            "correctness": 100.0 * sum(checks) / len(checks) if checks else 0.0,
            "operation_history": HISTORY_POINTS[history[k]],
            "fillet_quality": 100.0 * sum(valid(e) for e in fillets) / len(fillets) if fillets else 0.0,
            "median_ms_shared": statistics.median(medians[k].values()) if shared else None,
            "memory_errors_per_1000_operations":
                1000.0 * sum(any(f["kind"] == "asan" for f in fs) for fs in san) / len(entries),
            "ub_reports_per_1000_operations":
                1000.0 * sum(any(f["kind"] == "ubsan" for f in fs) for fs in san) / len(entries),
        }
        i = integration.get(k, {})
        per[k]["integration_cost"] = (40 * bool(i.get("builds_from_unmodified_upstream"))
                                      + 30 * bool(i.get("ci_build_minutes", 1e9) <= 15)
                                      + 30 * bool(i.get("shipped_megabytes", 1e9) <= 50))
    fastest = min((v["median_ms_shared"] for v in per.values() if v["median_ms_shared"]), default=None)
    for k, v in per.items():
        v["performance"] = (100.0 * fastest / v["median_ms_shared"]) if fastest and v["median_ms_shared"] else 0.0
        v["weighted"] = sum(WEIGHTS[m] * v[m] for m in WEIGHTS)
        # Reported alongside, not scored: geometric mean of per-part time ratios vs the fastest.
        if shared:
            ratios = [medians[k][n] / min(medians[o][n] for o in KERNELS) for n in shared]
            v["geomean_time_ratio_vs_fastest"] = math.exp(sum(map(math.log, ratios)) / len(ratios))
    return per, shared


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
    # UBSan prints and continues; ASan aborts at its first report in both builds (neither is
    # built with -fsanitize-recover=address), so each part yields at most one ASan report.
    asan_env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="print_stacktrace=1")
    parts = list_parts(binary)
    integration = json.loads(args.integration.read_text()) if args.integration else {}

    results = {k: {} for k in KERNELS}
    history = {}
    for k in KERNELS:
        for name, category in parts.items():
            # Two processes: the second confirms the result is reproducible (flaky = differs).
            runs = [run_part(binary, k, name, repeat=args.repeat) for _ in range(2)]
            statuses = [s for s, _, _ in runs]
            reports = [r for s, r, _ in runs if s == "ok"]
            first = reports[0]["parts"][0] if reports else None
            if reports and k not in history:
                history[k] = reports[0]["history_support"]  # measured by the driver
            entry = {
                "status": "ok" if reports and all(s == "ok" for s in statuses) else
                          ("flaky" if reports else statuses[0]),
                "repeat_statuses": statuses,
                "part": first,
                "category": category,
                "timings_ms": [r["parts"][0]["build_ms"] for r in reports],  # per-process medians
            }
            if asan_binary:
                s, _, err = run_part(asan_binary, k, name, env=asan_env)
                entry["sanitizer"] = {"status": s, "findings": sanitizer_findings(err)}
            results[k][name] = entry
            print(f"{k:5} {name:18} {entry['status']:8} "
                  f"{'pass' if first and first['success'] else 'FAIL'}"
                  + (f"  san={len(entry['sanitizer']['findings'])}" if asan_binary else ""),
                  file=sys.stderr)
        history.setdefault(k, "no")

    scores, shared = score(results, integration, history)
    report = {
        "date": datetime.date.today().isoformat(),
        "corpus": "reference parts only (ABC pending RFC 0002, #7)",
        "unit": "one reference part = one operation script",
        "repeat": args.repeat,
        "sanitizer_pass": bool(asan_binary),
        "history_support": history,
        "correctness_excludes": "consensus parts (two kernels cannot settle which is right)",
        "integration_inputs": integration,
        "performance_shared_parts": shared,
        "scores": scores,
        "results": results,
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=1, sort_keys=True) + "\n")
    for k, v in scores.items():
        print(f"{k:5} weighted {v['weighted']:6.2f}  " + "  ".join(
            f"{m}={v[m]:.1f}" for m in WEIGHTS)
              + f"  geomean_ratio={v.get('geomean_time_ratio_vs_fastest', float('nan')):.2f}"
              + f"  mem/1000={v['memory_errors_per_1000_operations']:.0f}"
              + f"  ub/1000={v['ub_reports_per_1000_operations']:.0f}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
