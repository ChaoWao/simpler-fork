> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# PR2564 run身份修复复审

Head: `9d71625e0128c16dda067575d298cd4c6f7761cd`; base: `a89c1170c87ae6c776a977bce2dcabea5fd74b7a`。

用户已授权修复，manager在独立 `pr2564-run-identity` worktree为唯一writer；无新agent，无其他产品分支编辑。已通过精确旧head lease更新lwDavid原分支，保留David author，原PR仍一个commit。

## 修复闭环

- 当前七列记录按run_epoch分组，再各自执行已有图聚合与两种关键路径算法；五/六列保持None身份的历史单run语义，epoch0是合法身份。
- 图内任务起止、块数、数据依赖和core前驱都不跨run聚合；未改变同run算法。抽取函数后把初次min/max默认值改为本行st/en，消除inf浮点类型，不改变数值结果。
- CLI每目录写一个报告，报告按run分别显示makespan、计算与等待；不把run间空闲变成计算。旧build_graph单run入口遇到多个run明确拒绝，CLI使用build_graphs。
- 两份Perfetto保持原完整trace，只用(epoch,taskId)集合改变WorkerView任务名称。跨run同ID不能填补缺失trace；验证两份输出后才写文件，维持既有避免部分输出行为。
- 混合历史/带身份记录、非法epoch、错误列宽明确失败。无版本字段、无runtime/设备/传输接口改动。
- README明确每run报告和trace身份要求；PR body已更新新scope与真实验证范围，旧作者全量数据不冒充新head验证。

## 验证

新增六个测试函数参数化共11项，首次实际执行11 passed in0.21s，覆盖TMR/HBG布局、重复ID且不同关键路径、两次10tick执行/90tick间隔、epoch0、旧五/六列CLI和无stamptrace、非法/混合身份、跨run同ID遮盖缺失trace。初次收集因未安装worktree缺simpler导入路径退出，未执行任何测试；随后源代码PYTHONPATH及UT conftest执行。无既有测试重跑，无硬件测试、本地构建或安装。

Ruff check/format、Pyright、headers/English/retirednames/taskid lint、diff whitespace通过。静态核对原六项回归的fixture改动和错误信息assert一致。

## 当前提交最终收口（2026-10-14T01:48:41.270652+00:00）

Ready。当前19成功1跳过；workflow38493471954的17个job及其步骤全部成功。两份完整UT日志证明实际checkout786f2dd为9d71625e0与最新main5f4f9525d的合并：

- Ubuntu：关键路径17项通过；Python3071passed/12skipped/68deselected；CTest314/314通过。
- macOS：关键路径17项通过；Python3062passed/21skipped/68deselected；CTest313/313通过。
- 以上为已有CI日志取证，没有重跑任何本地测试或提交CI重试。
- 三面反馈：inline0；一份旧head空正文approval已dismissed，不能视为当前批准；两条conversation分别为旧bot概要/原作者历史验证说明，无新未处理要求。PR body已描述当前完整行为，manager不改他人历史评论。
- 当前head未变，原精确静态评审保持。main新增#2561只改A3故障清理及相应probe，与本PR三文件不相交，且当前CI已实际覆盖新base，无需为新base重复rebase。

证据：`../claude-managed/pr2564-current.json`、`pr2564-threads-current.json`、`pr2564-jobs-current.json`以及`pr2564-115623925557.log`/`pr2564-115623925602.log`。用户检查点为合入；manager没有合入。
