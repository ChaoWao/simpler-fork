# Unstarted launch: retire this invocation's source references

Baseline: main `49ae1ef1be8ab1e995ab12e1967721ef1421b86e` (#2612). Branch `fix/release-unstarted-launch-sources`; [PR #2614](https://github.com/hw-native-sys/simpler/pull/2614), current head `fc24b0d0a9a20262a1223f700151b5e67150b0cd`; submitted for review, not merged.

## Exact behavior

A successfully prepared call can fail launch setup before any execution-visible submission. In the baseline, a completion-event creation failure returns -4321 with NotStarted progress. Finalize releases the native bindings and returns zero, but ChipWorker also retains the original launch rc as the completed run's result. The lane treats that result as failed cleanup, poisons further admission and never releases the public invocation's source reservation. The same Worker/Buffer cannot retry; Buffer.close still refuses after Worker.close.

This PR consumes the same per-epoch clean-refusal proof as #2612 after successful finalize of an explicitly unstarted launch. It preserves the original launch error message/code, releases only this invocation's references and allows a valid retry in a usable environment. It adds no public exception class or configuration option. RunPreparationRefused retains its fact-based contract: no operator submitted and all native input/preparation ownership released.

## Red and negative controls

- Real C API over CPU transport: all four TMR/HBG × A3/A5 native-admission targets fail the new exact-epoch receipt assertion on baseline behavior (0.14 s).
- A3 baseline task `task_20261017_015443_9001029576`, exclusive device 8: both public runtime arms fail. The existing shim refuses completion-event creation with -4321; logs show arm_run_fence failure before launch, then retry failure and Buffer.close reporting unproven uses. Two cases fail in 8.17 s.
- The first board submission (`task_20261017_014730_2374327057`) was cancelled while pending because all cards were held; it ran no NPU work and is not evidence.
- Consumer negative control: replace the lane's proof-consuming finalization with the old void finalization, rebuild the actual lane target, and its new release/retry regression fails with the original poisoned-lane error. Exact original source is restored in finally; rebuild restores the passing target.

## Ownership and failure boundaries

The host carrier records positive NotStarted evidence from the launch entry or its returned transaction. That evidence is cleared before calling launch_execution; an exception from that call cannot infer no submission from an absent ActiveExecution. Only full finalization success publishes the existing epoch release marker. Carrier destruction alone, a stale/missing marker, partial progress and failed cleanup provide no such permission.

ChipWorker consumes the marker within the exact allocated slot, under its existing finalization reservation. A private finalization helper returns that fact even when it reports the persisted original launch error. The lane uses it to avoid treating an execution result as failed cleanup, preserves the launch message in RunPreparationRefused and retires only this run's borrow. Existing Python immediate/deferred/compatibility consumers already release references on that exception.

Four runtime/architecture C API targets cover returned NotStarted, returned Partial, backend throw without an active pointer and cleanup attachment failure. The last deliberately retains resources through subprocess exit rather than fabricating release evidence. An initial in-process negative test correctly left quarantined allocations and polluted the test fixture's zero-allocation assertion; subprocess disposal corrects the test ownership without changing product cleanup. An attempted throwing override of abandon_prepared_execution was rejected by its noexcept contract and was replaced by the actual attach-failure seam.

A separate existing ownership gap remains: a backend can throw after enqueue but before returning ActiveExecution (for example allocation failure at make_unique<ActiveExecution>). Baseline finalization infers no launch from the absent pointer. This PR never grants its new no-submission proof on that path; internal retention for ambiguous backend exceptions needs its own correction. The CPU model does not establish a silicon failure or any AIV cause.

## Focused validation

- Six native-admission/lane targets pass, including original error preservation, repeated terminal wait, retry and missing-proof poison.
- Four actual A3 public cases (prepare/launch × TMR/HBG, borrowed) pass in task `task_20261017_015702_10647314421`, 16.31 s.
- Eight public cases (prepare/launch × owned/borrowed × TMR/HBG) pass in task `task_20261017_020150_12915932076`, 32.46 s. Focused Python ownership tests: 62 passed.
- A new joined-failure test initially failed: the joined path poisoned the lane before cleanup could supply its positive proof. The final implementation defers that decision until the same proof is available, keeps original launch priority when cleanup also fails, and passes the case where only successor borrow 2 retires while predecessor borrow 1 stays live; an immediate retry can join beside that predecessor.
- All changed-file hooks pass. Full validation and the two wrapper/assertion corrections are recorded below; the final product source is frozen during its task-submit sweep.

Original logs, device logs, temporary mutation scripts and binaries remain local. No capture or general fault recovery is claimed.


## Full validation and raw caller migration

On original product head 15afcaf3, full Linux Python UT passes 3415 / 127 skipped in 141.01 s; full rebuilt C++ UT passes 327/327 targets in 11.70 s. Full a2a3sim passes 61/61 Resource dispatch jobs plus L2 HBG 14 passed / 7 skipped and TMR 31 passed / 1 skipped (exit 0, 177.12 s). Full a5sim passes 57/57 Resource dispatch jobs plus L2 HBG 16 passed and TMR 30 passed (exit 0, 170.20 s).

The first final-board wrapper (task_20261017_020858_1765471965) exits before pytest: task-submit --list is rejected inside task-daemon as a nested task invocation. Queue snapshots are moved outside the daemon; npu-smi snapshots stay inside the held locks. The next wrapper (task_20261017_021043_21775616780) reaches the full C ABI/caller-stream suite: 42 pass, four raw-ABI arms fail at the old all-zero storage assertion after a deliberately refused legacy launch. They are the same assertion under two runtimes and release-fault settings. It is updated to assert the same release-marker family and current descriptor epoch; the next prepare consumes/reuses that unconsumed marker. Ordinary no-launch finalization and failed-release zero assertions remain unchanged.

Revision bc46b441 changes only that Python regression assertion (one file, 3 additions / 2 deletions); production src/ and python/ trees are identical to 15afcaf3. It is rebuilt for the new commit stamp. New exclusive A3 task task_20261017_021653_32225722924 reruns the C ABI/caller-stream suite and CI-shaped scene sweep (excluding sdma and sdma_fault markers) on devices 8–11 with isolated logs and before/after occupancy snapshots. The revised C ABI/caller-stream suite passes all 46 cases / 29 architecture deselections in 131.67 s; the ordinary scene sweep completes successfully (details below). Old full UT/sim numbers remain attributed to 15afcaf3.


## Final local board result and CI snapshot

Task `task_20261017_021653_32225722924` exits 0 on frozen head bc46b441 and releases all four locks (devices 8–11). After the 46-case C ABI suite, the CI-shaped non-SDMA scene command completes in 610.14 s: all 151 Resource dispatch jobs exit successfully, L2 HBG has 43 passed / 1 skipped, L2 TMR has 60 passed, and the remaining parent item passes (204 items are delegated/deselected by the harness). Resource dispatch counts include inherited SceneTestCase placeholders that self-skip when no CASES match; they are not 151 independent test assertions. The custom device-result chain is separately dispatched and passes. No unexpected AIV failure is observed in this run. Before/after NPU occupancy and queue snapshots remain with the isolated local task evidence.

CI query at 2026-10-17 09:29 UTC: no red checks. A5 onboard scenes, A3 hardware UT, both A5 simulator jobs, network/DeepSeek scenes, packaging and profiling smoke are successful; A3 scenes/simulators, host UT and A5 hardware UT are still running. The successful A5 onboard job log was downloaded and checked; that scene result is not evidence that the separately scheduled A5 hardware-UT refusal matrix has finished. PR remains submitted for review, not merged.


## Rebase onto #2613

Current parent is main `2ee57567d01d0d86d5afb605f15e644a06a16200`; current candidate head is `fc24b0d0a9a20262a1223f700151b5e67150b0cd`. The sole conflict comes from #2613 moving the native-admission fixture out of the common test file. Keep its split: launch refusal/partial/throw injection moves to support/native_admission/fixture.h, while all four regression tests remain in common/platform/test_native_admission.cpp and execute under all four runtime/architecture targets. No retired runtime selector macro or duplicate backend assertions are restored. Range-diff changes only this test layout; the PR production patch remains identical. Main contributes its separate three-field HBG graph-descriptor initialization.

All changed-file hooks pass. The editable extension/runtime binaries are rebuilt at the new head. Full rebuilt C++ UT passes 327/327 targets in 88.13 s. Exclusive A3 task `task_20261017_034334_292562022153` on device 8 passes all 8 public refusal/retry arms (TMR/HBG × owned/borrowed × prepare/launch), 33.08 s, exit 0 and lock released; occupancy snapshots and isolated logs are retained. Full Linux Python UT passes 3415 / 127 skipped in 137.05 s. All 23 native-admission test names are retained across the common/HBG files, and byte comparison confirms production/Python/docs/scene-test patches match the prior PR exactly. The original branch is updated with an explicit force-with-lease against bc46b441; CI restarts for the new head. Prior complete scene/simulator sweeps remain attributed to their original heads; they are not claimed rerun by this rebase.
