# simpler 核心设计快照

发布日期：2026-11-04。当前 main `63ffeb8a8bd9b6527912fee87f6a8defdf82d5e0`；[L3+ 范围、证据与维护动作](review/track-2716.md)。

## 阅读入口

 | 目的 | 文档 |
 | ---- | ---- |
 | 完整目标与当前证据 | [#2716 范围分析](review/track-2716.md) |
 | 本地合同与验收状态 | [工作项](work-items.md) · [看板](roadmap.html) |
 | 设计原则与边界 | [L3+ 设计](l3plus-design.md) · [分线契约](track-boundaries.md) |
 | 目标来源与覆盖 | [来源清单](goal-sources.json) · [覆盖口径](goal-coverage.md) |
 | 内存与资源退休 | [内存模型](worker-memory-model.md) · [用户面](worker-memory-model/user-surface.md) · [退休契约](worker-memory-model/access-retirement-contract.md) |
 | 场景与里程碑 | [L3/L4](l3l4.md) · [总路线图](feature_roadmap.md) |

## 快照范围

- 主 checkout 的 `.docs` 是唯一维护真源，保持 gitignored；本目录仅是导出快照。
- 当前 #2716 原生树 30 节点 / 29 边；正文、逐条 checkbox 和决定评论共 199 个索引。根正文更新时间不能代替原生关系查询。
- 只采用目标 main 的 PR；本轮 59 条新合入。覆盖区间共 188 条合入；61 个开放 PR、107 个开放 issue 均已登记。
- 本地 11 条文档守卫通过；34 个合同工作项、568 个来源编号、80 个文档载体有归属，网页在桌面与手机宽度实看。编号完整不等于功能全部实现。
- 来源编号复核与当前具名代码/CI 核对日期为 2026-11-04；历史全量源码审计仍是 2026-10-18 的 `937c57efc71ad01a01c3a72072df075d2a5dd24f`，L3+ 旧代码地图仍是 2026-10-21 的 `fdcbdcc9502195f7837030cf3c130f75f83d3719`。没有把备份日期冒充所有平台重新验收。
- 本轮没有运行产品或硬件测试。CI 子用例证据使用日志中的实际 checkout SHA，与 PR head、当前 main 分别记录。
- 分支起点为 `11eb5d2c9ef8fe5a484a3d7c686d408ae5ff3e3b`；未选入材料的链接标为本地参考，文件来源和导出哈希见 [清单](SOURCE-MANIFEST.json)。HTML 下载后可交互查看。

## 当前设计与交付边界

- #2716 四方向承载完整目标：公共基础、通信与后端、单 L2 Run 常驻、多级多机部署。RA/DS/EX 是横切合同索引，必要的资源/执行/backend 改动随完整场景交付。
- 应用定义数据布局、消息和 queue 协议、模型策略；simpler 提供资源、访问、必要可见性/顺序、执行、完成和安全退休。必要通信 kernel、后端队列、注册和完成桥接在 runtime 范围内。
- #2639/#2668 已提供统一域 API、Worker ownership、Buffer/Tensor 消费与跨 run 使用登记；#2594 CLOSED，#2120 CLOSED 未合入且被公共 API 替代。#824 的原行为需要对照已有交付核定结项，不能再规划第二套 detach owner。
- #2779/#2790/#2825 分别交付 URMA 完成协议、workspace 分离与默认编译/使用前验证。#2674 的历史 query/enable 备选不再是当前待办；域资源仍显式申请。RDMA 资格、URMA/RDMA 异常清理、多 AIV、重叠域、batch/flush 分别由当前 issue 承接。
- 当前 URMA 限 A5 TMR L3 dense-prefix；五个 URMA ST 在具名 CI merge ref 实际通过，ChunkedFetch 因 #2860 跳过。#2854/#2860 间歇故障仍开放；#2858 为诊断。不能将旧成绩或 CI 总绿当作所有目标完成。
- #2656/#2665/#2741/#2792 已提供 Direct MPI session、逐 rank 报告、独立 health timeout 和 heartbeat drain；#2659 的外部持久账本、关闭期限、故障回收与 Group 迁移仍独立。普通 TCP/daemon 不预先删除。
- #2829 分离物理完成和结果观察；#2866 仅提供 backlog 计数/采样峰值，pipeline_depth 不封顶所有 caller-held 结果内存。
- #2720 Region 完整方案仍待分析，#2635 当前 head `05a058f9c8`，旧评审针对 `850a36bd9`，不能直接沿用旧缺陷结论。L3 进程复用、重复 run、单 L2 Run 多轮 work 分别验收。
- #2657 的 #2730 本地同步 MTE 与 #2731 跨 rank 异步 Host fetch 分开；A3 L2/L3/L4 Host 候选按拓扑和资源合同协调，局部路径不互相证明。
- #2429 当前归 #2666 的 vLLM 消费者交付，系统组合归 #2658；历史 VLLM/TrackA 编号保留原义，不重复开发已交付普通 Async。

## 历史证据阅读规则

“数据面未通”“必须独立 daemon”“缺 SHMEM 库接入”“ABORT 失败只写 stderr”“A5 RDMA backend 空白”等旧判断均有具名更正，不作为当前待办。旧文按其日期和 SHA 阅读，当前状态与证据见工作项及范围分析。通用 eager/capture、HBG 调度、DFX 与跨仓模型保持原外部归属；这不排除 L3+ 必需的 L2 通信接入。`simpler-pr-list.html` 是历史候选排期，未收入导出，相关编号仍有归属。

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
- [2716：范围、代码证据与维护动作](review/track-2716.md)
- [目标来源与正文指纹](goal-sources.json)
