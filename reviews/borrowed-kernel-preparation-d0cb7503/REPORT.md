# Borrowed kernel preparation: local validation

- Branch: `feat/borrowed-kernel-preparation`; commit `d0cb7503`; base `121d807bb` (#2501).
- Scope: shared per-invocation preparation and finalize on borrowed onboard devices, both TMR and HBG. Launch remains rejected and supported remains 0.
- Submitted as draft [PR #2507](https://github.com/hw-native-sys/simpler/pull/2507), rebased head `2ac0d906` on `f16b6bb42`. Planning/probe files stay outside the repository. Results below are the original revision unless stated otherwise.

## Passed

- 294 no-hardware C++ targets.
- 2909 Python UT, 59 skipped.
- A2/A3 sim and A5 sim full sweeps (exit 0); no A5 hardware validation.
- 29 A2/A3 kernel lifecycle C ABI cases. Borrowed preparation covers both runtimes, fresh HBG HOST/NONE effects (11 -> 18 -> 25), DEVICE pointer opacity, transfer refusal before effects, failed metadata-copy cleanup, wrong-current-device retry and ownership API guards.
- Pre-commit hooks.

## Full onboard sweep is not clean

Four resource-phase HBG cases failed in the full four-device Program sweep:

| Case | Child PID | Device |
| --- | --- | --- |
| early_enqueue depth-one control | 1366490 | 8 |
| early_enqueue device-result chain | 1366562 | 6 |
| dep_gen across runs | 1373478 | 5 |
| early_enqueue sustained refill | 1386417 | 7 |

All four preserved host plogs report `VEC instruction error: the ub address out of bounds` before the scheduler timeout. This is not classified as an ordinary slow kernel. Both L2 runtime batches in the full sweep passed.

The depth-one case passed alone on the merged #2501 implementation (`f632d25a`) and this branch, on the same device. All four cases subsequently passed as a four-device batch on both baseline and branch within one task, baseline first. These reruns neither establish a root cause nor prove the fault fixed. The branch is not recorded as having a clean full onboard sweep.

## Test fixture maintenance

The existing fault shim had a stale five-argument host-log callback; current ABI has four. After fixing it, four Program teardown tests expected reinitialization of an Unproven context. The baseline reproduces the refusal; #2473's context lifecycle gate requires it. Tests now assert refusal and no stale loader unload during destruction. No teardown production policy changed.

New shim forwarding resolves symbols from the loaded component handle because its RTLD_LOCAL ACL dependencies are absent from the preload shim's RTLD_NEXT scope.

## Reproduction follow-up

At the same revision, a second full four-device sweep passed (430.42 s), followed by 10 fresh four-case rounds, 40/40 passed with no VEC UB bounds signature. The original fault remains unresolved; this is not a fix. See [reproduction evidence](../../probes/hbg-aiv-sweep-repro-20261008/REPORT.md).

## Rebased head validation

`2ac0d906`: local rebuild, pre-commit, 294 C++ UT, 54 related Python UT (34 skipped), and 29 A2/A3 lifecycle cases (17 deselected) passed. Full sweeps above were not repeated on this head.
