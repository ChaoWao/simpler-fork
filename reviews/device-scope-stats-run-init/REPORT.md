# Scope-stats 设备入口初始化：C1 增量

本轮基于 main `fdcbdcc95`。初始生产功能验证提交 `949fb596c`，当时单提交 `d8f2696d0`（仅把新硬件测试的整数字面量改成布尔值，src/docs 树相同），单提交分支 `feat/device-scope-stats-run-init`。[PR #2663](https://github.com/hw-native-sys/simpler/pull/2663) 已移除 Draft；当前最终提交与 CI 状态以文末 2026-10-22 修订记录为准。下列初始 head/排队状态为历史证据。

## 功能边界

普通与 retained 模式均由 AICPU 唯一 leader 在 handshake/configuration 之前复位四项 producer accounting，并重新归属仍由 producer 持有的 buffer。Host admission 只清 host 窗口，不再读回 held pointer/seq、上传两项计数或 buffer header。队列游标与持有指针保持不变。

本次初始化 epoch 占用 ScopeStatsBufferState 既有 padding，总大小和原字段 offset 不变。普通/unmapped 收集在需要 producer 时核对 shared accounting epoch，再检查所有用于恢复的复制结果；未进入设备初始化或复制失败时，不发布沿用前次 shadow 的文件。该 epoch 只证明本次初始化，不证明设备已停止；读取/释放仍依赖既有完成与 collector 边界。Mapped retained 仍按各 run 的 terminal bank 与 receipt 结算。

## 回归与当前验证

- 修改前两架构均复现 host admission 修改设备状态、无 bank 的设备入口不复位。
- 新的 skipped-entry 用例在加入 epoch 校验前失败：生成前次统计文件。
- 已完成首轮 C++ 380/380、Python 3525 passed /185 skipped、A3sim/A5sim 普通 sweep、两套 scope-stats 场景和 DFX-off 构建。
- 两轴复核发现 ordinary held-buffer D2H 未检查；删除 host adoption 后旧 shadow 可保留前次记录。新 detached-shadow transport 测试在两架构先失败：shared epoch=22 且真实 held count=0，失败复制却恢复出前次的一条记录并写文件。修复后复制失败立刻拒绝恢复/导出，解除故障后成功重试。该合成 transport 不表示 A3 SVM 常态会发生这次复制失败。

| 初始提交的验证（历史） | 结果 |
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

以下为历史排队记录，后已取消：板测通过 task-submit 提交，任务 `task_20261022_000018_70153720238`。该快照当时正在等待共享设备锁；没有绕过排队。脚本先验 public owned/borrowed 连续 ON→OFF→ON→ON 与 SDK 桩，再验 scope-stats 场景和非 SDMA/non-level4 普通 sweep。设备日志、二进制 hash、固定 basetemp、每约10秒队列/设备占用快照均使用独立路径；采样不能排除间隙内旁路占用。未在本地验证 A5 silicon。

不增加新公开 API、版本查询、PreparedCall 或第二执行器。不开放 capture DFX，不改变 HBG 的 scope-stats 能力。不声称修复未分类 AIV。与 #2633 初始化预热和 #2625 图 owner 分开记账。


2026-10-22 更新：首个上板 task `task_20261021_234819_391627928975` 在新增测试设置 `CallConfig.enable_scope_stats` 时 TypeError，未到 kernel launch。原因是 nanobind 的 bool 属性拒绝 int；修为 `(True, False, True, True)`，直接配置烟测通过，pre-commit 通过。重建最终 extension 后重新排队，未把旧失败叫作设备异常或撤回。


## 2026-10-22：Claude 独立 review 与修订

按用户要求，用 Simpler 的 `review-pr 2663` 流程和 docs-kernel-mode 原则/ROADMAP 完成 [完整独立审查](claude-review-d8f2696d0.md)，再做 [修订复核](claude-recheck-0de1e010b.md)。不是复用作者的两轴结论。Claude 确认该 PR 是完整、当前生产路径实际消费的功能改动，而非一两行占位或孤立 seam。

该轮单提交 `a579b791a`（后续被下节替代），基于 main `fdcbdcc95`。修订使 disabled producer begin/flush/close 全部不写 resident pool/terminal，修正文档中 leader 与 host boundary 的责任，并加强跨调用 epoch 唯一性。完整处置见 [review-disposition](review-disposition.md)。

新增 OFF 测试在两架构旧代码上失败；分别删除三个 guard 的六个临时变体全部 exit1。修订源树的 C++382/382、scope suites 五轮 shuffle、Python3525/185、A3sim65/A5sim61 resource sessions、L2组、两套开启 scope-stats 的场景、DFX-off、axis、pre-commit 和严格文档均通过。新 head 只对 Claude 复核版本改一行文档；全量测试归其相同 src/tests 树，不把未完成的 CI 写成通过。

本地排队的重复 A3 retry 已在改源码前取消，无本地板测通过主张。d8f2696d0 的 CI 原始 job log 已核对四个新增实板组合、六个 publication probes；这些是旧 head 的证据。最终 head 的 CI 重新执行，结果待补。Draft 已移除以恢复 CodeRabbit 自动审查；用户交付仍等待该 head 的 CI 和评论收尾。

## 2026-10-22：HBG metadata 兼容修复与最终交付

当前单提交 `97aaed615d2f42b06f645275cfa3749434c04851`，基于 main `fdcbdcc9502195f7837030cf3c130f75f83d3719`。PR 已 Ready，尚未合入。上节 `a579b791a` 是历史审阅快照。

CodeRabbit 在 Ready 后指出有效兼容缺口：HBG 允许 scope-stats 开关，但既有 `CapacityMetadata.required=false` 表示没有 record producer，仍应输出 metadata-only 文件；无条件 epoch 门控错误拒绝了它。实际 public HBG Worker 在 A3sim 复现计算结果42正确、metadata 文件缺失。修复复用已有 capacity 合同，不增加模式枚举或状态布尔值：begin_run 当场消费 capacity；显式不需 producer 时期望 epoch0，未知或需要 producer 时仍严格要求本次 epoch。sim/onboard 普通入口和 retained-unmapped 均传既有 metadata；mapped terminal/receipt 策略不变。

A3sim/A5sim 的实际 public HBG 调用修复后均保留 metadata-only 文件。提交单测同时验证下一次未知 producer 仍拒绝 epoch0。原 inline 已回复并 resolve。公共实板用例扩成 TMR/HBG × A3/A5 × owned/borrowed，共8臂；TMR 要求 records 与不同非零 epoch，HBG 要求 metadata-only，OFF 不产生文件。

| 当前源树的本地验证 | 结果 |
| --- | --- |
| C++ UT | 382/382；scope suites shuffle 5轮通过 |
| Python UT | 3525 passed /189 skipped |
| 普通 sim sweep | A3sim65/A5sim61 resource sessions；两 runtime 的 L2组通过 |
| 开启 scope-stats 场景 | A3sim/A5sim 各1 passed |
| 其余 | 两架构 DFX-off、axis、pre-commit、strict docs 均通过 |

最终 head CI run [39747756140](https://github.com/hw-native-sys/simpler/actions/runs/39747756140) 的两个硬件 UT raw log 已逐项核对：8个公共矩阵臂全部 PASSED（[A3 job](https://github.com/hw-native-sys/simpler/actions/runs/39747756140/job/119722904748)、[A5 job](https://github.com/hw-native-sys/simpler/actions/runs/39747756140/job/119722904953)）。最终 CI 结论见下节；没有把旧 head 的成绩移挂。

[最后独立复核](claude-metadata-recheck-97aaed615.md)确认无剩余 Must/Should，认可本 PR 为完整生产功能。Claude 自建“始终严格”变体在两架构均拦到 HBG 兼容回归；“始终宽松”变体在两架构均被旧 skipped-entry 与新 producerless 后续严格调用测试拦截。它在最终 head 上独立完成 scope/admission 套件 shuffle×3。结论为 final-head CI 全绿后 approve。

### 最终 head 的 A5 排队超时

初次 A5 scene job `119722904791` 在 08:34:57 UTC 提交 `task_20261022_163457_2403885271`，08:57:01 才拿到四张卡（等待约22分04秒），09:05:24 因 `task-submit --timeout 1800` 返回等待超时；测试实际执行约8分23秒，日志截至截断仍在继续通过用例，未见断言失败/AIV 签名，且 daemon 提示任务仍在运行。该日志只能支持“排队耗尽外层等待预算”，不能宣称整个 sweep 通过。按原代码/预算仅重跑该 job，DFX smoke 在失败后被跳过也需要这次重跑完成。

### 最终验收结果（2026-10-22）

A5 仅重跑 job [119737947677](https://github.com/hw-native-sys/simpler/actions/runs/39747756140/job/119737947677)，同 head、预算未改：场景步骤4分20秒通过，HBG L2 29 passed /1 skipped、TMR L2 47 passed；随后各项 DFX smoke 也全部通过。该结果不抹去首轮排队超时记录。两个 task 均走 task-submit。

最终 run `39747756140` attempt2 为 completed/success，head 严格为 `97aaed615d2f42b06f645275cfa3749434c04851`。当前 Actions checks 18 success /1 expected deploy skipped，另 CodeRabbit status success（其含义是 incremental review skipped，不是一次新的代码批准）。没有失败或待运行检查。三处评论面重新拉取：1个 inline thread 已回复/resolve，3个 review body（另两份为空）、1个 bot conversation，无新增有效反馈或未解决线程。

PR Ready，保持1个提交，未合入。独立 Claude 最后审查无 Must/Should，其“CI 全绿后 approve”条件已满足。产品 PR 不含路线/规划资料；本报告及 HTML 单独备份到 fork 设计分支。
