# Ordinary args-dump loss accounting: combined candidate

> 本文保留并行子任务的候选与验收证据；其代码已集入同一个#2677，不另提loss PR。当前合成head及最终验证见[主执行报告](../args-dump-stop-proof/REPORT.md)。


Status: combined CPU acceptance passed; local candidate awaiting parent final review.
No push, PR, GitHub message, new issue, or onboard work was performed.

## Fingerprints and dependency

- Internal task: original #2666.
- Original exact baseline: fa6528f4e45c42242e2162c2ba0192d09cbd3b88.
- Original standalone loss fingerprint (preserved): c1f9cfe9fb7fde00f8d0dcebf8e39dca1a82dee0.
- Required stop-proof parent: 19ed2e4625703cad9096d8c9407b516d115e8ef8.
- Combined loss head: 21537980b33b6b6d0490b379549f2e83a3370bc1.
- Branch: fix/args-dump-loss-accounting.
- Worktree: simpler/.claude/worktrees/args-dump-loss-accounting.
- Exactly one loss commit follows the stop-proof parent; tracked worktree clean.
- Delivery order remains stop-proof first, then the dependent loss change.

The branch was rebased with --onto onto the stop-proof commit. One expected
args-dump.md conflict was resolved after reading resolving-merge-conflicts/SKILL.md:
the final text preserves both the parent's stopped-producer/host-reader shutdown
contract and the loss candidate's new incomplete-accounting statement. The parent's
ordinary close implementation and runner decisions were not modified.

## Resulting contract

Ordinary manifests retain numeric dropped_records and add dropped_records_complete.
A run starts incomplete. Successful stopped-producer reconciliation can establish
complete record-loss accounting. Failed state or held-metadata reads, unresolvable
or unattributable held metadata, malformed held record count, uncounted retired
transport buffers and saturation retain false; the numeric field is an observed
lower bound. A zero with false does not prove zero loss. The flag does not certify
payload transfer, disk writes or the whole artifact.

Counter retry uses a replacement snapshot, avoiding duplicate addition. Retired
transport loss remains sticky for the run even after report_drain_drops consumes
its base tally. Ordinary uint32_t host counters preserve their type and saturate
as a lower bound. The shared manifest metadata uses optional<bool> so retained
exporters leave the new field absent and preserve their existing schema.

The integrated stop-proof boundary is the production precondition for that
confidence claim: completed execution OR independently joined producers allow
quiesce/reconcile/export; unknown completion stops and joins host readers, skips
producer-held recovery, and exports host-owned records with complete:false.
Direct reconcile_counters callers still must satisfy its documented stopped-
producer and host-transport-quiescence precondition.

## Combined changes versus stop-proof parent

Six files, 229 insertions, 12 deletions:
- args_dump_collector.h: confidence state and corrected snapshot-counter contract.
- args_dump_collector.cpp: confidence reset/reconcile/export logic.
- args_dump_manifest.h: optional ordinary confidence serialization.
- test_args_dump_read_failure.cpp: dedicated producer-to-manifest regressions.
- test_args_dump_ordinary_close.cpp: three combined real-manifest assertions.
- docs/dfx/args-dump.md: count lower-bound/completeness contract.

No runner, close implementation, retained protocol, CMake registration, ABI version,
capabilities or PreparedCall change is present in the loss commit.

## Combined validation

Project-local .venv uses --system-site-packages. Maximum build parallelism -j 4;
ctest -j 2. No board use. Production C++ sources are compiled directly by CPU UT.

Equivalent replay commands (16 targets):

  cmake -B tests/ut/cpp/build -S tests/ut/cpp
  cmake --build tests/ut/cpp/build --target \
    test_{a2a3,a5}_{tmr,hbg}_args_dump_{ordinary_close,read_failure,collector,retained_runs} -j 4
  ctest --test-dir tests/ut/cpp/build \
    -R 'args_dump_(ordinary_close|read_failure|collector|retained_runs)$' \
    --output-on-failure -j 2

Result: 16/16 binaries pass; 160 GoogleTest cases complete; ctest wall time 7.89 s.
- Configure: LOCAL_PATH
- Build: LOCAL_PATH
- ctest summary: LOCAL_PATH
- Complete per-case output: LOCAL_PATH

Each close binary runs these three production-runner boundary cases, including
real payload and manifest output, across both archs and runtimes (12 total):
- CompletionRecoversAnUnpublishedHeldRecord: dropped_records_complete:true.
- ProducerStopRecoversAnUnpublishedHeldRecord: dropped_records_complete:true.
- UnknownCompletionExportsOnlyHostOwnedRecordsAndJoinsReaders: false, host-owned
  record retained, held record absent, readers joined, no later transport over-ack.

Each read_failure binary runs 10 cases with two transport parameters (80 total):
- SuccessfulReconcileReportsCompleteZeroLossCount
- FailedReadPreservesCollectedRecordsAndMarksLossCountIncomplete
- ExportWithoutReconcileReportsIncompleteLossCount
- DeviceRefusedRecordIsCountedExactly
- RetryReplacesCounterSnapshotAndCanCompleteLossCount
- RetiredTransportBufferKeepsLossCountIncompleteAcrossReconcileRetry
- NewRunDoesNotInheritCompleteLossAccounting
- UnattributableOrMalformedHeldBufferLeavesLossCountIncomplete
- SaturatedRunCounterIsOnlyALowerBound
- FailedReadCannotRecoverCachedRecordsAndCanRetry (original regression)

The held read failure preserves observed count 7 while bulk failure leaves observed
0, both with false; successful retry yields 7 rather than 14. Null free-slot
refusal exercises actual producer loss accounting without a 30 s timeout.
The neighboring collector suite runs 4 cases per binary (16 total), and retained
runs 13 cases per binary (52 total).

Full applicable pre-commit hooks pass on all six loss files, including clang-tidy,
clang-format, cpplint, markdownlint-cli2 and repository checks. Raw:
LOCAL_PATH git diff --check and staged checks pass.

## Preserved negative controls

Original exact-baseline experiment: new success, read-failure and no-reconcile
real-manifest assertions all failed, 6 selected parameter cases. Their output
lacked completeness information even when dropped_records was 0 after a read
failure. Raw: LOCAL_PATH build: LOCAL_PATH

Targeted mutation: removing only counts_complete = false from the held-buffer
copy-failure branch made the dedicated held-failure case exit 1 and print a real
manifest falsely claiming complete:true. Original source was restored in a finally
block, rebuilt, and the identical case passed with exit 0. Raw:
LOCAL_PATH and LOCAL_PATH This mutation is
absent from the combined commit. The initial full standalone report is preserved
at LOCAL_PATH

## Limits and local environment note

Combined close tests use the production sim runner, actual collector threads and
real producer/collector/export behavior on CPU. Read-failure tests inject copy
failures deterministically with separate host shadows on both archs. No real DMA
failure, onboard test or broad scene sweep was attempted. Runs with no collected
metadata retain the existing no-manifest behavior. The flag is deliberately
narrower than artifact completeness.

The isolated venv needed pre-commit installation. pip reported inherited
ms-service-profiler 26.0.0 missing optional dependencies, changed no global
packages, and all requested checks passed. This unresolved environment warning is
recorded in the worktree's ignored KNOWN_ISSUES.md; initial install output is
LOCAL_PATH

## 主执行验收状态

已检查候选源码及真实manifest回归，组合基线是#2677 head19ed2e462，后续loss单commit21537980b。16目标160cases通过；接受为本地后续候选，未push/提PR，不表示已合入主线。生产语义必须先消费停机证明，不能把成功读取等同producer已停止。

## 集成交付边界修订

上文保留子任务初始独立候选及原head证据。独立Claude审阅指出，unknown close若跳过counter读取却仍只输出0，报告本身不完整。主执行因此将21537980b的统计/manifest/回归整体集入#2677，和停机证明一起交付同一完整功能，不再计划独立loss PR。原候选成绩不自动移挂最终合成head；组合重验与最终审查由主执行统一负责。
