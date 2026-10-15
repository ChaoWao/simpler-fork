# 调度：submit 指定 worker + 去自由调度 + heap

> **发布快照：2026-10-15。** 设计、实现状态与历史调查须按正文各自基线阅读；不代表当前 main 全量验收。参见[阅读入口与已知更正](../README.md)。
>
> 来源：原 `l3l4-cluster-requirements-analysis.md` §C.3。

## tmr runtime 简化：submit 指定 worker + 用户指定调度；heap buffer 梳理

> **首要澄清（层次错位）**：C.3 说的"L3 worker 内的 tmr runtime"，实际要改的是 **`src/common/hierarchical/`（L3 主机侧编排器，一个模仿 L2 tmr 自由调度+环堆设计的独立 host 实现）**，**不是**设备侧 tmr（`PTO2_RING_HEAP` / `DeviceArena` 跑在每张卡的 AICPU 上，L3 编排器不碰）。"L2 那种自由调度"应定位为**对照参考**，而非待改代码。方案需先钉住这层边界。
>
> ## ⚠️ (a)–(c) 是 **#1436 之前的现状调查记录（2026-07-17）**，**不是当前代码**
>
> 下方 (a)/(b)/(c) 描述的 `worker_id=-1`、`affinities`、`eligible_worker_ids` 自由选择、
> `pick_idle`、`Scheduler::dispatch_ready()` 的 **Pass 1 / Pass 2 双通道**（`scheduler.cpp:341-425`）
> **全部已被删除**。保留原文是为记录"改造前长什么样、为什么要改"；**任何当前状态的判断
> 请以本 banner 与 (d) 为准**。
>
> ## 当前调度模型（main@`b69f4537`）
>
> | 维度 | 现在是什么 | 落地 PR |
> | ---- | ---------- | ------- |
> | NEXT_LEVEL 目标 | **submit 时必须点名 exact worker id**；拒 bool/float/str/负数/重复；group 每成员一个**互不相同**的 id | #1436 `fee284e8` |
> | 队列拓扑 | `FIFO[target_worker_id]` **每 worker 一条** + **一条共享 group FIFO**；SUB 一条共享 FIFO | #1436 |
> | `eligible_worker_ids` | **降级为纯约束校验**（target 必须落在集合内），**不再驱动任何选择** | #1436 |
> | 自动选择 | NEXT_LEVEL **无**；仅 SUB 保留 `pick_idle_sub_excluding`，且 SUB **拒收** worker-selection 元数据 | #1436 |
> | group 派发 | 队头 group **保留其目标 worker**，singles 跳过被保留者，直到整套目标空闲才 all-or-nothing 发射 | **#1565 `6bd6c3b9`**（`Fixes #1551`） |
> | 跨 run admission | `begin_run()` 在 callback 前取得 negotiated pipeline lease；只有 **whole-run FIFO head** 可执行，下一 callback 不再等前一 run launch acceptance | **#1541 `9a2a5f71`**（取代 #1467 gate） |
> | 深度-2 资源 | generation-safe slot 已接入 whole-run FIFO：depth=2 可让 N 执行、N+1 构图/准备；仍无第二个并发 device run 或跨 run publication overlap | **#1540 + #1541** |
> | native run lifecycle | `prepare -> launch -> poll/wait -> finalize` 已可推进；blocking API 是其组合，但 single-frame child 仍同步等完整 run | **#1574 `87dce627`**（B3a） |
> | L2 sync-start | owner scheduler 本地容量够时**局部提前启动**，不够才回退 global stop-the-world drain | **#1558**（`fixes #1548`；a2a3+a5 tmr 走通，hbg 备好机器但未启用） |
>
> `worker_id=-1` / `affinities` / Pass 2 在当前代码中**不存在**。

### C.3(a) 当前代码状态 —— ⚠️ **pre-#1436 快照,见上方 banner**

#### ① "submit 时指定 worker" —— 已实现，且是 L3 主流写法

- C++ `submit_next_level(callable, args, config, int32_t worker_id=-1, eligible_worker_ids={}, remote_sidecar={})`（`orchestrator.h:102-105`）：`worker_id` = stable NEXT_LEVEL worker id 亲和度，`-1` 不约束。`submit_next_level_group(worker_ids={})`（`orchestrator.h:109`）N args → N worker 选择。`submit_impl` 把标量 `worker_id` 包成 `affinities` 存进 `TaskSlotState`（`orchestrator.cpp:150-164, 229, 251`）。
- Python facade `submit_next_level(handle, args, config=None, *, worker=-1)`（`orchestrator.py:157-207`）。
- **`submit_sub` / `submit_sub_group`（`orchestrator.h:116/119`, `orchestrator.py:288-314`）没有 worker 参数**——SUB（Python leaf）一律走空闲池自由调度。
- **真实用法**：examples/tests 里 160 处 `submit_next_level` 有 **67 处（41.9%）显式带 `worker=`**；**其余 93 处未分类** —— 迁移面大小须先分类这 93 处才能判断（不能直接称"主流/小"）。

**② "自由调度 vs 用户指定" 的分派路径**（同一函数 `Scheduler::dispatch_ready()` → lambda `drain_one`，`scheduler.cpp:341-425`）：

- **Pass 1（钉 worker）**（`scheduler.cpp:362-374`）：读 `s.get_affinity(i)`，`>=0` 就 `get_worker_by_id`（NEXT_LEVEL stable id）/ `get_worker_by_index`（SUB index）拿指定 worker，检查 `idle()`；拿不到就整个 slot push 回队列 head-of-line 阻塞。
- **Pass 2（自由调度）**（`scheduler.cpp:376-387`）：亲和度 `-1` 的成员调 `pick_idle(worker_type, workers, eligible_workers_for(i))` 从空闲池挑（受 `eligible_worker_ids` 候选集约束）。

**结论**：C.3 想要的"只钉 worker、不自由调度"大部分**已到位**，缺口只是把 Pass 2 自由调度路径设为可选/移除。

**③ heap buffer —— 必须按归属层拆开 5~6 个不同概念**：

| 概念 | 归属层 | 代码位置 | 生命周期 |
| ---- | ------ | -------- | -------- |
| **`orch.alloc(shape,dtype)` 的 HeapRing** | **L3 主机侧编排器** | `ring.{h,cpp}`；`orchestrator.cpp:70-144` | 4 个 `MAX_RING_DEPTH`（`types.h:117`=4）独立 `mmap(MAP_SHARED\|ANON)` 环，默认每环 1GiB（`ring.h:70`）。按 scope 深度选环，绑合成 COMPLETED slot，FIFO 随 `last_alive` 回收，`Worker.run` 结束 `drain()`→`reset_to_empty()` 整体归零 |
| **OUTPUT 空指针自动分配** | **L3 主机侧编排器** | `reserve_outputs_and_slot`（`orchestrator.cpp:485-524`） | 同一 HeapRing；submit 时把 `data==0` 的 OUTPUT 切片（1024B 对齐），随产出任务 slot CONSUMED 回收 |
| **`Orchestrator::malloc/free/copy_to/copy_from`** | **下级(L2/芯片)设备内存**（=kind4） | `orchestrator.h:92-95` → `WorkerThread::control_malloc` | 在指定 chip worker 上分配 device 内存，与 HeapRing 无关 |
| **`create_host_buffer` / POSIX shm** | **L3 主机侧（born-shared）** | `Worker.create_host_buffer` / `Worker._create_host_buffer_locked`；`HostBuffer` | 显式 `SharedMemory(create=True)`，广播 `_CTRL_MAP_HOST`，显式 `free_host_buffer`。**这就是 C.3 说的 malloc+bind 用户接口模型**；preflight 需 chip/SUB，端到端只覆盖所有被广播 endpoint 都支持 MAP/UNMAP_HOST 的拓扑。当前 L3/remote NEXT_LEVEL 不支持，mixed L4 可过 gate 后失败 |
| **`PTO2_RING_HEAP` / `ring_heap`** | **L2 设备侧 (NPU AICPU)** | `pto_ring_buffer.h:464` | 每 chip 在 NPU 上跑的 tmr GM 堆，per-ring。**与 L3 主机 HeapRing 只是同构不同物** |
| **DeviceArena / `PTO2RuntimeArenaLayout` / 预建 arena** | **L2 设备侧** | `pto_runtime2.h:103-249` | 连续 device 分配，主机侧构建、init 时 prewarm、跨 run 复用（#1193/#1234/#1322） |
| **TRB temp-var buffer（#1198）** | — | **已 revert（`fd5f16f8`）** | 当前代码树不存在 |

**结论**：C.3 说的"orch 里的 heap buffer"**只指 L3 主机侧 `Ring` 的 HeapRing（前两行）**。

**④ `create_buffer` / `alloc_shared_tensor` 是否存在**：

- `create_host_buffer(nbytes) -> HostBuffer`：**存在**（`worker.py:4222-4322`），显式 malloc+bind 现成接口。
- `orch.alloc(shape, dtype) -> Tensor`：**存在**（`orchestrator.py:440-456`），runtime 托管（HeapRing 支撑）。
- `alloc_shared_tensor`：**不存在**（全仓库 0 命中）。
- `create_buffer`：**不存在**（全仓库 0 命中）。

**结论**：C.3 想要的两个名字是新名字；语义上 `create_host_buffer`（显式）+ `orch.alloc`（托管）已覆盖两种模型，缺的是**统一/改名并让 OUTPUT-auto-alloc 走显式路径**。

### C.3(b) 相关 Issue/PR

- **#1009 merged** —— remote endpoint eligibility scheduling：remote L3 靠 `eligible_worker_ids`+`pick_idle` 做候选负载均衡，是"取消自由调度"的**直接约束**（删掉会退化为逐个手点）。
- **#1341 CLOSED**（曾标 High）—— AICPU scheduler 单线程囤 running+pending 饿死 sibling：**注意这是设备侧 AICPU scheduler（L2 tmr），只能类比、不是 L3 host scheduler 的直接证据**。它佐证"自由调度负载不均有弊端"的一般方向，但 L3 host 侧的直接实证应看 `scheduler.cpp:389` 队首阻塞与 **Issue #1105**（group eligible endpoint 永久 requeue）。
- **#1105 CLOSED（2026-07-28，我关的）** —— Group eligible endpoint reuse can leave scheduler requeueing forever：曾是 **L3 host scheduler HOL 的直接证据**。#1436 删掉 NEXT_LEVEL 自动选择（共享队列 + idle-pool 回退 + "排除同组已选 endpoint" 规则全部移除），该形状现在在 submit 期被 `unique_targets` 拒（`"duplicate NEXT_LEVEL worker id N"`），`eligible_worker_ids` 降级为纯约束校验、不再驱动选择 ⇒ 机制不复存在。
- **#1137 open** —— 把 wiring 换成 polling task readiness（a2a3 L2 tmr，中位 −16.9%）：**理解"L2 自由调度到底做了什么"的最新权威实现**，是 C.3 要在 L3 简化掉那套的 L2 侧**对照基线**（非同层、不作 C.3 基线）。
- **#1245 merged（`b31253f6`）** —— 删 tmr `PTO2LocalReadyBuffer` local-first dispatch：设备侧"简化调度、去掉一层缓冲"先例，砍自由调度可参照其手法（非同层）。
- **#1337 merged（`d9047f89`）** —— host_build_graph ring/heap overflow 干净报错而非 hang：**保留任何堆都要补这类超时/溢出 backstop**。
- **#1198 REVERTED（`fd5f16f8`）** —— TRB 临时变量 buffer 复用已回滚，当前不存在。
- **#1390 merged / #1256 merged** —— expose host buffers to L3 subworkers / post-fork zero-copy host buffers：`create_host_buffer` 那套 born-shared 主机缓冲的**实现来源**。
- **#1388 open** —— TensorMap READER 追踪自动推导 WAR 依赖：**方向与 C.3 相反**（L2 侧继续增强 auto-dep），存在方向张力需注意。
- **#824 open** —— 跨 `Worker.run` 持久化通信域，与"把 comm/heap 给用户显式管理、生命周期可控"的诉求相关。
- **#984 / #545 open** —— orch/scheduler perf 量化伞状追踪，是评估"移除 heap/TensorMap"收益的数据基线。

### C.3(c) 差距 / 可行性评估

**(a) "只保留 submit 钉 worker、去掉自由调度"**：

- NEXT_LEVEL 侧**可行**——把 `worker=-1` 判为非法（或删 `scheduler.cpp:376-387` 的 Pass 2 `pick_idle`）即可强制钉 worker；67/160（41.9%）已钉，**其余 93 处未分类，迁移面待评估**。
- **但撞 SUB 池**：`submit_sub` 无 worker 参数、SUB 池本质靠 `pick_idle` 自由分配（`scheduler.cpp:380` + `ready_sub_queue_`）。→ 统一为：**NEXT_LEVEL（含 remote）都可强制显式 placement**；remote 的 `eligible_worker_ids`（#1009）保留**只作合法性/内存归属校验，不做自由负载均衡**；Pass 2 `pick_idle` 暂只留给 SUB 池。

**(b) "移除 heap，简化为 create_buffer/alloc_shared_tensor，submit 时 malloc+bind"**：

- `Ring` **不止是堆**：同时是 **task-slot 池 + 单调 task-id + slot-state deque + drain/reset** 载体（`ring.h:148-204`）。**不能整体删**，只能关堆容量：`Ring::init(heap_bytes=0)` 已支持"禁堆但仍发 slot"（`ring.h:106-108`，`alloc(bytes>0)` 抛异常）。
- 关堆会**同时废掉** `orch.alloc`（`orchestrator.cpp:96`）和 **OUTPUT 空指针自动分配**（`reserve_outputs_and_slot`，`orchestrator.cpp:485-524`）——这两个是现有 L3-L2 orch-comm、message-queue 测试直接依赖（`test_worker_chip_message_queue.py`、`worker_chip_orch_comm_stream`）。
- **OUTPUT 语义迁移**：当前 `data==0` 的 OUTPUT 由编排器隐式切堆；若走"submit 时 malloc+bind"，需把 tag 语义改成"用户必须显式提供 buffer"，影响 `infer_deps` / `reserve_outputs_and_slot` / 所有 OUTPUT 用例。**auto-OUTPUT 依赖推断是 DAG 正确性核心，不宜直接删**。
- 保留任何堆需补溢出/超时 backstop（Ring 已有 `ALLOC_TIMEOUT_MS`=10s 背压）；`create_host_buffer` 路径**无背压**（直接 shm 分配，OOM 即失败），需明确 OOM 语义。

**(c) 文档过时**：`docs/orchestrator.md` §2 step6 / `docs/scheduler.md` §4 仍描述"wiring queue / enqueue_wiring / Phase 0 wiring"，但代码已把 fanin wiring 内联进 `orchestrator.cpp` submit_impl Step 5（`scheduler.h` 无 `wiring_queue_`）；重构时应同一 commit 修文档。

### C.3(d) 建议 / 落地路径

1. **先钉边界**：方案文档明确 C.3 要改的是 `src/common/hierarchical/`（L3 主机编排器），不是设备侧 tmr（`PTO2_RING_HEAP` / `DeviceArena`）；"L2 那种自由调度"（#1137/#1341）定位为对照参考。
2. **~~NEXT_LEVEL 强制钉 worker前先修 HOL~~ ✅ 已完成**：#1436 已交付 exact-target per-worker FIFO 并删除 NEXT_LEVEL 自由选择；#1565 已为 group FIFO head 保留目标 worker，#1105/#1551 均 CLOSED。原 `q->push(slot); break`、Pass 1/Pass 2 与漂移行号只属于 pre-#1436 快照，不是当前待办。
3. **堆简化用"关容量不删结构"**：保留 `Ring` 作 task-slot/task-id/drain 载体，走 `Ring::init(heap_bytes=0)`；把 `orch.alloc` 与 OUTPUT 空指针自动分配**重写到 `create_host_buffer` 那套显式 shm 之上**（用户 submit 前显式 `create_buffer` 再 bind，生命周期由 handle 管理而非 HeapRing FIFO），统一暴露为 `create_buffer` / `alloc_shared_tensor`（新名字复用现有实现）。
4. **拆开评估 auto-OUTPUT 与 orch.alloc**：`orch.alloc` 可先并存一个显式 `create_buffer` 用户 API，度量后再决定是否废弃 HeapRing；**auto-OUTPUT 依赖推断不宜直接删**，需配迁移期报错（"OUTPUT 必须显式绑定 buffer"）并同步改 `infer_deps` / `reserve_outputs_and_slot` 与全部 OUTPUT 用例。
5. **保留背压**：全走 `create_host_buffer` 直接 shm 分配需明确 OOM 语义（无背压，失败即抛）；参考设备侧 #1337 补溢出/超时 backstop。
6. **顺手修文档**：`docs/orchestrator.md` §2 step6 / `docs/scheduler.md` §4 的"wiring queue / Phase 0 / enqueue_wiring"已与代码不符，重构同一 commit 更新。
7. **动手前查否决记录**：grep `docs/investigations/` 是否已有 heap 简化的否决记录，再决定是否推进（参照 discipline.md §4）。

---

### 本节状态小结

| 子诉求 | 状态 | 落地抓手 |
| ------ | ---- | -------- |
| C.1 L3 创建即拉起 L2 | **✅ 已落地（#1397 merged `483d97e3`）**：eager 递归 init + full-tree READY + 删 lazy trigger + 本地有界回滚；取代 #1381。**加固第一批 #1398 merged `0ffb88f8`**：单一状态机 + **fail-fast close** + eligibility + L2 两并发 init 第二个立即报错（done-P0.2.md（本地参考：`../P0.2/done.md`）） | 剩余加固转 P0.2.md（本地参考：`../P0.2/done-followup.md`） follow-up；**#1003 已 merged**，补 TASK 阶段 child-exit liveness；sim-remote 细分 P0.3/P2/P3 |
| C.2 L2/L3/L4 接口 + orch 接口收敛 | **部分实现**（参数基座已统一；**callable 身份未真正统一**——L3 用 raw Python callable、register→下级 LOCAL_PYTHON；`run`/orch/malloc 同名异义未消歧，L4 域接口不对称） | 冻结参数基座 + 补 callable 统一 → 消歧 `run` → 统一内存面 → 补 L4 across-L3 域 |
| C.3 submit 指定 worker | **已实现**：NEXT_LEVEL single/group 均要求 exact target | #1436；group reservation 由 #1565 补齐 |
| C.3 跨 run admission | **已实现**：negotiated whole-run FIFO；下一 callback 由 pipeline lease 开放，非前一 run launch acceptance | #1540 + **#1541**；仍无第二个并发 device run |
| C.3 移除 heap | **未实现**，且 2026-09-08 起**拆成两项** —— `EX-3` **零容量运行验收**（`feature_roadmap.md` §2.5 第 5 步，Ring 已支持 `heap_bytes=0` 的 slot-only allocation ⇒ **不等 arena**，且这一条是 P1 退出条件）· `EX-5` **删除 heap-coupled scope reclaim**（第 6 步，等 `RA-7`）。⛔ **本行原写 `create_host_buffer`，该名全仓 0 命中**（现名 `create_buffer`，`worker.py:10877`） | 归属真源 [`work-items.md`](../work-items.md)；auto-OUTPUT 重写到显式 shm |

---
