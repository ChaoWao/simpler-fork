# Empty eager completion and persistent state

Baseline: main `905d336e41d324ba8c8dbf87fe138965d84e74e2` (#2599 merged). PR [#2604](https://github.com/hw-native-sys/simpler/pull/2604), head `1f97d9f88878f06651df01f94e5b73497e583095`, one commit on the stated baseline. Eight product files only: four three-line production changes, real cold-path tests, and the shared public stateful test. Planning documents and logs are excluded from the product PR. This qualifies K2c; it does not deliver capture or repeated PreparedCall consumption.

## Contract and finding

A completed orchestration may submit zero tasks. TMR must acquire orchestration completion before comparing the final task count; HBG dispatch starts only after initialization publishes the final host-built count. A final count of zero completes normally. Both runtime/architecture cold paths currently require a positive count, so a legitimate empty call never reaches normal completion. A5 HBG also has a dedicated empty lifecycle path; its scheduler check still has the same unnecessary restriction.

The real cold-path UTs fail on zero and pass the nonempty control before the fix. Removing the positive-count restriction preserves the orchestration publication barrier and waits for the last task on nonempty graphs. HBG tests cover both arches; the TMR peer test covers the A5 target. Full C++ UT passed all 321 targets after the final production changes.

## Public parameter/state acceptance

One public DEVICE INOUT Buffer retains its identity across empty → write → empty → write → empty, checking every unchanged sentinel word. Scalars select execution and write values. HBG additionally reads an explicit HOST/NONE control Tensor; modifying the host source after successful submit must not change that invocation. Onboard caller H2D precedes submit and caller D2H follows it; caller-stream synchronization precedes RunHandle.wait. Borrowed and owned execution use the same Worker/Buffer/Tensor/TaskArgs types.

The initial A3 board run failed: device 0 rejected prepare with halMemCtl EACCES, and HBG zero calls timed out on other devices. An exclusive task on 0/8 ran the pre-existing nonempty public test: 0 failed identically, 8 passed. New TMR owned and borrowed zero/active sequences both passed on 8. After the HBG guard fix, HBG owned scalar and HOST-control sequences passed; borrowed HBG reached the second invocation and correctly refused an arena growth from 6765760 to 6765952 bytes. That is a capacity boundary, not a completion timeout.

Borrowed HBG currently commits its first compact arena and refuses growth thereafter. Its acceptance sequence must first establish sufficient capacity using a nonempty state-preserving call. Empty-first owned HBG and TMR remain separate tests. The first-empty-commit capacity gap is recorded for follow-up; it is not solved by changing completion.

## Validation status and honest limits

- Real UT red/green logs exist for TMR and both HBG schedulers.
- A5 sim targeted: three owned combinations passed, three borrowed combinations explicitly skipped because they require onboard RTS.
- Initial runtime-filtered sim sweeps both failed two fatal-log checks. A two-case isolated rerun passed, but the complete fatal module reproduced missing log records on the same head. A clean baseline worktree at `905d336e4`, rebuilt with the same supported nanobind 2.15.0, reproduces exactly the same two failures (2 failed / 1 passed); this serial module failure predates the fix. Those sweeps are not recorded as passes.
- Full CI-shaped dispatch sweeps for both simulators completed with exit 0: A3 61 Resource jobs plus both L2 groups; A5 57 Resource jobs plus both L2 groups. These counts include capability skips; they are not test-pass totals.
- All six new A3 hardware combinations pass after the documented borrowed-HBG capacity warmup. Full ordinary A3 hardware regression completed with exit 0 in 741.17 s under task `task_20261016_023058_151505213634`, one lock for devices 8–11. Both runtime L2 groups and all Resource jobs passed; the new six cases also pass again inside the sweep. All four locks were released. Changed-file formatting/type checks pass; final C++ suite passed all 321 targets (12.65 s).
- A5 hardware is unavailable locally; CI must supply that qualification. No AIV root-cause fix is claimed.

Local logs remain outside the product PR and are excluded from the fork's design backup.

## Local evidence index

[Real TMR red](../../LOCAL-EVIDENCE.md#artifact-c86f3ea6d7a9), [TMR green](../../LOCAL-EVIDENCE.md#artifact-7cf3584d9374), [real HBG red](../../LOCAL-EVIDENCE.md#artifact-5c0d6166a172), [HBG green](../../LOCAL-EVIDENCE.md#artifact-8be6f7a3550b), [full C++](../../LOCAL-EVIDENCE.md#artifact-016c7c371646), [hooks](../../LOCAL-EVIDENCE.md#artifact-f820c6692011), [A3 dispatched sim](../../LOCAL-EVIDENCE.md#artifact-ef7d3c6a5723), [A5 dispatched sim](../../LOCAL-EVIDENCE.md#artifact-8f9da0ba1daa), [A3 serial failure](../../LOCAL-EVIDENCE.md#artifact-6d214f30b46a), [A5 serial failure](../../LOCAL-EVIDENCE.md#artifact-799eee46fd3d), [isolated fatal cases pass](../../LOCAL-EVIDENCE.md#artifact-d65625d96810), [clean baseline module failure](../../LOCAL-EVIDENCE.md#artifact-e5a285de5b34), [initial board failures](../../LOCAL-EVIDENCE.md#artifact-057608ae8282), [device control](../../LOCAL-EVIDENCE.md#artifact-3f708cfabb83), [capacity boundary](../../LOCAL-EVIDENCE.md#artifact-a1821f02dcb4).

Final [full hardware sweep](../../LOCAL-EVIDENCE.md#artifact-74ad272e9c26) and [A5 sim stateful rerun](../../LOCAL-EVIDENCE.md#artifact-d0132f55fca6). No new AIV incident was observed in this sweep; this is not a claim that the previously recorded intermittent AIV issue is fixed.

## Final commit and next gate

The committed head rebuilt successfully. Final-head focused A3 sim completed with exit 0 (three owned cases pass, three borrowed cases skip). The full sweeps above ran before commit with identical product content; they are not represented as a second final-head hardware run. [Final focused log](../../LOCAL-EVIDENCE.md#artifact-d5b95a435af5). PR CI is pending; no A5 hardware pass or merge is claimed. K2c remains open for actual framework emission ordering and the separately tracked fault/DFX/business-state matrix. C1 capture and S3/C2 ownership remain subsequent work.
