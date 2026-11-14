# simpler 核心设计快照

发布日期：2026-11-14。当前 roadmap 仅维护 Worker L3+ 四方向：#2721 公共基础、#2722 通信与后端、#2723 L2 常驻执行、#2724 多级多机部署。当前进展、依赖与历史记录统一由 roadmap 提供；来源观察仍为 2026-11-13，main 基线 `3bcaf8d4ff86fa9d47a192d31c7fd7cb9221bc47`。

## 阅读入口

- [当前进展、下一步与依赖图](roadmap.html)：四方向总览、L3+、Host DDR、合同细化与责任树直接在同页交互；历史阶段和判断记录折叠阅读。
- [目标与验收索引](work-items.md) · [来源清单](goal-sources.json) · [覆盖口径](goal-coverage.md)。
- [L3+ 设计原则](l3plus-design.md) · [公共用户面](worker-memory-model/user-surface.md) · [退休合同](worker-memory-model/access-retirement-contract.md)。
- [#2714 具名复审](review/pr-2714.md)：保留 reviewed head、测试边界与合入回执。

## 四方向与下一步

| 方向 | 关注点与下一步 |
| ---- | -------------- |
| #2721 公共基础 | Buffer/Tensor/Worker、共享区间及资源寿命；收敛 #2460/#2720 及现有消费者迁移。 |
| #2722 通信与后端 | CCU 合同对齐、HBG URMA 后续、故障复发及 PCIe Host-source；Host DDR 两条路线分别验收。 |
| #2723 L2 常驻执行 | 同一 Run 的多轮 work/completion、背压、STOP/drain 与异常退出。 |
| #2724 多级多机部署 | #2791 部署迁移；结果、退出与资源释放分别确认。 |

模型应用、跨仓性能与通用 Kernel/capture 不进入当前进展、下一步或依赖图。历史交接资料保留供追溯，不继续维护其业务进度。L3+ 必需的 L2 通信、资源和后端接入仍属四方向。

## 快照范围

- #2716 原生树 43 节点 / 42 条归属边；当前保留 44 个相关 Issue、13 个交接节点 / 11 条关系及 7 条历史资源迁移合同关系。
- 当前图来源观察保留原日期；范围确认日期不能替代状态或硬件复验日期。父子归属不当作执行先后。
- 16 项文档守卫通过；桌面 1500 与窄屏 390 的五个视图及交互通过。模型节点回流、模型下一步回流、遗漏四方向的三项负对照均被拒绝。
- 历史目标继续保留原归属，不恢复 RA / DS / EX 本地实施轨。全仓元数据登记只防遗漏，不扩大当前维护范围。
- `.docs` 保持单一、gitignored；本目录是导出备份。来源和发布哈希见 [清单](SOURCE-MANIFEST.json)。

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
- [review/pr-2714.md](review/pr-2714.md)
