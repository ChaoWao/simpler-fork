> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# PR2588 current-head macOS UT disposition

- Head: `71006e41175b652050e3eced0907e85b622d77e9`; job `116732131965`; workflow `38824870209`.
- Full log downloaded with HTTP206, 980908 bytes; checkout `c5e680261b4a9549e55dbdd1d7efeeb15cba5306` matches the already verified base/head merge.
- GitHub annotation explicitly reports maximum execution time of 20 minutes exceeded. This is a job timeout, not a demonstrated unit assertion failure.
- Python: 3251 passed, 21 skipped, 92 deselected. CTest: 354/354 passed at 00:13:01 UTC. Job cancelled at 00:16:37, completed 00:16:46.
- The same shell step then schedules `check_ut_cpp_axis.py --jobs 4`, stub-linkage validation and the swimlane exporter/parser pytest. There is no completion output for these after CTest. Orphan clang processes were terminated during cleanup. This supports investigating the post-CTest checks; it does not prove which process consumed the remaining time or exclude a source defect.
- Do not claim the whole job passed. No manager retry while known functional blockers remain; preserve this evidence for the original author's next substantive repair/review. No local tests run.

Evidence: `../claude-managed/pr2588-job-116732131965.log`, `../claude-managed/pr2588-710-macos-annotations.json`, `../claude-managed/pr2588-710-macos-final-job.json`.

## Read-only follow-up at 2026-10-16 01:00 UTC

At exact710, `tests/lint/check_ut_cpp_axis.py` and the UT workflow have no diff against current basecc6. The axis checker replays each covered target's compile entries with `-E -H`, a four-worker ThreadPoolExecutor in CI, captured output and no subprocess timeout. It waits for all results before reporting; successful completion itself is silent. Thus the post-CTest log gap is consistent with preprocessing, but does not locate the blocked process conclusively: later checks can also run without a preceding completion marker. Cleanup's orphan clang processes are supporting evidence only. No local reproduction, workflow timeout edit or CI retry performed. Keep this separate from the PMU functional failure and preserve the exact-head pass evidence for the 354 CTest cases.
