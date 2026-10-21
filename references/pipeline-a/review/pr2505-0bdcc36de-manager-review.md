> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# PR #2505 — 0bdcc36de manager review

**Static PASS; not Ready until current-head CI completes.** Exact head `0bdcc36de0a81e19ca650b258806f7d301114977`, fetched main and merge-base `121d807bb988af39761c577d4e23ad2b17d6f69f`. Original temporal owns this one PR; manager performed source review only, no tests or external reviewer CLI.

## Purpose and mechanism

The approved contract is swimlane handoff identity, per-run loss accounting and safe ACK uncertainty. A 32-byte ready entry carries epoch/count before tail publication. Host validates the declaration and descriptor, cross-checks a readable payload, attributes a retired buffer to its held run, and quarantines a queue whose ACK cannot be resolved. Finite per-queue cuts and terminal evidence govern sealing. This does not enable diagnostic early enqueue or capture.

Review order: producer/layout → shared drain and ACK/cut → retained bucket lifecycle → tests. This review reads the five-file revision delta plus the shared drain, producer declaration barrier, loss callback, admission/release/seal and test lifetime paths; previously reviewed unchanged producer/ACK/cut findings remain closed.

## Blocking feedback disposition

- **F1 closed.** Both normal sweep and final drain first observe an entry, then call `settle_handoff_trust`. Unsettled state leaves that entry untouched; any shard can probe under the same mutex, and release/acquire publishes the verdict. Producer declaration precedes publication with `wmb()`. Bounded failed reads remain distinct from undeclared producers. The bounded fallback does not prove identity: successful payloads report unverified identity, failed/untrusted handoffs follow unattributed error reporting. Context replacement resets trust. No first-shard scheduling assumption remains.
- **F2 closed.** `loss_epoch`, `LossState` and counters share `loss_mu`. `release_run_slot` invalidates authority before publishing the slot Free; run admission installs a new identity under the same lock; seal snapshots and changes authority together. A late handoff cannot silently charge a canceled run or its successor. A refusal records the existing collector error after unlocking. Release does not hold `loss_mu` while acquiring `retained_mu_`.
- **F3 closed.** Callback storage is copied under its mutex into a shared owner before invocation. Tests no longer replace the active callback; `ParkedCharge` selectively parks the first callback and releases/joins through RAII, including ASSERT exits. The 7ad Ubuntu four-variant segfault occurred in the old mixed-successor test; that implementation is replaced, but new-head CI must independently validate it.

No additional blocking source finding identified in this revision. Three remote inline threads are resolved; seven review summaries and one bot conversation comment checked, no new actionable request. Bot green/skip status is not an independent current-head code review.

## Evidence and limits

Author reports nine selected cases passing, two negative controls failing as intended, restored filter passing, and whole cpput compilation. This includes revised previously executed exclusion cases, plus repeated filter execution for controls; therefore the public wording “nine new cases, run once / every previously executed identity case not rerun” overstates uniqueness. Manager does not adopt that wording and ran nothing. No further reruns requested.

The five new drain cases execute the final drain using split host/device storage and an unmappable payload. Normal-sweep integration is source-checked, not directly exercised by these new tests. Concurrent reuse and release are tested; concurrent seal is **not** tested. No new on-device result is claimed; shared collectors' behavior awaits CI. These are validation limits, not proof of unsupported behavior. Prior CI failure belongs to old7ad, not to this head.

## Size and integration

Nine files, +1898/-40 =1938 changed lines: Core7 files +1018/-40 =1058; Build1 +1; Test1 +879. This is a large shared transport change with review/integration cost; the user authorized one cohesive PR, so it remains one PR. `pto_isa.pin` unchanged and no new pin/header dependency. Existing #2096 touches a separate additive CMake block. External #2506 changes A3 scheduler retirement and does not duplicate this collector scope. Fresh main unchanged; no rebase needed now.

Next: monitor exact-head CI, triage any failure before retry, and mark Ready only with completed relevant CI. No merge by manager. Completion of this PR remains a prerequisite for a separately scoped diagnostic early-enqueue design.
