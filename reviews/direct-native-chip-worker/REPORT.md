# Direct L2 native chip ownership — PR #2589

2026-10-15. Base `11eb5d2c9ef8fe5a484a3d7c686d408ae5ff3e3b` (#2587 merged).
[PR #2589](https://github.com/hw-native-sys/simpler/pull/2589), head `1a113d50662dab2e720dddc47a70f7cb9296a331`.
14 product/test/doc files, +287/−99. Planning and evidence stay outside the code repository.

## Contract

Worker already owns its callable registry, argument imports, source uses, operation leases and close journal. Direct L2 now owns `_ChipWorker` directly; no public Python ChipWorker registry/lock sits in between. The existing native lane and completion contracts remain unchanged. Shared `_initialize_chip_runtime` handles logging, optional binary paths and exact native init-option forwarding.

The public compatibility ChipWorker and forked chip child retain their wrapper. Child communication cache/internal access and shared materialization remain K2b work; public borrowed init/caller submit is K2c. This PR does not complete K2b or qualify public kernel eager.

## Validation

| Check | Result | Evidence |
| --- | --- | --- |
| Python UT | 3270 passed, 91 skipped | [log](../../LOCAL-EVIDENCE.md#artifact-d49186164148) |
| A2/A3 simulation | 15 scheduler items passed | [log](../../LOCAL-EVIDENCE.md#artifact-1ecff554d502) |
| A5 simulation | 11 scheduler items passed, 3 deselected | [log](../../LOCAL-EVIDENCE.md#artifact-052ac8bf52ee) |
| A2/A3 hardware | 17 scheduler scenes + 44 caller/lifecycle tests passed | [log](../../LOCAL-EVIDENCE.md#artifact-30fd47c65ef6) |
| Native ownership and init options | 3 new tests passed | [log](../../LOCAL-EVIDENCE.md#artifact-cba3aeb03a54) |
| Final committed head | Rebuilt, 280 passed / 16 skipped | [build](../../LOCAL-EVIDENCE.md#artifact-451cb974f726), [tests](../../LOCAL-EVIDENCE.md#artifact-a33c42bab2d8) |
| Changed-file hooks | Passed, final fixture docs rechecked | [hooks](../../LOCAL-EVIDENCE.md#artifact-5e724fd0f75f), [docs](../../LOCAL-EVIDENCE.md#artifact-05c23b720ccf) |

Hardware allocation: `task_20261015_004854_289386017235`, device 0, via task-submit. Ascend logs are under [caller/ascend](../../LOCAL-EVIDENCE.md#artifact-d03e5863da08); independent occupancy sampling is [here](../../LOCAL-EVIDENCE.md#artifact-b0d6c0bca624). No AIV occurred. This is targeted validation, not a full hardware sweep. A5 is simulation only on this A2/A3 host.

Native owner coverage uses the actual C++/nanobind `_ChipWorker` and a host-backed stub DSO. Constructing the public wrapper is made fatal. Registration refcounts, memory roundtrip and teardown identity are checked. The borrowed hardware coverage is the existing native caller bridge, not public borrowed qualification.

The broad suites ran on the final product working-tree content before commit; the final commit was rebuilt and focused tests rerun. No C++ product/header/build changes, so no current full C++ suite result is claimed. Initial fixture-shape, global stub-counter and typing failures were fixed without adding product fallback behavior.

## Next

Merge review decides this PR. Then consolidate chip-child native access/communication-cache ownership and shared materialization in bounded PRs; complete K2c before capture/replay. Existing AIV investigation remains independent and unclassified.

## Revision 2 — scene-consumer migration

Head `83200862e3809b1583c1cf6a594722e1d312ba2f`, same base `11eb5d2c9`. Combined PR: 23 files, +417/−223. The original validation above did not exercise all white-box scene consumers; initial CI correctly caught that omission.

Nine `tests/st/` files obtained the real native object through Worker but still called wrapper registration/run/unregistration methods, a differently ordered lease signature, or `_impl`. They now use existing native entries and named slot_id/generation fields. No native compatibility aliases were added. Both concurrent-prepare accesses and the HBG lifecycle prepare signature are covered, rather than stopping at the first AttributeError. A one-off audit checked 182 direct native attribute accesses in scene tests against the actual binding; none are missing. Existing scene tests are the committed regression barrier.

The initializer restores the sim-context registry and dispatcher facts and annotates bins with the existing RuntimeBinaries class under TYPE_CHECKING. There is no runtime dependency/import or new protocol class.

| Check | Result | Evidence |
| --- | --- | --- |
| Exact reported failure | 1 failed before, 1 passed after | [red](../../LOCAL-EVIDENCE.md#artifact-84d761448113), [green](../../LOCAL-EVIDENCE.md#artifact-646279f1e8de) |
| A2/A3 affected sim groups | 19 passed, 8 skipped | [log](../../LOCAL-EVIDENCE.md#artifact-c51dcfc3bcb5) |
| A5 affected sim groups | 14 passed | [log](../../LOCAL-EVIDENCE.md#artifact-22c96cf4477f) |
| A2/A3 affected hardware groups | HBG 20 passed / 1 skipped; TMR 11 passed | [log](../../LOCAL-EVIDENCE.md#artifact-bfb72bdaae37) |
| Related Python UT | 110 passed | [log](../../LOCAL-EVIDENCE.md#artifact-78c62d85b387) |
| Final committed head | Rebuilt; 110 passed | [build](../../LOCAL-EVIDENCE.md#artifact-1529c58b9c3e), [tests](../../LOCAL-EVIDENCE.md#artifact-1f7bc9ae9271) |
| Hooks | Passed after Ruff formatted the extended lease call | [log](../../LOCAL-EVIDENCE.md#artifact-ce499c6856e7) |

Hardware uses task-submit `task_20261015_012247_40805628180`, device 2, with [independent occupancy](../../LOCAL-EVIDENCE.md#artifact-45eb212f16ce) and [Ascend logs](../../LOCAL-EVIDENCE.md#artifact-9b72348d3df9). The missing-entry registration arm intentionally triggers the observed 507018 and passes; no unexpected AIV was observed. Onboard includes all four concurrent-prepare stress arms and native lifecycle. Simulation skips hardware-only paths. Neither this nor the initial run is a full onboard sweep.

Initial CI failure logs and all canceled siblings were read. See [triage](ci-triage.md). The independent macOS host-child exit failure is not fixed or dismissed as flaky; Linux rerun passes and revised-head CI must validate it. CodeRabbit discussion 4280523840 is addressed by this migration; its reply and resolution are recorded on the PR.

### Final revision and validation scope

Final head `27098ab9a168a647b45ed2fc3d9b0e3038ebffe5`. The intermediate 83200862 CI stopped at Pyright because the option-forwarding test still used SimpleNamespace for the newly typed RuntimeBinaries parameter. The local initial hook rerun selected only revision-edited files, not all PR files; rerunning the full 23-file PR hook set reproduced this exact error. The fixture now constructs the actual RuntimeBinaries class, all 23-file hooks pass, and 110 related tests pass.

Scene/sim/hardware evidence above ran on the scene-migration working tree before the amend; the subsequent change only replaces this UT fixture/import. They are validation of that product content, not new board executions on the final SHA. The final SHA is rebuilt and the focused tests rerun; no full Python or hardware sweep is re-attributed to it.

Final-head evidence: [build](../../LOCAL-EVIDENCE.md#artifact-5c9839daad9c), [110 related tests](../../LOCAL-EVIDENCE.md#artifact-ed9c93316455), [all PR-file hooks](../../LOCAL-EVIDENCE.md#artifact-0c4ae882e3b0).

### Updated-head CI checkpoint

[Run 38706012077](https://github.com/hw-native-sys/simpler/actions/runs/38706012077), head 27098ab9, checked at 2026-10-15 08:50 UTC: 16 of 17 CI jobs succeed. All four simulation jobs and both ordinary onboard jobs succeed, as do hardware UT, network, DeepSeek and profiling smoke. macOS Python unit-test step succeeds, so the original orphan-child failure did not recur in this run. The only pending job is macOS UT's C++ build/run step; no remaining red is reported. This checkpoint is not a claim that the pending job passed or that the orphan observation's root cause is fixed.

CodeRabbit thread PRRT_kwDORCBKLM6srUiL was replied to and resolved; [reply](https://github.com/hw-native-sys/simpler/pull/2589#discussion_r4280711180).
