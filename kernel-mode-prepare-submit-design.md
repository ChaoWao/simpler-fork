# Kernel / Program 共同执行协议

更新：2026-09-18。**唯一规范：[设计原则](runtime-design-principles.md)。实施顺序：[ROADMAP](ROADMAP.md)。源码事实：[固定 main 2085182a](SOURCE_BASELINE.md)。** 本文给出选定机制与验收边界，不能用目标图冒充已实现代码。[旧版原件](LOCAL-EVIDENCE.md#artifact-54669faff63e)保存此前完整推导。


**实现进度补充（2026-09-19）：** main `f187c089` 已含 #2342 / #2353 的 HBG 统一发布和 #2358 的公共 descriptor seam。Descriptor prepare/publish 在唯一生产调用点仍紧邻，尚无中间 Runtime 修改窗口；快照性质已建立，分配／复制开销已支付，async / capture 未实现。完整状态以 [ROADMAP 的 S2](ROADMAP.md) 为准；旧源码约束仍引用固定历史基线。

**当前核查补充（2026-10-18 / main `6348a0d1`）：** 公共 Worker eager 已消费共同 native lane；#2618 继承进程拒绝已合入。TMR 的 `workers` 已是尾部，稳态 `runtime_device_copy_size` 不含握手；冷分配只初始化一次。原先“workers 首成员、每次整体覆盖”的论据只属于旧基线，§4.1 已更正。候选 `e23f46e8` 正将两 runtime 的 descriptor 转为实际 AICPU 流中的异步复制，并保留 pinned source；HBG bulk image、DFX 与 capture 仍需各自交付。详见[本轮证据](reviews/async-runtime-descriptor/REPORT.md)。下表是固定历史核查，不是当前缺陷清单。

## 0. 已有基础与交付边界

| 固定历史基线的事实 | 对设计的影响 |
| --- | --- |
| `simpler_kernel_mode_supported` 返回 0；launch 是拒绝 stub | Context init/资源准备不等于 kernel eager 可用 |
| `KernelEventKind` 五事件链，PrepareTail 已删除 | 使用 caller ↔ AICPU ↔ AICore；不恢复四事件或 caller 双分叉图 |
| Orchestrator 仍以 `active_run_id_` 控制执行父 run，`begin_run` 获取 admission lease | 只改 lane 不会开放 Python 排队；父 run 的 A→B→C 顺序须保留 |
| Lane 的 leased overload 容量满时报错，direct overload 可 drain | 不能把 direct overload 的行为当成 Python 的准入逻辑 |
| `RunStreamPair::ensure` 假定 idle，`owner_` 是单一 owner；换 core image 涉及 instruction-cache | 不能只删除 prepare/admission guard；并发 owner 与 code/stream generation 要接 Pipeline A |
| `wait_run_fence` 完成后仍调用 `sync_stream_pair` 取得后端 verdict | #2303 通知 owner 已合入，但正常 drain 和健康消费尚不能算交付 |
| HBG `get_pipeline_contract` 已有 HOST_PER_RUN 双 bank；TMR arena 是 DEVICE_SCRATCH | 提前发布沿用已有分类，不给所有资源乘同一个 depth |
| HBG #2309 已缩短 device prefix；#2315 的显式 descriptor 已合入 | 显式 descriptor 是布局收口，不再重复记同一传输收益 |
| `init_runtime_args` 仍调用同步 `rtMemcpy` | S2 先分离准备/发布，C1 必须改成受支持的异步流内 publication |
| Program 的 AICore launch 已传本次参数；kernel 冷资源类仍有 device args 存储 | 不能用一个旧 `device_k_args_` 模型描述所有入口；分别审计实际消费者 |
| PMU 表从 runner 常驻表选择，enable 决定传真实地址还是 0 | 撤回“当前每次 prepare 分配、cleanup free 表”的旧事实；逐调用字段仍不可冻结 |

当前代码约束的出处见源码索引。S1 已完成，S2 可与 A 并行；共同执行接线以 A 验收为前提。不恢复独立 PreparedCall、重复 submit 或跨调用整次 host 结果自动缓存。

## 1. 两个入口，一个执行合同

以下是目标职责流程，名称是内部职责，不是宣布一套已存在的 public ABI。

```text
Program 用户
  → program 资源层：设备所有权、tensor 绑定/必要 staging
  → 本次参数值 + 本次 IO owner ───────────────────┐
                                               ↓
Kernel 用户 / PyPTO → 框架有序队列 → kernel adapter
  → 借用 device tensor / caller stream + host 控制值快照
                                               ↓
共同执行层：校验 → 本次 host 准备 → 独立发布 → 有序初始化 → 完整 kernel enqueue
                                               ↓
                              本次终态 + 双侧完成 + 健康判定
                                               ↓
Program：copy-back / DFX 消费 / future / retire
Kernel ：caller 后序可继续；内部结果/诊断消费完后 retire
```

### 1.1 与当前 API 层次的融合

公开 Worker 是共同执行主门面；Program IO provider 与 borrowed 环境 adapter 分担资源责任，进入同一 C++ ChipWorker/run lane 与 host-runtime C ABI。公开 ChipWorker 只作兼容，不复制 kernel 后缀执行接口。NativeRunContext/PreparedExecution 是内部一次调用载体；DeviceContextHandle/RuntimeHandle 都是 host opaque handles，不是 RTS context 或 device image。

共同拒绝合同在 K2a 收口，环境/source/generation 在 K2b 消费，公开入口在 K2c 验收。环境拒绝不终结已有 run；真实执行失败按双侧完成与健康证明退休，partial/unknown 保留。Python 管自身逻辑引用，native 管物理执行资源，二者消费同一状态合同。

S3 集中动态库 owner 与能力 profile，并在 C2 为图提供 module/code 保活；不新增另一份执行函数表或第二套 runner。C1 仍从真实 KernelArgs/device descriptor 的生产写入和 reader 改 ABI，未使用的封包声明不能替代生产协议。[融合步骤](kernel-eager-open-work.md)给出唯一细分映射，本文只说明机制。

两条 eager 路径每次重新准备、只提交一次。程序资源层不必绕 public kernel API。Simpler 管所有内部执行态 workspace；输入输出、权重、KV/MTP 等业务持久状态通过外部 IN/INOUT 传入。HOST/NONE Tensor 是显式控制值，不是 kernel 偷偷做 tensor staging 的入口；scalar 保留 dtype/位宽/位模式。

Native enqueue 成功意味着全部依赖已建立；框架队列的 accepted 是另一个阶段。若中途失败，返回失败且保留已经入队的引用，不能假装从未接受设备工作。

## 2. 单次准备结果、执行状态与资源

| 对象/职责 | 保存内容 | 生命周期 |
| --- | --- | --- |
| Callable / generation | code、编排入口、签名、常量和容量合同 | 有效 handle、执行、graph 的最后引用全部退出后 |
| 本次执行上下文 | 参数快照、host 编排产物、重定位信息、DFX device 配置、引用、状态 | 一次 prepare/submit；退休不早于全部消费者 |
| Workspace | publication bank、握手、scratch、终态/诊断、DomainControl | 按子区实际读写者使用期限复用 |
| Graph owner | 捕获源、固定地址/shape、节点 bank 绑定、事件及代码引用 | 图及其在飞 replay 全部结束后 |

沿用现有 per-run / PreparedExecution 的承载位置整理职责，不把 `Runtime*`、CallConfig 或 host-only DFX 直接变成公开准备句柄。API 内借读的 host 内容返回前消费或快照；异步 DMA 读的是 simpler 自己保有的字节。

HBG 先建立本次 host-view window 和 tensor slice offsets，再做依赖它们的契约检查与构图。每次 fresh 编排保留 get/set、dep-gen 和错误效果。#2285 的 canonical/bound 拆分是历史实验，不是本路线要求的缓存机制。

## 3. 资源布局与容量账

默认 publication banks B=2，在 attach/generation 时确定；单 bank 必须通过显式容量配置选择，选定后同样不可在已有图下改 B。这是同一个协议的容量参数，不是可选功能路线。改变 bank 数/地址/布局需新 generation 和新图，旧代引用退出前继续保活。

| 区域 | 选定份数/上限 | 复用边界 |
| --- | --- | --- |
| LaunchState / host-populated execution image | B 个 publication bank | 此 bank 上次双侧与其他真实读者结束 |
| TMR gm_heap / gm_sm / runtime_pool scratch | 同一串行执行域通常一份 | 前次完整结束后初始化 |
| HBG GM_HEAP / RUNTIME_IMAGE | 遵守 HOST_PER_RUN，默认双 bank | 不能把 host 生成的逐调用内容误算成一份 scratch |
| Handshake / scheduler 控制 scratch | 串行执行域一份，独立 cache line | 双侧最后写回之后；本次 startup report 之前 reset |
| DomainControl | 每执行域一份 | 不在每次 reset 的 sm_handle/arena/mailbox 内 |
| 终态、结果、DFX record | 按未消费数和容量上限 | device 结束且回拷/collector 等消费者结束 |
| 图可更新稳定槽 | 每个需要更新的图节点一槽 | 图销毁并且在飞 replay 结束 |
| Host/device 不可变版本与 staging | 按同时存活版本、在途更新分别预算 | 最后 DMA/graph 读者结束；不覆盖仍会被读的源 |
| Code、寄存器/PMU 表、配置常量 | context/generation 常驻 | 所有 handle、run、graph 引用退出 |
| Event / stream | 分开预算 | 排队 wait/record、执行及 graph 引用全部退出 |

容量配置须明确单位，至少含 `publication_banks`、`publication_bytes_per_bank`、scratch/control/result/DFX 字节、`graph_update_slot_bytes`、`device_version_bytes`、`device_staging_bytes`、`host_pinned_bytes`、`max_inflight_submissions`、`event_budget`；这些是**设计字段**，不是声称已有同名 C++ 结构。

图 g 有 N_g 个节点，U_g 个需要显式原图更新。稳定槽 HBM 为 `Σ_g Σ_(i in U_g) aligned(slot_bytes[g,i])`。K 张同形图、每图 N 个可更新节点时是 K×N 份，不是 B 份。每类 budget 计入对齐、保留空闲块和仍被引用的旧代块；各类不重叠记账，HBM 总上限覆盖它们之和。Host 版本与 RTS/provider 自有内存另列可见账。

已有图引用的底层地址冻结；包括 `requested_size=0` 的释放请求也不能放行。已分配块内的逻辑缩小可保留原地址和 committed size；不能借缩小换块。对所有将改动的区域先整批预检，避免后一块拒绝触发回滚、反而释放前面已被图引用的地址。图接入必须按图 owner 的实际引用保护这些地址。Kernel/borrowed 模式本身不代表已有图引用；[#2608](https://github.com/hw-native-sys/simpler/pull/2608) 在 capture 仍拒绝的前提下，让 eager 消费共同替换事务，解除按模式冻结容量的限制。C2 仍须交付图引用期间的地址保活，不能从 eager 可增长推定图中地址可以移动。

本协议执行链含五个逻辑事件；publication 增加每节点 Ready，每图增加 Root。当前选择**每图独立预留**：

```text
graph_event_budget = Σ_g (1 + 6 × N_g)
total_event_budget = graph_event_budget + eager 存活事件 + adapter/其他用途存活事件
```

主线 context 的五个物理 event 不等于已满足此图预算。Probe D 证明过特定多图共享事件形状，但未证明完整 executor 的并发/销毁合同；本交付先不抵扣共享收益。未来优化若复用物理事件，必须重新证明最近 record 归属及所有 wait/graph 生命周期。

B−1 是 publication 与执行的重叠窗口；max_inflight 只增加 host 提前准备/入队范围。Probe L 的 1792 次接受不是可移植 RTS 容量常数。Simpler 可在自身预算不足时预先拒绝或背压，**一旦进入 RTS，实测饱和调用可能直接阻塞，没有可依赖的 WouldBlock 返回**。

## 4. Publication、共享 workspace 与五事件链

### 4.1 按发布范围与设备写回控制分区

旧基线中 TMR `workers` 是首成员，整段 H2D 会覆盖握手；这个论据不能继续当作当前未完成事项。`6348a0d1` 的两种 TMR 布局及共同 HBG descriptor 已将 workers 放在尾部：`runtime_device_copy_size` 的稳态范围在它之前结束，`runtime_device_initialized_prefix_size` 仅供新分配的冷初始化，`teardown_gates` 从不由 host 发布。各 slot 的分配/初始化记录与本次 reservation 共同保护首次使用。不要因此重复新增一个相同作用的物理 ABI 拆分。

规范仍是发布区不覆盖仍在使用的写回控制区，且不能与 AICore 整条 Handshake cache-line 写回共享 cache line。实际 producer/reader 和编译期边界断言一起验收；“分区”可以是同一 allocation 的不同范围，不强制两次独立分配。未来若改为多个 publication bank 共享一个控制区，必须重新证明地址传递、epoch/reset 和 cache invalidate 的范围，不能从当前每 slot 隔离直接推导共享安全。

不能只删 lane guard 或只移动 memcpy。Launch-time host 清零没有配套 Runtime 上传，不能充当设备 reset。目标 reset 放在设备依赖中，早于所有本次首次读写；不在 AICPU 晚到的入口盲目清 AICore 已写报告。

两侧参数中的 workspace 地址指向同一 simpler 分配；RTS 对 host launch 参数的值拷贝不管理其二级指针。AICPU envelope 只保留 capture 不变量和稳定地址。`runtime_args` 指向固定节点 bank，冻结这个指针正是所需行为；tensor/scalar 从 Runtime 参数区读取。真正要移出的，是逐调用变化的 inline enable/有效 buffer 地址等字段，不是把所有 AICPU 数据再搬一次。

Program 主线已使用本次 AICore launch 参数；kernel 冷资源还保留 device args。实现时逐入口核对，不能沿用已撤回的“两侧都读旧 device_k_args”假设。PMU 表常驻并不使 `pmu_reg_addrs = enable ? table : 0` 的值恒定；DFX 同样需审计 enable→disabled、初始化失败→0 等分支。当前 AICore 的 `AicoreLaunchArgs.enable_profiling_flag` 也按值传入：捕获后同样被冻结，不能只改 AICPU envelope 而漏掉另一侧。

### 4.2 最终设备顺序

```text
图入口 caller : 前图/eager 完整结束 → record Root
publication   : wait Root → wait 本图 bank 上次 SerialTail（若存在）→ async publish → Ready
caller        : 前次 SerialTail + wait Ready → fault gate / reset → Start
AICPU         : wait Start → AicoreStart → launch CPU → wait AicoreDone → AicpuDone
AICore        : wait AicoreStart → launch Core → AicoreDone
caller        : wait AicpuDone → SerialTail → caller 后序
```

所有箭头表示流内/事件依赖，CPU launch 和 Core launch 可并发进行。AicoreStart 必须在 CPU launch **之前** record。独立 publication 流保留提前上传窗口；若一律在被前驱完成挡住的 caller 上复制，双 bank 的重叠收益就消失。

Publication **必须是该平台支持捕获的异步复制，发到纳入闭包的流上，并检查返回值**。不能直接在 capture 里重用同步 `init_runtime_args`。Probe I/K 实测 sync H2D 返回 107030 后 capture 仍 ACTIVE，合法 async 后续和 CaptureEnd 可成功；忽略错误可能交付一张缺参数发布的图，不是必然“当场毁掉捕获”。

### 4.3 每图 bank 依赖

每张图建立独立 `last_use[bank]`。节点 i 使用 `i % B`：若本图还没有使用此 bank（顺序轮转时 i<B），只依赖 Root；否则 publication 等本图 last_use 的 SerialTail，再将 last_use 更新到本次尾部。

第一次使用的安全性来自：逐图 capture 前的执行域静止，以及每次 replay 的 Root 被排在域内前图/eager 完整结束后。不能引用 capture 外或另一张图的旧 Done。本图第一个调用也不在图内等待捕获外的 SerialTail；它继承 Root 的入口顺序。跨图顺序由同一 caller stream 的 FIFO 或图外合法桥接建立，不能把外部 event record 偷带成图内 wait。每个 batch bucket 都要付自己的 cold-path 检查和预留成本。Bank 绑定和上述边在捕获时固定。

### 4.4 失败、挂死与已排队后继

| 情形 | 必须保留的行为 |
| --- | --- |
| 设备可检测 fault | 先写 first-fault，再打开本次结束边界；后继 gate 才能可靠读到 fault |
| 正常或受控 early-exit | 走共同停机协议，让两侧停止访问；状态明确为成功/失败/跳过 |
| 已入队的后继 | gate 位于前驱双侧完成之后；观察 fault 后不执行业务写入，但仍完成必要停机与终态 |
| Event wait 成功、launch 失败 | 队列已引用 event；不能按“尚未 launch”立即释放 |
| Kernel 成功提交、event record 失败 | 工作可能仍活跃且无完整 fence；保留资源，走有界恢复/隔离 |
| A 挂死、不发尾部 | B 可能一直堵在 wait，不能声称 B 已执行 Skip；fault gate 不能自行解锁这个环 |
| 只在后端同步/异步通知中显露错误 | 通过健康消费者与有界监测处理，不能把 event complete 当作后端成功 |

DomainControl 的 fault latch 位于每次 reset 重建区之外，保持 sticky；使用 device 递增执行序号区分 replay。Fault-before-completion 是协议不变量。对挂死与迟到后端错误，由 Pipeline A 的健康/终态机制扩展监测；停止新提交，标记未决状态，保留未证明静止的资源。Kernel 不 reset caller 设备，已发生 tensor 写入不能回滚。

CompletionMonitor 替代每次 whole-stream wait 所承担的外层时限，不新设一个任意 deadline。沿用 `RuntimeTimeoutConfig` / `validate_runtime_timeout_order`：scheduler < **实际生效** op-execute < 外层期限，外层还覆盖 scheduler+1500ms 和图特定的 producer window。合法队列等待时间与 device 开始后的执行时限分开；拿不到可靠开始证据时不能把 enqueue 时间当作精确执行起点。Probe M 的量化与“何时真正收割”的未决问题见证据索引，不能只对请求值排序就宣称安全。

## 5. Capture、replay 与显式换参

### 固定绑定

Caller 按顺序更新同地址 device tensor 的内容，再 replay；图中无需每次回 host 准备。Host 派生的 metadata/image 用图持有的不可变源，经捕获的 async publication 送往 bank。HBG 每次 restore 被执行消费的 pristine 内容；TMR 每次在 device 重建编排状态。

### 原图更新

地址/scalar/shape 的变化若影响 host 派生结果，先重新准备相关数据并检查图兼容性。兼容时按如下顺序更新，超出结构/generation 合同则重建图：

```text
不可变 host/device 版本
  → 图外：按 caller 顺序 async 写 graph-owned 稳定 device 槽
  → replay 内：固定槽 → 本次 bank
  → 初始化与执行
```

每次异步更新保有自己的不可变源或独立 staging，不覆写前次仍被 DMA 读取的内容；slot 更新与其前后 replay 在执行域排序。整批兼容性和容量先检查，发布中途失败则图进入不可继续使用状态，不能半更新后仍报告成功。

固定绑定不需要这些额外可更新槽。Device 版本使用 HBM 和 D2D 带宽；不可变 pinned host 版本用 host RAM 和 H2D 带宽。N 个不可变 host 版本可做到没有显式 host 等待，不能把 host 方案一概描述为“必须每轮同步”。固定 graph 源本身由图持有到最后 replay；更新版本在其 DMA 消费完后即可退休。

HBG 动态输入改变构图时必须重做相关 host 决定，不能只 patch 顶层 args。构图时 `set_tensor_data` 的效果必须转为图内有序设备写入；未表达的效果在捕获前明确拒绝。禁止一次逻辑调用部分执行后隐式回 host 决策再补任务。Device 动态控制可用谓词/有界计划；否则上层显式拆调用。

## 6. DFX、关闭与保活

DFX 分三部分：host-only 报告；每次 device 执行要读取的配置；执行产生并等消费者读取的记录。只捕获第二部分的发布和第三部分的设备行为，不将建图期时间戳冒充每次执行数据。

寄存器/PMU 表及 buffer 可常驻；逐调用 enable/有效地址由 publication 更新。#2225 的每 launch writer-cache invalidation 是先例：可以逐调用失效 level、指针、thread index，不能据此清除未消费的 record 队列游标。设备执行序号每次递增，host run_id 不足以标识 replay。

退休检查完整清单：**有效 caller handle、device 两侧执行、DMA、输出回拷、状态读取、DFX consumer、event wait/record、framework callback、captured graph 及其在飞 replay**。一项仍有引用就不能物理回收对应资源。图结束和一次 fence 完成不同；外部 tensor 的 allocator 保活由框架 adapter 接口落实。

Close 拒绝新工作并处理未决执行；未证明静止时不得 free。借用设备不隐式 reset；destroy/free 失败保留可解释的 owner 状态或显式终止处置，不在析构时偷偷重试未知设备。Generation 改动不能原地替换旧图地址，也不能淘汰仍有效的 callable handle。

## 7. 验收矩阵

| 层次 | 最低证据 |
| --- | --- |
| Program 回归 / A 接收 | native 和 Worker 路径两个真实 LAUNCHED；父 run 顺序；合法 host get/set；正常 drain 不等后继整流；错误归属与安全退休 |
| Kernel eager | TMR/HBG、两架构与声明支持的 legacy；caller 前后序；TQ=0/1；host 值快照；无隐式 tensor staging/reset |
| Capture 基础 | 五事件链+publication；每图合法 record/wait；异步复制返回值；reset 在首次读写前 |
| Replay 语义 | TMR 每次 device 编排；HBG 每次 restore 与副作用；重复结果正确；无 host 隐式中途续跑 |
| 更新与容量 | 固定绑定、原图换参、交替多图；B=1/2；稳定槽与版本真实占用；不覆写活跃源；超限不破坏有效图 |
| 故障与生命周期 | fault-before-tail、挂死、partial enqueue、record 失败、未决健康、callback 延迟、图销毁与 close 失败 |
| 诊断与性能 | DFX 开关/切换、记录保留、实际执行身份；HBG STRACE 的 bytes/copied/sm；发布重叠、吞吐和长尾 |

**普通算子探针不是整套 executor 已通过。** 本轮文档更新没有运行新设备实验。测试所需机制和硬件范围按 ROADMAP 逐阶段交付；不以暂时拒绝尚未支持的能力代替最终验收。
