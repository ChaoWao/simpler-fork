> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# #2534 — complete-function evidence supplement

## 0. Summary

Head `8300e34c6595544f36cd9620bdbdfaf59156209d`, merge-base `e0a5e9a16ce2df88643fbd20c4e31406f4062e95`. Current CI: 19 success, 1 skipped, all complete. Existing exact-head static PASS remains. This supplement assesses the full public scenario; **manager Ready**, after the r3 report correction and current-head feedback/CI recheck. No new tests or external review posted.

## 1. Background and problem

The selected capability is local A3 HBG level1/2 swimlane diagnostics while three runs can be submitted and continuously replaced. Before this change the collector only admitted two retained run records even when execution had three resources. Example: submit A/B/C with explicit depth3, then continue to sixteen runs; each run must keep its own result and trace, device execution remains serial, and flush must finish file publication before reading or deleting the output directory.

The user's newer requirement is a complete usable capability per PR. Increasing a constant alone would not satisfy it. Execution capacity and retained host-output capacity are different ownership limits; accepting bounded backpressure does not promise unlimited runs admitted at once or arbitrary slow-disk progress.

## 2. Solution and rationale

Swimlane gets independent three-run physical storage and a runtime-granted immutable capacity, rather than widening every collector. Capacity changes are rejected after initialization/running/held-state boundaries. Existing writer seals a run after shard-reference release, merges its export, publishes output, then releases its bucket/cut/credit. An active publication therefore remains visible to flush through the occupied run state. Writer exceptions establish a persistent error and wake waiters.

For the example, public Worker.submit tests now demonstrate three LAUNCHED/ACCEPTED runs and sixteen successive replacements; tests drain all handles and flush before inspecting artifacts. Output-directory cleanup is conditional on proven quiescence; failed drain/flush preserves the directory so a writer cannot be racing deletion.

## 3. Implementation–claim consistency

| Goal | Implementation/evidence | Assessment |
|---|---|---|
| Three outstanding runs | runtime grant + swimlane storage | Supported |
| Continuous public use | three-run and sixteen-run scene cases | Current-head A3 executed |
| Serial device execution | adjacent whole-operator timeline boundaries | Current-head A3 executed |
| Per-run file identity | unique epochs, rows and handoff checks after flush | Current-head A3 executed |
| Failure-safe cleanup | conditional directory removal after drain+flush | Source reviewed |
| Grant and write failure | grant lifecycle / one-publication-fails UTs | Source coverage [1] |
| Receiver progress independent of slow file writer | current single writer still couples progress | Boundary under author review [2] |

[1] UTs include AnUngrantedCollectorKeepsTheSharedDefault, ALimitOutsideTheModulesRangeIsRefused, AGrantIsRefusedOnceTheCollectorIsInitialized, AGrantIsRefusedOnceTheReaderThreadsAreRunning, AGrantIsRefusedWhileARunIsHeld, AGrantedCollectorReservesForWhatItMayHold, ThreeRunsAreHeldAndEachPublishesItsOwnFile, OnePublicationReclaimsOneCapacityAtAThreeDeepFront, OneRunsPublicationFailureDoesNotDisturbTheOtherTwo. These internal fixtures are not public hardware fault injection; job-level green alone is not a claim that each named case was independently confirmed in logs.

[2] The draft one-entry queue on the same writer gives at most one extra admission of slack. It does not prove fully independent receiver progress while output blocks. Original author is revising the exact end-to-end guarantee and whether additional work is required in this PR. Mere same-file conflicts or historical CI are not reasons to mandate merge then a second fragmented PR.

A3 execution evidence: job115088784310, tested merge2114f24 contains this head and basee0a5. Level1/2 three-run and sixteen-run tests each passed (four real cases); inherited test_run placeholders skipped and are not counted. See ../claude-managed/pr2534-830-a3-execution-evidence.json. A5 current jobs completed successfully; no invented A5-specific new-feature claim.

## 4. Residual issues and risks

- **Uncovered scope:** independent receiver/output progress is not established by existing single-writer design; exact necessity for the selected capability is the outstanding design question, not an asserted new code bug. Active author r2 must resolve it before recommendation.
- **Costs:** additional retained host metadata/storage, conservative admission while writes retain ownership, slow-disk backpressure. Default modes and other collectors retain their scope. No latency gain claimed.
- **Follow-up ownership:** no separate numerical-capacity or test-only micro-PR. Necessary failure/flush/budget work belongs with the capability. Any optional broader architecture needs its own concrete authorization.
- No newly proved must-fix implementation defect in this supplemental inspection; keep earlier addressed feedback and current static review. Do not convert pending design reasoning into false CI failure.

## 5. Merge recommendation

Real capacity mismatch → implemented grant/storage and full repeated public scenario → current-head functional evidence and completed CI. Final link (accepted complete-function boundary versus receiver/output separation) remains in active author review. **Manager Ready after r3 review below; no merge action.** When resolved, reassess against the user's complete-feature requirement, not solely the green checks.

## Appendix A. Change breakdown

| Bucket | Files | Added | Deleted | Churn |
|---|---:|---:|---:|---:|
| Core | 10 | 257 | 28 | 285 |
| Build | 0 | 0 | 0 | 0 |
| Test/Examples | 2 | 369 | 18 | 387 |
| Docs | 0 | 0 | 0 | 0 |
| Uncategorized | 0 | 0 | 0 | 0 |
| TOTAL | 12 | 626 | 46 | 672 |

This is a supplement to pr2534-8300e34c6-manager-review.md; it does not claim another fresh all-file review. Selected collector lifecycle and test diff were reread.

## Appendix B. pto-isa pin — advisory

Pin `418de18dde8fa33773dc5c384fdfe5506463b628`; no pto-isa header-reference change found in the reviewed PR. Not blocking. Any needed pin bump must rebuild onboard a2a3 host_runtime.so using SIMPLER_PTO_ISA_BUILD_COMMIT; no rebuild performed here.

## Appendix C. Independent reviewer notes

No optional independent CLI reviewers invoked. Original temporal author is actively revising the design; its unreviewed draft is not independent acceptance evidence.

## R2 author response inspected

The original author now substantiates the normal end-to-end capability and withdraws a mandatory follow-on PR. Manager accepts that distinction: independent slow-disk progress is not promised by a bounded backpressure contract. The requested budget documentation amendment is rejected: reserve_run_records already charges twice record capacity to include the merged copy. No product edit is needed from that mistaken premise. SAME author r3 will correct only the design report and qualify unbounded filesystem I/O and schematic API configuration. Current CI is complete; recommendation is held for this finite report correction, not a newly invented code defect or extra capacity feature.

## Final disposition after r3

R3 result and revised decision/design reviewed. All four retained record classes use reserve_run_records with 2*sizeof(T) accounting; the unsupported extra-budget patch is withdrawn. Normal scoped repeated submission and per-run publication are complete with bounded admission; independent sealer/publisher throughput is optional, not a mandatory follow-on fragment. File I/O remains blocking: flush can time out without canceling a write, and stop_run_writer joins without a bound. This pre-existing boundary is explicitly retained, not claimed fixed by capacity expansion. No performance claim and no fault-injection execution claim.

Rechecked exact8300 OPEN: 19 successful checks,1 skipped, all complete. Reviews and inline comments empty; conversation bot reports no actionable feedback. Earlier implementation findings addressed, current-head static review stands. **Manager Ready: user merge checkpoint**, with the filesystem-stall close limitation disclosed. No new product patch, tests, CI retry, review comment or merge performed.
