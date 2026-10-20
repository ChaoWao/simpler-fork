# simpler 核心设计快照

发布日期：2026-10-19。#2635 head `850a36bd9` 的定点评审建议 request changes：finalize identity/slot 复用竞态及 main `2a6d2d50c` 集成冲突；[完整评审](review/pr-2635.md)。全目标核验仍截至 2026-10-18。本目录是维护者本地设计材料的可分享快照，覆盖资源与访问（RA）、分布式会话与通信域（DS）、执行与常驻运行（EX）的契约和实施边界。

## 阅读顺序

 | 目的 | 入口 |
 | ---- | ---- |
 | 理解物理资源与逻辑 Worker 的关系 | [核心原理](logical-physical-separation.md) |
 | 理解三条实施线和四处交接合同 | [分线边界](track-boundaries.md) |
 | 查看 P1/P2/P3 的目标与退出条件 | [总路线图](feature_roadmap.md) |
 | 理解 Buffer、Tensor、Attachment 与授权 | [内存模型](worker-memory-model.md) · [用户接口](worker-memory-model/user-surface.md) |
 | 理解最后使用者、完成事实和释放条件 | [访问、完成与资源退休](worker-memory-model/access-retirement-contract.md) |
 | 理解跨节点启动、建域与数据访问 | [L3/L4 总览](l3l4.md) · [通信方案 D.4](l3l4/cluster-comm.md#d4-remote-l3-通信mpirun--共享窗口) |
 | 查看工作项及验收归属快照 | [工作项](work-items.md) · [目标覆盖](goal-coverage.md) |
 | 查看图形看板 | [roadmap.html](roadmap.html)；下载后用浏览器打开 |

## 快照范围

- 本次只发布设计材料。主 checkout 的 `.docs` 仍为维护真源，保持 gitignored；此目录不作为第二份实时状态表。
- 分支起点为 upstream/main `11eb5d2c9ef8fe5a484a3d7c686d408ae5ff3e3b`。文档全量状态核验基线为 `937c57efc71ad01a01c3a72072df075d2a5dd24f`；79 条 main 合入、28 个 main open PR、60 个 open issue 已登记。硬件与 CI 证据保留各自测试 SHA，分支起点不等于硬件验收基线。
- 本地 11 条守卫全绿；34 个实施项、364 个来源编号及 77 个文档载体均有归属（10 月 18 日全扫描）；10 月 19 日增加 #2635 定点评审，当前载体为 78 份。非空合入区间、缺登记/缺编号负对照与桌面/窄屏浏览器检查通过；目标有归属不等于全部实现。
- kernel 模式、HBG 内部实现、DFX 内部实现和外部仓算子开发不属于 RA/DS/EX 实施范围；保留相关交接契约。
- 原文指向未选入的调查、任务记录或工具时，以“本地参考”显示；核心设计之间的链接保留。完整文件与来源摘要见 [来源清单](SOURCE-MANIFEST.json)。

## 当前设计裁决

- mpirun 负责启动；PTO 通信从 CANN ACL Runtime 构建。SHMEM 仅为实现参考，不增加 SHMEM 库接入任务。
- A3 Fabric 共享窗口与 peer-TLOAD 已有实现并受双机 PR CI 看护；其他 backend、组合及生产故障闭环逐项验收。
- host 通过 capability-specific attachment 参与，不占 device rank；copy-capable 分支不等待 A5 direct-map。
- Buffer identity、授权、在飞保留和 completion 契约随各 consumer 交付；不以未来统一 registry 为由推迟保护。
- Buffer/domain registry 的具体组织、锁序和 journal 组合仍是契约 D 的待决项，不在本次发布中作新裁决。

## 已知更正与证据边界

以下结论来自 2026-10-18 全量复核；10 月 19 日另完成 #2635 定点评审，不推进全目标核验基线。历史调查不得覆盖工作项的当前结论。

1. `68d137fdf` 的 `_allocate_global_domain` 已对 MPI 与普通节点 ABORT 失败调用 `_record_unreclaimable`，并有故障注入测试。历史“只写 stderr、缺 poison”的判断已失效；这不等于远端窗口已完成确定性回收。
2. 同一基线上 A5 TMR 已有 RDMA completion backend 和 poll/retire 接入，不能写成“backend 空槽”。建链、提交、完成及平台覆盖应分别验收。
3. `#2496`、`#2497` 是 issue；`#2120`、`#2026` 在本轮查询时仍是面向 main 的开放 PR。`#2594` 的七条建域验收已归 DS-6 联验相关 RA/DS 项；`#2598` 的 cleanup 观测归 DS-1，不等于资源已回收。
4. 能力选择复用已有判定入口，按 backend 增量接入；不等待全部引擎完成才定义契约。不以新增 `HOST_MAPPED` 名字作为 copy-capable host attachment 的必需前置。
5. `simpler-pr-list.html` 是历史候选排期；其 42 项及本地别名均已纳入目标覆盖。历史数量和“立即开工”声明不作为当前交付承诺，该页面未收入本目录。

核验入口：[ABORT 失败处理](https://github.com/hw-native-sys/simpler/blob/68d137fdfd435d8067f0efd9836cb428da3e700a/python/simpler/worker.py#L10827)、[故障注入测试](https://github.com/hw-native-sys/simpler/blob/68d137fdfd435d8067f0efd9836cb428da3e700a/tests/ut/py/test_global_comm_domain.py#L669)、[RDMA completion 接入](https://github.com/hw-native-sys/simpler/blob/68d137fdfd435d8067f0efd9836cb428da3e700a/src/a5/runtime/tensormap_and_ringbuffer/runtime/async_wait.h#L95)。

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
