# Ordinary args-dump multi-lane recovery retry candidate

Status: local single-commit candidate for parent integration into #2677.
No push, PR, GitHub operation, onboard use or delegation was performed.

## Fingerprints and scope

- Exact base: d9808101e9b4080bcbd3752f913794b41e1b8f39.
- Commit: 523ee7a6655f2ea956fa3605ffd6b35d40ff721c.
- Branch: fix/args-dump-multilane-recovery.
- Worktree: simpler/.claude/worktrees/args-dump-loss-accounting.
- Original loss branch/fingerprint preserved: fix/args-dump-loss-accounting,
  21537980b33b6b6d0490b379549f2e83a3370bc1.
- Current branch has exactly one commit after the specified base; tracked status
  is clean. The candidate changes three files (174 insertions, 3 deletions).

Read current AGENTS.md/CLAUDE.md, relevant rules and testing/commit workflows;
the rule/workflow files are unchanged from the previously loaded candidate.
Changes are limited to args_dump_collector.h/.cpp recovery bookkeeping and
common/platform/test_args_dump_read_failure.cpp. No changes to close_ordinary_run,
ProfilerBase, docs, CMake, retained protocol or ordinary-close tests. The parent's
separate fix for consuming skipped host drain-drop tallies remains independent.

## Defect and mechanism

The previous replacement counter snapshot was idempotent, but recovery itself was
not: when lane 0's held metadata/payload was recovered and lane 1's held-buffer
read failed, a second reconciliation recovered lane 0 again as well as lane 1.
This duplicated real metadata records and args.bin bytes while dropped_records
still reported the correct replacement sum.

The fix keeps one run-scoped successful held-buffer identity per lane, comprising
the device pointer and current buffer sequence. A subsequent reconciliation still
reads and sums the lane's current loss counter, then skips recovery when that
held identity is already represented in host-owned records/payloads. A failed
copy never sets its receipt. The receipt is recorded only after the real buffer
callback returns. It is reset by initialize, begin_run, both export-reset paths,
and finalize. The vector is sized to the actual number of dump threads. No
producer pointer, sequence or wire field is modified.

The existing precondition is load-bearing: device producers are stopped and host
transport is quiescent before reconcile_counters. That fixes a lane's held pointer
and sequence during retry. The cache is not an incremental live-buffer reader and
it does not claim to support producers modifying a held buffer between calls.

## Exact-base red evidence (before production changes)

Only new test/fixture code was added to the specified d980 base, then built:

  cmake --build tests/ut/cpp/build --target test_a2a3_tmr_args_dump_read_failure -j 4
  tests/ut/cpp/build/test_a2a3_tmr_args_dump_read_failure \
    --gtest_filter='*ArgsDumpMultiLaneReadFailure.PerLaneDroppedCountsAreSummed*:*ArgsDumpMultiLaneReadFailure.RetryRecoversEachHeldBufferExactlyOnce*'

Result: four selected parameter cases ran. The two 3+7=10 positive controls passed;
the two partial-read/retry cases failed (failing-lane order swapped). After the
initial partial recovery and two retries, the old real manifest contained 5 args
instead of 2; successful lane metadata appeared 3 times and the initially failed
lane 2 times. The old args.bin contained 20 bytes instead of exactly two uint32_t
payloads (8 bytes). dropped_records was still 10, isolating data duplication from
counter arithmetic.

- Raw failure/manifest/binary evidence: LOCAL_PATH
- Exact-base test build: LOCAL_PATH
- This failing test was run before production implementation edits.

## Green focused matrix

Project-local .venv with --system-site-packages; direct C++ test compilation.
Maximum build concurrency was -j 4 and test concurrency -j 2. Replay:

  cmake -B tests/ut/cpp/build -S tests/ut/cpp
  cmake --build tests/ut/cpp/build --target \
    test_{a2a3,a5}_{tmr,hbg}_args_dump_read_failure -j 4
  ctest --test-dir tests/ut/cpp/build -R 'args_dump_read_failure$' \
    --output-on-failure -j 2

Result: 4/4 binaries pass, 32 GoogleTest cases each (128 total), 2.73 s ctest wall
time. Both archs use separate device/host shadows in these transport tests.

- Configure: LOCAL_PATH
- Build: LOCAL_PATH
- Summary: LOCAL_PATH
- Full per-case output: LOCAL_PATH

The six new two-lane tests each run with Lane0/Lane1 failure/unknown/saturation
ordering, using the real device record function, real held tensor buffers, real
host recovery and actual manifest/bin output:

1. PerLaneDroppedCountsAreSummed: lane counters 3 and 7 produce 10/true, two
   metadata entries and exact payload bytes.
2. RetryRecoversEachHeldBufferExactlyOnce: one held read fails; after failure
   clears, two retry calls leave exactly one metadata entry per lane and exactly
   8 payload bytes, while loss count remains 10/true. Lane0-success/Lane1-fail and
   the reverse are both exercised.
3. UnknownLanePreservesOtherLaneKnownLossCount: one lane has an unattributable
   prior epoch; the other lane's known count 7 remains exported with false.
4. UnreadHeldBufferKeepsKnownCounterSumIncomplete: both device counters are
   readable while one held copy fails; manifest keeps sum 10 and reports false.
5. SaturatedLaneKeepsCombinedCountCappedAndIncomplete: one lane is UINT32_MAX and
   another 7; the existing uint32 host cap remains UINT32_MAX with false, never
   wraps. This follows the parent's explicit instruction to retain the cap
   contract rather than widen a host or wire field.
6. NewRunRecoversReusedHeldBuffers: both device held addresses are physically
   reused by the next run, and the next run still exports its two new payloads.

The 20 existing single-lane parameter cases remain passing in each binary,
including stale-shadow guards, complete/incomplete confidence, counter retry,
retired transport uncertainty and saturation.

## Neighboring regressions and checks

  cmake --build tests/ut/cpp/build --target \
    test_{a2a3,a5}_{tmr,hbg}_args_dump_{collector,retained_runs} -j 4
  ctest --test-dir tests/ut/cpp/build -R 'args_dump_(collector|retained_runs)$' \
    --output-on-failure -j 2

Result: 8/8 binaries pass (4 collector cases x4 plus 13 retained cases x4 = 68
GoogleTest cases), 3.75 s ctest wall time. Together with focused testing, 196
GoogleTest cases passed across 12 binaries.

- Neighbor build: LOCAL_PATH
- Summary: LOCAL_PATH
- Per-case output: LOCAL_PATH

Complete applicable pre-commit hooks passed on all three changed files, including
clang-tidy, clang-format, cpplint, headers and repository checks. Raw:
LOCAL_PATH git diff --check and staged checks passed.

## Limits

This is a local candidate pending parent integration and acceptance. It is based
on d980, not the parent's still-pending host drain-drop consumption follow-up.
The parent must run its combined checks after integrating both independent fixes.

Tests use synchronous real producers with deterministic injected metadata-copy
failure and read actual exported files; no NPU or DMA fault was exercised. No
broad Python or scene sweep was run. Existing payload/file-integrity exclusions
and uint32 loss-counter saturation semantics remain in force. Bookkeeping is
owned by the closing thread and assumes the existing stopped-producer and
transport-quiescence contract; concurrent reconciliation or live-buffer mutation
is outside that contract.

The worktree's ignored KNOWN_ISSUES.md still contains the previously recorded
inherited optional system-profiler dependency warning. It did not affect these
C++ checks. No new unrelated defect or environment change was introduced.
