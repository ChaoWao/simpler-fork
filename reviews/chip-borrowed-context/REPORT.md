# Borrowed ChipWorker context

初始基线 main `946f49acbbd4df72b333784cfad60b1573e70b40`（#2572 合入），分支 `feat/chip-borrowed-context`。本报告、HTML、路线与日志留本地。

## 本次交付

同一 ChipWorker.init 新增 keyword-only `borrowed_context_generation`。缺省仍调用 owned init；非零值选 caller 已 current 的 RTS context，走现有借用 native init。binary/runtime storage/lane/submit/handle/poll/wait/finalize 共用。SDMA、sim_context_path、warmup、Program workspace budget/management 在借用初始化前拒绝。Legacy supported 位不决定 native 入口；仍为 0。

初始化失败分两种：已建立借用资源时显式 close 后才能 destroy/dlclose；若 close 失败，保留 context、library、storage，initialized 仍 false、拒绝 reinit，显式 finalize 可重试。UNSUPPORTED 没建立资源，不欠 close。模拟器 unclaimed close 本身也不得释放 caller thread 上别人的绑定。成功 rollback 可重试 init；成功 finalize 是 terminal，需要新 Worker。

共同 current-context 校验入口在 host lane/native phase 修改前执行。错误 RTS context 下 progress、wait、close 不能消耗状态；恢复 current context 后同一 run handle 可重试。对象析构时若 close 仍未完成，则旧 handle 脱离已销毁的 ChipWorker 并报告错误；native host storage、lane snapshots、RTS resource owners 和库保留到进程结束，不伪造完成证明。

共享注册也沿共同环境检查：owned attach / borrowed validate 在 runner 内决定；TMR 实际注册使用已经建立的 control stream。注册 wait 未证明任务完成时不释放它可能还读取的 callable。HBG 仍每次 host 编排，不引入 PreparedCall 或整次准备缓存。

## 验证

最终语义代码（尚未 commit）：

- C++ 317/317 无硬件目标通过；joined lane 最终 31/31（stop-admission 补充前 30/30），新增四项验证错误 context 不消费 phase/活跃 lane 与 owner 析构后旧 handle 不访问死对象。
- Python 3202 passed / 91 skipped。
- A2/A3 sim：55 resource cases 和两个 runtime 的 L2 分组通过；A5 sim：51 resource cases 和两个 L2 分组通过。
- 两架构 × 两 runtime：实际 runner / RTS 桩四组通过；包括 caller closure、partial enqueue 和新 common context preflight。A5 本机无 silicon。
- task-submit `task_20261014_014956_379065229806`，device 5：44 passed / 27 deselected，175.78s。新测试覆盖 owned/borrowed × TMR/HBG × depth1/2；借用使用 caller 的外部分配，四次调用交替两条 caller，仅同步 caller 即验全部结果，随后 wait；错误 context 注册与带活跃 run 的 finalize 均拒绝，恢复可继续；close 后 caller stream 仍可同步、context identity 不变。
- 普通 Program sweep 最终通过（exit=0，523.83s，121 resource cases 与 L2 分组通过），见 sweep/run.log。按 CI 排除 level4/manual/sdma/sdma_fault；此轮在共享注册修正后执行，不能使用前一轮代替。
- lint/diff check 通过，最终 log 见 lint-approved.log。

## 红对照与诊断

1. 移除 common context preflight 的单独编译负对照，三个新 lane 测试 3/3 失败；正常 suite 30/30。negative.log 与 without-context-guard.cpp；没有修改运行 sweep 的 native binary。
2. UNSUPPORTED mock 回归先失败：不应发生的 finalize 次数为 1；修正后完整 UT 通过。unclaimed-red.log。
3. 四个 actual sim unclaimed-close 测试先失败：caller 的 pipe storage 被删除，查询返回 None。检查存在后才读数据，没有读已释放地址。sim-unclaimed-red.log；修正后四组及 full Python 通过。
4. 初次实板借用 ChipWorker 四臂在注册返回 -1003；native caller 旧入口与 owned ChipWorker 通过。单臂重现同样失败，确认共享注册仍用了 owned attach，TMR 另有 program stream 选择。修正共同路径后单臂与四臂完整测试通过。原始日志保留 smoke-before-registration / focused-before-registration；该失败发生在 launch 前，不是 AIV。
5. 初次 full C++ build 卡在 init signature 静态断言；新参数实际是 appended，更新 frozen alias 后 317/317 通过。没有删除断言。
6. 初次 Program 普通 sweep 在注册修正前也通过（522.42s）；保留 sweep-before-registration，最终不能冒称为修正后验证。

两轮板测都有独立 ascend 目录与持续 occupancy.log，使用主线 DFX、没有 overlay。不将本次通过等同于 AIV 已修复；原故障仍未定因。

## 边界及后续

只开放内部 _ChipWorker 初始化选项；公开 Worker 还没有借用上下文入口，统一 Buffer/TaskArgs 外部 provider、generation 实际消费者与框架队列保活仍需下一份 PR。传入 generation 本步仅转发 native admission 的已有非零校验，不声称解决 handle 回收/ABA；旧 kernel launch stub/capability 位继续关闭。Capture/replay 在完整 eager 验收之后。

## 最终提交

单个 commit `ea2819be`（main `946f49acb`）。全量 sweep 归属于最终诊断文案与 stop-admission 补充之前，未把旧数据移挂成新 head。最后一项只影响错误 current context 下停止 admission：仍停止新提交，延迟未启动 run 的资源回收；恢复 context 后不重新开放 admission，31 项 lane 全过。最终提交核验另补。

最终 head 重建成功；31 项 lane 测试通过，完整 lint 通过。一次 pre-commit 与 commit 重叠，pyright 零错误但因工作树相对 HEAD 改变被报 files modified；工作树没有产生额外改动，随后在固定 head 重跑全部 hooks 通过。最终 Python 定向结果见 py-committed.log。

[PR #2578](https://github.com/hw-native-sys/simpler/pull/2578) 已创建，head `ea2819be`，16 文件 +550/−222，待 review/CI。提交 head 的 Python 82 passed / 34 skipped、lane 31/31。规划 HTML 和本报告未进 PR。
