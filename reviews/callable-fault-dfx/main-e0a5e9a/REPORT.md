# PR #2536 update after #2537 merge

Head: 280ee5286a44339c4c1d2ff45749f7c12e3a52b0. Base: e0a5e9a16ce2df88643fbd20c4e31406f4062e95. One commit, 11 files, no temporary material in PR.

Rebased without conflicts. Changed only the diagnostic UT factory relative to the previous PR diff: fixed-template 1024 became make_chip_callable, aligned with main's 2048-child ChipCallable. Ran the rebased original test first: two failures, one pass; after correction all three pass. Both architecture cold-path suites pass (four cases each), including separate SIMPLER_DFX=0 builds. Pre-commit passes after formatting the shorter factory call. Runtime rebuilt with worktree .venv.

HBG a2a3sim 30 passed/7 skipped; a5sim 32 passed. HBG a2a3 onboard 70 passed/7 skipped (255.55 s pytest), task task_20261012_184257_345165715396, reserved devices 4/5/6. Arch precheck passed. External occupancy sampling and private Ascend logs are retained here. Runtime artifact hashes checked after validation: 28, changed: []. This is a runtime-scoped regression, not a new full all-runtime or Network reproduction experiment.

Downloaded old CI Network device logs and classified the actual failure as TMR VEC UB out of bounds. Same-run fault PC equals the 244-byte child's end-exclusive address; evidence extends the previous HBG observation to TMR, without proving a shared root cause. General onboard + DeepSeek's device-8 bootstrap 507018 remains unclassified without matching device logs. See ci-triage.md and network-fault-excerpts.txt. Old failures remain disclosed in the PR even though this local regression passes.

Removed the resolved log-sink issue from local KNOWN_ISSUES.md and added the two CI findings. Raw artifacts, this report, test harness, and source manifest remain outside the repository. No AIV fix or stable reproducer is claimed.
