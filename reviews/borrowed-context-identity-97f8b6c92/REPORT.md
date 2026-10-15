# Borrowed RTS context identity — #2510

2026-10-08 · base main `97f8b6c92` · head `a5a10c51`.

## Contract and scope

Same device does not imply same RTS context. Record the current native context before allocating kernel resources; require it for callable prepare/unregister, run prepare/finalize, and context close. Reject without rebinding; restore the original context to retry. Keep identity after stream/event close because other resource owners can still need cleanup retries. Caller keeps the native context alive; pointer comparison cannot detect external destruction/address recycling. `context_generation` is not yet consumed and kernel launch remains disabled.

## Validation

- Old code negative controls: host state test releases resources under the wrong context; onboard HBG/TMR fail the new refusal assertion. The original onboard assertion did not print its return value, so these logs prove assertion failure, not a specific numeric return.
- Fixed code: C++ 297/297; Python 2990 passed, 59 skipped; pre-commit passed.
- A2/A3 simulator and A5 simulator full sweeps: both exit 0.
- A2/A3 C ABI lifecycle: 29 passed, 17 deselected, 70.74 s; task `task_20261008_051218_235874515893`. Two actual contexts on one device, no simpler ownership calls, wrong-context rejection and retry covered.
- Full Program sweep: exit 1, 590.69 s; task `task_20261008_051342_240982619974`, devices 0/1/2/3. Four failed resource jobs; later L2 HBG 43 passed/1 skipped and TMR 60 passed.
- Exact base selected-case comparison: all four pass, 27.65 s; task `task_20261008_052228_291312114407`, same four devices. Different workload from the full sweep: cannot classify this as an introduced regression or a pre-existing defect from that result alone.

## Four device faults

| Test | PID / device | VEC fault current PC |
| --- | --- | --- |
| early-enqueue depth-one | 2434840 / 3 | 0x12c0c00ee75c |
| device-result chain | 2434650 / 1 | 0x12c0c00ee75c |
| dep-gen across runs | 2440567 / 0 | 0x12c0c0101f5c |
| worker_async_endpoint | 2474492 / 2 | 0x12c0c00ed39c |

Plogs record VEC UB address out of bounds before scheduler timeout. These are device exceptions, not inferred slow kernels. Earlier #2507 data contains this fault class and the same dep-gen PC, without a proven root cause. No stable reproduction or production fix is claimed.

## Occupancy evidence and limits

`failure-occupancy-extract.txt` preserves actual NPU PIDs and ancestors. At 12:13:59–12:14:07 UTC, PIDs 2434650, 2434840 and 2440567 all descend from this task's pytest runner 2411220, npu_lock.sh 2410471, and task-daemon 2410053. No foreign NPU PID appears in those samples. This is sampled evidence, not a proof of exclusive access throughout all four failures.

The in-task `task-submit --list` returned rc=1 (nested-task rejection), so its queue snapshot is missing. The external sampler began after the faults; its successful queue records cannot retrospectively fill that gap. A second external sampler covers the main selected-case comparison from before task execution.

Raw logs are adjacent, plus selected failing PID logs in `ascend/`. Planning and temporary diagnostics stay outside the code PR. PR #2510 is ready for review at unchanged head a5a10c51, as requested by the maintainer. Hardware attribution remains unresolved; active reproduction is paused. The updated PR body includes the later full-sweep controls and pending A5 hardware CI. Merge remains the maintainer’s decision.

## Final-head selected-case repeat

The same four selected tests also pass on the final PR head after rebuilding (25.53 s). The failure remains sweep-dependent in these observations; this clean rerun is not a fix.

任务已提交: task_20261008_052522_33412813037 (断开后可用 task-submit --wait task_20261008_052522_33412813037 重连)

## Later full-sweep controls

See ../../probes/hbg-aiv-trigger-20261008/REPORT.md for the second failing full sweep, the passing exact-base full sweep and the passing branch full sweep after three first-wave repeats under one continuous reservation. These passing results do not classify the fault or establish a fix. The published body is preserved in pr-body-ready.md.

## Shared-entry revision

Superseded by `3c8cad19` on main `60167bcc`: context preflight is unified at runner entry points and architecture cleanup uses the non-virtual checked finalize entry. Current validation and the newly retained AIV scene are in [the revision report](../pr2510-context-refactor/REPORT.md).
