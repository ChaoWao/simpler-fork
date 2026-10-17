# Borrowed internal workspace management and budget

Baseline: main `e6d546d4c6a98e1a87bc80cd0418806630c027f9`, including #2608 and Pipeline A #2596. Branch `feat/borrowed-workspace-budget`, draft [PR #2610](https://github.com/hw-native-sys/simpler/pull/2610), head `086fe83ed152a9489da7cefb9a595e548c94ded7`. Full committed-head sweeps and CI are in progress.

## Functional scope

Both onboard ownership policies use the existing internal workspace manager. Public borrowed Worker accepts the existing optional `workspace_budget_bytes`; it is staged before native borrowed initialization and installed before arena allocation. No budget means management without a byte ceiling. No new API, capability-query struct, ABI version or executor is introduced.

The budget covers retained temporary backing and the three pooled arenas, including retained generations. It excludes caller tensors, registered code, RTS arguments, diagnostics and providers. Caller context/stream/allocation remain caller-owned; internal memory ownership does not imply device ownership. Capture remains refused.

## Red evidence

Baseline plus the new tests: four native policy cases fail and one public fixture errors because management is disabled or explicit budget is rejected. Exclusive A3 task `task_20261016_185642_412419131934` holds device 8; all six new resource cases fail on baseline. Explicit budgets are rejected before init; the no-budget borrowed path reports disabled. These are intended failing controls, not device faults.

## Integration and limitations

Public Worker → native ChipWorker → existing budget staging → borrowed init → existing WorkspaceManager → native prepare/launch/finalize. Positive budget cases use 64-task rings and 1 MiB per-ring heaps to make a 128 MiB bound meaningful; TMR defaults alone request about 1 GiB of heap.

Tests cover both runtimes with no limit, 128 MiB, and 1 KiB refusal, and continue using the same caller RTS context/stream/allocation after Worker.close. Active writes and untouched sentinel words distinguish real execution from a no-op. The refusal arm checks the budget diagnostic and unchanged output.

Existing public native-exception behavior is deliberately preserved: a prepare failure returns no RunHandle, retains source uses and marks the source context failed. Worker.close does not release those uses; Buffer.close continues to refuse. The refusal subprocess retains its external allocation until process exit. No immediate retry or public release proof is promised. This unresolved gap is recorded locally in KNOWN_ISSUES.md and remains a separate proof-carrying failure/retirement task.

Initial integration identified this source-retention limitation and an undersized TMR test budget; that run exited 1 and is not counted as a pass. After using explicit small-ring sizing and asserting the existing conservative source policy, all 12 public stateful/budget cases pass in an exclusive A3 task. Full validation is in progress.

## Next work

Carry actual no-submission/completion evidence through the existing native error boundary so a refused prepare can release exactly its source reservation without weakening partial-enqueue/fault retention. Continue framework/DFX/fault eager qualification, then C1–C4 capture/replay. Do not add a capability negotiation layer as a prerequisite.

## Validation receipts

- Full Linux Python UT: 3405 passed / 107 skipped, 138.21 s.
- All 327 C++ UT targets pass, 11.49 s.
- Changed-file hooks pass, including clang-tidy, pyright and Markdown.
- Final committed head rebuilt; focused host checks: 151 passed, 7.56 s.
- Exclusive A3 working-content integration `task_20261016_190013_416590820251`: all 12 stateful/budget resource jobs pass, exit 0, 59.70 s.
- Final committed-head A3 task `task_20261016_190420_530193363` holds devices 8–11; lifecycle, stateful and full ordinary sweep run sequentially with occupancy/PID snapshots and isolated ASCEND logs. Still in progress.
- Full A3/A5 simulator sweeps pass on the committed head (177.69 / 172.38 s). A3 dispatches 61 Resource jobs and both L2 groups; A5 dispatches 57 Resource jobs and both L2 groups. Resource jobs include skips and are not individual test-pass counts. No local A5 hardware qualification is claimed.

Final committed-head A3 lifecycle phase passes: 34 inline cases and three dispatched management cases, exit 0 (75.96 s). All 12 stateful/budget Resource jobs pass again, exit 0 (14.95 s). The full ordinary A3 sweep is still running.
