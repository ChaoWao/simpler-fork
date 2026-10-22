# Ordinary args-dump stop-proof close

- Base: `fa6528f4e45c42242e2162c2ba0192d09cbd3b88` (#2671 merge).
- Product PR: [#2677](https://github.com/hw-native-sys/simpler/pull/2677), final documentation head `154c77af6baad138ec8b05b37f2253bb2188c55a` (code-equivalent to tested/reviewed `a547efa3d`). First-head evidence below belongs to `19ed2e462`.
- Parent tracker: #2666. This is one bounded C1 correctness fix, not capture DFX qualification.

## Behavior

The real ordinary runner boundary consumed neither of its stop proofs. Unknown completion could recover a held unpublished record, export with host readers live, and use the recovered write to credit a later uncollected publication.

The common collector close now permits recovery after execution completion OR independent producer termination. Without proof it joins host transport/readers, skips held recovery and exports only published host-owned records. Host thread exit supplies no device-free authority. Successful ordinary resident collection and retained settlement preserve their paths.

## Parallel execution and acceptance

One parent owns production changes and final integration. A separate child wrote real-runner/AICPU-producer tests on the exact base. Parent inspected the commit and raw red matrix, then integrated only the accepted tests into the same functional PR. A second child independently produced ordinary loss completeness. Independent review showed that unproven close must also express unknown accounting in the artifact; the parent therefore integrates both branches into the same functional PR instead of shipping a misleading zero and a later correction.

## Validation

- Four arch/runtime binaries, three cases each: completion and joined-producer controls pass on the original base; unknown completion fails reader retirement, held payload/manifest exclusion and later arena credit. With the fix all pass.
- Full CPU C++ suite: 390/390 programs passed.
- Applicable pre-commit: passed, including clang-tidy, cpplint and markdown checks.
- Initial Python run crossed a commit identity change: four subprocesses correctly rejected an extension stamped with the previous HEAD. Fixed-HEAD rebuild and rerun pass: 3525 passed / 192 skipped in 191.78 s. The initial failed run remains recorded and is not counted green.
- A3 task-submit task_20261022_062351_354754418141 passed: public owned/borrowed args-dump matrix (4 cases), TMR full and HBG full scenes. Before/after source/runtime hashes match; raw logs, persistent basetemp/plog and occupancy snapshots are retained locally. Actual Claude CLI independent review and final-head CI remain running. No hardware hang injection is performed.

## Limits

The close does not establish a bounded device-stop guarantee, alter device release/abandonment, or add capture support. Epoch/read checks do not prove quiescence. The combined PR explicitly marks unknown ordinary dropped-record accounting; retained manifest shape is preserved. The CPU regression observes effects and actual thread state, not every individual memory read.

## Review rework: live publication during stop

GitHub review4348885168 identified a remaining first-head gap: stop() still performed a device-queue final drain despite unknown producer state. An active publisher could keep it busy before export. The parent accepts the finding.

A deterministic child regression gates the real drain thread, waits for the actual stop request, publishes a finite second device buffer, then releases the gate. All four first-head binaries pass the original3 cases and default-stop positive control, but fail the unknown-close late-publication case: queue head advances, the late payload is exported and its metadata appears. This uses no infinite producer or sleep-based ordering. Test commit e73a25835961146e220a3dfb655e44706610fa69 was inspected and accepted.

The same product PR is being corrected: unknown close uses stop(false), normal draining checks stop at each entry boundary, and the final device drain is conditional. Already handed-off host records still finish. First-head test/board results above remain tied to19ed2e462; final-head rebuilding, review and CI must run after amendment. The independent loss candidate will also be rebased and revalidated rather than inheriting old evidence.

## Independent Claude review and integrated loss accounting

Actual Claude CLI reviewed first head19ed2e462 read-only. It found no Must-fix but3 Should-fix: unknown loss after skipped reconciliation, a similar ScopeStats residual, and stale recovery docs. The parent accepts unknown reporting as part of this functional boundary and integrates the already-tested loss branch21537980b into#2677. No second loss PR is planned. All identified stale doc lifecycle/recovery descriptions and source comments are corrected, residency assertions are added, and the ScopeStats ordinary proof gap is recorded separately with explicit scope in the PR. The final combined head must receive a new independent review and CI; first-head approval is not claimed.

## Combined delivery: d9808101e

Single product commit,10files +727/-90: stop-proof consumption, conditional/entry-boundary host stop, ordinary loss completeness, real boundary/manifest regressions and corresponding documentation. The two implementation branches form one error-close behavior; no separate loss PR remains. Eight final-source close/read-failure targets pass with applicable hooks. Full C++/Python rebuilds, actual Claude resumed re-review and final-head CI are in progress; first-head validation is retained independently.

The related ordinary ScopeStats proof gap is recorded in KNOWN_ISSUES and the parent tracker, explicitly outside this args-dump PR. No hardware race or AIV root cause is established by that static finding.

## Final-head local validation

At d9808101e, full CPU C++390/390 passed in47.45s; Python3525 passed/192 skipped in184.42s after rebuilding at the fixed HEAD. Axis check and strict docs build pass. Final-head A3 task-submit (exclusive device14) passed public args-dump4 cases plus TMR full and HBG full; source/runtime16-file hashes are identical before and after. No A5 local silicon claim or injected hardware hang. Actual Claude re-review and final-head CI remain pending; no full acceptance yet.

GitHub comment4348885168 was replied to with the concrete conditional drain/entry guard and red/green evidence, then resolved. First-head Claude review is preserved separately and is not approval of the expanded final head.

## Second independent review and focused rework

Claude reviewed the entire d9808101e expanded scope and confirmed proof/stop memory ordering and retained compatibility, but found a real host drain-drop counter carryover after unknown close, a multi-lane recovery retry duplication, and documentation exceptions/drift. The parent accepts the carryover and requires the host-only count consumed in its own failed-run boundary. A new real boundary/next-run regression fails on d980. Parallel multi-lane tests reproduce duplicate metadata/payload despite a non-additive counter; that child is correcting successful held-buffer identity bookkeeping per run. The no-record/no-manifest convention is preserved and explicitly documented, rather than extending this fix into a new empty-artifact API. Canonical manifest keys, teardown comment and diagram alignment are corrected. These changes require a new fixed-head qualification; d980 CI/local results remain its own evidence.

## Accepted parallel rework and current head: a547efa3d

- Parent real boundary+clean-successor regression proves d980 leaves a host drain-loss counter pending and taints the next manifest. Unknown close now consumes that host-only counter after stopping its readers.
- Child commit523ee7a (based exactly on d980) reproduces two-lane duplicate recovery: old artifacts contain5 metadata records/20 bytes instead of2 records/8 bytes, although the drop counter remains10. The parent inspected/accepted run-scoped lane+buffer-address/sequence bookkeeping, lifecycle resets and both failure-lane orders. Child focused128cases and adjacent68cases pass; parent combined8targets pass, including the host-counter regression.
- Explicit no-record/no-manifest policy is retained and documented; absence does not prove zero loss. Example JSON/key list, teardown contract, diagram alignment and quiesce/residency comments are corrected.
- Integrated and pushed as a547efa3d, still one product commit11files +947/-102. No independent loss/retry PR. Final-head Python3525/192, strict docs and A3 targeted task-submit pass; final CPP, actual Claude re-review and CI are in progress. Earlier head results are not transferred.

Final-head local completion: CPP390/390 in46.95s, Python3525/192 in195.30s, axis and strict docs pass. Final-head task-submit A3 public4cases plus TMR/HBG full scenes pass;17 source/runtime hashes unchanged. The two resolved ordinary args-dump KNOWN_ISSUES entries are removed; ScopeStats, output-root collision, host scheduling observation and unclassified AIV remain. Final actualClaude/CI still pending.

## Code approval and final documentation delta

Actual Claude approved the complete implementation at a547efa3d after independently rechecking recovery identity/lifecycle, stop ordering, counter consumption, proof disjunction and retained compatibility. Its remaining Should was the §7.4 summary matrix. Final154c77af6 adds exactly2lines/removes1 in that doc: record-loss confidence in the buffer-full row and the unproven-close/no-artifact row. All other tracked files, including source/tests/build/CI, are byte-identical to a547. Docs hooks and strict build pass. No a547 runtime result is relabelled as a154c77 execution; a final doc/equivalence verification and final-head CI are pending.

## Final independent review acceptance

Actual Claude approved a547 implementation, then independently verified154c77 changes only the summary matrix and resolves the last Should. Final verdictapprove, all Must/Should closed. Optional Consider items remain explicitly dispositioned, not silently described as fixed. See review-disposition.md and per-head independent reports. Final154c77 CI still pending; product worktree clean and fixed.

## Final acceptance: 154c77af6

The exact-final-head checks are complete: **18 successful check-runs plus the successful CodeRabbit commit status, with one expected deploy skip**. CI run [39796063157](https://github.com/hw-native-sys/simpler/actions/runs/39796063157), attempt1, includes both onboard architectures, four sim jobs, both hardware UT jobs, both host UT jobs, profiling-flags smoke, packaging and A3 network/deepseek. The outer build/deploy run is39796063173. CodeRabbit's incremental review was skipped: its status is not counted as an independent final code review. Actual Claude separately approved implementation a547 and independently verified/approved final154c77's doc-only delta. The one actionable inline thread is replied/resolved. PR2677 is non-draft and ready for user review, not merged or auto-approved on GitHub.

Runtime validations remain attached to a547: CPP390/390, Python3525/192, axis/strictdocs and task-submit A3 public4+TMR/HBGfull,17 hashes unchanged. Final154c77 has exact-head CI and documentation checks; all other tracked files equal a547. Earlier pending statements above are chronological checkpoints, superseded by this acceptance. The parent inspected and accepted each child result before integration. ScopeStats proof-aware ordinary close is the next bounded functional scope, not implemented here; I1 queue integration must align with existing PyPTO#3100 ownership.
