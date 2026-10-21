> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# PTO 异常首步：有限审计结论与待补入口

复核 lexical `.docs/pto-exception-stop-next-decision.md`（exec77620）；原会话已交付并退出。

接受：五个正常终态 producer 均记录 shutdown rc，a5 HBG 的 -1 已传播，不能重复修复已合入代码。
既有失败在 copyback 前被拦截；ChipWorker 的 finalize 异常经过子进程 mailbox 文本传到父调用方。
manager 检查 `chip_worker.cpp:935-973` 与 `chip_run_lane.cpp:97-134`，确认 finalize 错误
还会置 lane poison，不再准备后继。

不能把有限源码审计称为“异常正常停止已验收”：发出 EXIT 或等待 ack 超时都不证明设备已停止。
作者 §4 的“只有 a2a3 有 device may still be running coupling”过强；a5 的 ack 缺失同样没有
完成证明，正常发信号也不证明故障核心已执行信号。a2a3 未确认核心的 release gate 必须继续关闭。
既有 RTS timeout/teardown 恢复承担兜底，不据此承诺像普通算子一样的停止延迟。

不接受 N1 单独满足用户首步：只加数值 callable_id 不一定是用户可识别的 PTO 算子名，
也不能证明 torch_npu 最终显示它。未批准新 C ABI、完整 kernel launch 或详细任务错误通道。

如果将来开展端到端框架验收，需确定实际集成入口及版本；这不是用户当前需补充的信息。
源码树内未找到 simpler 编排算子的 torch 包装器，不等于部署中没有。
后续相关设计应沿真实异常转换点设计验收；当前无需让原 Claude 循环搜索相同源码。
本轮审计完成、原会话 idle。用户明确这是后续演进原则，不要求当下实现到位；撤回当前框架入口信息请求，不新建实现任务。

用户澄清记录 2026-09-21T08:13:51Z：PTO 算子就是我们的算子；后续整体沿正常停止、明确算子归属、再到内部 task 定位演进。不把参考原则改成当前阻塞验收。
