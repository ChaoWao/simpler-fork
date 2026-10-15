# HBG AIV trigger investigation — unresolved

The earlier pause was revoked by the user. Two subsequent full replays and a callable-upload observation arm completed; the defect remains unresolved. See [2026-10-09 replay report](../hbg-aiv-same-conditions-20261009/REPORT.md). Next-incident preservation and replay procedure: [NEXT-FAILURE.md](NEXT-FAILURE.md). Local archive helper: `freeze_scene.py`; offline fixture smoke passed, including overwrite rejection. Neither helper nor procedure is wired into CI.

Fixed branch head a5a10c51, main control 97f8b6c92. No production code changes.
All hardware work goes through task-submit; external occupancy collection starts before submission.

## Completed experiments

| Workload | Devices | Outcome |
| --- | --- | --- |
| Exact full Program sweep, branch | 0/1/3/4 | Four target VEC UB faults; 442.75 s pytest time |
| Full collection, four actual tests selected | same | Pass; 26.92 s |
| Full collection, exact first-wave four jobs (including skipped test_run) | same | Pass; 26.91 s |
| Full collection, early prefix (12 test items) | same | Pass; 43.91 s |
| Exact main control, full sweep | same | Pass; 568.08 s pytest time, zero target signatures |
| Continuous reservation: first-wave repeats 1–3 | same | All pass; 29.48 / 29.77 / 29.79 s wall time |
| Same reservation, full branch sweep after those repeats | same | Pass; 441.84 s wall time, zero target signatures |
| Same reservation, first wave with copy interposer | same | Pass; 29.05 s wall time, zero target signatures |

The full sweep failed depth-one, device-result chain, dep-gen across runs and depth-two two-runs. Endpoint passed in this round. Prior full sweep on this head failed endpoint instead of depth-two. Failure count is not a count of independent reproduced sweeps.

Both Python and libc environment changes during collection were inspected. Full and selected full-collection arms have identical changed environments; narrower-file collection additionally lacks KMP_DUPLICATE_LIB_OK=TRUE. Full-collection selected arms pass, so that variable alone is not an established trigger.

## Occupancy

Initial 60 seconds include 13 occupied samples. Our failing child PIDs are on devices 0/1/3/4. Other users' sampled processes are on 11/13, outside the reservation. Raw task queue, NPU PID and ancestor records are retained. This is sampled evidence, not uninterrupted exclusion proof.

## Completed controls and remaining boundary

Exact main full sweep and the continuous reservation have completed. The latter held devices through three first-wave repeats and the full branch sweep, with no release between steps. All passed. Main waited behind a registered all-device task; its queue-inclusive duration is not its execution time.

Thus full workload alone is not a sufficient trigger. This is consistent with a history/timing-sensitive failure, but does not establish which state or timing matters, nor absolve the feature branch. The four failures in the first sweep remain unresolved. Full-vs-filtered collection generated identical first-four job argv/cwd/env records (`aiv-dispatch-full.json` and corresponding first-wave record); later dispatch/refill and runtime state are not covered by that equality.

A separate LD_PRELOAD observation arm captured only 15 small successful synchronous H2D writes from one PID. None of their address ranges covers the prior fault PCs. This capture is insufficient to identify the faulting incore image; it is not evidence of missing production uploads. The interposer currently excludes writes over 256 KiB and stops after 128 accepted copies per process. It also changes timing, so its passing result is not an uninstrumented control.

`device-code-section-comparison.json` compares executable artifacts rather than entire ELF hashes: AICore executor `.text` is identical between branch and main for HBG and TMR; AICPU `.text` differs. The differences have not been attributed. This comparison does not cover the separately compiled incore callable images and does not establish binary equivalence of the full execution path.

Next useful diagnostic step: obtain complete callable upload address/size/bytes coverage for every tested child, verify that coverage before another fault campaign, then preserve those records on a failing run. Do not interpret a passing instrumented run or another warm sweep as a fix.

No minimal reliably red trigger has been established yet. The outcome classifier requires the original VEC UB signature; generic timeout is a separate outcome. `elapsed` in launch.py includes task queue wait; pytest times above exclude it.
