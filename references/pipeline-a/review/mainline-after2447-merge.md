> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# Mainline after #2447 merge

GitHub confirms #2447 merged 2026-09-30T02:01:22Z as 9b283ab1e45d69c354adb9ed5b0610aea28dd6e9, head88a7370ab. Fetched upstream/main at this commit; 18-file delivered scope is actual Qwen Worker.submit depth-one path and artifact/input validation, not generic early enqueue.

Read-only git merge-tree (no worktree/index mutation; no tests) against exact new main:

- #2456 head9ec7438bc: eight conflicting files, recorded ../claude-managed/pr2456-after2447-merge-tree.txt. Contract, README, reference runner, input validator, mkdocs, pyproject, reference test and modify/delete stack_manifest. Original author must reconcile only remaining fallback/report delta, preserving merged digest validation and deletion. Do not blindly apply stale full tree or infer old green CI covers this combination.
- #2471 head18291ec85: clean merge-tree; net two tests +34 lines. Full increment read: distinct buffer identity/address assertions in A3 HBG early_enqueue scene plus distinct submission/run/slot/lease identity test in hierarchical admission. No production implementation; still not full same-content clone/Qwen multi-request proof. Clean merge is not test completion on new main.

#2429 remains OPEN/14 comments. External authors retain ownership. No branch rebase/push/comment/merge or tests performed by manager. Future dispatch overlap records must use new main; old 8aae snapshots no longer valid.
