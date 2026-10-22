# Ordinary args-dump stop-proof close

- Base: `fa6528f4e45c42242e2162c2ba0192d09cbd3b88` (#2671 merge).
- Product PR: [#2677](https://github.com/hw-native-sys/simpler/pull/2677), integrated head `d9808101e9b4080bcbd3752f913794b41e1b8f39`. First-head evidence below belongs to `19ed2e462`.
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
