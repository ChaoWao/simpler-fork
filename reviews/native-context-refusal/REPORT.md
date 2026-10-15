# Native current-context refusal classification — PR #2581

2026-10-14. Main baseline: `7c7ec43d51ae1bdbeb955dd7ad13896fd5e0025f` (includes #2580).
PR [#2581](https://github.com/hw-native-sys/simpler/pull/2581): `e92d952bb282b3d19d2383e1da9c8fe636fc35a5`.
The code PR contains 11 files, +117/−43; this evidence and the design HTML stay outside the repository.

## Delivered boundary

`ChipWorker::validate_execution_context` throws `ExecutionContextRefused` for its current-context preflight refusal. Existing guard positions remain; C ABI/status codes unchanged. Nanobind preserves a RuntimeError subclass, with canonical public import `simpler.task_interface.ExecutionContextRefused`.

`RunHandle.wait/result` distinguish this category from an execution verdict. Refusal clears waiter election through the existing unwind and retains run ID, argument snapshot, registry/source uses. Native wait supplies validation, replacing #2580's stored Python callback. Restoring the environment allows completion; ordinary RuntimeError with the identical context message is still terminal. Owner destruction keeps terminal lane poison, never retryable context refusal.

The underlying onboard validator checks current device/context only. It returns no run completion verdict and does not inspect kernel fault status. Native terminal lane errors are separately wrapped as poison.

## Evidence and scope

| Check | Result | Raw evidence |
| --- | --- | --- |
| Before fix | 3 failed: missing native category plus wait/result terminalization when native validator emits refusal inside a controlled wait | [red.log](../../LOCAL-EVIDENCE.md#artifact-fd3955154e6e) |
| Initial focused tests | 108 passed | [green.log](../../LOCAL-EVIDENCE.md#artifact-97b5f4d8f5d2) |
| Python UT | 3209 passed, 91 skipped | [pyut.log](../../LOCAL-EVIDENCE.md#artifact-3b3332e1460f) |
| C++ UT | 317 targets passed, including typed phase/poll/wait/close rejection, restoration, and terminal owner destruction | [cpp.log](../../LOCAL-EVIDENCE.md#artifact-65881e0ed0c4) |
| L2 Program simulation, manual cases included | A2/A3 6 cases; A5 3 cases passed | [A3](../../LOCAL-EVIDENCE.md#artifact-d44ad0d80e23), [A5](../../LOCAL-EVIDENCE.md#artifact-ee9a134b2560) |
| Hardware | 44 passed, 27 deselected, 166.84s; task-submit with one device (device 0), both runtimes and owned/borrowed depths 1/2 | [run.log](../../LOCAL-EVIDENCE.md#artifact-10aac7945a64) |
| Final committed head | Editable binding rebuilt; 360 related tests passed | [build](../../LOCAL-EVIDENCE.md#artifact-5d608d4841ce), [tests](../../LOCAL-EVIDENCE.md#artifact-601bb79c0485) |
| Style/static checks | All changed-file hooks passed | [precommit.log](../../LOCAL-EVIDENCE.md#artifact-19a46c5b8d5c) |

The new DSO test uses a real C++ validator and real nanobind translation, with a controlled Python fence, not a full native ChipRun execution. Hardware separately exercises real borrowed ChipRun runs bridged into Python RunHandle. Neither stands in for complete public borrowed Worker.init/submit qualification. Full matrix tests ran on the pre-commit tree with the same runtime/binding code; the final commit was then rebuilt and the related tests rerun.

Hardware runs went through task-submit after architecture precheck, with continuous [occupancy sampling](../../LOCAL-EVIDENCE.md#artifact-2718c7b0d65a) and independent [Ascend logs](../../LOCAL-EVIDENCE.md#artifact-cdfa76e2552c). This run did not diagnose or fix the separately tracked intermittent AIV/A5 failures.

## Remaining required work

K2a is not finished. Next, consume the same environment predicate before Python submit/admission, register/unregister and close commit their logical state; verify related done/native/C ABI paths. An unaccepted refusal must not poison source or terminalize façade state. Partial enqueue and accepted close debt remain conservative, not reversible by blanket exception handling.

Then K2b binds environment identity/generation/source and consolidates internal access; K2c exposes the complete public borrowed eager call path and qualifies both runtimes. Capture/replay remains later. Historical #2580 evidence stays at its original head.
