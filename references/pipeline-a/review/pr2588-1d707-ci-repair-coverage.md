> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# PR2588 current CI failure coverage map

Published head: 1d707f440; Ubuntu UT job116315330402, workflow38702289624. The 24 failed CTest targets contain 21 distinct failed case names across architecture/runtime builds. This map checks candidate fixes, not execution success. No tests rerun.

| Family | Failed targets | Current candidate / outstanding verification |
| --- | ---: | --- |
| DepGen retained runs | 2 | Mapped bank copy must read bytes, not success-only stub. RefusedGraph budget assertion uses pre-first-admission zero baseline although run_begin opens budget with fixed overhead; compare against established fixed overhead before producing records and prove record blocks return, not change assertion to arbitrary nonzero. Later runs must still emit their own graphs with sticky prior loss. |
| ScopeStats retained runs | 1 | Mapped terminal bank copy addresses missing accounting; held-buffer test must preserve producer-owned records rather than flush them. Recheck whole terminal conservation path. |
| PMU retained runs | 4 | Mapped copy plus final partial-buffer published_record_count/published_buffer_count update before terminal close; successful enqueue must snapshot count before host recycling. |
| ArgsDump ownership | 4 | Mapped copy and held-buffer fixture; bank release at terminal must stay distinct from output publication. Unknown stop retention remains blocking independently. |
| Admission rollback notifier | 4 | New attempt needs its own output target; refusal for pre-existing output names is not missing capacity. Preserve generation suppression and real rollback assertions. |
| Diagnostic admission | 4 | Hold all output slots before unrelated-epoch withdrawal; prove no other run released. Do not lose separate duplicate identity coverage. |
| Swimlane retained runs | 4 | Collector run_begin is nonblocking under lane lock; fixture must retry capacity externally. Sleep-based fixture does not validate real credit-generation wakeup. Retain production lane wake/rollback tests. |
| Chip lane credit block | 1 | Fixture initializes pipeline_generations_ to granted count; then verify actual capacity refusal leaves Prepared carrier and retry/close works. |

## Further blockers beyond 24 targets

Four scene jobs fail on first-run finalize (-1000); candidate shared bank-copy and PMU changes need actual new-head CI, not inference from UT. The passed four settlement tests inject stop facts and do not establish actual architecture worker failure/join proof. Working-tree quarantine changes require invalid-identity, last-producer, stale-generation/reopen and full-container ownership checks; see [working-tree review](pr2588-bank-fix-wip-review.md). Main2579 integration and all eight unresolved review threads remain in final-head acceptance.

## Exact distinct failures from current CI

- `ArgsDumpRunTerminal.AMappedCloseReadsNoLaneBuffer`
- `ArgsDumpRunTerminal.AMappedRunIsSettledFromItsOwnProducerAccounting`
- `ArgsDumpRunTerminal.ANormalMappedCloseSucceeds`
- `ArgsDumpRunTerminal.AProducersHeldRecordsAreNotASuccessfulManifest`
- `ArgsDumpRunTerminal.TheBankIsFreedAtTheBoundaryNotAtPublication`
- `ChipRunLaneCreditBlockTest.ACapacityRefusalLeavesTheRunPreparedRatherThanFailed`
- `ChipSwimlaneRetainedRunsTest.RunsUnderOnePrefixEachOwnTheirOwnDirectory`
- `ChipSwimlaneRetainedRunsTest.SlotReuseAcrossRunsKeepsEachRunsRecordsInItsOwnFile`
- `ChipSwimlaneRetainedRunsTest.ThreeRunsProgressWithoutAnyFlush`
- `DepGenMappedTest.AMappedRunIsSettledFromItsOwnProducerAccounting`
- `DepGenMappedTest.ARefusedGraphReturnsItsReceiptStoreAndBudget`
- `DepGenMappedTest.ASecondAdmissionDoesNotEraseThePredecessorsEdges`
- `DepGenMappedTest.ASuccessorsRecordsAreNotForeignWhileThePredecessorIsOpen`
- `DepGenMappedTest.AnOverflowChainStillPublishes`
- `DiagnosticAdmissionTest.WithdrawingARunAnOwnerNeverAdmittedLeavesTheOtherRunAlone`
- `DiagnosticRollbackNotificationTest.TheSameUndoBeforeTheWriterTakesTheRunWakesNobody`
- `PmuRunTerminal.AMappedRunIsSettledFromItsOwnCoreAccounting`
- `ScopeStatsRunTerminal.AHeldBufferIsReportedByTheRunRatherThanRecoveredByTheHost`
- `ScopeStatsRunTerminal.AMappedRunIsSettledFromItsOwnProducerAccounting`
- `ScopeStatsRunTerminal.ASecondAdmissionDoesNotEraseThePredecessorsRecords`
- `ScopeStatsRunTerminal.EachOpenRunKeepsItsOwnReceiptCount`
