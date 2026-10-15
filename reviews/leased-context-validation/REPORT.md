# Common operation-lease validation — PR #2584

2026-10-15. Main baseline `7e19db4df679278d8bc4f9eea06c8e8fda4a492e` (#2582 merged).
PR [#2584](https://github.com/hw-native-sys/simpler/pull/2584) head `165939a1d8eeb31a303c3fd73af03e5f9c0f6ca3`.
Code PR: seven files, +211/−15. Planning HTML, source inventory and this evidence stay outside the repository.

## Review correction

#2582's individual submit/registry checks did not protect other leased APIs or future additions. The review correctly identified `_operation_lease` as the convergence point. It now reserves the temporary lease under the lifecycle CV, releases that lock, validates the native chip context, and only then yields into the operation body. Failure unwinds the lease through the existing finally. The native tree remains pinned during validation.

Worker's direct submit/register/unregister checks are removed. Its only explicit other validation site is close's separate pre-claim boundary. Helper docstring states refusal-before-publication and correct-context retry. `device_location` explicitly opts out as a source-only descriptor query while retaining its lifecycle lease. New leased APIs default to validation, without a name whitelist. L3+ parents retain their own child/environment contract; they do not validate a local caller RTS context.

Direct native malloc/free/copy_to/copy_from/device_memory_info/flush_diagnostics now reject before backend entry. The shared predicate is const for the read-only device query. Cached native host counters, addresses and workspace/teardown observations remain non-device queries. Raw C ABI implementations and legacy HCCL borrowed capability qualification are unchanged; this PR does not expose public borrowed initialization or qualify every low-level embedding API.

## Evidence

| Check | Result | Log |
| --- | --- | --- |
| Before fix | Seven failed: six native memory/query/flush methods plus an unlisted leased API | [red](../../LOCAL-EVIDENCE.md#artifact-13834c2d00e2) |
| Focused | 13 passed: native/public memory refusal, restored context, unlisted API default, descriptor exemption, lease unwind, concurrent lifecycle-lock availability | [focused](../../LOCAL-EVIDENCE.md#artifact-f49767134c4d) |
| Python UT | 3235 passed, 91 skipped | [Python](../../LOCAL-EVIDENCE.md#artifact-b347ddeef668) |
| C++ UT | 317 targets passed | [C++](../../LOCAL-EVIDENCE.md#artifact-c5452a00c728) |
| L2 Program simulation | A2/A3 six and A5 three cases passed, manual cases included | [A3](../../LOCAL-EVIDENCE.md#artifact-e9eac13740e4), [A5](../../LOCAL-EVIDENCE.md#artifact-0d79c51e5517) |
| Hardware | 44 passed, 27 deselected, 171.06s under task-submit, device 2 | [hardware](../../LOCAL-EVIDENCE.md#artifact-98e8110de75e) |
| Final committed head | Rebuilt extension; 207 related tests passed | [build](../../LOCAL-EVIDENCE.md#artifact-47eb70a52421), [tests](../../LOCAL-EVIDENCE.md#artifact-f2b0c4071594) |
| Changed-file hooks | All passed | [hooks](../../LOCAL-EVIDENCE.md#artifact-5692483316ec) |

The DSO memory script uses host-backed storage and real byte copies; tests exercise the actual C++ predicate/binding without invoking an NPU. Initial public-memory assertions incorrectly treated the allocation registry as iterable; corrected to lookup/identity/length checks, then the complete suite reran green. No product change was needed for that test defect.

Hardware adds wrong-context native memory/query/flush checks using valid existing allocations and host buffers, then restores context and verifies caller outputs. Both runtimes, owned/borrowed depths 1/2 remain covered. Borrowed runs are bridged into Python handles; this is not the full public borrowed Worker qualification. Full matrix ran on the pre-commit tree with the same implementation, followed by final-head rebuild and related tests.

Architecture prechecked; every NPU run through task-submit, with continuous [occupancy sampling](../../LOCAL-EVIDENCE.md#artifact-236c4c9e8cbe) and independent [device logs](../../LOCAL-EVIDENCE.md#artifact-3adacf4fd736). No intermittent AIV/A5 hang fix is claimed.

## Roadmap

Accept this structural closure before advancing K2b environment identity/generation/source and internal adapter consolidation. K2c still must expose and qualify the complete public borrowed eager path; graph/capture/module lifetime remains later. Historical #2582 and #2581 evidence retain their own heads.
