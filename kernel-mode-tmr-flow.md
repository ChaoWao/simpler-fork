# TMR：当前五事件模型与目标执行时序

**依据：[设计原则](runtime-design-principles.md)；源码基线：[2085182a](SOURCE_BASELINE.md)。** 本文解释时序，不另立原则。旧版四事件 / PrepareTail 方案已归档，不能继续实施。


**当前接线补充（2026-10-18 / main `6348a0d1`）：** 公共 eager 走共同 native run lane。下列五个名字是逻辑角色：`Start/AicoreStart/SerialTail` 分别由 slot 的 `RunCallerOrder::Start/Fork/Tail` 承担，`AicoreDone/AicpuDone` 由本次 `RunCompletionFence` 承担；不能只看旧 `KernelExecutionState` 预建的五个 event 就断言生产入口尚未接通。候选 `9b1bfaf4` 在 AICPU wait Start 之后、Fork 之前排 descriptor async H2D；普通无 caller 的复制同样用 Fork 保护 AICore reader。它不开放 capture/graph owner。历史探针与 ABI 描述继续归原基线；当前 workers/prefix 范围更正见[共同协议 §4.1](kernel-mode-prepare-submit-design.md#41-按发布范围与设备写回控制分区)。

## 1. 已有定义与尚未交付的边界

`kernel_execution_state.h / KernelEventKind` 已定义五个 event，context 创建、持有两条内部流和事件资源。`c_api_shared.cpp / simpler_kernel_mode_supported` 仍返回 0，`simpler_kernel_mode_launch` 仍拒绝执行。**以下五事件顺序是主线声明的执行合同，不是完整生产 launch 已通过的报告。**

| Event | record 所在流 | wait 所在流 | 含义 |
| --- | --- | --- | --- |
| Start | caller | AICPU | caller 前序接入逻辑算子 |
| AicoreStart | AICPU，且在 AICPU launch 之前 | AICore | 允许 AICore 启动并发布 handshake |
| AicoreDone | AICore launch 之后 | AICPU，AICPU launch 之后 | 汇入 AICore 的真实结束边界 |
| AicpuDone | AICPU launch 与 AicoreDone wait 之后 | caller | 两侧均结束后返回 caller |
| SerialTail | caller 的 AicpuDone wait 之后 | 执行域下一调用的串行入口 | caller 可见的整个调用尾部 |

拓扑固定为 **caller ↔ AICPU ↔ AICore**。Caller 与 AICore 不直接相邻。`AicoreStart` 若放到 AICPU launch 之后，AICPU 可能正等待 AICore startup report，导致事件永远无法 record。

```text
caller   : 前序 ─ record Start ───────────────────────── wait AicpuDone ─ record SerialTail ─ 后序
AICPU    :        wait Start ─ record AicoreStart ─ launch CPU ─ wait AicoreDone ─ record AicpuDone
AICore   :                    wait AicoreStart ─ launch Core ─ record AicoreDone
```

这张图表示各流中的设备顺序，不要求 host 等待 CPU 结束才提交 Core。实现的 host 发射顺序也必须避免在流头饱和时卡住必要的另一侧发射；Probe L 只测单流，不构成多流活性证明。

## 2. Prepare 不发布捕获外 tail

**PrepareTail 已删除，不是待定项。** 准备阶段可以注册、预留容量并形成本次 host 结果，但不得 record 一个供后续 capture 内 launch 等待的 tail。

Event 对象可以常驻；其最近一次 record 与 wait 必须处在合法的同一捕获上下文。等待捕获前的 record 会遭遇 107024。不能因持有同一个 event handle 就认为捕获依赖合法。依据是主线 `KernelEventKind` 前的注释及 [Probe D 等原始记录](probes/RESULTS.md)。

## 3. 加上 metadata publication 的最终目标流程

本节是 C1/C2 **待交付协议**。沿用上面的五事件执行链，独立 publication 流添加 Ready；每张图添加 Root。五事件定义不等于所有发布/图资源只需五个物理对象。

```text
图入口 caller : 执行域前序完整结束 → record Root
publication   : wait Root → [本图 bank 的上次 SerialTail] → async publish(bank) → record Ready
caller        : 前次 SerialTail + wait Ready → 检查 fault / 初始化共享状态 → record Start
AICPU         : wait Start → record AicoreStart → launch CPU → wait AicoreDone → record AicpuDone
AICore        : wait AicoreStart → launch Core → record AicoreDone
caller        : wait AicpuDone → record SerialTail → caller 后序
```

Root 只约束 publication 不越过图入口的前序；不会把本图所有后继发布都串到前驱执行后。Bank 有 B 份时，第 i 个节点使用 `i % B`。本图对 bank 的第一次使用省略方括号中的 wait；之后等本图该 bank 的上次 SerialTail。不能等待另一张图或 capture 前 eager 的 record。跨图及 eager/graph 间由同一执行域的完整顺序保证，capture 前逐图确认静止。本图第一个调用只继承 Root，不在捕获体内等待捕获外 SerialTail；跨 caller stream 的桥接放在图外，图内不引用外部 record。

默认 B=2 时可有一次后继发布与当前执行重叠。更多 host 在途节点不增加 B−1 的发布窗口。Bank 数在 attach/generation 时固定；改 B 必须重新建图。详细预算、图换参和故障处理见[共同执行协议](kernel-mode-prepare-submit-design.md)。

## 4. TMR 每次执行必须做什么

1. Host eager 每次构造本次参数值；capture 只在建图／显式更新时计算这些值。Replay 不重新运行 host 编排。
2. 将本次 launch state 异步发布到选定 bank；单独保留握手和 DomainControl，不能用整块 Runtime 上传覆盖前驱握手。
3. 等前次双侧最后使用结束，执行有序初始化。Reset 必须早于本次 AICore 首次 handshake 写入；不能让较晚开始的 AICPU 擦掉它。
4. 两侧传入同一 workspace 的 device 地址，按照五事件链执行。TMR 在 device 运行编排并重建 scheduler/arena/mailbox 所需状态。
5. 设备可检测的 first-fault 在结束边界打开之前写入；host 另外判断终态、健康和资源静止，不能把 event 完成当作全部成功。

**当前 ABI 障碍有读写两侧证据：** TMR `DeviceRuntimeLaunchDesc` 首部含 workers；host 发布覆盖整个 device prefix；AICore 从 `runtime->dev.workers[block_idx]` 读取，AICPU 也对同一 Runtime 初始化握手。AICore 的 `dcci(my_hank, SINGLE_CACHE_LINE, CACHELINE_OUT)` 会写回整条线。独立握手区是设备 ABI 与 cache 协议改造，不是只改 host 指针。当前 launch 中清 host 的 `aicore_done` 不提供对应 device reset。

## 5. 不能从 TMR 推到 HBG 的地方

TMR 当前 contract 将三个 arena 分类为 DEVICE_SCRATCH；HBG program 的 GM_HEAP/RUNTIME_IMAGE 已经是 HOST_PER_RUN ×2。HBG eager 每次 host 构图填未发布 bank，attach-without-reset 有其正确的前提。HBG replay 没有这次 host 重建，必须 pristine restore 或等价重建，还要保留 `set_tensor_data` 等设备可见效果。

因此，两 runtime 共用调用、顺序、owner 与完成协议；TMR device 编排和 HBG host 编排/restore 分别验收。不能把 TMR 的 reset 对整块 HBG image 套用。

## 6. 验收与历史

- 正常路径：caller 前后算子、两侧握手和终止、换流、TQ=0/1。
- 捕获：每个 record/wait 合法；第一轮与连续 replay；交替多图；首次 bank 不跨 capture 等待。
- 失败：wait 成功但 launch 失败、kernel 已入队但 record 失败、两侧任一挂死。队列引用退出之前不能销毁 event。
- 最小 reset 区、可见性与跨 task 的真实最后使用者需要 executor 实验；不能用 memset 原语通过代替。

[旧版完整原件](LOCAL-EVIDENCE.md#artifact-1f983d001087)仅保存历史假设和当时记录；当前事件数量与时序以本文及固定源码为准。
