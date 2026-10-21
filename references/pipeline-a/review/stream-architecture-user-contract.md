> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# Stream 架构：用户确认的设计前提

更新：2026-09-21T03:01:33Z。来源：用户对 simpler stream 工作逻辑的明确说明。

## 已确定的设计约束

- PyPTO 算子在 NPU 上禁止相互并发执行。这是设计约束，不是硬件能力判断。
- simpler 在进程粒度持有一个串行逻辑工作 stream。多个 run 可以提前入队，设备仍逐个执行；不能把每 run 独立 stream 当成隔离错误的替代架构。
- A3：同一个算子的 AICPU 与 AICore task 需要两条 RTS stream 才能并行协作。AICPU 为主控；AICore stream 通过 event record/wait 依附于 AICPU stream，内部用 AICPU stream 呈现这个逻辑执行序列。
- A5：可以聚合为一条 RTS stream。这是用户给出的设计能力，不表示当前代码已经完成聚合。
- kernel 模式：通过 event record/wait 与框架传入的 stream 建立时序附着。
- program 模式：simpler_submit 可以在 AICPU kernel_launch 主机提交结束后异步返回，不需要等待设备执行完成。

## 实施设计必须明确的语义

1. 外部依赖必须覆盖完整算子：外部前驱 → 内部两部分 → 外部后继。AICPU 代表的完成边界必须涵盖 AICore 最后访问；不能将同一算子的 AICPU/AICore 误排成先后执行，也不能令下一算子的任何部分越过前驱完整边界。
2. submit 成功表示完整提交被接受，不表示执行成功、输出回拷完成或资源可回收。若必要 event/另一半提交失败，仍须保留 Partial 与原始错误，不能用单次 kernel_launch 返回零覆盖。
3. 当前 A3 launch_run 先提交 AICore 并记录边界，再提交 AICPU 并记录边界（lexical worktree src/a2a3/platform/onboard/host/device_runner.cpp:634–718）。因此当前代码中 AICPU launch 返回后还有必要 event 记录；精确返回点应是这些提交动作完成后，仍不等待设备。
4. 提前入队需要每 run 的参数、结果、event 引用及资源寿命；串行设备执行不会自动解决主机的提前覆盖或释放问题。固定两槽仍只是当前实现，不是最终容量约束。
5. E2 的错误结果语义、D2H 原始错误保留、输出和退休合同仍须单独论证。stream 前提不构成无同步成功判定的 SDK 证明。
6. 实施前把进程级所有权对应到实际 Worker/子进程/device/context 的拥有者；不能把现有 runner 内部 pair 直接称为已完成进程级共享。

## 明确保留待议

program 模式是否也由 PyPTO 传入自己创建的 stream，尚未决定。宏观异步提交可以用 run handle/完成对象表达，不必把 stream 作为必需公共参数；具体 API 仍待设计。

本次明确架构方向，不等于批准此前 E2/P4 候选实现。先在原会话修订具体设计、管理者评审，再按既定合同确认实现边界。无测试、CI 重试或合并授权变更。
