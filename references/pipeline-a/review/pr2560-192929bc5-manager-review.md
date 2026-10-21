> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# PR #2560 — 192929bc5 manager review

## Summary and goal

Exact head `192929bc5fa01a26052d36b962301e44efaaf9b4`, freshly fetched main and merge-base `55eb522468fe702d564a131ee910ac1b5326858a`. One complete A3 TMR ordinary early-enqueue feature remains the approved goal: local L3 single/dual endpoints, sustained refill, serial whole-operator execution, run-owned errors, HOST/DEVICE lifetime, fallback and close.

**Current source review: PASS. Not Ready: A5 checks remain incomplete and two A3 bootstrap failures remain unresolved. All six CPU regressions passed on Ubuntu; nine substantive A3 early-enqueue cases also passed on this head.** Prior-head successes are historical evidence only. No local test, build, install, device probe, benchmark or CI retry was run by the manager.

## Problem and method

The former unmarked regression functions in scene modules were collected but never dispatched. Resource selection excludes them, while the level-two subprocess excludes an item with no scene level. The CPU workflow selects `tests/ut -m "not requires_hardware"` without platform/runtime/level filters. Moving the checks there makes them eligible for pytest's ordinary loop.

The new parametrized fixture loads each architecture's actual scene module under a private name and drives its real evidence-reader methods. It verifies a missing successor boundary keeps the wait open, present but inverted boundaries still fail ordering, and another child's identically numbered run cannot establish this endpoint's evidence. There is no device access in these fixtures. Runtime import availability still requires actual CI execution; static reasoning alone is not reported as a test pass.

## Implementation and claim consistency

| Requirement | Exact-head evidence | Result |
|---|---|---|
| A3 TMR public early-enqueue and run-owned errors | Production patch unchanged through rebase; inherited exact-head source review | Static PASS |
| Preserve hardware oracle | AST comparison of both scene modules: no surviving function or class changed; only planted helpers and two unreachable test functions removed | PASS |
| Execute regression checks | `tests/ut/py/test_early_enqueue_evidence_reader.py`, three checks × two suites; workflow and root filtering/dispatch inspected | All six executed and passed on current-head Ubuntu CI |
| Test actual readers | `spec_from_file_location` invokes each suite, no copied predicate implementation; private module removed by monkeypatch | PASS |
| Preserve latest stream ownership | Main #2557 keeps submission ordering while binding streams to PreparedExecution; no competing lifetime rewrite | PASS |
| Complete feature and feedback | Previous capability/result/lifetime corrections preserved, inline 4254663539/4254663541 addressed; three feedback surfaces re-read | No new blocker |

The source delta versus f620 is limited to relocating regression coverage, adding the explicit overlapping-boundary case, and correcting a comment from three to all onboard runtimes. The latest main integration does not weaken event or shared-arena ordering. The existing nine A3 hardware cases remain unchanged; their f620 pass does not substitute for execution on 192929.

## Residuals and recommendation

Approve statically; current-head Ubuntu CPU regressions and A3 acceptance are verified. Wait for remaining relevant CI, including the A5 dual-endpoint scene. The older device9 BootstrapDispatcher 507018 investigation remains unresolved; neither rebase nor tests on other devices establish its cause. Do not merge or mark Ready now.

The two HBG suites' related evidence-wait pattern remains a scoped follow-up already recorded in the previous review, not part of this TMR feature. No HBG failure is claimed without evidence. External graph, kernel and workload owners remain separate.

## Change breakdown

| Category | Files | Added | Removed |
|---|---:|---:|---:|
| Core | 4 | 80 | 50 |
| Tests/examples | 7 | 2207 | 73 |
| Docs | 2 | 29 | 19 |
| Build / uncategorized | 0 | 0 | 0 |
| Total | 13 | 2316 | 142 |

Large overall diff, dominated by complete public-API acceptance coverage. Keep one complete feature PR per user requirement; no core-size concern or newly added partial-feature PR.

The active pto-isa pin is unchanged (`418de18dde8fa33773dc5c384fdfe5506463b628`); the full feature's `pto/pto-inst.hpp` test-kernel dependency was compiled on the prior head. Current-head A3 onboard CI has reconfirmed compilation and execution; no new dependency or speculative pin bump in this revision. Optional external reviewers were not requested.

Earlier CI snapshot (superseded by the current-head execution section below): 7 success,1 skip,7 in progress,3 queued,2 failed. Both failed A3 logs were downloaded completely and checked against test merge721bf64=192929+55eb: UT/DeepSeek fail at device9 bootstrap sync507018 before target run. See the linked diagnosis for runner identities and evidence limits. Source approval remains static only; this is not Ready.


## Current-head execution verified (2026-10-13 07:00 UTC)

Both downloaded logs are complete (verified HTTP206 ranges), and their final checkout is721bf64=192929+55eb. Ubuntu job115164775581 runs all six named actual-reader regressions successfully,3041 Python tests pass and314 CTest cases pass. A3 onboard115164775267 runs nine substantive early-enqueue cases successfully; six inherited test_run placeholders skip and are not counted as acceptance. DFX smokes succeed. This closes the previously missing CPU execution evidence for Ubuntu and reconfirms A3 acceptance on this exact head. macOS checks are successful; A5 onboard remains queued, A5 UT timed out after its hardware cases passed, and the two bootstrap failures remain unresolved; still not Ready. See [structured evidence](../ARCHIVE-INDEX.md#ref-33a14c75656e0d8f).


## Follow-up (2026-10-13 07:35 UTC)

CI now15success/1skip/2failure/1cancelled/1queued. macOS completed successfully. The A5 UT cancellation is the job30-minute limit after device queue wait, with four hardware cases passed but CANN smokes incomplete; see [timeout analysis](pr2560-192929-a5ut-timeout.md). No retry submitted while this workflow remains incomplete. New main dc73516d2 changes package revision stamping and DeepSeek timeout, not these failure paths; current test-merge evidence remains tied to55eb.

## Current-head A5 execution evidence (2026-10-13)

A5 onboard115164775451 completed on exact test merge721bf6422b09c0e653fe229116f997e8cbbaa29c. Full210508-byte206 log confirms13 substantive early-enqueue cases:7HBG and6TMR, including both actual device endpoints, sustained refill, correct failure ownership and live communication-domain rejection. Seven inherited test_run placeholders are skipped, not executed passes. Scene tests and DFXsmokes completed. This closes the A5 evidence wait/rank-attribution execution gap; it does not fix the separate A3 bootstrap failures or complete the canceled A5UT CANN examples. Ready remains false. Structured case evidence: [execution ledger](../ARCHIVE-INDEX.md#ref-33a14c75656e0d8f).


## Retry completion 2026-10-13T10:37:43.930864+00:00

A5 UT retry115217384657 completed success; full56738B HTTP200 log finalcheckout721bf64=192929+55eb, hardware4PASS and CANN7built/4ran/3archskip/0failure. All checks terminal17success1skip2A3failure. Bootstrap device9 root remains unknown; no further POST.


## Ready verification — 2026-10-13T12:38:41.023655+00:00

Current exact head192929bc5fa01a26052d36b962301e44efaaf9b4 remains OPEN. CI19success/1skip, all completed. Both review threads are resolved/outdated; summary/conversation feedback reviewed and blocking comments addressed. Current-head static review and CPU/A3/A5 actual feature execution evidence satisfy manager Ready. Await user merge; no merge performed.

Workflow38357440538 external attempt3 (triggering actor ChaoWao) completedsuccess at12:20:41Z. New A3UT115289620187 and DeepSeek115289620067 succeeded. Full HTTP200 logs (42434/52327 bytes) confirm finalcheckout721bf6422b09c0e653fe229116f997e8cbbaa29c=192929+55eb. A3UT four hardware cases/CTest1/CANN7 built7ran0failure; DeepSeek HBG1pass devices4,5 and TMR1pass devices6,7. These are different successful device allocations; prior device9 bootstrap rootcause is still unknown, not proven repaired. Retain diagnostic history, withdraw current user-log blocker. Manager sent no new POST; prior A5UT retry remains the only manager POST for this head.
