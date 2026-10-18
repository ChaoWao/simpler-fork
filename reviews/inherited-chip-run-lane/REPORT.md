# Refuse inherited chip runs before taking the lane mutex

## Scope and source

Baseline main `ce493639b45fb2ddf173e5198c78b6978567ed1c` includes #2615. PR [#2618](https://github.com/hw-native-sys/simpler/pull/2618), implementation head `996628a9e7edd44fa523c64e5e6ac66e371c4115`, branch `fix/refuse-inherited-chip-run-lanes`. This corrects an observable K2 process-refusal deadlock; it neither delivers S3's complete module owner nor adds capture support. No new public API, runtime query, version/size field, behavior flag or mode-specific executor is added.

Stable source keys: `ChipRun::{done,wait_until,activate,abandon,launched,lane_poisoned,preparation_disposition}`, `ChipRunLane::{submit,drain,close,poisoned,stop_admission,detach_after_failed_teardown}`, and `ChipWorker::{validate_execution_context,abandon_after_failed_teardown}`.

## Deterministic red control

The existing real lane suite supplies the backend's blocking wait. A parent thread enters that wait while owning the real lane mutex; a condition-variable handshake proves entry. The parent keeps the mutex held until its forked child exits. Each child has a two-second alarm. The child first exercises one of: run operations; lane operations; or destruction of the worker that owns the lane. The three baseline tests each terminate with SIGALRM (status 14), 3 failed / 0 passed in 6.002 s. This reproduces the concrete mutex deadlock, not an inferred silicon error.

The fixture uses host model callbacks, no NPU and no user address dereference. Its only thread join occurs after releasing the parent wait gate, including failed-fork/timeout paths.

## Implementation and proof boundaries

`ChipRunLaneState` retains an immutable creation PID independently of the mutable worker pointer. Every throwing public run/lane entry checks it before locking; even cached terminal status is process-bound. Non-throwing stop/detach returns without touching foreign state. The existing worker abandonment then releases its copied lane owner and abandons runtime storage; its raw context/library remain retained. No child device cleanup or DSO unload is inferred as safe.

The tests cover both live and terminal handles, both submit overloads, status/wait/activation/abandonment, drain/close, non-throwing stop, lane destruction and Worker destruction. A surviving child handle still refuses after its copied Worker is destroyed. Backend event histories stay unchanged in the child. The parent resumes its original run and completes a new run on the same lane.

This is a narrow refusal/abandonment contract. It does not make inherited device execution or arbitrary multi-threaded fork use supported. Current-context checks within the original process keep their prior behavior.

## Validation

On frozen head `996628a9`, all changed-file hooks pass and the canonical editable install succeeds. Full non-hardware C++ UT passes **327/327 targets**, 88.47 s, including **27/27 lane cases**. Full Linux Python passes **3415 / 127 skipped**, 141.83 s.

Full a2a3sim exits 0 in 205.44 s: 61 Resource jobs succeed; L2 HBG 14 passed / 7 skipped, TMR 31 passed / 1 skipped. Full a5sim exits 0 in 191.86 s: 57 Resource jobs succeed; L2 HBG 16 passed, TMR 30 passed. Job totals include inherited self-skipping cases; parent 346/357 deselections are scheduler delegation and are not additional assertions.

Exclusive A3 task `task_20261018_021507_2441430360` holds device 8 after architecture precheck. Real C ABI/caller-stream/public Worker suites pass **54 / 37 architecture deselections** in 209.59 s. The stateful eager directory then passes all **12 Resource cases**, 59.79 s. It covers owned/borrowed and both runtimes, stateful empty/nonempty calls and workspace-budget behavior. The task exits 0 and releases its lock; isolated ASCEND logs and before/after device occupancy are retained. Before the task, the queue and actual process snapshot show other work on devices 5/6, not device 8. No unexpected AIV is observed. This is targeted board validation, not a full onboard scene sweep; no local A5 hardware result is claimed.

CI is checked separately from these local results. No test result is relabeled as a measurement of the merge commit or a later head.

Raw local evidence is under `/tmp/fork-lane-*`: red/green test logs, rebuild/hooks, full C++/Python/simulator logs and a task-submit board directory with isolated ASCEND logs and occupancy snapshots. Raw material stays outside the product PR and the published design branch.
