#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0
"""Agent evaluation suite over the reference parts (RFC 0011, Phase 3 gate G1).

Every part in --parts yields three tasks: build it from its description, edit one parameter,
and repair a part whose parameter lost its unit. An agent works through `nf mcp` and saves
its result; the harness then measures that result with `nf build` and compares exact
volume, area and bodies against a golden built the same way. Rebuild status is never the
verdict; measured geometry is. Standard library only; never shipped.

Runners: `reference` performs the ideal tool calls through the MCP server (tests the harness
and the MCP contract without a model); `claude` runs Claude Code headless with the server as
its only tool. A report JSON and a summary are written to --out.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

REL_TOLERANCE = 1e-6  # volume and area, relative to the golden value
EMPTY_PART = {"format": "nightfall.part", "version": 1, "units": {"length": "mm", "angle": "deg"},
              "meta": {}, "params": [], "references": [], "features": [], "history_end": None,
              "ui": {}}
TASKS = ("build", "edit", "repair")


def build(nf, part_path, out_dir, overrides=()):
    """`nf build` as the independent measurement: evidence with per-body metrics, or errors."""
    step = Path(out_dir) / (Path(part_path).stem + ".step")
    args = [str(nf), "build", str(part_path), "--out", str(step)]
    for name, value in overrides:
        args += ["--set", f"{name}={value}"]
    run = subprocess.run(args, capture_output=True, text=True)
    if run.returncode != 0:
        try:
            errors = json.loads(run.stderr).get("errors", [])
        except json.JSONDecodeError:
            errors = [{"code": "internal", "message": run.stderr.strip()[:500]}]
        return None, errors
    return json.loads(run.stdout)["evidence"], []


def metrics(evidence):
    return {b["id"]: {k: b[k] for k in ("volume", "area", "centroid", "minimum", "maximum",
                                         "solids", "faces", "edges")}
            for b in evidence["body_metrics"]}


def within(actual, golden):
    return abs(actual - golden) <= REL_TOLERANCE * abs(golden)


def compare(task, result, golden):
    """Scores measured bodies against the golden; returns (passed, deltas)."""
    deltas = {}
    if task == "build":
        # Any body naming is fine: compare the totals of the single-body references.
        total = lambda m, k: sum(b[k] for b in m.values())
        for key in ("volume", "area"):
            deltas[key] = {"golden": total(golden, key), "actual": total(result, key)}
        deltas["bodies"] = {"golden": len(golden), "actual": len(result)}
        passed = all(within(d["actual"], d["golden"]) for d in (deltas["volume"], deltas["area"]))
        return passed and len(result) == len(golden), deltas
    passed = set(result) == set(golden)
    deltas["bodies"] = {"golden": sorted(golden), "actual": sorted(result)}
    for body, g in golden.items():
        a = result.get(body)
        if a is None:
            continue
        for key in ("volume", "area"):
            deltas[f"{body}.{key}"] = {"golden": g[key], "actual": a[key]}
            passed = passed and within(a[key], g[key])
        for key in ("solids", "faces", "edges"):
            deltas[f"{body}.{key}"] = {"golden": g[key], "actual": a[key]}
            passed = passed and a[key] == g[key]
    return passed, deltas


def scaled(value, factor):
    """'30 mm' -> '45 mm'; a bare number scales as a number."""
    match = re.fullmatch(r"\s*([-+]?\d+(?:\.\d+)?)\s*([a-z]*)\s*", str(value))
    if not match:
        return None
    number = float(match.group(1)) * factor
    text = f"{number:g}"
    return f"{text} {match.group(2)}".strip()


def unitless(value):
    match = re.fullmatch(r"\s*([-+]?\d+(?:\.\d+)?)\s*([a-z]+)\s*", str(value))
    return match.group(1) if match else None


def infrastructure_error(run):
    """True when the agent could not run at all (account limits, missing CLI, crashed
    session before any turn); those tasks are reported as not run, never as agent failures."""
    text = " ".join(str(run.get(k) or "") for k in ("result", "error")).lower()
    if run.get("is_error") and any(m in text for m in ("session limit", "usage limit",
                                                        "rate limit", "overloaded")):
        return True
    return "claude returned no json" in text or "no such file or directory: 'claude'" in text


def make_tasks(nf, part_path, work):
    """Derives build/edit/repair tasks for one part, each with its starting document,
    prompt, golden metrics and the path the agent must save to."""
    doc = json.loads(Path(part_path).read_text())
    name = Path(part_path).stem
    golden_evidence, errors = build(nf, part_path, work)
    if errors:
        raise RuntimeError(f"{name}: reference part does not build: {errors}")
    golden = metrics(golden_evidence)
    meta = doc.get("meta", {})
    param = doc["params"][0] if doc["params"] else None
    tasks = []
    description = f"{meta.get('name', name)}: {meta.get('doc', '')}".strip()
    tasks.append({
        "part": name, "task": "build", "start": EMPTY_PART, "golden": golden,
        "prompt": (f"Build this part as a Nightfall part document: {description} "
                   f"Units are millimetres. Make the main dimension a named parameter"
                   + (f" called `{param['name']}` with the value `{param['value']}`."
                      if param else ".")
                   + " Verify with measure, then save the document with save_part to "
                   "{save_path}. Change nothing else on disk."),
    })
    if param and scaled(param["value"], 1.5):
        new_value = scaled(param["value"], 1.5)
        edited_evidence, errors = build(nf, part_path, work, [(param["name"], new_value)])
        if not errors:
            tasks.append({
                "part": name, "task": "edit", "start": doc, "golden": metrics(edited_evidence),
                "edit": {"name": param["name"], "value": new_value},
                "prompt": (f"Set the parameter `{param['name']}` of the open part to "
                           f"`{new_value}`. Change nothing else. Verify with measure, then "
                           "save the document with save_part to {save_path}."),
            })
    if param and unitless(param["value"]):
        broken = json.loads(json.dumps(doc))
        broken["params"][0]["value"] = unitless(param["value"])
        tasks.append({
            "part": name, "task": "repair", "start": broken, "golden": golden,
            "prompt": ("The open part fails to build. Its design intent is: "
                       f"{description} Make the smallest change that restores it, keep every "
                       "dimension as described, verify with measure, then save the document "
                       "with save_part to {save_path}."),
        })
    return tasks


class Mcp:
    """A minimal line-delimited MCP client over `nf mcp` for the reference runner."""

    def __init__(self, nf, start_path, allow):
        self.process = subprocess.Popen(
            [str(nf), "mcp", "--in", str(start_path), "--allow-path", str(allow)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.next_id = 0
        self.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                    "clientInfo": {"name": "evals", "version": "0"}})

    def request(self, method, params):
        self.next_id += 1
        message = {"jsonrpc": "2.0", "id": self.next_id, "method": method, "params": params}
        self.process.stdin.write(json.dumps(message) + "\n")
        self.process.stdin.flush()
        line = self.process.stdout.readline()
        if not line:
            raise RuntimeError("nf mcp closed: " + self.process.stderr.read()[:500])
        return json.loads(line)

    def call(self, tool, arguments):
        reply = self.request("tools/call", {"name": tool, "arguments": arguments})
        if "error" in reply:
            raise RuntimeError(f"{tool}: {reply['error']}")
        result = reply["result"]
        if result.get("isError"):
            raise RuntimeError(f"{tool}: {result['structuredContent']}")
        return result["structuredContent"]

    def close(self):
        self.process.stdin.close()
        self.process.wait(timeout=30)


def run_reference(nf, task, start_path, save_path, allow, original):
    """The ideal agent: the tool calls a correct run would make, through the real server."""
    client = Mcp(nf, start_path, allow)
    try:
        if task["task"] == "build":
            client.call("replace_part", {"document": json.dumps(original)})
        elif task["task"] == "edit":
            client.call("set_parameters", {"values": {task["edit"]["name"]: task["edit"]["value"]}})
        else:
            client.call("replace_part", {"document": json.dumps(original)})
        measured = client.call("measure", {})
        client.call("save_part", {"path": str(save_path)})
        return {"turns": 3, "measured_total_volume": measured["totals"]["volume"]}
    finally:
        client.close()


def run_claude(nf, task, start_path, save_path, allow, model, max_turns, timeout):
    """Claude Code headless with `nf mcp` as its only tool and no access to the golden."""
    config = {"mcpServers": {"nightfall": {"command": str(nf), "args": [
        "mcp", "--in", str(start_path), "--allow-path", str(allow)]}}}
    config_path = Path(allow) / "mcp.json"
    config_path.write_text(json.dumps(config))
    args = ["claude", "-p", task["prompt"].format(save_path=save_path), "--mcp-config",
            str(config_path), "--strict-mcp-config", "--allowedTools", "mcp__nightfall",
            "--output-format", "json", "--max-turns", str(max_turns)]
    if model:
        args += ["--model", model]
    env = {k: v for k, v in os.environ.items() if k != "CLAUDECODE"}
    try:
        run = subprocess.run(args, capture_output=True, text=True, timeout=timeout, env=env,
                             cwd=str(allow))
    except subprocess.TimeoutExpired:
        return {"error": f"claude timed out after {timeout} s"}
    try:
        payload = json.loads(run.stdout)
    except json.JSONDecodeError:
        return {"error": "claude returned no JSON", "stdout": run.stdout[-500:],
                "stderr": run.stderr[-500:]}
    return {k: payload.get(k) for k in ("num_turns", "total_cost_usd", "duration_ms",
                                        "is_error", "subtype")} | {
        "result": str(payload.get("result", ""))[:400]}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--nf", required=True, help="path to the nf CLI (occt build)")
    parser.add_argument("--parts", required=True, help="directory of reference .nightfall parts")
    parser.add_argument("--out", required=True, help="report directory (created)")
    parser.add_argument("--runner", choices=("reference", "claude"), default="reference")
    parser.add_argument("--tasks", default=",".join(TASKS), help="comma list of task kinds")
    parser.add_argument("--only", default="", help="comma list of part names to run")
    parser.add_argument("--model", default="", help="claude runner: model override")
    parser.add_argument("--max-turns", type=int, default=60)
    parser.add_argument("--timeout", type=int, default=900, help="seconds per task")
    parser.add_argument("--resume", action="store_true",
                        help="keep scored tasks from an existing report in --out; rerun the rest")
    args = parser.parse_args()

    nf = Path(args.nf).resolve()
    out = Path(args.out).resolve()  # the server resolves relative paths against --in
    out.mkdir(parents=True, exist_ok=True)
    kinds = [k for k in args.tasks.split(",") if k]
    only = {n for n in args.only.split(",") if n}
    parts = sorted(p for p in Path(args.parts).glob("*.nightfall") if not only or p.stem in only)
    results = []
    kept = {}
    if args.resume and (out / "report.json").exists():
        for record in json.loads((out / "report.json").read_text())["tasks"]:
            if record.get("passed") is not None:
                kept[(record["part"], record["task"])] = record
    with tempfile.TemporaryDirectory(prefix="nightfall-evals-") as golden_work:
        for part_path in parts:
            original = json.loads(part_path.read_text())
            for task in make_tasks(nf, part_path, golden_work):
                if task["task"] not in kinds:
                    continue
                label = f"{task['part']}.{task['task']}"
                if (task["part"], task["task"]) in kept:
                    results.append(kept[(task["part"], task["task"])])
                    continue
                work = out / label
                shutil.rmtree(work, ignore_errors=True)
                work.mkdir(parents=True)
                start_path = work / "start.nightfall"
                start_path.write_text(json.dumps(task["start"], indent=2) + "\n")
                save_path = work / "result.nightfall"
                started = time.monotonic()
                record = {"part": task["part"], "task": task["task"], "prompt": task["prompt"]}
                try:
                    if args.runner == "reference":
                        record["run"] = run_reference(nf, task, start_path, save_path, work,
                                                      original)
                    else:
                        record["run"] = run_claude(nf, task, start_path, save_path, work,
                                                   args.model, args.max_turns, args.timeout)
                except Exception as error:  # the harness keeps scoring the other tasks
                    record["run"] = {"error": str(error)[:500]}
                record["seconds"] = round(time.monotonic() - started, 3)
                run = record["run"]
                if infrastructure_error(run):
                    # The agent never worked: no verdict about the agent, rerun with --resume.
                    record.update(passed=None, errors=[{"code": "infrastructure",
                                                        "message": run.get("result") or
                                                        run.get("error", "")}])
                elif save_path.exists():
                    evidence, errors = build(nf, save_path, work)
                    if errors:
                        record.update(passed=False, errors=errors)
                    else:
                        passed, deltas = compare(task["task"], metrics(evidence), task["golden"])
                        record.update(passed=passed, deltas=deltas)
                else:
                    record.update(passed=False, errors=[{"code": "no_output",
                                                         "message": "no result.nightfall saved"}])
                results.append(record)
                verdict = {True: "PASS", False: "FAIL", None: "SKIP"}[record["passed"]]
                print(f"{verdict} {label} "
                      f"({record['seconds']} s)", flush=True)
    summary = {kind: {"passed": sum(1 for r in results if r["task"] == kind and r["passed"]),
                      "failed": sum(1 for r in results
                                    if r["task"] == kind and r["passed"] is False),
                      "not_run": sum(1 for r in results
                                     if r["task"] == kind and r["passed"] is None),
                      "total": sum(1 for r in results if r["task"] == kind)} for kind in kinds}
    report = {"generated": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
              "runner": args.runner, "model": args.model or None, "nf": str(nf),
              "rel_tolerance": REL_TOLERANCE, "summary": summary, "tasks": results}
    (out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(summary))
    return 0 if all(s["passed"] == s["total"] for s in summary.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
