# Run-pipeline 重构设计 v7 — 分两轨：program 自然排队 / kernel 接入

设计归档快照：状态与阅读顺序见[索引](README.md)。设计约束、历史合同和当前实现须分别阅读。

> **历史设计 / 评审快照**：保留文中的原始基线、结论与验证记录，不代表当前待办或支持范围。当前进度统一见 [路线与阶段表](run-pipeline-track-a.md) / [HTML 看板](run-pipeline-track-a.html)。

基线 `748c39fbe`。归属 issue #1853。

前六版把"去掉固定槽位"和一整套新 API、tiling 缓存、ACLGraph 绑成一个交付。两轮
review 加一份源码核查之后，最有价值的一条结论是**分轨**：

> 先明确哪些工作服务于 **program 自然排队**，哪些服务于**新 kernel 接入**。

§14 记录历次撤回。**核心判据、寿命分类、构建读写合同**这三处在 v7 被替换，不是微调。

## 1. 为什么必须分轨

`simpler_kernel_mode_supported()` 返回 **0**，其后的 kernel 入口是共享参数校验的
**占位实现**（`src/common/platform/onboard/host/c_api_shared.cpp:1253`）。所以
"program 与 kernel 共用设备提交"不能读作"复用一个成熟的 `kernel_launch`" ——
**本分支能工作的只有 program 路径**，kernel 接入要建立在它之上。依赖方向是反的。

| 项目 | A 轨 · program 自然排队 | B 轨 · kernel 接入 |
| ---- | ----------------------- | ------------------ |
| 目标 | 去掉人为固定槽位，让 host 尾巴离开关键路径 | 纯 host build API、tiling 缓存、ACLGraph |
| 前置 | 无 | kernel 路径不再是 stub |
| 需要 tiling 缓存 | **不需要** | 需要，且被 §5.3 卡着 |
| 需要 ACLGraph | **不需要** | 需要 |
| 交汇点 | A2 产出"构建计划 / 每次绑定"结构 | B 轨 build API 的返回物就是它 |

A2 做对了 B 轨才有地基；反过来不成立。**不要把完整 tiling cache 和所有 ACLGraph
组合捆成同一个交付前置。**

## 2. 资源判据：版本与最后使用者

前几版用的是"**当且仅当 host 碰它** ⟺ `PER_RUN`"。**撤回**——它与本文档自己的
ledger 矛盾（只读 callable 代码被 host 写过却可共享），而且它不回答何时释放。

> **判据：后继的准备是否可能覆盖一个仍有使用者的版本；以及最后一个使用者何时结束。**
>
> 最后使用者包括：device 执行、回拷、状态读取、event wait、**captured graph**。

这条严格更强：它同时给出分类和释放时机。

### 2.1 不能由一个字段推及整个结构

`Runtime` / handshake 要**按字段拆**：哪些是每次参数、哪些是共享执行状态、哪些是结果。
不能因为整个结构在 prepare 里被写过，就永久规定整份每 run 一份。

可选布局是"不可变的每次参数快照 + 串行复用的执行状态"。但**拆字段前保留当前隔离**：
共享化需要逐字段的读写与复位证明。

设备侧 `Runtime` 与 `KernelArgs` 今天本就是独立分配、独立上传
（`device_runner_helpers.cpp:60,95`），可以池化，但必须保留独立的参数版本。

## 3. 四类寿命

前几版是三类。第四类由源码核查补上，它不受任何 run 的 fence 约束：

| 类 | 内容 | 何时可释放 |
| -- | ---- | ---------- |
| 1 · host 构建临时数据 | 构建工作区 | 上传/延迟提交完成、runtime 不再引用；**不能按函数返回判断** |
| 2 · 每次执行记录与参数版本 | `PreparedExecution`、参数快照、结果 | device、回拷、状态读取等**最后使用者**结束 |
| 3 · 共享资源与同步对象 | heap、arena、event | 按实际引用与**代际**管理，不随某个 run 的 finalize 释放 |
| 4 · **captured graph 引用** | 被捕获图持有的地址 | **跨多次 replay**，不由任何单次执行的 fence 界定 |

第 4 类的证据：kernel 模式禁止已提交 arena 的扩容/释放，理由就是**捕获图仍保存地址**
（`device_runner_base.cpp:428`）。所以 §9 里"等旧引用退出"对捕获场景**不成立** ——
eager 的最后一次提交完成 ≠ 图的最后一次引用。

## 4. 五个内存所有者

前几版把它们混成两类。分开之后每一项的回收方式和压力出口都不同：

| 所有者 | 管理方式 | 约束 |
| ------ | -------- | ---- |
| 外部 device Tensor | 调用方所有 | simpler 不跟踪不释放，**也不该无故复制** |
| host-backed Tensor 的 device staging | simpler 每次分配 + H2D，留到回拷 | `runtime_maker.cpp:1200-1228`，全尺寸；program 路径的真实成本 |
| Definition block | simpler 分配并单独上传 | `runtime_maker.cpp:485,510`；按 slot 保留，扩容会释放旧 device block（`device_runner_base.cpp:224`） |
| runtime / SM image + 执行 heap | simpler 自有 arena，构图后提交 | `runtime_maker.cpp:851,875`；串行执行时可复用 |
| 驻留 callable 与代码 | simpler 自管，跨调用复用 | **callable id 上限 64**，且是 host↔AICPU 协议的一部分（`callable_protocol.h:31`，AICPU 侧 `orch_so_table_[MAX_REGISTERED_CALLABLE_IDS]`） |

**本分支没有"整个 HBG 图包随 launch args 交 RTS 管理"的路径**：AICPU launch 传
`sizeof(KernelArgs)`（`device_runner_base.cpp:1542`），AICore launch 参数只是指向
device `KernelArgs` 的指针（`:1778`）。review 引用的 RTS 大 args 与每副本 ~80 MiB
属于另一实现，**标为目标/外部，不进本分支的成本模型**。

同理不采纳为本分支常量：`8192`、`2 GiB`、`2 MiB`、`~2048`、`80 MiB`。

### 4.1 预算与回收分开

代码证明的是"需要安全回收和压力出口"，**不证明需要一套完整的字节预算系统**。
可以保留池化、回收空闲块、等待相关完成、有限重试及最终错误，**不必对外暴露 depth**。

两点限定：

- 当前 retained pool **不会仅因 stream sync 自动释放**，必须显式 finalize / 回收 / 淘汰。
  timeline 回收策略覆盖不了这些块。
- 前几版的"两阶段预算"是对**当前顺序**（staging 先于编排）的推导，**不是永久契约**。
  真拆成纯 host build、在 bind 阶段统一分配之后，很多尺寸可以在首次 device 分配前
  确定，一个预留点可能就够。

## 5. 构建的读写合同

### 5.1 入口值可读，本次图内 producer 的结果不可读

前几版（v6）把这条写成"**HBG 构图禁读任何 device 内容**"。**撤回**——那会回退 HEAD。

真实不变量是：**orch 必须在本次 device 执行开始前独立完成**。入口 tensor 存放在
device，不等于读它依赖本次 device 执行。

| 构建时需要的信息 | 允许？ | 依据 |
| ---------------- | ------ | ---- |
| shape / dtype / stride 等 host 已知元数据 | 允许 | — |
| host Scalar、调用前已就绪的 host-backed 控制输入 | 允许 | — |
| device Tensor 的地址或不透明引用 | 允许 | 仅记录/传递 |
| **已就绪入口 tensor 的内容（含 child-memory）** | **允许** | `host_tensor_access.cpp:123` 映射；`:134` 无映射时走 `copy_from_device`；`runtime_maker.cpp:1182` 主动注册 child-memory region |
| **本次图内已记录 producer 的区域** | **拒绝** | `runtime_core.cpp:121` 的 `require_no_producer` 同时查 `owner_task_id` 与 TensorMap 重叠 producer |

`748c39fbe`（HEAD，#2212）加的正是 child-memory 的读取能力：在它之前
"residency was mutually exclusive with data-dependent orchestration"，那个互斥是它
修的 **bug**。规模：**32 个 orchestration 文件调 `get_tensor_data`，21 个调
`set_tensor_data`**。a5 onboard 没有 host-map 路径，那里每次访问本就是一次
device copy（`host_tensor_access.h:40-43`）。

"已记录 producer"指图中已登记生产者，**不需要等到 device 实际写入**——恰恰因为还没
执行，等待本次任务不能产生进展。覆盖用例：
`tests/st/host_build_graph_validation/kernels/orchestration/validation_orch.cpp:50,65`
分别是读本次 task 输出、以及对本次 task 写过的入口 tensor 做 host 写入，两者共用同一
producer 守卫。

review 的全面禁读是**比现有不变量更强的限制**，记为未来 API 边界提案：若要求构建函数
完全不调用 device API，必须另行定义入口值快照如何提供，并承担 32+21 个 orchestration
的迁移成本（改走 TRB 或 predicate）。**不作为本轮规则。**

### 5.2 `set_tensor_data` 是可观察的构建副作用

两轮 review 都没覆盖这条。构图会**写**外部 tensor：

```cpp
// available_aicore_counts_orch.cpp:74-81
set_tensor_data<int32_t>(shape, 1, idx, cluster_count);
```

`cluster_count` 只有 host 构建知道，这个写**就是本次调用应产出的结果之一**。
路径：`runtime_core.cpp:177` → `host_tensor_write` → 有映射直写，无映射
`copy_to_device`（`host_tensor_access.cpp:151`）。

排队之后它是 §10 那条 `copy_to` hazard 的同型问题，而且**比读危险**：读只是拿到旧值，
**写会破坏前驱正在读的数据**。

### 5.3 因此"可缓存的纯 tiling"要先二选一

缓存命中跳过 orchestration，就跳过本次应发生的外部写入——调用方拿到未初始化或上一次
的值，**且不报错**。仅把 H2D 延后不解决问题。必须选一个并写进设计：

1. **纯 tiling 禁止修改外部输入**；或
2. 把支持的写入变成**产物里的显式初始化描述**，由每次 bind 按序执行——还要定义可观察
   写入的时机、别名、读后写与失败语义。

初版可以只缓存**确认无此类副作用**的构建，其余每次构图。**禁止静默跳过写入。**

## 6. 同一 callable 的构图并发合同

`g_current_runtime` 是 orchestration SO 内的**普通全局变量**
（`a2a3/runtime/host_build_graph/orchestration/common.cpp:39`，a5 同文件），每次构图
`entry_points->bind(rt)` 重新绑定（`runtime_maker.cpp:707`），而 recorder 依靠这个
全局找到当前 runtime（`graph_recorder_pool.h:25`），`wait` 排空**整池**而非某个 build
的任务（`:125`）。

该变量的注释写明了为什么不是 TLS：glibc TLSDESC stale-resolution 崩溃（BZ #32412），
SO 在执行轮次之间被 dlclose / re-dlopen。它同时声明"**所有 orchestrator 线程绑定同一个
rt 值，所以不需要 per-thread 存储**"——而这条假设正是跨 build 并发会打破的。

> **合同：同一已加载 callable 的 build 串行；保留 build 内部的 recorder 并发;
> build 仍可与前驱的 device 执行重叠。**

最后一句是保住收益的关键：要消掉的是 host 尾巴与设备执行的串行，不是 build 之间的并行。
只有确实需要跨 run 并行 build 时，才增加显式 `BuildContext`、job 绑定和按 build 完成
等待——**不能简单改 TLS**，recorder 线程也要绑定，且代码记录了选择非 TLS 的原因。

## 7. caller stream 顺序与四态

### 7.1 返回时必须已建立调用者可依赖的顺序

```text
caller stream 前序 → 本次执行 → caller stream 后序
```

device 可以尚未完成，但不能只把工作放进 host 队列就返回，让随后提交的普通算子抢在本次
执行之前。kernel 路径的 `runtime_c_api.h:639` 明确"返回 0 表示已入队"。

### 7.2 四态必须分开

program 的 `FRAME_STAGED` 可以只是 `VALIDATED_ONLY`，此时**尚未 native prepare，更不等于
已入队**（`python/simpler/worker.py:3262`）。

| 态 | 含义 |
| -- | ---- |
| accepted | host 队列已接受 |
| staged | 帧快照已就位（可能仅 `VALIDATED_ONLY`） |
| **enqueued** | 已提交到设备，占据了 caller stream 的顺序位置 |
| completed | 设备完成 |

**不能把尚未完成构建的 host 请求冒充已占据 caller stream 顺序的位置。**

### 7.3 跨流 join 要覆盖入口和出口

一个 run 占两条流（AICPU Run kernel 在 handshake 里自旋等 AICore），两条流各自 FIFO，
之间没有顺序（`run_stream_pair.h:31`）。用 event 显式表达：

```text
aclrtStreamWaitEvent(后继 aicore 流, 前驱 aicpu event)
aclrtStreamWaitEvent(后继 aicpu 流,  前驱 aicore event)
```

**但两条内部流互相 join ≠ 已与 caller stream 对接。**契约要同时覆盖 caller 前序 →
内部执行、内部完成 → caller 后序，以及不同 run 之间。

前几版拿 a2a3 的 teardown 协议当跨流序的证明，**已撤回**：a5 没有 return gate
（`a5/platform/shared/aicpu/platform_regs.cpp:39-58` 在 AICore 已返回后才把
`DATA_MAIN_BASE` 写回 idle），a2a3 上也证不出 AICPU kernel 最后退出。

## 8. 完成、回收与部分提交

`aclrtSynchronizeStreamWithTimeout` 是"迄今提交的全部"，对不断追加的流是错问题。
每条流在**自己那次提交之后立即** record 一个 event，每个 run 等自己的。
poll 用 `aclrtQueryEventStatus`（`aclrtQueryEvent` 已 deprecated，`acl_rt.h:1514`）。

### 8.1 部分提交没有完整 fence

`ExactLaunchTransaction` 先 AICore 后 AICPU（`device_runner.cpp:562`）；AICore 成功后
AICPU 失败即 `Partial`，资源保留、准入污染（`native_run_execution.h:126-145`）。

| kernel 已提交 | event 已记录 | 应判 | 今天 |
| ------------- | ------------ | ---- | ---- |
| ✗ | ✗ | `NotStarted` | ✅ |
| ✓ | ✓ | `Partial` / `Complete` | ✅ |
| **✓** | **✗** | **`Partial`** | ❌ 判成 `NotStarted` 并立即回滚——**设备已持有 kernel** |

陷阱机制：`progress = Partial` 是在 `submit_aicore` **返回 0 之后**才赋的
（`native_run_execution.h:152-160`），所以把 `aclrtRecordEvent` 的错误当作该
callback 的返回值，会被判成"首次提交之前的失败"。

- **kernel 提交与 event 记录分账**，`Partial` 要覆盖 `kernel_submitted && !event_recorded`
- **已提交但无 fence 的工作要有不依赖 event 的有界 drain/teardown**
- **watchdog 不是替代**：AICPU 提交失败后，已排队的 AICore 可能在等它，把回收 event 排在
  后面不能产生进展
- 即使证明设备静止，**也不自动证明业务未产生副作用**。只在可证明未执行或有专门恢复协议
  时重试一次，**不重新运行 host 编排**

### 8.2 event 的引用寿命

SDK 明写：销毁 event 前，涉及 `aclrtSynchronizeEvent` / `aclrtStreamWaitEvent` 的任务
必须已完成（`acl_rt.h:1458-1464`）。所以 event **不随 run 的其它资源回收**。

而且需要失败退出路径：wait 是在 launch **之前**入队的，所以

```text
N+1:  StreamWaitEvent(my_stream, N_event)   ← 成功，任务已在队列
      rtKernelLaunch(...)                    ← 失败
```

按 N+1 回滚释放会销毁队列仍引用的 event；等"N+1 自己的 fence"则永不触发。

- **预留 / 提交 / 撤销**：入队前预留，成功时提交，入队失败则撤销
- 已提交的引用**恰好释放一次**——正常 fence 或 §8.1 的静默证明
- **`NotStarted` ≠ 没有任何 wait 入队**；它立即回滚的是 kernel 与资源状态

跨流排序 event 与用于大 args 回收的 timeline event **职责不同，不能因都叫 event 就混用**。

## 9. 共享资源的重建规则

`commit_region` 在 `requested_size > cached_size` 时先 `release()` 再重 reserve，失败路径
还会释放 bank 的三个区（`device_runner_base.cpp:454-498`）。共享 heap 之后，一个要更大
heap 的后继会在 prepare 里 free 掉前驱正在执行的那块。

答案按优先级：

1. **容量够** — `requested_size <= cached_size` 已短路为 no-op
2. **新建一代** — 保留旧代直到所有引用它的 run 结束
3. **等待** — 这就是背压

**但第 2、3 项对 captured graph 不成立**（§3 第 4 类）：捕获图的引用不由任何单次执行的
fence 界定。kernel 模式因此直接禁止已提交 arena 的扩容/释放。先例已有：
`kernel_arena_change_is_forbidden`。

TMR 另有两条：缓存 miss 时 arena 在 bind 内就建好上传
（`tensormap_and_ringbuffer/host/runtime_maker.cpp:853-860`），所以兼容性 probe 必须留在
prepare 门（#1588 现状，**不要挪到 launch**）；失败校验会 D2H 读 `SharedMemoryHeader`
（`:203-221`, `:940`）而 `sm_reset` 是 run 开跑时的 AICPU phase
（`aicpu_executor.cpp:620`），所以 header 的错误字段是每次结果，需单独保全。

## 10. 五层串行与跨 run 顺序

| # | 层 | 位置 | 门 |
| - | -- | ---- | -- |
| 1 | L3 Orchestrator | `orchestrator.cpp:183-200` | `active_run_id_ != INVALID` 就直接 return |
| 2 | WorkerThread 车道 | `worker_manager.cpp:497-511` | 固定 `ACTIVE`/`STAGED`，`active.occupied` 即拒 |
| 3 | runner execution claim | `c_api_shared.cpp:940` → `:1123` | launch 需要它，finalize 在 drain 之后才还 |
| 4 | chip run lane | `chip_run_lane.cpp:196-203` | 后继的 `launch_front()` 排在 `finish(前驱)` 之后 |
| 5 | mailbox frame 数 | `worker_manager.h:98-101,533` | 编译期 2，加定长 `std::array` |

① ② 在 **L3 `Worker.submit`** 路径上——生产负载从这里进来。只改 ③④⑤ 会让直连
`ChipRun` 排上队而 L3 仍然串行。

### 10.1 多 active 父 run 是正确性门

```text
run N   :  A → B      B 写 X，只在 A 完成后 ready
run N+1 :  C          C 读 X，提交时已 ready
```

多 active 下 ready 队列可给出 **A → C → B**，stream FIFO 忠实执行。**无报错、无超时，
只是结果错。** 而且没有机制能恢复：`TensorMap` 按 `(RunId, TensorKey)` 隔离
（`tensormap.h:13`），`validate_explicit_deps` 对跨 run 显式依赖直接抛
（`orchestrator.cpp:1176-1186`）。

二选一：**① 保持父 run 的提交顺序**（保守，默认）或 **② 建跨 run 依赖**（要动 key、
producer 寿命规则和 `validate_explicit_deps`）。**per-execution 的 event join 替代不了
这一层。** 也不应据此得出"必须做外部 tensor 的全局跨 run 依赖跟踪"。

### 10.2 但提交顺序覆盖不到 host 侧的旁路

`await_run_admission` 的注释写明：直接设备控制 — malloc / free / copy_* / domain 与
region 创建 — **没有 TaskSlot**，ready-queue FIFO 不排它
（`orchestrator.cpp:554-563`）。**加上 §5.2 的 `set_tensor_data`**，旁路有两类：

```text
run N   :  kernel 已提交，仍在读 X
run N+1 :  copy_to(X, 新数据)          ← 走 mailbox
           set_tensor_data(X, ...)     ← 走 host view 或 device copy
```

kernel 顺序完全正确，两者都可能提前落地。今天 `await_run_admission` 把它们挡到该 run
成为 **active**，即所有在先 run 都**终止**——那是完成等待，不是排序。

选项 ① 的作用域因此是：

| 工作 | 处理 |
| ---- | ---- |
| 同一设备队列能排序的 dispatch | 可提前入队 |
| `copy_to` / `copy_from` / `free` / `malloc`、domain / region | 保留完成等待，或给明确依赖 |
| **`set_tensor_data`** | 同上；而且它是本次调用应产出的结果，不能跳过 |
| host 读入口值（§5.1） | 需要**按实际依赖**排序，不是一律等前驱 |
| 跨不同 endpoint 的依赖 | 两个 child 的流之间毫无顺序 |

有一条相邻不变量框住范围：同一个 run 内，直接设备控制**必须先于该 run 的首次提交**
（`python/simpler/orchestrator.py:255-262`）。所以暴露面是"run N+1 的控制/写 vs run N
已提交的 kernel"，不是任意交错。

反过来也要注意：§10 原来"malloc 等一律等待"是沿用整体 FIFO 的保守策略，**不是所有新
分配都天然依赖前驱**——新资源的创建不必等待无关前驱。

## 11. A 轨分步

| 步 | 内容 | 前置 |
| -- | ---- | ---- |
| **A0** | 共享资源安全：arena 扩容/回滚守卫（§9，含 captured graph 的额外禁止）、TMR 失败状态保全 | — |
| **A1** | per-run event fence；event 引用三态与失败退出（§8.2）；kernel 提交与 event 记录分账、partial 协议（§8.1）；跨流 join 含 caller 前后序（§7.3） | — |
| **A2** | 寿命四分与按字段拆（§2.1、§3）；**同 callable build 串行 + 内部 recorder 并发合同**（§6）；"构建计划 / 每次绑定"结构 | — |
| **A3** | claim 提早释放 + lane 放开非队首 launch（层 ③④）；accepted/staged/enqueued/completed 四态（§7.2） | A0–A2 |
| **A4** | mailbox 请求帧与状态数组拆分（状态容量作为普通背压，错误文本另置存储） | A3 |
| **A5** | L3 两层（层 ①②）+ §10.1 的正确性门先定 + §10.2 的旁路规则 | A3 |

**A2 是与 B 轨的交汇点。** B 轨前置：kernel 路径不再是 stub；其 tiling 缓存部分还被
§5.3 卡着。

诊断相关：DFX collector 目前单例且 `begin_run` 破坏性，带诊断的 run 仍停在一个在飞
——形状同 #2201 之前的 `allow_prepared_successor`，但 gate 的是 launch 而非 prepare。
**不要回退 #2201 对 prepare 的放宽。**

## 12. 证据状态

### 已实测（本分支）

**op-execute watchdog 只算执行时间，不算排队时间。** a2a3 device 1，经 `task-submit`；
探针在 `.docs/wdprobe/`。

| 臂 | 配置 | 结果 |
| -- | ---- | ---- |
| A 正对照 | 1 s op / 3 s 预算 / n=1 | `sync_rc=0`，1008 ms |
| B 负对照 | 6 s op / 3 s 预算 / n=1 | **`sync_rc=507018`**，3929 ms — 被收割 |
| C 问题 | 3 × 2 s op 排一条流 | **3/3 ran，`sync_rc=0`**，6001 ms |

C 的末尾 op 从提交起悬约 6 s 未被收割，因为它只**执行**了 2 s。B 是让 C 成为结论而不是
形式的那一步。**此结论只代表这个探针，不推广为全平台保证。**

**GM heap 是 host 不碰的**：`acquire_pooled_gm_heap()` 的结果只用来造 `HeapRebase`
（`runtime_maker.cpp:905-931`），回拷只走 `tensor_pairs_`（`:1440-1445`）。

**dsv4 arg 91 是调用前写好的入口值**：共享 driver 在
`examples/a2a3/tensormap_and_ringbuffer/deepseek_v4_flash_decode/main.py:678` 分配 host
buffer 并在 `:694` 写入 `[T] * n_ranks`。**依据是 driver 确实提前写了，不是 README 的
标签**；也不能因读取语句出现在 task 中间就认定数据来自前序 task。

### 未验证 —— 各自卡住一条结论

| 项 | 卡住什么 |
| -- | -------- |
| **实际 launch 遇队列压力是阻塞还是返回哪类错误** | "靠 CANN 自动排队承接背压"的可行性全压在它上面，本轮无设备证据 |
| watchdog 的有效预算 | 三臂都回报 `actual = UINT32_MAX µs`（请求 3 s），而臂 B 在 3.93 s 被收割。若钳制是下限，则低于约 4.29 s 的 `SIMPLER_OP_EXECUTE_TIMEOUT_US` 静默失效，**含 CI 的 2 s**。`device_runner_base.cpp:610-624` 只 INFO 记一行 |
| RTS 大 args / timeline 回收 | 属另一实现，本分支无此路径（§4）；不作本分支成本模型 |

### 顺带发现的过期文档

`docs/task-flow.md:322-326` 说 a5 host_build_graph 不发布 contract 且停在深度一。已不成立：
它声明 `pipeline_depth = 2` 且 `concurrent_native_prepare_supported_impl` 返回 1
（`a5/runtime/host_build_graph/host/runtime_maker.cpp:107-132`）。四个 onboard runtime 都是 2。

## 13. 验收

`concurrent_prepare_stress` 断言的是 *prepare* 重叠，四个臂在本改动下全部继续通过——
它量的是本来就成立的性质，**不能当验收**。

| # | 断言 | gap 为什么盖不住 | 何时要有 |
| - | ---- | ---------------- | -------- |
| 1 | 设备侧 inter-run gap 塌缩，**走 `Worker.submit`** | 这是价值测量本身 | A1 前测基线 |
| 2 | 至少 4 个 run 提前被接受 | 两深给出同样的 gap 塌缩 | A3 |
| 3 | §10.1 的 A→B / C 顺序正确 | 结果错不改变时间 | **A5 前必须有** |
| 4 | event record 失败判 `Partial` 而非 `NotStarted` | 健康 sweep 不走失败路径 | A1 |
| 5 | host / child-memory 入口值可读；图内 producer 结果被拒 | 现有不变量的回归栏 | A0 |
| 6 | **每次调用的 `set_tensor_data` 写入都发生**（缓存命中时尤其） | 静默跳过不报错 | A2 |
| 7 | 同 SO 并发被显式串行或被显式支持 | 错误路由不一定崩 | A2 |
| 8 | caller stream 前序 / 本次 / 后序 | 内部 join 不覆盖 | A1 |
| 9 | 各类分配压力与部分提交失败 | 健康 run 碰不到 | A0/A1 |
| 10 | 捕获图跨 replay 的资源寿命 | 单次 fence 不界定 | B 轨 |

## 14. 历次撤回

| 主张 | 版本 | 处理 |
| ---- | ---- | ---- |
| "深度仍是 2" / Little 定律两 bank 够用 / `copy_count(arena)` = 容量 / mailbox 每 run 一帧 / "0.3 MB per run" | 2 | 全部撤回：没有深度；bank 不该是限制；副本数 ≠ 容量；状态槽几十字节；成本是工作负载轴 |
| a2a3 teardown 协议证明跨流序 / "没人 driving finalize 会死锁" / 挪 TMR probe 到 launch / `aclrtQueryEvent` | 2 | 撤回：a5 无 gate；lane 已在 `progress→finish` 驱动 native finalize；miss 在 bind 内就上传；改用 `aclrtQueryEventStatus` |
| "identity = `run_id`" | 3 | 撤回：`run_id` 是父编排来源且直连路径为 0；改用随逻辑执行记录生成的 execution id |
| "prepare 前查精确字节" | 3 | 撤回：HBG 尺寸是编排的结果 |
| "decode 只读地址，不读值" | 3 | 撤回：`get_tensor_data` 是设计能力（32 文件）。dsv4 结论保留，但依据改为 driver 提前写入 |
| "两个机制钉住一个 launched" | 3 | 不完整：**五层**，两层在 L3 |
| "状态数组满 = 误配置" / "等本 run 两个 event 就够" | 3 | 撤回：满表是普通背压且错误文本要另置；`Partial` 没有完整 fence |
| "S5 = 保持提交顺序即等价" / event waiter 计数无失败路径 | 5 | 收窄：只覆盖同队列可排序的工作（§10.2）；引用要三态（§8.2） |
| **"当且仅当 host 碰它 ⟺ `PER_RUN`"** | **6** | **撤回**：与自身 ledger 矛盾且不回答释放时机。换成"版本 + 最后使用者"（§2） |
| **"HBG 构图禁读任何 device 内容"** | **6** | **撤回**：会回退 #2212（HEAD）。真实边界是"入口值可读、图内 producer 拒绝"（§5.1）。全面禁读记为未来提案 + 32/21 个 orchestration 的迁移成本 |
| **三类寿命** | **6** | **改为四类**：captured graph 引用不由任何 run 的 fence 界定（§3） |
| **两阶段预算为主方案** | **6** | 降为"当前顺序的产物，非永久契约"；预算与回收分开（§4.1） |
| **"复用成熟的 kernel_launch"** | 6 隐含 | 撤回：`simpler_kernel_mode_supported()` 返回 0，是占位实现；依赖方向相反（§1） |

## 15. 记录边界

- 本文是静态源码核查加设计建议。未修改 runtime，未跑设备测试（除 `.docs/wdprobe/` 的
  watchdog 探针）。
- review 中"用户已明确"的记录不等于本分支已有能力或已批准的兼容性变更。
- `8192` / `2 GiB` / `2 MiB` / `~2048` / `80 MiB`、timeline 同步必然回收、全平台
  eager/capture 恢复保证、旧 HBG 功能可无代价切 TRB：**均不采纳为既定事实**。
- 代码归一化不自动意味着 program/kernel 可在同进程共存；此前的资源与 ACL 所有权限制
  是否解除需另行确认。
