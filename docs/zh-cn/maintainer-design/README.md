# simpler 核心设计快照

发布日期：2026-10-21。阅读入口：[L3+ 设计原则、职责与代码地图](l3plus-design.md)，源码基线 main `fdcbdcc9502195f7837030cf3c130f75f83d3719`。本文档快照解释资源与访问（RA）、分布式会话与通信域（DS）、执行与常驻运行（EX）的职责和交接；不是实现交付承诺。

## 阅读顺序

 | 目的 | 入口 |
 | ---- | ---- |
 | 理解 L3+ 做什么、怎样工作、复杂度在哪里 | [L3+ 设计](l3plus-design.md) |
 | 理解逻辑身份与物理部署 | [逻辑/物理总纲](logical-physical-separation.md) |
 | 理解三条实施线的职责和交接 | [分线契约](track-boundaries.md) |
 | 理解内存与资源退休 | [内存模型](worker-memory-model.md) · [用户面](worker-memory-model/user-surface.md) · [退休契约](worker-memory-model/access-retirement-contract.md) |
 | 查看场景和里程碑 | [L3/L4 场景](l3l4.md) · [总路线图](feature_roadmap.md) |
 | 查看实施状态快照 | [工作项](work-items.md) · [覆盖报告](goal-coverage.md) · [看板](roadmap.html) |

## 快照范围

- 主 checkout 的 `.docs` 是唯一维护真源，保持 gitignored；此目录是可分享的导出快照。
- 本次 L3+ 源码梳理基线为 `fdcbdcc9502195f7837030cf3c130f75f83d3719`。相对 10 月 19 日登记 13 条 main 合入及新 open 项；源码/测试入口已读，不宣称当前硬件或 CI 重跑通过。
- 全目标扫描仍截至 2026-10-18 的 `937c57efc71ad01a01c3a72072df075d2a5dd24f`。本次只定点校正设计与相关交付，不能把日期前移成全目标验收。
- 本地 11 条文档守卫通过；102 条非空合入区间、98 个 open PR/issue 登记受检，34 个实施项、364 个来源编号及 79 个文档载体有归属。登记/归属不等于实现完成。
- 分支起点为 `11eb5d2c9ef8fe5a484a3d7c686d408ae5ff3e3b`；分支起点、设计源码基线、全目标扫描与历史硬件证据分别保留。
- kernel/HBG/DFX 内部实现和外部算子仍按既定范围排除，只保留与 RA/DS/EX 的交接。
- 未选入的本地调查/工具链接标为本地参考；来源与导出哈希见 [清单](SOURCE-MANIFEST.json)。HTML 下载后可交互查看。

## 当前设计边界

- 应用定义任务、数据布局、同步协议和 queue。simpler 提供受管理区间、访问授权、必要的可见性/顺序保证、执行和安全退休；SPSC 是受 CI 看护的参考样例。
- 编排树、任务 DAG 与资源访问关系分别建模；level 不代表硬件能力，控制 transport 不代表设备数据路径。
- mpirun 是启动方向；PTO 通信从 CANN ACL Runtime 构建，SHMEM 只作实现参考。direct MPI 已在 main；group/TCP 路径并存，退役与生产清理仍需独立验收。
- main 已有统一 L3/L4 建域入口和配置复用，内部 local/global 资源表仍分开。Buffer/domain 的共同退休规则不能由 API 改名自动获得；契约 D 的具体 registry/锁序/journal 组织仍待裁决。
- accepted、prepared、enqueued、执行完成和资源退休是不同事实；容量增长必须有 backend/resource 证明，不能泛化到任意拓扑。

## 证据边界与历史更正

- #2635 的本地评审只针对旧 head `850a36bd9`。10 月 21 日 OPEN head 为 `0bc9b0f690f3`；旧 head 的竞态结论不自动适用于新 head。资源机制/应用 queue 的职责评论单独保留。
- 历史“跨机数据面未通”“必须独立 daemon”“缺 SHMEM 库接入”“ABORT 失败只写 stderr”“A5 RDMA backend 空白”等表述不能作为当前待办；对应更正和具名证据在原文中。
- A3 Fabric 与 peer-TLOAD 的历史 CI 证据不替代当前 SHA 或其他硬件组合的验收。当前 bootstrap 故障登记与已有正常路径可以同时成立。
- `simpler-pr-list.html` 是历史候选排期，未纳入导出；其来源编号已在覆盖报告中归属，不能据其旧判断重复开发。

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
- [设计轨道与在飞项图形快照](roadmap.html)
- [分线：本原理决定**什么是一条 track**](track-boundaries.md)
- [工作项登记表 —— 一个工作项，一个实施归属](work-items.md)
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
