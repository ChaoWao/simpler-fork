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
