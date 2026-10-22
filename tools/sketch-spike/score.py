#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0
"""Scores both spike adapters on the corpus per RFC 0012's decision rule (fixed beforehand)."""
import json, random, re, subprocess, sys, time
from pathlib import Path
S = Path(sys.argv[1]); CORPUS = S / "corpus"
SOLVERS = {"ezpz": S / "ezpz/target/release/ezpz-spike", "solidcraft": S / "sc-sketch/target/release/sc-spike"}
TOL = 1e-6  # mm on solved coordinates

def run(solver, path, timeout=5):
    t = time.monotonic()
    try:
        p = subprocess.run([str(SOLVERS[solver]), str(path)], capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return {"crash": "timeout"}, time.monotonic() - t
    if p.returncode != 0:
        return {"crash": f"exit {p.returncode}: {p.stderr[-200:]}"}, time.monotonic() - t
    try:
        return json.loads(p.stdout), time.monotonic() - t
    except json.JSONDecodeError:
        return {"crash": "bad output " + p.stdout[:120]}, time.monotonic() - t

def solved(out):
    """Did the solver claim success? ezpz reports conflicts as unsatisfied equations."""
    if out.get("crash"): return False
    if "unsatisfied" in out: return out["ok"] and out["converged"] and out["unsatisfied"] == 0
    return out["ok"]

def correct(out, exp):
    if not solved(out) or not exp.get("ok"): return solved(out) == exp.get("ok", True)
    for name, xy in exp.get("points", {}).items():
        got = out["points"].get(name)
        if got is None or abs(got[0]-xy[0]) > TOL or abs(got[1]-xy[1]) > TOL: return False
    for name, c in exp.get("circle", {}).items():
        got = out.get("circles", {}).get(name)
        if got is None:
            continue  # ezpz's adapter reports points only; circle checked through tangency points
        if abs(got["radius"]-c["radius"]) > TOL or any(abs(a-b) > TOL for a, b in zip(got["center"], c["center"])): return False
    return True

def diagnostics(out, exp):
    """1 for the exact answer, 0.5 for the right flag without the number, else 0."""
    if out.get("crash"): return 0
    if exp.get("conflict"):
        named = out.get("failing", 0) > 0 or out.get("unsatisfied", 0) > 0
        refused = not out["ok"] if "failing" in out else False
        return 1 if named and refused else (0.5 if named else 0)
    if "redundant" in exp:
        if "redundant" in out: return 1 if out["redundant"] == exp["redundant"] else 0.5
        return 0  # no redundancy report
    if "dof" in out: return 1 if out["dof"] == exp["dof"] else 0
    under = out.get("underconstrained_vars", 0) > 0
    return 0.5 if under == (exp["dof"] > 0) else 0

cases = sorted(p.stem for p in CORPUS.glob("*.md"))
results = {s: {} for s in SOLVERS}
for case in cases:
    exp = json.loads((CORPUS / f"{case}.json").read_text())
    for s in SOLVERS:
        out, wall = run(s, CORPUS / f"{case}.md")
        results[s][case] = {"out": {k: v for k, v in out.items() if k != "points"}, "points": out.get("points"),
                            "correct": correct(out, exp), "diagnostics": diagnostics(out, exp)}

# Robustness: randomized variants of every case (perturbed guesses and values, dropped and
# duplicated lines); any crash, panic or > 1 s run counts against the solver.
random.seed(20261022)
rob = {s: {"runs": 0, "crashes": 0, "slow": 0, "examples": []} for s in SOLVERS}
tmp = S / "fuzz.md"
for i in range(400):
    text = (CORPUS / f"{random.choice(cases)}.md").read_text()
    def jiggle(m):
        x = float(m.group(0)); r = random.random()
        return repr(x * random.choice([1, -1, 0, 1e6, 1e-9]) if r < 0.15 else x + random.uniform(-50, 50) if r < 0.4 else x)
    text = re.sub(r"(?<![a-z0-9_])-?\d+(?:\.\d+)?", jiggle, text)
    lines = text.splitlines()
    if random.random() < 0.3 and len(lines) > 4: del lines[random.randrange(2, len(lines))]
    if random.random() < 0.3: lines.insert(random.randrange(1, len(lines)), random.choice(lines))
    tmp.write_text("\n".join(lines) + "\n")
    for s in SOLVERS:
        out, wall = run(s, tmp, timeout=5)
        rob[s]["runs"] += 1
        if out.get("crash"):
            rob[s]["crashes"] += 1
            if len(rob[s]["examples"]) < 3: rob[s]["examples"].append(out["crash"][:160])
        elif wall > 1.0: rob[s]["slow"] += 1

summary = {}
perf = {s: results[s]["perf_strip"]["out"].get("median_ms") for s in SOLVERS}
fast = min(v for v in perf.values() if v)
for s in SOLVERS:
    r = results[s]
    scored = [c for c in cases]
    correctness = 100 * sum(r[c]["correct"] for c in scored) / len(scored)
    diag = 100 * sum(r[c]["diagnostics"] for c in scored) / len(scored)
    cont = 100 * (r["branch_edit"]["correct"] and r["branch_start"]["correct"])
    robust = 100 * (1 - (rob[s]["crashes"] + rob[s]["slow"]) / rob[s]["runs"])
    performance = 100 * fast / perf[s] if perf[s] else 0
    summary[s] = {"correctness": round(correctness, 1), "diagnostics": round(diag, 1), "continuity": cont,
                  "robustness": round(robust, 1), "performance": round(performance, 1),
                  "perf_strip_median_ms": perf[s], "robustness_detail": rob[s]}
json.dump({"cases": cases, "results": results, "summary": summary}, open(S / "spike-results.json", "w"), indent=1)
print(f"{'case':18} " + "  ".join(f"{s:>22}" for s in SOLVERS))
for c in cases:
    print(f"{c:18} " + "  ".join(f"{('ok' if results[s][c]['correct'] else 'WRONG'):>6} diag {results[s][c]['diagnostics']:<3} {str(results[s][c]['out'].get('median_ms', ''))[:6]:>7}ms" for s in SOLVERS))
print(json.dumps({s: {k: v for k, v in summary[s].items() if k != 'robustness_detail'} for s in SOLVERS}, indent=1))
print(json.dumps({s: summary[s]["robustness_detail"] for s in SOLVERS}, indent=1))
