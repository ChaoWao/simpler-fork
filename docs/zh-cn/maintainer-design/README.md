# simpler 核心设计快照

发布日期：2026-11-13。业务状态以 GitHub 为准，本目录是本地 `.docs` 的导出备份。全量方向观察为 2026-11-10；本次局部确认 #2893 merge `37c2ad5e1e083c9e805e24c002702423eef61c18` 已在 main、#2731 已关闭，没有重跑产品或硬件验收。

## 阅读入口

- [当前进展与下一步](roadmap.html)：首屏为交互 DAG，历史阶段和判断记录折叠阅读。
- [全屏依赖 DAG](dependency-dag.html)：整体、L3+、模型、Host DDR、合同细化与责任树；节点包含状态、下一步及精确 PR 版本。
- [目标与验收索引](work-items.md) · [来源清单](goal-sources.json) · [覆盖口径](goal-coverage.md)。
- [L3+ 设计原则](l3plus-design.md) · [公共用户面](worker-memory-model/user-surface.md) · [退休合同](worker-memory-model/access-retirement-contract.md)。
- [#2714 具名复审](review/pr-2714.md)：保留 reviewed head、测试边界与合入回执。

## 当前关键进展

本次具名评审 head `4f04c79446882dde96bb5b173bb7d584ef230afa`：共享内存 payload 限长、扩大容量哨兵和 BF16 独立 backing/输出/golden 已核对，评审建议 approve。CI 13 成功 / 2 跳过；实际测试合并提交 `e1df1e8`，Python 9876 passed / 40 skipped、C++ 501/501。[作者验证记录](https://github.com/hw-native-sys/simpler/pull/2893#issuecomment-6847289612) 提供 18/18 共享内存回归、4/4 A5 样例及两种错误 backing 负对照；本次未独立执行硬件或 macOS。socket gate 证明 L3 admission/建图重叠，不证明 child MAP 或 Device kernel 同时执行。

除上述交付及关联下一步外，下列其他方向仍为 11 月 10 日观察。

- #2714 / #2893 已合入 main，#2730 / #2731 关闭。整表多 L2 MAP 不创建 URMA 资源；下一步由 #2657 承接实际应用、异步完成与有用计算重叠验收，#2820 对齐共享准备契约。
- #2922/#2913 的分片 Host 域与 #2938/#2939 的首次初始化已合入，是独立路线；PCIe Host-source URMA 的验证限制保留，不能由本地 MTE 通过消除。
- 通信子域 #2970、CCU #2974、部署 #2791/#2900 与资源/常驻设计可并行推进，依赖指具体接口与验收产物。
- #2429 汇总 Qwen 六格应用集成；本地成绩与各仓 main/正式 CI 收口分开，跨仓组合和性能仍由 #2658 跟踪。
- #2910/#2823/#2824/#2885 为 NOT_PLANNED 暂缓，不计已交付。Issue 父子关系单列，不能当作执行先后。

## 快照范围

- #2716 原生树 39 节点 / 38 条归属边；50 个具名 Issue；17 个交接节点 / 17 条关系及 7 条历史迁移合同关系。读取 139 个 main 目标 PR，近期 71 个合入均已核 main 祖先。
- 16 项文档守卫全部通过；97 份 Markdown 可达、128 份文档载体有归属，604 个历史来源编号有归属。2026-11-13 的 169 个 open 条目已登记；覆盖区间内 302 个 main 合入 PR 均有记录。本次补充登记不等于逐 PR 技术评审，也不代表功能完成率。
- 四项错误注入均被守卫拒绝：整表 MAP 错误阻塞分片域、合入丢失祖先证明、暂缓误标完成、下一步投影漂移。桌面 1500 与窄屏 390 的六视图、筛选、搜索、缩放和跳转通过。
- GitHub 目标不在本地恢复 RA / DS / EX 独立实施轨。资源、部署通信、执行及 Qwen 的历史交接保留原编号与验收边界；kernel/HBG 内部实施仍按原范围外入口管理。
- joint-goals 资料作为跨仓联合目标、协作工具或历史证据登记；不将其中全部任务纳入本轮 simpler 产品进度。
- 当前图与 Host DDR 用户面采用本轮来源。其余文档按各自基线阅读；此前全量源码审计仍为 `937c57efc71ad01a01c3a72072df075d2a5dd24f`，不是本轮全目标验收。
- `.docs` 只有原 checkout 一棵并保持 gitignored。本目录不用于独立维护状态；未导出的本地参考已标注，来源及发布哈希见 [清单](SOURCE-MANIFEST.json)。

## 历史材料阅读规则

旧“独立 daemon”“需要 SHMEM 库”“数据面全未通”、公共额外读写权限与 COW 策略不能自动成为当前待办。mpirun、原生 CANN ACL Runtime/PTO、Buffer 存储 / Tensor 几何 / Worker 逻辑部署的边界按现行设计解释。映射、数据就绪与最后使用分别验证，旧验收不外推所有平台、拓扑或故障组合。

## 全部材料

- [cpu-npu-shared-memory.md](cpu-npu-shared-memory.md)
- [cpu-npu-shared-memory/domain-member-vs-attachment.md](cpu-npu-shared-memory/domain-member-vs-attachment.md)
- [cpu-npu-shared-memory/domain-membership.md](cpu-npu-shared-memory/domain-membership.md)
- [cpu-npu-shared-memory/hardware-constraints.md](cpu-npu-shared-memory/hardware-constraints.md)
- [cpu-npu-shared-memory/w2-w5-inversion.md](cpu-npu-shared-memory/w2-w5-inversion.md)
- [cpu-npu-shared-memory/work-breakdown.md](cpu-npu-shared-memory/work-breakdown.md)
- [feature_roadmap.md](feature_roadmap.md)
- [goal-coverage.md](goal-coverage.md)
- [l3l4.md](l3l4.md)
- [l3l4/cluster-comm.md](l3l4/cluster-comm.md)
- [l3l4/inventory.md](l3l4/inventory.md)
- [l3l4/lazy-init.md](l3l4/lazy-init.md)
- [l3l4/memory-kinds.md](l3l4/memory-kinds.md)
- [l3l4/remote-startup.md](l3l4/remote-startup.md)
- [l3l4/scheduling.md](l3l4/scheduling.md)
- [logical-physical-separation.md](logical-physical-separation.md)
- [roadmap.html](roadmap.html)
- [track-boundaries.md](track-boundaries.md)
- [work-items.md](work-items.md)
- [worker-memory-model.md](worker-memory-model.md)
- [worker-memory-model/access-retirement-contract.md](worker-memory-model/access-retirement-contract.md)
- [worker-memory-model/backend-matrix.md](worker-memory-model/backend-matrix.md)
- [worker-memory-model/bufferhandle-abi.md](worker-memory-model/bufferhandle-abi.md)
- [worker-memory-model/graph-execution-contract.md](worker-memory-model/graph-execution-contract.md)
- [worker-memory-model/p1b-corrected-design.md](worker-memory-model/p1b-corrected-design.md)
- [worker-memory-model/p1b-review-findings.md](worker-memory-model/p1b-review-findings.md)
- [worker-memory-model/user-surface.md](worker-memory-model/user-surface.md)
- [review/2026-09-28-cross-plan-lessons.md](review/2026-09-28-cross-plan-lessons.md)
- [review/pr-2096.md](review/pr-2096.md)
- [w2-p1b-endpoint-convergence.md](w2-p1b-endpoint-convergence.md)
- [review/pr-2635.md](review/pr-2635.md)
- [l3plus-design.md](l3plus-design.md)
- [review/track-2716.md](review/track-2716.md)
- [goal-sources.json](goal-sources.json)
- [review/ra-handoff.md](review/ra-handoff.md)
- [review/qwen-handoff.md](review/qwen-handoff.md)
- [review/ds-handoff.md](review/ds-handoff.md)
- [review/ex-handoff.md](review/ex-handoff.md)
- [dependency-dag.html](dependency-dag.html)
- [review/pr-2714.md](review/pr-2714.md)
