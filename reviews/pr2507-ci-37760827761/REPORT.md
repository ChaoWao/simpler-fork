# PR #2507 CI failure: confirmed facts and evidence limits

CI run: https://github.com/hw-native-sys/simpler/actions/runs/37760827761

## HBG sustained-refill failure

Job `113258241936`, runner `mutil-A3-125-1`, machine `liteserver-hps-5f81-00013`.
Task `task_20261008_181443_13886846118` acquired devices 8/9/10/11. The failing pytest process was 1391978; its device-10 child was 1392567.
At 10:15:13 UTC the case reports failure at `handle.wait()`: `finalize_native_run failed with code -100 (run_id=1 slot=0 generation=1 dispatch_id=1 run_epoch=1)`. The teardown repeats the same poisoned-lane error; it is not a second independent defect. No overlap-count assertion failed.

HBG's `read_runtime_status` returns the negation of `SharedMemoryHeader.sched_error_code`; code 100 is `SIMPLER_ERROR_SCHEDULER_TIMEOUT`. This establishes scheduler timeout, **not** an AIV UB fault. No plog, device log, scheduler subclass or fault PC for this process was uploaded. The test routes child host logs into process-tree files rather than captured stderr; the onboard workflow has no upload-artifact step for those files or device logs. The job has only `Captured stderr setup` (ready PID), no execution diagnostics.

## Occupancy comparison

- Main onboard task records its device locks, not a snapshot of actual NPU PIDs.
- Network1 uses local devices 12/13 and finishes its job at 10:11:21 UTC, before the HBG failure at 10:15:13. It is not evidence of concurrent use of device 10.
- DeepSeek runs on `infra-gpu-npu-021`, a different machine.
- Profiling flags smoke runs on a GitHub-hosted runner, not this NPU host.
- Unregistered work outside these CI jobs remains unobservable: no fault-time actual-PID snapshot exists. Holding the queue lock does not exclude it.

## Separate network1 red job

Job `113258241719`: `test_compute_then_tload_mixed_l3`, TMR, device 12, child PID 387084. Host logs show 507015/507018 and scheduler subclass `S1:running-stalled`, completed=0/1, running=1, task=0, core=26. The uploaded artifact lacks this failing process's plog/device log and contains later successful cases instead. Therefore its precise instruction fault and relation to HBG are also unestablished.

## Local reproduction under the CI timeout values

Head `2ac0d906`; task `task_20261008_034642_367058829074`, device 0. Three independent executions of the exact sustained-refill case passed with scheduler=2000 ms, op=3000000 us, stream-sync=4000 ms. Target VEC UB signature count: zero.
Timestamped task-submit, npu-smi and process snapshots are retained. The only process sampled on device 0 was 3678804, which matches round 1's own plog; process ancestry had already disappeared at the subsequent ps snapshot. Sparse sampling cannot rule out short-lived interference between samples and says nothing about historical CI occupancy.

No production changes or timeout increase were made. A local pass does not resolve either CI failure. Before a useful next CI failure, preserve child host logs, redirect/persist ASCEND_PROCESS_LOG_PATH, capture device/task occupancy, and upload these on failure. Re-running the unchanged workflow can measure recurrence but still loses the evidence required to attribute it.

## Instrumented follow-up (603be21c)

Temporary pytest evidence capture now preserves child host-log tails, own-PID/time-window CANN errors, and timestamped task-submit/NPU/process ancestry samples. CANN can initialize during imports before the fixture redirects logs, so the collector also reads default locations for this test's PIDs. No timeout, input or execution-order change was made. Evidence capture itself can perturb host timing, so passing instrumented runs are not a proof of absence.

- Fixed-head local full sweep under CI timeouts passed: task `task_20261008_041539_34190528263`, devices 0/1/2/3, 463.02 s. Includes resource cases and both L2 runtime batches.
- Three cold/warm cache cycles passed: task `task_20261008_042224_6339753016`, devices 0/1/2/3, four original cases per phase, 24 case executions total. Each cold phase used a fresh kernel cache; warm reused it. Original cache restored afterwards. No reproduction, hence no evidence attributing the fault to first compilation/loading.
- CI run `37768447086` on runner `infra021-npu-1`: ordinary A2/A3 scene-test step succeeded, as did SDMA and SDMA fault recovery; DFX smokes were still running at this observation. Network1 also succeeded. This runner differs from the original failed HBG runner, so it is not an exact-machine replay.
- An earlier local diagnostic sweep was invalidated by a HEAD change during execution (editable-build commit mismatch). It is excluded from product failure/pass counts; the fixed-head sweep above replaced it.

## Original local fault narrowed to the scalar-add stage

This observation concerns the original four local AIV exceptions, not the unclassified CI timeout. The scheduler snapshots and callable registrations agree:

| Original child PID | Completed tasks | Still running | Registered function |
| --- | --- | --- | --- |
| 1366490 (depth-one control) | 2/513 | task 0x2, kernel 1 | worker_async_fifo/aiv/kernel_add_scalar.cpp |
| 1366562 (device-result chain) | 1/513 | task 0x1, kernel 1 | worker_async_fifo/aiv/kernel_add_scalar.cpp |
| 1386417 (sustained refill) | 1/513 | task 0x1, kernel 1 | worker_async_fifo/aiv/kernel_add_scalar.cpp |
| 1373478 (dep-gen vector graph) | 1/5 | tasks 0x1 and 0x2, kernel 1 | dfx/dep_gen/kernels/aiv/kernel_add_scalar.cpp |

In all four the preceding addition task completed; failure was inside the first run, not necessarily its first internal task. The scalar-add sources use a 128x128 float tile with local addresses 0 and 0x10000. The three early-enqueue plogs share fault PC `0x12c0c00ee29c`; the dep-gen plog reports `0x12c0c0101f5c`. This narrows the execution stage but does not establish a source instruction or cause: no verified original loaded-binary address map is available. Runtime logical core IDs must not be equated with physical error-report core IDs without the mapping. Do not infer a TADDS implementation bug from the kernel name alone.

Stable reproduction and root cause remain unresolved. No runtime fix has been made. Retain the draft and the next-failure evidence capture rather than label successful retries as a fix.
