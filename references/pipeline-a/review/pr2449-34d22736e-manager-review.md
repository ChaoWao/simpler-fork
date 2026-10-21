> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# PR #2449 review — 34d22736e

## 0. Basic information

- PR: https://github.com/hw-native-sys/simpler/pull/2449
- Head: `34d22736e4640b4ead0355294fb18850aa12d8ac`.
- Fresh-main merge-base: `a9e6a42b64933f2a555d0d13432826e3d5b83359`; reviewed the merge-base-to-head diff and surrounding scheduler, worker and orchestrator code.
- Current GitHub state: OPEN, MERGEABLE; 19 successful checks/statuses, one skipped. No review threads or substantive review/conversation findings in the retrieved feedback.
- Method: static review plus existing CI and author evidence. No tests, benchmarks, CI retries or remote review submissions were performed.

## 1. Background and problem

A successor run can contain independent tasks targeting several NEXT_LEVEL workers. Each worker has its own staging lane and endpoint capacity. Preparing these tasks need not activate the successor before its FIFO turn.

Previously the scheduler stopped as soon as any worker had staged this run, and returned after its first successful dispatch. For example, when successor B targets workers 0 and 1, worker 0 could prepare while worker 1 unnecessarily waited for predecessor A to finish. This PR fixes that bottleneck under issue #2415. It does not add cross-run token dependency tracking or permit concurrent operators on one device.

## 2. Solution and rationale

`dispatch_preparable_next_level_singles` removes both run-wide early exits and visits every eligible worker. It still takes only an exact-target NEXT_LEVEL single task from the preparable FIFO successor; the existing group exclusion and task-state claim remain. A worker with a full staging lane, no staging support, or exhausted inflight capacity is skipped before taking its task from the queue.

Thus B can stage on workers 0 and 1, while a capacity-limited worker 2 remains queued. When FIFO promotion occurs, worker 2 can use ordinary dispatch. Staging ownership remains per endpoint; it does not activate the parent run, consume the preceding run's result, or bypass its lifetime fence.

The new scan gate uses wake-generation changes and drained completion notifications. I traced submission-close notification, FIFO/lease refresh, task completion and worker progress. Worker endpoint progress, lane release and inflight decrement occur on the scheduler thread before completion draining and the next dispatch scan; the newly freed capacity is therefore visible when that completion triggers a scan. Ordinary dispatch and staged-launch authorization still run each round. I found no missing capacity-release wake introduced by the gate.

The changed path retains the existing atomic claim/cancellation boundary and rejected-publication completion path. No new reverse acquisition against admission/lane locking was introduced.

## 3. Implementation–claim consistency

| Goal | Mechanism | Evidence | Assessment |
| --- | --- | --- | --- |
| Prepare all eligible successor workers | Remove both first-worker exits | Two-worker scheduler regression | Consistent |
| Respect limited endpoint capacity | Check inflight before popping a task | Three-worker partial-staging regression | Consistent |
| Preserve FIFO and ordinary fallback | Existing activation and dispatch paths | Capacity-one and mixed-worker cases | Consistent |
| Avoid scans on each busy spin | Wake/completion gate | Scan-count assertions | Consistent |
| Demonstrate real multi-rank staging | Mailbox states and output checks | Author reports two-rank A3 test | Bounded evidence [1] |

[1] The PR reports 247/247 C++ cases and a two-rank board case passing. This is useful correctness evidence, not a manager reproduction, a 16-rank performance measurement, or proof of the entire parent issue's end-to-end target. Generic CI success does not expand that evidence.

## 4. Residual issues and risks

### Must fix

None found at this head.

### Follow-up

- Keep 16-rank/performance acceptance separate from this bounded scheduling fix. Do not claim throughput or latency improvement solely from the two-rank staging observation.
- The implementation remains intentionally limited to exact-target singles; group/SUB scheduling has not acquired equivalent early preparation through this PR.
- The scan is O(number of workers) per relevant state change. Its frequency is bounded by notifications, not a measured speedup.

## 5. Merge recommendation

**Approve the bounded fix.** The first-worker bottleneck is real, the change removes it without relaxing FIFO/capacity ownership, and the regressions exercise full and partial preparation. No blocking defect was found; current CI is complete. Parent-issue scale/performance acceptance remains separate. This is a local manager recommendation, not a GitHub approval or merge.

## Appendix A. Change breakdown

| Bucket | Files | Added | Deleted | Churn |
| --- | ---: | ---: | ---: | ---: |
| Core | 3 | 14 | 8 | 22 |
| Build | 0 | 0 | 0 | 0 |
| Test/Examples | 3 | 378 | 48 | 426 |
| Docs | 0 | 0 | 0 | 0 |
| Uncategorized | 0 | 0 | 0 | 0 |
| TOTAL | 6 | 392 | 56 | 448 |


## Appendix B. pto-isa pin — advisory

`pto_isa.pin` remains `c0d7148e95ef73bd12a73165fdce4b723a3b7e72`. No pto-isa header references changed. No pin bump is indicated by this scheduler-only change.

## Appendix C. Independent reviewer notes

No independent CLI or sub-agent review was requested or run. Existing GitHub feedback was read; this report is the manager's own static review.
