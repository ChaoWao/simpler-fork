# Async Runtime descriptor publication on the common eager path

## Source and scope

Baseline main `6348a0d1fdacd58a4f5df85ebfbfaab47e2ccdd4`. Frozen implementation head `9b1bfaf4475595dc7d734ebc39a9999ccb08dd4a`, branch `feat/async-runtime-descriptor-publication`. Both runtime variants and both onboard architectures use the same source retention and enqueue path. No public API, ABI version/size, capability query, device layout change, runtime flag or second executor is introduced.

This delivers asynchronous **Runtime descriptor** publication during eager launch. HBG's bulk host-built arena upload and DFX publication stay on their existing paths. It does not qualify capture/replay or replace the framework/DFX work in #2605/#2588. No throughput improvement is inferred from changing an RTS primitive.

## Correction to the historical layout argument

Current TMR `DeviceRuntimeLaunchDesc::workers` is in the tail. `runtime_device_copy_size` excludes it, first-use `runtime_device_initialized_prefix_size` includes initialization, and `runtime_device_extent_size` additionally covers device-initialized gates. The common HBG layout has the same publication/initialization distinction. Existing cache-line and copy-range assertions are real guards. The September argument “workers first, overwrite handshake on every publication” is historical, so repeating a physical ABI split would duplicate delivered work. This revision preserves those lengths and uses a stream dependency to protect cold initialization.

## Execution and ownership

1. Prepare reserves the device destination plus a persistent pinned host source on the existing slot and captures the descriptor prefix by value. HBG bulk preparation remains unchanged.
2. Inside the exact launch transaction, optional caller Start is recorded and the AICPU stream waits on it.
3. The descriptor snapshot is copied into the slot's pinned source; `rtMemcpyAsync` queues H2D on the actual AICPU stream. Any copy attempt marks the transaction sink before entering RTS. A warm TMR prefix cache hit queues no copy.
4. The entry owner records Fork after publication and queues the AICore wait before its kernel. With no caller and no copy, no fork is queued. With a caller, its existing entry/exit chain stays intact.
5. The two completed kernel boundaries prove that an ordinary publication fork was consumed. A partial run without this proof uses bounded private-stream discharge; failure retains/quarantines the resources. Slot reservation prevents source reuse while its run is live. Normal teardown retries failed host frees; fatal abandonment makes no source-free call on an unproven reader.

Success means enqueued in stream order, not DMA-complete on the host. Both kernels must enter behind the copy, including on a cold allocation whose workers need first initialization. Metadata is inside the existing active owner and launch progress contract, so a failed enqueue cannot acquire the clean no-submission receipt.

## Tests and negative control

Before implementation, the new asynchronous-source test fails both real helper targets (TMR/HBG): async copy count is 0, expected 1. The candidate uses a deferred RTS-copy model: the device still has poison after enqueue; caller Runtime mutation and helper-view release do not change the pinned source; the delayed copy then publishes the original value. Pointer-identity and no-free assertions run before any delayed source dereference.

Additional tests cover source reuse, host-allocation failure/retry, host-free failure/retry, failed async enqueue progress, source abandonment, metadata ordering between caller entry and AICore fork, no-caller ordering, zero-copy cache hits, failed event record, throwing publication and bounded discharge. The other TMR architecture now builds the same helper tests.

A real C ABI test interposes only the watched pinned-source copy and returns an error without enqueueing DMA. It asserts the original error, no kernel launch, absence of a clean release receipt, refusal of a successor and source retention across repeated failed close. A kernel-launch guard prevents a faulty implementation that skips/ignores publication from executing an uninitialized descriptor; it fails the test rather than causing AIV. Both runtimes and SDK task queue settings are exercised in separate processes.

## Validation on 9b1bfaf4

[PR #2620](https://github.com/hw-native-sys/simpler/pull/2620), head `9b1bfaf4`, baseline `6348a0d1`. Changed-file hooks and complete install/rebuild pass. All results below use this frozen head; the source tree stayed clean throughout the hardware task.

| Validation | Result |
| --- | --- |
| Full non-hardware C++ UT | 328/328 targets; 89.23 s |
| Full Linux Python UT | 3415 passed / 135 skipped; 156.04 s; eight new arch-marked hardware cases skip here |
| A2/A3 simulator | 61/61 Resource jobs; L2 HBG 14 passed / 7 skipped, TMR 31 passed / 1 skipped; exit 0 |
| A5 simulator | 57/57 Resource jobs; L2 HBG 16 passed, TMR 30 passed; exit 0 |
| New real C ABI copy-rejection cases | 4 passed; 39.16 s; A3 HBG/TMR × TASK_QUEUE_ENABLE=0/1 |
| Existing real C ABI/public Worker suites | 54 passed; 246.59 s |
| Full ordinary A3 scene sweep | 151/151 Resource jobs; L2 HBG 43 passed / 1 skipped, TMR 60 passed; exit 0; 629.40 s |

The A3 cases and full sweep ran in one exclusive task, `task_20261018_191742_37506192512`, on devices 8–11. The daemon reports completed/exit 0 and releases all four devices. The default 600 s client wait expired once; reconnecting retrieved the final output without restarting any test. That client wait is not an executor timeout. The sweep excludes SDMA and level-4 tests; no local A5 hardware result is claimed. GitHub CI remains in progress as of 2026-10-18 19:34 UTC; changed-file CI hooks and docs build pass.

An additional cleanup audit checked the no-kernel partial case: `RunCompletionFence::fully_fenced()` requires both kernels and both records, so a metadata-only run reports Unfenced and `wait_run_fence` falls back to bounded pair synchronization. It cannot use the ordinary completed-boundaries shortcut until the queued copy and fork are covered. Existing unit tests cover the bounded-discharge failure/retry ladder.

Intentional fatal/watchdog cases emit RTS exceptions; raw error-code counts are not a failure count. The sampled core exception PIDs 188984, 222942 and 234721 map to the Resource scheduler's op-timeout, aicore_hang and tensor_wait_timeout negative cases. Resource job verdicts are checked individually, not inferred from the single aggregate dispatcher pytest result. These observations do not diagnose the historical intermittent HBG AIV issue.

An initial task used an unsupported hyphenated card-list argument and was rejected before the script or any NPU work started. It is not counted as a runtime/test failure or hardware observation. The successful task uses an explicit comma-separated list.

Raw logs and scripts stay under `/tmp/async-descriptor-*`; isolated ASCEND logs, timed occupancy snapshots and queue records stay in the board directory. The source/head remains frozen during all runtime testing. Raw material stays out of the product PR and the published text-only design snapshot.
