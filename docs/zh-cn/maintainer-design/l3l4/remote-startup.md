# Remote L3 启动与接口

> **发布快照：2026-10-19。** 设计、实现状态与历史调查须按正文各自基线阅读；不代表当前 main 全量验收。参见[阅读入口与已知更正](../README.md)。
>
> 来源：原 `l3l4-cluster-requirements-analysis.md` §A + §C.2。

## A. L4 管理本地 / remote L3 的初始化（对齐 A.1）

> **⚠️ 历史诊断（基线 main@8fba2321+#1396）：本章 (a-1)/(a-2)/A.3 中"本地 L3
> 首次 run() 才懒 fork""本地 lazy vs 远端 eager 时机不对称"等表述描述的是
> \#1397 合并前的状态。#1397（merged `483d97e3`）后本地路径也 eager（`init()`
> 即拉起整棵子树，无 lazy trigger），该不对称已消除。**
> ⚠️ **本框此前紧接着写「remote L3 仍 sim-only、HCCL 发现仍未实现——这两点仍是当前事实」**
> —— 前半已于 2026-08-05 作废（见下框），**后半（HCCL 发现未实现）截至 `e78d8437` 仍成立**。
>
>
> **当前主路径（`68d137fdf`，2026-10-13）**：L4 以 `add_mpirun_worker_group` 启动 MPI L3 group；
> rank 0 与父进程共享 mailbox，各 rank 通过 MPI 派发任务、交换建域描述符。
> 不要求预起 Simpler daemon。A3 Global CommDomain 用 ACL/Fabric V2 建共享窗口，peer-TLOAD 路径已实现并受持续双机 PR CI 看护；实际测试 SHA 与逐项结果见通信方案 D.4。
> 原生 ACL Runtime + PTO 为通信路径；SHMEM 仅为实现参考，不依赖其库，见 [通信方案 D.4](cluster-comm.md#d4-remote-l3-通信mpirun--共享窗口)。
> 下面保留的 daemon 走读只描述 `RemoteWorkerSpec/host_tcp` 路径，不能代表全部 L4 启动方式。

### A.1 当前代码状态

> ⚠️ **本小节(a-1/a-2)的代码走读是 #1397/#1420 之前的旧状态,已过时,勿据此实现。**
> 现状:**本地也 eager**(#1397——`init()` 返回即整树 READY,**无懒 fork、无"首个 run() 才
> `_start_hierarchical`"**);**remote session 在最后一次 local fork 之后 open+activate**
> (#1420 `_activate_remote_sessions`,**非 pre-fork open**);**runner 只调 eager `init()`**
> (init() 内部即 `_init_hierarchical→_start_hierarchical`,**不双调**)。权威现状见
> **C.1 §(c) 差距 0** 与 **§D.4 更新(#1420)**。下面保留旧走读仅作历史/行号参考。

#### (a-1) 本地 L3：`add_worker` + fork/shm — 已实现（onboard）

L4 管理本地 L3 走 fork/共享内存，是三阶段：

1. **注册**：`w4 = Worker(level=4)`；`l3_id = w4.add_worker(Worker(level=3, device_ids=[...], num_sub_workers=M))`。`add_worker`（`python/simpler/worker.py:3219`）要求 `level>=4`、与 `device_ids` 互斥、须在 `init()` 前调用，返回稳定的 NEXT_LEVEL worker id。本地 child 与 remote child 共用同一 worker-id 空间，后加者不会重编号已返回的 id。
2. **`w4.init()` → `_init_hierarchical`**（`worker.py:3302`）：为每个 next-level child 分配一块 `MAILBOX_SIZE` 的 `SharedMemory` 邮箱（`3339-3344`），并在 fork **之前**构造 C++ `_Worker`（`3346-3353`），使 HeapRing 的 `MAP_SHARED` mmap 被所有子进程按同一虚拟地址继承。此阶段**不 fork**。
3. **首个 `run()` → `_start_hierarchical`**（`worker.py:3385`）：对每个 `inner_worker` `os.fork()`，子进程内先 `inner_worker.init()` 再进入 `_child_worker_loop`（`3478-3495`）。C++ 侧 `dw.add_next_level_worker_at(worker_id, mailbox_addr)`（`3511-3513`，绑定 `worker_bind.h:385`）注册端点，每个 child 由一个 `WorkerThread` + `LocalMailboxEndpoint`（`worker_manager.h:247`）驱动，轮询邮箱 shm 的 `_TASK_READY`/`_CONTROL_REQUEST` 状态机（`_child_worker_loop`, `worker.py:1616-1714`）。

**关键时机不变量**：inner L3 自己的 chip/sub 子进程是**懒 fork** 的——要等 inner_worker 的首个 `run()` 才 `_start_hierarchical`（注释 `worker.py:3475-3477`）。即本地 L4→L3→L2 中，L2 芯片上电发生在"首个 L4→L3 任务"，而非 `L4.init()` 或 L3-child 创建时。

> 备注：#583 里的旧函数名 `_l3_worker_loop` 在 main 上**已不存在**，现名 `_child_worker_loop`（`worker.py:1616`）。梳理时勿再按旧名 grep。

#### (a-2) Remote L3：daemon + session runner + socket 帧 — 部分实现（**只有一个 transport profile**）

远端 L3 走完全不同的一条路：**daemon（`simpler-remote-worker`）+ 每会话 runner（`simpler-remote-l3-session`）+ 版本化 socket 帧协议**。

1. **注册**：`spec = RemoteWorkerSpec(endpoint="host:port", platform=..., runtime=..., device_ids=..., num_sub_workers=..., transport="sim", session_listen_host=None, ...)`（`worker.py:338-369`）；`l3_id = w4.add_remote_worker(spec)`（`worker.py:1843`，要求 `level>=4`）。
2. **"同 IP 不同端口" 的表达**：天然由 `endpoint="host:port"` 承载——**每个 remote L3 = 一个预先起好的 daemon 监听地址**。注意：**本地 L3 走 fork/shm，没有监听端口**（见 a-1），"同 IP 不同端口" 只对**同一机器上并行起的多个 remote daemon** 成立（daemon-A 在 `IP:PORT_A`、daemon-B 在同 IP `:PORT_B`），地址由用户**硬编码**在 `spec` 里。
3. **`w4.init()` 打开会话**（`_init_hierarchical`, `worker.py:3355-3381`）：逐 remote spec 生成 `session_id`，`_open_remote_session`（`worker.py:1946`）用 `socket.create_connection((daemon_host, daemon_port))` 连 daemon，发 JSON manifest（`_build_remote_manifest`, `worker.py:1922-1944`，含 session_id/worker_id/platform/runtime/device_ids/num_sub_workers/transport/dispatcher 条目/listen_host）。
4. **daemon 起 runner**（`remote_l3_worker.py:177` `serve` → `_start_session` `:117`）：`_validate_manifest`（**硬性要求 `transport=="host_tcp"`**，`remote_l3_worker.py:67`；⚠️ #1688 `e78d8437` 前该常量叫 `"sim"`，但**旧实现已是真 TCP socket** —— 见文首框）→ 写临时 manifest → `subprocess.Popen([python, -m, simpler.remote_l3_session, --manifest, --ready-fd])` 起**新进程**的 session runner（fork-safe：daemon 从不构造 inner Worker、从不在起线程后 fork）→ 等 runner 经 ready-fd 回传 `{ok, command_host, command_port, health_host, health_port}`。
5. **runner 预启动内层 L3 全树**（`remote_l3_session.py:897` `run_session`）：`inner_worker = Worker(level=3)` → `inner_worker.init()` + `inner_worker._start_hierarchical()` **立即 fork 内层 L3 的 chip/sub 全树**（`remote_l3_session.py:913-915`）——这一步在 HELLO READY **之前**完成，是"预启动屏障"。随后 bind command/health 两个 socket、accept、进 `_run_command_loop`，首帧发 `HELLO ready_state=READY`（`remote_l3_session.py:526`）。
6. **父侧注册端点**：`self._worker.add_remote_l3_socket(worker_id, session_id, transport, cmd_host, cmd_port, health_host, health_port, timeout_s)`（`worker.py:3367`，绑定 `worker_bind.h:402`）→ C++ 构造 `RemoteL3SocketTransport` + `RemoteL3Endpoint`，`expect_hello_ready` 校验 HELLO/session/worker/comm_profile/READY（`remote_endpoint.cpp:558-575`）成功后端点才进入可调度集。

> **本地 vs 远端的时机不对称（与 C.1 相关）**：远端 runner **eager** 预启动内层 L2 全树（`remote_l3_session.py:915`），而本地 L4→L3 却是**懒** fork（inner 首个 run() 才 fork chip，`worker.py:3475-3477`）。

#### (a-3) 启动、建域控制与数据传输

- **启动/发现**：mpirun 按部署配置启动 L3，rank 0 落在 L4 父进程所在主机，manifest 通过 MPI 广播。
  显式主机配置不等于通用自动发现，但不要求用户先部署 Simpler daemon。
- **建域控制**：MPI descriptor/status exchange 与现有 PREPARE_EXPORT/IMPORT/COMMIT/ABORT；
  session epoch、lease 与故障回收仍按 DS-1/DS-2/DS-9 验收。
- **设备数据**：A3 Fabric V2 共享窗口由 ACL 分配/导出/导入，kernel 通过 TLOAD/MTE 访问。
  沿用原生 ACL/PTO 路径；不新增 SHMEM 库或强制 HCCL/HCOMM 建域前置。

HCCL 初始化 device communicator，不启动或发现 L3；本地文件式 rootInfo 不限制 MPI + Fabric 全局域。
选择跨机 collective adapter 时再定义 opaque rootInfo 分发，不能将其当作既有 A3 路径缺口。

### A.2 相关 Issue / PR

**已合并（本地递归 + 远端 sim 骨架）**：

- **#583 merged** — L4+ 递归 Worker 组合（`add_worker` + `_child_worker_loop`，旧名 `_l3_worker_loop`）+ `DistWorker::run`。落地"L4 fork 并递归驱动**本地** L3(→L2) 层级"，是 A.1 本地路径的地基。局限：仅本地、同机、同进程树 fork。
- **#605 merged** — L4 错误传播：child 异常经 `MAILBOX_OFF_ERROR_MSG`（mailbox 尾 256B）上抛到 `Worker.run` 调用者。
- **#1006–#1011 merged（remote L3 拆栈）** — 把"远端 L3 worker"逐层搭到 sim 打通：#1006 文档契约（`docs/remote-l3-worker-design.md`，HCOMM 硬件 profile 与 Remote CommDomain 明确列 future）；#1007 remote import callable descriptor；#1008 wire codec（`remote_wire.h/.cpp`，仅连续张量）；#1009 endpoint eligibility 调度；#1010 C++ `RemoteL3Endpoint` facade（`remote_endpoint.cpp`）；#1011 Python session runtime（`remote_l3_session.py`/`remote_l3_worker.py`，全 socket-backed sim）。**推进程度**：A.1 从零到"契约 + 四层骨架已合并，但只在 sim 打通"。

**2026-07-17 当时的在途原型（均保留作审计，不是当前 OPEN 基线）**：

- **#866 CLOSED-未合并**（历史 diff +12350/-332，43 files，已是 C++ 实现而非 docs-only）— 曾尝试用 `WorkerEndpoint` 统一 local/remote run/control/shutdown，并预留 HCOMM adapter；可用于追溯取舍，当前实现基线是 merged #1006–#1011。
- **#711 CLOSED-未合并**（历史 diff +8485/-3，38 files，含 C/C++ 与 RXE 数据面）— 曾是另一条 L4→L3 dispatch 路线；不要从 closed head 派生第二套 remote stack。
- **#803 CLOSED-未合并** — L3/L2 host-device 通信历史原型（D.3 host↔device 轴，非 remote-L3 dispatch 轴）。
- **#1381 CLOSED，superseded by #1397** — 早期最小 eager patch；目标已由 #1397（merged `483d97e3`）完整覆盖。
- **#824 open** — 跨 `Worker.run` 的持久通信域，与 remote CommDomain future 相关。

> **结论**：#866/#711 与 merged remote-L3 拆栈（#1006–#1011）同轴，现已关闭；
> \#803/#861 属另一条 D.3 host↔device 轴，也已关闭但需求未消失。区分需求轴仍有价值，
> 不能据 closed PR 的存在声称任一缺口已实现。
>
> ## ⚠️ PR 状态总校正（2026-07-30，逐条 `gh pr view` 核实）
>
> ### 本文（含上方各汇总表的 `open #…` 单元格）对下列 PR 的状态标注全部过期。以本表为准
>
> | PR | 本文旧标注 | **实际状态** |
> | -- | ---------- | ------------ |
> | **#711**（Python distributed L4→L3 + RXE） | open | **CLOSED，未合并** |
> | **#755** | open | **CLOSED，未合并** |
> | **#803**（L3/L2 host-device 通信，D.3 轴） | open | **CLOSED，未合并** |
> | **#861**（四类内存模型 / a5 `-ENOTSUP` 桩） | open | **CLOSED，未合并** |
> | **#866**（remote L3 worker C++ 实现） | open | **CLOSED，未合并** |
> | **#1090**（`TASK_RUNNING` + register lane，§E 上下级并行） | open | **CLOSED，未合并** |
> | **#1003**（post-READY liveness） | open | **MERGED**（2026-07-25） |
>
> ⇒ 上方"疑似被取代、建议核对是否关闭"的讨论**已经有结论**：#866/#711 确实被
> \#1006–#1011 栈取代并关闭；**但 #803/#861/#755 这条 D.3 轴也一并关掉了**，
> 而 D.3 的需求**并未随之消失** —— 它现在的承接者是本目录的
> [cpu-npu-shared-memory.md](../cpu-npu-shared-memory.md) 设计，**尚无对应 open PR**。
>
> **当前与本文直接相关的 open PR（此前完全未进入本文）**：
>
> | PR | 内容 | 落在哪一节 |
> | -- | ---- | ---------- |
> | **#1456** | `Add: support mixed local and remote L4 CommDomains` —— L4 收集 rank-ordered L2 export descriptor、事务式 prepare/import/commit/release、A3 Fabric + sim 后端、**无 mpirun** | **D.5「L4 为多 L3 建域」的直接候选实现**；它仍是 open PR，在合入前 `main` 仍是 reserved/stub，不能把候选当成已交付 |
> | **#1599** | `Add: the BufferHandle/Tensor wire ABI, its codec, and create_buffer` —— 历史上从 #1453/P1-B 拆出的 PR-1；按当前 roadmap 归 **P1-A identity/wire + minimal allocation-lifetime slice** | **B「四类内存模型」的统一 ABI 起点**（该节现标"非统一 ABI"）；仍是 open candidate，非 main 能力 |
> | **#1453** | P1-B 全量（DRAFT，#1599 是其拆分产物） | 同上 |

### A.3 后续验收

1. **原生 backend 能力**：A3 HCCS 数据面已在 #1456；其他互连适配归 DS-5，
   按硬件、CANN/PTO 版本、同步/completion 和回收证据验收。
2. **启动与发现**：DS-6 承接 mpirun 部署、rank 放置与跨-L3 接口；HCCL 不承担 L3 服务发现。
3. **建域事务**：DS-2/DS-9 在已有 MPI exchange 与阶段协议上补生产合同，
   HCCL rootInfo 带外分发仅适用于选择该 collective adapter 的路径。
4. **窗口与权限**：A3 Fabric V2 已支持跨机；remote direct Attachment、owner-delegated copy、
   显式 Transfer 的能力与身份仍分别验证，不能用 HCCL collective 透明替代 peer-window 访问。
5. **启动顺序**：本地 eager init 与 fork-before-thread 是已有约束；禁止恢复 lazy fork、
   `init()+_start_hierarchical()` 双调用或 pre-fork 打开 remote session。
6. **参数/任务边界**：张量布局、OUTPUT 分配、remote attachment 与 callable 身份的验收
   分别归 RA-13/DS-3/EX-4；不能用共享窗口已通替代 wire consumer 的完整验收。

### A.4 落地路径

复用 mpirun/mailbox 与 A3 共享窗口基座，按 DS-1/DS-2/DS-9 补会话和成员事务的失败闭环；
DS-4 负责远端生命周期与回收，DS-5 负责其他原生通信 backend，DS-10 联验异构组合。
这些是工作项的完整退出条件；不要求先部署独立 daemon，也不将 backend 实作串行锁在所有前项后面。

---

## C.2 L2/L3/L4 worker 接口 + orch 接口对照与重构

### C.2(a) 当前代码状态 —— 一个工厂、两套机制

simpler 用同一个 Python `Worker(level=N, **config)` 工厂统一三层入口（`worker.py:1733`），但底下是**两套完全不同的机制**，分界线正是 L2（`docs/hierarchical-level-runtime.md:44`：L0–L2 在设备侧 AICPU/AICore，L3–L6 在 host/集群）：

- **L2 = 薄封装的 C++ `ChipWorker`**：`run()` 跑的是**编译进 `ChipCallable` 的 C++ orch 二进制**、在片上 AICPU 上**自由调度** AIC/AIV（设备侧 API 如 `rt_submit_aiv_task`，见 `examples/workers/l2/vector_add/kernels/orchestration/vector_add_orch.cpp:35`）。
- **L3+ = C++ `Worker` 复合体**（TensorMap + Ring + Scope + Orchestrator + Scheduler + WorkerManager）：`run()` 跑的是 **host 进程里的 Python `orch_fn(orch, args, cfg)`**，经 host 侧 `Orchestrator` 建 DAG，由 host C++ Scheduler 线程分发到 fork 出的子 worker（`examples/workers/l3/multi_chip_dispatch/main.py:160`）。
- **关键推论**：L3 的每个 chip 子 worker 本身就是一个 L2，其 ChipCallable 的 orch 仍跑在该 chip 的 AICPU 上——设备侧 orch API 在两层共享，不共享的是 host 侧那一层。

**生命周期对照**：

 | 维度 | L2 (ChipWorker) | L3 (Worker 复合体) | L4+ (递归 Worker) |
 | ---- | --------------- | ------------------ | ----------------- |
 | 构造 | `Worker(level=2, platform, runtime, device_id)` | `Worker(level=3, device_ids=[…], num_sub_workers=M)` | `Worker(level=4, num_sub_workers=M)`（禁 device_ids，`worker.py:3306-3307`） |
 | 子 worker 注册 | 无 | 无（init 里按 device_ids 自动建） | `add_worker(child)`（`worker.py:3219`）/ `add_remote_worker(spec)`（`worker.py:1843`），须 init 前 |
 | `init()` | `_init_level2`（`worker.py:3279`）：`ChipWorker.init`，**不 fork 不起调度线程** | `_init_hierarchical`（`worker.py:3302`）：**#1397 后 `init()` 内 eager fork 整树**（返回即 full-tree READY，原"推迟到首个 run"的 lazy fork 已删；#1381 CLOSED/superseded，见 C.1） | 同 L3，子是 Worker；C++ 侧 `add_next_level_worker` / `add_remote_l3_socket`（`worker.h:77/82`） |
 | C++ `Worker::init()` | 不适用 | `worker.cpp:94`：`orchestrator_.init` → `manager_.start` → `scheduler_.start` | 同 L3 |
 | `close()` | `ChipWorker.finalize` | `worker.py:4611`：释放 CommDomain → `Worker::close`（`worker.cpp:125`）→ 拆子进程/unlink shm | 同 L3 + 拆 remote session |

**Orch 函数接口对照**（L3+ `orch.*` 见 `python/simpler/orchestrator.py`；L2 无 host 侧 orchestrator 对象）：

 | L3+ `orch.*` | 签名 / 位置 | L2 对应物 |
 | ------------ | ----------- | --------- |
 | `submit_next_level(handle, args, cfg, worker=-1)` | `orchestrator.py:157` / C++ `orchestrator.h:102` | 设备侧 `rt_submit_aiv_task` / `rt_submit_aic_task` |
 | `submit_next_level_group(handle, args_list, cfg, workers=[])` | `orchestrator.py:209` | 无直接对应 |
 | `submit_sub / submit_sub_group` | `orchestrator.py:288/304` | 无（L2 无 Python sub 概念） |
 | `alloc(shape, dtype)` | `orchestrator.py:440` / `orchestrator.cpp:70-144`（HeapRing） | 设备侧 runtime 自己的 ring |
 | `malloc/free/copy_to/copy_from(worker_id, …)` | `orchestrator.py:424` → mailbox CTRL_MALLOC → chip 子 | `worker.malloc` 直连 `ChipWorker.malloc`（rtMalloc） |
 | `allocate_domain / release_domain` | `orchestrator.py:321/362` | `ChipWorker.comm_alloc_domain_windows`（`chip_worker.h:118`），被 L3 驱动 |
 | `create_worker_chip_region / create_worker_chip_queue` | `orchestrator.py:366/372` | L2 侧 `ChipWorker` 实现 |
 | `scope() / scope_begin / scope_end` | `orchestrator.py:404-422` | 设备侧 `pto2_scope_end` 语义 |

L2 Worker facade 只暴露 `malloc/free/copy_to/copy_from(size, worker_id=0)`（`worker.py:4179`）、`register`、`run`；**不暴露** `get_orchestrator`/`allocate_domain`/`submit_*`（L3+ 专属）。

**部分跨层统一的基座**（可复用，但 callable 身份未真正统一）：TaskArgs / Tensor / TensorArgType / CallConfig / DataType 参数与依赖 tag 模型可依赖；SHA-256 digest 身份机制两层同源（L2 `task_interface.py:1088` 与 L3+ `worker.py:2455`）。**但 CallableHandle 尚未真正跨层统一**：L2 `run` 消费已注册 ChipCallable handle，而 **L3 当前层 orchestration 消费的是 raw Python callable**；L3 `register` 一个 Python callable 得到的是**下级 `LOCAL_PYTHON` target**（`worker.py:687/2859`），**不是当前层 orchestrator 的 handle**。真正统一需新增 current-orchestrator namespace + 生命周期 + 错误语义。

### C.2(b) 相关 Issue/PR

- **#1345 merged `ee40a7f0`**（最终 scope = *barrier-free per-thread scheduler init*，a2a3+a5；早期 draft 的 "Decouple Orchestrator from handshaking" 描述已过时）：orch 解耦思路仅作参考，**不作 C.2 callable 统一基线**。
- **#1379 OPEN** —— Pipelined orchestration（per-token L3↔L2 重叠 + 并发 `Worker.run()`，含 Perfetto 证据）：host-orch 流水方向，与 D.2 强相关。
- **#984 OPEN** —— Streaming orchestrator design space：orch 单线程 per-task submit 热路径量化基线（去掉 TensorMap 把 lookup+dep+insert 从 23.4% 砍到 4.7%），是评估"tmr TensorMap/heap 能否精简"的**量化依据**。
- **#583 / #605 merged** —— L4+ 递归 Worker 组合与错误传播，是 `add_worker` 递归组合的实现底座。
- **#817 merged** —— orch-driven 动态 CommDomain（Option A），确立 `allocate_domain` 在 orch 层的现状；D.5（L4 跨 L3 域）是其未覆盖延伸。
- **#866 / #711 均 CLOSED-未合并** —— 两条 historical remote-L3 路线；当前 L4↔L3 接口基线是 merged #1006–#1011，接口对称性缺口仍在。
- **#1390 / #1256 merged** —— `create_host_buffer`（kind3）实现来源。

以上没有一个是"L2/L3/L4 接口统一重构"的单一 PR —— C.2 是一次**接口消歧与收敛工程**，需围绕已统一基座展开。

### C.2(c) 差距（C.2 必须消歧的清单）

1. **`run()` 语义未消歧**：L2（跑编译二进制、AICPU 自由调度）与 L3（跑 Python orch 函数、host Scheduler 分发）共用 `run` 名，调用者无法从签名看出执行基底不同——**最大的概念混淆**。
2. **L2 没有 host 侧 Orchestrator 对象**：`submit/scope/alloc/malloc(worker_id)` 在 L2 无对应；要真正统一，要么给 L2 造一个 1-worker 退化 orchestrator，要么正式承认边界、只统一 handle/args/memory 三件套（参考记忆中的 host_build_graph→host-orch 迁移方向）。
3. **`malloc/copy_*` 双入口**：Worker facade 靠 `worker_id=0` 默认值抹平签名（`worker.py:4179-4216`），但 L2=直连 C ABI、L3=orch+mailbox IPC；L3 上"真正"入口是 `orch.malloc`，`worker.malloc` 只是转发——两入口并存易混。
4. **`register()` 随 level 重载**：L2 只收 ChipCallable，L3 加 Python，L4 加 RemoteCallable（`worker.py:2462`）。
5. **NEXT_LEVEL placement 已收敛**：#1436 删除 `worker=-1`/affinity 自由选择，single/group 均要求 exact target；#1565 为 group FIFO head 保留目标 worker。C.2 只需保留该接口契约，不再把旧 `pick_idle` 路径列为当前缺口。
6. **子 worker 注册路径三种写法不统一**：L2 无 / L3 靠 device_ids 自动建 / L4 靠 `add_worker`。**#1397 的 eager-init 已统一启动*时机*（init() 内 eager fork），但三种*注册写法*仍未收敛**——这才是 C.2 剩下的活。
7. **L4 缺 across-L3 通信域接口**：comm/CommDomain 只在 L3 orch 层，底层原语全在 L2 ChipWorker；L4 跨多 L3 建域（D.5）无对称接口。

### C.2(d) 建议 / 落地路径

1. **冻结可复用基座 + 补 callable 统一缺口**：TaskArgs / Tensor / CallConfig 参数模型保持不动；但 **CallableHandle 尚未真正跨层统一**（L3 当前层用 raw Python callable，register→下级 `LOCAL_PYTHON` target），真正统一需新增 current-orchestrator namespace / 生命周期 / 错误语义，不能当已完成基座直接复用。
2. **消歧 `run()`**：短期给 L3+ 的 DAG 入口起独立名（如 `run_orch` / `run_dag`）并保留 `run` 兼容；长期评估让 L2 也暴露一个退化的 1-worker Orchestrator，使 L2/L3 都用 `orch.submit + Worker.run(orch_fn)`，把"编译二进制 orch"与"Python orch"的分歧收敛到 orchestrator 内部而非 `run` 签名。
3. **统一内存面**：让 L2 的 `malloc/copy_*` 也经过一个 `worker_id=0` 的（进程内、无 mailbox 的）orchestrator 实现，从而 `worker.malloc` 在每层都只是 `orch.malloc(worker_id)` 的薄转发，消除双入口混淆。
4. **补 L4 域对称性（服务 D.5）**：把 L3 现有 `allocate_domain` 抽象成"对下级 worker 集合建域"的通用操作（L3 下级是 chip，L4 下级是 L3），使通信域接口在层间对称——这也是 D.3"不感知对端是 L3 还是 L2"的接口前提。
5. **统一子 worker 注册路径**：把 L3 的 device_ids-自动建与 L4 的 `add_worker` 收敛为同一 `add_worker/add_child` 语义。（"创建即拉起子 worker"的**时机**已由 **#1397** 统一为 init()-time eager，#1381 CLOSED/superseded；本项剩下的是**注册写法**收敛。）
6. **重构前先对齐设计讨论**：C.2/C.3 与 #1345/#1379/#984 同域，动手前对齐这些 orch 重构讨论，并 grep `docs/investigations/` 是否已有否决记录。

---
