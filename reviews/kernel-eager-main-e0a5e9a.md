# Kernel eager 主线重启核查与实施顺序

日期：2026-10-12。固定源码：main `e0a5e9a16ce2df88643fbd20c4e31406f4062e95`。本文是实施判断，不替代 runtime-design-principles.md。

## 结论

#2507 的共同 prepare/finalize、#2510 的借用 RTS context 身份检查均已合入。下一步是共同执行路径的实际 stream 绑定和 caller 顺序；不再推进已完成的身份校验，也不重做参数模型、PreparedCall/cache 或 Program IO 抽象。

AIV 的现场规划已移到 [独立计划](../aiv-evidence-plan.md)。保留已知故障、按需取证，不将无边界复现变成 kernel eager 的隐含前置。#2536 是独立诊断 PR，不能当作 AIV 修复。

## 1. 源码现状：按符号核查

以下源码路径省略 `src/` 前缀；`tests/` 路径相对仓库根目录。符号是主键，行号不作状态依据。未在本轮运行 kernel 实际 launch 实验。

| 路径 / 符号 | 核查事实 | 对下一步的约束 |
| --- | --- | --- |
| common/platform/onboard/host/c_api_shared.cpp / simpler_kernel_mode_supported、simpler_kernel_mode_launch | supported=0，launch 恒拒绝 | 不能把资源初始化或 probe 通过写成 kernel eager 已交付 |
| 同文件 / simpler_prepare_run | 借用 context 校验，kernel 参数检查，共同 NativeRunContext、slot/bank 与准备路径已接通 | 继续消费这一份实现，HBG 每次重新编排 |
| 同文件 / launch_prepared_run | 首行拒绝 kernel；内部仍调用 attach_current_thread | 只去掉 guard 不正确：需要统一环境进入和 caller 排序协议 |
| common/platform/onboard/host/device_runner_base.cpp / ensure_device_initialized、init_kernel_context | kernel 冷路径创建 context-owned streams/events；prepare 只验证既有环境 | 不重复创建 Program 执行 streams、不接管外部设备 |
| a2a3/platform/onboard/host/device_runner.cpp / launch_run、poll_execution、reap_run | 使用 run_streams_；发布、内核、完成和退休读这对流 | 新接线必须覆盖所有消费者，不只给 KernelLaunch 换两个参数 |
| a5/platform/onboard/host/device_runner.cpp / launch_execution | 发布与 kernel 提交直接使用 stream_aicpu_/stream_aicore_ | A5 当前生产实现同样要绑定实际执行流，不能按未来 MIX 能力省略 |
| 两架构 / recover_device_or_mark_unusable | 调用 aclrtSynchronizeDeviceWithTimeout，再标记 unusable | borrowed 路径不能沿用全设备同步；失败保留与 caller 恢复责任要明确 |
| common/platform/include/host/run_stream_pair.h / ensure、mark_submitted、retire | 已支持多个 owner；stale stream 只能在没有 owner 时替换 | Pipeline A 的旧单 owner 限制已过时；stream 代码代际与最后使用者仍必须证明 |
| common/worker/native_run_execution.h / exact_launch_transaction、LaunchProgressSink | kernel/等待已入队但后续失败会保留 Partial | caller fork/join 的 record/wait 也会产生未完成引用，必须进入相同事务记账 |
| common/platform/include/host/kernel_execution_state.h / KernelEventKind | 五事件词汇已定义，实际生产 launch 链尚未使用 | 枚举存在不是完成证明；不再使用 PrepareTail 或捕获前 record |
| common/worker/runtime_c_api.h / simpler_kernel_mode_init 合同 | context_generation 只验证非零，config/resource contract 尚未作为执行上下文被完整消费 | 在实际调用接线中保留并验证配置/代际，不为存值而另造公开对象 |
| common/worker/chip_worker.cpp / kernel_launch_fn_ | 加载/保存符号，无生产调用 | C API 打通后仍须公开 Worker 参数绑定与提交接入 |
| tests/st/a2a3/kernel_capture / capture probe | 普通 probe kernel 验证两条 internal stream 的 capture | 不是 TMR/HBG executor 的 eager/capture 验收 |

## 2. 选定实现方向

Program 与 kernel 继续走一份 NativeRunContext → PreparedExecution → ActiveExecution → 完成/退休逻辑。PreparedExecution 是一次调用的内部状态，不成为公开可反复 submit 的对象。

执行流在本次取得执行资格时绑定，后续 metadata 发布、两侧 kernel、event、poll/drain、诊断收尾和退休都引用该绑定。流的实际 owner 仍在 context/runner，不转移给调用方或一次 run，也不靠临时覆盖 runner 的全局 stream 字段切换环境。

- Program 使用已有 Program 执行流及设备所有权策略。
- Kernel 使用借用 RTS context 内 simpler 管理的执行流，caller stream 只作为本次排序入口，不销毁、不接管。
- 本次绑定不是新公开 API，不再另造 parallel runner 或单独的 kernel 内存体系。共同路径的生产调用方也实际消费该绑定，避免只有测试使用的空接缝。
- 借用 context 的异常路径只能 poison/保留本域资源和按明确合同进行有界完成检查；不擅自全设备同步、reset 或销毁 caller context。没有完成证明时不得以函数返回错误为由释放已入队引用。
- 每次调用重新准备并提交一次；HBG host 构图与 TMR device 编排保持各自语义。Kernel 无隐式 tensor H2D/D2H；HOST 控制数据按值保留，DEVICE 参数保持借用。

## 3. 三个依次交付的范围

### 下一 PR：共同执行的 stream 绑定与生命周期收口

目标：让一次执行的所有操作使用同一份实际 stream 绑定，并将 owned/borrowed 恢复责任放在共同生命周期决策处。先保持公开 kernel launch 拒绝，不能在只有 happy path 接通时开放。

范围：两架构的提交、query/drain、event 退休和错误处理；共同执行接口与真实调用点。复用现有 PreparedExecution/ActiveExecution、RunCompletionFence、QueuedStreamWaits 和 workspace facts。环境策略不散落成每个 C API 重复 is_kernel 分支。

验收：

1. 真实调用路径上，以不同的 Program/borrowed stream 标记验证发布、两次 launch、fence/query/retire 没有串流；不是只测一个 selector getter。
2. Program 现有排队、逐 run 完成、错误归属不回退，不能重新引入逐次整流 synchronize。
3. 借用环境不会创建/重绑 Program 设备或执行全设备 reset/sync；错误 current context 在副作用前拒绝。
4. 失败点覆盖首次入队前、入队后、record/wait 失败、资源释放重试；保留原始错误与未证明静止的资源。
5. 先从 Program 生产消费者证明这条 seam 有效；公开 kernel 能力位仍为 0，后续连通和验收才开放。

### 接着：caller stream 的 native eager 闭环

目标：caller 前序写入 → 本次 TMR/HBG 执行 → caller 后序读取，无每次 host sync。两架构均沿当前实际双流实现接线。

协议：caller record Start → AICPU wait Start / record AicoreStart → AICore wait AicoreStart / launch / record CoreDone；AICPU 在 fork 后 launch，再 wait CoreDone / record whole-call completion；caller wait whole-call completion，再发布 caller-visible tail。AicoreStart 必须在 AICPU kernel 之前，避免 AICPU 等 handshake 而 AICore 又等其返回。

复用已有逐 run 完成 fence 与 event 引用机制，具体句柄可复用现有拥有者；不能为了套五事件枚举而平行维护两套完成状态。事件记录代次、被等待引用和 context 寿命要同时成立。这里定义的是因果关系，不把注释里的固定 event 数量当资源份数证明。

- 同域串行 host submit 允许在资源容量内提前入队；仍需跨 run 双侧完成依赖。不能借此并发使用一份共享 scratch。
- 切换 caller stream 也须遵守同一执行域的前驱顺序。每次调用保留实际绑定与等待引用，不能由“当前 stream”倒推旧 run。
- caller fork 的 event/等待同样可能已经入队：失败时是否提交了 kernel，与是否存在 stream 引用分别记账。Complete 必须覆盖 caller exit edge 成功建立，不能提前把双 kernel 成功当整个 API 成功。
- 复用 NativeRunContext 保留参数、workspace、callable 和 per-run 结果到实际最后使用者结束；容量不足按明确预算回收/等待，不在每个 launch 后强制等待。
- init 的 config、资源合同和 context generation 在真实消费者处闭合，区分 context 代际、slot generation 和 run_epoch。沿用统一 TaskArgs，不因旧 invocation header 存在就引入第二套用户参数模型。
- eager 阶段在任何不支持的 capture 状态下先拒绝，再发生准备/发布副作用；不让同步 metadata memcpy 悄悄进入捕获。

验收：caller 前写→kernel→caller 后读；HBG HOST 控制 + DEVICE 数据、TMR DEVICE 数据；两个连续调用及换流；TQ=0/1；host 输入容器立即修改/销毁；全部 event/launch 失败边界；不同 context、过期 generation、close outstanding 拒绝与保留。必须使用真实 executor，不以普通 ACL 算子 probe 替代。

### 再接：公开 Worker / Buffer / TaskArgs 路径

目标：上层通过统一 Tensor/Buffer/TaskArgs 使用上述 eager 能力。外部 context/source provider 与目标 Worker attachment 分离，不重建 Worker.borrow_device_buffer 或平行 Tensor 类。

- 将 ChipWorker 当前仅保存的函数指针接到实际提交消费。
- 本次参数绑定、host snapshot、外部 tensor 使用保留及完成/关闭语义贯穿现有公开调用；不把 Python 容器的引用当成 device allocation 自动保活证明。
- 不要求 Program public API 绕经 public kernel API。两者共享内部执行机制，保留各自 IO 和设备所有权合同。
- 真实调用验收通过后才能开放支持位。Capture/replay 按 C1–C4 继续，不因为 eager 通过而默认可用。

这些是同一确定方案的交付顺序，不是可选扩展；每个范围都要有生产消费者或真实 executor 验收，不单独堆放无调用方的辅助类型。

## 4. 与 Pipeline A 和当前 PR 的关系

- #2531（三 run 资源）和 #2534（swimlane 三份容量）是 Pipeline A 扩展；kernel 接线消费已落地主线的完成/引用机制，不要求它们全部完成。
- #2533（HBG root publication）影响 HBG 正确性；#2545（fresh exit ACK）修改两架构两 runtime 的退出协议。实施时跟进并在集成验收前重新核对，不能复制旧的控制 ABI/退出逻辑。本文只读取其当前描述，未评审或宣称已合入。
- #2493/#2494/#2453/#2280 的旧 kernel/capture 候选不能按标题整体移植；只有在具体接入点上与当前共同生命周期、参数模型和 ABI 比对后才复用代码。下一步不是按旧 PR 排号交付。
- U2b 的 Program IO/TensorLease 整理、Remote 完整接入、更多容量与性能优化不作为此次 eager 的隐含前置；每条实际路径的生命周期仍必须当场闭合。

## 5. 本轮工作边界

本轮是读主线代码、登记诊断规划、修正路线状态；没有新增 runtime 实现、没有开启上板复现、没有改远端 Issue/PR。既有 #2536 验证属于该 PR 的 head，不算本轮 kernel eager 验收。设计文件保持仓库外。
