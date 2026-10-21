> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# Archived P3 worktree (#2356)

PR #2356 merged as d17842db595dc01417b8c1c006ecaf19c848588c; archived branch tip2680b0f20044babc299bddc014acfe91b0397f7c exactly matches merged PR head. No uncommitted tracked/untracked source or active worktree process found. All14 local .docs evidence files copied and SHA256-verified (manifest.json); source commit retained in pr-2356.bundle with prerequisite0dbf12ae3. Disposable .venv and tests/ut/cpp/build outputs are not evidence and are removed with the worktree. Original Claude session history remains outside the worktree.

Restore if necessary from the main repo: git fetch /absolute/path/to/pr-2356.bundle refs/heads/fix/drain-hbg-recorders-before-releasing-build-state:refs/heads/fix/drain-hbg-recorders-before-releasing-build-state, then git worktree add <path> fix/drain-hbg-recorders-before-releasing-build-state; copy docs/ to <path>/.docs. No remote branch deletion is part of this local cleanup.
