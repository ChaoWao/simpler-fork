# simpler 核心设计快照

发布日期：2026-11-13。业务状态以 GitHub 为准，本目录是本地 `.docs` 的导出备份。当前 tracking/main 观察基线为 `3bcaf8d4ff86fa9d47a192d31c7fd7cb9221bc47`；本轮核来源与关键源码，没有重跑硬件验收。

## 阅读入口

- [当前进展与下一步](roadmap.html)：首屏为交互 DAG，历史阶段和判断记录折叠阅读。
- [全屏依赖 DAG](dependency-dag.html)：整体、L3+、模型、Host DDR、合同细化与责任树；节点包含状态、下一步及精确 PR 版本。
- [目标与验收索引](work-items.md) · [来源清单](goal-sources.json) · [覆盖口径](goal-coverage.md)。
- [L3+ 设计原则](l3plus-design.md) · [公共用户面](worker-memory-model/user-surface.md) · [退休合同](worker-memory-model/access-retirement-contract.md)。
- [#2714 具名复审](review/pr-2714.md)：保留 reviewed head、测试边界与合入回执。

## 当前关键进展

- #2970 已合入，#2833 关闭：A5 TMR Device 有序和重叠子域可复用；Host-source 仍有 dense-prefix 限制。
- #2986/#3040/#3043 已合入 CCU Worker 接入、交错提交修复和 SDK 查找。#2971 正文及开放的 #2974 与主线注册/launch 合同需由原 owner 收敛；PCIe 板型的明确不支持不能记作已验收。
- #2999 是 #2997 的公共请求基础；HBG sender、完成登记与 CQ 观察尚待交付。#3007 共享 JFC、#3051 PCIe Host-source 归 #2722；#3054 工具链元数据归 #2971。
- #2995 已加固 Host 域失败/清理，#3016 已去除 owner 映射的重复 SetAccess。#2854 曾关闭后复发重开；#2860/#2969 按原验证范围关闭，不宣称全部 URMA 故障已根治。
- #2714/#2893 已合入，#2730/#2731 关闭；MAP 与分片 URMA 两条路线独立。#2657 继续应用、异步完成、有用计算重叠和复用验收。
- #2429 的 Serving #308/#312/#317/#326 均已核 main 祖先；Simpler #2948 迁移关闭。Lib #1759 四格与 PyPTO #3194 仍开放。#2658 的同卡试测记录 TMR/HBG 为 41.2175/43.3749 ms，不代表 HBG 性能目标已达成。
- #2900 关闭未合入，核心修复和确定性回归已在 main；#2791 保持部署迁移入口。#2910/#2823/#2824/#2885 为 NOT_PLANNED 暂缓，不计交付。

## 快照范围

- #2716 原生树 43 节点 / 42 条归属边；54 个具名 Issue；17 个交接节点 / 17 条关系及 7 条历史迁移合同关系。读取 147 个 main 目标 PR，其中 75 个合入提交已核 main 祖先。
- 16 项文档守卫全部通过；167 个 open 条目已登记，覆盖区间内 304 个 main 合入 PR 有记录。登记与技术评审、产品验收分别阅读。
- 桌面 1500 与窄屏 390 的六视图、筛选、搜索、缩放和跳转通过，无横向溢出或脚本错误。历史四项错误注入记录保留，本轮没有重跑产品测试。
- 当前图记录来源观察时间；其他设计、源码审计和硬件成绩保留各自基线。A3、A5、UB、PCIe 与不同 runtime 的结果不互相替代。
- GitHub 目标不在本地恢复 RA / DS / EX 独立实施轨；通用 kernel/HBG 内部实施仍按原范围外入口管理。新子项在原方向内登记，父子归属不当作执行顺序。
- `.docs` 只有原 checkout 一棵并保持 gitignored；本目录是导出快照。未导出的参考已标注，来源与发布哈希见 [清单](SOURCE-MANIFEST.json)。

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
