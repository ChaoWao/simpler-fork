# Chip-child communicator control state — PR #2593

2026-10-15. Base `a8c682a3217593d8ad7562c0a6ef1a03d61d6670` (#2589 merged, includes #2579).
[PR #2593](https://github.com/hw-native-sys/simpler/pull/2593), head `149c8997633d15998055e409822260df90de32d2`.
Four product/test/doc files, +194/−32. Planning and evidence stay outside the code PR.

## Scope and contract

The serve loop keeps the opaque base-communicator handle returned by successful CTRL_COMM_INIT. ALLOC/RELEASE consume it explicitly. Native ChipWorker still owns base_comm_handle_, communicator sessions, caching and teardown; code inspection confirms comm_init returns the cached value and native finalization destroys sessions. No physical ownership moves to Python.

A failed or zero-handle initialization publishes no new control value. A cold loop refuses allocation/release before calling native methods; a failed reinitialization retains the previously published value. Synthetic failure tests prove this control behavior, not native rollback recovery. Child still constructs the public compatibility wrapper. Its remaining native calls and shared materialization remain K2b work; this PR does not open public borrowed eager.

## Validation

| Check | Result | Evidence |
| --- | --- | --- |
| New control cases before/after | 5 failed against dynamic-attribute coupling; 5 passed after | [red](../../LOCAL-EVIDENCE.md#artifact-2519d3f65c8e), [green](../../LOCAL-EVIDENCE.md#artifact-b7a62adc362f) |
| Full Python UT (Linux, initial head) | 3275 passed, 91 skipped | [log](../../LOCAL-EVIDENCE.md#artifact-c9dfe79fe95f) |
| Focused host/control suites | 253 passed, including cold-loop isolation | [log](../../LOCAL-EVIDENCE.md#artifact-c0554c48c89f) |
| A2/A3 simulation | 4 communication scene items passed | [log](../../LOCAL-EVIDENCE.md#artifact-2e1ef5364794) |
| A5 simulation | 3 communication scene items passed | [log](../../LOCAL-EVIDENCE.md#artifact-0f63c78ec0c1) |
| A2/A3 hardware | 5 communication scenes on three devices | [log](../../LOCAL-EVIDENCE.md#artifact-25e661e6321a) |
| HBG hardware | 1 domain-cleanup/FIFO scene passed | [log](../../LOCAL-EVIDENCE.md#artifact-d574458de2a1) |
| All changed-file hooks | Passed | [log](../../LOCAL-EVIDENCE.md#artifact-4b8d79c7a14d) |
| Final committed head | Rebuilt; 253 focused tests passed | [build](../../LOCAL-EVIDENCE.md#artifact-bc28526ab2a0), [tests](../../LOCAL-EVIDENCE.md#artifact-8df2cb816793) |

Hardware uses task-submit: task_20261015_040311_415546613094 (devices 3/4/5) and task_20261015_040855_30193917151 (device 0). Separate Ascend logs and independent occupancy samples are under caller/ and hbg/. The initial three-device task waited while another queued task owned all 16 devices; no unlocked device use occurred. No AIV or unexpected device failure occurred.

The communication scenes exercise allreduce, rank mapping, overlapping domains, async and deferred notifications. The HBG case proves that domain allocation/release keeps the cleanup/FIFO rule. These are targeted cases, not a full hardware sweep. A5 is simulation only on this A2/A3 host.

The new host-only tests invoke actual request parsing and control handlers with real POSIX shared-memory payloads. The runtime double accepts only its declared interface and refuses dynamically attached control fields. Existing native-allocation commit tests retain their overflow and result-conversion-failure checks with an explicit handle. Full Python and scene tests ran before commit; the final head is rebuilt and focused suites rerun. No local current full C++ suite result is claimed for this Python-only change.

## Next

Review/CI for this PR, then move child execution from the compatibility wrapper to the existing native binding and consolidate materialization in bounded changes. K2c public borrowed eager still precedes capture/replay. Historical #2589 evidence remains pinned to its own heads.

## Review revision: shared-memory sizing (2026-10-15)

Current revision `25be88ec68d65960bb974ecea4d75c68c42bdacd`, rebased onto main `cc6c4d51` (three commits after the original base). The API source inventory remains pinned to `a8c682a32`; rebase does not relabel that audit.

The original macOS CI run 38725582799 fails five new control cases at the unbounded init-request payload assignment. Shared-memory capacity may exceed the requested payload length. Ubuntu's Python stage passes 3265 tests; its later C++ build is canceled by matrix fail-fast, not an independently identified UT failure. These facts were checked against both raw job logs.

The init payload write uses an explicit end offset. The five control scenarios now each run with native-sized and 16 KiB-rounded real shared-memory allocations, exercising the real request handlers. On Linux before the fix, the rounded cases reproduce the exact ValueError (5 failed / 5 passed). After the fix, the focused suite passes 258 cases. The domain-control diagnostic also names successful CTRL_COMM_INIT, avoiding the obsolete bootstrap_context name and allocation-only prefix. No protocol, native ownership or admission rule changes.

Local validation is Linux-only. Actual macOS coverage must come from the new CI revision; the initial hardware/simulator evidence above belongs to `149c8997`, not this revision. Raw revision logs remain local.

Final committed revision rebuilt successfully; full Linux Python UT: **3283 passed, 91 skipped**, one existing PyTorch read-only-buffer warning, 142.99 seconds. All four PR files pass hooks after rebase. New CI run 38738767777 has passed pre-commit; macOS UT was queued at the last query. This is not an all-green CI claim.

Merged as main `3351950f4b0fdbd60523ea63266b321ab12f5f2e` (#2593). Follow-up child native access is tracked separately in [#2595 evidence](../chip-child-native/REPORT.md).
