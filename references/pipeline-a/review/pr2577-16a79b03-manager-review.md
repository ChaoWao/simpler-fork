> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# #2577 review — preserve run publication order

## 0. Summary

**Approve at reviewed head `16a79b03f79819a4e5c4b94c76fd17df2a9f5e3b`: no blocking findings identified.** The patch fixes predecessor/successor publication inversion with an active-first dispatch pass and an endpoint-specific predecessor guard. Integration with #2571 must preserve both rules.

Review is static plus inspection of existing CI logs, with no local test execution and no GitHub comment or approval submitted.

## 1. Background and problem

The host may prepare later runs while an earlier run is active. Native chip lanes order entries by dispatch ID; a prepared successor cannot launch before authorization from predecessor acceptance. Therefore publication order is part of forward progress, not just an optimization.

Issue #2565 supplies a concrete failure: run 2 obtains dispatch ID 1, while run 1 obtains dispatch ID 2. The inactive run 2 sits at the native FIFO head, blocking run 1. Run 2's authorization in turn needs run 1 to be accepted. Neither launches. The later unbounded cleanup wait hides the initial launch-fence assertion.

The PR's goal agrees with the issue: restore publication ordering, including dependency-blocked predecessor tails and groups. It does not claim arbitrary-depth resources or a generalized new scheduling policy.

## 2. Solution and rationale

- `Scheduler::dispatch_ready` retains one active-run snapshot for group reservations, NEXT_LEVEL singles and SUB tasks. Those dispatch phases now precede successor staging and early-launch authorization.
- Before popping a successor single, `Orchestrator::can_stage_run_on_worker` checks its PREPARED phase and valid lease, then walks earlier FIFO runs. An open submission blocks staging because further predecessor tasks are not yet known. For a closed predecessor, PENDING/READY/BUILDING NEXT_LEVEL tasks targeting this endpoint block it; already claimed/published work does not.
- `Worker::init` wires the guard into scheduler configuration. This remains internal host C++ coordination; no wire format, public API, KernelArgs or device memory ownership changes.
- Scheduler loop serialization spans claim and publication. The guard takes `runs_mu_`, then a predecessor's `completion_mu`; relevant retirement/acceptance paths release the completion lock before acquiring `runs_mu_`. Task slots are retained until quiescent compaction, which also takes the scheduler loop lock.
- Endpoints with no remaining predecessor work can still stage successors. A global all-endpoints acceptance gate would eliminate more preparation overlap than the per-endpoint ordering invariant needs.

In the cold-queue example, run 1 now receives its dispatch ID before runs 2 and 3. For a run with a dependency-blocked tail, merely moving the staging block would be insufficient: the new guard defers the successor until that tail has actually been published on the same endpoint.

## 3. Implementation–claim consistency

| Goal | Mechanism | Location | Assessment |
| --- | --- | --- | --- |
| Fix cold-queue inversion | Active dispatch before staging | scheduler.cpp:485–518 | Solid |
| Protect pending predecessor singles | Scan closed predecessor task states and target IDs | orchestrator.cpp:450–475 | Solid |
| Protect groups | Same task scan covers every target endpoint | orchestrator.cpp:468–471 | Solid |
| Retain independent-endpoint overlap | Block only relevant NEXT_LEVEL targets once submission closes | orchestrator.cpp:461–471 | Solid |
| Deterministic regressions | Loop-mutex cold setup; scan synchronization; explicit dispatch order checks | test_scheduler.cpp:2256 onward | Solid |

The diff adds three regression cases. Cold setup holds the scheduler loop mutex until all three runs are queued. Tail/group tests wait for a staging scan and then synchronize on that mutex before checking absence of premature publication; they release the predecessor and verify the later positive sequence. Existing assertions are retained.

Current GitHub checks: **19 success, 1 skipped**, all complete. Independently read existing logs:

- Ubuntu UT job `115768426178`: builds `test_scheduler`, executes it successfully, and reports **317/317 CTest targets passed**.
- A5 onboard job `115768426040`: the original `test_a_device_result_feeds_the_next_two_runs` passes on device 4 in 15.4 seconds. Concurrently accepted/launched three-run and sustained-refill cases also pass.

The author's five extra hardware repetitions and Python totals are reported in the PR body; this review does not relabel those separate local runs as manager-reproduced results.

## 4. Residual issues and risks

### Uncovered cases

No concrete unhandled case introduced by this diff was established. Green CI is not proof against every possible interleaving. The new negative and positive ordering checks materially cover the identified cycle.

### Costs

Each relevant staging scan can walk prior runs and their task slots under orchestrator locks. The scan remains state-change driven and is skipped for endpoints without a successor single; no latency improvement is claimed.

### Integration follow-up

#2571 changes the same scheduler/orchestrator area and currently does not contain this guard. Resolve semantic overlap when integrating; neither textual conflict resolution nor the configurable-depth acceptance tests substitutes for retaining the three ordering regressions. This is an integration requirement, not a defect blocking #2577 in isolation.

## 5. Merge recommendation

The ordering cycle is concrete; active-first dispatch plus the endpoint-specific guard addresses the cold, dependency and group cases; the implementation matches its stated scope and existing CI exercises the reported failure. **Recommend merging #2577**, preserving its invariant during subsequent #2571 integration. No merge performed.

## Appendix A. Change breakdown

Merge-base `946f49acbbd4df72b333784cfad60b1573e70b40`; reviewed against freshly fetched main `d793c89c32c438de6b411595a4485b2943aabf44`.

| Bucket | Files | Added | Removed | Churn |
| --- | ---: | ---: | ---: | ---: |
| Core | 5 | 55 | 13 | 68 |
| Test/Examples | 1 | 152 | 1 | 153 |
| Build | 0 | 0 | 0 | 0 |
| Docs | 0 | 0 | 0 | 0 |
| Uncategorized | 0 | 0 | 0 | 0 |
| Total | 6 | 207 | 14 | 221 |

## Appendix B. PTO-ISA pin

Pin remains `418de18dde8fa33773dc5c384fdfe5506463b628`; no PTO-ISA include or pin change in this patch. No pin bump is indicated by this review. Any future bump requires corresponding onboard runtime rebuild.

## Appendix C. Evidence

PR: https://github.com/hw-native-sys/simpler/pull/2577
Issue: https://github.com/hw-native-sys/simpler/issues/2565
Local evidence: `../claude-managed/pr2577-review-current.json`, `pr2577-ubuntu-ut.log`, `pr2577-a5-onboard.log`.
No independent CLI reviewers were requested or started. No external review comments were present at inspection.
