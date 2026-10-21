> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# R7 final design review

## Result

Design accepted as consolidated in [normative contract](p4-dynamic-run-resources-contract.md), subject to explicit user implementation approval. Original R7 is archived unchanged. This is design acceptance, not code/CI acceptance.

Read full successful original result, the complete376-line delta, and the previously reviewed unchanged resource/transport/lifetime scope. Same original process exited. Wait outcome persistence, explicit finalize-over-wait error precedence, unknown C mutation quarantine and deterministic native fixture plan now close the substantive R6 findings.

Manager corrected three local inconsistencies in the consolidated contract (no product/source author worktree edits):

1. F2 zero finalize result still reports persisted nonzero wait/launch error, as pseudocode already does; only both-zero is success.
2. F6 uses finalize error, then persisted wait error, then synthesized cleanup error; table now matches code.
3. F5 non-destroying success is internally inconsistent. Mark destruction_unknown before throwing, prohibit subsequent low-level calls, retain/quarantine storage/set. This enforces R7's stated 'only F4 retryable' rule. Include it in deterministic fixture19a.

F7 zero-tag C throw conservatively retains host storage as well as uncertain resources. This is an explicit conservative failure cost, not proof that the context is still alive. No implicit retry/reset or public recovery is introduced. Resource release still requires device/no-submission proof, copyback and binding/caller borrow retirement; magic alone never releases device resources.

Four-backend ordinary N>3 remains one complete feature. Optional workspace budget stays optional; no new device-wide cap, cross-device pointer inference, capture, communication or diagnostic capability. Shared #2563 files require integration before publishing, not a merge prerequisite. Implementation must refresh main/all-open PRs and absorb external coverage before edits.

The same original P4 author must read this review plus normative contract at the next authorized implementation dispatch. No further author turn solely to reword tables is necessary; manager-owned normative contract is self-contained and matches the public decision.

No tests, builds, CI POST or publication performed.
