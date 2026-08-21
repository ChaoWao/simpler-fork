---
name: measure-bind-path
description: Measure host_build_graph's host-side bind path (orchestration, Definition upload, H2D) on the dsv4 and qwen decode cases, and compare two branches. Use when the user asks how long the bind path or control plane takes, whether a change moved host_orch / graph_upload / sm_h2d / arena_h2d, or to A/B a host-side change. This is the HOST path with the device run skipped — for on-device latency use `benchmark` or `perf-example-device` instead.
---

# Measuring the `host_build_graph` bind path

`host_build_graph` builds the whole task graph on the host before the device
executes anything, so its **bind path** is a first-class cost. This skill drives
[`tools/hbg_bind_measure.sh`](../../../tools/hbg_bind_measure.sh); the reasoning
behind every switch and every trap lives in
[`docs/dfx/hbg-bind-measurement.md`](../../../docs/dfx/hbg-bind-measurement.md).
Read that when a number looks wrong. Do not re-derive the flag combinations by
hand — each way of getting them wrong is silent, which is why the script exists.

**The device never runs.** Every mode sets `SIMPLER_SKIP_DEVICE_RUN`, so this
measures the host and nothing else, needs no working device execution, and costs
no device time waiting for one to fail.

## Prerequisites

```bash
python3 -m venv --system-site-packages .venv     # once per worktree
source .venv/bin/activate
pip install --no-build-isolation -e .            # after EVERY change to HEAD
```

**Rebuild after every HEAD move**, including a rebase or an `--amend`. The
extension does not rebuild on import; a stale one aborts the run with
`_task_interface was built from <sha>, but this source tree is at <sha>`. That
guard is the good case — heed it rather than working around it.

Onboard work goes through `task-submit`, which holds the device lock for the whole
job (see [`running-onboard.md`](../../rules/running-onboard.md)).

## The two cases

| Property | dsv4 FLASH decode | qwen3-14b decode |
| -------- | ----------------- | ---------------- |
| Devices | **2** (EP2/TP2) | **1** |
| Level | 3 | 2 |
| Host tasks | 129 (seven Graph Definitions) | 47 |
| Passes per `-n N` | 2N (one per rank) | N |
| Compile on a cold cache | **minutes** (369 kernels) | seconds |

Both are `manual`, which the script already selects.

## Recipe — per-phase numbers

```bash
# dsv4 (the default case)
task-submit --device auto --device-num 2 --timeout 1800 --max-time 1800 \
  --run "tools/hbg_bind_measure.sh -d \$TASK_DEVICE -m stats -n 5"

# qwen: one device, level 2, so pass its example and case and clear the level
task-submit --device auto --device-num 1 --timeout 2400 --max-time 2400 \
  --run "tools/hbg_bind_measure.sh -d \$TASK_DEVICE -m stats -n 5 \
     -e examples/a2a3/host_build_graph/qwen3_14b_decode/test_qwen3_14b_decode.py \
     -c 'TestQwen314BDecodeHostBuildGraph::' -l ''"
```

The script prints each phase's min / median / max over the warm passes and the
**control-plane total** — `host_orch + graph_upload + relocate + sm_h2d +
arena_h2d`, everything between "the caller's data is in place" and "the device can
start". `args` and `host_view_close` are excluded: they scale with the caller's
tensor bytes, not with the graph.

## Recipe — a timeline inside `host_orch`

```bash
task-submit --device auto --device-num 2 --timeout 1200 --max-time 1200 \
  --run "tools/hbg_bind_measure.sh -d \$TASK_DEVICE -m swimlane"
```

Prints the paths of the per-event records and a swimlane JSON; load it in
[Perfetto](https://ui.perfetto.dev). Use this to see *where inside* `host_orch`
the time goes — which producer recorded what, and where the gaps are.

**The two modes are mutually exclusive by construction, not by choice.**
`--rounds > 1` force-disables every diagnostic, so there is no output directory
and no per-event artifact. Many rounds give statistics; one round gives a
timeline. Asking for both in one run silently yields no artifact.

## Comparing two branches

This is a different measurement from a single reading, and it has produced wrong
answers on this box. Three rules, all of them learned the hard way:

1. **Both arms must be the same ruler, and it is not enough that one of them used
   the script.** A hand-assembled baseline once lacked
   `TORCH_DEVICE_BACKEND_AUTOLOAD=0`, so it alone paid for `torch_npu` grabbing a
   device on import, and the gap was credited to the branch. The script stamps its
   whole environment as the log's first line:

   ```bash
   diff <(head -1 base.log) <(head -1 measure.log)   # must differ only in head=
   ```

2. **Interleave the arms.** `base` then `measure` attributes every drift in host
   load to the branch, and the drift is larger than most effects worth measuring.
   Run `A, B, A, B` — rebuilding between each — and require the per-repetition
   delta to **agree in sign**. A repetition that disagrees says the run was
   contended, not that the effect is small.

3. **Compare minima, never means.** On a shared box the mean measures the other
   tenants. Report the spread too: a change that lowers the floor while widening
   the range has made the cost less predictable, which is a cost of its own.

Use `-o` to name each log, or the second run of an arm overwrites the first.

```bash
# one repetition; repeat it, alternating, and check the sign each time
git checkout A && pip install --no-build-isolation -e . && \
  task-submit ... --run "tools/hbg_bind_measure.sh -d \$TASK_DEVICE -m stats -n 5 -o /tmp/A1.log"
git checkout B && pip install --no-build-isolation -e . && \
  task-submit ... --run "tools/hbg_bind_measure.sh -d \$TASK_DEVICE -m stats -n 5 -o /tmp/B1.log"
```

**A phase can disappear between arms, and that is a result, not an error.**
Folding the shared-memory image into the arena copy retires `relocate` and
`sm_h2d` outright. The parser totals over the phases a run has and names the
absent ones; a retired phase contributes nothing, so read which set each total
covers before comparing them.

## Reading the result

- **`host_orch`** is all orchestration: every task submitted, every Graph node
  recorded, the Definition built. On dsv4 it is where a change to the submit or
  recording path shows up.
- **`graph_upload`** is the Definition objects and replay boundary images. On a
  many-Definition workload this can exceed `host_orch` — check it before assuming
  orchestration dominates.
- **The first pass of each rank is warm-up** and is dropped. Pass `--rounds` (the
  script does) so the parser infers the rank count and drops one cold pass *per
  rank* rather than one in total.
- **A min of sums is not a sum of mins**, and they can disagree in sign. The
  parser sums within a pass before taking any minimum; never assemble a total from
  per-phase minima by hand.

## Traps

| Trap | Symptom | What to do |
| ---- | ------- | ---------- |
| Stale extension after a HEAD move | run aborts naming two SHAs | `pip install --no-build-isolation -e .` |
| dsv4 run through the module runner's L3 phase | log has zero `bind phase=` lines, test passes | keep `-l 3`; the script builds the child command itself |
| Golden-checking case with the device skipped | `Golden mismatch on '<tensor>'` | the script passes `--skip-golden`; the device produced no outputs to compare |
| Same `-o` for two runs of one arm | second overwrites the first | name each log |
| Comparing an unstamped log | parser says "no environment stamp" | re-run it through the script; do not compare conditions from memory |

## Relation to the other skills

- [`benchmark`](../benchmark/SKILL.md) and
  [`perf-example-device`](../perf-example-device/SKILL.md) measure **on-device**
  latency. This skill measures the host path with the device skipped; they answer
  different questions and neither substitutes for the other.
- [`dfx-analyze`](../dfx-analyze/SKILL.md) covers the device-side DFX tools.
