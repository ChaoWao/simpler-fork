> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# Workspace：按位置、写入方与最后使用者分类

历史源码分类快照（不是当前进度）：main `47de5bb168f3b9c9351317b8c1b7517ed6873272`。仅静态核查，未运行用例。
当前状态见 [路线图](run-pipeline-track-a.md) 与 [内存报告](memory-current-state.html)。原始源码分类保留下面的精确基线；当前预算政策以修订后的 G3 为准，#2571 的传输/执行寿命拆分仍是未合入 PR。

本文提供 [运行时总体原则 G2–G4](runtime-design-principles.md) 的内存分类与代码依据。
代码事实仍以以上核查基线为准；目标原则适用于 program / kernel 共用的内部资源管理，
不以当前布局或历史 Issue 提案替代目标，也不表示 Pipeline A 必须先完成全部内存迁移。
A 轨范围见 [设计 §0.4](run-pipeline-track-a.md#principles-application)。

## 1. 先区分三个问题

1. **存储在哪里、谁拥有？** host RAM、simpler device 内存、外部 tensor、RTS 存储分别记账。
2. **内容由谁在什么时候写入？** host 提前 prepare 写入，与设备在有序执行中生成，是不同性质。
3. **谁最后使用？** host 构图结束、device 双流完成、输出回拷完成、collector 归还、graph 生命周期结束，不能合并成一个“run 完成”。

**内容每 run 不同，不等于必须为每 run 分配一整套空间。**
只有旧内容与新内容必须同时存活，才需要独立版本；版本可以来自同一池的不同子区。
“复用空间”也不是复用上一 run 的计算值，而是在正确的边界重置、重新写入。

## 2. Host：submit 使用的内存至少有五类

| 类别与实际对象 | 是否有 per-run 值 | 最后使用边界 | 原则 |
| --- | --- | --- | --- |
| 提交封包与快照：TaskArgs、CallConfig、mailbox frame、ChipRunState.args | 是 | 接收方完成必要的快照 / 解码；frame 中若还承担状态和错误通知，则这些字段继续保留 | 请求字节与完成状态分开看寿命。不能因 submit 返回就复用；也不应让大请求字节无条件活到 finalize。该历史基线尚未完成这种寿命拆分；当前候选 #2571 已实现并验收该方向，但仍待评论闭环与合入。 |
| HBG 构图 scratch：host TensorMap、GraphRecording 的 node / tensor / dependency 数组、recorder 临时池 | 是，但一般只服务构图 | 构图和所有 recorder 作业结束，产物已转交 | 保留容量、重置内容；按真实并发构图 / recorder 数量提供工作区，不按 device 在飞数机械复制。允许同一 build 内并行 recorder，各自仍需 scratch。 |
| 上传源与 host mirror：SM mirror、Definition staging、打包 image；TMR 初始化 image / cache | HBG 每次构图有新内容；TMR 部分是配置相关缓存 | 所有 host 读者结束，且上传已消费源字节；异步上传须等相应完成证据 | scratch 容量可以复用，缓存须按身份 / 配置失效；不因 device 继续运行就保留已经无人引用的上传源，也不能在异步复制前提早覆盖。 |
| Run 控制与回收记录：NativeRunContext.runtime、TensorLease、Prepared/ActiveExecution、fence 状态、错误与 trace 元数据 | 是 | 输出回拷、资源退休、错误传播及对应消费者结束 | 每个未退休 run 保留逻辑独立状态，可池化；不能当构图 scratch 在 prepare 结束时清掉。 |
| Host 常驻缓存和采集存储：callable / SO 元数据、配置 image cache、collector shard / 导出记录 | 按 callable / 配置 / run变化 | 缓存失效或最后结果消费者结束 | 与 submit scratch 分开管理；容量常驻不表示记录可无限积累，消费后的记录应按协议回收。 |

**接口按值交付与内部异步保留不冲突。** 调用方的临时描述在约定消费边界后可失效，
simpler 将后续仍需使用的字节保存为自己的快照。Tensor 描述符中的地址另有借用寿命；
异步 H2D 尚在读取调用方 host buffer 时，不能声称已完成数据的值交付。

**Host 内存不占 device workspace 上限。** 采用“暂存区复用容量，异步状态按寿命保留”，
而不是要求所有 submit 内存放进一个全局 bump allocator。
是否需要独立 host 上限是另一项策略，device workspace 的可选预算策略不自动回答它。

代码依据：

- [ChipRunState](https://github.com/hw-native-sys/simpler/blob/47de5bb168f3b9c9351317b8c1b7517ed6873272/src/common/worker/chip_run_lane.cpp#L25) 与 [mailbox 布局](https://github.com/hw-native-sys/simpler/blob/47de5bb168f3b9c9351317b8c1b7517ed6873272/src/common/hierarchical/worker_manager.h#L93)。
- [HBG TensorMap 的 host 数组](https://github.com/hw-native-sys/simpler/blob/47de5bb168f3b9c9351317b8c1b7517ed6873272/src/common/host_build_graph/tensormap.h#L367)、[thread-local GraphRecording](https://github.com/hw-native-sys/simpler/blob/47de5bb168f3b9c9351317b8c1b7517ed6873272/src/common/host_build_graph/host/orchestrator.cpp#L890)。
- [Definition staging / SM mirror](https://github.com/hw-native-sys/simpler/blob/47de5bb168f3b9c9351317b8c1b7517ed6873272/src/common/platform/onboard/host/device_runner_base.h#L1664)、[NativeRunContext](https://github.com/hw-native-sys/simpler/blob/47de5bb168f3b9c9351317b8c1b7517ed6873272/src/common/worker/native_run_context.h#L78)。

## 3. Device：按内容协议划分，不按分配块名称划分

下表的“空间可复用”均以同一串行执行域为前提，不能跨独立并发执行域强行共享。

| 类别与实例 | 值是否 per-run；谁写 | 空间复用原则 |
| --- | --- | --- |
| 执行中间 tensor：TMR ring heap，HBG 中间输出、整 run 临时 tensor | 是；主要由 device 执行生成 | 只需可容纳峰值同时存活数据的空间。同一 run 内按 consumers / scope 寿命复用，跨 run 在全部使用结束后复用；不能按单个 kernel 返回回收整个 heap。 |
| 动态调度 scratch：TMR task ring、device TensorMap、dependency pool、ready queue、RuntimeContext；HBG GraphExecution 的可重建状态 | 是；device 初始化并更新，可能有 host 预制初始布局 | 布局兼容且后继初始化在前驱最后使用之后时，可共享空间；初始化必须恢复完整不变量。HBG 的提前物化窗口可能同时存活，不能仅按任务 DAG 的数据依赖推算此容量。 |
| 跨核握手 / 控制：workers、teardown_gates、aicore_done 等 | 是；AICPU / AICore 互相读写，混有常驻配置 | 可复用，但要证明跨双流的初始化、首次发布和最后使用顺序。不能让后继 host prepare 的 memset/H2D 擦前驱状态，也不能把本 run 已发布的字段清掉。 |
| Host 提前发布的执行内容：TMR orch args / 每次配置、Runtime 中的 host-written 字段；HBG task/argument image、Definition、a5 scheduler 初始状态 | 是；host prepare 产生，之后 device 读取或修改 | N 仍在读取而 N+1 要提前写入时，必须保留不同内容版本，可用 RTS 值副本或池内子区；也可改为设备有序上传 / 重建并证明源数据寿命后共享目的空间。RTS 拷贝不深拷贝其中地址所指的内容。不能因为它名字叫 arena 就归作 scratch。 |
| Host-backed tensor 的 device staging：RetainedTempBump 切片，输入/输出/INOUT 副本 | 是；H2D 输入、device 写输出、D2H 读结果 | IN 到最后 device 读取；OUT/INOUT 至所需回拷与设备引用都完成。不是外部 tensor 本体，也不是可在 device 完成时一律覆盖的 scratch。 |
| 每 run 终态与计时：DeviceRunResultRegion、device_wall | 是；device 写，host 晚读 | 保留到 host 成功消费且设备不再引用；N+1 启动不能覆盖 N。每个未消费 run 有独立归属/版本，可从统一池分子区。 |
| DFX 采集空间：swimlane / scope_stats / dep_gen buffer、args_dump payload arena、PMU 记录及队列 | 是；device producer 与 host collector 协作 | 池可常驻，buffer 按发布、消费、归还协议复用。run/device 结束不等于 host 已消费；身份随记录或 buffer 固化，不能靠清空猜归属。 |
| 常驻代码和常量：kernel_bin、callable 地址表、Ctrl/PMU 地址表 | 通常不随 run 变；注册或 context 初始化时写 | 按代码身份、配置与 context 代际共享；独立常驻预算和容量限制，不按排队 run 复制。已注册并交付的资源不透明淘汰；释放遵循明确生命周期合同，不能因当前无在飞 run 就推断 graph 不再引用。 |
| RTS 管理的 launch 参数 | 是；launch 传值后由 RTS 管理 | 依照 RTS 的任务/捕获协议保留参数值。参数中的 device 指针不会因此深拷贝其所指内容；不计入 simpler workspace 池。 |
| Provider 管理的 SDMA workspace | 由 provider 初始化，执行时使用；runner 保存 handle/地址 | 按 provider 生命周期申请、释放与核算，不能未经接口支持就改成池内子区。它是内部功能资源，不因不走 MemoryAllocator 就在容量报告中消失。 |

外部框架 tensor、输出和 cache 另属借用内存，不由以上内部池接管。Program 外层自管输入输出时，
该管理层与执行 workspace 仍分开；kernel 模式也由 simpler 管理内部 workspace。
Program 不必每次搬运 tensor，kernel 也不等于所有内部资源都由调用方提供。

### 当前物理布局不等于目标分类

- TMR 的 PipelineContract 将 GM heap、GM SM、prebuilt runtime image 声明为 DEVICE_SCRATCH，
  TASK_ARGS 为 HOST_PER_RUN；这是其预制布局复用合同，不表示所有 Runtime 字段都不变。
- HBG program 路径目前把 GM heap 与 runtime image 都按 HOST_PER_RUN 声明、按 bank 隔离。
  内部有可复用的 scratch 成分，但不能直接把整个 bank 改成共享；须先分离提前写入的内容。
- `DeviceRuntimeLaunchDesc` 本身就混有 workers / teardown_gates、每 run 标量配置、函数表。
  对整个 Runtime 贴“参数”或“workspace”标签都会漏掉寿命差异。
- HBG 的入口 orch args 已在 host-only tail，不能再算作每 run 上传的 device 参数。

代码依据：[TMR 合同](https://github.com/hw-native-sys/simpler/blob/47de5bb168f3b9c9351317b8c1b7517ed6873272/src/a2a3/runtime/tensormap_and_ringbuffer/host/runtime_maker.cpp#L86)、
[HBG 合同](https://github.com/hw-native-sys/simpler/blob/47de5bb168f3b9c9351317b8c1b7517ed6873272/src/a2a3/runtime/host_build_graph/host/runtime_maker.cpp#L109)、
[混合 Runtime descriptor](https://github.com/hw-native-sys/simpler/blob/47de5bb168f3b9c9351317b8c1b7517ed6873272/src/a2a3/runtime/tensormap_and_ringbuffer/runtime/runtime.h#L149)、
[HBG host-only args](https://github.com/hw-native-sys/simpler/blob/47de5bb168f3b9c9351317b8c1b7517ed6873272/src/common/host_build_graph/runtime.h#L193)、
[结果与计时存储](https://github.com/hw-native-sys/simpler/blob/47de5bb168f3b9c9351317b8c1b7517ed6873272/src/common/platform/onboard/host/device_runner_base.h#L1788)。

## 4. 总体内存原则与 A 轨应用

1. **Host 构图 scratch 复用容量，host 异步状态保留到退休。** 数量取决于真正并行的构图/recorder，而不是 run 总数；不消除已有的 build 内部并行。
2. **Device 执行 scratch 复用空间，不保留上一 run 的值。** 按峰值同时存活需求定容量，受 init 总上限约束；在设备有序边界初始化和复用。
3. **后继不能提前覆盖前驱尚未用完的数据。** 提前写入时使用不同子区；复用同一子区时，覆盖必须排在前驱最后使用之后，并保证上传源的寿命。
4. **Device 产生、host 后消费的内容保留到消费结束。** 结果、输出 staging、DFX 各自有回收边界，不能和 scratch 一起 reset。
5. **常驻身份资源与外部所有者资源单列。** 代码/常量有独立上限，已交付项不透明淘汰；外部 tensor 不接管；RTS/provider 管理的资源遵循其接口，单独报告占用。

对于 simpler 自有 device 工作内存，总预算包含 scratch、同时保留的参数/image 版本、staging、
未消费结果/DFX buffer、块内空闲/对齐及旧代保留块；不能只预算 tensor heap。
Host RAM、常驻代码/常量、RTS 参数、provider workspace 分项列明，不能将其中一项当总显存。
Provider 是否能纳入同一个硬上限，要先取得其容量和生命周期接口，不能假设已经可池化。
但 simpler 引入的 provider 内部资源仍须纳入容量管理；专用分配接口不能成为无预算、无记账的例外。

例：N 执行时 N+1 prepare。host recorder scratch 在工作和上传结束后可复用；N 的 device
ring/heap 仍由 N 使用；N+1 的 image/输入不能覆盖 N。N 双流结束后，N+1 可按协议复用
执行 scratch，但 N 的输出与错误结果仍须等回拷/读取结束后归还。

### Pipeline A 只承接提前入队改变的寿命

| A 轨必须做 | 可独立交付，不是 A 轨全量前置 |
| --- | --- |
| 检查 N+1 prepare 不覆盖 N，保护参数 / staging 上传源 | 全部 workspace 迁入统一 allocator |
| 共享 scratch 的重置排在前驱全部使用者结束之后 | Program / kernel API 统一及 A5 mixed launch |
| N 的终态、计时和输出保留到最后消费 | Capture / replay 的完整参数更新能力 |
| DFX 未连续化时保留诊断独占，维护现有输出保证 | 完整 DFX 会话与合并输出、常驻代码容量和 handle 改造 |

统一池是总体资源方向；A 轨可在现有 bank 和 allocator 上先兑现安全排序与回收。
任何取消隔离或更改分配方式的支线都必须遵守相同寿命合同。一次成功的 eager 排队验收，
不代表 capture / replay 已通过，也不证明 workspace 统一管理已经完成。

## 5. 相关 Issue 提供什么证据

| 跟踪项 | 与分类相关的内容 | 使用限制 |
| --- | --- | --- |
| [#2254 追加评论](https://github.com/hw-native-sys/simpler/issues/2254#issuecomment-5726628274) | 存储与内容寿命分开；scratch、常驻代码、RTS 参数独立预算 | 其中 #2289 分支的布局和用量不是 main 的测量 |
| [#2223](https://github.com/hw-native-sys/simpler/issues/2223) | stream 排序不会自动复制其指针指向的 Runtime/image | 历史“两个 bank 必定足够”的论述不是本设计原则 |
| [#2052](https://github.com/hw-native-sys/simpler/issues/2052) | HBG TensorMap 在 host，TMR TensorMap 在 device；两者不同的容量与回收机制 | 此位置差异已对代码核实；旧测量和建议比例不是通用容量公式 |
| [#1956](https://github.com/hw-native-sys/simpler/issues/1956) | HBG heap 分输出活跃集、物化窗口、整 run 长寿命分配 | main 已按 host 构图实际分配量提交 heap；不能再声称 program 路径仍固定申请 256 MiB。实际分配总量也不等于最优活跃峰值 |
| [#1920](https://github.com/hw-native-sys/simpler/issues/1920) | ready queue 按实际可达任务定容 | 已关闭；不把旧缺陷当待实施项 |
| [#2291](https://github.com/hw-native-sys/simpler/issues/2291) / PR #2295 | input staging、output copy-back、binding release 三种寿命 | 接口已分离；不表示已经移除同步或获得安全的任意复用 |
| [#2267](https://github.com/hw-native-sys/simpler/issues/2267) / PR #2283、#2298 | per-run 终态、完成与资源退休 | 结果有归属，不等于资源已可回收 |
| [#2202](https://github.com/hw-native-sys/simpler/issues/2202) / #2078 | DFX 常驻服务与采集记录寿命分开 | 服务常驻不等于所有逐 run reset 已经消除 |
| [#2042](https://github.com/hw-native-sys/simpler/issues/2042) | recorder host 内存与冷启动有关 | 不把“保留容量”直接解释成所有页面已常驻，或已证明延迟收益 |

当前 HBG heap 定容依据：[a2a3 runtime_maker.cpp](https://github.com/hw-native-sys/simpler/blob/47de5bb168f3b9c9351317b8c1b7517ed6873272/src/a2a3/runtime/host_build_graph/host/runtime_maker.cpp#L874)。
