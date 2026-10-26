# 实施分解：从当前代码到新设计（计划）

> **发布快照：2026-10-26。** 设计、实现状态与历史调查须按正文各自基线阅读；不代表当前 main 全量验收。参见[阅读入口与已知更正](../README.md)。
>
> **状态**：⬜ **计划（plan-only）**。本文只做**工作分解与排序**，
> **不含实现者 prompt**——那是 gate 打开后再按 W 项单独拆。
> **上游**：[cpu-npu-shared-memory.md](../cpu-npu-shared-memory.md)（设计）
> ／ [domain-membership.md](domain-membership.md)（成员契约）
> ／ [hardware-constraints.md](hardware-constraints.md)（约束）。
> **基线**：`upstream/main@e711d482`（2026-08-04 复核）。**实施前重新 fetch**。
> ⚠️ **该基线已漂移**（当前 main 是 `9a03d92c`，2026-08-07）。三处已知增量已在本文就地折回：
> W2 的「规模」「依赖」行 · §3 问题 2（关闭）· §4（**整节前提被推翻：它们不是「交叠」，是同一条 track**）。
> ⛔ **本文不再是排期真源** —— 2026-08-07 用户裁决：本 track 与 worker 内存模型 P1 track
> 是**同一个 capability 判定函数**的两半（[`track-boundaries.md`](../track-boundaries.md) §2 —— ⚠️ **2026-08-14：该结论仍成立但理由已换**，原依据的「五个求值时刻」表已被证伪），
> 合并后按「物理输入 / 判定函数 / wire 载体」分组，见 [`../README.md`](../README.md) §1.5。
> **本文 §2 只保留依赖关系**（依赖是硬的，排期是判断），§2 的 SM0–SM4 编号已作废。
>
> ⛔ **2026-09-07 第二次降级：W0–W8 不再是一条链，只是九份逐步设计说明。**
> 上一次降级拿走了「排期真源」，这一次拿走的是**链本身** —— 三条实施线重组后，
> W 各步被拆给了不同的线：`W0`→`RA-2` · `W1lite`/`W1`→`RA-3` · `W2`→`RA-8` ·
> `W3`→`RA-12` · `W4①`→`RA-11`、`W4②`→`RA-4` · `W5`→`DS-9` · `W6`→`RA-11` ·
> `W7`→`EX-2` · `W8`→`RA-14`（映射真源 [`../work-items.md`](../work-items.md)）。
> ⇒ **W0→W8 的顺序与各步「退出条件」不再成立**，因为它们已不在同一条线上；
> **依赖改由工作项之间表达**。本文各 W 小节继续作为**那一步的设计说明**有效 ——
> 「做什么 / 现状锚点 / 规模 / 风险」这四行是本文独有的，别处没有。
> ⚠️ **只有一处例外仍是本文的硬约束**：`W4②` 的 a2a3 onboard 验证是必要条件
> （PR #540 有真实事故），这条随 `RA-4` 一起走。
>
> ⭐ **为什么必须明写而不是让它自然过时**：一份文档同时定义顺序、而排期按别的顺序走，
> 正是这次重组要消灭的形状。留着「W0→W8」而不说它已作废，下一个人会照它排期。
> **标注**：✅ 已核实｜🟡 待验证｜⬜ 全新。
>
> **锚定约定**：仓库内引用一律用 `file::symbol`、异常消息原文或
> 合并 commit，不用行号。`worker.py` 与 C++ 平台文件都在持续变动；符号与 commit
> 能表达“哪个契约”，而非偶然的文本位置。

## 0. 关键发现：三个抽象点**已经就位**

本设计**不是从零造**。核实后，最关键的三处扩展点仓库里已有：

 | 抽象点 | 现状（✅ 已核实） | 含义 |
 | ------ | ----------------- | ---- |
 | **host-map 的 vtable 位** | `src/common/platform/include/common/host_api.h::HostApi::register_device_memory_to_host` / `unregister_device_memory_from_host`。注释**已声明**"返回 VA 可能 ≠ `dev_ptr`，调用方必须用返回值" | **契约本来就是 VA-正确的**；只有 `host_build_graph` 消费端额外断言了 identity |
 | **a5 的接入点** | `src/common/platform/onboard/host/device_runner_base.h::DeviceRunnerBase::register_device_memory_to_host` 是 **virtual + base default 返回 nullptr**；a2a3 override，a5 走 base default | **a5 启用 = override 两个虚函数**，不是造新机制 |
 | **map-or-copy 的 profile 抽象** | `src/common/platform/include/host/worker_chip_orch_region_access.h::WorkerChipRegionAccessProfile`；binding 层的 `_l3_host_mapped_region_*` / `_l3_host_mapped_payload_*` / `_l3_host_mapped_counter_*` 已按 profile 分派 | **"不同后端藏在同一 API 后"的模式已跑通**，新增一个 profile 即可 |

> ➡️ **结论**：本设计 = **① 打开 a5 ② 加一个 access profile ③ 把成员模型从"层级"换成"端点"**。

## 1. 工作分解（W0–W8）

### W0 🚧 前置：a5 host-map 能力核实 —— **阻塞 W1 的 ld/st 分支**（W1lite / W2 / W3 不受阻，见下）

 | 项 | 内容 |
 | -- | ---- |
 | **做什么** | 探测 a5 驱动：`ascend_hal` 是否导出 `halHostRegister`、`DEV_SVM_MAP_HOST` flags 语义是否同义、**是否返回 identity VA**、最低驱动版本与探测方式、**BAR/aperture 大小与数量上限** |
 | **现状锚点** | a2a3 取法可直接照抄：✅ `src/a2a3/platform/onboard/host/device_runner.cpp::load_hal_if_needed` / `get_halHostRegister` / `DeviceRunner::register_device_memory_to_host`（`dlopen("ascend_hal")` + `dlsym`），签名 `int (*)(void*, size_t, unsigned, int, void**)` |
 | **规模** | **XS**（探测小程序 + onboard 跑一次） |
 | **依赖** | 无 |
 | **产出/影响** | 决定 W1 的 a5 分支是"翻门控"还是"移植实现"；**BAR 上限直接进 host-mappable Buffer 容量预算**（约束 §4） |
 | **状态（2026-08-07）** | 🔴 **仍不可用**（外部驱动假设未在本仓验证，见 [hardware-constraints.md](hardware-constraints.md) §2 状态表），且**无人认领**。⇒ **不再等它** —— 见下方 W1lite，用一层封装接口把 W0 从关键路径上摘掉 |

### W1lite ⬜ **新增，解耦 W0**：register/access 两接口，宏选 ld/st 或 `rtMemcpy` —— **不阻塞下游**

> **动机（2026-08-07 用户裁决）**：W0 的驱动侧能力短期不可用，但 a5 host-map **一旦打开就是纯性能优化**——
> 消费端（`host_tensor_access.cpp` 的 `read`/`write`）已经是"能映射就直读、不能就 mirror + `copy_to_device`"
> 的双路径，唯一缺的是把这条选择做成**可切换的接口**而不是"等 W0 通过才能开始写消费端代码"。
> ⇒ 把 W0 从「阻塞 W1」降级为「W1lite 内部一根宏开关的翻转」，**该翻转本身不需要新代码**——
> 消费端从第一天就调用同一对接口，只是接口内部今天走 `rtMemcpy`，明天走 `ld/st`。

 | 项 | 内容 |
 | -- | ---- |
 | **做什么** | 定义与 `HostApi::register_device_memory_to_host` / `unregister_device_memory_from_host` 同层的两个接口，**按 platform 而非按能力探测结果分派**：① **注册接口**（对应现有 `register_device_memory_to_host`/`unregister_device_memory_from_host` 虚函数对，签名不变）② **访问接口**（对应 `host_tensor_access.cpp` 里的 `read`/`write`，即 `HostTensorRegion` 的取数路径）。两接口内部各用一个宏在两种实现间二选一：；&nbsp;&nbsp;· **注册**：`ld/st` 分支 = 真映射（`halHostRegister` 返回的 host VA，直接 CPU deref，即 a2a3 现状）；`rtMemcpy` 分支 = **不注册**（`register_device_memory_to_host` 直接返回 `nullptr`，退回 `host_tensor_access.cpp` 已有的 mirror-copy 路径 —— 这条路径今天就存在，等价于"假装没有 host-map"）；&nbsp;&nbsp;· **访问**：`ld/st` 分支 = 对映射出的 host VA 直接 `memcpy`（等价当前 `HostTensorAccessor::read`/`write` 的非-mirror 分支）；`rtMemcpy` 分支 = 对 staging 缓冲 `memcpy` + `copy_to_device`/`copy_from_device`（等价当前的 mirror 分支）；&nbsp;&nbsp;⇒ **两个分支在 `host_tensor_access.cpp` 里都已经存在**，W1lite 只是把"选哪个"从**运行时探测结果**（当前：`register_device_memory_to_host` 返回值是否为空）**收窄为一个编译期/平台期宏**，从而不必等 W0 出结论也能把 a5 接上 |
 | **平台选择** | **A3 = `ld/st`**（现状不变，a2a3 已有 `halHostRegister` override，见 W1）；**A5 = `rtMemcpy`**（W0 结论落地前的默认，且与 [hardware-constraints.md](hardware-constraints.md) §2 表中列出的现有 a5 绕行——PMU / L2 swimlane / scope_stats / args-dump 收集器的 `rtMemcpy DEVICE_TO_HOST` 绕行——**是同一条路径，不是新绕行**）。宏建议挂在现有 `WorkerChipRegionAccessProfile`（`ONBOARD_VMM` / `SIM_POSIX_SHM`）同一层，而非新开 `#if a5 / #if a2a3`——后者正是 W1 要禁止的硬编码模式（见 W1「禁用 `#if a5 / #if a2a3` 硬编码」条） |
 | **W0 可用后怎么切** | **只改宏映射，不改调用方**：把 A5 的宏分支从 `rtMemcpy` 改成 `ld/st`（即真正调用 `halHostRegister` 并按返回 VA 直读），`host_tensor_access.cpp` 与更上层的 `HostTensorAccessor::add`/`read`/`write` 调用点**零改动**——它们从写下的第一天起就只调用注册/访问这两个接口，从不关心宏另一侧是什么 |
 | **现状锚点** | ✅ 两条分支的实现**已经都在** `src/a5/runtime/host_build_graph/host/host_tensor_access.cpp`（`add` 的 `host_view != nullptr` 分支＝ld/st，`fallback_host_view` 分支＝mirror）；✅ `unregister_device_memory_from_host` 的 base default 已经是空实现（`device_runner_base.h:163`），即天然的"rtMemcpy 分支＝不注册"。W1lite **不是新写访问逻辑**，是**把现有的运行时二选一，改造成 platform 定的宏二选一** |
 | **规模** | **XS–S**：`host_api.h` 的 vtable 层加一层按-platform 选择的宏封装；a5 `DeviceRunner` 用宏把 `register_device_memory_to_host` 接到"直接返回 nullptr"（rtMemcpy 分支）而不是留空 override（base default 已是空，行为不变，只是**显式**写出宏分支，避免以后有人误读"a5 无 override＝忘了写"） |
 | **依赖** | 无（**不依赖 W0**，这正是它存在的理由） |
 | **产出/影响** | 把 W0 从「阻塞 W1」降为「W1lite 内部一次宏翻转」；a5 下游（W4 同步原语归一等）可以立刻针对**统一接口**开发，不必等驱动结论 |

### W1 平台层：打开 a5 host-map + 新增 `HOST_MAPPED` profile —— **扩展已有**

 | 项 | 内容 |
 | -- | ---- |
 | **做什么** | ① a5 `DeviceRunner` **override** 那两个虚函数（照抄 a2a3 的 `dlopen`/`dlsym` 实现），**即 W1lite 宏的 ld/st 分支的真实现**；② `WorkerChipRegionAccessProfile` 新增 **`ONBOARD_HOST_MAPPED`**：host 侧直接 CPU deref，取代 ACL-copy 路径；③ **运行时能力探测**（`dlsym` 成功 + 一次小规模试注册）选 profile，**禁用 `#if a5 / #if a2a3` 硬编码**；④ **device-id namespace gate**：所有直接 `halHostRegister` 调用均先将 ACL/rt logical id 经 `pto::acl_to_hal_device_id()` 转成 driver-visible id；`aclrt*` / `rt*` 入口仍传 logical id，不转换 |
 | **现状锚点** | ✅ `src/a5/platform/onboard/host/device_runner.{h,cpp}` 的 `DeviceRunner::init_chip_swimlane` / `init_args_dump` / `init_pmu` / `init_dep_gen` / `init_scope_stats` 仍显式传 `register_cb=nullptr`；✅ `l3_l2_orch_region_access.h::WorkerChipRegionAccessProfile`；✅ binding 的 `_l3_host_mapped_*` 分派入口；✅ #1600（`9a30e6dd`）增加 `acl_hal_device.h::acl_to_hal_device_id`；⛔ a2a3 的 `DeviceRunner::register_device_memory_to_host` 与五个 DFX `register_cb` 当前仍把 logical `device_id` 直传 `halHostRegister` |
 | **规模** | **S–M**。虚函数 override 是 XS；新 profile 要贯穿 binding 五个入口 |
 | **依赖** | W0 **+ W1lite**（W1 = 把 W1lite 的宏分支从 `rtMemcpy` 翻成 `ld/st`，不是另起一套） |
 | **风险** | 🟡 若 W0 发现**非 identity VA**，则 `src/a2a3/runtime/host_build_graph/host/runtime_maker.cpp::bind_callable_to_runtime_impl` 的 identity 断言会挡路，需先让消费端真正按返 VA 走 → 规模由 S 升 M |
 | **附带** | ⚠️ 删除以下符号附近“a5 无 `halHostRegister`”的过时断言：`KernelArgs::scope_stats_data_base`、a5 `DeviceRunner::init_scope_stats` / `init_dep_gen`、`pmu_profiling.h` 的 streaming-buffer design 注释、`DeviceRunnerBase::register_device_memory_to_host`、`ProfilerBase::release_one_buffer` / `alloc_paired_buffer`。**收集器的 host-影子绕行简化单独立项，勿夹带** |

> **当前 correctness 缺口不受 W0 阻塞**：④ 中 a2a3 既有路径的 logical→driver
> 转换应独立先修；W0 只决定 a5 host-map 能否开启。测试必须使用
> `ASCEND_RT_VISIBLE_DEVICES` 下 **logical id ≠ driver-visible id** 的配置，否则 identity 映射会
> 让漏接 gate 的实现也绿灯。

### W2 端点与成员模型 —— ⬜ **全新，设计落地的主体**

 | 项 | 内容 |
 | -- | ---- |
 | **做什么** | ① **端点标识 `EndpointId = session_id + worker_path + deployment`**——路径 `L4/L3[2]/L2[5]`，部署 `HOST_CPU / DEVICE_AICORE / DEVICE_AICPU`（**术语依 [../feature_roadmap.md](../feature_roadmap.md) 规范**）；② **成员选择器**（子树展开）+ 显式端点列表两种表达；③ 对每个 `(member, CanonicalBufferIdentity)` 计算完整 **potential capability**：`deployment × interconnect × backing/backend × runtime support`，互连矩阵只是第一道必要门槛；④ 把 **actual capability** 收窄为 `potential ∩ authorization ∩ live Attachment/materialization`；W2 冻结计划/校验模型，W5 负责事务性建立 live Attachment；⑤ `TransportPlan` 按 operation 选择规范 adapter：HCCL collective、direct-map/device-peer、owner-delegated copy 或 explicit transfer；adapter 再按 backing/backend 物化为 VMM、fork-shm 等实现。不支持或未 attach 均**显式失败**，禁止仅从 MemberSet 推导后端 |
 | **现状锚点** | ⬜ **代码里没有任何端点路径概念**——端点路径只存在于 ABI 设计（[../worker-memory-model/p1b-corrected-design.md](../worker-memory-model/p1b-corrected-design.md)），且那里它已收窄为**进程内诊断用 `path_id`**，grep 无实现。现有成员表达是**裸 chip 索引**：✅ `Worker._allocate_domain` 校验 `worker_id {w} outside [0, len(device_ids))` |
 | **规模** | ⚠️ **M–L，但「不碰 C++/wire」已于 2026-08-07 作废** —— 见 [w2-w5-inversion.md](w2-w5-inversion.md) §1.1。原文写「纯 Python 层的标识 + 校验」；实际上 **W5 的机制先于 W2 的词汇落地了**（#1456），成员**已经在跨节点控制面 wire 上**：`global_comm_domain::encode_domain_command` 打包 `len(members)` ＋ 逐个 `_put_member`，`validate_member_table` 直接以 `(node_worker_id, local_worker_id)` 判重。编码器确实是 Python，但它是**跨进程、带版本号的序列化格式**，不再是进程内标识。⏰ **且 #1623 / #1624 正在同一协议上继续叠加 ⇒ 现在改是加字段，等它们落地后是换协议。** |
 | **依赖** | 无（**可与 W1 并行**）。⚠️ **但 2026-08-07 发现一条跨里程碑的_反向_供货关系**：**PR #1696 造的端点注册表就是 P1-B 卡着的那张 endpoint 矩阵** —— 见 [../w2-p1b-endpoint-convergence.md](../w2-p1b-endpoint-convergence.md)。W2 不依赖 P1-B，**是 P1-B 的 `ImportContext` 依赖 W2**。⇒ 「谁拥有 endpoint 矩阵」必须先拍板（决定 ①），否则仓库会长出两张矩阵 |
 | **⚠️ 关键** | **必须先冻结路径的字符串/结构表示**——但要注意两处**用法已分岔**：；· **域模型**用**完整 path**（是 `EndpointId` 的组成部分，参与成员判定，必须跨进程可解析）；· **ABI** 只存 `uint32 owner_worker_path_id`，**纯诊断**，侧表在 owner 进程内，消费者解析不出属正常（见 [../worker-memory-model/p1b-corrected-design.md](../worker-memory-model/p1b-corrected-design.md)）；⇒ **要同源的是「路径怎么写」这一个表示，不是它的存储与语义**。ABI 不再把 path 放进 identity，正是为了消灭定长 `char[64]` 那类越界；域模型则不能丢 path。**两者共用格式、各存各的** |
 | **契约来源** | [domain-membership.md](domain-membership.md) §3（域与完整 capability 公式）／§4.2（potential vs actual）／§6（成员表达）／§7（Attachment/Transport 选择） |

### W3 persistent Buffer/Attachment 生命周期 + 跨 run 存活 —— **改已有行为（风险点）**

 | 项 | 内容 |
 | -- | ---- |
 | **做什么** | ① Buffer Attachment 的 map/unmap + **refcount**（多进程 attach 同一 Buffer）；② persistent Buffer/domain resource **跨 `run()` 存活**；③ **容量核算与上报**（HBM 预算可观测）—— **③ 已被 #1592 部分交付，但缺口正好落在本项上，见下** |
 | **现状锚点**；（**按符号锚定**） | ✅ `Worker.submit` 为每个 `RunHandle` 建 `_RunResources`；`RunHandle.wait` 跨过 native fence 后调 `Worker._finalize_run_handle`，由 `_RunFinalizationCursor` 顺序驱动 `_release_all_live_domains(resources)` 等 one-shot 步骤。`Worker._teardown_ready_tree` 的 `_step(...)` 只属于 terminal close 扫尾，**不是 run-fence 路径** |
 | **规模** | **M → M-L**（见下方 #1466 影响） |
 | **依赖** | 无（但语义上是 issue **#824** 的实质内容） |
 | **⚠️ 交叠** | 与 **P0.2 follow-up 簇C（retryable journal）**交叠——都是"资源只在 native free 成功后才丢账本"。**应对齐，别各做一套**（见 ../P0.2.md（本地参考：`../P0.2/done-followup.md`） §簇C） |

#### ⚠️ #1466 改变了这一项的地基（`da75d350`，2026-07-27 合入）

**「Refactor: isolate resources per worker run」** 把 CommDomain / L3-L2 region /
remote slot refs / host-buffer 注册**从 Worker 级注册表移入 per-run 的 `_RunResources`**，
绑定到拥有它的 `RunHandle`。原因是**正确性修复**：此前一个 run 的 fence 会清扫
Worker 级注册表，**可能释放另一个 run 仍持有的资源**。

##### 对 W3 的三点影响

 | 影响 | 说明 |
 | ---- | ---- |
 | **① 方向相反了** | commit message 明写：*"A domain's lifetime **ends at the fence of the run that allocated it**, so **allocate_domain outside graph construction has no owner to charge and is rejected**."* ⇒ main **刚刚把 run-所有权收紧**，跨 run 存活比之前**更难**，不再是"别在 fence 清掉"就行 |
 | **② 但地基更好了** | 所有权从**隐式**（Worker 级注册表）变成**显式**（charge 到 RunHandle）。要加"跨 run 持久"类别，现在是**新增一个显式的 lifetime 归属**，而不是在隐式清扫里开洞 —— 这更干净 |
 | **③ 规模上调** | 需要把 **RunHandle / Worker-tree / persistent** 三个 ownership domain 的边界写成可执行契约，并定义 `_submit_mu` 单写者约束下 persistent owner 如何创建、被 run retain、显式释放与 close 兜底。**M → M-L** |

#### #1541 把 run finalization 的 one-shot 边界变成了显式对象

\#1541（`9a2a5f71`）之后，当前 run-owned 路径是：

1. `Worker.submit` 创建 `_RunResources` 并绑定到该 `RunHandle`；
   `_building_run_resources` 只在 graph construction 期间指向它。
2. `RunHandle.wait` 跨过 native run fence 后，由唯一 elected owner 调用
   `Worker._finalize_run_handle`。
3. `_finalize_run_handle` 创建 `_RunFinalizationCursor`，它按名称驱动每个
   one-shot cleanup edge，只在一个 edge 已返回后才推进 `next_step`。
4. cleanup/poison 结果先发布，再在同一 lifecycle lock 下把 handle 从
   `_accepted_run_handles` 移除，因此后继 run 不会看到“前任已退出但 cleanup
   结果尚未发布”的窗口。

`_RunFinalizationCursor` **不是可重放 journal**。若 async 异常落在某个
operation 返回之前，native side effect 是否已落地就有歧义；cursor 标记
`incomplete`，handle 转入 `_abandoned_run_handles`，worker 停止新准入，只有
whole-tree teardown 才是安全的最终回收边界。W3 不得绕开或重放这套边界。

#### W3 的资源生命周期：**只有两种**（2026-08-03 定稿）

> **决策（用户）**：本设计中任何资源**只有两种生命周期** —— **per-orch** 与 **per-worker**。
>
> ##### 提供两种还是只提供其一，按需求定
>
> 这比本文早前写的"三个 ownership domain"更准。更正如下：
>
> | 早前写的 | 更正 |
> | -------- | ---- |
> | RunHandle | ✅ = **per-orch** |
> | **Worker-tree** | ❌ **不是资源生命周期** —— 它是执行树/容器本身（native worker、children、mailbox）。资源挂在它里面，但"worker 树的存活"不是一种资源归属 |
> | Persistent | ✅ **就是 per-worker**，不是第三类。"persistent" 只是 per-worker 的别名，**本文此后统一写 per-worker** |
>
> ⇒ **不存在"第三种 lifetime"这个设计任务。** 早前几轮把它当成待发明的新类别，是把
> "Worker-tree 容器"误算成了一种资源归属所致。

#### 两种生命周期的当前状态

 | 生命周期 | 当前 owner / 终止边界 | W3 要求 |
 | -------- | --------------------- | ------- |
 | **per-orch** | ✅ `_RunResources`；该 run 的 native fence 后由 `_finalize_run_handle` / `_RunFinalizationCursor` 精确一次清理 | 保持为默认；**一个 run 的 fence 不得释放其他 run 或 per-worker 的资源**（#1466 修的正是这条） |
 | **per-worker** | 🟡 **已有先例，但只覆盖无 device 流量的资源**：；· `Worker._buffer_handles`（POSIX shm，`close()` 回收）；· Graph Execution 的 recording state / definition cache / host submission pool —— 其文档明写 *"owned **per worker** (per runtime instance), not process-global"* | 扩到**带 device 控制流量**的资源（CommDomain / L3-L2 region）。run 使用时持 **retain**，run fence 只释放 retain；显式 release 须等所有已接受 run 的 retain 归零；`close()` 顺序 = **先排空 per-orch → 再回收 per-worker → 最后拆 Worker 树** |

> **修正 W3 的做法**：不是"去掉 fence 的清理"，而是
> **把 per-worker 生命周期从"无 device 流量的资源"扩到"带 device 流量的资源"**。

##### 创建入口：问题不是"入口在哪"，是"创建时发不发 device 控制流量"

我早前把它写成 "A1 复用建图期 vs A2 新增入口" 的二选一。**在两生命周期模型下这个框架是错的**
—— per-worker 资源**本来就该由 worker 级 API 创建**，而且**先例已经存在**
（`Worker.create_buffer` 就在 run 之外创建 per-worker 资源，无需建图期）。

真正的分界是另一条：

 | per-worker 资源 | 创建时发 device 控制流量？ | 入口 | 阻塞 |
 | --------------- | -------------------------- | ---- | ---- |
 | host shm Buffer | ❌ | ✅ **Worker 级 API，今天就能做**（`create_buffer` 即是） | 无 |
 | **CommDomain / L3-L2 region** | ✅ 多 chip alloc + export/import | Worker 级 API，但**必须进 run/admission 排序域** | 🔴 **控制操作准入** —— 异步流水 §5.8 要求本地/远端控制与 run 共享同一排序域，而这正是 **#1541 尚未闭合的 merge gate 之一** |

##### ✅ 该阻塞**已解除**（2026-08-03 核实于 `upstream/main`）

我上一轮把它标成 🔴，**是错的** —— 那是基于"控制准入尚未合并"的判断，而
**#1541（B2b）已经把它交付了**。计划的验收清单原文：
*"B2b **linearizes every public local/remote control with submit admission**"*。

main 上的机制是 **`Worker._control_reservation(api)`**，其 docstring 就是 W3 要的东西：

> *"**Reserve this worker for one command that belongs to no run.**
> A public `Worker.malloc/free/copy_*/remote_*` outside a graph callback never becomes a
> task, so the whole-run FIFO cannot order it against one … It is ordered only by being alone."*

它已经做到四件事：

 | — | 机制 |
 | - | ---- |
 | **与 run 互斥** | 取 **`_submit_mu`** —— submit 自己用的那把序列化锁，**check 与命令之间不可能挤进一个 run** |
 | **poison 传导** | `_ordered_cleanup_error` 非空即拒，并提示 `close()` |
 | **静止要求** | 有任何 live run handle 即拒：*"only ordered when nothing else is"* |
 | **可重入** | 一个控制调用可由另一些构成（region 造 queue），第二次**加入**预留而非死锁 |

##### ⭐ 静止约束正好回答了决策 A 的悬念

`_control_reservation` **要求 worker 静止**（无 in-flight run）。于是：

> **per-worker 域只能在"两轮 run 之间"创建/释放** —— 这不仅**可行**，而且是**唯一可行的时机**。

我在决策 A 里担心的"A1 表达不了两轮 run 之间建域"因此**不成立**：真正的机制**只允许**
那个时机。启动 → warmup → **建域** → 稳态循环，正是 `_control_reservation` 的典型用法。

##### ⭐ 判断三被代码直接证实，不是提案

`requires_ordered_cleanup` 是 **`_RunResources` 上的一个字段**（`worker.py:3029`），
在该 run **创建**域/region/remote-ref 时置位；`_cleanup_bearing_predecessor` 读的就是它。

⇒ **per-worker 资源不进 `_RunResources`，就天然不置位。** 分类器**已经按 run-owned 判别**，
不需要为持久资源加任何特例 —— 判断三描述的是**既有行为**，不是要新增的规则。

##### ⇒ 真正剩下的只有"入口"

 | — | 状态 |
 | - | ---- |
 | 排序机制 | ✅ **已在 main**（`_control_reservation`），但**只覆盖"run 外"这一个入口** —— 见下节 |
 | cleanup-bearing 正确分类 | ✅ **天然成立**（键在 `_RunResources.requires_ordered_cleanup`，`worker.py:3142`） |
 | **`allocate_domain` 的建图期门** | 🟡 **仍在** —— `worker.py:1173`。但它**不是独立改动**，理由见下节 F5 |

⇒ W3 要做的是**加一条 per-worker 路径**：走 `_control_reservation` + charge 到 per-worker
owner，而不是 `_building_run_resources`。**这是小改动，不是被阻塞的大工程。**

##### 🔬 与 #824 已验证实现的逐条对账（2026-08-03，读 `upstream/main@2a650f2d`）

\#824 报告者称已有验证过的实现，四条声明：**① `Worker` 域分配 · ② 复用前清零每个参与的
local window · ③ 每个 `Worker.run` 对每个结构域键一个 lease · ④ `Worker.close()` 释放**。
①③④ 与本节的两生命周期模型**一致**——他们独立走到了同一形状。但该实现验证于 **2026-07-20**，
早于 **#1466（07-27）/ #1541（08-01）/ #1587（08-02）**，所以是在**旧世界**里成立的。

###### F1 —— `_control_reservation` 覆盖不到 #824 的创建时机

`worker.py:8270-8272` 自己写明有两条路：*"a call inside a graph callback belongs to that run
and waits for the FIFO head; one that belongs to no run reserves the worker for its duration."*
从 graph callback 里调 `_control_reservation` 会**双重失败**：`:8305-8311` 有任何 live run
handle 即 raise（当前 run 自己的 handle 已由 `:8341` 放进 `_accepted_run_handles`），
且 `:8298` 取的 `_submit_mu` 在 `_submit_l3_locked` 路径上已被持有 ⇒ 死锁在非重入 Lock 上。

⇒ **机制由创建入口决定，而这正好是两生命周期的分界**：per-orch 走 callback 内的
`direct_control`（已工作）；per-worker **必须在 run 外创建** → `_control_reservation`。
\#824 的"第一个用到的 run 里懒创建"是**第三种**，两边都不落 —— 要 per-worker 的寿命，
却走 per-orch 的入口。

###### F2 —— `_RunResources` 隔离是硬要求；今天的 `_allocate_domain` 无条件违反它

```python
:7188  _release_fn=lambda released, owner=resources: ...   # 闭包绑死创建它的 run
:7193  resources.live_domains[name] = handle              # 无条件
:7194  resources.requires_ordered_cleanup = True          # 无条件
```

⇒ `:8442` run fence 的 `_release_all_live_domains(resources)` **在创建它的那个 run 结束时
就释放它**。持久域**活不过创建它的 run** —— 正是 #1466 那个 bug 的形状；顺带把创建它的
run 打成 cleanup-bearing，K=2 白降 1。

###### F3 —— per-worker 的注册表与 `close()` 兜底 *已经在 main 上*

```python
:7192  self._live_domains[name] = handle      # Worker 级注册表，今天就在写
:7543  live_domains = self._live_domains if resources is None else resources.live_domains
:8964  if self._live_domains: _step(self._release_all_live_domains)   # close()，无参 ⇒ 扫 Worker 字典
```

⇒ #824 的声明 ④ **在 main 上已经成立**，零新代码；它今天不生效**只是因为 run fence 先到**。
⇒ 所以 W3 的改动比"加一套生命周期"小得多：**只是创建时别登记进 `resources`** ——
跳过 `:7193`、跳过 `:7194`、换掉 `:7188` 的 owner。

**F5 —— `:1173` 的建图期门与 F2 是同一件事。** 那道门存在，**是因为**今天的设计要求一个
run 拥有域（`resources` 取自 `_building_run_resources`）。取消所有权要求，门的理由自动消失。
上表把它列为"剩余项"是记账方便，不是第二处改动。

#### W3 设计：per-worker 生命周期的判断（2026-08-03）

上面定了**两种生命周期**。以下是把 per-worker 扩到"带 device 流量的资源"所需的判断。
每条都能追到一条已存在的约束。

> **异步流水计划的权威副本在
> `~/workspace/simpler/.claude/worktrees/pipeline-codex/worker-async-pipeline-plan-v2.md`**
> —— **2026-08-03 更新版，基线 `2a650f2d`**。本节引用的 §5.7/§5.8/§5.9 均以该副本为准。
> **复核（2026-08-03）：§5.7 的分类清单与 §5.9 的六条逐字未变**，其中 §5.7 那句
> *"another operation that installs **run-owned** device teardown"*（判断三的全部关键）
> 与 §5.9 第 ⑥ 条（#1456 审计的判据）都在原处；decision log 增至 22 条，
> #20/#21 未动。⇒ **本节各判断继续成立。`#824` 在该计划中仍是 0 次命中。**

##### 判断一：不变量只有一条 —— **run 的 fence 永不触碰 per-worker 资源**

\#1466 修的 bug **不是**「Worker 级注册表有害」，而是**「run 的 fence 去扫它」**。
分开之后，per-worker 的本体就是一个 Worker 级 owner，加一条不变量：

> **任何 run 的 fence 都不得触碰 per-worker owner。** run fence 只释放**自己的 retain**。

这条在 #1541（`9a2a5f71`）之后是**可强制的**：清理边界已是显式对象
（`_RunFinalizationCursor` 按名字驱动 one-shot edge），per-worker 只要**不在它的 edge
列表里**即可 —— 不需要在隐式扫描里开洞。

##### 判断二：入口按"发不发 device 控制流量"分，见上节

~~早前写作 "A1 复用建图期 vs A2 新增入口" 的二选一。~~ **该框架已作废** ——
在两生命周期模型下，per-worker 资源**本来就该由 worker 级 API 创建**（`create_buffer`
即是先例）。真正的分界与阻塞见上一节的表。

##### 判断三 ⭐ **`cleanup-bearing` 必须区分 run-owned 与 per-worker** —— 这是 K=2 不降级的关键

异步流水 §5.7 的分类器今天是「**该 run adopt 了 CommDomain ⇒ cleanup-bearing ⇒ 深度降 1**」。
若照搬，**每个用到持久域的 run 都会降级**，#824 的流水收益归零。

精确化后的判据（**可导出，不是声明**，满足 decision log #21）：

> ###### `cleanup-bearing(run) ⟺ 该 run 的 fence 上需要执行 device-affecting teardown。`

**这不是我另立的判据 —— 计划自己的措辞已经蕴含它**：分类清单的最后一条写的是
*"another operation that installs **run-owned** device teardown"*。
**`run-owned`** 这个限定词就是全部关键：per-worker 资源装的**不是 run-owned teardown**。

逐项代入：

 | run 对持久资源做了什么 | fence 上要 teardown 吗 | cleanup-bearing |
 | ---------------------- | ---------------------- | --------------- |
 | **创建**它（`lifetime=PER_WORKER`） | ❌ —— 持久资源不在本 run 的 fence 上释放 | **否** |
 | **retain / 使用**它 | ❌ —— 只掉一个引用计数，无 device 流量 | **否** |
 | 创建**run-scoped** 域（今天的默认） | ✅ | **是** |

⇒ **持久域的整条使用路径都不降深度。** 注意这不是给持久资源开的特例：
判据换成"fence 上有没有 device teardown"之后，**结论是算出来的**，
而旧判据（"adopt 了 CommDomain"）只是它在"域总是 run-scoped"前提下的一个巧合近似。

##### 判断四：显式 release **自己就是一次 cleanup-bearing 的控制操作**

decision log #20 要求回答「persistent 的释放属于**哪个 run 的 `S(N)``」。答案是：

###### 不属于任何 run —— 它自己就是一次受控操作

依据是异步流水 §5.8「Direct control uses the same ordering domain」：本地控制、
远端控制与 mailbox 设备查询**共享同一个 run/admission 排序域**。于是 release 的契约是：

```text
release_per_worker(handle):
  1. 拒绝新 retain（handle 转入 RELEASING）
  2. 等待所有已接受 run 的 retain 归零        ← 不是等 native active_tasks==0
  3. 作为一次控制操作进入排序域，并按 §5.7 声明自己 cleanup-bearing
     ⇒ 它这一次把深度降到 1（后继必须等它的 device teardown 完成）
  4. 事务性 teardown（判断五）
  5. 发布 cleanup handoff，恢复 K=2 准入
```

##### ⇒ 成本模型：把降深度从「每个 run」搬到「一次 release」

 | — | 每 run 创建域（今天） | persistent（W3） |
 | - | --------------------- | ---------------- |
 | domain alloc/free | 每 run 一次（实测 **150 ms**） | 一次 |
 | K=2 → 1 降级 | **每个 run 都付** | **只有 release 那一次付** |

这是 [`../README.md`](../README.md) §1.3 记录的那条双向耦合的**定量形式**。

##### 判断五：事务性与账本，**沿用既有机制，不另造**

 | 要求 | 沿用什么 |
 | ---- | -------- |
 | 多 chip teardown 的事务性 | 异步流水 §5.9：provisional ownership → per-chip 成功追踪 → 任一失败**全员尝试回滚** → 完整清理不可证时 **poison ordered admission** |
 | 失败后的可重试账本 | **P0.2 follow-up 簇C（retryable journal）** —— `../P0.2.md`（本地参考：`../P0.2/done-followup.md`）。⚠️ **两者必须是同一套**，否则 persistent 与 run-scoped 会有两本互相不认的账 |
 | 陈旧 handle 拒绝 | `DomainId = stable id + generation/epoch`；release 后 epoch 递增，旧 handle 失配即拒 |
 | close 兜底 | 上表已定顺序：**先排空 RunHandle → 再回收 persistent → 最后拆 Worker-tree** |

###### 🔬 核实结果：账本**只统一了一半**（2026-08-03，核 PR #1456 `a05b944f` / 基线 `2a650f2d`）

判断五要求 persistent 与 run-scoped 共用一本账。逐条核对 §5.9 六条后：

 | 失败路径 | ①provisional | ②逐参与者 | ③回滚所有成功者 | ④失败仍继续 | ⑤全程后抛首错 | ⑥不可证时 poison |
 | -------- | ------------ | --------- | --------------- | ----------- | ------------- | ---------------- |
 | 正常 release（`worker.py:8844`） | n/a | ✅ | ✅ | ✅ | ✅ | ✅ 经 fence `:9857` |
 | **分配失败 ABORT**（`:8759`） | ✅ `:8699` | ✅ | ✅ | ✅ | ✅ | ❌ **stderr** |
 | L3 节点 close（`:8460`） | n/a | ✅ | ✅ | ✅ | ❌ 不抛 | ❌ **stderr** |

⇒ **release 账本已经是同一本**（两条路都经 run fence，都能 poison）。
⇒ **分配失败账本不是**：本地路径（`main:7192-7194`）**先注册再冒险**，让 fence 拥有清理并在
释放失败时 poison；远端路径**内联回滚 + stderr**，且失败时 `:8785-8787` 从未执行 ⇒ 该 domain
**既不在 `_live_global_domains` 也不在 `resources.live_global_domains`**，fence 与 `close()`
都够不着，`requires_ordered_cleanup` 也未置位 ⇒ **失败的 run 连 cleanup-bearing 都不算，
后继按满深度放行**。这就是 §5.9 所说 **false clean handoff** 的纯粹形态。

⇒ **判断五补一条前置**：**W3 的持久化全局域不得原样继承分配失败路径。**
修法不是新造机制 —— main 上已有 `_record_unreclaimable(message, cause)`，其 docstring 正是
此场景（*"device state that no cleanup can reach: nothing tracks it, so there is no handle for
a run's fence to fail on"*），只需在 `:8766` 的 `except` 里调用它。

> **更正我自己**：§D.5 此前挂的疑点是「`ABORT` 只是一个*阶段*，未必是逐参与者回滚」。
> **该疑点不成立** —— `:8759-8773` 就是规范的逐参与者 fanout，内层 `try` 保证一个失败仍继续，
> 末尾重抛原始异常。**回滚强度本身达标；缺的是第 ⑥ 条。** 硬证据：#1456 在 `worker.py`
> 加 +1451 行，**新增 poison setter = 0**（两版 `_ordered_cleanup_error` 赋值点一一对应，全为既有）。

##### 判断六 ⭐ **复用前的清零必须是图里的一个 task，不能是 host 侧的准备步骤**

前五条判断只覆盖了**创建**与**释放**，漏了**复用** —— 而持久资源的整个价值就在复用。
\#824 已验证实现的声明 ② 是「**复用前清零每个参与的 local window**」，这是 **device 流量**。

在**深度 1** 的世界里它安全：run N 结束 → 清零 → run N+1 用。
但 **#1587（`2a650f2d`，2026-08-02 合入，＝当前 main HEAD）** 已经把
`P(N+1) ‖ S(N)` 变成 main 上的现实：

```text
S(N):    run N 正在读写持久窗口 W
P(N+1):  准备 N+1 …… 若在此清零 W  ← 与 S(N) 数据竞争
```

⇒ **#824 的实现是在 pre-pipelining 世界里验证的；直接搬到 K=2 上，"复用前清零"会与
前一个 run 的执行竞争。** 修法是确定的：**清零必须是 N+1 图里的一个 task** ——
设备执行严格串行（`S(N)` happens-before `S(N+1)`），作为 task 的清零天然排在 N 之后；
作为 host 准备步骤则不排。

> 这条**两边都没写**：#824 不知道后来会有 K=2，异步流水计划不知道有人要跨 run 复用
> 域窗口。与 §「W3 与 Worker 异步准备流水 track 的双向耦合」同源 —— 只有把两条 track
> 摆在一起才看得见。
>
> **本判断依赖的是「机制已在 main」这个结构事实，不依赖重叠被测量。** 需要区分：
> 异步流水计划 §15 自陈 *"the only evidence of `P(N+1) || S(N)` is a timing number
> quoted in a PR description"*，并据此把 **C2a trace V1 排到 B5 之前**去补证据。
> ⇒ **重叠的_证据_待 C2a-T5；但让 host 侧清零可能与 `S(N)` 并发的_代码路径_已经存在**
> —— 竞争成立与否只取决于后者。

##### 仍然遗留（不在本判断集内）

- **容量不可见**：持久资源长期钉住 HBM，而 `committed_device_memory()` **不计 comm window**
  （见下节）。持久化**放大**了这个缺口 —— 常驻期整段占用都看不见。
- **retain 的粒度**：按 handle 还是按 `(handle, run)`？前者简单，后者能诊断"谁没放手"。
  倾向后者，但需要看 `_RunFinalizationCursor` 的现有记账粒度再定。

#### 🟡 #1592 交付了 W3③ 的一半，缺口正好是 Buffer-backed comm window（`ce81c973`，2026-07-30 合入）

`committed_device_memory()` 三层贯通（L1 `MemoryAllocator` 的 ptr→size map + 累计
字节计数 → L2 `DeviceRunnerBase::committed_device_memory()` 过 C ABI + `ChipWorker`
nanobind 只读属性 → L3 `control_committed_device_memory` 控制op），关闭了 issue
**#1457**（下游按 `aclrtGetMemInfo` 估 KV cache 会漏算 simpler 自己的 rtMalloc arena）。

但 commit 显式声明作用域:

> Scope: counts memory routed through the host `MemoryAllocator` (user tensors +
> pooled arenas + runtime buffers); **excludes HCCL/VMM comm windows**.

**被排除的 HCCL/VMM comm window 恰恰承载本设计的 payload Buffer backing。** 所以 W3③ 现在是:

- ✅ **上报的骨架有了** —— 三层管线、C ABI、Python 属性都已就位，**新增一类计量不必再造管线**
- ⬜ **Buffer-backed 域资源仍不可见** —— comm window / payload Buffer 的字节数没有任何计入口

⇒ W3③ 的做法从"从零做容量上报"改成 **"给 persistent Buffer/domain resource 定义计量口径并接入
`committed_device_memory()` 已有的三层管线"**，规模下降；但要**先想清楚口径**:
VMM window 是**保留地址空间**还是**已提交物理页**、peer-import 的那份在 importer 侧
**算不算重复计入** —— 口径不定，接进去就是错数,比没有更糟。

#### ⭐ W3 与 **Worker 异步准备流水** track 的双向耦合（2026-08-02 发现）

> **出处**：`~/worker-async-pipeline-plan-v2.md`（1987 行，2026-07-31 更新，基线
> `main@a07bc904`）—— 一条**独立的轨道级计划**，目标是
> `P(N+1) ‖ S(N)`（host/L2 准备 N+1 与设备执行 N 重叠），终点是让 Qwen decode 稳态
> device-bound。**该文档不在 `.docs/` 下**，登记见 [`../README.md`](../README.md) §1。
> 它的 A0 阶段就是 **#1466** —— 也就是上面那条"改变了 W3 地基"的重构。

**该计划 §5.7「Device-affecting cleanup is part of `S(N)`」**：

> 分类依据是 run 实际持有的资源：**CommDomain created** / L3–L2 region created /
> remote slot ref adopted …
>
> Pure task runs retain K=2 admission.
>
> ##### A cleanup-bearing predecessor conservatively degrades that boundary to depth one

⇒ **任何创建 CommDomain 的 run 都会把流水深度从 2 降到 1**，即**丢掉整条异步流水的收益**。

##### 方向一：W3 让那条 track 的成本消失

域一旦**跨 run 持久**，它就**不再是"这次 run 创建的资源"** ⇒ 该 run **不再 cleanup-bearing**
⇒ **不降级**。

**所以 #824 的价值被低估了**：它不只是省掉实测的那 150 ms domain alloc
（203 ms → 53 ms，3.84×），它还让 **K=2 流水在带域的 workload 上活下来**。
两条 track 的收益**叠加**，而不是各自独立。

> ⚠️ **两边都没看见对方**：该计划全文 **`#824` 出现 0 次**；本设计此前也只把 #1466
> 当作"某次 refactor 撞了 W3"。这条耦合是 2026-08-02 才对上的。

##### 方向二：W3 的 lifetime 设计**必须**满足那条 track 的规则

新增的第三种 lifetime（persistent）**不能自由设计**，至少要过三关：

 | 约束（出处） | W3 必须满足什么 |
 | ------------ | --------------- |
 | **§5.7 cleanup-bearing 分类** | persistent 域必须能被**分类器识别为「非本 run 创建」**，否则会被误判成 cleanup-bearing，**白白降深度**——那样 #824 就只剩省 alloc，丢掉本节方向一的收益 |
 | **§5.7 分类来源** | 分类**只能来自 run 实际 adopt 的资源集合**，**不得**从 callable 符号 / code image / 用户承诺推断（decision log #21）。⇒ persistent 域的"归属"必须是**可枚举的运行期事实**，不能是声明 |
 | **§5.9 cleanup 事务性** | 多 chip 域的创建/释放要求：先记 **provisional ownership** → per-chip 成功追踪 → 任一失败**全员回滚且逐个尝试** → 完整清理不可证时**poison ordered admission**。⇒ persistent 域的 release 事务要与此**同源**，不能另起一套 |
 | **decision log #20** | run fence **不结束于** native `active_tasks == 0`；当 run 拥有 device-affecting Python cleanup 时，**该 cleanup 属于 `S(N)`**。⇒ persistent 域的释放发生在**哪个 run 的 S(N)** 里，必须有明确答案 |

##### 对 W3 的净影响

 | — | — |
 | - | - |
 | **优先级** | ⬆️ 上调 —— 从"CPU↔NPU track 的一个 W 项"变成**两条 track 的共同前置** |
 | **规模** | **M-L 不变**，但设计约束变紧（上表四条） |
 | **不可再"独立设计"** | 此前 l3l4 说 #824 是"独立、低风险的第一步"，**已被 #1466 推翻一次**；现在进一步明确：它还要与异步流水 track 的 slot/lease 模型对齐 |

### W4 同步原语归一 + arch 维护抽象 —— **扩展已有**

 | 项 | 内容 |
 | -- | ---- |
 | **做什么** | ① 把 counter `notify/test/wait` 提升为 Buffer-backed Attachment 的通用同步 API（现绑在 `WorkerChipRegion` 代码符号上）；② **按 arch 的 cache maintenance 抽象**：**a5 上编译成 no-op 也必须有调用点**（a5-first 纪律①）；③ **只暴露 publish/observe，不暴露裸 fence** |
 | **现状锚点** | ✅ 已有大半：`python/bindings/task_interface.cpp` 的 `_l3_host_mapped_counter_notify` / `test` / `wait`；`src/common/platform/include/aicpu/cache_maintenance.h::cache_invalidate_range` / `cache_flush_range`；契约 `docs/l3-l2-orch-comm.md` §4–§5 |
 | **规模** | **S–M**。主要是**搬家 + 补 a2a3 维护点** |
 | **依赖** | W1 |
 | **⚠️ 最高风险** | **这是整个设计最容易错的一块**——`docs/hardware/cache-coherency.md` 记录了 PR #540 的真实事故（用错原语、靠内嵌 `dsb sy` 歪打正着）。**a2a3 onboard 验证是必要条件** |

### W5 委派机制 + 事务语义 —— ⬜ **全新，承重墙**

 | 项 | 内容 |
 | -- | ---- |
 | **做什么** | ① **声明自顶向下、执行委派给 owner**（L4 声明 → L3 展开 → chip 执行 → 逐级上报）；② **部分失败回滚** / **成员冻结** / **域 epoch·代际** / **全员 READY barrier** |
 | **现状锚点** | ✅ **单层版本已存在、可推广**：`worker.py` 的 `_handle_ctrl_alloc_domain`（chip child 侧处理 + 回报）、`Worker._allocate_domain`（L3 侧输入校验 + per-chip shm 布置）、`Worker._dispatch_control_domain`（*"Fan out CTRL_ALLOC_DOMAIN / CTRL_RELEASE_DOMAIN to all participating chips"*）。**本设计 = 再往上推一层** |
 | **规模** | **L**。展开是小的，**事务语义（回滚/冻结/epoch/barrier）是大的** |
 | **依赖** | **W2**（需端点模型才知道往谁委派） |
 | ~~**⚠️ 待拍板**~~ ✅ **已关闭 2026-08-07** | 这四项在 l3l4 §D.5 已列为 L4 建域必需件。~~**若改为"L4 直接操作 L2"，W2 + W5 都要重写**~~ —— ⛔ **本格 2026-08-22 更正：这个问题在同一文件的 [§3 问题 2](#3-三个必须先答的问题会改变分解) 已标为关闭，而本格还留着它的旧拷贝**（§4 规则 1 的形状，且两处在同一个文件里 ⇒ 更难发现）。依据：**#1456 事实上替它投了票** —— 它就是委派机制（`PREPARE_EXPORT → IMPORT → COMMIT`），双机真硬件 pod CI 跑通；**2026-08-22 复核该握手仍在树上**（`upstream/main@6ebaf771`，`python/simpler` 四文件 12 处；阳性对照：`ProviderRegionStore` 在同目录 3 文件 19 处 ⇒ 搜索姿势有效）。；⚠️ **一次性代价已经付出去了**：**#1933 `6ebaf771` 依这个已关闭的答案冻结了 provider 契约**（正文 *"It freezes the provider contract W5 will call"*）。若这个决定被重开，作废的是：**provider 是唯一 allocate/describe/release 权威** · **私有 allocate/release 控制 wire（L3→L2 mailbox 上的私有平面）** · **`RegionInstanceRegistry` 的 run-scoped 清扫与保留策略**。不作废的是 PAYLOAD/COUNTER 两块独立物理分配、commit-last 的发布边、`NotifyOp::Add` 的回绕修法。⇒ 状态跟踪在 [`README.md`](../README.md) §2 的 W5 行 |

### W6 模板库 —— ⬜ **全新，但有现成原型**

 | 项 | 内容 |
 | -- | ---- |
 | **做什么** | **第一版只做 SPSC queue**，再按实际 workload 扩 ring、freelist pool、mailbox；用户以自己的元素类型实例化。**MPMC deferred**：只有跨 domain/endpoint 的 atomic RMW、cache maintenance、fairness 和 failure recovery 都有硬件证据后才能设计，不得从 SPSC 直接外推 |
 | **现状锚点** | ✅ **`python/simpler/worker_chip_message_queue.py` 是现成 SPSC 原型**——descriptor ring + payload arena + head/tail counters、`WorkerChipQueueDescSlot{seq,opcode,payload_offset,payload_nbytes}`、STOP opcode。第一版只参数化这个已验证形状 |
 | **规模** | **M**（第一个 queue 模板）＋每增一种 **S** |
 | **依赖** | W1 + W4 |
 | **⚠️ 硬契约** | **一律有界 + 背压，禁止隐式扩容**（约束 §4 推出）；第一版只宣称 SPSC；**必须双 arch onboard 覆盖**才能标 stable |

### W7 常驻 worker —— **改执行模型**

 | 项 | 内容 |
 | -- | ---- |
 | **做什么** | `orch_fn` while 循环常驻；干净的 STOP / drain / 错误传播 |
 | **现状锚点** | ✅ `worker.py` `Worker.run`；STOP 语义有原型（WorkerChip queue 的 STOP opcode）；"常驻 L2 编排任务"模式已在 `examples/workers/l3/worker_chip_orch_comm_stream` 跑通 |
 | **规模** | **M**。**循环体内 dispatch 仍走现有 DAG 调度**（不改执行模型，风险最小） |
 | **依赖** | **W3**（persistent Buffer/domain resource 必须先能跨 run 存活） |

### W8 API 收敛与迁移 —— **收尾**

 | 项 | 内容 |
 | -- | ---- |
 | **做什么** | 按 roadmap 语义收敛为 `Worker.create_buffer(...)` 与 `Worker.create_domain(MemberSet, ResourceBundle, AttachmentPlan, TransportPlan, ...)`（按需带 `DeviceRankMap`）；`Queue` 绑到域中某个 Buffer 的 live Attachment，不绑层级对。迁移旧调用点；**并行提供 → 迁移 → 废弃**；⚠️ **决策 C（2026-08-03）**：`attach_buffer` / `detach_buffer` **不进 public surface**，Attachment 只经 `AttachmentPlan` 在建域时批量声明 —— 见 [`../worker-memory-model.md`](../worker-memory-model.md) §5.7。这是 roadmap §2.3「public surface 可以更小」许可范围内的收窄 |
 | **现状锚点** | ✅ **迁移面已量化：66 个调用点、4 个文件**——`examples/workers/l3/worker_chip_message_queue/`、`examples/workers/l3/worker_chip_orch_comm_stream/`、`tests/ut/py/test_worker/test_worker_chip_orch_comm.py`、`tests/ut/py/test_worker/test_worker_chip_message_queue.py` |
 | **规模** | **S–M**（迁移面比预想小） |
 | **依赖** | W2 + W6 |
 | **命名纪律** | 见 [domain-membership.md](domain-membership.md) §10：**API 名不得含层级对；端点用 `EndpointId`；禁止以 level 判定共享能力**。旧 landing 中的 `create_region(...)` 只能当迁移期 placeholder/legacy alias，不得再作为规范资源名：物理资源是 Buffer，域组合是 ResourceBundle |

## 2. 依赖图与排期

> **2026-07-30 更正**：旧图与各项「依赖」行**互相矛盾**（图里 W3←W2、W5←W1、W6←W5，
> 三条在表中都不存在；且 W5→W6 会让 `W6=SM2` 早于 `W5=SM4` 成为不可能）。
> **以各项表格的「依赖」行为准**，图按它重画；"W0 阻塞一切"的说法同时更正为
> **W0 只阻塞 W1 及其下游**（W2 / W3 与 W0 无关，可立即开工）。
>
> **2026-08-07 再更正**：**"W0 阻塞 W1"已进一步收窄**——新增 **W1lite**（按 platform 分派
> `ld/st` / `rtMemcpy` 的封装接口，见 §1 W1lite 条）无需 W0，W1 本身只是把 W1lite 里
> A5 的宏分支从 `rtMemcpy` 翻成 `ld/st`。**W4 依赖 W1lite 提供的统一接口，而非 W1 的
> 真映射**——W4 做的是"把 counter notify/test/wait 提升为通用 API"，这是接口层工作，
> 不需要 host-map 真的生效即可开工；等 W0 通过、W1 把宏一翻，W4 已完成的代码不必改动。

依赖关系（**唯一真源 = 各 W 项的「依赖」行**）：

 | 项 | 依赖 |
 | -- | ---- |
 | W0 | 无 |
 | W1lite | 无 |
 | W1 | W0 + W1lite |
 | W2 | 无 |
 | W3 | 无 |
 | W4 | W1lite（**不是 W1** —— 见上方 2026-08-07 更正） |
 | W5 | W2 |
 | W6 | W1 + W4 |
 | W7 | W3 |
 | W8 | W2 + W6 |

```text
链 A（平台/模板）：  W1lite ──→ W4 ──┐
                       │            ├──→ W6 ──→ W8
                       ▼            │
                  W0 ──→ W1 ────────┘
链 B（成员/事务）：  W2 ──┬──→ W5             │
                          └────────────────────┘
链 C（生命周期）：   W3 ──→ W7
```

三条链**只在 W8 汇合**（W8 = W2 + W6）。链 B 的 W5、链 C 整条都不在 W8 的前置里。
**W6 仍需真正的 W1**（真实 ld/st 映射带来的性能收益是模板库要展示的能力），
但 **W4 不必等 W1**——这是 W1lite 存在的全部意义：把"能开工"从"W0 通过"提前到"现在"。

对应设计文档 [cpu-npu-shared-memory.md](../cpu-npu-shared-memory.md) §5 的
**shared-memory track 局部分期 SM0–SM4**（不是全局 roadmap P0–P3）：

 | 阶段 | 工作项 | 排期如何满足依赖（**不表示已完成**） |
 | ---- | ------ | ------------------------------------ |
 | **SM0** | **W0** | 无前置；当前仍为 W0 标题所示的 🚧 待核实状态 |
 | **SM1** | **W1**(a5) + **W4** + **W2**（**W2 与 W0/W1 无依赖关系，可立即并行**） | 排期为 W0→W1→W4；W2 无前置，可与该链并行。W0/W1/W4 当前状态仍以各 W 项标题为准 |
 | **SM1.5** | **W1 的 a2a3 落地 + onboard 验证** | **与 SM1 紧邻**——a5-first 纪律②③ |
 | **SM2** | **W6**(queue 模板) + **W8** 部分迁移 | W6 依赖 SM1 内的 W1+W4；**W8←W2+W6，故 W8 只能在 W6 之后、同阶段内串行**（"部分迁移"指此） |
 | **SM3** | **W3** + **W7** | 两者与前面各阶段**无依赖**——放 SM3 是**排期选择（人力/风险）**，不是依赖所迫；若 #824 提前，**可整体前移到 SM1** |
 | **SM4** | 对称/OpenSHMEM 拓扑 + **W5** 完整事务语义 | W5←W2；放 SM4 是**排期选择**，W5 可在 W2 完成后任意阶段开工 |

> **读法**：SM0→SM4 的顺序里，**真正由依赖强制的只有** W0→W1→W4→W6→W8 这一条链。
> W3/W7 与 W5 落在后面的阶段是**优先级判断**,不是技术阻塞 —— 需求变了可以直接前移。

**规模汇总**：XS×1（W0）· S–M×3（W1/W4/W8）· M×3（W3/W6/W7）· M–L×1（W2）· L×1（W5）。

## 3. 三个必须先答的问题（会改变分解）

1. **W0 的结论** —— 若 a5 返回**非 identity VA**，W1 需连带修 `host_build_graph` 的 identity
   断言，规模 S → M。
2. ~~**委派机制是否认可**（W5）~~ —— ✅ **已关闭（2026-08-07）**。原文：「"声明自顶向下、执行委派给
   owner" 若被否决（改为 L4 直接操作 L2），**W2 + W5 重写**」。**#1456 事实上已经替这个问题投了票**
   —— 它就是委派机制（`PREPARE_EXPORT → IMPORT → COMMIT` 五步握手），并在双机真硬件 pod CI
   跑通。⇒ **W2 / W5 不再背「可能整体重写」的风险**，这解除了 W2 开工的最后一个不确定性。
   见 [w2-w5-inversion.md](w2-w5-inversion.md) §5.2。
3. **第一个真实 workload** —— 决定 W6 先做哪个模板、W2 先支持哪种成员集合
   （见设计文档 §8）。

## 4. ⛔ 这一节的前提错了 —— 它们不是「交叠」，是同一条 track

> **2026-08-07 用户裁决。** 本节标题一度是「**三个交叠提醒（勿各做一套）**」，
> 而第三条是同一天加的 —— **三次记录到这两条线撞在一起，三次的处理都是「记一条提醒」
> 而不是「合并」**。而 [总纲](../logical-physical-separation.md) §1 的
> `potential = deployment × interconnect × backing/backend × runtime support`
> 早就写着答案。
>
> **原理**：物理资源（内存、通信域）与逻辑概念（Worker）解耦；
>
> ### 一个 worker 能不能持有或访问一块资源，由「它部署在哪」×「那里到资源物理位置的连通性」判定
>
> ⇒ `capability(endpoint_deployment, resource_physical_identity)` **只有一个函数**，
> 而本 track 与 worker 内存模型 P1 track **都是它的一部分** —— 前者供物理输入与 wire 载体，
> 后者供资源侧身份，判定函数本身（W2 / #1696）两边都要。
>
> ⇒ **合并后的分组与顺序见 [`../README.md`](../README.md) §1.5**；本节下面三条降为
> **历史记录**，说明这个错是怎么积累的。判据见 [`track-boundaries.md`](../track-boundaries.md) §3：
>
> ### 当你第二次为同两条 track 写下「别各做一套」时，那不是提醒该加，是分线该改

### 4.x 历史记录：那三条「提醒」

1. ~~**W3 ↔ P0.2 follow-up 簇C（retryable journal）**~~：✅ **已消解（2026-08-07）** ——
   **P0.2 follow-up 现已 DONE**（八条退出条件全绿，末批 #1689 `f8106e24`）。W3 不再需要
   「与一个在飞的东西对齐」，**直接复用已落地的 journal 即可** ⇒ **W3 的风险与规模下调**。
   见 [w2-w5-inversion.md](w2-w5-inversion.md) §5.3。
2. **W2 的 `EndpointId.worker_path` ↔ 全局 P1-A 的诊断 path side table**：只共享
   路径的格式/格式化工具，不共享存储或语义；worker path 不进入
   `CanonicalBufferIdentity`，`owner_worker_path_id` 也不参与 routing。
3. ⭐ **W2 端点注册表 ↔ P1-B 的 `ImportContext`（2026-08-07 新增，且比上两条更紧）** ——
   上两条是「别各做一套」，这条是**同一件活被算进了两个里程碑**：`materialize` 缺的那份
   endpoint × `address_space` 上下文，正是 PR #1696 的 `EndpointRegistry` ＋
   `RegionAccessService` 已经造出来的形状，而**两边互不知情**。判定所需的信息**结构性地不在
   wire 上**（`owner_instance_id` 是不透明 nonce · `owner_worker_path_id` 按设计不参与任何
   routing/identity 决策 · `address_space` 只有 HOST/DEVICE 两值不区分哪张卡）⇒ 只能由
   registry / owner authority 供上下文 ⇒ **既然只能有一个 registry 权威，就不该有两个矩阵**。
   完整依赖链与合入排序见 [../w2-p1b-endpoint-convergence.md](../w2-p1b-endpoint-convergence.md)。

## 5. 本文不含什么

- **实现者 prompt**（怎么写代码）——按 W 项在各自 gate 打开时单独拆到 `.docs/tasks/`。
  建议**先只写 W0 + W1**：W0 结论会改变后面的形状，现在写全套有返工风险。
- **收集器 host-影子简化**（PMU / L2 swimlane / scope_stats / args-dump）——a5 host-map
  打开后的**附带机会，单独立项**。
