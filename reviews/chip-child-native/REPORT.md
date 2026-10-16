# Forked chip child uses the native binding

2026-10-15. Base main `3351950f4b0fdbd60523ea63266b321ab12f5f2e` (#2593 merged). Current head `2358c5a36a4bc389cfbf811d8f999ccb8eab7561`, PR #2595.

## Scope

Child startup constructs the existing `_ChipWorker` and consumes `_initialize_chip_runtime`, just as direct L2 does. Initial/dynamic registration, materialized run submission, lane control, legacy/global communicator methods and teardown all address that native instance. The child retains its identity tables, imports, provider/global domain stores and base communicator control value; native retains the physical context/runtime/communication resources. No executor, public argument type, capability bit or wire format is added.

The public compatibility `ChipWorker` remains available. Its lifecycle locks and registry are not child state owners: the serve loop is single-threaded, local slot identity belongs to the loop, and its ImportRegistry is already closed by child resource teardown. Host-log flushing formerly performed by wrapper finalize is retained after raw finalize, including the provider-store startup failure path. Teardown observation is still published from the existing finally path.

This change does not centralize materialization or deliver public borrowed eager. Those remain subsequent K2b/K2c steps. Pipeline A admission, completion, resource counts and mailbox commands are unchanged.

## Validation

| Check | Result | Evidence |
| --- | --- | --- |
| Strict native collaborator against original consumer | 10 fail on `_impl` access; native consumer accepts the same collaborator | [red](../../LOCAL-EVIDENCE.md#artifact-0d99ca702a4d) |
| Full Linux Python UT before commit | 3285 passed, 91 skipped; one existing PyTorch warning | [log](../../LOCAL-EVIDENCE.md#artifact-02b874b2befe) |
| A2/A3 simulation | 9 scene items passed | [log](../../LOCAL-EVIDENCE.md#artifact-706dbaba9b42) |
| A5 simulation | 6 scene items passed | [log](../../LOCAL-EVIDENCE.md#artifact-639061704434) |
| A2/A3 hardware | 15 scene items passed, task-submit devices 2/3/4 | [log](../../LOCAL-EVIDENCE.md#artifact-f51db90ed224) |
| Hooks | All eight changed files passed | [log](../../LOCAL-EVIDENCE.md#artifact-7f733615e1f6) |

Task `task_20261015_214857_330866810062` holds all selected devices through the hardware run. Device logging is isolated under `/tmp/chip-child-native-board/ascend`. No unlocked device invocation is used. These are targeted runs, not a full onboard sweep; A5 hardware and macOS are not locally available.

Hardware selection includes four HBG FIFO cases, six TMR FIFO cases (including the multi-rank case), and five TMR communication cases (async/deferred notify, allreduce, rank mapping, dual domains). Dynamic-register scene files are simulator-only and are deselected onboard. A3 sim covers all five dynamic-register cases and four communication cases; A5 sim covers four dynamic-register cases and two communication cases. Do not equate the selected path list with executed cases.

Two new host-only cases invoke real native initialization/finalization through a compiled stub runtime, inspect native registration and teardown counters, and check Host-log flushing on normal exit and injected startup preparation failure. The successful arm verifies memory round-trip and cleared native environment after finalize. This is not actual child device execution; real fork/dispatch is covered by existing migrated harness and scene cases. The communicator tests retain exact-size and page-rounded real shared-memory coverage.

## Next

Unify the direct L2 and child argument-materialization boundary while preserving target-specific authorization and ImportRegistry lifetime. K2c public borrowed init/submit and complete dual-runtime eager acceptance follow. No capture/replay capability is claimed here.

Final committed head `2358c5a3` rebuilt successfully; **306 focused cases passed**. Product PR: [#2595](https://github.com/hw-native-sys/simpler/pull/2595). CI pre-commit and docs build have passed; Python/C++ UT, simulator and hardware jobs are pending or running. No all-green cross-platform result is claimed.
