# Shared chip-argument materialization

2026-10-16. Base main `8c3b0e174cb41acb13bcb2d49baeb68b38ad82ba` (#2595 merged, includes #2592). Head `246d21562f0d31980d2e71de8f2c0efcb5fb7da5`, PR #2597.

## Scope and invariants

The private `_materialize_chip_task_args` in `simpler.task_interface` combines descriptor resolution through an explicitly supplied ImportRegistry with the existing C++ `materialize_task_args` POD builder. It accepts only address-free TaskArgs. Direct L2, serial child handling, staged child submission, and the compatibility ChipWorker use the same conversion. It returns a fresh chip-only POD per invocation, preserving view offsets/strides, dtype/address space, transfer requests and scalar words.

The function does not own the registry, retain external allocation ownership, copy tensor data or cache a prepared call. All pre-existing snapshots, callable HOST permissions, source checks/reservations and accepted-use publication stay at their entry points. Direct L2's accepted references still publish before mapping. Child wire arguments are still decoded while the frame is held. The compatibility wrapper still excludes DEVICE arguments and non-contiguous H2D, closes imports after a pre-native materialization failure/success, and retains them after unknown native completion. Direct Worker and child caches keep their previous lifetimes. No mailbox, device ABI, lane ordering or public option changes.

A conversion error may follow an earlier successfully imported descriptor. The shared converter cannot unmap the caller's existing cache: other in-flight consumers may still hold those addresses. The failure test reuses a real POSIX mapping, adds a refused foreign-owner DEVICE descriptor, and proves the prior mapping remains usable until explicit caller close. Removing this property fails the test.

## Validation

| Check | Result | Evidence |
| --- | --- | --- |
| Focused local/compatibility/source suites | 73 passed | [log](../../LOCAL-EVIDENCE.md#artifact-641ae3116460) |
| Full Linux Python UT | 3336 passed, 91 skipped; one existing PyTorch warning | [log](../../LOCAL-EVIDENCE.md#artifact-a4e7104aa842) |
| Six new cases | 6 passed; local/mailbox parity, fresh values, type gate, cache lifetime | [log](../../LOCAL-EVIDENCE.md#artifact-31ce4a41426d) |
| Negative: converter closes imports on every return | 3 failed / 3 passed | [log](../../LOCAL-EVIDENCE.md#artifact-776ee8037d21) |
| Negative: converter closes imports only on error | Failure-lifetime case fails at the retained-mapping check | [log](../../LOCAL-EVIDENCE.md#artifact-b010002fcdcb) |
| A2/A3 simulation | 10 items passed: 9 resource-scheduled + 1 HBG L2 | [log](../../LOCAL-EVIDENCE.md#artifact-62d73493d655) |
| A5 simulation | 6 items passed: 5 resource-scheduled + 1 HBG L2 | [log](../../LOCAL-EVIDENCE.md#artifact-7a2d5ffbc978) |
| A2/A3 hardware | 14 items passed: 13 resource-scheduled + 1 HBG L2 | [log](../../LOCAL-EVIDENCE.md#artifact-36763a034e75) |
| Changed-file hooks | All four files pass | [log](../../LOCAL-EVIDENCE.md#artifact-d47577dd2402) |

The negative controls are subprocess-local monkeypatches; production files are never edited underneath running tests. They do not introduce a production flag or alternate path.

Hardware task `task_20261016_000147_12853944985` holds devices 1/4 through the run. Device logs are isolated at `/tmp/chip-materialization-board/ascend`; no unlocked NPU work is used. Board coverage includes four HBG FIFO items, six TMR FIFO items (including the multirank item), three direct TMR items (vector add, run trace, allocation) and one direct HBG vector item. The sim selections add simulator-only dynamic registration. No AIV or unexpected runtime failure occurred. This is targeted coverage, not a full sweep; local UT is Linux-only and A5 hardware is unavailable on this host.

## Next

After merge/acceptance, K2c connects public Worker borrowed initialization and per-submit caller stream to the existing native path. It must qualify source/context refusals, both runtimes and caller ordering through the real public entry; common internal materialization does not establish that qualification. S3 module ownership remains independently required by C2. Capture/replay is subsequent work.

Final committed head `246d2156` rebuilt successfully; **321 focused tests passed**. Product PR: [#2597](https://github.com/hw-native-sys/simpler/pull/2597). Initial CI is pending; no cross-platform all-green result is claimed.
