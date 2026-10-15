# State publication preflight — PR #2582

Main baseline: `620807d2dd2dad83e8529788d7998a77b7c14e2d`, includes #2581.
PR [#2582](https://github.com/hw-native-sys/simpler/pull/2582): `f0f6e700d7653c30f38241fd1d7b2d1f8662c9cf`.
Planning and this evidence remain outside the Git repository. Code PR: 10 files, +287/−12.

## Boundary

The same native predicate now precedes direct L2 submit's snapshots/source publication, READY callable registry mutation and close's CLOSED/attempt claim. Native register/unregister (including raw-blob binding) and legacy Python registry protect their own mutations. No copy of the environment rule, new exception type, C ABI/status change, or execution queue reordering.

Repeated unaccepted refusal preserves lifecycle, handles/ref counts, source uses and run sequence. Restoration permits retry. Accepted close failure remains CLOSED with cleanup debt; wrong-context retry does not consume the debt or replace the failed attempt, and correct-context retry drains it without reopening the Worker. Ordinary execution errors/partial or unproven work keep their conservative paths. Context validation stays at mutable execution-owner boundaries, not general read-only descriptor queries.

## Evidence

| Check | Result | Log |
| --- | --- | --- |
| Initial negative cases before fix | 8 failed on baseline: native/legacy registry and public submit/register/unregister/close | [red](../../LOCAL-EVIDENCE.md#artifact-133f5e080b94) |
| Extended related tests | 206 passed, including shared/final refs, raw registration and accepted debt | [related](../../LOCAL-EVIDENCE.md#artifact-0bd9a155d4b5) |
| Full Python UT after fixture repair | 3222 passed, 91 skipped | [UT](../../LOCAL-EVIDENCE.md#artifact-2403466606e6) |
| C++ UT | 317 targets passed | [C++](../../LOCAL-EVIDENCE.md#artifact-ab59ee585958) |
| L2 Program simulation (manual included) | A2/A3 6, A5 3 cases passed | [A3](../../LOCAL-EVIDENCE.md#artifact-2fd1d9d37167), [A5](../../LOCAL-EVIDENCE.md#artifact-a52643e70a2c) |
| Final hardware matrix | 44 passed, 27 deselected, 171.58s; task-submit device 0; both runtimes, owned/borrowed depth 1/2 | [hardware](../../LOCAL-EVIDENCE.md#artifact-340de1adb916) |
| Final committed head rebuilt | 206 related tests passed | [build](../../LOCAL-EVIDENCE.md#artifact-9af1d2bedc4a), [tests](../../LOCAL-EVIDENCE.md#artifact-c2757df29bb4) |
| Changed-file hooks | All passed | [hooks](../../LOCAL-EVIDENCE.md#artifact-923a4f6e9959) |

The initial full run found missing validator methods in test doubles; they now expose the native surface explicitly. A later run had one failing teardown-fault expectation because two cases reused the same loaded DSO and exhausted its one-failure script. Faultful configurations now load uniquely named modules, preserving an independent failure budget. No product behavior was changed to address those fixture defects.

Hardware adds typed native registration and Python done/wait/result, submit/registry/close refusal checks under a real wrong RTS context. It restores context and verifies caller outputs. Borrowed native runs are bridged into a Python facade; this is not complete public borrowed Worker init/submit qualification. Host-only public lifecycle tests use the real DSO validator/binding plus controlled submission/completion. Full matrix testing preceded commit with the same implementation; final commit was rebuilt and related tests rerun.

Continuous [occupancy sampling](../../LOCAL-EVIDENCE.md#artifact-1cb9ca3b8733) and isolated [device logs](../../LOCAL-EVIDENCE.md#artifact-b212e97fffef) accompany the task-submit run. No AIV or historical A5-hang fix is claimed.

## Next

After this PR merges and is qualified, K2b binds execution environment identity, process, generation and Buffer source to real consumers, then consolidates internal chip access. K2c must still expose and qualify the complete public borrowed eager path; capture/replay and graph/module ownership remain later.
