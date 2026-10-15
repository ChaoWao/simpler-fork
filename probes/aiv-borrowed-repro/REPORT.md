# AIV borrowed readiness reproduction

## Objective

Reproduce the runtime's own correctness defect with valid arguments and ordinary
allocation/call/cleanup sequences. No invalid-pointer or memory-corruption
injection is used. A repeatable occurrence of the original VEC UB fault is the
criterion; a generic 507018 or test failure alone is not a match.

## Original observation

Original local run: task_20260928_192015_382784821124, device 3, PID 3833411.
The get-borrowed arm failed at native run 4. RTS plog reports
`VEC instruction error: the ub address out of bounds` at 19:20:26.312,
error 0x4000000000000000, stream 43/task 10, MTE errorCode=0.
The device scheduler reports task 0 RUNNING, completed 0/1 and timeout at
19:20:44.802. AICPU synchronization later returns 507018. This is an AIV
fault preceding the scheduler timeout, not evidence of a merely slow op.

The original logs are preserved at `/tmp/2475-original-fault/`. Root cause is
unresolved; PC attribution needs the corresponding incore address map/payload.

## Fixed baseline

Branch: investigate/aiv-borrowed-repro, based on PR #2475 head 1c095ade.
No production code edits. Project-local editable runtime/bindings rebuilt for
this head. All hardware execution uses task-submit and arch precheck.

## Experiments

| Batch | Workload | Result |
| --- | --- | --- |
| batch01-full-device3 | Original six pytest cases, 30 fresh pytest processes, device 3; 180 cases / 1080 native submissions | All passed; 324.036 s total child-process runtime; 0 target signatures |
| batch02-hot-device3 | Original get-borrowed case with its valid offset sequence repeated 300 times per Worker; 1800 submissions per round, 10 fresh processes requested | All 10 passed (18,000 normal submissions); 0 target signatures |

Batch 1 task: task_20260928_215418_171720924899.
Batch 2 task: task_20260928_231757_162218431001.

`run.py` runs the unmodified pytest cases. `run_hot.py` calls `hot_case.py`,
which changes only the test's loop count in memory: the `(5, 11, -4)` offsets
repeat, with the same allocation sizes, values, waits, result assertions and
cleanup. Each batch stores manifest.json, results.jsonl, and per-round pytest /
Ascend logs. The classifier matches the original plog text; the detector was
checked against the original evidence. A different failure stops the batch and
is reported separately. A target fault stops the batch for minimization; this
stopping rule must not be presented as an unbiased frequency estimate.

## Re-run

Set REPO to the investigation checkout and PROBE to this directory. Choose a
new OUTPUT directory for each invocation; the harness refuses to overwrite one.

```bash
cd "$REPO"
.claude/skills/onboard-arch-precheck/check.sh a2a3
task-submit --timeout 1800 --max-time 900 --device 3 --run \
  "$REPO/.venv/bin/python $PROBE/run.py --repo $REPO --output $OUTPUT --rounds 30"
```

For the hot-loop arm use `run_hot.py --rounds 10 --cycles 300` instead.
TASK_DEVICE is supplied by task-submit; direct hardware invocation is refused.

## Current conclusion

The original signature is classified, but a stable reproduction has not yet
been established. Passing repeats do not prove a fix. No speculative runtime
change or PR update is justified by these results alone.
