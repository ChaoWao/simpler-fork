# 工作项登记表 —— 本地合同归属与验收证据

> **发布快照：2026-10-26。** 设计、实现状态与历史调查须按正文各自基线阅读；不代表当前 main 全量验收。参见[阅读入口与已知更正](README.md)。
>
> **2026-10-26 / #2716 定点核验**：main `daf2f8418907592b5b881792bd9a2a0a5ea8f19d`；四方向与原生子树的范围、下一动作见 [track 分析](review/track-2716.md)。GitHub issue 承载完整目标与业务进度，本表维护本地合同与验收证据；RA/DS/EX 不另拆一份 issue 交付。下方 10 月 18 日是全目标核验基线，本轮仅刷新文中注明的相关项。
>
> **本轮核验（2026-10-18）**：main `937c57efc71ad01a01c3a72072df075d2a5dd24f`；实现与验收证据分开记录，历史双机 CI 样本保留其实际测试 SHA。
>
> ## 本文是本地合同归属与验收证据的唯一真源；GitHub 是完整目标、实际 owner 与业务进度的真源
>
> 每个工作项**恰好属于一条实施线**。它可以被任意多个交付里程碑引用，
> **但状态只在这里维护一份** —— 别处只许引用 ID，不许复述状态。
>
> 交付维度（用户已经能做什么、还缺哪项证据）在 [`README.md`](README.md) §2，
> 场景到工作项的映射在 [`l3l4.md`](l3l4.md)，三条线的职责与交接契约在
> [`track-boundaries.md`](track-boundaries.md)。
>
> **建表日期**：2026-09-07。**核验基线**：`upstream/main@937c57efc71ad01a01c3a72072df075d2a5dd24f`（2026-10-18）。
>
>
> **PR 范围**：只采用目标为 main 的 PR。全量状态、目标来源和代码证据按各自具名基线阅读；#2716 定点更新按 10 月 26 日 main；OPEN 不等于尚未实现，MERGED 不等于整个父目标完成。

## 0. 为什么会有这张表

重组前，「跨 run 域生命周期」这一件事在看板上有 **5 个格在各自维护状态**
（`P1-E` · `L3/L4 D.5` · `W3` · `W5` · `P2-D`，跨 4 条轨；实测 2026-09-07）。
后果不是重复劳动，是**判断被稀释**：`P2-D` 写着「被 P2-C 挡住」，`D.5` 写着「唯一残留」，
而 **PR #2120 已经在做它**，开了四天没人发现 —— 因为没有任何一格是它的家。

⇒ **判据**：**一个工作项一个实施归属；它可以支撑多个里程碑，但不能在几个地方分别维护状态。**
由 `check-single-owner.py`（本地参考：`check-single-owner.py`） 机器保证。

## 1. 三条本地合同线与四个交付方向

 | 线 | 前缀 | 统一负责 |
 | -- | ---- | -------- |
 | **资源与访问** | `RA-` | Buffer/Tensor · 所有权 · Attachment · capability · adapter · copy/Transfer · arena · 预算 · **用户可见的访问结果** |
 | **分布式会话与通信域** | `DS-` | session/lease · 成员与 rank · 建域事务 · 远端回收 · 通信 profile · 规模扩展 |
 | **执行与常驻运行** | `EX-` | admission · 异步准备 · control 响应 · completion/finalize · STOP/drain |

前缀选择说明：`RA`/`DS`/`EX` 与既有编号空间（`P1-A…F` · `P2-A…E` · `W0…W8` ·
`A.1`–`E` · `AR1–AR10` · `AF1–AF5` · `G1–G6` · `T5`/`T7`）**均无碰撞**，已实测。

### 1.1 #2716 的完整目标归属

 | GitHub 方向 | 原生子项（唯一完整交付入口） | 本地合同与验收索引 |
 | ----------- | ---------------------------- | ------------------ |
 | #2721 公共基础 | #2460、#1931、#2720 | RA 公共资源、EX 编排调度/任务下发；不吸收其他场景专用基础改动 |
 | #2722 通信与后端 | #2594、#824、#2621、#2657、#2708 → #2673/#2674 | DS 域/backend 与 RA 访问/寿命、EX completion 交接 |
 | #2723 L2 常驻执行 | 当前直接在方向 issue 分析方案；原生子列表为空，对照 #2721 的 3 项和 #2722 的 5 项 | EX-2 主索引；RA-11 等是消费合同 |
 | #2724 L4 多级多机部署 | #2659 | DS-1/DS-6 会话与部署、DS-4/EX-6 清理交接 |

**归属判据**：一个完整场景目标留在一个 GitHub issue，可分多个 PR；因该场景需要而修改 Buffer、Region、scheduler 或 completion，不据此再拆方向 1。只有具有独立意义且能独立验收的公共基础才单列。方向编号不是执行顺序；阻塞必须指向具体产物。本地聚合行的依赖是其原合同退出条件，不自动成为新场景的硬前置；#2621 第三方 owner-local 扩展不等待 DS-2 全量清点或原生 RoCE。既有 RA/DS/EX 的编号拆分仅保留为合同索引，不代表把 #2621/#2657/#2594 的实施进度再拆给多个 owner。根 track 的四方向验收是上层完整性检查，不新增第五套实现。

## 2. 资源与访问（`RA`）

 | ID | 工作项 | 吸收的旧编号 | 依赖 | 状态 |
 | -- | ------ | ------------ | ---- | ---- |
 | **RA-1** | device-id 逻辑→driver 翻译 | `AR5` | — | 🔵 **device-id 翻译仍待实现**。本基线 `device_runner.cpp::register_device_memory_to_host` 仍将 `device_id_` 直接传给 `halHostRegister`；`acl_to_hal_device_id` 0 命中、阳性对照 `halHostRegister` 16 处。#2516 的 driver 声明与 #2539 的 A5 VMM logical ID 修复不等于此处转换。 |
 | **RA-2** | a5 能否 host-map device 内存 | `W0` | — | ⛔ **A5 direct-map 验收未满足**。#2496 OPEN 继续承接 driver SVM 版本、注册/注销、双向可见性与 consumer 验收。#2501 的 HBG HOST/NONE 是显式 host storage，不是 device GM 的 host mapping；copy-capable 分支仍可推进。 |
 | **RA-3** | 本地 attachment adapter（Host DDR ＋ 本卡 GM） | `P1-C` · `W1lite` · `W1` · `AR6` · `U2a` | RA-1 · 条件 RA-2〔只挡 a5 direct-map 那半；`W1lite` 按 arch 宏选实现，不等驱动结论就能做〕 | 🟡 **U2a 显式 HOST 消费已交付 #2501 `121d807bb`**。HostTensorAccessor 仅登记 HOST/NONE 的合法方向/范围；HBG DEVICE get/set 的隐式 mapping/copy 与 producer wait 已移除，PA/BGEMM/FIFO 等双侧消费者随 PR 迁移。原 `acquire_child_memory_host_view` 在该文件 0 命中，阳性对照 `AddressSpace::HOST` 与 `TensorTransfer::NONE` 各 1 处。U2a 合入不关闭全部本地 adapter：#2497 跨进程 registration 与 HOST_SVM_MAP producer/materializer、RA-1/RA-2 direct-map 仍待验收。 |
 | **RA-4** | arch cache maintenance 抽象 | `W4②` | RA-3 | 🟡 **边界对齐已交付，通用可见性仍有缺口**。10 月 26 日 main `daf2f8418907` 的 #2672 `71077b04a5e4` 在 DomainConfig.capture 拒绝非 64 字节倍数的 nbytes/window_size，并检查总容量；调用方显式 padding，非 runtime 自动补齐。#2273 的边界缺口已被此代码覆盖，不能再说只有窗口越界检查。单 buffer 内信号布局、publish/observe 与跨架构硬件可见性仍须验收；本轮未重跑硬件。⛱ **挡住 `RA-11`**。 |
 | **RA-5** | 显式 Transfer 与授权层 | `P1-D` · `AR7` · `AR8` · `U1a` | RA-3 | 🟡 **显式 TaskArgs-entry Transfer 已合入 #2454 `ec6a33dac`**（定点核验 main `255d78568`）。TaskArgs::transfers_与 add_tensor 为同一 view 保留逐次 request；Remote v4 只接受可表达的 location-default request，U3 仍是后续范围。copy_to/copy_from 保持 identity；统一授权与新分配 identity 的公开契约仍按本项验收，不能仅凭 U1a 合入关闭整个 RA-5。 |
 | **RA-6** | 跨任务依赖谓词 | `P1-E` 前半 · `AR9` · `ST-2` · `KV-R1` · `KV-R2` · `KV-R3` · `KV-R4` · `KV-R5` | RA-3 | 🟡 **canonical footprint 依赖已有，显式 writable alias 仍待交付**。hierarchical `lookup_overlapping`/`tensor_footprint` 仍是依赖基座；main `task_args_wire.h::validate_submit_args` 仍拒绝同任务重叠写。#2450 已 CLOSED/COMPLETED，正文契约 KV-R1–R5 并未因此在 main 实现，继续归本项；#1388 OPEN 的 WAR 是能力扩展。ST-2 按共同 backing/实际 submit 验收，不恢复 raw-VA 假设。 |
 | **RA-7** | arena 拆分 ＋ accounting | `P1-E` 后半 · `AR10` | RA-6 | 🔵 **L3 arena 分离仍待实现**。`Worker.alloc_shared_tensor` 仍调用 `self._orch._o.alloc`，创建 FORK_SHM backing，scope 生命周期仍由 hierarchical HeapRing 管。worker.py 的 SlotArena/SharedBufferArena/BufferSliceArena 均 0 命中；阳性对照 alloc_shared_tensor 2 处、实际 alloc 调用 1 处。#2571/#2596 的 run resources 与 RA-16 workspace 不是此 arena。 ⛱ **挡住 `EX-5`**。 |
 | **RA-8** | endpoint 与 capability view | `W2` · `P3①` | — | 🟡 **共同能力判定与 attachment wire 已有，消费组合仍需验收**。`select_adapter`、`_global_domain_attachment_matrix` 与 GlobalDomainAttachment 校验保留；worker.py 的 local_window_bases 仍为字段/赋值共 2 处，retain_after_run 阳性对照 4 处。#2594 的完整成员身份、rank、域引用须消费同一判定权威；SPL 机型/链路输入归本项，不另建选择器。 |
 | **RA-9** | ResourceBundle 组合多 backing / adapter | `P3②` 本地半 | RA-8 · RA-5 | 🔵 **组合契约待实现**。需同时表达 Host DDR control、Device GM payload 与 communicator，含组合失败语义。本地部分归本项，跨节点验收归 DS-10；既有 Region backing 不等于完整 ResourceBundle。 |
 | **RA-10** | 向用户暴露 plan / capability / completion | `P3③` | RA-8 | 🔵 **逐路径接口验收要求**。RA-3、RA-5、DS-3 均须向用户呈现实际 plan/capability/completion，显式 policy 不得隐藏 copy 或 identity 变化。它不另立运行机制。 |
 | **RA-11** | 最小同步原语 ＋ 首个有界 SPSC 示例 | `P3④` · `W4①` · `W6` | RA-7 · **RA-4** | 🟡 **部分交付：#2096 已合入 main**（`ae234fad0`，2026-10-08）。内部 Region Template、双向有界 SPSC、10-scalar SPSQ 与旧 facade 迁移已交付；三个核心文件与已审 head `2297a9a4f29a` 一致。方案仍每次物化专用 Region，尚非既有 Buffer/slice 的受管理绑定；publish/observe/wait 通用接口、RA-7 资源基座与 RA-4 可见性保留为 stable 退出条件。共享区间与访问/可见性保证属 runtime，数据布局、同步协议和 queue 算法属上层；SPSC 是受 CI 看护的样例，不能自动提升为公共 queue 工厂。常驻执行/停止机制归 EX-2；通用接口与受管理绑定继续归本项，公共迁移衔接 RA-13/RA-14。**#2720 是方向 1 的完整需求入口，方案仍待分析；#2635 为候选，未获方案批准。#2635 OPEN**，10 月 26 日 head `0bc9b0f690f3`；[定点评审](review/pr-2635.md) 的 finalize 身份竞态与集成冲突结论仅针对旧 head `850a36bd9`，不据此裁定新 head。公共职责以 [L3+ 设计](l3plus-design.md) 的资源机制/应用协议切分为准。合入不关闭 RA-11/P3④；具名证据见 [评审与合入记录](review/pr-2096.md)。 |
 | **RA-12** | 本地独立 per-worker domain ownership | `P2-D` 本地半 · `W3` · `#824` 本地半 | RA-8 | 🟡 **owner 转移仍在飞 #2120**（OPEN，`cd69b1e0ecf9`）；10 月 26 日 main `daf2f8418907` 的 worker.py 中 `detach_persistent_domain` 0 命中，`retain_after_run` 10 行为阳性对照；#2668 采用创建时 retention，仍在飞。L4 retained release 已能绑定后续正在构建的 run fence，仍不等于所有已接纳使用的 Worker 级 ownership。#2594 的复用、跨 run 和释放规则与本项共用 owner；契约 A/D 仍适用。 |
 | **RA-13** | 四类 worker 内存模型统一成 ABI | `l3l4 B` · `AF5` · `U1b` · `U2b` · `ST-1` | RA-3 · RA-5 | 🟡 **公共参数与 materialization 进一步收敛**。#2589 `a8c682a32` 与 #2595 `8c3b0e174` 分别让 direct L2、fork chip child 使用 native ChipWorker；#2597 `e994e106d` 使三类入口共用 `_materialize_chip_task_args`。#2587 `11eb5d2c9` 把 DEVICE source 登记绑定 native environment identity；#2612/#2614 细化失败后的 source-use 退休。旧 ChipStorage/ChipTensor ABI 兼容及 U2b 统一 IO 账本仍须单独验收；#2594 的域 Buffer/远端描述消费由 #2668 在飞承接（10 月 26 日 head `d4a77803bf1f`），不因引用这些公共合同而另建业务 owner。#2146 OPEN `247689f4e3ba`，公共 tensor.h 仍逐维比较 stride，ST-1 未被这些 PR 顺带关闭。 |
 | **RA-14** | API 收敛与迁移 | `W8` · `C.2` 的 `malloc` 半 | 就绪 RA-3 · 就绪 RA-5 · 就绪 RA-10 | 🟡 **构造和 chip 消费入口已收敛，兼容退役未完成**。#2479/#2480 提供公共 Tensor(Buffer, shapes=...)；#2501 同批迁移 HOST 控制用户，#2597 共享私有物化转换。#2594 的域 API 改名/兼容归本项，域目标主归属 DS-6、ownership 归 RA-12；全消费者迁移后才移除旧 ABI，不等全部 backend 串行交付。 |
 | **RA-15** | 统一 logical close ＋ 物理释放 | `U4` | RA-12 · RA-13 · DS-3 | ⛔ **U4 完整退出条件未满足，但 consumer 保护在推进**。#2612 `49ae1ef1b` 仅在 verified prepare rollback 后释放 source refs；#2614 `8b466791f` 补未开始 launch 的 cleanup proof。#2615 已合入 `ce493639b45f`，提供 launch 异常后的 execution owner 保留；仍不等于统一退休目标完成。Buffer.close 仍直接执行 host shm.close/unlink，统一 owner/lock/journal、独立 grant、network→H2D 及 graph/replay 联验仍保留。#1931 属方向 1，10 月 26 日 main 的 child `_CTRL_FREE` 仍 unpack Q 后 cw.free(ptr)，相邻 COPY 的 ImportRegistry.materialize 是身份校验阳性对照。#2594/#2621 的最后使用者由方向 2 完整交付，本项提供共同退休合同，不另拆其场景工作。 |
 | **RA-16** | device context workspace ownership 与预算 | `VLLM-4` 的资源半 | — | 🟡 **context 资源与预算已有进一步交付**。#2571 `78365f79c` 按 context grant 配置 run resources 并拆开传输帧/执行记录；#2596 `a2b0ad2ad` 提供四个 onboard program backend 的受限自动增长/trim/retry，#2610 `aa91a9d60` 将现有内部 workspace 管理与可选字节预算接入 borrowed eager。预算只覆盖 retained staging 和三类 pooled arena，caller tensor、代码、DFX、provider 不在其中；不等于整设备 OOM 恢复，也不替代 RA-7/RA-15。 |

## 3. 分布式会话与通信域（`DS`）

 | ID | 工作项 | 吸收的旧编号 | 依赖 | 状态 |
 | -- | ------ | ------------ | ---- | ---- |
 | **DS-1** | MPI 会话的 session/epoch registry | `P2-A` | — | 🟡 **Direct MPI 身份与逐 rank 清理观测部分交付**。10 月 26 日 main `daf2f8418907` 已含 #2656 的 MpiSessionView/session_epoch、hub failure/close/cancel 失效与 endpoint planning/materialization 拒绝；#2665 `ce181c8cdacf` 提供外部 receiver、有界逐 rank 报告、身份/sequence 校验与缺失 UNKNOWN。源码正例是 MpiDirectCleanupLedger 与 run_supervisor；主等待仍为无 deadline 的 proc.wait()，最终输出不等于持久账本/恢复闭环。#2659 仍需持久化、独立关闭期限、故障回收及 Group 迁移；#2598 OPEN head `7857e73fc5a1` 的 Group/TCP 观测须与 Direct 交付去重，不能再称所有逐 rank 报告都待开发。业务归属方向 4。 |
 | **DS-2** | device lease ＋ rank inventory ＋ 冻结 global rank | `P2-B` 的清点半 | DS-1 | ⛔ **生产清点/冻结验收未满足**。归属为 endpoint inventory、device lease、global rank 编号；PREPARE/COMMIT/ABORT 的执行归 DS-9。 |
 | **DS-3** | remote transfer / attachment：两条 copy 语义分清 | `P2-C` · `U3` | DS-2 · RA-5 | ⛔ **U3 合并验收未满足**。现有 owner-delegated socket copy 不能算 device-peer/collective。#2460 要求 producer/receiver/version 成对迁移、保留 entry request/direction/grant/identity/range、不得 receiver 默认 H2D 或把 host transport storage 标成 DEVICE；network→H2D 故障时保留中间资源。 |
 | **DS-4** | 远端 domain lease、断连与回收 | `P2-D` 远端半 · `#824` 远端半 | DS-3 · RA-12 · **DS-9** | ⛔ **远端 lease/断连回收退出条件未满足**。与 RA-12 共用域生命周期；DS-9 提供建域事务。依赖表示完整验收，不妨碍本地 owner 转移原型先交付。 |
 | **DS-5** | A3 HCCS/RoCE ＋ A5 UB/URMA profile | `P2-E` · `D.2` 的 backend 半 | DS-4 | 🟡 **已有 A3 Fabric 与 A5 RDMA/URMA 接入，完整 backend 组合仍待验收**。历史数据面证据沿用 #1456/#1623/#1624/#2157 及通信方案 D.4。10 月 26 日 main `daf2f8418907` 的 A5 init_urma_workspace 仍调用 UrmaWorkspaceManager::Init，未显式选择布局；#2708 → #2673 承接并发队列，#2674 的编译能力衔接 DS-11。#2675 OPEN `cd5e26f0d9ac` 与 #2650 OPEN `e22f05a79837` 均触及 URMA 完成/资源寿命，需合流而非重复改造；它们不交付 Host DDR fetch 或多 AIV shared-pool 目标。L3+ 必需的 L2 初始化、通信 kernel、队列和 completion 属方向 2；通用 scheduler 改造仍属外部轨。本轮仅源码/PR 范围核验，不宣称硬件 PASS；具体 profile、非有限值验金及 #2026 仍需独立验收。 |
 | **DS-6** | mpirun 启动与发现 ＋ 跨-L3 域接口 | `A.1` 残留 · `D.4` · `C.2` 的 L4 半 | — | 🟡 **统一建域与 direct MPI 部分交付；完整目标分别归方向 2/4**。main `daf2f8418907` 保留 #2634 `106d60b81295` supervisor/controller/executor 与 #2639 `3b58364657c1` 统一 allocate_domain、DomainConfig 精确复用；#2656/#2665 的会话和清理证据见 DS-1。方向 2 的 #2594 由 #2668 OPEN head `d4a77803bf1f` 继续承接 Buffer/Tensor 消费、跨 run 使用与释放，其声明 Closes 不作为已合入/验收。#824 的长期 owner 目标与 #2120 保持关联，不因相似自动关闭。方向 4 的 #2659 承接清理闭环、Group 迁移/退役；普通 TCP/daemon 不因此列为删除目标。RA-12/RA-13/RA-14/RA-15、DS-3/DS-9 作为共同合同联验，不拆第二份业务 owner。 |
 | **DS-7** | 移除或重构 64-rank device ABI 上限 | `P3⑤` | DS-2 | 🔵 **独立规模扩展包**。device rank ABI 的 64 上限需要独立兼容与容量验收；不阻止小规模异构域先行。 |
 | **DS-8** | host 参与域的语义（非对等成员） | `D.3` | RA-8 · 条件 RA-2〔仅 A5 direct-map 分支；copy-capable host attachment 走 `OWNER_DELEGATED_COPY`，不需要它〕 | ⛔ **host attachment 的组合验收仍未满足，已有具体在飞路径**。10 月 26 日 #2685 OPEN `88736dbcce54` 为 A3 L4 全局域增加 Host DRAM，依赖 #2668；#2714 OPEN `cf384c94a5e9` 为 direct L2 提供单 A5 同步本地 Host DDR 读取。二者是方向 2 的候选交付，不互相证明对方平台/拓扑，更不关闭 #2657 的跨 rank 异步 fetch。Host 不占 NPU rank；CPU 远端访问与 device 可访问是不同能力。仅 A5 direct-map 分支受 RA-2 条件限制；copy-capable attachment 不等待全部驱动能力。 |
 | **DS-9** | 成员事务执行（唯一设计归属） | `W5` · `P2-B` 的事务半 | DS-1 · DS-2 | 🟡 **PREPARE/IMPORT/COMMIT/ABORT 事务已有，生产闭环未完成**。_allocate_global_domain 的普通/MPI ABORT 失败都调用_record_unreclaimable；SPL-1.6 的“只写 stderr 后继续”已被源码反证。epoch/lease、冻结成员、全员 READY、失败资源可追踪仍需与 DS-1/DS-2 联验，不重复设计现有事务。 ⛱ **挡住 `DS-4` `DS-10`**。 |
 | **DS-10** | 跨节点异构域的组合与验收 | `D.5` 残留 · `P3②` 跨节点半 | DS-4 · RA-9 · **DS-9** | 🟡 **跨机基本栈已交付**（#1456/#1623/#1624）。异构 ResourceBundle 的跨节点组合仍须 session、成员事务、授权及失败回收共同验收；与 RA-9 各负责一半。 |
 | **DS-11** | 同步与异步模型（SDMA / URMA gate） | `D.2` 的 opt-in 半 | — | 🟡 **backend 完成与能力按路径限定，#2708 子项独立验收**。#2674 要求读取已编译 runtime 的 URMA 支持或显式要求后拒绝，不从运行时环境变量猜编译能力；支持编译也不等于 workspace 初始化成功。#2673 与 #2674 不互为前置；#2650/#2675 的 completion/workspace 合流消费此合同。请求已发出、DMA 完成与可安全复用必须分开；#2657 的 Host 注册、issue/data completion/DAG 接入完整归方向 2。已有 #2115 simulator SDMA 不是 URMA 真机证明，#2026 默认策略仍单列。 |

## 4. 执行与常驻运行（`EX`）

 | ID | 工作项 | 吸收的旧编号 | 依赖 | 状态 |
 | -- | ------ | ------------ | ---- | ---- |
 | **EX-1** | responsive local edge：覆盖面与验收 | `l3l4 E` · `P1-F` · `AF2` · `AF4` | — | 🟡 **local mailbox/control 与 grant 传输已有，扩展组合验收未完成**。#2571 将固定请求帧与按 grant 的 run-state entries 分离；NEXT_LEVEL 构造传 entry.granted_sets，SUB 构造仍使用默认值。旧 task_frame_count 双帧描述已不适用于当前构造。#2116 OPEN `6b8d56d4b54f` 承接 control extension；nested/remote/sim 的长期任务、取消、health 按组合验收。 |
 | **EX-2** | 单 L2 Run 常驻：多轮 work/completion ＋ STOP/drain | `W7` · `AF1` | EX-1 | 🟡 **单 L2 Run 多轮交互已有示例，通用常驻能力未验收**。10 月 26 日 main `daf2f8418907` 的 worker_chip_message_queue 只 submit_next_level 一次，随后多轮 enqueue；chip orchestration 的 for (;;) 处理 DATA/STOP，停止前 finish_pending_inputs 并检查 drained。它是 #2723 的现有基座，不等于完整公共方案：work/completion 关联、槽位寿命与复用、背压/progress、停止准入/drain/异常退出及支持矩阵须共同验收。L3 进程/mailbox 多次接任务、重复 Worker.run 和单 L2 Run 常驻分别计证；AF1 的 idle 阻塞唤醒保留为历史交接，不再代指常驻目标。派发路径不得 sleep/backoff；#2720/RA-11 不预先批准 SPSC 公共 ABI。 |
 | **EX-3** | admission · 定向调度 · **零 heap 运行验收** | `l3l4 C.3` 的 admission 半 | — | 🟡 **定向与 whole-run FIFO 已交付；零 hierarchical heap 的运行验收仍单列**。这是 feature_roadmap §2.5 第 5 步，不要求先删除结构。#1137 已 CLOSED 且原属 L2 polling 优化。#2476 `dfa63eb81` 已合入：retention 测试使用 1024/4096/8192 递增窗口、无在飞 read 基准及诊断；#2470 已 CLOSED。合入测试不等于所有 overlap 或性能目标完成，EX-7 的真实 enqueue/refill 仍按其独立验收。 |
 | **EX-4** | 执行接口与 callable 身份收敛 | `l3l4 C.2` 的 `run`/`orch`/callable 半 · `AF3` | — | 🟡 **公共 callable/执行接口仍需收敛验收**。AF3 的 pre-init live-object 与 digest 快照契约由 #1938（OPEN，`47e60a866df1`）提供可失败的观测；run/orch/callable 统一不得靠改名替代身份验证。 |
 | **EX-5** | 删除 heap-coupled scope reclaim | `l3l4 C.3` 的 heap 结构半 | RA-7 · EX-3 | ⛔ **使用者迁完后才能删除结构**。RA-7 提供 §2.5 第 1–4 步，EX-3 提供第 5 步；本项是第 6 步。L2 TMR device GM heap 不在删除范围。 |
 | **EX-6** | 终态回收 · child freeze · nested-shm 枚举 | `P0.2-f4` · `P0.2-f5` · `P0.2-f6` · `P0.2-f7` | — | ✅ **f4–f7 已交付，当前基线复核**。#1689 `f8106e24e` 已提供 `_teardown_worker_tree`：pre-transport 全集先入 post-success journal，失败保留 transport；remote frees 与 session shutdown 分相；child/shm 有独立重试。`add_once(kind, identity, cleanup)` 的数据表含 region/domain/provenance/remote/buffer，不能只 grep `.add("child")`。`test_cancellation_journal.py::test_both_teardown_paths_retry_each_resource_class` 与 namespace/child 顺序案例在树上（本轮读测试，未重跑）。既有 f5 双锁、f6 survivor 重试、f7 确定名与 namespace sweep 均保留；新增 U4/device-workspace 的寿命目标分别归 RA-15/RA-16，不倒灌为 P0.2 遗留。 |
 | **EX-7** | 连续 run 共享 stream 的顺序契约（Track A） | `TrackA-P1` · `TrackA-P2` · `TrackA-P3` · `TrackA-P4` · `TrackA-P5` · `TrackA-P6` · `VLLM-1` · `VLLM-2` · `VLLM-3` · `VLLM-4` 的 admission 半 | EX-3 | 🟡 **四个 onboard program backend 的限定 early enqueue、可配置资源与自动准入已有交付**。#2560 `a89c1170c` 补 A3 TMR；#2571 `78365f79c` 拆分 transport/run lifetime；#2596 `a2b0ad2ad` 对未显式配置的 startup-root 本地 L3 direct program endpoints 按完成资源 commit 增长容量；nested/remote/MPI、L2、kernel、borrowed、sim 不在自动模式范围。#2611 `0eda81c93` 拒绝同线程递归 native prepare。#2511/#2568/#2592 已交付 Qwen clone、同请求 DEVICE feedback、限定持续多请求 driver；自动准入与真实 Qwen 组合、动态到达和性能验收仍独立。TrackA-P6 归本项，资源部分联验 RA-16；#2588 DFX 已合入 `545c05a43469`，#2605 vLLM eager 仍为范围外交接。#2028 CLOSED 未合入，#2425 历史故障已关闭，新 HBG 卡住由 #2559 承接；#2615 launch 异常 owner 修复已合入 `ce493639b45f`。 |

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

### 本轮新增目标与候选排期归属（2026-10-18）

候选排期来自 simpler-pr-list（本地参考：`simpler-pr-list.html`），用 `SPL-` 命名空间避免与阶段编号混淆；编号是验收/能力目标，不承诺一条一 PR。阻塞只约束完整退出，不禁止已有路径先行。

 | 来源编号 | 唯一主归属 | 交接/边界 |
 | -------- | ---------- | --------- |
 | `SPL-1.1` · `SPL-1.2` | DS-1 | cleanup ledger 与 epoch/lease 分开；#2598 仅观测 |
 | `SPL-1.3` | DS-2 | 按本项既有契约验收 |
 | `SPL-1.4` · `SPL-1.6` | DS-9 | SPL-1.6 poison 已有，保留失败回收验收，不重复开发 |
 | `SPL-1.5` · `SPL-1.9` | DS-6 | 按本项既有契约验收 |
 | `SPL-1.7` | RA-12 | 按本项既有契约验收 |
 | `SPL-1.8` | DS-4 | 按本项既有契约验收 |
 | `SPL-2.1` · `SPL-2.2` · `SPL-3.1` · `SPL-3.2` · `SPL-3.3` · `SPL-3.5` · `SPL-3.8` · `SPL-3.9` | DS-5 | CCU/MTE 与机型适配为候选能力；RDMA producer 已有，先验能力再排实现 |
 | `SPL-3.4` · `SPL-3.6` · `SPL-3.7` · `SPL-3.10` | RA-8 | 机型/链路是共同 capability 输入；不新建第二选择器 |
 | `SPL-4.1` · `SPL-4.5` | RA-3 | 按本项既有契约验收 |
 | `SPL-4.2` · `SPL-4.3` · `SPL-4.6` | DS-8 | host 为 attachment 不占 rank；copy 分支不等 direct-map |
 | `SPL-4.4` | RA-2 | 按本项既有契约验收 |
 | `SPL-5.1` · `SPL-5.2` · `SPL-5.3` · `SPL-5.4` · `SPL-5.5` · `SPL-5.6` · `SPL-5.7` · `SPL-5.8` | DS-11 | 完成事实联验 DS-3；内部 workspace 联验 RA-16 |
 | `DOMAIN-A1` · `DOMAIN-A3` · `DOMAIN-A4` · `DOMAIN-A7` | DS-6 | #2594 接口/配置/事务竞争/两层端到端；RA-14、DS-9 联验 |
 | `DOMAIN-A2` | RA-12 | #2594 同名同配置复用 identity/内容/handle；与 #2120 共用 owner |
 | `DOMAIN-A5` | RA-13 | #2594 域 Buffer 经 L3 到正确 chip；DS-3 负责远端 request/grant |
 | `DOMAIN-A6` | RA-15 | #2594 跨 run/borrower/close 最后使用者；RA-12、DS-4 联验 |
 | `SESSION-R1` · `SESSION-R2` · `SESSION-R3` | DS-1 | #2598 command acknowledgement/process exit/resource release 三种结果独立；不是远端 lease 完成声明 |
 | `VLLM-S3F-S1` · `VLLM-S3F-S2` · `VLLM-S3F-S3` · `VLLM-S3F-S4` · `VLLM-S3F-S5` · `VLLM-S3F-A1` · `VLLM-S3F-A2` · `VLLM-S3F-A3` · `VLLM-S3F-A4` · `VLLM-S3F-A5` · `VLLM-S3F-A6` | EX-7 | #2429 最终 Step 3 评论：RA-16 资源联验；adapter/硬件运行由 Qwen 轨实施，不以 #2592 关闭自动组合 |

SPL 本地别名归一：`SPL-C4`/`SPL-C5`/`SPL-C8`/`SPL-C11`/`SPL-C12`/`SPL-C13`/`SPL-C14` → DS-5；`SPL-C6`/`SPL-C9`/`SPL-C10` → RA-8；`SPL-C7` → DS-11；`SPL-M2` → DS-8。公共 backend 能力先证明再排期，不因旧表写“新建”而新增重复 provider。

### #2716 原生子树的完整目标与验收索引（2026-10-26）

这些是来源索引，不是新建实施工作项。`SCOPE` 覆盖整条 issue，`A` 顺序对应其正文 checkbox，`R` 对应具名提案编号；#2674 两种 API 是备选。现有 U1a–U4、DOMAIN-A1–A7 原合同归属保留。GitHub issue 的完整目标所有权优先于本地横切合同索引。

 | 来源编号 | 本地合同索引 | 业务归属 |
 | -------- | ------------ | -------- |
 | `T2716-SCOPE` · `T2716-A1` · `T2716-A2` · `T2716-A3` · `T2716-A4` · `T2716-A5` · `T2716-A6` | 四方向共同验收 | #2716 完整交付不拆 owner；以来源正文和 [track 分析](review/track-2716.md) 为准 |
 | `T2721-SCOPE` · `T2721-A1` · `T2721-A2` · `T2721-A3` · `T2721-A4` | RA/EX 公共合同 | #2721 完整交付不拆 owner；以来源正文和 [track 分析](review/track-2716.md) 为准 |
 | `T2722-SCOPE` · `T2722-A1` · `T2722-A2` · `T2722-A3` · `T2722-A4` | DS-5（通信合同索引） | #2722 完整交付不拆 owner；以来源正文和 [track 分析](review/track-2716.md) 为准 |
 | `T2723-SCOPE` · `T2723-A1` · `T2723-A2` · `T2723-A3` · `T2723-A4` | EX-2 | #2723 完整交付不拆 owner；以来源正文和 [track 分析](review/track-2716.md) 为准 |
 | `T2724-SCOPE` · `T2724-A1` · `T2724-A2` · `T2724-A3` · `T2724-A4` · `T2724-A5` | DS-6（部署合同索引） | #2724 完整交付不拆 owner；以来源正文和 [track 分析](review/track-2716.md) 为准 |
 | `T2460-SCOPE` · `T2460-A1` · `T2460-A2` · `T2460-A3` · `T2460-A4` · `T2460-A5` · `T2460-A6` · `T2460-A7` · `T2460-A8` · `T2460-A9` | RA-13（U1a–U4 原归属保留） | #2460 完整交付不拆 owner；以来源正文和 [track 分析](review/track-2716.md) 为准 |
 | `T1931-SCOPE` | RA-15 | #1931 完整交付不拆 owner；以来源正文和 [track 分析](review/track-2716.md) 为准 |
 | `T2720-SCOPE` · `T2720-A1` · `T2720-A2` · `T2720-A3` · `T2720-A4` | RA-11 | #2720 完整交付不拆 owner；以来源正文和 [track 分析](review/track-2716.md) 为准 |
 | `T2594-SCOPE` · `T2594-A1` · `T2594-A2` · `T2594-A3` · `T2594-A4` · `T2594-A5` · `T2594-A6` · `T2594-A7` | DS-6 | #2594 完整交付不拆 owner；以来源正文和 [track 分析](review/track-2716.md) 为准 |
 | `T824-SCOPE` | RA-12 | #824 完整交付不拆 owner；以来源正文和 [track 分析](review/track-2716.md) 为准 |
 | `T2621-SCOPE` · `T2621-A1` · `T2621-A2` · `T2621-A3` · `T2621-A4` · `T2621-A5` | DS-3（RA-13/RA-15 联验） | #2621 完整交付不拆 owner；以来源正文和 [track 分析](review/track-2716.md) 为准 |
 | `T2657-SCOPE` · `T2657-A1` · `T2657-A2` · `T2657-A3` · `T2657-A4` · `T2657-A5` · `T2657-A6` · `T2657-A7` · `T2657-R1` · `T2657-R2` · `T2657-R3` · `T2657-R4` · `T2657-R5` | DS-8（DS-5/DS-11/RA-15 联验） | #2657 完整交付不拆 owner；以来源正文和 [track 分析](review/track-2716.md) 为准 |
 | `T2708-SCOPE` | DS-5 | #2708 完整交付不拆 owner；以来源正文和 [track 分析](review/track-2716.md) 为准 |
 | `T2673-SCOPE` · `T2673-R1` · `T2673-R2` · `T2673-R3` | DS-5 | #2673 完整交付不拆 owner；以来源正文和 [track 分析](review/track-2716.md) 为准 |
 | `T2674-SCOPE` · `T2674-R1` · `T2674-R2` | DS-11 | #2674 完整交付不拆 owner；以来源正文和 [track 分析](review/track-2716.md) 为准 |
 | `T2659-SCOPE` · `T2659-A1` · `T2659-A2` · `T2659-A3` · `T2659-A4` · `T2659-A5` · `T2659-A6` · `T2659-A7` · `T2659-A8` · `T2659-A9` · `T2659-A10` · `T2659-A11` · `T2659-A12` | DS-1（DS-6 迁移联验） | #2659 完整交付不拆 owner；以来源正文和 [track 分析](review/track-2716.md) 为准 |

## 7. 不属于这三条线的活

本重组排除通用 kernel 模式/eager-capture、HBG 硬件规划及其内部调度工程；**L3+ 场景必需的 L2 资源访问、通信初始化、通信 kernel 与 completion 不在排除之列**，按 #2716 的完整目标归属。因此下列通用工作不进三条线，
但 [`README.md`](README.md) §1.1「目标 main 的 PR / issue 都要占一行」**仍然适用** ——
它们登记在 §1.2「不在本体系内」：

- **kernel 模式**：维持用户既定排除；独立栈按 §1.2 登记，完整编号与原则见 [覆盖清单](goal-coverage.md)。是否第四线仍待用户裁决，不能用栈已合入推导批准。
- **runtime / 调度器与 HBG 硬件需求**：GE 内部实现和硬件预算归各自维护者；我们的 GE-R1–R6 交接要求仍在 §6，不能随排除项消失。
- **DFX**：`VLLM-5`（continuous DFX）由 collector/runtime 的独立交付负责；#2588 已合入 `545c05a43469`，具体验收仍按 collector/backend，不能由普通 run 的 early enqueue 推导 DFX 完成。#2465 `02fd1f63e` 与 #2483 `92a14e4e7` 已分别交付 scope-stats、TMR dep-gen 后台输出，#2484 `8aae172e5` 修 A5 错误关闭的 run identity 传参；#2490 `01bf24bfa` 已交付 HBG 后台依赖图输出，发布证明来自 host capture，区别于 TMR device record 的完成/隔离条件；run 返回与产物发布分离，flush_diagnostics 是产物屏障。它们不等于任意 collector/runtime 的 continuous DFX 全部完成。T7 跨 host 部分由 #2217/#2304 跟踪，不能用本地 #2152/#2218 的交付关掉 #2134 全范围。
- **Qwen / vLLM adapter 与跨仓验收**：`Q1` `Q2` `Q3` `Q4` `C2b` `V1`、`VLLM-6` `VLLM-7` `VLLM-8` `VLLM-9` 归现有 Qwen 集成轨及 #2429 的 adapter/capture/E2E 交付；程序 admission 归 EX-7，资源账本归 RA-16。#2447/#2456 的 depth-one/fallback、#2511 的双请求 clone、#2568 的 DEVICE feedback、#2592 的固定形状持续多请求均已合入；#2605 vLLM eager 仍 OPEN，自动资源准入与真实模型组合及端到端性能继续独立验收。
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

候选跨仓编号 `SPL-P.1` · `SPL-P.2` · `SPL-P.3` · `SPL-E1` 归外部 pypto；`SPL-L.1` · `SPL-L.2` · `SPL-L.3` · `SPL-L.4` · `SPL-E2` 归外部 pypto-lib，交接分别为 DS-6 与 RA-8/DS-5。它们是候选范围，不表示本轮跨仓代码或硬件已验收。

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
