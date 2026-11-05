# 工作项登记表 —— 本地合同归属与验收证据

> **发布快照：2026-11-05。** 本轮为资源、部署通信、执行与跨仓目标的跟踪交接；不代表当前 main 全量验收。源码事实按各自 SHA 阅读，资源进度查 GitHub。参见[阅读入口](README.md)。
>
> **2026-11-04 状态与来源刷新**：main `63ffeb8a8bd9b6527912fee87f6a8defdf82d5e0`；全部本地编号重新推导，外部来源正文/决定性评论和 #2716 原生子树重新获取。新增交付、剩余合同及下一动作按本页和 [范围分析](review/track-2716.md)。下方 10 月 18 日保留为历史全量源码审计；当前具名代码/CI 核对不冒充所有平台的重新验收。
>
> **历史全量源码核验（2026-10-18）**：main `937c57efc71ad01a01c3a72072df075d2a5dd24f`；实现与验收证据分开记录，历史双机 CI 样本保留其实际测试 SHA。
>
> ## 本文是本地合同归属与验收证据的唯一真源；GitHub 是完整目标、实际 owner 与业务进度的真源
>
> 每个工作项**恰好属于一条实施线**。它可以被任意多个交付里程碑引用，
>
> ## 所有目标直接引用 GitHub，本地只保留合同、验收来源和历史证据，不维护重复进度
>
> 交付维度（用户已经能做什么、还缺哪项证据）在 [`README.md`](README.md) §2，
> 场景到工作项的映射在 [`l3l4.md`](l3l4.md)，GitHub 目标与验收入口 的职责和交接契约在
> [`track-boundaries.md`](track-boundaries.md)。
>
> **建表日期**：2026-09-07。**核验基线**：`upstream/main@937c57efc71ad01a01c3a72072df075d2a5dd24f`（2026-10-18）。
>
>
> **PR 范围**：只采用目标为 main 的 PR。全量状态、目标来源和代码证据按各自具名基线阅读；#2716 与本轮具名交付按 11 月 4 日 main；OPEN 不等于尚未实现，MERGED 不等于整个父目标完成。

## 0. 为什么会有这张表

重组前，「跨 run 域生命周期」这一件事在看板上有 **5 个格在各自维护状态**
（`P1-E` · `L3/L4 D.5` · `W3` · `W5` · `P2-D`，跨 4 条轨；实测 2026-09-07）。
后果不是重复劳动，是**判断被稀释**：`P2-D` 写着「被 P2-C 挡住」，`D.5` 写着「唯一残留」，
而 **PR #2120 已经在做它**，开了四天没人发现 —— 因为没有任何一格是它的家。

⇒ **判据**：**一个工作项一个实施归属；它可以支撑多个里程碑，但不能在几个地方分别维护状态。**
由 `check-single-owner.py`（本地参考：`check-single-owner.py`） 机器保证。

## 1. GitHub 交付方向与本地验收索引

| 线 | 前缀 | 统一负责 |
| -- | ---- | -------- |
| **资源与访问** | GitHub 入口 | §2 只记录设计、验收及跟踪链接，不设独立本地 track |
| **部署与通信** | GitHub 入口 | §3 分别引用 #2724 与 #2722；只保留验收与交接，不设本地进度 |
| **执行与常驻运行** | GitHub 入口 | §4 引用 #2723/#2658；公共合同 #2721 与部署 #2724 各自归属保持 |

旧资源、分布式和执行编号只用于历史交接；GitHub 入口承接完整目标。

### 1.1 #2716 的完整目标归属

| GitHub 方向 | 原生子项（唯一完整交付入口） | 本地合同与验收索引 |
| ----------- | ---------------------------- | ------------------ |
| #2721 公共基础 | #2460、#1931、#2720 | GitHub 公共资源、公共编排调度/任务下发；不吸收其他场景专用基础改动 |
| #2722 通信与后端 | 17 个直接子项；#2657 → #2730/#2731，#2708 → #2673/#2674；完整 30 节点树见 §6 与范围分析 | 通信场景与公共访问/寿命、执行 completion 交接 |
| #2723 L2 常驻执行 | 当前直接在方向 issue 分析方案；原生子列表为空，对照 #2721 的 3 项和 #2722 的 17 项 | #2723 主索引；#2720 等是消费合同 |
| #2724 L4 多级多机部署 | #2659 | #2724 会话与部署；通信域清理对接 #2722/#824，执行清理对接 历史清理证据（#1689） |

**归属判据**：一个完整场景目标留在一个 GitHub issue，可分多个 PR；因该场景需要而修改 Buffer、Region、scheduler 或 completion，不据此再拆方向 1。只有具有独立意义且能独立验收的公共基础才单列。方向编号不是执行顺序；阻塞必须指向具体产物。本地聚合行的依赖是其原合同退出条件，不自动成为新场景的硬前置；#2621 第三方 owner-local 扩展不等待 #2724/#2722 全量清点或原生 RoCE。既有 GitHub 目标与验收入口 的编号拆分仅保留为合同索引，不代表把 #2621/#2657/#2594 的实施进度再拆给多个 owner。根 track 的四方向验收是上层完整性检查，不新增一套本地实施轨。

## 2. 资源与访问：GitHub 入口

本节只索引设计与验收的跟踪位置，不维护本地资源任务、状态或排期。完整的 16 项历史交接见 [一次性对照](review/ra-handoff.md)；旧编号不能再作为活跃任务或依赖节点。

共同原则以 [#2721 正文](https://github.com/hw-native-sys/simpler/issues/2721) 为准：Buffer 描述存储，Tensor 是 Buffer 引用与几何描述，Worker 是逻辑角色；实际部署与 OS／驱动接入决定可访问性。共享映射按共享读写建立，任务方向服务依赖与 IO，不增加公共读写策略。绑定保留为运行时内部状态；映射、复制、就绪、完成和寿命分别表达。旧 access/wire 按 #2460 成对迁移，不能只删校验。

[设计澄清评论](https://github.com/hw-native-sys/simpler/issues/2721#issuecomment-6651217998)记录必要性待决的问题，不承诺公共 ResourceBundle、plan、capability 对象或两套 Arena。历史方案中与此冲突的实现形状仅作背景，不构成新增任务。

| GitHub 入口 | 设计主题 | 吸收的旧编号 | 范围与验收定位（非进度） |
| ----------- | -------- | ------------ | ------------------------ |
| [#2721](https://github.com/hw-native-sys/simpler/issues/2721) | 公共设计与待决问题 | `AR5` · `P1-E` 前半 · `AR9` · `ST-2` · `KV-R1` · `KV-R2` · `KV-R3` · `KV-R4` · `KV-R5` · `P1-E` 后半 · `AR10` · `W2` · `P3①` · `P3②` 本地半 · `P3③` · `l3l4 E` 公共执行半 · `P1-F` · `AF2` · `AF4` · `AF1` · `l3l4 C.3` 的 admission 半 · `l3l4 C.3` 的 heap 结构半 · `l3l4 C.2` 的 `run`/`orch`/callable 半 · `AF3` | 公共控制响应、Callable 身份、有界资源与寿命；设备标识、别名、访问及组合设计按两条澄清评论讨论，不预设零 heap 或 Arena。 |
| [#2460](https://github.com/hw-native-sys/simpler/issues/2460) | 统一模型、绑定、迁移与关闭 | `P1-C` · `W1lite` · `W1` · `AR6` · `U2a` · `P1-D` · `AR7` · `AR8` · `U1a` · `l3l4 B` · `AF5` · `U1b` · `U2b` · `ST-1` · `W8` · `C.2` 的 `malloc` 半 · `U4` | U1a/U1b/U2a/U2b/U4；#1931 身份校验、#2888 COW 清理；平台接入衔接 #2497，Remote 消费由 #2722 引用同一合同。 |
| [#2720](https://github.com/hw-native-sys/simpler/issues/2720) | Region 与基础同步 | `W4②` · `P3④` · `W4①` · `W6` | 共享区间、访问／可见性和必要同步；队列、发布协议与背压由应用定义。 |
| [#2496](https://github.com/hw-native-sys/simpler/issues/2496) | A5 映射能力 | `W0` | 归 #2495；只约束需要 A5 direct-map 的分支，不能阻止 copy-capable 路径。 |
| [#824](https://github.com/hw-native-sys/simpler/issues/824) | 通信域跨 run 寿命 | `P2-D` 本地半 · `W3` · `#824` 本地半 | 归 #2722；公共关闭与接纳合同引用 #2460，远端 lease／回收与 #2722 交接。 |
| [#2666](https://github.com/hw-native-sys/simpler/issues/2666) | 执行资源与消费者交接 | `VLLM-4` 的资源半 | 共同 prepare/launch/retain/release 及 workspace；#2883 资源分类、#2658 组合验收分别跟踪，通用 kernel 实现保持范围外。 |

依赖栏中的 `接口 #N〔具体合同〕` 是跨入口的接口交接要求，不表示等待整个 Issue 关闭；`条件 #N〔适用分支〕` 只约束该分支。GitHub 链接不是新的本地工作项。

## 3. 部署与通信：GitHub 入口

本节只索引既有方向的目标与验收，不维护 DS 独立进度。11 项旧目标与具名历史证据见 [交接对照](review/ds-handoff.md)。设计问题分别提交于 [#2724 评论](https://github.com/hw-native-sys/simpler/issues/2724#issuecomment-6652724365)和 [#2722 评论](https://github.com/hw-native-sys/simpler/issues/2722#issuecomment-6652726385)；评论发布不等于负责人已接受方案。

| GitHub 入口 | 设计主题 | 吸收的旧编号 | 范围与验收定位（非进度） |
| ----------- | -------- | ------------ | ------------------------ |
| [#2724](https://github.com/hw-native-sys/simpler/issues/2724) | Worker 部署、会话与任务路由 | `P2-A` · `P2-B` 清点的部署半 · `A.1` 残留 · `C.2` 的 L4 部署半 · `l3l4 E` 多机部署半 | mpirun 启动/发现、真实 endpoint/设备部署、session/epoch、连续任务、结果返回与退出观察；#2659 承接既有 supervisor/Direct MPI 清理。独立 device lease 的必要性待评论讨论。 |
| [#2722](https://github.com/hw-native-sys/simpler/issues/2722) | 通信域、数据传输与后端 | `P2-B` 清点的通信成员半 · `P2-C` · `U3` · `P2-D` 远端半 · `#824` 远端半 · `P2-E` · `D.2` 的 backend 半 · `D.4` · `C.2` 的 L4 域接口半 · `P3⑤` · `D.3` · `W5` · `P2-B` 的事务半 · `D.5` 残留 · `P3②` 跨节点半 · `D.2` 的同步/异步半 | 成员/rank、建域事务、域寿命、传输/完成、Host 场景、多域/跨节点与异常清理。64-rank 扩展保留为规模问题；接口与寿命引用 #2460/#824/#2721，不预设公共组合或授权对象。 |

部署结果是通信域构建的输入，成员/rank 是本域语义；两个方向通过 session/endpoint、完成及释放结果衔接。具体场景需要的成员一致性、事务、最后使用与失败回收按 [交接合同](review/ds-handoff.md#跨方向验收合同) 联验，不能把整个方向 Issue 的关闭当作统一前置。

## 4. 执行与常驻：GitHub 入口

七项目标、原验收与历史代码证据见 [静态交接](review/ex-handoff.md)，本地不再维护 EX 进度。公共执行、Callable 和生命周期沿用 §2 的 #2721；多机部署组合沿用 §3 的 #2724。设计问题见 [#2721 评论](https://github.com/hw-native-sys/simpler/issues/2721#issuecomment-6653477675)、[#2723 评论](https://github.com/hw-native-sys/simpler/issues/2723#issuecomment-6653480374)、[#2658 评论](https://github.com/hw-native-sys/simpler/issues/2658#issuecomment-6653482776)，发布不等于接受。

| GitHub 入口 | 设计主题 | 吸收的旧编号 | 范围与验收定位（非进度） |
| ----------- | -------- | ------------ | ------------------------ |
| [#2723](https://github.com/hw-native-sys/simpler/issues/2723) | 单次 L2 Run 常驻 | `W7` | 多轮 work/completion、复用与背压、STOP/drain/异常退出；不把进程复用或重复 Run 当常驻证据。 |
| [#2658](https://github.com/hw-native-sys/simpler/issues/2658) | 连续执行的真实模型组合 | `TrackA-P1` · `TrackA-P2` · `TrackA-P3` · `TrackA-P4` · `TrackA-P5` · `TrackA-P6` · `VLLM-1` · `VLLM-2` · `VLLM-3` · `VLLM-4` 的 admission 半 | #1853/#2316 已关闭，基础交付归历史证据；剩余 M2/M3 组合、持续 refill、资源峰值与性能由本入口跟踪。#2429 仅 vLLM 消费者，#2666 执行资源交接。 |

零 hierarchical heap、两套 Arena、删除 scope 回收均为待论证方案。必要目标是寿命正确、有限压力下背压、完成后可复用且无无界滞留。历史 P0.2 清理已交付，见 §5；不通过重新编号恢复待办。

## 5. 已交付，保留为验收证据（不另建工作项）

`P0.2-f4` · `P0.2-f5` · `P0.2-f6` · `P0.2-f7`：#1689 `f8106e24e` 的终态清理、child freeze、nested-shm/journal 证据归 [执行交接](review/ex-handoff.md)，保留原核验范围，不重开实施项。

`P1-A` 冻结 identity/wire 字节布局（#1599 #1703 #1704）· `P1-B` owner registry ＋ 生命周期 ＋ wire 切换（#1729 等）·
`C.1` L3 创建即拉起 L2（#1397 ＋ P0.2 加固）· `D.1` L3 为多 L2 建通信域（#817 #1424）·
审计清单 `AR1`（#1953 交付 `select_adapter`）· `AR2` `AR3` `AR4`（#1850 ＋ #1864 全闭）·
`P1-A` 的六道 merge gate `G1` `G2` `G3` `G4` `G5` `G6`（随 `P1-A` 交付，G2 由 #1430 `c8c7b735` 收口）·
Host trace / DFX 的 `T5`（#1811 `0d74814d` 等）与 `T7`（本地 host/device containment 已交付；跨 host 残留见 §7 DFX）·
`P0.1` / `P0.2` / `P0.3` 三条里程碑 · `Worker 异步准备流水` 15 格 · `Host trace / DFX` 4 格。

编号家：已交付里程碑与流水线的子编号。闭环证据见 tasks/README.md 与 implementation-record；这些是既有完成轨的细分，不重新排期。

`P0.1` · `P0.2` · `P0.2-D0` · `P0.2-D1` · `P0.2-D2` · `P0.2-D3` · `P0.2-a` · `P0.2-b1` · `P0.2-b2` · `P0.2-c` · `P0.2-d` · `P0.2-f1` · `P0.2-f2` · `P0.2-f3` · `P0.3` · `P0.3-R1` · `P0.3-R2` · `P0.3-R3` · `P0.3-R4` · `PIPE-A0` · `PIPE-A0b` · `PIPE-A1` · `PIPE-B1` · `PIPE-B2` · `PIPE-B3` · `PIPE-B3a` · `PIPE-B3b` · `PIPE-B4` · `PIPE-B5` · `PIPE-B6` · `PIPE-B6a` · `PIPE-B6b` · `PIPE-B6c` · `PIPE-D1` · `PIPE-E1` · `PIPE-E2` · `PIPE-H1` · `PIPE-H2` · `PIPE-N1` · `PIPE-W1a` · `PIPE-W1b` · `PIPE-W1c` · `PIPE-W1d` · `PIPE-E1′` · `PIPE-C2a` · `T1` · `T2` · `T3` · `T4` · `T5`

`P1A-A1` · `P1A-A2`：P1-A 的 blob 容量断言与 same-build 校验，归既有 P1-A 交付；当前 main 分别核到 worker_manager.h 的 sizeof(Tensor) 上界和 task_interface.py 的 **build_commit** 比较。

## 6. 约束，不是工作项

有些条目读起来像工作项，其实是**加在某个工作项上的验收条件**。给它们建格会得到一个
永远无法「做完」的格 —— 它们没有交付物，只有「凡交付必须满足」。

| 约束 | 原编号 | 加在谁身上 |
| ---- | ------ | ---------- |
| capability view **只在对应 backend 完成功能、故障和硬件验收后**才实现；不为未落地能力发布空壳资源或伪成功接口 | `P3⑥` | `#2721`，并经它传导到 `#2721` / `#2721` |
| 不以「统一内存」名义提供隐式 coherence；`copy_to`/`copy_from` 保持既有 Buffer identity，任何 Transfer 创建的新 identity 必须显式可见 | `P3` 退出条件 | `#2460` · `#2721` |
| queue schema / opcode / batching / backpressure policy **不进 Buffer/Attachment 基础 ABI** | `P3④` 的边界 | `#2720` |

契约编号家：GE-R1/R2 → #2460/#824/#2722；GE-R3 → #2721；GE-R4 → #2658；GE-R5/R6 → 外部 GE 实现（交接要求保留）。VLLM-S3-1…7 全部作为 #2658 的 clone-request 验收，资源退休与 #2666 联验。

`GE-R1` · `GE-R2` · `GE-R3` · `GE-R4` · `GE-R5` · `GE-R6` · `VLLM-S3-1` · `VLLM-S3-2` · `VLLM-S3-3` · `VLLM-S3-4` · `VLLM-S3-5` · `VLLM-S3-6` · `VLLM-S3-7`

`EE-QA-1` · `EE-QA-2` · `EE-QA-3` · `EE-QA-4` · `EE-QA-5`：#2425 的五条 early-enqueue 验收 → #2658；其他不同根因症状不合并为一个缺陷。

`CAP-R1` · `CAP-R2` · `CAP-R3`：#2466 合入 `fb76a54d5` 的 late-preparation 发布、默认容量/范围、三 run 顺序与 16-run refill 约束 → #2658；默认资源容量与 #2666 交接。

跨阶段验收义务：权威细节见 [访问、完成与资源退休契约](worker-memory-model/access-retirement-contract.md)，推导及来源见 [设计评估](review/2026-09-28-cross-plan-lessons.md)。以下约束随对应 consumer 交付，不以 U4 或 kernel 实现为前置：

| 既有归属 | 必须满足的验收义务 |
| -------- | ------------------ |
| #2722 → #2460/#2666 | 分别证明源不再被读、目的可消费、操作已静止；延迟接收/H2D、第二段失败和旧完成记录不得提前放行或回收。 |
| #2460；#2722 | 全调用/路径预检在数据副作用前完成；绑定点原子取得使用保留；mixed owned/external 与未知完成安全随 consumer 交付，不等 U4。 |
| #2658 → #2666 | 分开核 accepted/prepare/enqueue/执行容量；只发出 wait 尚未 launch、launch 后 fence record 失败仍保留资源；正常完成后须有可复用阳性对照。 |
| #2666；#2658 联验 | 真实 producer 与滞后 consumer 的多轮复用；核旧版本、graph 使用引用、释放失败和各预算域；不把 CPU 协议验证当作 device 可见性证明。 |

\#2460 遵循 [用户面契约](worker-memory-model/user-surface.md) 的源登记/目标接入分层，`Tensor(buffer, shapes=...)` 已由 #2479/#2480 提供，#2482 的 `Buffer.wrap` 已支持 direct-L2；其他 context provider、consumer 与兼容移除边界由 #2460 继续承接。#2460/#824 按契约 D 共享生命周期规则；registry 具体组织、锁序与 journal 组合仍待裁决。设计约束不代替交付状态。

\#2460/#824/#2722 的联验须覆盖实际映射的使用保留、graph/replay 引用及 unmap/free 失败；来源与反例见共同契约 §3/§5。#2666/#2658 的 CPU 协议验收与硬件可见性证据分别给出。

### 外部能力的接入验收（2026-10-07 来源快照）

以下 A 编号按来源 acceptance checklist 顺序生成，R 编号对应来源原有 Proposed Fix 1–4；是验收归属，不新建实施项。

| 来源与完整编号 | 唯一承接 | 范围 |
| -------------- | -------- | ---- |
| #2496：`A5SVM-A1` · `A5SVM-A2` · `A5SVM-A3` · `A5SVM-A4` · `A5SVM-A5` · `A5SVM-A6` | #2496 | 支持版本、注册/可见性/释放、平台接入、DFX、HBG 访问、对比；#2460/#2720 提供接入与可见性契约 |
| #2497：`XPSVM-A1` · `XPSVM-A2` · `XPSVM-A3` · `XPSVM-A4` · `XPSVM-A5` · `XPSVM-A6` · `XPSVM-A7` · `XPSVM-A8` | #2460 | import/registration 组合、两进程演示、双向可见性、权限范围、寿命与 reset、probe/materializer、真实用例、分 arch 证据；#2721/#2460 联验 |
| #2429 的 `PR-S3-1` | #2658 | #2471 是程序身份/lease 验收子集；不代替真实 Qwen clone-request adapter |

### 本轮新增目标与候选排期归属（2026-10-18）

候选排期来自 simpler-pr-list（本地参考：`simpler-pr-list.html`），用 `SPL-` 命名空间避免与阶段编号混淆；编号是验收/能力目标，不承诺一条一 PR。阻塞只约束完整退出，不禁止已有路径先行。

| 来源编号 | 唯一主归属 | 交接/边界 |
| -------- | ---------- | --------- |
| `SPL-1.1` · `SPL-1.2` | #2724 | cleanup ledger 与 epoch/lease 分开；#2598 仅观测 |
| `SPL-1.3` | #2724/#2722 | 按本项既有契约验收 |
| `SPL-1.4` · `SPL-1.6` | #2722 | SPL-1.6 poison 已有，保留失败回收验收，不重复开发 |
| `SPL-1.5` · `SPL-1.9` | #2724/#2722 | 按本项既有契约验收 |
| `SPL-1.7` | #824 | 按本项既有契约验收 |
| `SPL-1.8` | #2722 | 按本项既有契约验收 |
| `SPL-2.1` · `SPL-2.2` · `SPL-3.1` · `SPL-3.2` · `SPL-3.3` · `SPL-3.5` · `SPL-3.8` · `SPL-3.9` | #2722 | CCU/MTE 与机型适配为候选能力；RDMA producer 已有，先验能力再排实现 |
| `SPL-3.4` · `SPL-3.6` · `SPL-3.7` · `SPL-3.10` | #2721 | 机型/链路是共同 capability 输入；不新建第二选择器 |
| `SPL-4.1` · `SPL-4.5` | #2460 | 按本项既有契约验收 |
| `SPL-4.2` · `SPL-4.3` · `SPL-4.6` | #2722 | host 为 attachment 不占 rank；copy 分支不等 direct-map |
| `SPL-4.4` | #2496 | 按本项既有契约验收 |
| `SPL-5.1` · `SPL-5.2` · `SPL-5.3` · `SPL-5.4` · `SPL-5.5` · `SPL-5.6` · `SPL-5.7` · `SPL-5.8` | #2722 | 完成事实联验 #2722；内部 workspace 联验 #2666 |
| `DOMAIN-A1` · `DOMAIN-A3` · `DOMAIN-A4` · `DOMAIN-A7` | #2722 | #2594 接口/配置/事务竞争/两层端到端；#2460、#2722 联验 |
| `DOMAIN-A2` | #824 | #2594 同名同配置复用 identity/内容/handle；与 #2120 共用 owner |
| `DOMAIN-A5` | #2460 | #2594 域 Buffer 经 L3 到正确 chip；#2722 负责远端 request/grant |
| `DOMAIN-A6` | #2460 | #2594 跨 run/borrower/close 最后使用者；#824、#2722 联验 |
| `SESSION-R1` · `SESSION-R2` · `SESSION-R3` | #2724 | #2598 command acknowledgement/process exit/resource release 三种结果独立；不是远端 lease 完成声明 |
| `VLLM-S3F-S1` · `VLLM-S3F-S2` · `VLLM-S3F-S3` · `VLLM-S3F-S4` · `VLLM-S3F-S5` · `VLLM-S3F-A1` · `VLLM-S3F-A2` · `VLLM-S3F-A3` · `VLLM-S3F-A4` · `VLLM-S3F-A5` · `VLLM-S3F-A6` | #2658 | #2429 最终 Step 3 评论：#2666 资源联验；adapter/硬件运行由 Qwen 轨实施，不以 #2592 关闭自动组合 |

SPL 本地别名归一：`SPL-C4`/`SPL-C5`/`SPL-C8`/`SPL-C11`/`SPL-C12`/`SPL-C13`/`SPL-C14` → #2722；`SPL-C6`/`SPL-C9`/`SPL-C10` → #2721；`SPL-C7` → #2722；`SPL-M2` → #2722。公共 backend 能力先证明再排期，不因旧表写“新建”而新增重复 provider。

### #2716 原生子树与决定评论的完整目标索引（2026-11-04）

来源覆盖不表示实现完成。30 节点 / 29 边按逐父子集合核对；checkbox 逐字保存，历史 R 编号不因评论改向而换义。#2673 batch 转 #2855；#2674 的旧 query/enable 备选不再是待办。

| 来源编号 | 本地合同索引 | 业务归属 |
| -------- | ------------ | -------- |
| `T824-SCOPE` | #824 | #824 完整目标，父项 #2722 |
| `T1931-SCOPE` | #2460 | #1931 完整目标，父项 #2721 |
| `T2460-SCOPE` · `T2460-A1` · `T2460-A2` · `T2460-A3` · `T2460-A4` · `T2460-A5` · `T2460-A6` · `T2460-A7` · `T2460-A8` · `T2460-A9` | #2460（U1a–U4 原归属保留） | #2460 完整目标，父项 #2721 |
| `T2594-SCOPE` · `T2594-A1` · `T2594-A2` · `T2594-A3` · `T2594-A4` · `T2594-A5` · `T2594-A6` · `T2594-A7` | #2722 | #2594 完整目标，父项 #2722 |
| `T2621-SCOPE` · `T2621-A1` · `T2621-A2` · `T2621-A3` · `T2621-A4` · `T2621-A5` | #2722 | #2621 完整目标，父项 #2722 |
| `T2657-SCOPE` · `T2657-A1` · `T2657-A2` · `T2657-A3` · `T2657-A4` · `T2657-A5` · `T2657-A6` · `T2657-A7` · `T2657-R1` · `T2657-R2` · `T2657-R3` · `T2657-R4` · `T2657-R5` | #2722 | #2657 完整目标，父项 #2722 |
| `T2659-SCOPE` · `T2659-A1` · `T2659-A2` · `T2659-A3` · `T2659-A4` · `T2659-A5` · `T2659-A6` · `T2659-A7` · `T2659-A8` · `T2659-A9` · `T2659-A10` · `T2659-A11` · `T2659-A12` | #2724 | #2659 完整目标，父项 #2724 |
| `T2673-SCOPE` · `T2673-R1` · `T2673-R2` · `T2673-R3` | #2722 | #2673 完整目标，父项 #2708；R3 batch 已转 #2855，当前并发范围以决定评论为准 |
| `T2674-SCOPE` · `T2674-R1` · `T2674-R2` | #2722 | #2674 完整目标，父项 #2708；R1/R2 历史 API 备选已被默认编译/使用校验方案替代，非剩余工作 |
| `T2708-SCOPE` | #2722 | #2708 完整目标，父项 #2722 |
| `T2716-SCOPE` · `T2716-A1` · `T2716-A2` · `T2716-A3` · `T2716-A4` · `T2716-A5` · `T2716-A6` | 四方向共同验收 | #2716 完整目标，父项 总入口 |
| `T2720-SCOPE` · `T2720-A1` · `T2720-A2` · `T2720-A3` · `T2720-A4` | #2720 | #2720 完整目标，父项 #2721 |
| `T2721-SCOPE` · `T2721-A1` · `T2721-A2` · `T2721-A3` · `T2721-A4` · `T2721-A5` · `T2721-A6` · `T2721-A7` · `T2721-A8` | 资源合同与 公共执行合同 | #2721 完整目标，父项 #2716 |
| `T2722-SCOPE` · `T2722-A1` · `T2722-A2` · `T2722-A3` · `T2722-A4` | #2722 | #2722 完整目标，父项 #2716 |
| `T2723-SCOPE` · `T2723-A1` · `T2723-A2` · `T2723-A3` · `T2723-A4` | #2723 | #2723 完整目标，父项 #2716 |
| `T2724-SCOPE` · `T2724-A1` · `T2724-A2` · `T2724-A3` · `T2724-A4` · `T2724-A5` | #2724 | #2724 完整目标，父项 #2716 |
| `T2730-SCOPE` · `T2730-A1` · `T2730-A2` · `T2730-A3` · `T2730-A4` | #2722 | #2730 完整目标，父项 #2657 |
| `T2731-SCOPE` · `T2731-A1` · `T2731-A2` · `T2731-A3` · `T2731-A4` · `T2731-A5` · `T2731-A6` · `T2731-A7` · `T2731-A8` · `T2731-A9` | #2722 | #2731 完整目标，父项 #2657 |
| `T2738-SCOPE` · `T2738-A1` · `T2738-A2` · `T2738-A3` · `T2738-A4` · `T2738-A5` · `T2738-A6` · `T2738-A7` | #2722 | #2738 完整目标，父项 #2722 |
| `T2739-SCOPE` · `T2739-A1` · `T2739-A2` · `T2739-A3` · `T2739-A4` · `T2739-A5` · `T2739-A6` · `T2739-A7` | #2722 | #2739 完整目标，父项 #2722 |
| `T2775-SCOPE` · `T2775-A1` · `T2775-A2` · `T2775-A3` · `T2775-A4` · `T2775-A5` · `T2775-A6` · `T2775-A7` · `T2775-A8` | #2722 | #2775 完整目标，父项 #2722 |
| `T2783-SCOPE` | #2722 | #2783 完整目标，父项 #2722 |
| `T2787-SCOPE` · `T2787-A1` · `T2787-A2` · `T2787-A3` · `T2787-A4` · `T2787-A5` · `T2787-A6` · `T2787-A7` · `T2787-A8` · `T2787-A9` · `T2787-A10` · `T2787-L1` · `T2787-L2` · `T2787-L3` · `T2787-L4` | #2460 / #2722 | #2787 完整目标，父项 #2722 |
| `T2800-SCOPE` | #2722 | #2800 完整目标，父项 #2722 |
| `T2823-SCOPE` · `T2823-A1` · `T2823-A2` · `T2823-A3` · `T2823-A4` · `T2823-A5` · `T2823-A6` | #2460 / #2722 | #2823 完整目标，父项 #2722 |
| `T2824-SCOPE` · `T2824-A1` · `T2824-A2` · `T2824-A3` · `T2824-A4` · `T2824-A5` | #2722 | #2824 完整目标，父项 #2722 |
| `T2833-SCOPE` · `T2833-A1` · `T2833-A2` · `T2833-A3` · `T2833-A4` · `T2833-A5` · `T2833-A6` · `T2833-A7` · `T2833-A8` | #2722 | #2833 完整目标，父项 #2722 |
| `T2854-SCOPE` | #2722 | #2854 完整目标，父项 #2722 |
| `T2855-SCOPE` · `T2855-A1` · `T2855-A2` · `T2855-A3` · `T2855-A4` · `T2855-A5` · `T2855-A6` · `T2855-A7` · `T2855-A8` · `T2855-A9` · `T2855-R1` · `T2855-R2` · `T2855-R3` · `T2855-R4` · `T2855-R5` · `T2855-R6` | #2722 | #2855 完整目标，父项 #2722 |
| `T2860-SCOPE` | #2722 | #2860 完整目标，父项 #2722 |
| `C6599735013-SCOPE` · `C6599735013-A1` · `C6599735013-A2` · `C6599735013-A3` · `C6599735013-A4` · `C6599735013-R1` · `C6599735013-R2` · `C6599735013-R3` | #2722 | #2673 范围决定；正文历史编号保留溯源 |
| `C6492946320-SCOPE` | #2722 | #2674 范围决定；正文历史编号保留溯源 |
| `VLLM-CURRENT-A1` · `VLLM-CURRENT-A2` · `VLLM-CURRENT-A3` · `VLLM-CURRENT-A4` · `VLLM-CURRENT-A5` | 范围外 #2666 → #2429；#2658 / #2666 接口联验 | vLLM 完整数值/请求生命周期/产物；系统组合 #2658，非重新开发普通 Async |

### Host DDR 当前合同与历史编号迁移（2026-11-04）

\#2657/#2730/#2731 的正文已实质修订；旧编号与原验收文字不换义，当前采用 V2 命名空间。完整业务 owner 仍在方向二，#2722 是本地合同索引。

| 来源编号 | 本地合同索引 | 业务归属 |
| -------- | ------------ | -------- |
| `T2657-V2-SCOPE` · `T2657-V2-A1` · `T2657-V2-A2` · `T2657-V2-A3` · `T2657-V2-A4` · `T2657-V2-A5` · `T2657-V2-A6` · `T2657-V2-A7` · `T2657-V2-A8` | #2722；#2496/#2460 与 执行 completion 联验 | #2657 当前合同；旧 A/R 编号为历史目标，范围差异按下方迁移表 |
| `T2730-V2-SCOPE` · `T2730-V2-A1` · `T2730-V2-A2` · `T2730-V2-A3` · `T2730-V2-A4` · `T2730-V2-A5` · `T2730-V2-A6` · `T2730-V2-A7` · `T2730-V2-A8` · `T2730-V2-A9` | #2722；#2496/#2460 与 执行 completion 联验 | #2730 当前合同；旧 A/R 编号为历史目标，范围差异按下方迁移表 |
| `T2731-V2-SCOPE` · `T2731-V2-A1` · `T2731-V2-A2` · `T2731-V2-A3` · `T2731-V2-A4` · `T2731-V2-A5` · `T2731-V2-A6` · `T2731-V2-A7` · `T2731-V2-A8` · `T2731-V2-A9` · `T2731-V2-A10` · `T2731-V2-A11` · `T2731-V2-A12` | #2722；#2496/#2460 与 执行 completion 联验 | #2731 当前合同；旧 A/R 编号为历史目标，范围差异按下方迁移表 |

| 历史范围 | 当前去向 |
| -------- | -------- |
| #2657 原持久 Host、稀疏读取、异步完成、并发/错误与 overlap 验收 | V2 父验收及 #2730/#2731；两子项不自动关闭父项 |
| #2730 allocation-wide device_readable/read-only 模型 | 被显式 mapped view 替代；native Host/H2D 和原 grant 保持 |
| #2731 原自动分片/多表假设、旧固定依赖 head | 首版所有子 L2 注册同一完整 Buffer；每 consumer 一套 Host registration，第二独立 backing 拒绝；基于 #2730 最终合同，不冻结旧 PR head |
| 原本地/跨 rank读取与 BF16/MXFP8、边界、延迟完成、无整表复制要求 | 保留于当前 V2 验收；真实非 self PTO URMA 与有用计算重叠须独立证据 |
| 子集/懒准备、多独立 Host workspace、L4/跨节点、独立 L2 URMA loopback | 明确不属于当前两阶段首版；保留需求来源，不算已实现；L4 部署关联 #2724，扩展范围须另行明确 |

## 7. 既定范围之外的工作

本重组排除通用 kernel 模式/eager-capture、HBG 硬件规划及其内部调度工程；**L3+ 场景必需的 L2 资源访问、通信初始化、通信 kernel 与 completion 不在排除之列**，按 #2716 的完整目标归属。因此下列通用工作不进入L3+ 设计范围，
但 [`README.md`](README.md) §1.1「目标 main 的 PR / issue 都要占一行」**仍然适用** ——
它们登记在 §1.2「不在本体系内」：

- **kernel 模式**：维持用户既定排除；独立栈按 §1.2 登记，完整编号与原则见 [覆盖清单](goal-coverage.md)。是否第四线仍待用户裁决，不能用栈已合入推导批准。
- **runtime / 调度器与 HBG 硬件需求**：GE 内部实现和硬件预算归各自维护者；我们的 GE-R1–R6 交接要求仍在 §6，不能随排除项消失。
- **DFX**：`VLLM-5`（continuous DFX）由 collector/runtime 的独立交付负责；#2588 已合入 `545c05a43469`，具体验收仍按 collector/backend，不能由普通 run 的 early enqueue 推导 DFX 完成。#2465 `02fd1f63e` 与 #2483 `92a14e4e7` 已分别交付 scope-stats、TMR dep-gen 后台输出，#2484 `8aae172e5` 修 A5 错误关闭的 run identity 传参；#2490 `01bf24bfa` 已交付 HBG 后台依赖图输出，发布证明来自 host capture，区别于 TMR device record 的完成/隔离条件；run 返回与产物发布分离，flush_diagnostics 是产物屏障。它们不等于任意 collector/runtime 的 continuous DFX 全部完成。T7 跨 host 部分由 #2217/#2304 跟踪，不能用本地 #2152/#2218 的交付关掉 #2134 全范围。
- **Qwen 跨仓整体验收**：`Q1` `Q2` `Q3` `Q4` `C2b` 的唯一整体跟踪入口为 [#2658](https://github.com/hw-native-sys/simpler/issues/2658)；[交接对照](review/qwen-handoff.md)保留各项验收与旧方案边界，本地不另记进度。真实模型 admission 组合验收归 #2658，执行资源合同归 #2666；基础能力合入不替代真实模型、持续请求与端到端性能验收。
- **vLLM 消费者接入**：`V1`、`VLLM-6` `VLLM-7` `VLLM-8` `VLLM-9` 按原来源分项，adapter/capture 消费者交付归 #2429/#2666；系统组合与性能交接 #2658。原编号保留原义，不把实现范围与系统验收重复排期。
- **CI / 文档 / 打包**：按 README 登记，不建实施阶段。

⚠️ **这不是「不重要」，是「不由L3+ 设计范围单独管辖」。** 把它们塞进L3+ 设计范围会让
「一个工作项一个归属」这条判据失去意义 —— 因为它们的归属在别的仓或别的团队。

`T6` · `T8` · `T9`：DFX 备选方案，按既定范围裁决不排入实施线；不是已交付声明。

明确排除的编号家（不是实现完成声明）：

`C2b` · `KERNEL-10a` · `KERNEL-2B` · `KERNEL-2a` · `KERNEL-Binder` · `KERNEL-H1` · `KERNEL-H2` · `KERNEL-H3` · `KERNEL-K2` · `KERNEL-K3` · `KERNEL-K4` · `KERNEL-K5` · `KERNEL-K7` · `KERNEL-Principle-01` · `KERNEL-Principle-02` · `KERNEL-Principle-03` · `KERNEL-Principle-04` · `KERNEL-Principle-05` · `KERNEL-Principle-06` · `KERNEL-Principle-07` · `KERNEL-Principle-08` · `KERNEL-Principle-09` · `KERNEL-Principle-10` · `KERNEL-Principle-11` · `PIPE-C1` · `Q1` · `Q2` · `Q3` · `Q4` · `RequestSession` · `V1` · `VLLM-5` · `VLLM-6` · `VLLM-7` · `VLLM-8` · `VLLM-9`

历史与范围外的细编号（保留出处和继任关系，不把副本的“未做”当作当前状态）：

- 日志参考需求归外部 DFX/log 合同：F1–F5/F7 的观测面接 T1–T5 与 Host Swimlane；F6 的跨线程 flow 属 T9 备选边界；D5/T4 采用 T7 containment，不恢复已撤回的 clock-alignment 路线；C/N/G 与 T1–T3/T5 是 #1792/#1793/#1794 的约束及取舍来源。它们不与 P1 的 G1–G6 或流水线的 T1–T5 混名，也不整体宣称已交付。
  `LOG-C2` · `LOG-C4` · `LOG-C5` · `LOG-C6` · `LOG-C7` · `LOG-D5` · `LOG-F1` · `LOG-F2` · `LOG-F3` · `LOG-F4` · `LOG-F5` · `LOG-F6` · `LOG-F7` · `LOG-G1` · `LOG-G10` · `LOG-G11` · `LOG-G11b` · `LOG-G12` · `LOG-G13` · `LOG-G14` · `LOG-G15` · `LOG-G16` · `LOG-G17` · `LOG-G18` · `LOG-G19` · `LOG-G2` · `LOG-G20` · `LOG-G21` · `LOG-G22` · `LOG-G23` · `LOG-G3` · `LOG-G4a` · `LOG-G4b` · `LOG-G6` · `LOG-G6b` · `LOG-G7` · `LOG-G8` · `LOG-G9` · `LOG-N1` · `LOG-N2` · `LOG-N3` · `LOG-N4` · `LOG-T1` · `LOG-T2` · `LOG-T3` · `LOG-T4` · `LOG-T5` · `LOG-I1` · `LOG-I2` · `LOG-I3` · `LOG-I4` · `LOG-I5` · `LOG-I6` · `LOG-I7`
- 旧流水线 W1–W4 方案已退役；HBG prepare、native lane、TMR prepare、progress 分别由 PIPE-B4、PIPE-W1a、PIPE-B5、PIPE-B3/B6 继任，原 PR 是否合入与继任交付不是同一问题。
  `PIPE-v1-W1` · `PIPE-v1-W2` · `PIPE-v1-W3` · `PIPE-v1-W4`
- kernel-memory 的 D1–D6 是范围外历史方案；约束可供 #2666/#2658 参考，callable 容量、restore 与 stream/event 具体方案不成为本板承诺。
  `KERNEL-Memory-D1` · `KERNEL-Memory-D2` · `KERNEL-Memory-D3` · `KERNEL-Memory-D4` · `KERNEL-Memory-D5` · `KERNEL-Memory-D6`

外部 runtime/CANN 的独立验收：#2498 的 `SIMT-A1` · `SIMT-A2` · `SIMT-A3` · `SIMT-A4` · `SIMT-A5`（官方接口、接入去锚点、注册、SIMD/SIMT 混合、A5 验证）；#2492 的 `A2RET-R1` · `A2RET-R2` · `A2RET-R3` · `A2RET-R4`（TMR/HBG 顺序实验、pending handoff、保留退出顺序、分层验证）均明确范围外。#2495 是外部依赖总账，#1425 SDMA 故障恢复与 #905 perfmon 继续归外部 runtime/DFX；总账不形成第四实施线。

候选跨仓编号 `SPL-P.1` · `SPL-P.2` · `SPL-P.3` · `SPL-E1` 归外部 pypto；`SPL-L.1` · `SPL-L.2` · `SPL-L.3` · `SPL-L.4` · `SPL-E2` 归外部 pypto-lib，交接分别为 #2724/#2722 与 #2721/#2722。它们是候选范围，不表示本轮跨仓代码或硬件已验收。

## 8. 跨入口交接合同（机器检查）

下表只表达具体产物及适用条件，不建立本地实施 DAG，不要求整个 Issue 关闭。`接口` 必须写清合同，`条件` 必须写明分支。资源清理、常驻与系统组合仍按消费者场景联验。

| 消费入口 | 提供入口 | 关系 | 具体产物／适用条件 |
| -------- | -------- | ---- | ------------------ |
| #2723 | #2720 | 接口 | 公共区间、实际访问与必要同步；queue/发布由应用定义 |
| #2723 | #2721 | 接口 | 长任务中控制响应与完成/退出合同，不预设线程或双帧 |
| #2722 | #2724 | 接口 | 真实 session/endpoint/部署结果，不等待全量部署工作 |
| #2722 | #824 | 接口 | 域最后使用、关闭接纳与失败回收结果 |
| #2658 | #2666 | 条件 | 选用 kernel/capture 路径时核具体 prepare/launch/retain/release 产物；普通 program 不等全部 capture |
| #2658 | #2721 | 接口 | 公共使用寿命、有界资源和实际最后消费者，不强制零 heap |
| #2460 | #2496 | 条件 | 仅选用 A5 direct-map 分支时核实实际映射能力，不阻止 copy 路径 |

## 9. 引用规则

别处提到一个工作项时，**只写 ID ＋ 一句话，不复述状态**：

```markdown
（→ [`#824`](work-items.md#2-资源与访问ra)：本地 per-worker domain ownership）
```

⛔ **反例**（重组前 `P2-D` 的写法）：把「被 P2-C 挡住」「唯一残留」「无人认领」
写进四个不同的格 —— 其中三个在 #2120 开出来之后就错了，而**没有任何一处会被更新**，
因为更新哪一处都不是「唯一真源」。

## 10. 依赖图的具体关系

[交互式依赖图](dependency-dag.html) 按提供方 → 消费方绘制，原生父子归属单独展示。关系观察为 2026-11-05T08:49:45.036422+00:00；main 引用 `91f0004517a9f1415228d33bfd8ca95eddbf936d`，不推进代码/CI 核验基线。下表补充 §8 的具体关系；不是 Issue 关闭的顺序。#824 已关闭，作为已有基础引用。#2721→#2658 按正文仅适用于所选 L3+ 路径。

| 关系 | 提供方 | 消费方 | 类型 | 具体产物／条件 | 依据 |
| ---- | ------ | ------ | ---- | -------------- | ---- |
| e01 | #2720 | #2723 | interface | 常驻消费者使用 Region 的区间、访问和必要同步；queue 协议仍由应用定义。 | [#2723 · ③ 依赖与交接](https://github.com/hw-native-sys/simpler/issues/2723) · 正文/评论 SHA-256 `6ec53ee86b84aed483177ad1d464cf46a5325369b3190aa5830414fb0a586a0d` |
| e02 | #2721 | #2723 | interface | 公共执行和资源合同供常驻接入；场景所需改动随常驻任务交付。 | [#2721 · ③ 依赖与交接](https://github.com/hw-native-sys/simpler/issues/2721) · 正文/评论 SHA-256 `cbcafe38b02e1d1077023b8302354c526b0fdf8df0d46bd6d6c453b4976e266d` |
| e03 | #2724 | #2722 | interface | 通信消费真实 session / endpoint / 部署结果，不等待整个部署方向收口。 | [#2722 · ③ 依赖与交接](https://github.com/hw-native-sys/simpler/issues/2722) · 正文/评论 SHA-256 `c36c0d939ed9e6602b50481ee5d3a75f4cefac414059e7417ce692a221967b81` |
| e04 | #824 | #2722 | foundation | 跨 run 复用与显式释放已有交付；通信场景在此基础上各自验收。 | [#824 · 关闭依据](https://github.com/hw-native-sys/simpler/issues/824#issuecomment-6652302467) · 正文/评论 SHA-256 `0c588c2bdec39076bb54fbe6db2863790aa754bc701d86d21aa481331f2c9444` |
| e05 | #2666 | #2658 | conditional | 核定所选 kernel / capture 模式的 prepare、launch、资源使用寿命与产物；普通 program Async 可独立推进。 适用：仅选用 kernel / capture 模式 | [#2658 · ③ 依赖与交接](https://github.com/hw-native-sys/simpler/issues/2658) · 正文/评论 SHA-256 `a140154bfa04b897199e0075de6caffb31a160dc6bc5059e8a375330916a1172` |
| e06 | #2721 | #2658 | conditional | 对齐所选 L3+ 调用路径的公共身份、绑定、完成与最后使用寿命。 适用：仅选用相关 L3+ 路径 | [#2658 · ③ L3+ 设计交接](https://github.com/hw-native-sys/simpler/issues/2658) · 正文/评论 SHA-256 `a140154bfa04b897199e0075de6caffb31a160dc6bc5059e8a375330916a1172` |
| e07 | #2496 | #2460 | conditional | 只在需要 A5 Device→Host direct-map 的消费者上核定能力，不阻止 copy-capable 路径。 适用：仅 A5 direct-map 分支 | [#2721 · ③ 外部映射能力](https://github.com/hw-native-sys/simpler/issues/2721) · 正文/评论 SHA-256 `cbcafe38b02e1d1077023b8302354c526b0fdf8df0d46bd6d6c453b4976e266d` |
| e08 | #2460 | #2722 | interface | 通信消费统一资源身份与访问契约，Remote producer / receiver 成对接入；基础修改随场景交付。 | [#2722 · ③ 资源身份与访问](https://github.com/hw-native-sys/simpler/issues/2722) · 正文/评论 SHA-256 `c36c0d939ed9e6602b50481ee5d3a75f4cefac414059e7417ce692a221967b81` |
| e09 | #2460 | #2666 | interface | Kernel eager 消费需要的本地能力；graph retention 接入实际 capture 消费者，不等全部 Remote 后端。 | [#2460 · Delivery and dependency plan](https://github.com/hw-native-sys/simpler/issues/2460) · 正文/评论 SHA-256 `20f03b873343ae6fef97ff184b6db852a421a708fd71dd4a00902dccff3075cc` |
| e10 | #2721 | #2724 | interface | 部署方向消费已确认的资源与执行合同，自行完成多级、多机接入验收。 | [#2721 · ③ 依赖与交接](https://github.com/hw-native-sys/simpler/issues/2721) · 正文/评论 SHA-256 `cbcafe38b02e1d1077023b8302354c526b0fdf8df0d46bd6d6c453b4976e266d` |
| e11 | #2666 | #2429 | interface | vLLM adapter 使用共同执行接口和资源机制；eager 不等完整 capture。 | [#2429 · ③ 依赖与责任交接](https://github.com/hw-native-sys/simpler/issues/2429) · 正文/评论 SHA-256 `9f631b7d3e27a2bf00b91586ecbbf0d6a4f83932198443a4e83490ca1b371bee` |
| e12 | #2715 | #2429 | interface | 真实 vLLM Engine 验收需要可复现目标环境；adapter 和环境可并行开发、在约定组合联验。 | [#2429 · ③ #2715 交接](https://github.com/hw-native-sys/simpler/issues/2429) · 正文/评论 SHA-256 `9f631b7d3e27a2bf00b91586ecbbf0d6a4f83932198443a4e83490ca1b371bee` |
| e13 | #2429 | #2658 | conditional | 系统对比与集成使用 vLLM adapter、artifact、数值和请求生命周期证据。 适用：仅选用 vLLM 路径／比较对象 | [#2658 · ③ vLLM eager 接入](https://github.com/hw-native-sys/simpler/issues/2658) · 正文/评论 SHA-256 `a140154bfa04b897199e0075de6caffb31a160dc6bc5059e8a375330916a1172` |
| e14 | #2730 | #2731 | required | 多 L2 Host URMA 建立在单 L2 显式 mapped-view 与所有权合同之上；可先并行开发，交付需对齐最终合同。 | [#2731 · Dependencies / Delivery](https://github.com/hw-native-sys/simpler/issues/2731) · 正文/评论 SHA-256 `d338b5dd637ebc3b5dec252e59297d450f9b2c2e902dddf0ede088abe6503bf3` |

\#2460 合同细化按其正文 U1a→U1b→U2a→U2b、本地/Remote 并行合流→U4 展示，不建立新的实施编号或状态。#2730→#2731 是明确映射/寿命合同前置；Host DDR 不等待反方向 #2496。额外 device lease、零 heap、公共组合类型仍是设计问题，不进入强前置。
