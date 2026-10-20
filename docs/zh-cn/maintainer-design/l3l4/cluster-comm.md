# 通信 review 与改造

> **发布快照：2026-10-19。** 设计、实现状态与历史调查须按正文各自基线阅读；不代表当前 main 全量验收。参见[阅读入口与已知更正](../README.md)。
>
> **当前启动/建域/数据面契约见 §D.4**（`68d137fdf`，2026-10-13）。§D.5 保留旧调查与反证史，不作为实施清单；状态只引 DS-1/DS-5/DS-6/DS-9/DS-10。
>
> ⛔ **本文 §D.5 描述的机制有一半已被实现取代 —— 引用前必须先核对。**
> （措辞与 [`memory-kinds.md`](memory-kinds.md) 的 ⛔ banner 同源，处置方式相同。）
>
> **具体作废的是「remote 域 opcode 是桩」这条**：正文两处（`:209` `:265`）仍写
> remote session 收到域 opcode *"直接回错 `unsupported reserved remote domain control`"*。
> ⇒ **该字符串全仓 0 命中**（核验 `upstream/main@d1eb826f`；正对照：`region_registry()`
> 在 `python/bindings/task_interface.cpp` 19 命中 ⇒ grep 姿势没问题）。
> `remote_l3_session.py` **已实现 `COMM_INIT` / `ALLOC_DOMAIN`**（实测该文件 11 命中），
> 带 cluster_id / profile / node identity / topology hash / local worker id /
> global device rank 校验。
>
> ⚠️ **而 §D.5 顶部原有的那条 banner 自己也漂了**：它写 #1456 / #1623「正在实现」——
> **两者早已 merged**（`60ab6ab9` / `cc9bfc45`）。⇒ **一份「快照」banner 会和它所保护的
> 正文一起过期**，这就是为什么本 banner 只写「哪一条作废 ＋ 怎么核」，不写进度。
>
> ✅ **仍成立的**：D.3 关于 HCCL 的更正（`CommEngine` 0 命中 ⇒ 未实现的设计输入，
> 与本条相反极性 —— **别一起打折**），以及域生命周期仍 run-scoped（issue #824，
> 归 P2-D，见 README §「每条 track 的下一步」）。
>
>
> 来源：原 `l3l4-cluster-requirements-analysis.md` §D + D.1–D.5。

## D. 通信 review 与改造

> 状态图例:**已实现**(sim + onboard 均通)/ **部分实现**(仅 sim 或仅部分 arch onboard)/ **设计已存在未落地**(有协议位/设计文档但代码是桩)/ **未实现**。
> **当前定位基线为 `main@b69f4537`，函数/类型名是权威锚点。** 下方保留的裸
> `file:line` 只用于复核原调查快照，行号漂移时不得据其否定同名函数/类型的当前语义。

 | 诉求 | 状态 | 一句话定性 |
 | ---- | ---- | ---------- |
 | **D.1** L3 为多 L2 建域 | 已实现(sim + a2a3/a5 onboard) | `orch.allocate_domain` 为同一 L3 下的 chip 子集建对称 device 窗口 |
 | **D.2** 同步/异步模型 | 同步已实现；异步按 capability 分层 | 集合=域内同步 barrier；host↔device=counter notify/wait；A2/A3 SDMA 默认关、显式 opt-in；**A5 SDMA 自 #1615 起默认开**（URMA 变显式备选） |
 | **D.3** 对端无感/host+device 同域 | 对称同域**未实现**;host↔L2 点对点**部分实现** | host 不是域内 rank；URMA backend 存在不等于 host-peer domain 已可用 |
 | **D.4** remote L3 通信 | 见 DS-6 | 主路径为 mpirun + MPI mailbox；A3 Fabric 共享窗口的数据访问独立于 task/control transport。见 §D.4。 |
 | **D.5** L4 为多 L3 建域 | 见 DS-9 / DS-10 | PREPARE_EXPORT/IMPORT/COMMIT/ABORT 已有；异构组合与生产回收按工作项验收。历史调查见 §D.5，当前契约见 §D.4。 |

---

## D.1 L3 为多个 L2 构建通信域

### D.1(a) 当前代码状态 — **已实现**

一套完整的 "L3 为其手下多个 L2(chip)动态构建对称通信域" 机制已落地,sim 与 a2a3/a5 onboard 均通:

- **用户入口**:`Orchestrator.allocate_domain(*, name, workers, window_size, buffers)`(`python/simpler/orchestrator.py:321`),参数归一化后转发 `Worker._allocate_domain`。
- **host 侧 fail-fast 校验**(orch 线程,dispatch 前):`python/simpler/worker.py:3870` — 要求 `_initialized`、`level>=3`、hierarchical worker;`workers` 是本 L3 `device_ids` 下标子集、非空/无重复;`window_size>0`;buffer 名唯一;`sum(b.nbytes) <= window_size`(否则 `ValueError`,防止 chip 侧已 `aclrtMalloc` 成功而父侧漏 handle 泄漏)。
- **惰性 base communicator**:首个 allocate 触发 `_ensure_comm_base`(`worker.py:3802`),并行给每个 chip 发 `CTRL_COMM_INIT`,chip child 跑 `cw.comm_init` → HCCL `comm_init`(RootInfo 握手);`_comm_base_ready` 置位后后续 allocate 跳过,**base 只建一次、跨 run 复用**。
- **per-chip 分发**:`worker.py:3907` 造 request shm,关键映射是 base rank == chip 在 `device_ids` 的下标、`domain_rank` == 在 `workers` 列表的稠密位置;`_dispatch_control_domain`(`worker.py:4089`)每 chip 一条阻塞线程,让 chip 侧 file-barrier 收敛。
- **chip child 执行**:`_handle_ctrl_alloc_domain`(`worker.py:1007`)调 `cw._impl.comm_alloc_domain_windows(...)` 返回 `(device_ctx, local_window_base)`,按声明顺序切 `buffer_ptrs`。
- **父侧组装**:`worker.py:3954` → `ChipDomainContext`(`task_interface.py:781`)+ `CommDomainHandle`(`task_interface.py:792`),记入 `_live_domains`。
- **device_ctx / CommContext ABI**:`src/common/platform_comm/comm_context.h:44`(固定 1056B,与 pto-isa `HcclDeviceContext` 逐字节对齐:`workSpace`/`rankId`/`rankNum`/`winSize`/`windowsIn[64]`/`windowsOut[64]`)。kernel 通过 `windowsIn[peer_rank]` GVA 用 MTE2 DMA 读对端窗口。
- **跨进程对称窗口**:onboard 走 `domain_alloc_via_ipc`(`src/a2a3/platform/onboard/host/comm_hccl.cpp:765`)—— ACL VMM `aclrtMallocPhysical`+`aclrtReserveMemAddress`+`aclrtMapMem`+`aclrtMemExportToShareableHandle`(带 `DISABLE_PID_VALIDATION`)→ file barrier 广播 (pid,device_id,handle) → `aclrtDeviceEnablePeerAccess` → `aclrtMemImportFromShareableHandle` 逐 peer 导入 → 组 `CommContext` 上传成 device_ctx。sim 走 `comm_sim.cpp:458`(单 POSIX shm 切片 + 原子 ready barrier)。
- **示例**:`examples/workers/l3/allreduce/main.py:148`(单域 `workers=list(range(nranks))`)、`examples/workers/l3/domain_rank_map/main.py:172`(两重叠域 + 稠密 domain_rank)。权威文档 `docs/comm-domain.md`。

> 对照 HCCL 官方机制:simpler **只用了 HCCL 四种建域方式里的一种**——`HcclGetRootInfo` + `HcclCommInitRootInfo`(root 广播法),而 rootInfo 用**共享文件系统的文件**分发(`comm_hccl.cpp:266` 附近,rank0 写 `rootinfo_path`,其余 rank 轮询);全仓未使用 `HcclCommInitClusterInfo`/ranktable/`HcclCommInitAll`/`HcclCreateSubCommConfig`。子域切分是 simpler 自研的 VMM 共享句柄路径(`comm_alloc_domain_windows` / `comm_derive_context`),**不受 HCCL "子域不可嵌套" 限制** —— 这对 L4→L3→L2 的嵌套结构反而是优势。

### D.1(b) 相关 Issue/PR

- **#817 merged** — orch-driven dynamic CommDomain(Option A):把域从 init-time 静态声明改成 orch fn 内 `allocate_domain` 动态分配,是当前模型的形态来源。
- **#809 / #813 merged** — 多域类型(`ChipDomainContext`/`CommDomainHandle`)与 cleanup、删 dead `comm_create_subcomm`。
- **#1334 merged**(落地 **#1037**)— 把跨进程 window 从 PCIe 上会报 507899 的 `aclrtIpcMem` 换成 ACL VMM shareable-handle,是当前 onboard 对称窗口的实现基座。
- **#807/#842/#888/#1267 merged** — allreduce 2..16 rank / AllGather / ReduceScatter / broadcast / all_to_all,跑在域之上,验证多 L2 同域可用。

#### 结论:D.1 已被这批 PR 完整推进到可用状态,无需新建机制

### D.1(c) 差距

D.1 本身没有功能缺口。唯一牵连的是 **#824(OPEN)**:域窗口不跨 `Worker.run` 持久化(见 D.5 与本节末尾),TP/DP/EP 组在多步循环里被反复 `aclrtMalloc`+IPC+barrier 重建。

### D.1(d) 建议 / 落地路径

- **直接采用现有基线**:`examples/workers/l3/{allreduce,domain_rank_map}` 就是 D.1 的对齐样例,梳理时以它们为准。
- 持久化诉求(#824)作为独立、低风险的第一步单独推进(见文末"跨节说明")。

---

## D.2 同步 / 异步通信模型

### D.2(a) 当前代码状态

通信栈分三层,三种语义**不要混称"异步"**:

#### 1. 集合通信(域内同步 barrier)—— 已实现

5 种 allreduce(`onephase`/`twophase`/`ring`/`bidirectional_ring`/`ibing`)+ allgather/reducescatter/broadcast/all_to_all,全部构建在 D.1 的对称窗口之上。AICore/AIV kernel 通过 `CommContext.windowsIn[]` GVA 做跨-rank remote read/write(`TLOAD`/`TSTORE`/`TPUT<Atomic>`)+ `TNOTIFY`/`TWAIT` signal-counter barrier,**整个 collective 在单个 AIV task 内同步 inline 跑完**。barrier 两种实现:O(P) mesh(`allreduce_onephase_kernel.cpp:116`)vs O(1) NeighborBarrier(`allreduce_ring_kernel.cpp:70`,只 notify-right/wait-left)。远端指针 `CommRemotePtr(ctx, localPtr, pe) = windowsIn[pe] + (localPtr - windowsIn[rankId])`。

#### 2. L3-L2 host↔device 通道(counter notify/wait)—— 已实现

- **orch-comm region**(`docs/l3-l2-orch-comm.md`):一个 GM region(payload byte range)+ int32 signal counter(`notify`/`test`/`wait`,`Set`/`Add`,EQ/NE/GT/GE/LT/LE)。稳态数据路径**跑在 L3 Host 进程**:sim 直接 map parent shm;onboard 通过 VMM shareable-handle import + ACL copy。L3 `counter.wait(timeout)` **阻塞 L3 Host 线程**,`orch.copy_to` 是**同步** H2D。
- **message queue**(`src/common/platform/include/aicpu/worker_chip_message_queue.h:319`,`WorkerChipQueueEndpoint<MaxInflight>`):建在 orch region 之上的有序 SPSC 队列,`enqueue`/`peek`/`dequeue` 有 blocking 与 `try_*` 两套;#1236 的 L2 input window 支持 N 个 in-flight handle + prefix-release,让 L2 跑持久 receive/process/publish 循环。

#### 3. 片内 tpush/tpop GM-FIFO —— 已实现,但非跨卡

`docs/tpush-tpop.md`,用于 MIX kernel 跨 Cube/Vector 核的生产者-消费者 FIFO —— **L2 片内跨核,不是跨卡通信原语**,列此仅为与跨卡 TPUT/TGET 区分。

#### 4. 异步(deferred completion)—— 按 backend capability 分层

- **A2/A3 SDMA async completion**：`TGET_ASYNC`/`TPUT_ASYNC` 返回 `AsyncEvent`，producer kernel **不阻塞**等 DMA；`register_completion_condition` 把 event-record 写入 per-task `DeferredCompletionSlab`，AICPU 的 `drain_aicore_completion_mailbox_locked` 单消费者轮询并 retire。**被推迟的是 task 的逻辑完成，AICore 核本身不阻塞。** Provider 在 A2/A3 onboard 构建中始终编译，但 `Worker(..., enable_sdma=False)` 默认不 provision；只有显式 `True` 才创建 worker-lifetime workspace/streams。独立 `@pytest.mark.sdma` onboard ST 证明 opt-in 路径，不代表默认常开。
- **集合通信的 NPU 异步被阻断**:IBing forward phase 在 **sim** 用 `TPUT_ASYNC`+`WaitAll`,**NPU 仍走同步** `TPUT<AtomicNone>`(`allreduce_ibing_kernel.cpp:252` 是同步回退;async SDMA helper 触发的 AICore loader 问题记于注释 `:30/:96`,归因 issue **#900**——**已 CLOSED**)。
- **A5 SDMA overlay 默认门控 OFF**(`docs/a5-sdma-overlay.md`,#1315:CANN 缺 `aclnnShmemSdmaStarsQuery`),`CommContext.workSpace==0` 时 producer kernel self-skip。
- **A5 URMA backend 已实现但不可按默认构建直接执行**：#1392 已合并 AICore submit、AICPU `UrmaCqCtx` CQ poll/retire 和 host overlay 接线；device path 仍受 `PTO_URMA_SUPPORTED` capability gate，host `SIMPLER_ENABLE_PTO_URMA_WORKSPACE` 默认 OFF，且与 A5 SDMA workspace 互斥。因此准确状态是 **backend merged / capability+workspace gated**，不是“URMA 未实现”或“URMA 已可用”。

### D.2(b) 相关 Issue/PR

- **#1130 / #1187 / #1236 merged** — L3-L2 message queue 底座 / 示例 / L2 input window(异步流水载体)。
- **#1015 merged**(设计)/ **#1305 merged**(a2a3 onboard 数据路径)/ **#1277 merged**(a5 onboard);**#1291 CLOSED-未合并，但其目标已在 main 实现**(`docs/l3-l2-orch-comm.md:48`:稳态数据路径 sim + onboard 均跑在 L3 Host 进程、走同一高层 mapped 路径)。
- **#1383 merged** — TPUT_ASYNC(IBing forward)+ NeighborBarrier(ring)。
- **#1179 / #1109 / #823 merged** — SDMA workspace overlay + async completion demo + port 到 a5(SDMA deferred completion 的落地栈)。
- **#1392 merged `ca000263`**（A5 URMA deferred completion backend）/ **#1303 CLOSED-未合并**（早期 workspace staging）— backend 代码已落地，但当前 capability/workspace gate 仍使它不构成可执行 onboard 能力。
- **#755 CLOSED-未合并（docs-only 历史设计）** — L3/L2 multichannel MPMC 在 main 无落地文件；当前 message queue 仍仅 SPSC。
- **#900 CLOSED(bug issue,标题 "A2A3 spmd_paged_attention_highperf hardware run times out")** — 代码注释(`allreduce_ibing_kernel.cpp:30/96`)把 async SDMA helper 触发的 AICore loader 问题归到 #900;IBing NPU 分支仍走同步 `TPUT<AtomicNone>`(`:252` 是同步回退,非 #900 引用点)。**#900 已关闭,"NPU 集合异步仍被阻断"的定性需按当前代码/loader 现状复核**,不能仅据一条已关 issue 断言。
- **#1379 open** — pipelined orchestration/scheduling:提供 per-token L3↔L2 **背压式异步交互** + trace 证据,是"当前异步模型是什么"的最新演进答案。

### D.2(c) 差距

1. A2/A3 SDMA 是当前唯一有 runnable onboard evidence 的 DMA deferred-completion backend，但它是 **显式 Worker capability**，不是默认启用；NPU 集合 IBing 仍走同步 `TPUT<AtomicNone>`，历史 #900 不足以证明现行 blocker。
2. A5 URMA backend(#1392)已经实现，剩余缺口是 capability/workspace gate 与 onboard acceptance；#755 只是 CLOSED-未合并的 MPMC 纸面设计。
3. **异步不是"只在 sim"，也不是"A2/A3 常开"**：A2/A3 的 `enable_sdma=True` opt-in 路径有 onboard ST。⚠️ **「A5 SDMA/URMA overlays 均默认 OFF」自 #1615（`63f47a11`，2026-08-04）起不再成立** —— a5 SDMA workspace 现为 **default-on**，URMA 变显式备选；a2a3 仍是 opt-in。文档与测试选择必须同时表达 backend 是否编译、Worker 是否 provision、device capability 是否存在这三层，且 **a2a3 与 a5 的默认值现在相反**。
4. 集合(域对称窗口)与 L3-L2(独立 host↔device region)是两套底座并存,尚无统一抽象(是 D.3 的根)。

### D.2(d) 建议 / 落地路径

1. **交付 D.2 时明确四分**：(a) 集合=域内同步 barrier；(b) L3-L2=host↔device counter notify/wait（L3 侧阻塞）；(c) A2/A3 SDMA=provider compiled + Worker opt-in + runnable onboard evidence；(d) **A5 SDMA=default-on 自 #1615（`63f47a11`）** ／ A5 URMA=backend merged + capability/workspace gated 的显式备选。⚠️ **(c) 与 (d) 的默认值相反**，四分表述必须把「默认」而不只是「是否编译」写出来。
2. **NPU 集合异步（IBing）的启用不以 #900 为 gate**：#900 已 CLOSED 且归因可疑（其标题是 `spmd_paged_attention_highperf` 超时，非 AICore loader 专项）。IBing NPU 现走同步 `TPUT<AtomicNone>` 是代码现状，是否仍受 loader 限制须**按当前 loader 实测复核**，不应援引一条已关 issue 当阻断门。
3. **URMA(#1392)是完整 backend 链路，非"补一个表项"，但尚非可用 capability**：保留 AICore submit + AICPU CQ + host overlay 的实现事实，同时把 `PTO_URMA_SUPPORTED` 与 `SIMPLER_ENABLE_PTO_URMA_WORKSPACE` 的 gate 写进验收条件。
4. #755 已 CLOSED-未合并；若重新推进 MPMC，应从当前 SPSC queue 契约重新立项，而不是把 closed PR 当作在途基线。

---

## D.3 "对端无感"通信域(host + 多 device 同域,CPU↔NPU,让 L4 直连 L2)

### D.3(a) 当前代码状态 — 对称同域**未实现**;host↔L2 点对点**部分实现**

- **通信域成员强制是单个 L3 下的 chip 子集**:`workers` 是本 L3 `device_ids` 下标(`worker.py:3880`),域内所有 rank 都是 chip(L2),**host/L3 不是域内 rank**;`CommContext` 全是 chip window。
- **host↔L2 是另一条独立的点对点原语**,不是对称多成员域:`orch.create_worker_chip_region` / `create_worker_chip_queue`(`orchestrator.py:366`)—— L3 host 与**单个** L2 chip 之间的 SPSC region;onboard 用 `L3HostRegionMappingHandle` + `ONBOARD_VMM` + shareable_handle 让 **host 导入 child 拥有的 GM region**(`src/common/platform/include/host/worker_chip_orch_region_access.h`),host 端用 ACL copy 搬字节。**它不是对称域,也不"对端无感知"**。
- **L4 直连 L2 不存在**:L4→L3 是 mailbox/远端帧 dispatch,L3→在飞 L2 才是 orch-comm region,两层不统一,L4 无法直接进 L2。

> **关键纠偏(CANN/HCCL 官方机制):HCCL rank 是调用方分配的、绑定一张 NPU 的逻辑 communicator ID（≠ 物理 device ID；simpler 用 `chip_idx` 作 rank、`device_ids[chip_idx]` 选设备），host/AICPU/AIV 不是对等 rank。** 每个 rank 必须 `aclrtSetDevice` 绑定恰好一张 NPU；CANN **无 CUDA UVM / OpenCL SVM 式统一寻址**（host pinned 内存最接近零拷贝但仍非 device 对等直读 host）。HCOMM 把 **CPU、AICPU_TS、AIV、CCU 建模为不同的通信引擎 / 资源视图（CommEngine），而不是三个可互换的 rank**——不同芯片的 engine/protocol 矩阵不同（官方 `CommEngine` / `HcclChannelAcquire` 能力矩阵；**矩阵随 CANN 版本/产品变化，引用须固定 CANN tag**）。UB / URMA（Ascend 950 / A5 级，CloudMatrix384 xDeepServe / OpenURMA）扩大了 CPU/NPU 对等直访能力，但**仍不改变"rank 绑定单卡"**。**因此 D.3 的正确抽象是"一张 simpler 逻辑域 + capability-specific views"（device collective / host-device channel / AICPU / AIV / control 五视图），而不是把 host、AICPU、AIV 塞进同一个 `windowsIn[]`。API 可隐藏 peer 属于 L3 还是直接寻址的 L2，但不能隐藏"操作是否受当前硬件支持"（须 capability validation 门控）。** 与 `ascend.md` "AICore LSU 不能碰 SPR MMIO" 同源。
>
> **区分 host-CPU 与 AICPU 作 peer(D.3 明列 aicpu/aicore)**:D.3 说的"CPU"要分两种。**host CPU** 越 PCIe,只能做 importer/copier(VMM import + ACL copy),硬件上无法作对称 rank(除非 A5 UB/URMA)。**AICPU** 是片上控制处理器,本就能读写本卡 GM 并经 MMIO 与 AICore 协作(见 `ascend.md`:AICPU 写 DMB、AICore SPR-读;AICore LSU 不能碰 SPR MMIO)——它已经是 L2 片内 orch-comm region 的 host 侧对端(`create_worker_chip_region`,notify/wait counter 就落在 AICPU 可达的 GM/映射区)。因此"让 AICPU 参与通信域"在**本卡范围**已部分成立(L3 Host↔本卡 AICPU 的 region 通道),真正缺的是"**跨卡/跨 host 的 CPU(或 AICPU)作对称集合 rank**",那一步才需要 UB/URMA。梳理时不要把这两种 CPU 混为一谈。

### D.3(b) 相关 Issue/PR

- **#861 CLOSED-未合并** — `HostDeviceMappedRegion` 底层原语的历史设计（64B header + signal slot + data 区；A2/A3 host-register，A5 `-ENOTSUP` 桩）。概念可审计，但不是当前实现砖块。
- **#803 CLOSED-未合并** — L3/L2 host-device multichannel 通信历史原型；不能再称“最实的代码基线”。
- **#755 CLOSED-未合并（docs-only）** — 多 lane SPSC 对外呈现 MPMC 的历史语义草案。
- **已落地版本**:#803/#861 的实现雏形是已合并的 L3-L2 Orchestrator Communication 栈(#1015/#1305/#1277),但那是 **host↔L2 一对一**,不是 D.3 的对称多成员/对端无感域。
- **#1392 merged `ca000263` / #1303 CLOSED-未合并** — A5 URMA deferred-completion backend 已合并，但 capability/workspace 仍 gated；它既不证明 CPU 已是 rank，也不证明 host-peer domain 已可运行。

### D.3(c) 差距

1. **无 "host + 多 device 同一通信域" 路径**:域成员强制是 chip 子集,host 不是 rank,`CommContext` 全是 chip window。
2. **无 "对端无感知"**:host↔L2 只有独立点对点 `create_worker_chip_region`(SPSC),让 L4 直接与 L2 建域当前没有任何抽象。
3. **CommContext 寻址模型硬编码 `windowsIn[pe]+offset`**,若要让 host/CPU 参与同一集合域,需扩展 ABI 与寻址模型 —— 当前无此路径。
4. UB/URMA 的对等语义属 A5/Ascend950;A2/A3 上只能用机内映射窗口近似。

### D.3(d) 建议 / 落地路径

1. **正确方向是"逻辑域 + capability views"（非"host 当 rank"）**(建议写进 `docs/investigations/`,当前无相关条目):
   - **不要走"host 作 HCCL rank"**:HCCL rank 绑定一张卡（≠物理 dev ID，见上纠偏），把 host 塞进 `CommContext.windowsIn[]` 与硬件模型冲突;即便 A5 UB/URMA 也是"扩大对等直访",不是"host 变 rank"。
   - **倾向方案:逻辑域 + capability-specific views + facade**:在 L3 编排层加 facade，让 L4 提交时不区分目标是 L2 还是本地/远端 L3，由 L3 按 capability view 落到 domain(chip↔chip collective) 或 region(host↔chip channel);对不支持的 engine/protocol 组合走 **capability validation 明确失败**，不静默降级。
2. **实现起点应是 main 上 merged 的 L3-L2 Orchestrator Communication 栈**（#1015/#1305/#1277）与 [cpu-npu-shared-memory.md](../cpu-npu-shared-memory.md) 的当前设计；#803/#861/#755 仅用于提取历史需求，不能作为待合入分支。
3. A5 上的 CPU/NPU 对等**直访**仍须先证明 UB/URMA capability 与 workspace 可用；#1392 只交付 backend 代码，且仍非 "host 作 HCCL rank"。不要在 A2/A3 设计依赖 "host 直接解引用 device 指针" 的协议。

---

## D.4 remote L3 通信：mpirun ＋ 共享窗口

**用户选择：mpirun 启动；通信从原生 CANN ACL Runtime 接口构建，由 PTO 使用共享窗口。** Simpler 独立常驻 daemon 是已有可选路径，
不作为主路线或生产会话的必需 owner。核验基线为 `upstream/main@68d137fdfd435d8067f0efd9836cb428da3e700a`。

### D.4(a) 三个平面与现有实现

 | 平面 | 当前机制 | 证据与边界 |
 | ---- | -------- | ---------- |
 | 启动和任务控制 | L4-owned `mpirun`；rank 0 与 L4 共享 mailbox；rank 间 MPI dispatch | `add_mpirun_worker_group`、`docs/mpi-l3-mailbox.md`；无须预起 Simpler daemon，不建立每 rank 的 Simpler command/health TCP socket。MPI 自身可能使用 TCP，不能写成全栈不用 TCP。 |
 | 建域控制 | 全员 PREPARE_EXPORT，MPI allgather 交换 descriptor、IMPORT 状态；L4 协调 COMMIT/ABORT | `mpi_l3_session.py::MpiGlobalDomainExchange.prepare_import` 与 `global_comm_domain.py::GlobalDomainPhase`；full-group 用 MPI exchange，不能将其改写成待实现的 HCCL rootInfo 广播。 |
 | A3 HCCS 数据 | ACL VMM/Fabric V2 导出/导入共享窗口，生成 `CommContext`，kernel 经 TLOAD/MTE 读 peer window | `comm_global_domain_prepare/import`、`aclrtMemExportToShareableHandleV2`、`aclrtMemImportFromShareableHandleV2`；`global_tload_mixed_l3` 与 `global_tload_mpirun_l3` 为真实跨机内存访问用例。 |

main 祖先链包含 #1456 `60ab6ab9b775247e797a26179ce71f0586b5c7c9`、
\#1623 `cc9bfc453c74b1797a753a1d105a3a171b3061d7`、
\#1624 `43094d4415e4e5f789df49dbf6981ec033a0f2ec`。
**证据分级**：当前 `68d137fdf` 的代码调用链已核实；历史 #1624 head
`87489e1aa698ab14424d555aaf43bd2a169b3e60` 的双机用例有原始 CI PASS 日志，见下表。
\#1623 正文的三次 2×2 / `max_diff == 0` 属 PR 自报记录，本次未据此独立确认数值。
当前配置以面向 main 的 PR 为 CI 入口；10 月 12–13 日三个具名 PR 合并测试 SHA 的五项 NETWORK1 用例均通过（见下文）。这证明持续 CI 看护；结果仍归各自实际 checkout SHA。
`vector_add_mixed_l3` 只测任务拓扑的限制仅属于该用例，不能覆盖另两个 peer-TLOAD 用例。

### 调用链与执行证据（2026-10-13）

以下行号固定于 `68d137fdfd435d8067f0efd9836cb428da3e700a`，不能随工作树漂移解释：

 | 检查点 | 具名代码 | 能证明什么 |
 | ------ | -------- | ---------- |
 | 启动 | `worker.py:6070` 的 `_activate_mpirun_worker_groups` 构造 mpirun 命令并 `Popen`，READY 后 `add_mpi_group_mailbox` | 存在不经 Simpler daemon 的启动/派发实现 |
 | 全局建域 | `worker.py:10609`；`mpi_l3_session.py:107` 的 `prepare_import` 执行 PREPARE、descriptor allgather、IMPORT、status allgather；L4 发 COMMIT | 全组描述符交换已接入，不是仅有协议枚举 |
 | L2→ACL | `worker.py:2816` → `cw._impl.comm_global_domain_prepare`；`comm_hccl.cpp:1546/1609` | 真正分配并导入 peer Fabric 窗口；`:330/341` 调 ACL V2 export/import |
 | device context | `comm_global_domain_import` 将各 peer 的导入 VA 写入 `windowsIn/windowsOut`，上传 `CommContext` | kernel 使用的是本地映射出的 peer 地址，不是远端原始 VA |
 | kernel | `global_tload_kernel.cpp:30/71/82`：peer base + offset → `TLOAD(peer_tile, peer_global)` → TADD → TSTORE | 核函数执行 peer-memory 读取和求和；数据不靠 MPI 在 host 端求和 |
 | 验金 | MPI 示例 `main.py:178/191/197`：逐 rank 回读 result，对 `rank_count * index + rank_bias` 比较 | 代码设计上检查所有 rank 的累计结果；但有下面的 NaN 检查缺口 |

路径相对仓库：kernel 位于 `examples/workers/l4/global_tload_mixed_l3/kernels/aiv/`；
MPI 示例位于 `examples/workers/l4/global_tload_mpirun_l3/`；Python 实现位于 `python/simpler/`，
ACL backend 位于 `src/a2a3/platform/onboard/host/`。

**实际执行记录**：GitHub run `31785850986` 的 head SHA 为
`87489e1aa698ab14424d555aaf43bd2a169b3e60`；
[job 94722129298 原始日志](https://github.com/hw-native-sys/simpler/actions/runs/31785850986/job/94722129298)
在 2026-08-14 09:00:10 UTC 明确记录 `test_global_tload_mpirun_l3` 的
`PASS 15.1s, devices=[12, 13]` 与 `1 passed in 14.77s`。
这是用例实际执行证据，不是读取 job 绿色状态或 README expected output。
该用例配置为 a2a3/TMR、两机各两卡。它证明历史提交的测试程序通过，不能证明所有输出均为有限数，
也不能替代当前 main：自 #1624 merge 至当前 SHA，worker、MPI session、ACL backend 与 kernel 均有后续修改。
本机是否配置双机环境不决定 CI 看护是否存在；当前 main SHA 的 checks 也不是查 PR CI 的充分入口。

### 持续 CI 看护（2026-10-13）

配置核验于 `68d137fdfd435d8067f0efd9836cb428da3e700a`：`.github/workflows/ci.yml`
对 `pull_request.branches: [main]` 触发；`st-network1-onboard-a2a3` 需 pre-commit 成功、
`a2a3_changed && st_affected`。`_detect-changes.yml` 允许文档、A5 专属或纯 UT 变更不运行该 job。
`_st-network1.yml` 在配对机器上构建同一 checkout，执行
`examples tests/st --level 4 --platform a2a3 --require-pto-isa`。
这是有条件的持续 PR CI，不表示每个 main 提交都单独执行或每条 PR 都跑双机。

 | NETWORK1 用例（均为 a2a3 / TMR） | 看护范围 | 归属 |
 | -------------------------------- | -------- | ---- |
 | `test_global_tload_mpirun_l3` | 两机各两卡，mpirun/mailbox、MPI descriptor exchange、全局共享窗口与 peer-TLOAD、结果回读 | DS-6 启动；DS-5 数据面 |
 | `test_global_tload_mixed_l3` | 本地 fork L3 + 远端 daemon L3，两机各一卡的共享窗口与 peer-TLOAD | DS-5 |
 | `test_compute_then_tload_mixed_l3` | 同一 Worker/domain 多轮先计算、后 peer-TLOAD；分别检查计算与通信结果 | DS-5 |
 | `test_vector_add_mixed_l3` | 混合 L3 的任务派发和计算正确性；不能单独证明 peer-memory 通信 | DS-6 |
 | `test_global_tload_mixed_l3_network1` | 重用 mixed TLOAD 的 tests/st 收集哨兵，确认该目录也受 NETWORK1 选择 | DS-5 验收入口，不是第五种 transport |

**逐项日志样本**（PR 的 baseRefName 已核为 main；SHA 取 checkout 后 `git log -1 --format=%H`，不是只抄 API headSha）：

 | UTC 日期 / PR | 实际测试 SHA | 双机 job 的逐项结果 |
 | ------------- | ------------ | ------------------- |
 | 2026-10-12 / #2555 | `cd0287afe14d7e0d30b8e14ba974da7767f2d94c` | [job 114844848869](https://github.com/hw-native-sys/simpler/actions/runs/38259084452/job/114844848869)：五项均 PASS 且各有 `1 passed`；MPI 15.96s |
 | 2026-10-13 / #2562 | `85166d6c2df4d928db76e2f81ff733bc9e7029c9` | [job 115180581367](https://github.com/hw-native-sys/simpler/actions/runs/38362358570/job/115180581367)：五项均 PASS 且各有 `1 passed`；MPI 15.93s |
 | 2026-10-13 / #2543 | `1b77ad2ae2a37ed9805846c4e47f25135f01d72b` | [job 115197486707](https://github.com/hw-native-sys/simpler/actions/runs/38365437391/job/115197486707)：五项均 PASS 且各有 `1 passed`；MPI 16.00s |
 | 2026-10-13 / #2563 的该次提交 | `9ea0f50851d640e1691f14f4927bb3d2f782af60` | [job 115190933107](https://github.com/hw-native-sys/simpler/actions/runs/38365414293/job/115190933107)：五项 FAIL；mixed 路径报 `_submit_chip_run_materialized(): incompatible function arguments`；这是该次运行的提交接口错误，不据此断言主线 ACL backend 回归 |

\#2563 的失败对应 head `d095e1be74103a62b011977d0219d0ee9fa5d49f`；查询时其 head 已更新为
`431de6742af870aa0b97534b560d5cd9ccaef283`，旧 job 不充当新 head 的评审结论。
以上是近期连续看护的正反样本，不是全历史稳定率统计。

**看护边界**：MPI wrapper 缺 mpirun、mpi4py 或机器参数会 skip，必须读逐项结果。
共用 suite 的 `network1-run-pytest` action 会为 mixed 用例启动 peer daemon；
因此 MPI 用例通过证明 MPI 启动路径执行，不单独证明“整机没有 Simpler daemon 时也通过”。
本表的阳性是 A3/TMR 的具体拓扑，不是 RoCE、A5、任意异构组合或 session/lease 故障注入的验收证据；
这些目标仍按 DS-1/DS-2/DS-4/DS-5/DS-9/DS-10 各自退出条件判定。

**验金缺口（DS-5）**：当前 MPI 示例 `main.py:191–198` 只检查 `max_diff > 1e-3`。
本轮直接提取源码 `_input_values`/`_expected_values`，以 CPU 探针重放该比较：
正确四 rank 求和得到 `max_diff=0`，仅本 rank 输入得到 `1365` 并被拒绝；
全 NaN 输出得到 `max_diff=nan`，比较为 false，未被拒绝。
这是检查器的可复现盲点，不是设备实际产生 NaN 的证据。
后续验收须显式拒绝非有限数并保存逐 rank 数值；不因旧 PASS 声称该盲点不存在。
同例 batched dispatch 重写相同结果，最终验金也不能独立证明该轮产生了新结果。

**结论口径**：A3 peer-TLOAD 与 MPI 启动/建域已有实现，并有面向 main 的持续双机 PR CI 看护，近期具名合并测试 SHA 实际通过。该基座不能再列为“待首次打通”；NaN 验金和生产故障闭环仍是各自明确的缺口，不能由这些 PASS 推成整个 P2 完成。

### D.4(b) 原生 ACL Runtime、PTO 与 SHMEM 的关系

**当前实现参考 SHMEM 使用的底层 ACL 接口，取得相同类型的共享内存通信效果；
不直接依赖 SHMEM 库，也不规划一项 SHMEM 库接入或迁移。** PTO 的这条通信路径从
CANN ACL Runtime 长出：host 侧分配/导出/导入设备窗口，device 侧使用 PTO TLOAD/MTE 访问 peer。
这不表示 simpler 与 SHMEM 的全部 API、team、内存分配和完成语义完全等价。

```text
启动/控制：L4 → mpirun → L3 MPI group / mailbox
建域控制：PREPARE_EXPORT → MPI descriptor exchange → IMPORT → COMMIT
设备数据：CANN ACL Runtime / Fabric V2 建共享窗口 → PTO TLOAD/MTE → A3 HCCS peer window
```

核验 `68d137fdf`：`comm_global_domain_prepare/import` 直接分配、导出、导入 Fabric 窗口。
`src/`、`python/`、`simpler_setup/`、根 CMake 与 `pyproject.toml` 对
`aclshmem|libshmem|find_package(…shmem|#include…shmem` 搜索为 0；阳性对照为
`aclrtMemExportToShareableHandleV2`、`aclrtMemImportFromShareableHandleV2`、
`HcclCommInitRootInfo` 与 `NAMES hcomm`。该检查针对 simpler 的直接源码/构建依赖，
不将 CANN/PTO 的间接运行库细节泛化成 SHMEM 依赖。

**HCCL/HCOMM 不作为本路径必须新增的建域方式。** simpler 的本地 `comm_init` 另有
HCCL rootInfo/barrier/teardown 调用，当前 A2/A3 CANN 9 构建从 `libhcomm` 提供这些 API；
这是已有其他调用的事实，不等于 A3 Global Fabric 窗口必须先建立跨机 HCCL world，
也不等于 simpler 经 SHMEM 调用 HCCL。独立 collective/其他 transport adapter 仅在有明确需求时讨论，
不得因“与 SHMEM 效果一致”而增加库依赖、SHMEM team/heap 迁移或新的并行路线。

### D.4(c) 后续验收归属

- **DS-6**：mpirun 部署、rank 放置/发现与跨-L3 控制接口；rank 0 必须与父进程同机，
  各机 MPI/mpi4py 与解释器配置须匹配。
- **DS-1 / DS-2 / DS-9**：以 MPI group/runtime 为会话组织，补齐 session epoch、租约、
  device inventory 与已有成员事务的故障闭环；不要求部署常驻 daemon。
- **DS-4 / DS-5 / DS-10**：远端 lifetime/回收、原生通信 backend 与其他互连组合的验收。
  A3 数据路径有源码与持续双机 PR CI 证据，具名测试结果见上表；不能外推其他 backend。

状态只以 [work-items.md](../work-items.md) 为准。依赖是完整退出条件，不阻止 backend 独立推进。

## D.5 L4 为多个 L3 构建通信域

> **该节 poison 缺口的旧判断已失效。** `68d137fdf` 的 MPI 与普通节点 ABORT 失败均调用 `_record_unreclaimable`，后续 admission 拒绝；`test_global_domain_abort_failure_preserves_primary_error_and_poisons_admission` 已在树上。保留历史调查供追溯，不作为新增 poison 实现任务；资源实际回收仍按 DS-4/DS-9 验收。
>
>
> ## ⛔ 下方 (a)–(d) 是 **2026-07-17 的 pre-#1456 快照，且已过期两次**（2026-08-14 复核）
>
> ### 核验于 `upstream/main@3f4686c1`
>
> **① 栈已不在飞** —— banner 原写「#1456 → #1623 → #1624 三层栈**正在实现**」，实际
> **#1456 已 merged `60ab6ab9`、#1623 已 merged `cc9bfc45`**，**#1624 亦已合入 `43094d44`**（本句按 main `68d137fdf` 订正）。
>
> **② banner 之下的正文仍在陈述一个已被实现的桩** —— 原文说
> *「remote session 收到这三个 opcode 直接回错 `unsupported reserved remote domain control`」*。
> **那句错误消息全仓 0 命中**；`remote_l3_session.py:1032-1077` **已实现 `COMM_INIT` /
> `ALLOC_DOMAIN`**，带六条校验（cluster_id · profile · node identity · topology hash ·
> local worker id · global device rank）。
> ⇒ **(a)/(c) 的第 1、2 条差距不成立。**
>
> ⚠️ **读法**：本章 (a)–(d) 只能当**历史快照**；⭐ **但 §D.3 的 HCCL 纠偏与五视图论证仍然有效**
> —— 它点名的词汇（`CommEngine` 全仓 **0 命中**，阳性对照 `CCU`/`AIV` 均有命中）
> **在代码里根本还不存在** ⇒ 那部分是**尚未实现的设计输入**，不是过时描述。
>
> ### 两者性质相反，别一起打折
>
> ### 与本文 §5「四项连带责任」的逐项对照（核实于 #1456 head）
>
> 我在 [cpu-npu-shared-memory/domain-membership.md](../cpu-npu-shared-memory/domain-membership.md) §5
> 列过跨层级建域的**四项连带责任**。逐项对照 `python/simpler/global_comm_domain.py`：
>
> | 责任 | #1456 的覆盖 | 判定 |
> | ---- | ------------ | ---- |
> | **部分失败回滚** | `GlobalDomainPhase = PREPARE_EXPORT → IMPORT → COMMIT / **ABORT**`，即**两阶段提交**形态 | ✅ **强度达标**（2026-08-03 核实 `a05b944f`）—— `worker.py:8759-8773` 是规范的逐参与者 fanout：`prepared_nodes` 在 RPC **之前** append（provisional-before-create）、内层 `try` 保证一个失败仍继续、末尾重抛原始异常。**但审出另一处缺口，见下** |
> | **成员冻结** | 成员以**定长 member table** 随 descriptor 传入，有 `validate_member_table` | ✅ **by construction** —— 成员是参数不是可变状态，天然无法中途变动。无需显式 freeze 机制 |
> | **域 epoch / 代际** | `generation` 字段出现在 `GlobalDomainCommand` / `GlobalDomainReleaseCommand` / `GlobalDomainCopyCommand` **三个命令上** | ✅ **已覆盖**，且是逐命令携带而非仅建域时一次 |
> | **全员 READY barrier** | `barrier` / `READY` 零命中；但 **COMMIT 仅在全员 PREPARE 成功后才发出**，2PC 本身即构成 barrier | ✅ **by 2PC**（#1623 另有 "ready coordination / ready handshake"，属 MPI 拉起层） |
>
> **⇒ 四项责任全部覆盖**（两项完整、一项 by-construction、一项经核实达标）。
>
> ### 🔴 但核实过程中审出一处**未被这四项覆盖**的缺口：§5.9 第 ⑥ 条 poison
>
> §5.9 共六条。#1456 的分配失败路径满足 ①–⑤，**第 ⑥ 条「完整清理不可证时 poison
> ordered admission」没有实现**：`worker.py:8766-8772` 在 ABORT 对某个节点失败时
> **写 stderr 后继续**，而这正是 §5.9 结尾点名禁止的做法 ——
> *"do not reduce an RPC failure to a stderr message … so the worker cannot admit a
> successor under a **false clean-handoff** claim."*
>
> **为什么这次泄漏比普通泄漏更糟**：失败时 `:8785-8787` 从未执行 ⇒ 该 domain
> **既不在 `_live_global_domains`、也不在 `resources.live_global_domains`** ⇒
> run fence 与 `close()` 都够不着它；`requires_ordered_cleanup` 也未置位 ⇒
> **失败的 run 连 cleanup-bearing 都不算，后继立刻按满深度放行**。
> 节点上那块窗口已分配、ABORT 失败、无人可回收、无人知道。
>
> **硬证据**：#1456 在 `worker.py` 加 **+1451 行，新增 poison setter 0 个** ——
> 两版 `_ordered_cleanup_error` 赋值点一一对应（`main:6824/8186/8207/8486/8491`
> ↔ `pr:7486/9596/9617/9907/9912`），全部是既有的。所有新失败路径都无 poison 通路。
>
> **另一处同型**：`_release_all_global_domain_nodes`（`:8460`）连错误都不抛，只写 stderr。
>
> **修法已在 main，只是没被调用**：`_record_unreclaimable(message, cause)` 的 docstring
> 正是此场景 —— *"device state that **no cleanup can reach**: nothing tracks it, so there
> is **no handle for a run's fence to fail on** and no sweep that could retry it."*
>
> ⇒ 这条同时是 **W3 判断五的前置条件**：见
> [`cpu-npu-shared-memory/work-breakdown.md`](../cpu-npu-shared-memory/work-breakdown.md)
> 判断五「账本只统一了一半」—— release 账本已同一，**分配失败账本没有**。

### D.5(a) pre-#1456 历史快照（已撤回；当前契约见 D.4）

- **remote L3 线协议已 RESERVE 域 opcode 但实现是桩**:`ControlName.COMM_INIT=13 / ALLOC_DOMAIN=14 / RELEASE_DOMAIN=15`(`remote_l3_protocol.py:62`),但 remote session 收到这三个 opcode **直接回错** `"unsupported reserved remote domain control"`(`remote_l3_session.py:818`)。即 L4 跨(远端)L3 建域是 "占了 opcode 位、实现是桩"。
- **结构性走不通**:`allocate_domain`/`_ensure_comm_base`/`_dispatch_control_domain` 全走 `_chip_shms`(L3 的 chip mailbox);L4 的 `_chip_shms` 为空、next-level 是 L3 python worker(`worker.py:4170`),所以尽管 guard 只写 `level<3`,`allocate_domain` 在 L4 结构上跑不起来。
- **跨机窗口后端不成立**:VMM shareable-handle 是 host-local(`comm_hccl.cpp:20`),跨 host 需另一套 windows backend。
- **cross-host device_ctx 语义缺口**:`CommContext.windowsIn[]` 是本进程可解引用的 GVA(VMM 导入),跨 host 对端窗口不在本机地址空间,现有 kernel 直接 MTE2 读 `windowsIn[peer]` 的模型无法直接搬到 remote L3。
- 设计层面 implementation-plan **Step 13 "Remote allocate_domain() future work"** 仍是 future。

### D.5(b) 相关 Issue/PR

- **#824 open** — persistent communication domains across `Worker.run`:与 L4/L3 反复建域的开销直接相关(见文末)。
- **#866/#711/#803/#861 均 CLOSED-未合并** —— 只保留为历史设计来源；直接承接者 **#1456（mixed local/remote L4 CommDomains）已 merged `60ab6ab9`（2026-08-11）**；其后继 #1623 / #1624 亦已在 main 祖先链；当前层次见 §D.4。
- **#1392 merged `ca000263`** — A5 URMA deferred-completion backend 代码已合并，但 capability/workspace gated；它**不是** Remote CommDomain / window bootstrap，跨 host window 仍需 endpoint + memory registration。（#1303 CLOSED-未合并）
- **对照 HCCL 官方——管理层级 ≠ HCCL rank 嵌套**:D.5 的正确基线是 **L4 把所有选中的 L2 flatten 成一张全局 device-rank 空间、建立单一 global communicator**（用 ranktable `HcclCommInitClusterInfo`，或 L4 选 root L2 + 广播 opaque rootInfo `HcclCommInitRootInfo`）。**L4→L3→L2 是控制面管理层级，不对应 HCCL 的 rank 嵌套**；**不要"每层独立建 world"**（那会把 collective 域切碎）。子集（TP/DP/EP）如目标 CANN 支持可用 `HcclCreateSubCommConfig` 从 global 派生（该 API **只支持从 global 域拆分、不支持嵌套拆分**），否则由 simpler 自建 versioned subdomain。

### D.5(c) 历史差距（不可作为当前实施清单；归属见 DS-7/DS-9/DS-10）

1. **L4 为多个 L3 建跨 host 通信域未实现**:线协议 RESERVE 但 session 拒绝;VMM 是 host-local,跨 host 需 RDMA/URMA。
2. **结构性限制**:allocate_domain 分发链全走 `_chip_shms`,L4 无 chip mailbox,路径跑不起来。
3. **cross-host device_ctx 语义缺口**(见上)。
4. **[P0] 缺真正的分布式初始化协议**:当前既无 membership freeze / device lease / 重复 device 检测，也无 EndpointId→global rank 映射、domain epoch/generation、PREPARE/COMMIT/ABORT 与 partial-init rollback、全 rank READY barrier —— 这些是 L4 建 global communicator 的必需件。
5. **[P1] ABI 硬限 64 rank**:`CommContext.windowsIn[]`/`windowsOut[]` 固定 `COMM_MAX_RANK_NUM=64`（`comm_context.h:42,51-52`）—— **每台 16 卡时最多容纳 4 台满配 L3**。超过需 dynamic / hierarchical device context 并同步评审 PTO-ISA ABI。此前文档只记数组大小、未列为 D.5 前置限制。

### D.5(d) 已过时的建议（当前路线见 D.4）

1. **把 remote_l3 线协议中 RESERVE 的 COMM_INIT/ALLOC_DOMAIN/RELEASE_DOMAIN 从桩实现出来**（定位 `ControlName` 与 remote session control handler），并把 `allocate_domain` 的分发从 "`_chip_shms` only" **抽象成 L4 端点(local L3 endpoint | remote L3 endpoint)，再由每个 L3 fan-out 到其 L2**（**不是** "chip mailbox | remote conn"——那是 L3→L2 层，属抽象错层）。实现应与 ✅ 已 merged 的 #1456（`60ab6ab9`；2026-08-11 更正，原写 open） 或其后继合流，不能依赖已关闭的 #866/#711。
2. **已撤回的旧建议（当前契约见 §D.4）**：本行把所有 VMM shareable-handle 当作 host-local、要求跨 host 必换 RDMA/URMA，是对 A3 Fabric V2 的错误泛化。下述 flat HCCL communicator 只适用于选择 collective adapter 的场景——**注意 HCCL collective 不能透明替代 `windowsIn[]` 的 peer-addressed load/store/notify 语义，不是 window backend**;**flatten 全部选中 L2 成 global device rank 建单一 communicator**（非每层建 world），子集用 `HcclCreateSubCommConfig`（仅从 global 拆分）派生。
3. **补 L4 DomainCoordinator（事务化 membership + global fanout）**:EndpointId→global rank 映射、domain epoch/generation、device lease + 重复 device 检测、PREPARE/COMMIT/ABORT + partial-init rollback、全 rank READY barrier;membership 变化建新 epoch，不对现有 domain 隐式 hot-add。**关键：`HcclCreateSubCommConfig` 是 collective —— global communicator 的所有 rank 都必须调用，非成员也须以规定 null 参数参与；只向子集成员派发会挂住。故 DomainCoordinator 必须含一个 global fanout 状态机（每次建子域都向全 global rank 广播调用）。**
4. **对齐时说明这是 reserved-not-implemented**,与 #824、`comm-domain.md` 的 Option A 动态域一起规划,避免重复造轮子。

---
