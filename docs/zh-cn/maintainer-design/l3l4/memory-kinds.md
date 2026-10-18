# 四类 worker 内存模型

> **发布快照：2026-10-18。** 设计、实现状态与历史调查须按正文各自基线阅读；不代表当前 main 全量验收。参见[阅读入口与已知更正](../README.md)。
>
> 来源：原 `l3l4-cluster-requirements-analysis.md` §B。
>
> ## ⛔ 本文描述的机制**有一半已被删除** —— 引用前必须先核对（2026-08-14）
>
> ### 核验于 `upstream/main@3f4686c1`，每条 0 命中均附阳性对照
>
> 本文的中心结论是「**kind3 双机制未统一**」—— `orch.alloc`（HeapRing）与
> `create_host_buffer`（POSIX shm ＋ `MAP_HOST` 广播）两条路径、两张注册表。
> **它的两个主语里有一个已经不存在了**：
>
> | 断言 | main 实测 |
> | ---- | --------- |
> | `create_host_buffer` | **全仓 0 命中** —— 由 **#1729 `aa1d7c7d`** 删除（该 commit 内 **59 处删除、0 处新增**）。阳性对照：同文件的 `create_buffer`/`create_host_shared_buffer` 仍有 15 处 ⇒ 搜索有效 |
> | `MAP_HOST` / `_CTRL_MAP_HOST` | `worker.py` **0 命中**（阳性对照：`CTRL_GLOBAL_DOMAIN_PREPARE` 3 命中） |
> | `_host_buf_registry`（两张注册表之一） | **0 命中**；只剩 `_worker_chip_orch_comm_host_buffers`（4 处） |
>
> ⇒ **差距 2（双机制未统一）、差距 3（`MAP_HOST` 单跳）、差距 4（gate ≠ 支持矩阵）的机制载体
> 全部消失**，**§B.4 阶段 2 整节规划的对象不复存在**。
>
> ⚠️ **这不是「与别处重复、改成引用即可」** —— 我 2026-08-14 一度这样判过，**错了**。
> 它是**在描述一个已被删掉的系统**；改成引用会把过时内容**原样搬进权威文档**。
> ⇒ **正确处理是逐条重新核对后再决定留什么**，在那之前本文只能当**历史快照**读。
>
> ⭐ **仍然成立的那一半**：本文规定的 `BufferHandle` / canonical identity
> （`owner_instance_id` ＋ `buffer_id` ＋ `generation`）**已由 P1-A 冻结在 wire 上**
> （`src/common/task_interface/buffer.h`，`CanonicalIdentity` 32 B），
> **权威在 worker 内存模型那一侧**，本文对它的表述应改为引用。

## 四类 worker 内存模型（对齐诉求 B / MEM_TABLE）

> 本节当前结论按 `main@b69f4537` 复核；函数/类型名是定位锚点，保留的旧行号仅用于
> 追溯原调查快照。
>
> **⚠️ 部分现状已被 #1397（merged `483d97e3`）改变，读本节须知**：① `child_memory`
> host-rewrite 的 **guard ① 已落地**（`_rewrite_blob_host_addrs` 跳过 `child_memory`，
> `worker.py`），下文"数值区间猜、会误改 device VA"是 #1397 前状态；② `create_host_buffer`
> 现有 preflight **接受 sub-only L3**（chip 或 SUB direct child 皆可），但这不代表
> mixed L4 端到端可用：若同一 Worker 另有 L3/remote NEXT_LEVEL endpoint，后续 MAP_HOST
> 广播仍失败；下文只写"要求已 fork chip children"是旧约束；③ 本地子层已 **eager**
>（`init()` 即 fork，无首次-run 懒 fork），下文
> 依赖"首次 run() 才拉起"的表述均属 #1397 前。**guard ②：✅ 已交付（#1430 `c8c7b735`）**——
> typed `(worker_id,ptr)` provenance（解析后唯一 target 放行；拒零/多/wrong-worker/stale；严格
> ABA 留 P1），typed BufferHandle 仍是 P1（见 worker-memory-model.md）。

本节把用户给定的 kind1–kind4 内存分类逐条落到当前代码里可校验的实体上，核对"向下级递归下发"是否已实现、所谓"L(N)↔L(N+1) 注册表"在代码里到底是什么数据结构，并结合 #1390 / #1256 / #861 / #1334（**#1190 已 CLOSED-未合并，勿依赖**）给出统一落地路径。

一句话结论（**v2 下调**）：**四类内存目前是"零散机制映射"，不是统一的 typed ABI。** kind1/3/4 各有代码对应物并能向**直接下级**递归下发，但边界明确：kind4 仅支持 **L3→本地 L2**（L4 `malloc` 被显式拒绝），且 `child_memory` 只是 **1 个 bool**（无 owner / generation / address-space 校验）；kind3 双机制未统一、仅单跳。`create_host_buffer` 的 preflight 要求**至少一个已 fork 的 chip 或 SUB direct child**（sub-only L3 可用），但若 Worker 同时带 L3/remote NEXT_LEVEL endpoint，广播仍会触达不处理 MAP/UNMAP_HOST 的 endpoint 而失败，因此不能宣称 mixed L4 可用；kind1 **不校验来源是否真是上级 kind3/kind4**；kind2 完全无标记（纯约定）。**目标应是 owner-side `BufferHandle` + 可跨 edge 的 `BufferHandleDescriptor`/`BufferRef`：canonical identity 仅为 `owner_instance_id + buffer_id + generation`，descriptor 另含 address-space / size / access / backend descriptor；`owner_worker_path_id` 只作进程内诊断，且没有 `visibility` 字段。** 同时不应把 `orch.alloc` 直接改写成 `create_host_buffer`，应先拆 `SlotArena` 与 `SharedBufferArena`（见 C.3）。

### B.1 当前代码状态

#### 总览对照表

 | kind | 用户表述 | 当前主 API | 底层机制（file:line） | 递归下发到下级？ | 状态 |
 | ---- | -------- | ---------- | --------------------- | ---------------- | ---- |
 | kind1 | run 的 args；来自上级 kind3/kind4；无上级即 kind2 | `TaskArgs.add_tensor(t, tag)` + `orch.submit_next_level(h, args, cfg, worker=N)` | 见 §kind1 | 机制在（提交即下发） | **机制在，但不校验来源**（不验证 arg 真来自上级 kind3/kind4） |
 | kind2 | 本 worker local；torch tensor；不可下发 | 无专用 API（任意 CPU torch tensor） | 见 §kind2 | N/A（定义上不下发） | **未实现**（无标记/校验，纯约定） |
 | kind3 | 与下级 shared；orch.alloc/create_host_buffer；注册进 L(N)↔L(N+1) 表 | `orch.alloc(shape,dtype)` 或 `worker.create_host_buffer(nbytes)` | 见 §kind3 | 机制在，**仅单跳**（父→直接子） | **部分实现**（双机制未统一、不支持多跳；`create_host_buffer` gate 需 chip/SUB，且任何 L3/remote NEXT_LEVEL endpoint 会令后续 MAP_HOST 广播失败；跨机未落地） |
 | kind4 | 下级私有 device；worker.malloc(worker_id)+copy_to；child_memory=1 | `orch.malloc/copy_to(worker_id,...)` + `Tensor.make(...,child_memory=True)` | 见 §kind4 | 仅 L3→本地 L2 | **部分实现**：仅 **L3→本地 L2**（L4 malloc 被拒 `worker.py:4166`）；`child_memory` 仅 1 bool（无 owner/gen/addr-space）；远端需 sidecar（⚠️ 此前写「远端 sim-only」，见 §末的作废说明） |

#### §kind1 — run args 的下发（INPUT/OUTPUT 标签流）— 已实现

上级把 buffer 交给下级 run 的 args 走 `TaskArgs`：

- `TaskArgs.add_tensor(tensor, tag)`（`python/simpler/task_interface.py:624`），`tag` 是 `TensorArgType ∈ {INPUT, OUTPUT, OUTPUT_EXISTING, INOUT, NO_DEP}`（枚举绑定 `python/bindings/task_interface.cpp:808`）。
- `orch.submit_next_level(chip_handle, args, cfg, worker=N)`（`python/simpler/orchestrator.py:157`）把这份 args 送给下一级 worker N。

**关键：这里的"方向"是两套正交概念**，是理解 arg 流最容易踩坑的地方：

1. **`TensorArgType`（TaskArgs 上的 per-arg 标签）** — 只驱动 orchestrator 的依赖推导（RAW/WAR）与远端访问权限。`_required_remote_access_for_tag`（`python/simpler/task_interface.py:683`）把 `INPUT→read`、`OUTPUT/OUTPUT_EXISTING→write`、`INOUT/NO_DEP→readwrite`。
2. **`ArgDirection`（Callable 签名 signature）** — 驱动 H2D/D2H 拷贝。`stage_device_args`（`src/a2a3/runtime/tensormap_and_ringbuffer/host/runtime_maker.cpp:572`）读 `signature[i]==OUT` → 纯输出、跳过 H2D staging；`==IN` → 只读、跳过 D2H copy-back。二者在 child_memory 例子里同时出现：TaskArgs 用 `TensorArgType.OUTPUT_EXISTING`，ChipCallable 签名用 `[IN,IN,OUT]`。

无上级时（scenetest / L2 直接跑），kind1 退化成 kind2 —— 就是把一个 CPU torch tensor 经 `make_tensor_arg`（`simpler_setup/torch_interop.py:71`）直接喂给 run。

#### §kind2 — 本 worker local（torch tensor，不可下发）— 未实现

**没有专门 API，也没有任何"这是本地、拒绝下发"的标记或校验。** 语义完全靠约定：凡是不进 `submit_next_level` 的 buffer 就留在本级。

- `make_tensor_arg(tensor)`（`simpler_setup/torch_interop.py:71`）只做防呆：拒绝**非 CPU** 张量（device 指针必须显式 `Tensor.make(...,child_memory=True)`）、拒绝非 contiguous —— 但**不阻止**把一个纯本地 CPU 张量提交下去。
- 唯一相关的运行期校验 `_stage_host_buffers_for_chip_submit`（`python/simpler/worker.py:4427`）是防 fork 后未注册 host 张量读到 stale/unmapped 内存，且对 fork 前 `share_memory_()` 继承来的张量"不校验直接转发"—— 因此它**不是** kind2 的守卫。

#### §kind3 — 与下级 shared — 部分实现

> **[修正] 三个概念必须拆开，勿混为一张"L(N)↔L(N+1) 注册表"**：
>
> 1. **映射可见性**：HeapRing 的 slab 是 fork **之前**建立的 `mmap(MAP_SHARED)`（`ring.h:22`），**本地后代按同一 VA 递归继承**（**不是单跳**）；但其 slot **状态**只活在父进程堆，never crossed into children。
> 2. **依赖元数据**：C++ `tensormap_` 只是**本层 Orchestrator 的 DAG producer/依赖表**（`orchestrator.cpp` `infer_deps`），**子 worker 不读取它**（`worker.cpp:98` 是同进程注入）——它 **不是** 跨层注册表。
> 3. **MAP_HOST 注册**：`create_host_buffer` 的 `_CTRL_MAP_HOST` 控制路径，**这才是真正"仅单跳"** 的那条（父只广播给直接 forked 子）。
>
> B.3 的缺口与迁移路线必须建立在这三者区分之上，否则前提就错了。下面两套并行机制各自维护独立结构：

##### (a) `orch.alloc(shape, dtype)` — 运行时 heap（托管生命周期）

- Python `python/simpler/orchestrator.py:440` → C++ `Orchestrator::alloc`（`src/common/hierarchical/orchestrator.cpp:70`）。从运行时 HeapRing allocator 切一块（`MAP_SHARED` mmap，**fork 前建立，本地后代递归继承**），并登记进 C++ `tensormap_`（`TensorKey::local_host(ptr)`，`orchestrator.cpp:106`）—— 这是**本层 DAG 的 producer/依赖表**（供 `infer_deps`），**不跨进程传给子 worker**。
- 返回后 Python 侧再登记一张表 `_worker_chip_orch_comm_host_buffers: dict[base→nbytes]`（`worker.py:1811`；注册于 `_register_worker_chip_orch_comm_host_buffer` `worker.py:3662`；校验于 `_validate_worker_chip_orch_comm_host_buffer` `worker.py:3678`），用于 L3-L2 orch 通信 payload 缓冲的合法性检查。

##### (b) `worker.create_host_buffer(nbytes)` — post-fork born-shared host buffer（显式生命周期）

- `python/simpler/worker.py:4222`。用 POSIX `SharedMemory` 建一块"出生即共享"的 host 内存，登记进 `_host_buf_registry: dict[data_ptr→_HostBufEntry]`（`worker.py:1817`；entry 结构 `worker.py:387`），再 lock-free 快照到 `_host_buf_snapshot`。
- **广播尝试**：`_CTRL_MAP_HOST`(=14) 由 `_broadcast_host_control` 广播给**每个** NEXT_LEVEL **和** SUB endpoint（给 SUB 也发是 #1390 的修复）；“被广播到”不等于“该 endpoint 支持”。
- **只有 chip/SUB loop 接收** `_handle_ctrl_map_host`，把 shm 映射进自己地址空间，记 `(parent_lo, parent_hi, child_base)` 到 `host_buf_ranges`；每个 task 派发前 `_rewrite_blob_host_addrs` 把 args blob 里落在该区间的父 VA 改写成子进程自己的映射基址 → **成为下级 kind1(shared)**。L4→本地 L3 的 `_child_worker_loop` 与 remote endpoint 不处理 MAP/UNMAP_HOST，会返回 control error；因此该机制不能递归透传到孙层。

##### 真正的跨层 shared 注册（回答用户"注册表实体是什么"）= MAP_HOST 路径**：父侧 `_host_buf_registry`（create_host_buffer）+ `_worker_chip_orch_comm_host_buffers`（orch.alloc payload 校验）两张 Python dict；子侧 `host_buf_table`（token→mmap）+ `host_buf_ranges`（区间→child_base）。**C++ `tensormap_` 不在此列**——它是本层 DAG 依赖表、不跨进程；**HeapRing 的 MAP_SHARED 可见性是 fork 递归继承，与 MAP_HOST 注册是两回事

> 说明：orch.alloc 的 HeapRing 与 create_host_buffer 都是 **host-local（同机、依赖 fork/shm）**，onboard 可用。设备侧对等共享（VMM 窗口，见 §kind4 与 B.2 #1334）也仅同机。**跨机（remote L3）的 kind3 共享目前无对应实现** —— ⚠️ 理由**不再是**「remote 数据面 sim-only」（该断言 2026-08-05 已作废，见 §末），而是 `host_tcp` profile 下**没有 peer 内存直读**：#1688 的例子 README 自己划界 *"It does not read peer-machine memory"*。remote 侧的 buffer 自 #1688 起可由 `create_host_buffer` 背书（同机 host shm），但那是**远端本机**的共享，不是跨机共享。

#### §kind4 — 下级私有 device（child_memory=1）— 已实现（本地）

四类里最贴合用户表述、链路最完整的一条：

- `orch.malloc(worker_id, size)`（`orchestrator.py:424`）→ C++ `Orchestrator::malloc`（`src/common/hierarchical/orchestrator.cpp:34`）→ `worker->control_malloc` → mailbox `_CTRL_MALLOC`(=0) → 子进程 loop `cw.malloc(size)`（`worker.py:1406`）→ `ChipWorker::malloc`（nanobind `python/bindings/task_interface.cpp:1448`）在子进程自己的 device context 内 rtMalloc。`copy_to` 同理走 `_CTRL_COPY_TO`(=2)。
- `Tensor.make(dev_ptr, shape, dtype, child_memory=True)`（`python/bindings/task_interface.cpp:607`）置 `Tensor.child_memory=1`。运行时 `stage_device_args` / `RetainedTempBump::begin`（`runtime_maker.cpp:537`）见 `is_child_memory()` → **pass-through**：不 auto-malloc、不 H2D、不记进 `tensor_leases_`/`tensor_pairs_` → task teardown **不释放**，buffer 存活到 `worker.close()` 时由 `DeviceRunner::finalize` 回收。
- **递归 → 下级自有 device**：这块 child_memory 张量作为 run arg 进下级 kernel，直接引用下级 device buffer，即下级的 kind1(自有 device)。
- **L2 直通版**：level==2 时 `worker.malloc/copy_to/copy_from/free`（`worker.py:4179`）直接打到 `_chip_worker`，不经 orch。
- **L4 不支持 malloc（关键边界）**：`_check_chip_worker_id`（`worker.py:4166`）在 L4+ 直接拒绝——L4 的 `_chip_shms` 为空、next-level 是 L3 python worker（不 service `CTRL_MALLOC/FREE/COPY_*`）。**即 kind4 目前只能 L3→本地 L2 一跳，L4→L3→L2 的 device 私有内存下发不成立。**
- **`child_memory` 只有一个 bool**（`tensor.h:107`，`is_child_memory()`）：不带 owner / generation / address-space，运行时靠数值区间猜测 host/device 归属——这是"零散机制"而非 typed handle 的根因之一，也是 device VA 可能被误判为某 host buffer 区间的来源。
- **远端 L3（差距）**：child_memory 张量若无 remote sidecar 会被 `RemoteL3Endpoint::build_task_payload`（`remote_endpoint.cpp:610`）拒绝（reject 在 `:632`）—— 远端路径要求显式 sidecar 描述符，与本地 mailbox 直达路径不统一。

端到端范例：`examples/workers/l3/child_memory/main.py:161`（kind4 完整链路）、`examples/workers/l2/worker_malloc/main.py:99`（L2 直通）。回归测试：`tests/st/a2a3/tensormap_and_ringbuffer/test_l3_post_fork_host_buffer.py:116`（kind3 零拷贝 st，2026-08-08 随 #1729 遗留项 ⑤ 由 `test_l3_host_buffer_registration.py` 改名 —— `register_host_buffer` 早已删除，旧名只是历史残留）。⚠️ `tests/ut/py/test_worker/test_host_buffer_registration.py`（kind3 注册/校验 ut，含 #1390 回归）**在本仓已不存在**（只留一个孤立的 `.pyc`）——它测的内容现在归哪个文件未查证，此处先如实记为缺口，不要当作仍存在引用。

### B.2 相关 Issue / PR

 | PR/Issue | 状态 | 落到哪一类 | 把诉求推进到哪 |
 | -------- | ---- | ---------- | -------------- |
 | **#1256** | merged (a5895d8d) | kind3 (host shared) | L3 post-fork **zero-copy** host buffers：落地 `create_host_buffer` + `_host_buf_registry` + `_CTRL_MAP_HOST`。父子共享同一物理页、per-task blob 的 host 指针在 child 解码前重写，纯 Python 无 C++ 改动。是 kind3 host 侧的**权威落地基线**。 |
 | **#1390** | merged (470a84df) | kind3 对 SUB 的可见性 | 把 `MAP_HOST/UNMAP_HOST` 广播扩展到 **L3 SubWorker**，并在 SubWorker 任务解码前改写父 host 地址（修 subworker 不可见）。补齐 kind3 到 SUB 池。 |
 | **#1190** | **CLOSED，未合并** | kind3 | L3 post-fork host-buffer registration 的早期方案，**已被 #1256/#1390 取代**。梳理 kind3 应读后两者，而非 #1190。 |
 | **#1334** | merged | kind3/kind4 **device 侧** 跨进程 | 把跨进程 device window 从 exbus-gated `aclrtIpcMemImportByKey` 换成 **ACL VMM shareable-handle**（`aclrtMemExportToShareableHandle/ImportFromShareableHandle`，`comm_hccl.cpp:464-520/624-638`），修 PCIe 箱 507899。使设备内存能零拷贝跨进程对等访问 —— 但 `ACL_MEM_HANDLE_TYPE_NONE` 天然**单机**（`comm_hccl.cpp:20-23` 已自注 host-local），对跨机 kind3/kind4 不可直接用。 |
 | **#861** | **CLOSED，未合并**（历史实现/设计） | kind3 底层原语 | `HostDeviceMappedRegion`：64B header + signal slot + 裸 data 区，A2/A3 host-register、A5 `-ENOTSUP` 桩。只作为需求与设计审计材料；当前代码基线应读 merged #1015/#1305/#1277。 |

补充：kind3 相关的 host-buffer + runtime-arena 已合并栈还包括 #1234/#1193/#1322/#1245（arena 复用/prewarm/去 local-ready-buffer），但这些服务冷启动与调度，不直接改内存分类。**#1198（TRB 临时变量 buffer）已 merged 后又被 `fd5f16f8` 回滚，当前代码树中不存在，勿再把它当作在用基线**（与 C.3 一致）。**kind4 的 child_memory 机制无单独 PR 号**，已内置于 `tensormap_and_ringbuffer` 运行时（`runtime_maker.cpp` 的 `is_child_memory` pass-through + `examples/workers/l3/child_memory`）。

### B.3 差距

1. **kind2 无落地（未实现）**：不存在"本地、不可下发"的标记或校验。`make_tensor_arg` 只拒绝 device 张量/非 contiguous，不阻止把纯本地 CPU 张量提交给下级；语义纯靠约定。
2. **kind3 双机制未统一**：`orch.alloc`（运行时 HeapRing + C++ `tensormap_`）与 `create_host_buffer`（POSIX shm + MAP_HOST 广播）是两条独立代码路径、两张不同 Python 注册表（`_worker_chip_orch_comm_host_buffers` vs `_host_buf_registry`），语义都属 kind3 却互不相通，用户要自己知道何时用哪个。
3. **仅 MAP_HOST 路径单跳（HeapRing 不是）**：`_CTRL_MAP_HOST` 只从父广播到直接 forked 子，子侧 `_handle_ctrl_map_host` 不再向孙 worker 转播；**且 `_child_worker_loop`（L4→本地 L3）与 remote endpoint 目前不处理 MAP/UNMAP_HOST**。因此纯 next-level L4 在 preflight 即被拒；mixed L4 若因另有 SUB 而通过 preflight，会在广播给全部 NEXT_LEVEL 时失败，仍映射不到 L2 grandchild。（注意：`orch.alloc` 的 HeapRing MAP_SHARED 是 fork **递归**继承，深层级*本地*后代可见；单跳缺口只在 MAP_HOST 控制路径。）
4. **kind3 前置 gate 不等于支持矩阵**：`create_host_buffer` 要求 level≥3 且至少有一个已 fork 的 chip 或 SUB direct child；L2 单机不可用。端到端还要求没有不支持 MAP/UNMAP_HOST 的 L3/remote NEXT_LEVEL endpoint。`orch.alloc` 依赖 fork 继承 MAP_SHARED，与 remote L3（跨进程/跨机、非 fork）天然不兼容。
5. **kind4 远端不透明（设计已存在未落地）**：本地 `orch.malloc` 走 mailbox CTRL 直达子设备；但 remote L3 上 child_memory 张量必须显式携带 remote sidecar 描述符（`remote_endpoint.cpp:631`），否则抛错 —— "本地 vs 远端下级"的 kind4 下发路径不统一，上层需感知对端是否远端。⚠️ **此处此前写「远端数据面整体 sim-only（`remote_l3_worker` 只接受 `transport=="sim"`）」—— 已作废（2026-08-05，#1688 `e78d8437`）**：那个门现在是 `transport == "host_tcp"`，而且**旧的 `"sim"` 从来不是模拟器** —— `b535fa21` 上的 session 已是真 TCP socket（`AF_INET`/`bind`/`listen`），是名字撒了谎而我采信了它。**当前边界（`68d137fdf`）**：A3 Fabric 共享窗口已有跨机数据面；child_memory 的统一下发与授权仍按 RA-13/DS-3 验收，不能由 control profile 推导全部 peer-memory 能力。
6. **跨机 kind3/kind4 硬约束**：VMM shareable-handle 用 `ACL_MEM_HANDLE_TYPE_NONE`（OS 原生 handle）只能同机；跨机需 fabric handle（CANN 9.0.0+ 且 UB/HCCS fabric 域），或改走 HCCL/RDMA channel windows backend —— 这是环境级约束，非纯软件缺口。
7. **kind1 方向二元性无文档**：`TensorArgType`（依赖/远端权限）与 `ArgDirection`（H2D/D2H 拷贝）两套"方向"并存且作用不同，docs 未点破，容易误配（如签名给 IN 但实际要写回）。
8. **kind4 两个 guard（均已交付）**：① **guard ①：✅ #1397** —— `_rewrite_blob_host_addrs` 改写前排除 `child_memory`，不再因 device VA 与某 host range 数值碰撞误写 device 指针；② **guard ②：✅ #1430 `c8c7b735`** —— typed `(worker_id,ptr)` provenance，`submit_next_level` 下发 `child_memory` 参数时按 provenance 校验（`worker=-1` 解析出**唯一** eligible target 放行，拒零/多/wrong-worker/stale；严格 ABA 留 P1）。（历史：曾是 correctness blocker，现已闭。）
9. **kind1 不校验来源**：`add_tensor` 不验证一个 arg 是否真来自上级 kind3/kind4，纯靠约定。

### B.4 建议 / 落地路径（分阶段）

#### 阶段 1（低成本、纯文档 + 校验，先消除认知坑）

- 在 docs 里显式记录 `TensorArgType`（依赖/权限）与 `ArgDirection`（拷贝）的二元性及其组合语义，配 child_memory 例子的 `OUTPUT_EXISTING` + `[IN,IN,OUT]` 说明（对齐差距 7）。
- 正式落地 kind2：在 Tensor 上或 Worker 侧引入显式 `local-only` 标记，并在 `submit_next_level` / `add_tensor` 处校验拒绝，把"不可下发"从约定变成可检出错误（对齐差距 1）。这是纯 Python、改动面小的守卫。
- **✅ kind4 两个 guard 已交付（对齐差距 8，历史遗留条目）**：① `_rewrite_blob_host_addrs` 改写前排除 `child_memory` 张量（**guard ①，#1397**）；② typed `(worker_id,ptr)` provenance——拒绝**歧义/错-worker** child_memory 下发（**guard ②，#1430 `c8c7b735`**；`worker=-1` 解析出唯一 eligible target 放行，仅拒零/多/wrong-worker/stale，严格 ABA 留 P1）。完整 typed BufferHandle 仍是 P1。

#### 阶段 2（统一 kind3 接口，与诉求 C.3 强相关）

- 统一 kind3 接口：**不能直接把 `create_host_buffer` 改名复用**——`Worker._create_host_buffer_locked` 的 preflight 只检查 `_chip_shms` **或** `_sub_shms`；纯 `_next_level_shms` L4 会被拒，mixed L4 则可能过 gate 后在 MAP_HOST 广播阶段失败。接口还明确**不能与 run/create/free 并发**，而 `orch.alloc` 正是在 run/orchestration 中调用。正确路线：**新增 run 前的 Worker 级 `SharedBufferArena`**（对外 `create_buffer` / `alloc_shared_tensor`），再逐步把 `orch.alloc` 迁上去（对齐差距 2、4）。
- 补齐 kind3 多跳：**不仅是 rebroadcast**——L4 child loop 当前根本不处理 MAP/UNMAP_HOST，需先让 L4 child 支持这两个 control、再向自己的下级转播，才支撑 L4→L3→L2 深层级共享（对齐差距 3）。

#### 阶段 3（统一本地/远端下发路径，依赖 remote 数据面成熟）

- 统一 kind4 本地/远端：让 child_memory 张量在本地（mailbox）与远端（sidecar）下发对上层透明，呼应诉求 D.3"不感知对端是 L3 还是 L2"（对齐差距 5）。此步按 RA-13/DS-3 验收 consumer/wire 与授权；现有 A3 共享窗口可作基座，#866/#711 仅为历史方案。
- 跨机 kind3/kind4 device 内存：评估 fabric handle（CANN 9.0.0+/UB fabric 域）或 HCCL/RDMA channel windows backend；可吸收 #861 的 `HostDeviceMappedRegion` 概念，但实现须从当前 main 重新落地并补 capability contract（对齐差距 6）。

**边界提示**：以上阶段 2 的"移除/统一 heap"必须与诉求 C.3 协同 —— HeapRing 还兼任 task-slot 池/单调 task-id/drain-reset，**不能整体删**，只能把堆容量置 0 并把 `orch.alloc` 与 OUTPUT 空指针自动分配重写到显式 shm 之上（详见 C.3 章）。

---
