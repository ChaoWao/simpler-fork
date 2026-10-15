# HBG AIV sweep fault: reproduction follow-up

## Conclusion

**No stable reproduction established. The original defect is unresolved.**
The four failures were four cases within one sweep, not four independently reproduced sweeps. No production code or timeout setting was changed for this investigation. All hardware executions held task-submit device locks.

## Fixed revision and observations

Revision `d0cb7503`, base `121d807bb`; runtime binary hashes and PTO pin are in `manifest.json`.

| Experiment | Devices | Result |
| --- | --- | --- |
| Original full sweep, task_20261008_001317_129360232258 | 5,6,7,8 | Four HBG failures with VEC UB bounds signature before scheduler timeout |
| Full repeat, task_20261008_024717_137377928467 | 3,5,6,7 | Exit 0, 430.42 s pytest time; 95 resource cases, HBG L2 43 passed/1 skipped, TMR L2 60 passed |
| Four-case batch, 10 fresh pytest/Worker rounds, task_20261008_025511_209464617649 | 3,5,6,7 | 40/40 case executions passed; zero target signatures |

The original four-device assignment could not be retained: device 8 was occupied by another task. Its pending task was canceled before execution; the new batch used the same four devices as the full repeat. This is not an exact original-device replay.

The signature classifier was checked against all four original plogs: each is detected. Success requires inspecting the device logs as well as pytest outcomes. Each round selected exactly four cases, with four concurrent resource subprocesses. The dep-gen case itself runs more than one graph; 40 is the number of pytest case executions, not kernel launches.

## Cases and discriminating signal

- `TestEarlyEnqueueDepthOneControl::test_no_overlap_without_the_opt_in`
- `TestEarlyEnqueueDepthTwo::test_sustained_refill_overlaps_successive_run_pairs`
- `TestDeviceResultChain::test_a_device_result_feeds_the_next_two_runs`
- `TestHbgDepGenAcrossRuns::test_run`

Exact detector: `VEC instruction error: the ub address out of bounds`.
The original three early-enqueue plogs share current PC `0x12c0c00ee29c`; dep-gen reports `0x12c0c0101f5c`. These are locators, not a root-cause diagnosis. The original cases fail on their first run, so accumulated state in a long sweep is not established.

## Re-run the bounded loop

From the fixed checkout, set PROBE to this directory and OUTPUT to a fresh writable directory. The loop consumes TASK_DEVICE supplied by task-submit:

```bash
.claude/skills/onboard-arch-precheck/check.sh a2a3
task-submit --timeout 7200 --max-time 1800 --device auto --device-num 4 \
  --run ".venv/bin/python $PROBE/repeat.py --work $PWD --out $OUTPUT --rounds 10"
```

The original command, commit, device assignment, return code and matching plog lines are retained in each round's `result.json`. Full raw logs are alongside this report. A reused OUTPUT must not be used for comparing independent batches.

## Limits and next evidence

Passing repetitions do not classify the original failure as environmental, establish baseline parity under the original full-sweep conditions, or prove a fix. Prior targeted baseline comparisons also passed and cannot settle attribution. The Phase 1 diagnostic loop is executable but not yet reliably red; no speculative runtime fix follows from this result.

Before another broad stress campaign, retain the per-run loaded incore image address/size/hash and matching binary so the original fault PC can be attributed on the next occurrence. Compare a genuinely cold build/load with a warm run under otherwise identical conditions; neither cache state nor image corruption is currently established as the cause.

## Required occupancy check on the next hardware failure

User instruction (2026-10-08): check whether another process bypassed task-submit and occupied the same device. A task-submit lock coordinates cooperating users; it does not exclude a process that never takes the lock.

- Before starting, periodically during a reproduction run, and immediately on failure, save timestamped `task-submit --list` and actual NPU process/occupancy information from `npu-smi info`. If the read-only query fails for lack of DCMI permission, retry the query through task-submit without requesting a device lock.
- Map the task's logical devices to the chip/die reported by the driver. For unexpected PIDs, retain UID, PPID, process start time and enough command/process-tree information to identify the owner and workload. Match the complete pytest/Worker child process tree, not just the task launcher PID.
- Compare actual device users with the queue's assigned tasks. An unexplained PID is a lead, not proof of bypass; verify ancestry and overlapping timestamps before classifying contention. Do not kill or reset other users' processes/devices.
- Preserve these snapshots with the fault logs. A post-failure empty process table does not rule out a competing process that already exited.

The original four faults lack a contemporaneous device-PID-to-task correlation. External contention has therefore **not been ruled out**, and is not established as their cause either.
