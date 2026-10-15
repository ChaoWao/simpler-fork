# 工作项登记表 —— 一个工作项，一个实施归属

> **发布快照：2026-10-15。** 设计、实现状态与历史调查须按正文各自基线阅读；不代表当前 main 全量验收。参见[阅读入口与已知更正](README.md)。
>
> **2026-10-13 通信定点核验**：`upstream/main@68d137fdfd435d8067f0efd9836cb428da3e700a`；启动采用 mpirun；PTO 通信从原生 CANN ACL Runtime 接口构建。A3 使用 ACL/Fabric V2 共享窗口；SHMEM 是实现参考，不依赖或计划引入 `libshmem`。证据与层次见 [通信方案](l3l4/cluster-comm.md#d4-remote-l3-通信mpirun--共享窗口)。本次不推进全目标扫描基线。已核实面向 main 的持续双机 PR CI：近期三个具名测试 SHA 的五项 NETWORK1 用例实际 PASS；逐项覆盖、失败样本与 NaN 验金缺口见通信方案 D.4。
>
> ## 本文是「谁负责实现什么」的唯一真源
>
> 每个工作项**恰好属于一条实施线**。它可以被任意多个交付里程碑引用，
> **但状态只在这里维护一份** —— 别处只许引用 ID，不许复述状态。
>
> 交付维度（用户已经能做什么、还缺哪项证据）在 [`README.md`](README.md) §2，
> 场景到工作项的映射在 [`l3l4.md`](l3l4.md)，三条线的职责与交接契约在
> [`track-boundaries.md`](track-boundaries.md)。
>
> **建表日期**：2026-09-07。**核验基线**：`upstream/main@01bf24bfa8a997c89210258c5954c4105843d783`（2026-10-07）。
>
> **2026-10-08 定点核验**：#2096 合入 `upstream/main@ae234fad000cd6bfa9b67865e336d7e90058f7ad`；RA-11 按该 SHA 记账，其他条目沿用各自注明的全扫描证据。
>
> **PR 范围**：只采用目标为 main 的 PR。本轮状态、目标来源和代码证据均核到页首 main；OPEN 不等于尚未实现，MERGED 不等于整个父目标完成。

## 0. 为什么会有这张表

重组前，「跨 run 域生命周期」这一件事在看板上有 **5 个格在各自维护状态**
（`P1-E` · `L3/L4 D.5` · `W3` · `W5` · `P2-D`，跨 4 条轨；实测 2026-09-07）。
后果不是重复劳动，是**判断被稀释**：`P2-D` 写着「被 P2-C 挡住」，`D.5` 写着「唯一残留」，
而 **PR #2120 已经在做它**，开了四天没人发现 —— 因为没有任何一格是它的家。

⇒ **判据**：**一个工作项一个实施归属；它可以支撑多个里程碑，但不能在几个地方分别维护状态。**
由 `check-single-owner.py`（本地参考：`check-single-owner.py`） 机器保证。

## 1. 三条实施线

| 线 | 前缀 | 统一负责 |
| -- | ---- | -------- |
| **资源与访问** | `RA-` | Buffer/Tensor · 所有权 · Attachment · capability · adapter · copy/Transfer · arena · 预算 · **用户可见的访问结果** |
| **分布式会话与通信域** | `DS-` | session/lease · 成员与 rank · 建域事务 · 远端回收 · 通信 profile · 规模扩展 |
| **执行与常驻运行** | `EX-` | admission · 异步准备 · control 响应 · completion/finalize · STOP/drain |

前缀选择说明：`RA`/`DS`/`EX` 与既有编号空间（`P1-A…F` · `P2-A…E` · `W0…W8` ·
`A.1`–`E` · `AR1–AR10` · `AF1–AF5` · `G1–G6` · `T5`/`T7`）**均无碰撞**，已实测。

## 2. 资源与访问（`RA`）

| ID | 工作项 | 吸收的旧编号 | 依赖 | 状态 |
| -- | ------ | ------------ | ---- | ---- |
| **RA-1** | device-id 逻辑→driver 翻译 | `AR5` | — | 🔵 **待实现**。本基线 `src/a2a3/platform/onboard/host/device_runner.cpp:1255` 的注册仍传裸 `device_id_`；`acl_to_hal_device_id` 在该文件 0 命中，阳性对照 `halHostRegister` 16 处。RA-3 的 direct-map 前置；树上 #1826 是同类修法。 |
| **RA-2** | a5 能否 host-map device 内存 | `W0` | — | ⛔ **A5 direct-map 验收未满足**。#2496 OPEN 承接 A5 driver SVM 的支持版本、双向可见性、注册/注销、回收、HBG/DFX 接入与对比验收。main 的 A5 collector 仍传 register_cb=nullptr，HostTensorAccessor 以 acquire_child_memory_host_view 探测、失败走 DeviceCopy；copy-capable 分支可继续推进。跨进程注册是 #2497 / RA-3 的独立维度，不能由单进程 mapping 推导。 |
| **RA-3** | 本地 attachment adapter（Host DDR ＋ 本卡 GM） | `P1-C` · `W1lite` · `W1` · `AR6` · `U2a` | RA-1 · 条件 RA-2〔只挡 a5 direct-map 那半；`W1lite` 按 arch 宏选实现，不等驱动结论就能做〕 | 🟡 **部分交付；U2a 迁移待实现**。本基线 `src/common/host_build_graph/host/host_tensor_access.cpp` 保留 #2212 的 child-memory 按需 mapping/copy；pure OUTPUT 与 runtime-created GM heap 无可读 region。**这类 GM heap 是 L2 device scratch，不是 RA-7 的 L3 hierarchical HeapRing。** #2460 正文已明确 U2a：拒绝隐式 cross-side get/set，双侧用户迁为显式双参数，全部受影响用例与限制同一交付单元；U1a 保留当前行为。它不是尚待二选一的设计问题。#2497 OPEN 承接另一进程独立注册同 backing：export/import grant、进程本地 VA、generation、可见性和退出回收均须联验；HOST_SVM_MAP 仍仅有命名机制，实际 producer/materializer 尚未实现。W1lite 仍可不等 a5 direct-map；本地访问需遵守 RA-10 的结果可见性。 |
| **RA-4** | arch cache maintenance 抽象 | `W4②` | RA-3 | 🔵 **stable 验收待交付**。范围是 cache maintenance 的平台语义及 publish/observe 可见性；#1822 的 Python 包装不替代此证据。a2a3 onboard 必须覆盖，不能从 host-ISA 分支推断双 arch 已通过。⛱ **挡住 `RA-11`**。 #2273 的 cache-line 隔离缺口仍在：本基线 worker.py:2623–2635 顺序 carve，仅检查 window_size，未按 cache line padding；需与上层 window_size 计算协同。 |
| **RA-5** | 显式 Transfer 与授权层 | `P1-D` · `AR7` · `AR8` · `U1a` | RA-3 | 🟡 **显式 TaskArgs-entry Transfer 已合入 #2454 `ec6a33dac`**（定点核验 main `255d78568`）。TaskArgs::transfers_与 add_tensor 为同一 view 保留逐次 request；Remote v4 只接受可表达的 location-default request，U3 仍是后续范围。copy_to/copy_from 保持 identity；统一授权与新分配 identity 的公开契约仍按本项验收，不能仅凭 U1a 合入关闭整个 RA-5。 |
| **RA-6** | 跨任务依赖谓词 | `P1-E` 前半 · `AR9` · `ST-2` · `KV-R1` · `KV-R2` · `KV-R3` · `KV-R4` · `KV-R5` | RA-3 | 🟡 **canonical footprint 依赖已交付，扩展验收仍在**。`orchestrator.cpp 的 lookup_overlapping/tensor_footprint` 使用 `lookup_overlapping`/`tensor_footprint`；INPUT 仍为 lookup-only。#1810 已 CLOSED、未合入；#1388 仍 OPEN，WAR 是能力增强，不是已判定的正确性缺陷。**#2450 的同任务显式 writable alias 是另一条验收**：同 backing、聚合 footprint、嵌套传播、staging/copyback 约束、未声明重叠继续拒绝；全部归本项，不由 U1b 自动授权。本基线 `task_args_wire.h` 仍拒绝重叠写。ST-2 的旧 raw-VA 前提已不适用：`Worker.make_tensor_arg` 按 storage memoize identity；TensorMap UT 有同 key 不相交/相交/相邻案例，真实 submit 端到端证据归本项。 |
| **RA-7** | arena 拆分 ＋ accounting | `P1-E` 后半 · `AR10` | RA-6 | 🔵 **L3 arena 分离待实现**。本基线 `Worker.alloc_shared_tensor` 直接调用 `self._orch._o.alloc`，返回 `FORK_SHM` Buffer；其 backing 与 scope 回收仍由 hierarchical HeapRing 管。`SlotArena`/`SharedBufferArena`/`BufferSliceArena` 在 `src/`、`python/` 0 个文件，阳性对照 `HeapRing` 16 个文件。**device context 的 WorkspaceManager 归 RA-16，不能算本项完成。** ⛱ **挡住 `EX-5`**；同步示例的资源基座归本项。 |
| **RA-8** | endpoint 与 capability view | `W2` · `P3①` | — | 🟡 **判定和 attachment wire 已有，逐 backend 验收待补齐**。本基线 `buffer.py::select_adapter` 有 `select_adapter`；`global_comm_domain.py:215,647` 有 attachments 与校验。`worker.py` 的 `local_window_bases` 共 2 处，分别为字段和赋值，尚无访问消费者（阳性对照：同文件 `retain_after_run` 3 处）。本地拒绝路径服务 P1；逐 backend 访问物化和发布门服务 P3。 |
| **RA-9** | ResourceBundle 组合多 backing / adapter | `P3②` 本地半 | RA-8 · RA-5 | 🔵 **组合契约待实现**。需同时表达 Host DDR control、Device GM payload 与 communicator，含组合失败语义。本地部分归本项，跨节点验收归 DS-10；既有 Region backing 不等于完整 ResourceBundle。 |
| **RA-10** | 向用户暴露 plan / capability / completion | `P3③` | RA-8 | 🔵 **逐路径接口验收要求**。RA-3、RA-5、DS-3 均须向用户呈现实际 plan/capability/completion，显式 policy 不得隐藏 copy 或 identity 变化。它不另立运行机制。 |
| **RA-11** | 最小同步原语 ＋ 首个有界 SPSC 示例 | `P3④` · `W4①` · `W6` | RA-7 · **RA-4** | 🟡 **部分交付：#2096 已合入 main**（`ae234fad0`，2026-10-08）。内部 Region Template、双向有界 SPSC、10-scalar SPSQ 与旧 facade 迁移已交付；三个核心文件与已审 head `2297a9a4f29a` 一致。方案仍每次物化专用 Region，尚非既有 Buffer/slice 的受管理绑定；publish/observe/wait 通用接口、RA-7 资源基座与 RA-4 可见性保留为 stable 退出条件。schema/policy 属上层，常驻循环归 EX-2；通用接口与受管理绑定继续归本项，公共迁移衔接 RA-13/RA-14。合入不关闭 RA-11/P3④；具名证据见 [评审与合入记录](review/pr-2096.md)。 |
| **RA-12** | 本地独立 per-worker domain ownership | `P2-D` 本地半 · `W3` · `#824` 本地半 | RA-8 | 🟡 **owner 转移在飞 #2120**（OPEN，`cd69b1e0ecf9`）。main `worker.py` 中 `detach_persistent_domain` 0 命中，阳性对照 `retain_after_run` 3 处；现有保留寿命不等于独立 Worker ownership。与 DS-4 共用契约 A，与 RA-15 共用契约 D；用户尚未裁决两种资源 registry 的归一形状。 |
| **RA-13** | 四类 worker 内存模型统一成 ABI | `l3l4 B` · `AF5` · `U1b` · `U2b` · `ST-1` | RA-3 · RA-5 | 🟡 **统一 ABI 部分交付**。#2198 `c7ae45924` 已把 Region backing 接入 canonical Buffer；#2467 `255d78568` 提供 direct-L2 snapshot、identity/grant 验证与 accepted-use 保留，#2474 `05b5c19a0` 说明释放 API 边界。#2478 `3121da336` 保留 mixed owned/external 中已知 allocation，完整覆盖只决定 joined admission；旧 #2475 评审 P1 的 native 根因已修复。#2479 `7b614764b` 提供 `Tensor(buffer, shape=..., dtype=...)`，兼容 `buffer.tensor` 共用 validator。#2475 已 CLOSED 未合入；#2482 `fcbf4f1d1` 已提供 Buffer.wrap 源登记与 submit-time L2 接入。location 仅支持本进程 direct-L2 Program Worker incarnation，调用方仍负责物理寿命；未知完成保留 source use，不能当作外部 RTS context/cross-Worker/Remote 授权已交付。U1b 仍需 Region、borrowed pointer、L3/L2 TaskArgs 与旧 ChipWorker 调用迁移；#2486 `22c009811` 已交付 ChipWorker.run 共享 HOST/H2D Tensor 参数，异常保留 imports 至成功 finalize，不含 standalone DEVICE source 接入；#2489 `833327f60` 在四个 program binder 前置拒绝不支持的 HOST strides/native start_offset，公共 byte_offset 仍由物化归入地址；U2b 的 HBG/TMR IO/retention 记录独立验收。ST-1/AF5 留在本项；#2146 OPEN `247689f4e3ba` 的 singleton stride 修复尚未合入，main `tensor.h::is_contiguous` 仍逐维比较 stride。 |
| **RA-14** | API 收敛与迁移 | `W8` · `C.2` 的 `malloc` 半 | 就绪 RA-3 · 就绪 RA-5 · 就绪 RA-10 | 🟡 **构造统一已有交付，调用迁移继续**。#2479 `7b614764b` 的 `Tensor(buffer, shape=...)` 与 `Buffer.tensor` 兼容转发已在 main，共用几何与范围验证。#2480 `4d0610fb7` 已合入 `shapes=` 对齐、入门 Worker 示例迁移与 Buffer 类型缓存；#2482 源侧 Buffer.wrap 已交付 direct-L2 有限范围。#2486 的 ChipWorker.run HOST/H2D consumer 已合入；完整旧 POD 退役、DEVICE 与其他消费者的迁移仍由 RA-13 验收。就绪依赖仅要求对应路径接口定型；最终退役核全部调用者，不等所有 backend 串行完成。 |
| **RA-15** | 统一 logical close ＋ 物理释放 | `U4` | RA-12 · RA-13 · DS-3 | ⛔ **U4 退出条件未满足**。host-backed `buffer.py::Buffer.close` 仍直接执行 `shm.close/unlink`；#2482 的 source registration 分支按锁拒绝在飞/未知完成的 close，不释放外部分配；`Worker.free` 与 direct-L2 accepted-use 登记共用 chip lock，host `release_buffer` 仍是采样后 close。#1689 cleanup journal、#2446 caller borrow、#2467 direct-L2 保留和 #2478 partial borrow 是现有基座，不等于统一 logical close。#1931 的 child allocation/free 可信记录归本项。U4 完整退出仍需 U2b/U3 的 network→H2D 联验及契约 D 的 owner/锁序/journal 组织；在飞保护随每条 consumer 先交付，普通 Tensor wrapper 不延长 HeapRing scope。 |
| **RA-16** | device context workspace ownership 与预算 | `VLLM-4` 的资源半 | — | 🟡 **直接 L2 与 startup-root 本地 L3 的 workspace ownership 已交付**。#2440 `9ca91c522`、#2452 `a1ca296f8`、#2464 `818ee04d7`；main `_chip_children_manage_workspace` 仍按 device_ids 与_is_startup_root 选择 child ownership，nested/remote/MPI 与 L3 byte budget 不属于该保证。#2473 `35e9331b5` 已在 C program-context init 统一安装四区 workspace，覆盖所有走该入口的 onboard program context；teardown proof 先消费再安装，retention probe 记录 device-drain 事实。Python startup-root 路由条件不再限制 native workspace manager 覆盖；nested/remote/MPI 端到端验收与 L3 byte budget 仍独立。退休按 run/completion/host-view 引用、未知执行隔离与实际 free 结果，不拆 RA-7 HeapRing，不接管 RA-15 用户 Buffer。 |

## 3. 分布式会话与通信域（`DS`）

| ID | 工作项 | 吸收的旧编号 | 依赖 | 状态 |
| -- | ------ | ------------ | ---- | ---- |
| **DS-1** | MPI 会话的 session/epoch registry | `P2-A` | — | 🟢 **session/epoch 生产契约待实现，无工作项前置**。2026-10-13 通信核验以 `68d137fdf` 为准；主路径由 L4 拥有 mpirun group，rank-side runtime 参与资源回收，不要求独立常驻 daemon。已有 mailbox READY/SHUTDOWN、group timeout 与进程组退出；仍需跨端 cleanup 确认、epoch/lease 和故障后资源可枚举性，不能以杀掉 launcher 代替远端资源已清理。 |
| **DS-2** | device lease ＋ rank inventory ＋ 冻结 global rank | `P2-B` 的清点半 | DS-1 | ⛔ **生产清点/冻结验收未满足**。归属为 endpoint inventory、device lease、global rank 编号；PREPARE/COMMIT/ABORT 的执行归 DS-9。 |
| **DS-3** | remote transfer / attachment：两条 copy 语义分清 | `P2-C` · `U3` | DS-2 · RA-5 | ⛔ **U3 合并验收未满足**。现有 owner-delegated socket copy 不能算 device-peer/collective。#2460 要求 producer/receiver/version 成对迁移、保留 entry request/direction/grant/identity/range、不得 receiver 默认 H2D 或把 host transport storage 标成 DEVICE；network→H2D 故障时保留中间资源。 |
| **DS-4** | 远端 domain lease、断连与回收 | `P2-D` 远端半 · `#824` 远端半 | DS-3 · RA-12 · **DS-9** | ⛔ **远端 lease/断连回收退出条件未满足**。与 RA-12 共用域生命周期；DS-9 提供建域事务。依赖表示完整验收，不妨碍本地 owner 转移原型先交付。 |
| **DS-5** | A3 HCCS/RoCE ＋ A5 UB/URMA profile | `P2-E` · `D.2` 的 backend 半 | DS-4 | 🟡 **backend 部分交付**。A3 HCCS 共享窗口与 peer-TLOAD 已由 #1456 提供，#1623/#1624 补齐 MPI 启动/派发（`68d137fdf` 复核）；当前代码已核完整调用链；面向 main 的双机 PR CI 持续看护五项 NETWORK1 用例，10 月 12–13 日三个具名测试 SHA 均逐项 PASS（含 `1b77ad2a` / job `115197486707`）；不列为待首次打通。现有验金的全 NaN 误过已由 CPU 正反探针复现，强化验金归本项；证据见通信方案 D.4。沿用原生 ACL Runtime + PTO 共享窗口路径，参考 SHMEM 的实现效果；没有 SHMEM 库适配任务。#2157 已合入 A5 TMR RDMA/RoCE deferred completion；#2026（OPEN，`c3bbbd1e7084`）涉及默认 SDMA/URMA。P2 的 session/lease、profile 组合与故障验收仍需分别满足，不能把整项标 done，也不能把 DS-4 当作 backend 禁止开工的理由。 |
| **DS-6** | mpirun 启动与发现 ＋ 跨-L3 域接口 | `A.1` 残留 · `D.4` · `C.2` 的 L4 半 | — | 🟡 **MPI 启动与 mailbox 派发已交付**（#1623 `cc9bfc45`、#1624 `43094d44`，核验 `68d137fdf`）。主路径是 L4-owned mpirun、rank 0 本地 mailbox 与 rank 间 MPI；不需 Simpler 常驻 daemon。MPI bootstrap 与 A3 Fabric V2 数据面独立；HCOMM/HCCL 按底层 backend 需要复用，不另设必选启动路线。剩余为部署/发现契约与跨-L3 接口验收；#1888 的 MPI direct 是另一派发方案，不是既有 MPI 路径的前置（PR 状态见登记基线）。 |
| **DS-7** | 移除或重构 64-rank device ABI 上限 | `P3⑤` | DS-2 | 🔵 **独立规模扩展包**。device rank ABI 的 64 上限需要独立兼容与容量验收；不阻止小规模异构域先行。 |
| **DS-8** | host 参与域的语义（非对等成员） | `D.3` | RA-8 · 条件 RA-2〔仅 A5 direct-map 分支；copy-capable host attachment 走 `OWNER_DELEGATED_COPY`，不需要它〕 | ⛔ **host attachment 组合验收未满足**。host 不占 HCCL rank；现有 attachment 矩阵的 copy-capable 分支不等 a5 direct-map。仅 direct-map 分支受 RA-2 条件约束；本地多 backing 归 RA-9，跨节点组合归 DS-10。 |
| **DS-9** | 成员事务执行（唯一设计归属） | `W5` · `P2-B` 的事务半 | DS-1 · DS-2 | 🟡 **递归委派已交付 #2024；生产成员事务待验收**。本基线 GlobalDomainPhase 有 PREPARE_EXPORT/IMPORT/COMMIT/ABORT，不能写成事务从零。剩余要求是与 MPI session owner 的 epoch/lease、回滚、冻结成员及全员 READY 闭环；已有 PREPARE/IMPORT/COMMIT 不重复设计。⛱ **挡住 `DS-4` `DS-10`**。 |
| **DS-10** | 跨节点异构域的组合与验收 | `D.5` 残留 · `P3②` 跨节点半 | DS-4 · RA-9 · **DS-9** | 🟡 **跨机基本栈已交付**（#1456/#1623/#1624）。异构 ResourceBundle 的跨节点组合仍须 session、成员事务、授权及失败回收共同验收；与 RA-9 各负责一半。 |
| **DS-11** | 同步与异步模型（SDMA / URMA gate） | `D.2` 的 opt-in 半 | — | 🟡 **异步 backend 按 capability/workspace 限定**。#2115 已合入 simulator SDMA workspace；默认 SDMA/URMA 的 #2026 仍 OPEN。同步集合 barrier 与异步数据移动是不同契约；默认策略、completion 与 workspace 验收须分开。 |

## 4. 执行与常驻运行（`EX`）

| ID | 工作项 | 吸收的旧编号 | 依赖 | 状态 |
| -- | ------ | ------------ | ---- | ---- |
| **EX-1** | responsive local edge：覆盖面与验收 | `l3l4 E` · `P1-F` · `AF2` · `AF4` | — | 🟡 **窄 local lane 已交付，扩展验收未满足**。`run_two_frame_loop` 服务 control 与任务；`make_next_level_threads` 传 `entry.task_frame_count`，`make_sub_threads` 不传而使用默认单帧（函数内该参数 0 处，阳性对照邻接 NEXT_LEVEL 构造 1 处）。AF2/AF4 的 SUB 与 scheduler/control 约束仍属本项。nested、Local L4、A5、simulation、remote 的长期任务/control/cancel/health 是逐组合退出条件，不能以 local 双帧替代。#2116（OPEN，`6b8d56d4b54f`）是 control dispatch 扩展载体。 |
| **EX-2** | 常驻 worker：`orch_fn` while 循环 ＋ STOP/drain | `W7` · `AF1` | EX-1 | 🔵 **常驻循环与停止契约待验收**。AF1 的 idle spin 路径由 `_run_mailbox_loop` 承载；常驻 orch、STOP、drain 与阻塞唤醒归本项。派发路径不得用 sleep/backoff；RA-11 只提供同步原语和示例。 |
| **EX-3** | admission · 定向调度 · **零 heap 运行验收** | `l3l4 C.3` 的 admission 半 | — | 🟡 **定向与 whole-run FIFO 已交付；零 hierarchical heap 的运行验收仍单列**。这是 feature_roadmap §2.5 第 5 步，不要求先删除结构。#1137 已 CLOSED 且原属 L2 polling 优化。#2476 `dfa63eb81` 已合入：retention 测试使用 1024/4096/8192 递增窗口、无在飞 read 基准及诊断；#2470 已 CLOSED。合入测试不等于所有 overlap 或性能目标完成，EX-7 的真实 enqueue/refill 仍按其独立验收。 |
| **EX-4** | 执行接口与 callable 身份收敛 | `l3l4 C.2` 的 `run`/`orch`/callable 半 · `AF3` | — | 🟡 **公共 callable/执行接口仍需收敛验收**。AF3 的 pre-init live-object 与 digest 快照契约由 #1938（OPEN，`47e60a866df1`）提供可失败的观测；run/orch/callable 统一不得靠改名替代身份验证。 |
| **EX-5** | 删除 heap-coupled scope reclaim | `l3l4 C.3` 的 heap 结构半 | RA-7 · EX-3 | ⛔ **使用者迁完后才能删除结构**。RA-7 提供 §2.5 第 1–4 步，EX-3 提供第 5 步；本项是第 6 步。L2 TMR device GM heap 不在删除范围。 |
| **EX-6** | 终态回收 · child freeze · nested-shm 枚举 | `P0.2-f4` · `P0.2-f5` · `P0.2-f6` · `P0.2-f7` | — | ✅ **f4–f7 已交付，当前基线复核**。#1689 `f8106e24e` 已提供 `_teardown_worker_tree`：pre-transport 全集先入 post-success journal，失败保留 transport；remote frees 与 session shutdown 分相；child/shm 有独立重试。`add_once(kind, identity, cleanup)` 的数据表含 region/domain/provenance/remote/buffer，不能只 grep `.add("child")`。`test_cancellation_journal.py::test_both_teardown_paths_retry_each_resource_class` 与 namespace/child 顺序案例在树上（本轮读测试，未重跑）。既有 f5 双锁、f6 survivor 重试、f7 确定名与 namespace sweep 均保留；新增 U4/device-workspace 的寿命目标分别归 RA-15/RA-16，不倒灌为 P0.2 遗留。 |
| **EX-7** | 连续 run 共享 stream 的顺序契约（Track A） | `TrackA-P1` · `TrackA-P2` · `TrackA-P3` · `TrackA-P4` · `TrackA-P5` · `VLLM-1` · `VLLM-2` · `VLLM-3` · `VLLM-4` 的 admission 半 | EX-3 | 🟡 **有边界的 early enqueue 与三套资源容量已交付，真实 workload 接入仍部分完成**。#2419/#2446/#2449 是 FIFO、host-read 与多 successor 基座；#2466 `fb76a54d5` 支持 A3 HBG startup-root 本地 L3、单 local endpoint、无 SUB 的显式 pipeline_depth=3，默认资源深度仍 2。#2481 `5eb913c39` 支持 A5 HBG 显式 launch_depth=2；#2491 `67bce6291` 扩展到 A5 TMR，仅 host 提交重叠、整 operator 串行，cache miss/配置变化仍 fallback，错误只读本 run terminal record。#2471 `8e4dac671` 合入 DistinctSubmissionsOwnIdentityAndLease UT 与已有 A3 ST 注释，未增加 Qwen adapter 或完整 request-private KV 验收。#2447/#2456 已交付 Qwen depth-one 与 requested 2/effective 1 fallback；真实 Qwen clone-request 接线、同请求 DEVICE feedback、其他 backend/shape 与资源预算按本项/RA-16 联验。#2429 旧正文仍称 #2471 待合并，状态以 main 为准；#2459 distinct-row 不作为开始 clone 接线的前置。#2028 OPEN、#1987 历史 draft、#1967 activation 与 #2425 五项仍各按既有契约验收。 |

## 5. 已交付，保留为验收证据（不另建工作项）

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
| capability view **只在对应 backend 完成功能、故障和硬件验收后**才实现；不为未落地能力发布空壳资源或伪成功接口 | `P3⑥` | `RA-8`，并经它传导到 `RA-9` / `RA-10` |
| 不以「统一内存」名义提供隐式 coherence；`copy_to`/`copy_from` 保持既有 Buffer identity，任何 Transfer 创建的新 identity 必须显式可见 | `P3` 退出条件 | `RA-5` · `RA-10` |
| queue schema / opcode / batching / backpressure policy **不进 Buffer/Attachment 基础 ABI** | `P3④` 的边界 | `RA-11` |

契约编号家：GE-R1/R2 → RA-15/RA-12/DS-4；GE-R3 → RA-6；GE-R4 → EX-7；GE-R5/R6 → 外部 GE 实现（交接要求保留）。VLLM-S3-1…7 全部作为 EX-7 的 clone-request 验收，资源退休与 RA-16 联验。

`GE-R1` · `GE-R2` · `GE-R3` · `GE-R4` · `GE-R5` · `GE-R6` · `VLLM-S3-1` · `VLLM-S3-2` · `VLLM-S3-3` · `VLLM-S3-4` · `VLLM-S3-5` · `VLLM-S3-6` · `VLLM-S3-7`

`EE-QA-1` · `EE-QA-2` · `EE-QA-3` · `EE-QA-4` · `EE-QA-5`：#2425 的五条 early-enqueue 验收 → EX-7；其他不同根因症状不合并为一个缺陷。

`CAP-R1` · `CAP-R2` · `CAP-R3`：#2466 合入 `fb76a54d5` 的 late-preparation 发布、默认容量/范围、三 run 顺序与 16-run refill 约束 → EX-7；默认资源容量与 RA-16 交接。

跨阶段验收义务：权威细节见 [访问、完成与资源退休契约](worker-memory-model/access-retirement-contract.md)，推导及来源见 [设计评估](review/2026-09-28-cross-plan-lessons.md)。以下约束随对应 consumer 交付，不以 U4 或 kernel 实现为前置：

| 既有归属 | 必须满足的验收义务 |
| -------- | ------------------ |
| DS-3 → RA-15/RA-16 | 分别证明源不再被读、目的可消费、操作已静止；延迟接收/H2D、第二段失败和旧完成记录不得提前放行或回收。 |
| RA-3/RA-13；DS-3 | 全调用/路径预检在数据副作用前完成；绑定点原子取得使用保留；mixed owned/external 与未知完成安全随 consumer 交付，不等 U4。 |
| EX-7 → RA-16 | 分开核 accepted/prepare/enqueue/执行容量；只发出 wait 尚未 launch、launch 后 fence record 失败仍保留资源；正常完成后须有可复用阳性对照。 |
| RA-16；EX-7 联验 | 真实 producer 与滞后 consumer 的多轮复用；核旧版本、graph 使用引用、释放失败和各预算域；不把 CPU 协议验证当作 device 可见性证明。 |

RA-13/RA-14 遵循 [用户面契约](worker-memory-model/user-surface.md) 的源登记/目标接入分层，`Tensor(buffer, shapes=...)` 已由 #2479/#2480 提供，#2482 的 `Buffer.wrap` 已支持 direct-L2；其他 context provider、consumer 与兼容移除边界由 RA-14/RA-13 继续承接。RA-15/RA-12 按契约 D 共享生命周期规则；registry 具体组织、锁序与 journal 组合仍待裁决。设计约束不代替交付状态。

RA-15/RA-12/DS-4 的联验须覆盖独立 grant、graph/replay 引用及 unmap/free 失败；来源与反例见共同契约 §3/§5。RA-16/EX-7 的 CPU 协议验收与硬件可见性证据分别给出。

### 外部能力的接入验收（2026-10-07 来源快照）

以下 A 编号按来源 acceptance checklist 顺序生成，R 编号对应来源原有 Proposed Fix 1–4；是验收归属，不新建实施项。

| 来源与完整编号 | 唯一承接 | 范围 |
| -------------- | -------- | ---- |
| #2496：`A5SVM-A1` · `A5SVM-A2` · `A5SVM-A3` · `A5SVM-A4` · `A5SVM-A5` · `A5SVM-A6` | RA-2 | 支持版本、注册/可见性/释放、平台接入、DFX、HBG 访问、对比；RA-3/RA-4 提供接入与可见性契约 |
| #2497：`XPSVM-A1` · `XPSVM-A2` · `XPSVM-A3` · `XPSVM-A4` · `XPSVM-A5` · `XPSVM-A6` · `XPSVM-A7` · `XPSVM-A8` | RA-3 | import/registration 组合、两进程演示、双向可见性、权限范围、寿命与 reset、probe/materializer、真实用例、分 arch 证据；RA-8/RA-15 联验 |
| #2429 的 `PR-S3-1` | EX-7 | #2471 是程序身份/lease 验收子集；不代替真实 Qwen clone-request adapter |

## 7. 不属于这三条线的活

本重组**明确排除 kernel 模式与 HBG 硬件需求**。因此下列工作**不进三条线**，
但 [`README.md`](README.md) §1.1「目标 main 的 PR / issue 都要占一行」**仍然适用** ——
它们登记在 §1.2「不在本体系内」：

- **kernel 模式**：维持用户既定排除；独立栈按 §1.2 登记，完整编号与原则见 [覆盖清单](goal-coverage.md)。是否第四线仍待用户裁决，不能用栈已合入推导批准。
- **runtime / 调度器与 HBG 硬件需求**：GE 内部实现和硬件预算归各自维护者；我们的 GE-R1–R6 交接要求仍在 §6，不能随排除项消失。
- **DFX**：`VLLM-5`（continuous DFX）由 collector/runtime 的独立交付负责，#2465 `02fd1f63e` 与 #2483 `92a14e4e7` 已分别交付 scope-stats、TMR dep-gen 后台输出，#2484 `8aae172e5` 修 A5 错误关闭的 run identity 传参；#2490 `01bf24bfa` 已交付 HBG 后台依赖图输出，发布证明来自 host capture，区别于 TMR device record 的完成/隔离条件；run 返回与产物发布分离，flush_diagnostics 是产物屏障。它们不等于任意 collector/runtime 的 continuous DFX 全部完成。T7 跨 host 部分由 #2217/#2304 跟踪，不能用本地 #2152/#2218 的交付关掉 #2134 全范围。
- **Qwen / vLLM adapter 与跨仓验收**：`Q1` `Q2` `Q3` `Q4` `C2b` `V1`、`VLLM-6` `VLLM-7` `VLLM-8` `VLLM-9` 归现有 Qwen 集成轨及 #2429 的 adapter/capture/E2E 交付；程序 admission 归 EX-7，资源账本归 RA-16。#2447 `9b283ab1e`、#2456 `b57629cf9` 均已合入；真实 depth-one 与请求 depth-two 时的安全串行回退成立，真实 Qwen clone-request adapter 仍是下一段。后续 DEVICE clone-request 能力不等同于 Step 2 depth-one fallback。
- **CI / 文档 / 打包**：按 README 登记，不建实施阶段。

⚠️ **这不是「不重要」，是「不由这三条线的交接契约管辖」。** 把它们塞进三条线会让
「一个工作项一个归属」这条判据失去意义 —— 因为它们的归属在别的仓或别的团队。

`T6` · `T8` · `T9`：DFX 备选方案，按既定范围裁决不排入实施线；不是已交付声明。

明确排除的编号家（不是实现完成声明）：

`C2b` · `KERNEL-10a` · `KERNEL-2B` · `KERNEL-2a` · `KERNEL-Binder` · `KERNEL-H1` · `KERNEL-H2` · `KERNEL-H3` · `KERNEL-K2` · `KERNEL-K3` · `KERNEL-K4` · `KERNEL-K5` · `KERNEL-K7` · `KERNEL-Principle-01` · `KERNEL-Principle-02` · `KERNEL-Principle-03` · `KERNEL-Principle-04` · `KERNEL-Principle-05` · `KERNEL-Principle-06` · `KERNEL-Principle-07` · `KERNEL-Principle-08` · `KERNEL-Principle-09` · `KERNEL-Principle-10` · `KERNEL-Principle-11` · `PIPE-C1` · `Q1` · `Q2` · `Q3` · `Q4` · `RequestSession` · `V1` · `VLLM-5` · `VLLM-6` · `VLLM-7` · `VLLM-8` · `VLLM-9`

历史与范围外的细编号（保留出处和继任关系，不把副本的“未做”当作当前状态）：

- 日志参考需求归外部 DFX/log 合同：F1–F5/F7 的观测面接 T1–T5 与 Host Swimlane；F6 的跨线程 flow 属 T9 备选边界；D5/T4 采用 T7 containment，不恢复已撤回的 clock-alignment 路线；C/N/G 与 T1–T3/T5 是 #1792/#1793/#1794 的约束及取舍来源。它们不与 P1 的 G1–G6 或流水线的 T1–T5 混名，也不整体宣称已交付。
  `LOG-C2` · `LOG-C4` · `LOG-C5` · `LOG-C6` · `LOG-C7` · `LOG-D5` · `LOG-F1` · `LOG-F2` · `LOG-F3` · `LOG-F4` · `LOG-F5` · `LOG-F6` · `LOG-F7` · `LOG-G1` · `LOG-G10` · `LOG-G11` · `LOG-G11b` · `LOG-G12` · `LOG-G13` · `LOG-G14` · `LOG-G15` · `LOG-G16` · `LOG-G17` · `LOG-G18` · `LOG-G19` · `LOG-G2` · `LOG-G20` · `LOG-G21` · `LOG-G22` · `LOG-G23` · `LOG-G3` · `LOG-G4a` · `LOG-G4b` · `LOG-G6` · `LOG-G6b` · `LOG-G7` · `LOG-G8` · `LOG-G9` · `LOG-N1` · `LOG-N2` · `LOG-N3` · `LOG-N4` · `LOG-T1` · `LOG-T2` · `LOG-T3` · `LOG-T4` · `LOG-T5` · `LOG-I1` · `LOG-I2` · `LOG-I3` · `LOG-I4` · `LOG-I5` · `LOG-I6` · `LOG-I7`
- 旧流水线 W1–W4 方案已退役；HBG prepare、native lane、TMR prepare、progress 分别由 PIPE-B4、PIPE-W1a、PIPE-B5、PIPE-B3/B6 继任，原 PR 是否合入与继任交付不是同一问题。
  `PIPE-v1-W1` · `PIPE-v1-W2` · `PIPE-v1-W3` · `PIPE-v1-W4`
- kernel-memory 的 D1–D6 是范围外历史方案；约束可供 RA-16/EX-7 参考，callable 容量、restore 与 stream/event 具体方案不成为本板承诺。
  `KERNEL-Memory-D1` · `KERNEL-Memory-D2` · `KERNEL-Memory-D3` · `KERNEL-Memory-D4` · `KERNEL-Memory-D5` · `KERNEL-Memory-D6`

外部 runtime/CANN 的独立验收：#2498 的 `SIMT-A1` · `SIMT-A2` · `SIMT-A3` · `SIMT-A4` · `SIMT-A5`（官方接口、接入去锚点、注册、SIMD/SIMT 混合、A5 验证）；#2492 的 `A2RET-R1` · `A2RET-R2` · `A2RET-R3` · `A2RET-R4`（TMR/HBG 顺序实验、pending handoff、保留退出顺序、分层验证）均明确范围外。#2495 是外部依赖总账，#1425 SDMA 故障恢复与 #905 perfmon 继续归外部 runtime/DFX；总账不形成第四实施线。

## 8. 依赖栏的写法（有语法，机器检查）

⛔ **2026-09-07 起「依赖」栏有语法，无法解析的非空格子会让
`check-dep-closure.py`（本地参考：`check-dep-closure.py`） 报错。** 成因：上一版解析器只把编号
`findall` 出来，**限定词全部丢失**，于是两个格子静默失去了含义 ——
`RA-14` 把就绪条件挪到说明栏后被读成「无依赖」（看起来畅通），
`DS-8` 的「仅 A5 direct-map 分支」被读成**无条件**依赖（正是它原本被过度阻塞的原因）。

| 写法 | 含义 | 是否进依赖闭包 |
| ---- | ---- | -------------- |
| `—` | **明确没有依赖**（不要留空） | — |
| `RA-3` | 硬前置 | ✅ 进 |
| `就绪 RA-3` | **接口就绪**：那一项的接口形状已定，不要求它整体交付 | ✅ 进（它仍是前置，只是前置的是形状） |
| `条件 RA-2〔原因〕` | **分支条件**：只在某条分支上成立 | ⛔ 不进硬闭包，但会被单独打印 |

多个之间用 `·` 分隔。**`条件` 的 `〔原因〕` 是强制的** ——
一条没写下来的条件，和一条无条件依赖在文本上完全一样。

另有 **⛱ 挡住 `X` `Y`** 写在**挡人的那一项**行内：声明 `X`/`Y` 的依赖闭包必须到得了本项，
由同一条守卫验证。⚠️ **不要在被挡的那一项行里写 ⛱ 交叉引用** ——
解析器会把它读成「自己挡住自己」（已发生过一次）。

## 9. 引用规则

别处提到一个工作项时，**只写 ID ＋ 一句话，不复述状态**：

```markdown
（→ [`RA-12`](work-items.md#2-资源与访问ra)：本地 per-worker domain ownership）
```

⛔ **反例**（重组前 `P2-D` 的写法）：把「被 P2-C 挡住」「唯一残留」「无人认领」
写进四个不同的格 —— 其中三个在 #2120 开出来之后就错了，而**没有任何一处会被更新**，
因为更新哪一处都不是「唯一真源」。
