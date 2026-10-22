# Scope-stats 设备入口初始化：C1 增量

本轮基于 main `fdcbdcc95`。最终本地提交 `949fb596c`，单提交分支 `feat/device-scope-stats-run-init`。[PR #2663](https://github.com/hw-native-sys/simpler/pull/2663) 为 Draft，本地检查已完成，A3 板测排队中。

## 功能边界

普通与 retained 模式均由 AICPU 唯一 leader 在 handshake/configuration 之前复位四项 producer accounting，并重新归属仍由 producer 持有的 buffer。Host admission 只清 host 窗口，不再读回 held pointer/seq、上传两项计数或 buffer header。队列游标与持有指针保持不变。

本次初始化 epoch 占用 ScopeStatsBufferState 既有 padding，总大小和原字段 offset 不变。普通/unmapped 收集核对 shared accounting epoch，再检查所有用于恢复的复制结果；未进入设备初始化或复制失败时，不发布沿用前次 shadow 的文件。该 epoch 只证明本次初始化，不证明设备已停止；读取/释放仍依赖既有完成与 collector 边界。Mapped retained 仍按各 run 的 terminal bank 与 receipt 结算。

## 回归与当前验证

- 修改前两架构均复现 host admission 修改设备状态、无 bank 的设备入口不复位。
- 新的 skipped-entry 用例在加入 epoch 校验前失败：生成前次统计文件。
- 已完成首轮 C++ 380/380、Python 3525 passed /185 skipped、A3sim/A5sim 普通 sweep、两套 scope-stats 场景和 DFX-off 构建。
- 两轴复核发现 ordinary held-buffer D2H 未检查；删除 host adoption 后旧 shadow 可保留前次记录。新 detached-shadow transport 测试在两架构先失败：shared epoch=22 且真实 held count=0，失败复制却恢复出前次的一条记录并写文件。修复后复制失败立刻拒绝恢复/导出，解除故障后成功重试。该合成 transport 不表示 A3 SVM 常态会发生这次复制失败。

| 最终提交的验证 | 结果 |
| --- | --- |
| C++ 全量 | 382/382，包含新增两套 detached-shadow test target |
| Python 全量 | 3525 passed /185 skipped |
| A3sim 普通 sweep | 65 resource sessions 无失败；HBG L2 14 passed /7 skipped，TMR L2 31 passed /1 skipped |
| A5sim 普通 sweep | 61 resource sessions 无失败；HBG/TMR L2 16/30 passed |
| scope-stats 开启场景 | A3sim/A5sim 各1 passed；包含默认和低 thread case |
| DFX-off | A3sim/A5sim 两 runtime 构建通过，未据此宣称 off-mode 场景验收 |
| 工具/文档 | axis、全部 pre-commit、严格产品文档构建通过 |
| A3 板测 | task-submit 排队；尚无结果 |

两轴复核剩余 comment 契约已同步：host begin 不再有 re-stamp 失败/日志；device begin 在初始化 leader，close 仍在 orchestration 退出。没有修改历史报告的测试归属。

板测通过 task-submit 提交，任务 `task_20261021_234819_391627928975`。截至当前正在等待共享设备锁；没有绕过排队。脚本先验 public owned/borrowed 连续 ON→OFF→ON→ON 与 SDK 桩，再验 scope-stats 场景和非 SDMA/non-level4 普通 sweep。设备日志、二进制 hash、固定 basetemp、每约10秒队列/设备占用快照均使用独立路径；采样不能排除间隙内旁路占用。未在本地验证 A5 silicon。

不增加新公开 API、版本查询、PreparedCall 或第二执行器。不开放 capture DFX，不改变 HBG 的 scope-stats 能力。不声称修复未分类 AIV。与 #2633 初始化预热和 #2625 图 owner 分开记账。
