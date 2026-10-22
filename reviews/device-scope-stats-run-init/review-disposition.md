# Claude review 处置

原始 review 固定于 `d8f2696d0`，原文见 [Claude 报告](claude-review-d8f2696d0.md)。本页记录作者核对后的处置，不替换独立审查结论。

## 已修

- **S1**：ScopeStatsBuffer 注释明确由下一次启用调用的 AICPU init leader 重新归属；ordinary/unmapped host boundary 与 mapped device terminal 分开说明，后者允许 host seal 晚到。另纠正 local_seq 不在每次 run 复位的旧注释。
- **S2**：关闭采集的调用不进入 producer begin/flush/close。追读发现旧 flush 也与“disabled no-op”的注释不符，一起收口。新增 resident-base OFF 生命周期测试：整个 shared region、held header、一个有效 terminal bank 均保持字节不变；原实现两架构失败，修复后通过。
- **独立负对照**：在临时对象中分别删除 begin、flush、close 的 enabled 判断，两架构共六次都以断言失败（exit1）被同一测试拦截。未修改运行中的 SDK 二进制。
- **C1**：明确普通收集仍为 best effort，拒绝会日志报错且不写 artifact，不把诊断失败改成计算失败；retained 保留其既有错误回传。
- **C2 部分**：公共 ON→OFF→ON→ON 测试新增跨调用 epoch 唯一性。OFF 的 resident-base 条件由两架构真实 producer 单测覆盖；完整普通 sim sweep 和开启 scope-stats 的 sim 场景另验。

## 保留的边界

- 既有 caller-stream 硬件 helper 直接使用 RTS stream/context API，不能只增加 sim 参数就宣称模拟器也覆盖了它。未为此增加另一套公共执行 helper。
- 尚无专门驱动 AicpuExecutor::init 的 pre_handshake 失败注入回归；leader 中的放置位置仍依赖源码审阅，不能把底层 producer UT 当作该排序的动态证明。
- **C3**：retained-unmapped mismatch 的隔离沿用原 collector 策略，不借本笔改变 sticky-error/retirement 合同。
- **C4**：admission UT 是 host-owner 资格问题，其手写 initialized epoch 已有注释；真实 producer 初始化另由独立单测覆盖。未为这个 fixture 将 device globals 链入四个 host-only admission target。

## CI 与本地硬件

`d8f2696d0` 的 Actions raw job log 确认 A3/A5 × owned/borrowed 四个新增用例均实际通过，publication probes 六个 A3 组合也实际通过。A3 普通场景 job `119688344068` 为 success。修订后的最终 head 必须重新验收，不将这轮 CI 移挂。

本地 retry `task_20261022_000018_70153720238` 因设备一直占满而排队，已在修改源码之前主动取消；没有越过 task-submit，也未声称它通过。最早一次 TypeError 仍按测试输入类型错误保留，发生于 kernel launch 前。


## 独立复核

Claude 对修订代码 `0de1e010b` [复核](claude-recheck-0de1e010b.md)确认 S2、C1 已解决，接受 C2/C3/C4 的上述边界，并独立用删除 flush guard 的 a5 变体验证新测试失败。剩余一处 buf_seq 表格旧语已在同一提交修正；该轮 `a579b791a` 仅这一行文档不同，生产和测试代码未变。复核结论为：无剩余代码正确性阻断，最终 CI 全绿后可 approve。


## Ready 后的 CodeRabbit 反馈

HBG metadata-only 兼容缺口已实际复现并修复。沿用 CapacityMetadata.required，普通/unmapped 显式无 producer 时允许 epoch0；未知/需要 producer 保持严格。两 shared runner 及 retained-unmapped 传递既有 metadata，无新增模式状态。单测验证 producerless 输出及随后未知 producer 的拒绝，实际 A3sim/A5sim public HBG 调用均通过。原 inline 已回复并 resolve。最终 `97aaed615` 的 A3/A5 CI raw log 确认完整8臂全部通过；全部 CI 与独立复核最终结论见 REPORT 末节。

最后 [Claude 复核](claude-metadata-recheck-97aaed615.md)检查 metadata 值/寿命及全部调用点，完成两方向负对照；无剩余 Must/Should，最终 CI 已全绿，条件已满足；首轮 A5 排队超时与原 head 单项重跑证据均见 REPORT。

最终三面反馈复查仍无新增有效问题、无 unresolved thread。Ready PR 保留单提交，最终 head97aa，等待用户审核。
