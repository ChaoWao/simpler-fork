# CPU↔NPU 共享内存：硬件与平台约束（参考）

> **发布快照：2026-10-21。** 设计、实现状态与历史调查须按正文各自基线阅读；不代表当前 main 全量验收。参见[阅读入口与已知更正](../README.md)。
>
> **状态**：📋 **约束参考**（本设计的地基）。这是
> [cpu-npu-shared-memory.md](../cpu-npu-shared-memory.md) 的 §1 展开。
> **为什么单列**：这些是**硬件/驱动事实**，比设计本身稳定，且实现者需要反复查阅。
> **标注**：✅ 已核实（附 `file::symbol` / commit 锚点）｜🟡 待验证｜⬜ 提案。
> **权威外部参考**：`docs/hardware/cache-coherency.md`（一致性规则的 SSOT，本文不重复）。

**读这份文档的理由**：本设计的**每一条决策都能追溯到下面某条约束**，没有一条是风格
偏好。改设计前先确认对应约束是否仍然成立。

## 1. 没有 UVM，但**有** host 侧映射——四件事要分开看

CUDA UVM 是四个能力的合集；本平台只提供其中第一项，且**不是契约**：

 | UVM 能力 | `halHostRegister(DEV_SVM_MAP_HOST)` |
 | -------- | ----------------------------------- |
 | ① 同一 VA 两侧可解引用 | 🟡 **实测 identity，但 HAL 契约不保证** |
 | ② 自动页迁移（谁访问搬给谁） | ❌ 数据**永远留在 HBM**，host 每次访问走 PCIe |
 | ③ 硬件/运行时管理的一致性 | ❌ 必须手工 cache maintenance + 顺序原语（§3） |
 | ④ 超额分配 / 换页 | ❌ 物理钉死，不够即硬失败（§4） |

✅ 依据：
`src/a2a3/platform/onboard/host/device_runner.cpp::DeviceRunner::register_device_memory_to_host`
调
`halHostRegister(dev_ptr, bytes, DEV_SVM_MAP_HOST, device_id_, &host_va)`——
**`host_va` 是出参**，签名本身就允许它 ≠ `dev_ptr`。
`src/a2a3/runtime/host_build_graph/host/runtime_maker.cpp::bind_callable_to_runtime_impl`
明写：
*"the HAL contract permits a non-identity VA"*，并以**断言 + prepare 失败**兜底。

> **准确表述**：平台提供的是 **host 侧映射（host-side mapping）**，不是**统一内存**。
> 它解决"能不能访问"；UVM 解决的是"访问后数据在哪、快不快、一致性谁管"。

## 2. 🟡 a5 host-map：**外部驱动假设（未验证）+ simpler adapter 尚未实现**

> ### ⚠️ 状态定级更正（2026-07-30）
>
> 本节早前写作"驱动已支持"并据此把 a5 host-map 当作**既有能力**。**定级过高**，
> 更正为两条独立事实：
>
> | — | 状态 | 依据 |
> | - | ---- | ---- |
> | **驱动侧支持** | 🟡 **外部假设，未在本仓验证** —— 来自用户口述（2026-07-25），无 simpler 侧实测 | 待 **W0** 出结论 |
> | **simpler a5 adapter** | ⬜ **仍不存在**（2026-08-05 复核 `b535fa21`） —— `src/a5/platform/` 内**无 override**，a5 仍走 `DeviceRunnerBase` 的 base default（`device_runner_base.h:157` 返回 `nullptr`） | `git grep -- src/a5/platform/` = 空 |
>
> ⚠️ **判据要收窄：`git grep -- src/a5` 已不再是空的**（#1685 `c2b97cc9` 起）。
> `src/a5/runtime/host_build_graph/host/runtime_maker.cpp:756` **调**
> `api->register_device_memory_to_host(dev_ptr, size)` —— 但那是**调用方**，紧接着
> `if (host_view == nullptr) host_view = host_ptr;` 回落到 staging 镜像。
> ⇒ **「a5 没有 host-map」的正确判据是 `src/a5/platform/` 有没有 override，不是全 `src/a5` 有没有
> 这个符号**。调用点的存在恰恰说明 host-map 路径**在 a5 上已被接线、只等 adapter 落地**，
> 而不是说明它已经可用。用旧判据复核会得出「a5 已有 host-map」的相反结论。
>
> ⇒ **设计可以按"a5 将会有 host-map"来做（a5-first 纪律），但不得把它当作已具备的能力
> 去描述现状**；一切依赖它的路径必须挂在 **W0 的验证结论**之后。
> 另注：下表 6 处注释是**按"a5 无 host-map"写的绕行**，删除它们的前提是 **W0 通过 +
> adapter 落地**，而非仅凭驱动侧传闻。
>
> ⭐ **2026-08-07 用户裁决**：W0 仍无人认领、短期不可用，但不必再等它才能写消费端代码——
> 见 [work-breakdown.md](work-breakdown.md) 的 **W1lite**：注册与访问各封装成一个接口，
> 接口内部按 platform 用宏二选一（A3 走 `ld/st`、A5 走 `rtMemcpy`）。下表 6 处注释描述的
> 正是 **A5 该走 `rtMemcpy` 分支的现状**——W1lite 落地后它们从"待删的过时绕行"变成
> "宏的一个分支的注释"，仍应保留到 W0 通过、W1 把 A5 的宏分支翻成 `ld/st` 为止；
> 届时改的是宏映射，不是调用点。

因此下述仓库内的 "a5 没有 halHostRegister" 断言**待 W0 结论后再处置**（若 W0 通过，
它们即为过时结论）：

 | 过时断言位置 | 内容 |
 | ------------ | ---- |
 | `src/a5/platform/include/common/kernel_args.h::KernelArgs::scope_stats_data_base` | "a5 has no halHostRegister — host keeps a separate shadow and refreshes it via `rtMemcpy DEVICE_TO_HOST`" |
 | `src/a5/platform/onboard/host/device_runner.cpp::DeviceRunner::init_scope_stats` / `init_dep_gen` | "No halHostRegister on a5" |
 | `src/a5/platform/include/common/pmu_profiling.h` 的 streaming-buffer design 注释 | "a5 has no halHostRegister (DAV_3510)" |
 | `src/common/platform/onboard/host/device_runner_base.h::DeviceRunnerBase::register_device_memory_to_host` | "a5 onboard has no host-map path" |
 | `src/common/platform/include/host/profiler_base.h` 的 `SVM vs host-shadow paths`、`ProfilerBase::release_one_buffer` / `alloc_paired_buffer` | 收集器按"非 SVM 平台"分支 |

✅ **关键有利事实**：a2a3 侧的 `halHostRegister` 是通过
**`dlopen("ascend_hal")` + `dlsym`** 动态取得的
（`src/a2a3/platform/onboard/host/device_runner.cpp::load_hal_if_needed` /
`get_halHostRegister`），签名
`int (*)(void *dev_ptr, size_t size, unsigned flags, int device_id, void **host_ptr)`
——**它是 HAL 符号，不是 arch 专有 API**。

⇒ **这不是"为 a5 写一套新机制"，而是"把已有 HAL 路径在 a5 上打开"**：预期工作量为
**override `DeviceRunnerBase` 的两个虚函数 + 门控翻转**，而非新建 —— 但**这份工作
尚未做**（见上方状态表）。🟡 **SM0 验证项见主文档 §7.0。**

### 2.1 ⛔ 直接 HAL 调用必须跨 logical→driver device-id gate

\#1600（`9a30e6dd`）已把设备 id 空间契约收敛到
`src/common/platform/include/common/acl_hal_device.h::pto::acl_to_hal_device_id`：

- ACL/rt 入口（`aclrtSetDevice`、`rtMalloc`、`rtMemcpy`、stream API 等）接收
  **logical id**，**不做转换**。
- 直接 `hal*` / `dlsym` 得到的 driver 入口接收 **driver-visible id**，必须
  传 `acl_to_hal_device_id(logical_id)`。`ASCEND_RT_VISIBLE_DEVICES` 会把 ACL 可见卡
  重编为 `0..N-1`，但直接 HAL 仍按 driver-visible 空间索引；不转换会
  命中错卡或返回 driver 错误。

这是 **host-map 设计的强制 gate**，不是 a5 适配细节：现有 a2a3
`DeviceRunner::register_device_memory_to_host` 以及 `init_chip_swimlane` / `init_args_dump` /
`init_pmu` / `init_dep_gen` / `init_scope_stats` 中的五个 `register_cb` 仍把 logical
`device_id` 原样传给 `halHostRegister`，是当前 correctness 缺口；未来 a5 override
也必须从第一天走同一 gate。验证用例要在 `ASCEND_RT_VISIBLE_DEVICES`
下选择 **logical id ≠ driver-visible id** 的配置，identity 映射测不出该缺口。

### 2.2 其它三条能力状态同样应下调

 | 说法 | 更正 | 依据 |
 | ---- | ---- | ---- |
 | "a2a3 SDMA 已启用" | ❌ **是 opt-in**：`Worker(..., enable_sdma: bool = False)`，默认关；`Worker._init_level2` / `_start_hierarchical` 从 `_config.get("enable_sdma", False)` 传入。⚠️ **仅限 a2a3** —— a5 自 #1615（`63f47a11`）起 **default-on** | `python/simpler/worker.py::Worker.__init__` / `_init_level2` / `_start_hierarchical` |
 | "a5 URMA 可用" | 🟡 **代码在，但被编译期 capability gate 挡住**：`option(SIMPLER_ENABLE_PTO_URMA_WORKSPACE ... OFF)`，`comm_hccl.cpp` 里是 `#ifdef` 分支；且与 `SIMPLER_ENABLE_PTO_SDMA_WORKSPACE` **互斥**。⚠️ #1615 后 **SDMA 侧成了默认那一支**，URMA 是须显式选的备选 | `src/a5/platform/onboard/host/CMakeLists.txt` 中两个同名 option 及互斥 guard |
 | "数据 window 仅 VMM" | ❌ **漏了 A3 Fabric V2**：a2a3 的 per-rank symmetric window pool 在 A3 上走 **Fabric handles**（`create_local_fabric_window` / `export_fabric_window` / `ACL_MEM_SHARE_HANDLE_TYPE_FABRIC`），ACL VMM 是无 Fabric 支持的平台的回退 | `src/a2a3/platform/onboard/host/comm_hccl.cpp::create_local_fabric_window` / `export_fabric_window` |

> ### ⚠️ SDMA：两个 open 项方向相反（2026-08-03 记录）
>
> | — | — |
> | - | - |
> | **PR #1615** | `Update: enable A5 SDMA workspace by default` —— 把 a5 的 SDMA workspace **默认打开**。**✅ 已 merged `63f47a11`（2026-08-04）** |
> | **Issue #1425** | `Provisioning the PTO-ISA async-SDMA workspace degrades AICore fault recovery **~1000x（0.3 s → ~306 s）** on a2a3` |
> | Issue #1315 | `Require upgraded a5 environment supporting sdma` |
>
> 两者是**同一个 workspace 机制**：a2a3 上它让 AICore 故障恢复退化三个数量级，
> 而 #1615 在 a5 上默认开启。
>
> ⇒ **已发生的对表**：上表「a2a3 SDMA 是 **opt-in**」一行**仅对 a2a3 仍成立**；
> **a5 自 `63f47a11` 起是 default-on，URMA 变 opt-in 备选**，`docs/comm-domain.md`
> 的 async-DMA workspace 行已同步，`docs/a5-sdma-overlay.md` 已删除。
>
> ⚠️ **「默认开之前至少要证明 a5 没有同样的退化」这条前置未见兑现** ——
> \#1615 的验证只有 `test_runtime_builder.py`（45 passed）＋ pre-commit，**没有
> a5 上的 AICore 故障恢复计时**。#1425 的修法在 **#1664**（`Fixes #1425`，仍 open）：
> fatal 时发布 device-poison、放弃 device-backed handle，a2a3 SDMA fatal 路径等
> `max(10 s, op-execute timeout + 5 s)` 再 reset —— 作者明写这是 CANN 9.0.0 +
> driver 26.0.rc1 的**经验性 workaround，不是可移植的完成栅栏**。
> ⇒ **a5 现在默认走一条在 a2a3 上已知会退化、且其缓解手段尚未合并的路径。**
>
> **附带收益（不在本设计范围，但应记录）**：仓库为"a5 无 host-map"建了多处绕行
> （PMU / L2 swimlane / scope_stats / args-dump 收集器均保留 host 影子 +
> `rtMemcpy DEVICE_TO_HOST`）。a5 host-map 打开后这些可被简化——**单独立项，勿夹带**。

## 3. ③ 一致性：规则不对称、分代不同、sim 测不出

权威参考：**`docs/hardware/cache-coherency.md`**（不在本文重复，只记设计含义）。

要点摘录（✅ 该文）：

- **不对称**：a2a3 上 AICore 写 → AICPU 自动可见（不需 invalidate）；但
  **host DMA / SDMA 写 → AICPU 必须先 `cache_invalidate_range`**。反向 AICore 自己的
  cache 与 GM 不一致，**不 `dcci` 数据到不了 HBM**。
- **分代不同**：上述 invalidate **a5 不需要**（DMA/HBM 一致性更强）。同一模板在两代芯片
  上要生成**不同**的维护指令。
- **一致性 ≠ 顺序**：COND 是 `Device-nGnRE`、slot 是 Normal cacheable，**ARM64 不隐式排序
  两者**，需显式 `rmb()`（`dsb ld`）。
- **真实事故（PR #540 / #204 lineage）**：有人把 host-DMA 的 `cache_invalidate_range`
  照搬到 AICore→AICPU 路径（用错原语），而其内嵌的 `dsb sy` **恰好顺带提供了必需的
  load-load 顺序**——若后人"优化"掉它，顺序保证会**隐式消失**。
- **`dcci` 是整条 cacheline（64B）**：两个核并行写同一 cacheline 的不同元素 →
  各自回写整行 → **最后一个 flush 用旧值覆盖别人**（false sharing / last-writer-wins）。
  ⚠️ **sim 把 cache 建模成 no-op（`SINGLE_CACHE_LINE == 0`），此类错误只在硅上暴露。**

### 3.1 ⚠️ a5 优先带来的头号风险：a5 是"更宽松"的目标

一致性方向上 **a5 比 a2a3 宽松**（a5 的 DMA/HBM 一致性更强 → **不需要**那些
invalidate；a2a3 **需要**）。因此：

> **在 a5 上跑通 ≠ 在 a2a3 上正确。** 少写的 cache maintenance 在 a5 上**没有症状**，
> 到 a2a3 上才表现为**读到陈旧数据的静默损坏**。

这与 §3 的 **sim 陷阱同构**——现在多了一层，严格性排序：

> #### sim（无 cache 模型） ⊂ a5（强一致） ⊂ a2a3（需显式维护）

**因此 a5 优先必须配三条纪律**（否则 a5-first 会积累隐性债）：

1. **维护抽象从第一天就在设计里**——即使 a5 上编译成 no-op，调用点也必须存在，
   不能"a5 不需要就不写、以后再加"（事后回填极易漏点，正是 PR #540 那类事故的土壤）。
2. **a2a3 onboard 验证是模板"正确"的必要条件**，不是可选项。a5 绿灯只算功能通，
   不算正确性通。
3. **CI 门禁**：任一模板必须同时有 **a5 onboard + a2a3 onboard** 覆盖才允许标记 stable。

### 3.2 由一致性推出的两条既有硬规则

✅ **每 64B cacheline 单写者**（`docs/l3-l2-orch-comm.md` §4
`Signal Counters`）：同一 cacheline 内所有
counter 必须同一写者；不同 agent 的 counter 不得共享一条 cacheline。
（该规则由 `dcci` 的整行粒度决定，**两代芯片都适用**，与宽松/严格无关。）

✅ **publish/observe 契约**（`docs/l3-l2-orch-comm.md` §4–§5）：`signal_notify` 是
**发布点**，**匹配成功**的 `signal_test`/`signal_wait` 是**观察点**，
**失败的 test 不建立观察语义**；且明确
**"该契约不能仅靠 host 侧 `atomic_thread_fence` 提供"**。
所有 wait **必须有限超时**（无限等待会把协议死锁藏起来）。

## 4. ④ 超额分配：物理钉死的 HBM

region 走 `aclrtMallocPhysical` + `ReserveMemAddress` + `MapMem`（✅ `comm_hccl.cpp`），
是**物理页**，无换出/驱逐/spill。**HBM 不够 = 硬失败**。

三个后果：

1. **容量必须前置决定**，不能按需增长。
2. **与 KV cache / 权重 / `PTO2_RING_HEAP` / runtime arena / 已有 comm window 抢 HBM**。
   本仓库有过先例：大 ring heap 叠加其它占用把 KV 挤掉，**表现为误导性的"heap 耗尽"**，
   诊断方向被带偏。
3. **常驻会把它变成长期占用**：region 跨 `run()` 存活是常驻的前提（issue **#824**），
   所以 HBM 在**整个常驻期**被钉住，不是单次 run 的临时开销。

## 5. 其它硬约束

- ✅ **CPU 不能当 HCCL rank**：每 rank 必须 `aclrtSetDevice` 绑一张卡；host CPU / AICPU /
  AIV / CCU 是不同 CommEngine。故"host 加入通信域" **只能是同一逻辑域上的一种
  capability view**，不能是"host 变成一个 rank"（见
  [../l3l4/cluster-comm.md](../l3l4/cluster-comm.md) §D.3）。
- ⛔ **`halHostRegister` 不支持 VMM VA** —— ✅ CANN 9.0
  `ascend_hal_base.h` 的 `halHostRegister` 族 API 注释明确：
  **`Not support vmm va, use may result in unexpected behavior.`**
  **⇒ 同一块 backing 不能既被 VMM peer-import、又被 host-register。**
  这与本仓的 ABI 能力矩阵 [../worker-memory-model/p1b-corrected-design.md](../worker-memory-model/p1b-corrected-design.md)
  §5.1 `HOST × VMM_WINDOW = ❌ 拒绝` 一致。
  **设计后果**：必须拆成 `control_buffer`（可 host-map 的非 VMM backing）+
  `payload_buffer`（device VMM），见[主文档 §3.1](../cpu-npu-shared-memory.md) 的
  `ResourceBundle`。
  > **教训**：本约束曾被主文档 §3.1 违反（假设"一块内存两用"），而**两轮对抗式一致性
  > review 都没抓到** —— 因为它们分别在两个文档组**内部**查一致，**从未跨组核对物理能力
  > 声明**。**跨文档的物理能力断言必须显式对表。**
- ⛔ **`halHostRegister` 还受驱动能力位与 host 页大小双重门控** —— 🟡 Issue **#1531**
  （open）：aarch64 host 配 **64 KiB base page** 时，`rtMalloc` 出来的 1280 B DFX
  buffer 做 `halHostRegister(dev_ptr, size, DEV_SVM_MAP_HOST, ...)` 被驱动拒为
  **`DRV_ERROR_NOT_SUPPORT (65534)`**；同机 driver 报 **`support_bar_mem=0`**、
  **`support_bar_huge_mem=1`**，而 **2 MiB / 4 MiB 大页注册在同一台机器上成功**。
  即：**普通页注册不是一个可以无条件依赖的能力**。
  **设计后果**：上一条推出的 `control_buffer`（非 VMM、可 host-map 的那一半）
  **不能按"实际需要几 KB 就开几 KB"分配** —— 必须**按大页尺寸对齐分配**，或在建域时
  **探测能力位**（`support_bar_mem` / `support_bar_huge_mem`）并据此选路；两者都不可用
  时应**显式失败**，而不是让注册在运行期返回 65534。
  ⇒ [W1](work-breakdown.md) 的能力探测项须把这两个位纳入，而不只探
  `halHostRegister` 符号是否 dlsym 得到。
- ✅ **跨进程 device import 每个 importer 拿到不同 device VA**（VMM / Fabric-V2，
  `comm_hccl.cpp` export/import）。
- ✅ **host 解引用 device 内存走 PCIe**：无 cache 一致性、延迟高。
- ✅ **64 rank ABI 硬限**：`src/common/platform_comm/comm_context.h::CommContext`
  的 `windowsIn[]` / `windowsOut[]` 固定以 `COMM_MAX_RANK_NUM=64` 定长——每台
  16 卡时**最多 4 台满配 L3**。
- 🟡 **跨机：不再是"不可达"，是"实现中"**（2026-08-03 更正；2026-08-05 由用户进一步订正）。
  早前本条写作 ❌ **跨机不可达（remote L3 仅 sim）**，**该前提从一开始就是错的** ——
  ⚠️ **不是「曾成立、后被新证据推翻」，而是我把开发进度误记成了硬件能力**：
  **硬件上同一集群的两个 L3 本来就可达**，缺的只是 simpler 侧实现，且那正在进行中。
  下面这个 PR 栈是**那个实现的进展**，不是硬件能力的发现：
  一个三层 PR 栈正在实现跨机 —— **#1456**（L4-brokered mixed local/remote CommDomain，
  TCP 基座）→ **#1623**（mpirun 拉起的 multi-host L3，Global CommDomain 扩到 MPI 节点）
  → **#1624**（MPI 组的命令/健康通道换成 L4-owned 共享内存 mailbox）。
  **#1623 的验证是双机真硬件**：*three consecutive two-host A3 2x2 runs，四 rank
  compute/communication `max_diff == 0`* —— 不是 sim。
  **设计后果**：跨机成员集**不得再被当作恒假**；依赖"跨机不可达"来简化的地方要重新审。
  ⚠️ **这条审查当时没做干净**：`domain-membership.md` §4.1 的跨机行 2026-08-03 已重写，
  但同一份文档 §7 的后端表仍留着 ❌「今天不可达 → 显式失败」，**又活了两天**（2026-08-05 才改）。
  ⇒ **推翻一个前提时必须搜全仓该前提的所有副本**，否则「已更正」只是局部的。
  ⚠️ 仍成立的部分：**A3 Fabric V2 本身只验证过单机**——跨机数据面走的是上述栈的通道，
  不是 Fabric peer import。两者别混。

## 6. 地基结论

> ### 寻址必须 offset-based（base + 偏移），禁止绝对指针

**两条独立理由，任一成立即失效绝对指针**：

1. **HAL 契约允许非 identity VA**（§1，代码自身声明）；
2. **跨进程 device import 每个 importer VA 各不相同**（§5）。

> **注**：此前曾把"a5 根本没有 host map"列为第三条理由——**该理由已作废**（见 §2），
> 但**结论不变**，上面两条独立成立。
