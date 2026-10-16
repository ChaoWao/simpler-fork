# Borrowed eager arena growth

Baseline: main `17129ba496c6d7d710b0e1320b3d1c25aeb9d1e1` (#2606). PR [#2608](https://github.com/hw-native-sys/simpler/pull/2608), current head `dbc9bc6c9c8b8adfb046b6b7e57c2fb3355a769e`, branch `fix/borrowed-eager-arena-growth`, 8 product files / +94−128. The production-content full sweep and current-head focused checks passed; CI is pending. #2607 was closed without merge.

## User-visible contract

A borrowed HBG Worker must accept empty → one task → four tasks → empty → eight tasks → two tasks → empty without a nonempty warmup or context recreation. The same DEVICE INOUT allocation retains untouched sentinel words and identity. Scalar and HOST/NONE control paths exercise the real host orchestrator; caller H2D/launch/D2H order is preserved.

The obsolete mode-wide arena freeze blocks this sequence even though capture is refused and native admission protects bank use. Eager follows the common replacement transaction: all replacement allocations stage before publication, failure leaves old addresses, capacity and contents, and a peer bank is unchanged. Shared-bank native overlap still requires compatible storage; per-run HBG banks prepare independently. No new capability query, version, public resource object or executor is introduced.

## Red evidence

Exclusive A3 task `task_20261016_075736_408853220252` on devices 8/9 runs baseline production. Both borrowed HBG paths fail on the first nonempty image after the empty image; owned controls pass. The diagnostic is exactly `kernel mode forbids growing committed region runtime_pool (cached 6765760, requested 6765952)`. Both A3 real-library RTS probes also reject before the expected allocation-failure injection is reached. An earlier probe attempt had an enum-type compilation error and is not counted as red behavior evidence.

## Verification in progress

Four host-backed RTS probes pass (A3/A5 × TMR/HBG). Each tests owned/borrowed growth, failure at all three region allocations, byte-level rollback, smaller-request reuse, peer-bank preservation and final releases. The same probe retains the real caller launch ordering, partial failures and capture-before-publication refusal checks. These are host-backed probes and are not A5 hardware qualification.

Initial A3 task `task_20261016_075953_102603721632` held devices 8–11. All six public stateful cases and eight public caller cases passed. Its full sweep did not pass: committing during the sweep changed HEAD while the extension was stamped with the baseline. One late Resource child and both L2 groups refused collection with the exact source/build-stamp mismatch. This is a validation-process error, not an execution pass or an AIV diagnosis. The task exited 1 and released all locks.

The final committed head has been rebuilt. New exclusive A3 task `task_20261016_081205_212971510705` repeats all four RTS probes, the six public stateful cases, the eight public caller combinations and the complete ordinary sweep. HEAD and product source remain frozen during this run. It completed with exit 0: all six public stateful cases and eight public caller combinations pass again; the full ordinary A3 sweep exits 0 in 573.71 s. Both L2 groups pass (HBG 43 passed / 1 skipped; TMR 60 passed). Devices 8–11 were released. This evidence belongs to production head `a1dc0cb8fe0548462d8622ced953547bbaf44762`. No AIV fix or capture/replay support is claimed.

## CPU and simulator verification

- Full Python CPU UT: 3348 passed / 7 skipped / 108 hardware deselected, 147.59 s.
- Full C++ UT: all 321 targets pass, 11.00 s.
- Full CI-shaped A3 sim: exit 0, 61 Resource jobs plus both L2 groups (14 passed / 7 skipped HBG; 31 passed / 1 skipped TMR).
- Full CI-shaped A5 sim: exit 0, 57 Resource jobs plus both L2 groups. Resource job counts include skips and are not individual test-pass counts.
- Changed-file pre-commit checks pass, including C++ checks, Python checks, pyright and Markdown.

Local evidence: [baseline red](../../LOCAL-EVIDENCE.md#artifact-d3b4eb4327ba), [Python UT](../../LOCAL-EVIDENCE.md#artifact-49470ac462b2), [C++ UT](../../LOCAL-EVIDENCE.md#artifact-5f254ab90ee2), [hooks](../../LOCAL-EVIDENCE.md#artifact-6fe30a8caed1), [A3 sim](../../LOCAL-EVIDENCE.md#artifact-d2130f781ecd), [A5 sim](../../LOCAL-EVIDENCE.md#artifact-41e37c2f59de). No raw artifacts enter the product PR or the design backup branch.

Final-head focused host tests pass: 164 passed / 38 hardware deselected (8.04 s). These ran after rebuilding the committed head and do not substitute for the final hardware sweep.

## Documentation precision and current head

Current head `dbc9bc6c9c8b8adfb046b6b7e57c2fb3355a769e` differs from the fully swept `a1dc0cb8` only in one user-reference sentence: transaction rollback preserves arenas, while public submit errors retain their existing cleanup requirements. The probe's successful retry is an arena-transaction guarantee, not a claim that public submission errors can always be retried immediately. No production or test implementation changed. The amended head was rebuilt; final focused host checks pass (164 / 38 hardware deselected, 7.82 s), and the exclusive A3 six-case stateful rerun passes with exit 0 (9.73 s). The full sweep above remains attributed to the identical production content at `a1dc0cb8`. CI is pending.

[Successful full A3 run](../../LOCAL-EVIDENCE.md#artifact-60f2fa56285d), [initial stamp-mismatch sweep](../../LOCAL-EVIDENCE.md#artifact-e4371e151cff), [final focused checks](../../LOCAL-EVIDENCE.md#artifact-0b7622b279b0).

[Current-head A3 stateful rerun](../../LOCAL-EVIDENCE.md#artifact-38ba90682acb). All local tasks have exited and released their device locks. No A5 hardware run is claimed.
