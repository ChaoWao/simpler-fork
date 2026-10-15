# Discharge 后的清理状态

基线：main `55eb52246`。产品 worktree：`kernel-caller-order`，分支 `fix/cleanup-wait-poison`。

## 范围

A2/A3 `DeviceRunner::cleanup_execution` 在 discharge 前保存 abandon。若未证明消费的 wait 迫使 stream sync，且同步失败，discharge 会 poison runner；旧快照仍选择 release_run_view、AICore retirement 和 fence retirement。A5 已在 discharge 后读取状态。

修复读取顺序。测试使用真实 onboard runtime 库、RTS host 桩和已提交 wait 引用；没有在设备上注入错误或运行 kernel。检查 slot 隔离、引用/fence 保留、无 AICore retirement 尝试、正常回退、已 poison 和重复清理。Program 保留有界全设备恢复，kernel 不使用全设备同步。

## 验证

- 修复前：A2/A3 TMR/HBG 均在 slot 未隔离断言失败；A5 两个 runtime 通过。
- 修复后：真实库两架构 × 两 runtime × 两探针共 8 项通过。cleanup 内含 8 个失败/对照组合。
- Python UT：3040 passed / 75 skipped。C++ queued-wait、fence、stream-pair 三目标通过。
- A2/A3 ordinary sim 全量通过（55 个资源调度作业，L2 HBG 14 passed / 7 skipped，TMR 31 passed / 1 skipped）。
- task-submit 普通板测 `task_20261012_232710_3509026762`：exit 0，设备 0–3，106 个资源作业；L2 HBG 43 passed / 1 skipped，TMR 60 passed。未观察到 AIV。排队与执行约 810 秒，pytest 执行约 588 秒。
- pre-commit 通过。

原始日志、board 命令、source patch、runtime hash、PTO-ISA pin 和外部占用采样在此目录。主线已含 #2536；不是旧 DFX 叠加验证。

## 后续

这份修复不开放 kernel eager，不是 AIV 根因修复。下一项仍是 caller fork/join + native eager，之后接统一 Worker 入口。规划及 HTML 留本地。

## 下一步接线的约束（代码阅读，尚未实现）

- `launch_prepared_run`、`simpler_poll_run`、`simpler_wait_run` 仍使用 Program thread attach。kernel 接线须走共同 `enter_run_thread` 的借用环境验证，不能只删 launch guard。
- `QueuedStreamWaits` 的当前两个形状仅覆盖内部双流，fallback `sync_stream_pair(prepared.streams)` 不证明外部 caller 已消费 wait。caller join 的引用释放需要本身的 proof 或覆盖实际 waiter 的有界恢复。
- A2/A3 `RunStreamPair` 仍承担提交 owner、poll、退役；选择 context-owned streams 不能把它的 Program owner 状态一起误带过去。
- Caller fork 最早的 record/wait 已经是 execution-visible 操作；失败的 Partial 判定必须涵盖尚未发射 kernel 的情形，完整 receipt 必须晚于 caller exit 链建立。
- #2560（当前 open）变更列表与本修复无重叠；它开放 A2/A3 TMR joined launch，不替代 caller ordering。

## 产物一致性检查

23 个产物中 20 个（包括全部 host runtime 和 runtime executor）开始/结束 hash 一致。三个共享产物在排队期间 restage：A2/A3、A5 dispatcher 以及 A2/A3 SDMA warmup。时间戳为 23:27:23–24，任务约 23:31 开始在设备运行。原 hash 精确匹配 HBG cache 对应副本，结束 hash 精确匹配 TMR cache 对应副本（dispatcher 按生产规则先 strip）。

两份 dispatcher 的 SHF_ALLOC 非 NOBITS 段只有 `.note.gnu.build-id` 不同；warmup 的加载段全部相同。逐段比对保存在 `shared-artifact-comparison.json`，结束 hash 保存在 `runtime-sha256-after.json`。这不是所有文件 hash 始终一致；后续归档应在 task-submit 锁内、真正启动测试前再取一次 manifest，避免把排队时间计入验证窗口。

产品提交：[PR #2561](https://github.com/hw-native-sys/simpler/pull/2561)，`98e4da4f`，待审阅。仅 runner 修复和既有探针的回归用例；规划、日志和 HTML 留本地。
