# Per-run execution stream binding — local record

Baseline: `600540064` (main). Product branch: `refactor/run-execution-streams`.

## Scope

- `PreparedExecution::streams` captures actual Program streams before Runtime publication; both architectures consume that snapshot through launch, fence record, completion and wait-reference discharge.
- Existing A2/A3 stream-pair owner tracking and A5 runner-owned lifetimes remain responsible for retention. Copying handles grants no independent ownership.
- Borrowed context recovery poisons the runner and KernelExecutionState without whole-device synchronization. Program recovery remains unchanged.
- Public kernel launch is still rejected. Context-owned stream selection, caller fork/join, partial-enqueue resource retention and full kernel eager acceptance are next; this PR does not establish them.
- Existing publication rejection probe required migration: main's helper signature gained its route argument, and Prepared now means publish at launch. The probe now tests released/failed/empty states, with a separate real-launch arm accepting Prepared.

## Verification

- Red: against the baseline library, borrowed recovery incremented the whole-device sync counter (both A2/A3 runtimes).
- 8 real-library RTS-stub cases pass: two probes × two architectures × two runtimes. These use CANN headers/libraries but initialize no physical device.
- C++: 313 targets passed. Python: 3039 passed / 67 skipped.
- Ordinary simulation sweep: A2/A3 55 resource jobs + L2 45 passed / 8 skipped; A5 51 resource jobs + L2 46 passed. Resource job PASS can include a child skip, so these counts are not all assertions executed.
- Simulation ran without pytest-timeout installed (unknown-marker warnings); top-level per-session deadlines remained configured. Hardware validation has pytest-timeout installed.
- Board validation: separate detached worktree, main + product patch + #2536 (`280ee5286`). This is a combined diagnostic build, not the product-only binary. Exact patch and runtime SHA256 manifest are archived here. Task `task_20261012_194321_169459018719`, devices 2–5, task-submit plus external occupancy sampling.

## Carry-forward audit question

A2/A3 `cleanup_execution` reads `abandon` before `discharge_boundary_waits_noexcept`, whereas A5 reads it after. The discharge can itself poison the runner. Before enabling borrowed native launch, test that failure arm and verify no per-resource release follows an unproven wait. This is a source-level concern, not an observed AIV root cause or a reproduced regression in this PR.

## Final result

Product commit: `26d8abc38e511ef5f2022e5cefbfec5fcafafd29`. Board task exited 0: 100 resource jobs completed; L2 HBG 43 passed / 1 skipped, TMR 60 passed. No AIV reproduced. Pre-commit passed.

PR: [#2557](https://github.com/hw-native-sys/simpler/pull/2557), ready for review, CI running. Planning material and diagnostic overlay are local only.
