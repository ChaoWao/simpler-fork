> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# 派发前：main 与 open PR 去重

适用每次任务派发，包括设计、实现、修复及同会话反馈；单独查看上下文或压缩不派任务时无需此记录。

1. 准备最终任务prompt，运行（manager worktree根目录）：

```bash
python3 .docs/claude-managed/dispatch_preflight.py --out .docs/claude-managed/TASK-preflight.json --prompt .docs/claude-managed/TASK.prompt.md
```

脚本完整分页读取所有open PR（含draft）的题目、正文、作者、head/base及更新时间，读取main并fetch精确对象；不会checkout或改作者工作树。stdout包含prompt_sha256。

2. manager检查快照全部PR，筛选相关项，再读取实际diff和main相关源码/已合入实现。不能由题目不同推断无重叠。新架构仍受原授权规则约束。

3. 写 `TASK-overlap-review.json`，snapshot相对路径按review所在目录解析：

```json
{
  "snapshot": "TASK-preflight.json",
  "prompt_sha256": "复制脚本给出的值",
  "decision": "proceed",
  "main_evidence": "精确main SHA、相关源码/合入PR及仍未覆盖的需求",
  "open_pr_disposition": "逐项列相关PR、已读diff/head、负责人及不重复的范围；无相关项也需说明检索范围"
}
```

已覆盖时记录decision为covered/defer，不派发。设计只能在原授权边界内；核查记录不是新授权。

4. 通过原dispatcher派发：

```bash
python3 .docs/claude-managed/dispatch_claude.py memory --prompt .docs/claude-managed/TASK.prompt.md --prefix UNIQUE --overlap-review .docs/claude-managed/TASK-overlap-review.json
```

dispatcher在idle/context/必要压缩之后重新查询。任务文本改变、main SHA变化、open列表/head/base/更新时间变化、缺少结论或网络失败时拒绝启动任务；先实读变化后更新核查，不能直接改SHA绕过。live证据写入UNIQUE.preflight-live.json。

脚本验证证据是否新鲜和与任务匹配；语义去重仍由manager读源码负责。远端在检查后仍可能变化，所以原作者修改/发布前必须再次核对相关PR，避免短暂竞态造成重复实现。运行中的现有任务不重启。
