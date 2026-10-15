# PR #2510 — shared context precondition revision

## Scope

Base: main `60167bcc` (includes #2509). Validation is running on the working-tree delta over rebased head `56d9c655`; the exact patch is preserved in `tested-working-tree.patch`. Final commit: `3c8cad19b839a57c24ce40784c6350224a958dc2`. Post-test changes are only formatting and one comment; see `post-test-changes.patch`. Planning and probe files remain outside the repository.

- Retain native RTS context identity; wrong context refuses before mutation without switching the caller.
- C API prepare, run finalize and device finalize call a shared runner precondition without deciding whether the context is kernel-mode.
- Callable release validates inside the runner before changing registry or allocation ownership.
- Non-virtual base `finalize()` validates before the private per-architecture `finalize_impl()`; architecture implementations contain no duplicate context preflight.
- Program thread attachment remains in the environment entry, and device ownership/reset policies remain unchanged. This is not a full rewrite of every existing mode branch.
- A context with no adopted borrowed identity has no caller-context requirement. Tests also cover fully rolled-back initialization and identity retained after partial/completed stream/event close.

## Verification

- C++: 297 targets passed; focused execution-state target: 36 cases passed.
- Python: 2,990 passed, 59 skipped.
- A2/A3 simulation sweep: exit 0, 184.92 s.
- A5 simulation sweep: exit 0, 173.07 s.
- A2/A3 lifecycle: 29 passed, 17 deselected, 70.83 s; task task_20261008_233035_112260218837.
- A2/A3 full Program sweep: exit 1, 458.87 s, four target faults; later L2 HBG 43 passed/1 skipped and TMR 60 passed.
- Pre-commit checks passed. No new AIV investigation rounds were run.

## Recurrent AIV fault during normal regression

Task `task_20261008_233035_11224665011`, devices 1/2/3/4, reported four VEC UB faults in the same early HBG cases observed previously: depth-one, device-result chain, dep-gen across runs and depth-two two-runs. Three PCs are `0x12c0c00ee75c`; dep-gen is `0x12c0c0101f5c`. Matching signatures and PCs do not prove identical root cause or exclude this revision from attribution.

CPU-side verification overlapped the beginning of this hardware sweep: both simulator sweeps (max-parallel 4 each), Python UT, and C++ build/tests. This is part of the recorded execution conditions, not an established cause.

An external sampler recorded queue ownership before launch. Four retained samples name failed child PIDs and their ancestry. At those sampled times, the other registered user occupied device 0 and this test held 1/2/3/4. This is sampled evidence, not uninterrupted proof. Full records are under `full-onboard/occupancy.log` and `fault-occupancy-samples.txt`.

Six raw host logs from three instrumented failing cases have been copied from their evidence directories; the original host session paths in console output had already been removed. Device plogs and device logs are retained separately. Complete upload address-to-binary attribution remains unavailable.

Active reproduction remains paused. This was the prescribed regression sweep for the code change, not a new pressure campaign. Do not rerun to replace the red result. After completion, freeze its artifacts before rebuilding, formatting or committing; keep the result disclosed in the PR.

## Archive verification

`failure-archive/archive.json`: 2,409 files, 1,182,029,392 bytes; no missing sources or files observed changing during copy. All 23 runtime library hashes match the launch-time manifest. This is a software archive, not a device register/cache snapshot.
