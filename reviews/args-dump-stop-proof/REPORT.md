# Ordinary args-dump stop-proof close

- Base: `fa6528f4e45c42242e2162c2ba0192d09cbd3b88` (#2671 merge).
- Product PR: [#2677](https://github.com/hw-native-sys/simpler/pull/2677), head `19ed2e4625703cad9096d8c9407b516d115e8ef8`.
- Parent tracker: #2666. This is one bounded C1 correctness fix, not capture DFX qualification.

## Behavior

The real ordinary runner boundary consumed neither of its stop proofs. Unknown completion could recover a held unpublished record, export with host readers live, and use the recovered write to credit a later uncollected publication.

The common collector close now permits recovery after execution completion OR independent producer termination. Without proof it joins host transport/readers, skips held recovery and exports only published host-owned records. Host thread exit supplies no device-free authority. Successful ordinary resident collection and retained settlement preserve their paths.

## Parallel execution and acceptance

One parent owns production changes and final integration. A separate child wrote real-runner/AICPU-producer tests on the exact base. Parent inspected the commit and raw red matrix, then integrated only the accepted tests into the same functional PR. A second child independently produced ordinary loss completeness as a follow-up; it is not mixed into this PR.

## Validation

- Four arch/runtime binaries, three cases each: completion and joined-producer controls pass on the original base; unknown completion fails reader retirement, held payload/manifest exclusion and later arena credit. With the fix all pass.
- Full CPU C++ suite: 390/390 programs passed.
- Applicable pre-commit: passed, including clang-tidy, cpplint and markdown checks.
- Initial Python run crossed a commit identity change: four subprocesses correctly rejected an extension stamped with the previous HEAD. Fixed-HEAD rebuild and rerun pass: 3525 passed / 192 skipped in 191.78 s. The initial failed run remains recorded and is not counted green.
- A3 task-submit task_20261022_062351_354754418141 passed: public owned/borrowed args-dump matrix (4 cases), TMR full and HBG full scenes. Before/after source/runtime hashes match; raw logs, persistent basetemp/plog and occupancy snapshots are retained locally. Actual Claude CLI independent review and final-head CI remain running. No hardware hang injection is performed.

## Limits

The close does not establish a bounded device-stop guarantee, alter device release/abandonment, or add capture support. Epoch/read checks do not prove quiescence. Explicit unknown loss representation is a separate follow-up. The CPU regression observes effects and actual thread state, not every individual memory read.
