# A5 HBG launch metadata 异步发布

## 范围与依赖

基线：2026-10-19，main `f50f9007620160e63a270ba83d2dd33fc656b7ad`（#2620 合入）。本笔消费同一 `queue_run_entry` / `RunCallerOrder` / `LaunchProgressSink`，不新增执行器、公开 kernel API 或 PreparedCall。

当前运行时唯一剩余的 pending metadata prerequisite 是 A5 HBG resident scheduler state。Graph Definitions 已通过 AICPU launch envelope 传值，不能把它写成还需要异步化的 device Definition 副本。

## 实际调用流程

```text
Program：设备与 tensor IO 管理 ─┐
                              ├→ 共同 native prepare
Kernel：借用 context / IO ────┘
  → HBG bind 生成 execution image、scheduler metadata、Graph section
  → stage_launch_metadata 按值保存 metadata（空 batch 清旧值）
  → bulk execution image 同步 H2D，仍位于 prepare，可与前驱执行重叠
  → prepare_runtime_args 预留 slot pinned 容量、snapshot descriptor
  → 共同 launch transaction
      caller Start → AICPU wait（若有 caller）
      → scheduler metadata async H2D（A5 resident 路径）
      → descriptor async H2D
      → Fork record → AICore wait
      → Core / CPU kernel
      → 原 completion / drain / retire
```

TMR 与 A3 HBG 当前没有 metadata prerequisite，因此本笔不为它们增加 DMA。仿真 HostApi 没有 staging callback 时保持原同步复制 fallback。

## 所有权与失败

- 调用方 host source 只需活到 stage API 返回；平台按值持有本次快照，不接管原容器。
- metadata 与 descriptor 的 DMA 源均为 slot-owned pinned allocation。descriptor 使用前缀，metadata 使用从完整 descriptor extent 后开始的后缀，彼此不覆盖。
- pinned 容量为 slot 服务过的最大 `descriptor extent + Σmetadata bytes`，仅在旧调用已退休后增长；总量为各 slot capacity 之和，不属于 device workspace 字节预算。allocation rounding 另计。
- 增长先释放退休的旧源；free 失败保留 owner，allocation 失败可重试。metadata 快照与 device destination 不因此丢失。
- 每个异步复制尝试在 RTS 调用前标记 sink。首个失败停止后续 metadata / descriptor / kernel；复制成功不等于 DMA 已完成，释放 helper 不释放 pinned 源。
- 沿用原 completion proof / bounded discharge / quarantine；没有完成证明时 fatal abandonment 不 free 源。

代价：A5 scheduler metadata 每次增加 owned host snapshot 和 pinned-source CPU copy；没有据此声称吞吐收益。bulk image 不搬到相邻 kernel 间的串行区，但 scheduler metadata DMA 本身已从 prepare 的重叠窗口移入执行流，可能增加调用间隙；尚无 A5 silicon 延迟 A/B，不能把总收益当已验证。

在初版 `006840e31` 上调用真实 `scheduler_plan_layout` 的纯 host 布局测量（仅大小，不是执行耗时；AIV task 数与 trace_count 均取 task 数）：

| task 数 | activity 关闭 metadata bytes | activity 开启 metadata bytes |
| --- | --- | --- |
| 1 | 280704 | 1167744 |
| 8 | 284288 | 1171328 |
| 128 | 347648 | 1234688 |
| 4096 | 2442752 | 3329792 |

每 slot pinned 请求在此基础上加 A5 HBG descriptor extent 14080 bytes，取历史峰值保留。原 scheduler host/device pair 仍存在；本笔没有减少它们的容量。

## 验证

PR：[ #2628](https://github.com/hw-native-sys/simpler/pull/2628)，最终 head `83ad697e97f12c340dbdda9cf25e25ba484bd97b`，单 commit，基于上述 main。产品 PR 只含代码、测试与已有产品文档；规划 HTML 与本报告单独备份。

最终修订的 C++ 全部 328/328（11.86 s）；三种 runtime-compiled host probes × 两架构 × 两 runtime 共 12/12。它们使用真实 onboard host library 与替换的 RTS，不执行设备 kernel，不是 A5 silicon 验收。

负对照：删除 metadata 的实际异步复制，4 个新增 helper 用例失败；恢复后 A5 HBG helper 23/23。覆盖源独立、metadata→descriptor 顺序、各复制位置失败、源容量增长失败与整体预检。

- 初版 `006840e31` 的 Linux 完整 Python UT：3415 passed / 135 skipped，290.88 s。
- 同一初版的完整 A2/A3 sim：61 Resource groups；L2 HBG 14 passed / 7 skipped，TMR 31 passed / 1 skipped。
- 同一初版的完整 A5 sim：57 Resource groups；L2 HBG 16 passed，TMR 30 passed。
- 最终 head 重建后，12 个 host probes 再次全部通过（含编译真实 A5 backend 的 DFX 两分支检查）；不把 A5-layout host probe 当作 A5 silicon 验收。
- A3 普通全量 task `task_20261019_015811_25250939447`：head `006840e31`，设备 8–11，6 个发布探针、58 个 native/public UT、151 个 Resource case、L2 HBG 43 passed / 1 skipped、L2 TMR 60 passed，全通过，未观察到 AIV。UTC 08:58–09:13 独立设备日志和持续占用采样保留。
- 任务首次 `task_20261019_015641_24639179581` 在测试前因脚本嵌套 `task-submit --list` 被拒绝，移出队列采样后重提成功；这不是 device kernel 失败。
- 最终修订 A3 任务 `task_20261019_021907_383755728171`：设备 8，6 个发布探针（29.29 s）和 8 组合公共 Worker（79.84 s）全部通过，退出码 0；独立设备日志与前后占用已保留，不把前一 sweep 改标成新 head。

没有把 #2620 的历史通过结果挂到本笔。macOS 与 A5 silicon 由本 PR CI 验证，当前未宣布完成。


## A5 DFX 消费者补正

审查发现 A5 drain 在 metadata 入队失败且没有 kernel 的 Partial 路径上，仍会调用 scheduler extension exporter。该区可能保留前一调用内容。本笔让两条 drain 分支都检查既有 `runtime_args_published()`；失败没有本次发布状态，成功状态则保留原导出。

回归不是只有 helper 状态测试：`run_caller_launch` 的 A5 版本直接编译生产 `device_runner.cpp`，用 strong leaf 观察私有 weak exporter，真实 launch/drain、publication readiness 和资源状态判断保持原代码。覆盖：发布成功／metadata 失败／descriptor 失败 × drain 正常／失败；正例必须导出一次，负例不得导出。测试使用 host RTS 模型，不是 device DFX 数据正确性的完整验收。

负对照：同一个已提交 probe 链接初版 `006840e31` 的 A5 backend，在 metadata failure 后实际调用 exporter，`metadata_extensions == (fault == 0 ? 1 : 0)` 断言失败；生产判断补正后两种 runtime 都通过。未为此添加新 flag、能力查询或额外生命周期对象。

修订仅修改 A5 onboard 的 DFX 消费判断及其测试、说明；A3 生产代码不变。完整 sweep 仍归属于 `006840e31`，最终 head 的检查单独记录。


CI：最终 head `83ad697e9` 已推送，检查仍在运行／排队；本地 A5 项只验证 host layout、真实 backend 分支与替换 RTS，未宣称 A5 silicon 或 macOS 已通过。结果以 PR 的同一 head 检查为准。
