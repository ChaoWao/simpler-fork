> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# #2531 ca08f636900cfd5a8440ed60d838ca6d3141f343 — STATIC PASS; CI blocked

## Reviewed delivery

Approved local L3, single-endpoint A5 HBG program three-resource contract; default two and kernel capability unchanged. Merge base e6fd8c608a7271b76d06a4a598fab6fcf2ab9d2c; current main b58622478764433408ac3d06deeb401614df11bb adds documentation only. Prior full-PR reviews remain applicable. This revision changes only the A5 early-enqueue scene (+108/-37); production src/python unchanged from dc84091a3. Full PR six files +1262/-18, mostly acceptance tests. No new header-reference or architecture scope.

The original author committed, pushed and updated the PR before its final model request failed with HTTP503. Local clean HEAD and remote PR HEAD both match ca08f6369. The result error is a missing completion narrative, not evidence that publication failed. The original process exited; no duplicate writer is needed.

## F8 closed: all submitted runs share guaranteed cleanup

The complete case now has one try/finally, including producer, rejected preparation, explicit HOST copy, positive submission, waits and output checks. Every returned handle is registered immediately. The positive run cannot escape the drain when wait/copy/assert raises. The rejected handle is tolerated only after both terminal preparation error and specific child diagnostic are verified; otherwise it is treated as an ordinary failed handle and cleanup reports/retains resources. Free occurs only after successful settlement. An assertion before explicit frees conservatively retains allocations for fixture cleanup.

RunHandle caches terminal failure; waiting on the specifically verified refusal in cleanup does not re-execute the run. The early prepare-error path releases the unlaunched run's borrow spans and removes it from the FIFO; unrelated failed preparation is not inferred to prove device completion.

## F9 closed: rejection cause verified from this case's child log

_RunTrace is created before any submission and records each child PID's log byte origin. The bounded lookup requires get_tensor_data, FATAL(code=5), and the exact explicit HOST/NONE argument message on the same line. In this single-endpoint case there is one invalid submission before the lookup; the positive copy run is submitted afterwards. Thus a generic prepare_native_run failure alone no longer passes the oracle.

Source checked: runtime_status.h defines invalid-args=5; require_host_tensor reports that exact message; orchestrator's first-fault log emits FATAL(code=5) with the entry name. ChipWorker separates incompatible fallback from terminal prepare error. ChipRunLane releases only the unlaunched run on this error; the child loop stops the runner only for a poisoned lane. No production logging or implicit HOST copy was added.

## CI and validation limits

No manager tests, build, install, benchmark, CI POST or merge. Author transcript shows only static ruff/py_compile checks, then commit/push/PR-body update. Prior 9ed CI executed native host regressions (Ubuntu 313/313, macOS job successful) and actual A5 three-run enqueue, DEVICE chain and refill; that evidence does not execute this rewritten case. Current-head CI must validate it before Ready.

Current A3 UT job114140756920, workflow38027072918, failed before workload in BootstrapDispatcher on device8 with aclrtSynchronizeStream=507018, on infra-gpu-npu-021 / infra021-npu-4. This is the fourth observed same bootstrap failure across heads, not four manager retries (POST count remains zero). The TMR peer on device9 passed. Earlier successful scene/network jobs did not establish device8 recovery. Root cause remains unproven; no claim of flakiness or definite bad hardware. No blind retry under the repeated-error policy.

Evidence: ../claude-managed/pr2531-114140756920.log; ../claude-managed/ci-failure-2531-a3-bootstrap-device8.json. Earlier finite diagnostic audit: pr2531-9ed-ci-bootstrap-review.md. Other current jobs remain in flight. F1–F9 source findings are addressed; current-head CI failure and incomplete validation keep Ready false.

## New scene evidence — 2026-10-10T05:38:54.347027+00:00

Downloaded complete A3 onboard job114140757048 log (1,067,293 characters;11,379 lines), workflow38027072918, same ca08 head. Runner infra021-npu-1 on the same machine. Parsed 100 executor outcome groups: all35 groups whose allocated device set contains8 failed; all65 excluding8 passed. Includes both HBG and TMR and single-/multi-device configurations. All34 device-fault callback lines name device8;34 BootstrapDispatcher stream-sync507018 lines. First reported failure is HBG DepGen init before test body. This is a fifth failed job with the same bootstrap signature across heads, not35 new manager retries. DFX smokes after the failed scene step did not run.

This within-job contrast substantially narrows the investigation to device8 and its bootstrap environment/state. It is not proof of hardware damage or a specific driver/filesystem/runtime cause; tasks allocated to different devices are not identical controlled repetitions. No speculative product edit or retry. Artifact API currently returns zero artifacts, so no device-side failure cause is available there. User was asked for read-only slog/AICPU evidence covering05:27–05:35UTC; no test execution requested. Raw log and per-group parse: ../claude-managed/pr2531-114140757048.log and ../claude-managed/pr2531-ca08-device8-scene-evidence.json.

All three online feedback surfaces checked for2531/2534: no inline/review-summary findings; only existing bot summary/rate-limit comments. Current counts2531:13success1skip2running2failure2queued;2534:17success1skip2queued. Static source conclusions unchanged; neither Ready.

## Current-head host execution and queue check — 2026-10-10T06:03:53.128469+00:00

Ubuntu job114140757143 on ca08 completed successfully: Python3002passed/12skipped/64deselected; new test_three_run_capacity compiled, linked and executed successfully; all313CTest targets passed (log3604,5349–5365,5941–5942,6061). macOS job114140757122 also completed success. This confirms the host acceptance on the current head; the rewritten A5 scene is still queued, not executed. No manager test execution.

Current2531 counts15success1skip2failure2queued;2534 unchanged17success1skip2queued. No new failed steps. Both required self-hosted/a5 jobs on each PR have no assigned runner/steps. Repository runner API shows two matching runners: one online/busy, one offline. The online runner is executing external topology workflow38018253321/job114113778879, scene step started05:51:11UTC. No idle matching runner at this observation; do not call this a product test hang or cancel external work. Raw runner/active-job responses retained in ../claude-managed/ci-runner-status-current.json and ci-run-38018253321-jobs-current.json. Device8 artifact list still empty; existing log-access request remains pending, not re-asked.

## A5 acceptance completed — 2026-10-10T07:07:25.397995+00:00

Current-head job114140757106 passed scene and DFXsmokes. The log first shows the runner cleaning an older worktree; final checkout atline136 is c1ff32c, merge of ca08f6369 into b58622478. The final checkout, not the earlier cleaning HEAD, establishes tested code. Four actual pytest passes:

- line628: DEVICE control refusal plus explicit HOST copy positive, device6.
- line641: DEVICE result feeds the next two runs, device5.
- line667: repeated third-resource refill, device4.
- line706: three accepted/launched runs at once, device7.

Each block includes `1 passed`; the inherited test_run placeholder at602 is pytest-skipped despite the executor PASS label and is not counted. This closes the corrected HOST refusal and cleanup case's normal-path execution gap; it does not claim every injected failure path was exercised. Raw log ../claude-managed/pr2531-114140757106.log; structured evidence pr2531-ca08-a5-execution-evidence.json. No manager rerun.

Current16success1skip2existingA3failures1A5UTqueued. A3device8 remains unresolved; A5 success does not diagnose A3 bootstrap. Ready false. Mainline snapshot refreshed after a transient GET failure; later runner-status and2531 repeated job-read failedTLS, previous successful read times retained rather than fabricated freshness.

## Final A5 UT execution — 2026-10-10T09:03:05.316998+00:00

Job114140757063 completed success. Final checkout log131 is c1ff32c (ca08+b586). Four actual hardware pytest cases each pass: default-managed HBG andTMR contexts, prewarmed child close and provider-region two lifecycles, logs331/343/355/367. cann-examples step reports success; no claim of a smoke where none is provided. Full log ../claude-managed/pr2531-114140757063.log. AllCIterminal17success1skip2A3bootstrapfailures; NOTReady. No manager test/rerun. No device8artifact available; existing log-access question remains.

## External retry observed — 2026-10-13T02:03Z

Exact ca08 unchanged. Workflow38027072918 attempt2 was triggered by ChaoWao at01:54:52Z. A3UT115091097735 on infra021-npu-2 and A3onboard115091097763 on DEVICE_HOST are running hardware tests/scene tests respectively, no failed steps at read. Current17success1skip2running supersedes the current-failure count, but does not establish prior device8 root cause or recovery. Manager issued zero retry POSTs; do not duplicate this external attempt.
