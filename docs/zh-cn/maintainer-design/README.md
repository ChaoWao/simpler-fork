# simpler 核心设计快照

发布日期：2026-10-26。定点分析入口：[L3+ #2716 四方向、完整子树与下一动作](review/track-2716.md)，代码基线 main `daf2f8418907592b5b881792bd9a2a0a5ea8f19d`。

## 阅读顺序

 | 目的 | 入口 |
 | ---- | ---- |
 | 理解四方向范围和当前交付 | [#2716 范围分析](review/track-2716.md) |
 | 理解设计原则与代码关系 | [L3+ 设计](l3plus-design.md) |
 | 理解完整目标与横切合同 | [分线契约](track-boundaries.md) |
 | 查看本地合同、验收与具名证据 | [工作项](work-items.md) · [看板](roadmap.html) |
 | 核对来源编号与正文指纹 | [来源清单](goal-sources.json) · [覆盖口径](goal-coverage.md) |
 | 理解内存与资源退休 | [内存模型](worker-memory-model.md) · [用户面](worker-memory-model/user-surface.md) · [退休契约](worker-memory-model/access-retirement-contract.md) |
 | 查看场景和里程碑 | [L3/L4 场景](l3l4.md) · [总路线图](feature_roadmap.md) |

## 快照范围

- 主 checkout 的 `.docs` 是唯一维护真源，保持 gitignored；此目录是可分享的导出快照。
- #2716 本轮范围为 16 个原生节点、15 条父子边；93 个新增来源索引均有合同或验收承接。只纳入目标 main 的 PR，相关引用不自动扩大为后代或硬依赖。
- 当前 main 定点基线为 `daf2f8418907592b5b881792bd9a2a0a5ea8f19d`；源码证据、PR 状态和作者验证汇报分别记录，本轮未运行产品测试或硬件。
- L3+ 代码地图保留 2026-10-21 的 `fdcbdcc9502195f7837030cf3c130f75f83d3719`；全目标扫描仍为 2026-10-18 的 `937c57efc71ad01a01c3a72072df075d2a5dd24f`。这些日期不随备份日期自动前移。
- 本地 11 条文档守卫通过；129 条非空 main 合入区间及 127 个 open PR/issue 登记受检。34 个本地合同工作项、457 个来源编号、80 个文档载体有归属，不代表全部目标完成。
- 分支起点为 `11eb5d2c9ef8fe5a484a3d7c686d408ae5ff3e3b`，与本轮源码基线和历史硬件证据分别保留。
- 未收入的本地调查/工具链接标为本地参考；来源与导出哈希见 [清单](SOURCE-MANIFEST.json)。HTML 下载后可交互查看。

## 当前设计边界

- #2716 四方向承载完整业务目标：公共基础、通信与后端、单 L2 Run 常驻、多级多机部署。RA/DS/EX 是本地合同视图；场景所需的基础改动随场景交付，仅独立可验收的基础工作另列方向一。
- 应用定义布局、同步协议、queue 与模型策略；simpler 提供资源、访问、必要可见性/顺序、执行、完成和安全退休。必要的 L2 通信 kernel、后端队列、注册和完成桥接在范围内。
- #2720 的完整 Region 方案尚待分析，#2635 是候选实现；SPSC 样例不自动成为强制公共 ABI。
- L3 进程复用、重复 run、一个 L2 Run 多轮 work 分别验收。现有消息队列示例是方向三基座，通用常驻支持和故障退出仍须证明。
- #2665 提供 Direct MPI 逐 rank 清理观测，#2656 提供 session epoch 与 endpoint 关联；持久化、独立关闭期限、异常回收和 Group 迁移仍按 #2659 验收。普通 TCP/daemon 不预先删除。
- #2639 统一建域入口已合入；#2668 的 Buffer 消费/跨 run 使用在飞。#2714 本地同步读取不能关闭 #2657 跨 rank 异步 fetch；#2673/#2674 的并发队列与编译能力分别验收。
- 通用 eager/capture、HBG 硬件规划、DFX 与跨仓模型保留外部归属；不据此排除 L3+ 必需的 L2 通信接入。

## 证据边界与历史更正

- #2635 的旧评审针对 `850a36bd9`；当前 OPEN head `0bc9b0f690f3` 不自动继承旧缺陷结论。
- 历史“跨机数据面未通”“必须独立 daemon”“缺 SHMEM 库接入”“ABORT 失败只写 stderr”“A5 RDMA backend 空白”不能作为当前待办；具名更正保留在原文。
- #2672 的 64 字节边界校验已进入 main；它不证明通用 cache coherence 或 buffer 内任意信号布局正确。
- A3 Fabric 与 peer-TLOAD 的历史 CI 证据不能外推为当前 SHA、其他硬件或未执行用例的 PASS。
- `simpler-pr-list.html` 是历史候选排期，未纳入导出；来源编号仍在覆盖索引内。

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
- [2716：范围、现有交付与本地合同对照](review/track-2716.md)
- [目标来源与正文指纹](goal-sources.json)
