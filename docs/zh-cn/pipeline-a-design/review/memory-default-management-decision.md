# 默认工作区管理：一个 PR 的实施合同

设计归档快照：状态与阅读顺序见[索引](../README.md)。设计约束、历史合同和当前实现须分别阅读。

状态：用户已明确批准实现并交付一个 PR。基线：#2464 合入点 818ee04d7。
本文为实施权威合同；原作者 rev6 为取证材料，下面明确的调用顺序优先于其含混描述。

## 要解决什么

现在普通 L2、本地 L3 已默认管理工作区；nested L3、remote、MPI 和直接 C 调用等路径仍可能走旧分配逻辑。内部 manage_workspace 决定是否登记消费者、保留扩容前旧块和记录释放失败。新路径忘记传它，就会得到另一套行为。

本 PR 让 a2a3/a5 onboard **program 初始化入口默认安装同一管理器**。例如 retained_temp 从小块扩成大块时，先准备新块；失败保留旧块并让本次 run 报错，旧地址的最后消费者没有结束就不能释放。预算继续可选，不要求限额、设备均分或提前分配完整预算。

## 工作过程

1. program 的底层 simpler_init 在任何工作区分配前安装管理器，覆盖经 Worker 与直接 C 入口的调用。已安装时不重置账本；预算仍按现有一次设置规则处理。
2. 原有四类空间沿现有 acquire、consumer 登记及退休规则工作。容量够则复用，增长按需分配；最后消费者无法证明结束时保留，不能因为某条拓扑无法回传错误就释放或继续危险复用。
3. 关闭仍走现有设备清理和 reset 顺序，不新增 reset。内部只记录本次普通管理器回收是否真正完成；fatal/abandon 跳过回收、清理异常或非零返回都不能成为“正常关闭”的证明。
4. 同一 C handle 只有新建或已正常关闭时可初始化；仍活动、有 outstanding run、上次初始化/关闭失败或不确定时拒绝。普通 Worker 的既有 poison 守卫保持，未宣称已发现用户路径 UAF。
5. 正常关闭证明仅能用一次；再次初始化开始前消费，后续安装、attach、预热或异常失败都使状态不确定。不能借用上一轮正常关闭来放行失败后的再初始化。

## 必须遵守的实现顺序

- finalize_device 在 outstanding 检查和 finish_retained_runs 之前写入不确定状态，包括抛异常路径。
- 仅普通 managed 清理路径可产生 sweep-completed 事实。TerminalSweep 持有工作区 mutex：**先结束 sweep 的作用域、释放锁，再调用会再次加锁的 report**；不能在 finalize_except 返回后、sweep 仍存活时调用 report。要求清理 rc 为零、报告有效且无 quarantined / release-unconfirmed；外层 finalize 最终 rc 也为零，才能记录正常关闭。fatal 路径即使返回零也不能产生该证明。
- simpler_init 先保持 kernel/program 模式隔离，再检查 live/outstanding 和生命周期状态；检查通过后，在 install_staged_workspace 等任何可能改变资源状态的操作之前消费关闭证明。安装失败也属于消费后的失败。只有纯拒绝、不触碰资源的路径可以保留原状态。
- init/finalize 仍要求原有 owner-thread 调用；该状态不是新增并发初始化能力。内部字段不加入公共报告或 wire 协议。

## 兼容与边界

- 删除内部按拓扑选择是否管理的传递逻辑；对外 ChipWorker.init 的 manage_workspace 参数保留原位置和默认值，值不再改变上述 program 行为，避免破坏已有调用。
- 已导出的 simpler_enable_workspace_management_ctx 保留兼容语义：初始化前调用成功，安装后沿现有非法状态语义返回；不能直接删符号。
- sim 按其现有实现保持，kernel 走独立入口，不开放 capture，也不新增存储类别、用户开关或 PTO_/PTO2 定义。
- run 错误沿已有通道传播。remote/MPI/nested 的关闭错误回传缺口不在本 PR 改协议解决；不能声称所有 close 错误都会到用户手里，也不能声称 normal reset 返回成功就是已实测设备代际消亡。

## 成本与限制

新增覆盖路径要登记消费者并取得账本锁。扩容时新旧块同时占内存；旧代可能保留多份，没有默认硬上限，安全状态不明可能一直保留到上下文退出，进程退出后的驱动回收效果不作承诺。已有 arena 事务性扩容不重复算新收益。

直接 C 调用在未 finalize、仍有 run、初始化或关闭失败后再次 init，会比以前更早被拒绝。这是明确的行为收紧。清理失败仍沿现有返回路径；本 PR 不提供进程内故障恢复。无吞吐或延迟收益承诺。

## 验收与交付

交付一个 PR。验证默认入口覆盖、兼容参数/符号、增长失败保全、最后消费者退休，以及同 handle 正常关闭再初始化、活动拒绝、失败初始化、fatal 返回零但未 sweep 的拒绝、sweep 报告不重入锁。设计阶段未运行这些测试；实现阶段只允许尚未执行过的新增用例及必要编译/静态检查，不重跑既有或已执行测试，不恢复任何已消费 CI 权限。最终 Ready 仍需精确 head 审查、阻塞意见闭合和相关 CI 完成，不代用户合入。

取证材料：原 memory worktree 的 .docs/memory-after2464-default-mechanism-decision.md rev6；manager 的 memory-default-mechanism-rev5-review.md。当前已获用户实现与单个PR发布授权。
