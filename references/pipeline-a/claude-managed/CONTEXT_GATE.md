> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# Context gate for managed original Claude sessions

User rule: inspect before every task/feedback prompt. If current context exceeds 80%, compact the same idle UUID first, verify it, then dispatch.

From pipeline-a-manager:

```sh
python3 .docs/claude-managed/dispatch_claude.py purrfect
python3 .docs/claude-managed/dispatch_claude.py purrfect --compact-only
python3 .docs/claude-managed/dispatch_claude.py purrfect --prompt .docs/claude-managed/TASK.prompt.md --prefix UNIQUE_TASK
```

`--prompt` automatically performs the check and required compaction. Do not bypass it with a direct Claude launch. Keep the exec session registered while it runs. The guard takes a per-session manager lock and refuses a live original Claude process. No new UUID, fork or clear. Unknown window or failed/ineffective compaction blocks the dependent task. It does not authorize the task itself; manager must still apply CURRENT CONTRACT.

Measurement: latest main-session API input_tokens + cache_creation_input_tokens + cache_read_input_tokens, divided by the matching model contextWindow reported in that session's result JSON. Cumulative billing usage and JSONL file size are not context occupancy. After compaction use compactMetadata.postTokens until a newer API usage record arrives. These are provider/CLI-reported counts, not an independent tokenizer measurement; model/window changes require a fresh capacity source.

Sources: [Claude context fields](https://code.claude.com/docs/en/statusline#context-window-fields), installed CLI --help, original session transcript/result metadata. /compact must produce a new compact_boundary with reduced postTokens, not just a textual summary. Persist checks in context-checks.jsonl and compact prompts/results alongside task outputs. Preserve goal, authorization, exact head, unresolved findings and evidence paths. Re-read current registry after compression; the summary does not grant authority.

The policy runs between tasks. An already active turn is allowed to finish; do not start a simultaneous compaction writer or kill a useful turn merely for crossing the threshold. The policy does not change global Claude settings or the user's other sessions.
