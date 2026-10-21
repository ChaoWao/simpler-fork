> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# #2560 current-head A5 UT timeout

Head `192929bc5`, test merge `721bf64` onto `55eb52246`; job `115164775411`, workflow `38357440538`, runner `DEVICE_HOST`. Complete log verified with HTTP206 bytes 0–36020/36021.

GitHub check annotation explicitly says the job exceeded its 30-minute execution limit. The matching `_ut-npu-a5.yml` has `timeout-minutes: 30`. Hardware pytest was submitted at06:44:17UTC, but device locks4–7 were obtained at07:09:46. Four substantive cases passed and the task exited0 at07:10:04. The approximately25m29s before lock acquisition is queue/wait time, not evidence of slow test execution.

The following CANN example task waited from07:10:05 to07:12:22 for device7. Several examples compiled and ran, with expected platform-specific skips. At07:13:23 the job canceled while beginning `query`; no assertion or compiler failure is shown. Cleanup completed and orphan task-submit/tail were terminated. This does not prove any unshown device-side cleanup outcome.

Disposition: time-limit cancellation with device queue pressure, not an A5 Python UT failure and not complete CANN validation. No product-code fix inferred. Record one candidate precise-job retry under the user policy; first confirm cross-head issue counts and workflow completion, then submit at most once and consume immediately on accepted/uncertain response. Current workflow stillqueued because A5 onboard has not started: no POST now. Repeated exhaustion requires workflow/queue diagnosis, not blind retries or weakened tests.

New main `dc73516d2` (#2541) changes package revision stamping and DeepSeek timeout30→40; it does not change the A5 UT30-minute limit or fix device9 bootstrap failures. No correctness-driven rebase identified from this delta.

Evidence: [complete log](../ARCHIVE-INDEX.md#ref-65e368187f746abf), [annotations](../ARCHIVE-INDEX.md#ref-50a8168e33e440f8).


## Retry completion 2026-10-13T10:37:43.930864+00:00

A5 UT retry115217384657 completed success; full56738B HTTP200 log finalcheckout721bf64=192929+55eb, hardware4PASS and CANN7built/4ran/3archskip/0failure. All checks terminal17success1skip2A3failure. Bootstrap device9 root remains unknown; no further POST.
