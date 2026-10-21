> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# PR2596 — published feedback repair and execution audit

## Result

All five published repairs are present and address their reported failure paths. No additional product defect was found in this bounded closure review. Exact-head CI has now completed successfully. **Feedback closure remains outstanding:** all five review threads are still unresolved and contain no repair replies. This report recommends replying and resolving each one; it performs neither action and does not declare the PR Ready.

Reviewed head: `77460ea26466cb1d17a23f8a51758034a95c4a43`. Current base: `17129ba496c6d7d710b0e1320b3d1c25aeb9d1e1`. Local HEAD matches and the product worktree is clean. CI checked out merge `4fd9fd4`, explicitly logged as this head merged into this base. Bound manager gate: `codex-p4-77460-feedback-audit.review.json`, proceed after the 15:05:19Z main17129/all35 gate. Prior full feature and six-file repair reviews were read as context; the five fixes below were independently checked in published source.

Scope: read-only source, feedback APIs, Actions job steps and raw logs. No tests, collection, builds, product edits, commits, pushes, external replies, resolutions or CI writes were performed. External PR2600/2605 remain outside this closure; PR2607 remains closed unmerged.

## Complete feedback inventory

Paginated REST inline comments, review summaries, and issue comments were read. GraphQL thread pagination and every nested comments page report `hasNextPage: false`.

| Surface | Current content | Disposition |
| --- | --- | --- |
| Inline feedback | Five original comments, five unresolved threads, no replies | Reply with verified repair evidence, then resolve each thread below |
| Review summary | Review `5544158522`, COMMENTED, from older head `8f4725ed06c7010a93725737869fcce81cf7fe8d` | Repeats the same five findings; no sixth independent code request |
| Issue conversation | Bot comment `6209954827` | Walkthrough/status, automatic incremental review disabled/skipped, older risk summary repeats those findings; informational, no additional product repair required |

Only the simulator thread is currently non-outdated. The other four being outdated is not evidence of repair; their current source paths were traced. CodeRabbit's SUCCESS status is also not evidence that the unresolved findings were addressed.

## Per-thread verification and proposed replies

All paths/line numbers in this section refer to the published head. Replies below are drafts, not posted statements.

### 1. Exclude simulation before parent sizing

- Comment: [4291806358](https://github.com/hw-native-sys/simpler/pull/2596#discussion_r4291806358).
- Thread: `PRRT_kwDORCBKLM6tGOPE`.
- `python/simpler/worker.py:13084` rejects platforms ending in `sim` inside the `_automatic_admission_requested` property, before `_requested_pipeline_depth_for_init` selects the automatic directory at line 13102. Thus parent sizing and endpoint mode agree; this is not merely a late endpoint opt-out.
- `tests/ut/py/test_worker/test_automatic_admission_scope.py:85` checks both `a2a3sim` and `a5sim`: eligible worker structure, automatic request false, original default sizing retained. Explicit-depth regression is also present. Ubuntu raw log explicitly records both simulator cases and the explicit-depth case PASSED.
- **Disposition: reply and resolve.**

Proposed reply:

> Fixed in `77460ea`. The parent now excludes simulated platforms in `_automatic_admission_requested` (`python/simpler/worker.py:13084`) before choosing its requested pipeline depth. Both `a2a3sim` and `a5sim` retain their existing default sizing; explicit depth remains honored. The parameterized simulator scope regressions and explicit-depth regression passed in exact-head Ubuntu CI.

### 2. Apply a capacity decrease before releasing the FIFO lock

- Comment: [4291806366](https://github.com/hw-native-sys/simpler/pull/2596#discussion_r4291806366).
- Thread: `PRRT_kwDORCBKLM6tGOPL`.
- `src/common/hierarchical/orchestrator.cpp:280`: retirement holds `runs_mu_` while removing the FIFO entry, releasing its lease, folding endpoint capacity and applying the new limit at lines 304–305. Lease refresh occurs at line 306 and wakeup only after unlocking at line 310. A waiter cannot acquire using the pre-decrease limit between retirement and publication of the smaller limit. `apply_admissible_depth_locked` at line 175 changes admission limits without invalidating existing leases.
- `tests/ut/cpp/common/hierarchical/test_early_launch_admission.cpp:280`, `AutomaticStartupWaitsForProofThenEnqueuesACompatibleSuccessor`, exercises cold capacity 1, growth after completed proof, decrease back to 1 while a waiter exists, continued blocking of the fourth run, and preservation of the third run's slot/generation. It then drains the third and admits the fourth.
- Exact-head Ubuntu CTest target `test_early_launch_admission` passed (0.06 s). This is a production-orchestrator CPU regression with bounded waits, not a claim of exhaustive race exploration.
- **Disposition: reply and resolve.**

Proposed reply:

> Fixed in `77460ea`. Retirement now folds and applies endpoint capacity under `runs_mu_`, before refreshing leases or notifying admission waiters (`orchestrator.cpp:304–310`). A decrease constrains new admission while keeping an already-held lease valid. `AutomaticStartupWaitsForProofThenEnqueuesACompatibleSuccessor` covers the decrease with a blocked waiter and verifies the existing lease identity survives; its CTest target passed in exact-head Ubuntu CI.

### 3. Retry/wait only for genuine capacity refusal

- Comment: [4291806376](https://github.com/hw-native-sys/simpler/pull/2596#discussion_r4291806376).
- Thread: `PRRT_kwDORCBKLM6tGOPT`.
- `src/common/platform/include/host/run_resource_growth.h:95`: only `OverQuota` and `BackendFailed` classify as `Refused`; `Degraded` is separate, while `None`, `NotServing` and `Overflow` become `Failed`.
- Both production adapters use this classifier: staging at `src/common/platform/onboard/host/device_runner_base.cpp:1058`; bank transaction at line 1496. The bank adapter resets the recorded refusal before each transaction at line 1490, preventing an earlier allocation refusal from disguising a later semantic failure. `RunResourceGrowth` retries only `Refused`, and captures a wait only for that outcome. The adapter returns the original bank transaction error at line 1527 when no resource refusal result applies.
- `tests/ut/cpp/common/platform/test_run_resource_trim.cpp:955`, `OnlyCapacityRefusalsMayRetryOrWaitForAConsumer`, tests classification and one-attempt/no-wait behavior for all four noncapacity reasons with a live consumer present. This invokes the real growth coordinator with controlled outcomes; it is not a device OOM test. Exact-head `test_run_resource_trim` passed. Native admission regressions additionally cover noncapacity failure not parking.
- **Disposition: reply and resolve.**

Proposed reply:

> Fixed in `77460ea`. Both staging and bank growth use `classify_growth_refusal` (`run_resource_growth.h:95`): only quota/backend allocation refusal may retry or wait. The bank adapter clears its refusal reason per attempt and preserves the original transaction error for noncapacity failures (`device_runner_base.cpp:1490–1527`). The regression covers `None`, `NotServing`, `Overflow` and `Degraded` with an eligible live consumer and verifies no retry or wait. `test_run_resource_trim` and the native admission targets passed in exact-head CI.

### 4. Published arena ownership survives a later reference failure

- Comment: [4291806382](https://github.com/hw-native-sys/simpler/pull/2596#discussion_r4291806382).
- Thread: `PRRT_kwDORCBKLM6tGOPX`.
- `src/common/platform/onboard/host/device_runner_base.cpp:1504` commits workspace claims immediately after successful arena publication and before `reference_bank_arenas` at line 1509. A later reference error still returns, but failure cleanup can no longer roll those published claims back into obsolete storage.
- `tests/ut/cpp/common/platform/test_run_resource_trim.cpp:985`, `AReferenceFailureCannotRollBackAlreadyPublishedBacking`, injects reference failure after publication, verifies the claim list is empty, runs rollback and retirement/reclaim, then checks the published allocation remains owned and reused rather than freed.
- Coverage distinction: the regression's Endpoint fixture uses the real growth/ledger/transaction path and mirrors the production adapter ordering. It does not inject this failure through a live `DeviceRunnerBase` on hardware. Production ordering was independently inspected; exact-head `test_run_resource_trim` passed. This limitation is not an outstanding defect in the concrete repair.
- **Disposition: reply and resolve.**

Proposed reply:

> Fixed in `77460ea`. Successful bank publication now commits its workspace claims before attaching consumer references (`device_runner_base.cpp:1504–1510`). If reference attachment fails, the error propagates without making already-published backing obsolete. The CPU ledger/transaction regression `AReferenceFailureCannotRollBackAlreadyPublishedBacking` verifies that subsequent rollback and reclamation retain and reuse that backing; its CTest target passed. The production adapter ordering was also checked directly.

### 5. Failed plan abort cannot discharge device borrows as proven complete

- Comment: [4291806388](https://github.com/hw-native-sys/simpler/pull/2596#discussion_r4291806388).
- Thread: `PRRT_kwDORCBKLM6tGOPc`.
- `src/common/worker/chip_run_lane.cpp:251`: `release_run_plan` returns success/failure, catches abort failure, records an error and poisons the lane on refusal. Waiting-plan cleanup callers propagate that result into `release_device_spans`; launch-sensitive failure also requires that no launch fence was crossed (line 873).
- `release_device_spans` at line 290 passes `!proven_done` as the keep flag. `ChipWorker::release_caller_device_borrow` forwards this unchanged (`chip_worker.cpp:2104`). `CallerDeviceBuffers::release` at `src/common/platform/include/host/caller_device_buffers.h:179` marks retained allocations release-refused before removing the run's borrow-table entry. Clearing the lane's local flags therefore does not free/quarantine-discharge the allocation.
- `tests/ut/cpp/common/hierarchical/test_chip_run_lane_joined_launch.cpp:1057`, `ARefusedPlanAbortKeepsTheWaitingRunsBorrow`, injects abort refusal for a waiting successor, verifies `{identity 2, keep 1}`, verifies predecessor release remains `{identity 1, keep 0}`, and checks poisoned close reports failure. Exact-head `test_chip_run_lane_joined_launch` passed. This uses a real lane with callback fakes; the nearby mixed-buffer regression additionally uses the real borrow table and guarded free. Hardware outstanding-close tests exercise successful draining, not injected abort refusal.
- **Disposition: reply and resolve.**

Proposed reply:

> Fixed in `77460ea`. `release_run_plan` now reports abort success and cleanup uses that result when releasing caller borrows (`chip_run_lane.cpp:251,290`). A refused abort poisons the lane and retains/quarantines the affected allocations instead of claiming completion. `ARefusedPlanAbortKeepsTheWaitingRunsBorrow` verifies the failed successor keeps its borrow while its completed predecessor releases normally, and that close reports the failure. Its CTest target passed in exact-head CI; successful outstanding-close scenes also passed on A3/A5 for both runtimes.

## Exact-head execution evidence

The [CI run 38917751948](https://github.com/hw-native-sys/simpler/actions/runs/38917751948) is now `completed / success`, attempt 1, head `77460ea…`, updated `2026-10-16T15:05:45Z`. Refreshed PR rollup: **18 Actions successes, one skipped docs deploy, zero running/failing Actions jobs**. CodeRabbit is a separate successful status context. The PR API still reports `mergeStateStatus: BLOCKED`; this audit does not infer which protection causes that value.

### CPU and native linkage

- [Ubuntu UT job](https://github.com/hw-native-sys/simpler/actions/runs/38917751948/job/117031388163): raw log records **3390 Python passed, 12 skipped, 108 deselected** and **100% CTest passed out of 327**. The three specific feedback targets above were both built/linked and executed successfully.
- [macOS UT job](https://github.com/hw-native-sys/simpler/actions/runs/38917751948/job/117031388240): raw log records **3381 Python passed, 21 skipped, 108 deselected** and **100% CTest passed out of 326**. macOS count is 326, not 327.
- Both logs explicitly show the previously problematic eight native targets passing: A2A3/A5 HBG bind-ledger, A2A3/A5 TMR runtime-temp-buffer, and native-admission targets for all four architecture/runtime combinations. This is executed evidence for the six-file fixture/linkage repairs, beyond static inspection.
- New automatic scene helper Python regressions passed. Simulator scope cases were individually observed PASSED. All four simulator jobs completed successfully; these preserve explicit/simulation behavior and are not evidence that automatic mode runs in simulation.

### Actual automatic hardware scenes

Raw onboard logs explicitly identify each node and its PASS result, not merely collection or successful job status:

| Scene | A3 HBG | A3 TMR | A5 HBG | A5 TMR |
| --- | --- | --- | --- | --- |
| `TestAutomaticAdmission` | PASS 15.9 s | PASS 19.5 s | PASS 17.6 s | PASS 37.6 s |
| `TestAutomaticDeviceDemand` | PASS 26.3 s | PASS 26.2 s | PASS 28.8 s | PASS 33.5 s |
| `TestAutomaticOutstandingClose` | PASS 8.1 s | PASS 8.2 s | PASS 8.6 s | PASS 13.3 s |
| `TestAutomaticTwoEndpointFold` | PASS 5.2 s | PASS 5.2 s | PASS 8.7 s | PASS 9.4 s |

Sources: [A3 onboard job 117031387434](https://github.com/hw-native-sys/simpler/actions/runs/38917751948/job/117031387434), [A5 onboard job 117031388013](https://github.com/hw-native-sys/simpler/actions/runs/38917751948/job/117031388013). A5 completed at 15:05:44Z after scene execution, DFX smoke, and cleanup; its earlier in-progress status is superseded.

The shared scene support asserts actual automatic request and omitted explicit admission knobs. These scenes cover cold bounded startup, sustained/refilled real `Worker.submit`, native epoch/lease correlation, device ownership/demand, golden outputs, outstanding close with independent output mappings, and two-endpoint capacity folding. HBG fatal injection is intentionally a host prepare failure; TMR exercises a joined failing successor. They do not claim hardware allocator exhaustion or an on-device HBG fatal successor. Controlled allocator-pressure and abort-refusal paths are CPU regressions, as stated above.

## Closure handoff

1. Publish each of the five replies above on its corresponding thread, then resolve that thread. No further product repair is requested by this audit.
2. The old review summary is satisfied by those five dispositions; a concise completion summary may link the exact-head CI run without treating the bot walkthrough as an additional reviewer demand.
3. Refresh exact head, feedback surfaces and required-check state after external closure. Preserve the distinction between this positive source/execution audit and the repository's final approval/merge policy. At this audit snapshot, all five feedback threads remain unresolved and GitHub reports BLOCKED, so **Ready is not asserted**.
