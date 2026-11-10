# simpler 核心设计快照

评审快照日期：2026-11-10（Asia/Shanghai）。#2714 复审针对 head `d9ee29e55a90`：framework Host use 修复已验证，代码评审通过，当前 head A5 验证待补。其他设计与 DAG 保留各自原观察日期；本次不是全量目标刷新。

## 阅读入口

| 目的 | 文档 |
| ---- | ---- |
| Host DDR 技术评审 | [#2714 具名评审](review/pr-2714.md)：按调用 MAP、framework 退休修复及具名 CI 证据 |
| 当前目标依赖 | [交互依赖 DAG](dependency-dag.html)：合同、条件与来源；责任树单列 |
| 资源任务与设计问题 | [#2721](https://github.com/hw-native-sys/simpler/issues/2721) · [设计澄清评论](https://github.com/hw-native-sys/simpler/issues/2721#issuecomment-6651217998) |
| 本地入口与验收索引 | [工作项](work-items.md) · [看板](roadmap.html) |
| 历史资源编号交接 | [16 项完整对照](review/ra-handoff.md) |
| 部署与通信目标 | [#2724](https://github.com/hw-native-sys/simpler/issues/2724) · [#2722](https://github.com/hw-native-sys/simpler/issues/2722) · [11 项目标与评论交接](review/ds-handoff.md) |
| 执行与常驻 | [#2721](https://github.com/hw-native-sys/simpler/issues/2721) · [#2723](https://github.com/hw-native-sys/simpler/issues/2723) · [七项目标与评论交接](review/ex-handoff.md) |
| Qwen 跨仓目标 | [#2658](https://github.com/hw-native-sys/simpler/issues/2658) · [Q1–Q4 / C2b 验收交接](review/qwen-handoff.md) |
| 设计原则与边界 | [逻辑与物理](logical-physical-separation.md) · [L3+ 设计](l3plus-design.md) · [交接契约](track-boundaries.md) |
| 目标来源与证据 | [来源清单](goal-sources.json) · [覆盖口径](goal-coverage.md) · [此前范围审计](review/track-2716.md) |
| 内存与资源退休 | [用户面](worker-memory-model/user-surface.md) · [退休契约](worker-memory-model/access-retirement-contract.md) |
| 场景与里程碑 | [L3/L4](l3l4.md) · [总路线图](feature_roadmap.md) |

## 快照范围

- 依赖图观察时间为 2026-11-05T08:49:45Z，关联 main `91f0004517a9f1415228d33bfd8ca95eddbf936d`。14 条具体关系与 7 条迁移合同关系均保留来源；#2716 原生责任树为 33 个节点、32 条归属边。关系刷新不代表新 main 实现验收。
- `.docs` 是唯一维护树，保持 gitignored；本目录是导出备份，不是第二份实时看板。
- 公共资源不再维护本地 RA 独立任务或进度。历史 16 项全部交接到 GitHub，原编号只供历史引用。
- DS 不再维护独立本地进度轨；部署/会话归 #2724，通信域/数据面归 #2722。两条设计交接评论已发布，原正文与负责人保持；额外 lease、规模与公共组合类型必要性仍待确认。
- EX 不再维护独立本地进度；公共执行与寿命归 #2721、单次 L2 Run 常驻归 #2723，真实模型连续执行组合归 #2658。#2316/#1853 已关闭，基础机制与 P0.2 清理保留静态证据。零 heap、Arena、删除 scope 回收均须必要性论证，不因旧方案列过步骤而强制交付。
- Qwen 不再维护独立本地进度轨；Q1–Q4/C2b 的整体目标归 #2658，#2429/#2666 保留 vLLM 接入与执行资源边界。固定两帧等旧方案不是当前强制要求，具体场景保留于交接记录。
- 共同模型与迁移归 #2460，Region 归 #2720；平台映射、通信域寿命和执行资源分别引用其原有入口，不全部收进 #2721。
- Buffer 描述存储，Tensor 引用 Buffer 并描述几何，Worker 是逻辑角色。共享映射按共享读写建立；真实 OS／驱动限制保留，不新增公共读写策略。旧 access/wire 的迁移须成对处理消费者。
- 两套 Arena、公开 ResourceBundle、plan、capability 对象的必要性仍由 #2721 的设计问题核定。评论发布不代表负责人已经接受，跟踪覆盖不代表实现完成。
- 当前 #2714 复审重建 head CPU bindings，266 项定向用例通过，含原独立复现探针 4/4；原 P2 已修复。作者报告 `4b52750a4175` 上 9 项 A5 通过，独立已读硬件 CI 仍是 `e012a5d1aaf0`；当前 head runtime 组合的 A5 验证待补。16 项文档守卫中 12 项通过；全局仍有 30 份不可达文档、42 份缺 owner 登记、34 项 open PR/issue 未登记，merged coverage 缺 #2984 目标元数据。桌面与窄屏渲染通过。
- 本轮 GitHub 归属观察日期为 2026-11-05；具名代码与 CI 证据仍按各页原 SHA 阅读。此前方向审计为 `63ffeb8a8bd9b6527912fee87f6a8defdf82d5e0`，历史全量源码审计为 `937c57efc71ad01a01c3a72072df075d2a5dd24f`；备份日期不是全目标重新验收日期。
- 只登记 main 目标 PR。未收入的本地参考链接明确标注；文件来源与导出哈希见 [清单](SOURCE-MANIFEST.json)。HTML 下载后可以交互查看。

## 历史材料阅读规则

“必须独立 daemon”“缺 SHMEM 库接入”“数据面全未通”“ABORT 失败只写 stderr”等历史判断不能作为当前待办。已有 MPI、原生 ACL/PTO 通信及其 CI 证据保留原具名基线，不能外推全部后端、故障与生命周期验收。通用 kernel/eager-capture、HBG 调度、DFX 与跨仓模型保持既定外部归属；必要的 L3+ 通信接入仍按完整场景交付。

旧资源方案里的额外权限对象、两套 Arena 和公共 ResourceBundle 不构成当前已确认承诺。保留设计动机与验收场景，具体 API 和实现形状以对应 GitHub 设计决定为准。

## 全部材料

- [CPU ↔ NPU 共享内存与常驻编排（设计 · landing）](cpu-npu-shared-memory.md)
- [域成员、rank 与 attachment：三个概念该怎么分](cpu-npu-shared-memory/domain-member-vs-attachment.md)
- [通信域的成员模型：level 是逻辑概念，能力挂在 endpoint–resource 边上（契约）](cpu-npu-shared-memory/domain-membership.md)
- [CPU↔NPU 共享内存：硬件与平台约束（参考）](cpu-npu-shared-memory/hardware-constraints.md)
- [W5 先于 W2 落地：成员词汇的时间窗口与 track 重排](cpu-npu-shared-memory/w2-w5-inversion.md)
- [实施分解：从当前代码到新设计（计划）](cpu-npu-shared-memory/work-breakdown.md)
- [Simpler 功能演进路线图](feature_roadmap.md)
- [目标来源与覆盖口径](goal-coverage.md)
- [simpler 面向 L2/L3/L4 集群的未来诉求梳理 × Issue/PR 关联](l3l4.md)
- [通信 review 与改造](l3l4/cluster-comm.md)
- [Issue / PR 关联总表](l3l4/inventory.md)
- [L3 eager init 与 L2 启动](l3l4/lazy-init.md)
- [四类 worker 内存模型](l3l4/memory-kinds.md)
- [Remote L3 启动与接口](l3l4/remote-startup.md)
- [调度：submit 指定 worker + 去自由调度 + heap](l3l4/scheduling.md)
- [总纲：逻辑组件 ⊥ 物理资源](logical-physical-separation.md)
- [roadmap.html](roadmap.html)
- [分线：本原理决定**什么是一条 track**](track-boundaries.md)
- [工作项登记表 —— 本地合同归属与验收证据](work-items.md)
- [simpler Worker 内存模型（目标设计）](worker-memory-model.md)
- [访问、完成与资源退休契约](worker-memory-model/access-retirement-contract.md)
- [backend 矩阵 + 范例 + 边界](worker-memory-model/backend-matrix.md)
- [BufferHandle / BufferRef 逻辑 schema（**已被取代**，存档）](worker-memory-model/bufferhandle-abi.md)
- [内存模型对 Graph Execution 层的要求](worker-memory-model/graph-execution-contract.md)
- [P1-B 最终设计：BufferHandle / BufferRef 内存模型](worker-memory-model/p1b-corrected-design.md)
- [P1-B（PR #1453）修补清单](worker-memory-model/p1b-review-findings.md)
- [用户面契约：分配 API 与 `Buffer` 的可见性](worker-memory-model/user-surface.md)
- [两套方案的借鉴评估](review/2026-09-28-cross-plan-lessons.md)
- [PR #2096 再评审](review/pr-2096.md)
- [W2 端点模型 ↔ P1-B `ImportContext`：收敛计划](w2-p1b-endpoint-convergence.md)
- [PR #2635 评审](review/pr-2635.md)
- [L3+：设计原则、职责与代码地图](l3plus-design.md)
- [2716：范围、代码证据与维护动作](review/track-2716.md)
- [goal-sources.json](goal-sources.json)
- [资源与访问：GitHub 交接对照](review/ra-handoff.md)
- [Qwen 跨仓目标交接到 #2658](review/qwen-handoff.md)
- [分布式部署与通信：旧 DS 目标交接](review/ds-handoff.md)
- [执行目标交接：历史 EX 索引退役](review/ex-handoff.md)
- [dependency-dag.html](dependency-dag.html)
- [PR #2714 技术评审](review/pr-2714.md)
