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


ABC = []  # extra nf_bakeoff arguments selecting the ABC plan (set by --abc-plan)


def run_part(binary, kernel, part, env=None, repeat=0, abc=False):
    """One part in one process: (status, report or None, stderr). With repeat > 0 the
    process warms up once and reports the median of `repeat` timed builds."""
    with tempfile.TemporaryDirectory() as tmp:
        out = pathlib.Path(tmp) / "r.json"
        extra = (["--repeat", str(repeat)] if repeat > 0 else []) + (ABC if abc else [])
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
        report = json.loads(out.read_text())
        for p in report["parts"]:  # kernel messages may embed local paths
            p["error"] = scrub(p["error"])
        return "ok", report, proc.stderr


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


def list_parts(binary, abc=False):
    """Part name -> category, in corpus order."""
    out = subprocess.run([binary, "--list"] + (ABC if abc else []),
                         capture_output=True, text=True, check=True).stdout
    return dict(line.split()[:2] for line in out.splitlines() if line.strip())


def valid(entry):
    p = entry["part"]
    return entry["status"] == "ok" and p is not None and p["built"] and p["valid"]


def abc_model(name):
    return name.rsplit("-", 1)[0] if name.startswith("abc-") else None


def abc_exclusions(results):
    """Models whose import failed on any kernel (RFC 0001 F13): excluded from both."""
    return sorted({abc_model(n) for k in KERNELS for n, e in results[k].items()
                   if e["category"] == "abc_import" and (e["part"] is None or not e["part"]["built"])})


def score(results, integration, history):
    """RFC 0001 normalized scores (0–100) per kernel (reference corpus + ABC per F13)."""
    excluded = set(abc_exclusions(results))
    scored = {k: {n: e for n, e in results[k].items() if abc_model(n) not in excluded} for k in KERNELS}
    # Performance compares reference operations valid on every kernel; ABC is excluded
    # because SMLib's ABC timings include STEP -> OCCT -> USD translation (F13).
    shared = sorted(set.intersection(*({n for n, e in scored[k].items()
                                        if valid(e) and e["timings_ms"] and not abc_model(n)}
                                       for k in KERNELS)))
    medians = {k: {n: statistics.median(results[k][n]["timings_ms"]) for n in shared} for k in KERNELS}
    per = {}
    for k in KERNELS:
        entries = list(scored[k].values())
        analytic = [e for e in entries if e["part"] and not e["part"]["consensus_reason"]]
        checks = [e["part"][c] for e in analytic for c in ("volume_ok", "area_ok")
                  if e["part"][c] is not None]
        fillets = [e for e in entries if e["category"] in ("fillet", "abc_round")]
        # Rates are over the operations that actually had a sanitizer run.
        san = [e["sanitizer"]["findings"] for e in entries if "sanitizer" in e]
        per[k] = {
            "operation_success": 100.0 * sum(valid(e) for e in entries) / len(entries),
            "correctness": 100.0 * sum(checks) / len(checks) if checks else 0.0,
            "operation_history": HISTORY_POINTS[history[k]],
            "fillet_quality": 100.0 * sum(valid(e) for e in fillets) / len(fillets) if fillets else 0.0,
            "median_ms_shared": statistics.median(medians[k].values()) if shared else None,
            "sanitized_operations": len(san),
            "memory_errors_per_1000_operations":
                1000.0 * sum(any(f["kind"] == "asan" for f in fs) for fs in san) / max(len(san), 1),
            "ub_reports_per_1000_operations":
                1000.0 * sum(any(f["kind"] == "ubsan" for f in fs) for fs in san) / max(len(san), 1),
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
    return per, shared, sorted(excluded)


def abc_agreement(results):
    """Import volume agreement between kernels on ABC models (reported, not scored)."""
    diffs = []
    for name, e in results[KERNELS[0]].items():
        if e["category"] != "abc_import":
            continue
        vols = [results[k][name]["part"]["volume"] for k in KERNELS
                if results[k][name]["part"] and results[k][name]["part"]["built"]]
        if len(vols) == len(KERNELS) and all(v for v in vols):
            diffs.append(abs(vols[0] - vols[1]) / abs(vols[0]))
    return {f"within_{t:g}": sum(d <= t for d in diffs) for t in (1e-9, 1e-6, 1e-4, 1e-2)} | {
        "compared": len(diffs)}


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--rescore", type=pathlib.Path,
                    help="recompute scores of an existing results file in place (no runs)")
    ap.add_argument("--build-dir", type=pathlib.Path)
    ap.add_argument("--asan-build-dir", type=pathlib.Path)
    ap.add_argument("--repeat", type=int, default=5)
    ap.add_argument("--integration", type=pathlib.Path)
    ap.add_argument("--out", type=pathlib.Path)
    ap.add_argument("--abc-plan", type=pathlib.Path, help="ops plan (RFC 0001 F13)")
    ap.add_argument("--abc-dir", type=pathlib.Path, help="extracted ABC STEP models")
    ap.add_argument("--abc-sanitize", action="store_true", help="also sanitize ABC parts (slow)")
    ap.add_argument("--usd-plugin-dir", type=pathlib.Path,
                    help="usd-brep omniSolid/resources (PXR_PLUGINPATH_NAME for SMLib imports)")
    args = ap.parse_args()
    if args.rescore:
        report = json.loads(args.rescore.read_text())
        scores, shared, excluded = score(report["results"], report["integration_inputs"],
                                         report["history_support"])
        report.update(scores=scores, performance_shared_parts=shared, abc_excluded_models=excluded)
        args.rescore.write_text(json.dumps(report, indent=1, sort_keys=True) + "\n")
        for k, v in scores.items():
            print(f"{k:5} weighted {v['weighted']:6.2f}", file=sys.stderr)
        return 0
    if not args.build_dir or not args.out:
        ap.error("--build-dir and --out are required unless --rescore is given")
    if args.abc_plan:
        ABC.extend(["--abc", str(args.abc_plan), "--abc-dir", str(args.abc_dir)])
    if args.usd_plugin_dir:
        os.environ["PXR_PLUGINPATH_NAME"] = str(args.usd_plugin_dir)

    binary = str(args.build_dir / "tools/bakeoff/nf_bakeoff")
    asan_binary = str(args.asan_build_dir / "tools/bakeoff/nf_bakeoff") if args.asan_build_dir else None
    # UBSan prints and continues; ASan aborts at its first report in both builds (neither is
    # built with -fsanitize-recover=address), so each part yields at most one ASan report.
    asan_env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="print_stacktrace=1")
    parts = list_parts(binary)
    abc_parts = list_parts(binary, abc=True) if args.abc_plan else {}
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
        for name, category in abc_parts.items():  # one process each, untimed (F13)
            s, r, _ = run_part(binary, k, name, abc=True)
            entry = {"status": s, "repeat_statuses": [s], "part": r["parts"][0] if r else None,
                     "category": category, "timings_ms": []}
            if asan_binary and args.abc_sanitize:
                s2, _, err = run_part(asan_binary, k, name, env=asan_env, abc=True)
                entry["sanitizer"] = {"status": s2, "findings": sanitizer_findings(err)}
            results[k][name] = entry
            print(f"{k:5} {name:28} {s:8} {'valid' if valid(entry) else 'INVALID'}", file=sys.stderr)
        history.setdefault(k, "no")

    scores, shared, excluded = score(results, integration, history)
    report = {
        "date": datetime.date.today().isoformat(),
        "corpus": "reference parts + ABC (RFC 0001 F13)" if args.abc_plan else "reference parts only",
        "unit": "one reference part = one operation script",
        "repeat": args.repeat,
        "sanitizer_pass": bool(asan_binary),
        "history_support": history,
        "correctness_excludes": "consensus parts (two kernels cannot settle which is right)",
        "integration_inputs": integration,
        "performance_shared_parts": shared,
        "abc_plan": str(args.abc_plan) if args.abc_plan else None,
        "abc_excluded_models": excluded,
        "abc_import_volume_agreement": abc_agreement(results) if args.abc_plan else None,
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
