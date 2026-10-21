> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# PR #2451 — current-head manager review

- Reviewed head: `5ce210cec8ff44c588a90b104cdeb317ffff6ccd`.
- Base / merge-base: `a9e6a42b64933f2a555d0d13432826e3d5b83359`.
- Scope: user-approved local L3 PMU background output, same ONE PR.
- Result: **READY — static pass, current CI complete, blocking feedback addressed (2026-09-24T08:33Z).**

## What this delivers

With collection across runs explicitly enabled, each PMU run freezes its event group, columns, destination and completion evidence. Host receipt and CSV merge can continue during the next serial device execution. Readers must flush; two unpublished runs cause prelaunch refusal. Incomplete data or write failures stay visible for the runner lifetime. Default behavior and CSV format remain unchanged.

## Review findings closed at this head

1. R1: both platforms propagate configuration publication failure through a bool and refuse retained admission before launch. All field writes are attempted and any failed write refuses admission; default mode preserves its prior behavior. The refused epoch is withdrawn without a false lost-output verdict. Its identity was never launched, so a concurrently refreshed shard view cannot receive a producer buffer for it; reused slots are published to all shards before the next launch.
2. R2: the distinct late-row regression holds the writer, admits B only after A's close snapshot, then delivers A through production routing after B resets live configuration. It proves the intended routing/configuration window, not hardware overlap. Author-reported first execution passed on each architecture; no manager rerun.
3. R3: public flush and PMU documentation distinguish partial CSV failure, proved-empty success, sticky runner errors, two-run refusal, pool limits and native teardown's log-only late-error boundary. PR validation history now discloses prior repeated cases and pre-fix failures rather than presenting them as clean first-run passes.
4. Remote teardown finding: `release_resources` records `Abandoned` for remaining non-Free/non-Quarantined epochs before release; the new verdict is included in bounded reporting and error classification. Both platform finalizers stop/join writer and collectors before recording/releasing, and errors survive the collector rebuild. Already withdrawn slots are Free and excluded; quarantine already has its own error.
5. Remote permission finding: the directory-permission injection explicitly skips root, where it cannot cause the intended write failure. This is a coverage limitation, not a passing write-failure validation under root.
6. Previous F1–F5 fixes remain present: locked metadata release, no invented decrement, close-error check before rename, default begin/start order, and A5 narrow device snapshot into scratch.

## Non-blocking wording to clean up

`withdraw_unpublished_slot` says `abandon_run`'s run “did launch”, but `abandon_run` is expressly for admitted, never-launched rollback. The code does not launch it; the explanation should describe the conservative post-admission withdrawal protocol instead. This is not evidence of a functional lifetime failure.

## Validation and limits

Static review of the full approved change inherited from the previous exact-head review, plus every file in the 425-addition/29-deletion correction delta. No manager test execution, CI retry POST, merge or remote comment. Configuration-copy refusal is inspected but lacks injected local coverage. The current CI is a new automatically triggered run; old head green checks do not qualify this head. Remote findings are fixed in code, with original author handling thread resolution.

## CI evidence refresh — 2026-09-24T08:19:49.251053+00:00

Current-head Ubuntu UT job 107546535602 logs show all four new PMU targets actually ran and passed (CTest #103–106, lines4901–4908), rather than merely compiling. Evidence: `../claude-managed/pr2451-ci-evidence/ubuntu-ut-107546535602.log`. Target-level output does not disclose every individual skipped case; root-sensitive injection remains conditional. CI now15success/1skip/4running, no failures yet. Two remote threads remain resolved, no new summary/conversation feedback. No new implementation dispatch, test execution or CI retry.

Checkout provenance: job log line127 checks out GitHub PR merge commit `b655960`, combining reviewed head `5ce210cec8ff44c588a90b104cdeb317ffff6ccd` with main `c7ae459243fb9956290055aef749cd7e9f4fcd21`. The reported CI is this head’s integration check, not a local bare-head rerun. CTest reports251 targets passed.

## Ready checkpoint — 2026-09-24T08:33:19.629360+00:00

Head remains `5ce210cec8ff44c588a90b104cdeb317ffff6ccd`. All checks completed:19success/1skip; two remote threads resolved and all feedback surfaces unchanged. Prior static pass and addressed blocking feedback qualify **READY**, awaiting user merge. No CI retries or manager tests. Remaining non-blocking wording note does not change implementation behavior. Manager timer paused because no authorized task can currently progress without a decision or new evidence.
