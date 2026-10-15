# 主线下一步：可串接的设备异步调用

2026-09-23，重新核对基线 `9ca91c522e8a80483f6a7cf8ad094f994fdcff5f`。依据源码、runtime-design-principles.md G1–G7 和用户 stream 合同；本轮没有读取 Issue 他人计划。主线保持交接状态，此为独立分析，没有恢复实现。

## 结论

下一项完整交付建议：同一设备/context 的串行执行通路上，支持 `A(x) -> device y -> B(y) -> device z -> C(z)`。host 可连续准备/提交，不必为传递中间 tensor 先等前驱完成或回拷；device 仍按完整算子 FIFO 执行。读取最终结果时才等待对应完成边界。

第一项验收延续现有 A3 HBG program 有界路径，输入输出布局已知、同一 endpoint、无诊断。不是新增算子并发，也不是承诺任意动态构图可以提前执行。若 B 的 host 构图必须读取 A 尚未产生的数值，则该 host 依赖仍须等待；不要以空指针放行或猜测 shape 掩盖它。

## 源码支持的现状

- `src/common/worker/chip_run_lane.cpp:225` joinable_shape 逐项拒绝 device tensor。注释明确说明：HOST tensor 的内部 staging 有 run 所有权，裸 DEVICE 地址寿命属于调用方。故现在的后继提前提交还不是一般的设备数据链。
- `chip_run_lane.cpp:252` permits_joined_launch 同时检查前驱/后继、prepared/activated 状态、backend capability、诊断和通信资源。不能仅删除一项检查便称完整支持。
- `src/a2a3/runtime/host_build_graph/host/runtime_maker.cpp:143` 提供 joined_native_launch_supported_impl；公共 onboard 默认返回0，sim返回0。当前不是所有 runtime/架构已具同一能力。
- `src/a2a3/platform/onboard/host/device_runner.h:270` 的 RunStreamPair 是 runner 成员。它实现 run 共享一对流与按 owner 退休，但这本身不是用户所要求的进程内串行逻辑执行归属证明；多个 context/入口的顺序仍需明确。
- `src/common/platform/include/host/kernel_execution_state.h` 另有 context 私有 AICPU/AICore stream。program/kernel 应共用执行语义，不能只因每个对象内部都有队列，就认定跨入口不会并发。
- `python/simpler/worker.py:11683` Worker.submit 返回 RunHandle，L3 构图同步发生。公开逻辑提交受理、子进程 ACCEPTED、完整 RTS 提交、设备完成、host 可读、资源退休是不同边界，应分别说明；不能将返回 handle 泛称为 kernel 已提交。

## 同一交付必须闭合的三个面

### 1. 确定串行执行的实际拥有者

把“进程粒度一个逻辑 stream”落实到进程、device/context、Worker/子进程和 program/kernel 入口。先明确支持域，域内所有算子进入同一有序提交通路；不能每创建一个 runner 就绕开序列。不同 device/context 不能简单共用一个 RTS handle，需要显式边界。

A3 保持 AICPU 主控，AICore 完成通过 event 汇入完整算子边界，下一算子的任一部分都不能越过它。A5 单流是后端表达能力；不应成为上层另一套完成/所有权语义。

### 2. 中间结果直接在设备上传递

调用方提前提供布局已知的输出设备 buffer，后继只传描述符和地址。其拥有者仍为框架或原分配者，simpler 只保留必要的借用/引用到最后消费者；内部 workspace 由自身管理。不要求先引入跨 run 自动 tensor 依赖追踪，同域 FIFO 与显式调用方依赖合同可作为首步。

区别内容就绪与 host 构图输入就绪：B 的设备读可排在 A 之后，B 的 host 构图不能提前读取 A 的结果。跨进程指针须核对正确地址域和导入引用，不能把 Python 对象仍活着当作子进程映射有效。

### 3. 明确完成、背压和异常语义

host 受理不等于完整提交，完整提交不等于执行成功，设备完成不等于可释放所有引用。队列容量不足可以背压，但回收前驱/持续补位应有自主进度，不依赖用户逐个调用 wait。固定双槽不应成为最终业务/API定义；先在有限深度上验收持续供给，再按资源预算扩展。

部分 launch 失败须保留所有已提交工作的资源和原始错误，停止不安全的新准入；整设备故障可能影响已排队后继，不承诺自动逐算子失败隔离或异步设备侧取消。

## 验收：观察真实公开入口，不只测内部 event

1. A 未完成时 B 已实际入设备队列；device 仍 A 完整结束后 B 才开始。
2. A→B→C 中间 tensor 不回 host；数值、身份和地址归属正确。
3. 连续提交多于两个 run，持续补位无需逐个 wait。
4. 提前释放上层引用、部分提交失败、close、代码重新注册不会造成仍被使用的资源释放/覆盖。
5. 仅 wait(A) 不无故等待整条队列；final result 的 host 访问有明确同步。
6. 同域不同 callable/入口遵守一个串行顺序，不能只测单runner自循环。

在上述正确性闭合后，用最小短算子链测 host 提交成本与设备空档；收益以实测为准。DFX、上传、统一内存支线无需全部完成才能验收本项。之后按同一合同推广 TMR/A5、外部 caller-stream 附着和 capture；不要先为每条路径复制一套运行模型。

静态分析，未运行项目测试或硬件验证；不发布Issue评论，不向已交接主线派实现任务。
