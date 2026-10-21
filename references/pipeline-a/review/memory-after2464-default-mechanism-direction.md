> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# 合入2464后：默认内存管理机制与关闭责任

基线 upstream/main 818ee04d7308e129d7158ccbd8d078b53def4282（2464已合入）。这是当前设计审查方向，不是新增架构实施授权。

## 已核事实与需纠正的推论

- `manage_workspace`不是Worker配置键，但确实是Python ChipWorker.init可传入的关键字、绑定入口的bool；称完全不对调用者暴露不准确。默认false，普通L2与本地自行初始化L3路径内部传true。
- 原因是分路由接入workspace机制，管理器报告/启用能力缺失时在初始化明确失败。不能据此把支持任意新旧.so混搭当现有兼容承诺，也不能以关闭接收链未经验证推出“没有人能处理”。后者需要逐层检查真实退出、reap、报告与错误传播。
- onboard unmanaged retained_temp增长先free后alloc，失败清空槽；managed acquire失败保留旧块。RetainedTempBump.begin收到非零返回后直接false，不使用失败返回的旧raw。因此目前已证实缓存失败保全有差异，未由该片段证明UAF。begin注释承诺失败保留旧块也比unmanaged实现更强，需要收敛。
- `_is_startup_root`从启动deadline推导，确实把超时传播约定复用成拓扑/关闭责任的代理；需要明确owner事实，而不是让新路径靠是否传deadline决定安全机制。
- env-macro-gating直接约束环境变量/宏等配置门，不能仅凭其条文认定任何内部bool违规；减少重复行为路径的设计原则仍适用。

## 目标形状

统一runtime自有工作区的分配、增长失败保全、消费者引用和安全回收。预算可选，不把管理等同于设置额度。设备完成未知或最后消费者未结束时，任何拓扑都不能复用或释放；degraded状态也不能通过日志降级为继续发布。

拓扑决定错误如何到达调用者及谁保留资源，不决定是否遵守上述安全规则。不要预设二选一的refuse-vs-log策略：若只能记录日志，仍须有可证明存活的owner持有资源，并停止危险后续操作。默认统一机制增加的峰值/保留内存与引用开销必须列明。

## 已派任务

原memory同一会话开展有限只读审计：真实nested/remote/MPI关闭链、原始分配失败/释放失败路径、显式所有者上下文和manage_workspace最终删除条件，形成一个完整且有用户价值的下一PR合同。不得新增实现、测试、PR或capture接线。当前只是设计阶段，经理审查后再按既有约定提交具体合同审批。
