# Agent evaluation suite

Phase 3 gate G1 ([RFC 0011](../../docs/rfcs/0011-ai-design-loop.md)): can an agent build,
edit and repair the reference parts through `nf mcp`, judged by measured geometry?

## Tasks

For every part in `examples/parts/`:

| Task | Agent starts from | Prompt | Golden | Pass |
| --- | --- | --- | --- | --- |
| `build` | an empty part | the part's name and description, the parameter to name | the reference part | total volume and area within 1e-6 relative; same body count |
| `edit` | the part | set the first parameter to 1.5× its value | `nf build --set name=value` | every body within 1e-6 relative in volume and area, equal solid/face/edge counts, no body added or removed |
| `repair` | the part with the first parameter's unit removed (it fails to build) | the design intent; make the smallest fix | the reference part | as `edit` |

The agent sees only the starting document, the prompt and the MCP tools; it never sees the
golden. The harness measures the saved result independently with `nf build` and compares
the evidence record. A rebuild that succeeds with the wrong geometry fails; a silent change
to a body the task did not mention fails.

## Runners

- `reference`: a deterministic client that makes the ideal tool calls through the real
  server. It checks the harness and the MCP contract, not a model; CTest runs it as
  `evals.reference` under the occt preset.
- `claude`: Claude Code headless (`claude -p --mcp-config … --allowedTools mcp__nightfall`).
  Needs `claude` on the path and a signed-in account; each task is one session with
  `--max-turns` and `--timeout`.

```sh
python3 -I agent/evals/run.py --nf build/occt/tools/cli/nf --parts examples/parts \
  --out out/evals-claude --runner claude --tasks build,edit,repair
```

`--only washer,hex_spacer` limits the parts; `--model` overrides the model. A task whose session
could not start (an account or rate limit, a missing `claude`) is reported as not run
(`SKIP`, `passed: null`), never as an agent failure; `--resume` keeps every scored task
from the existing report and reruns the rest.

## Reading a report

`<out>/report.json` holds `summary` (passed/total per task kind) and one record per task
with the prompt, the run (`num_turns`, `total_cost_usd`, `duration_ms` for `claude`), the
measured deltas against the golden and any `nf build` errors. Each task's folder keeps
`start.nightfall`, the agent's `result.nightfall` and the measured STEP. The process exit
status is 0 only when every task passed. WORKFLOW §6: attach the report to any PR that
changes prompts, tools or providers; the pass rate may not drop.
