# Args-dump：普通调用的设备入口 accounting 与 lifetime transport

基于 #2663 合入后的 main `2522f02bfead6071a70603d0ccb35431ca03139b`。本地分支 `feat/device-args-dump-run-init`，[#2671](https://github.com/hw-native-sys/simpler/pull/2671)，Ready；当前 head `2894ac39cab86bdd5f101e9a60defb1d81e62c2e`，最终验证完成，待用户审核。此页是进度和证据，不进入产品代码 PR。

## 本笔交付

- Host ordinary admission 只清自己的窗口，不再改写三项 device counters。published/completed payload credit 和 lifetime drop counter 在 ordinary/retained 两种路径均保持单调。
- 设备的既有 dump_args_init 记录本次 `run_published_payload_base`，清本次 `run_dropped_record_count`，完成 lane/buffer 初始化后发布 `run_epoch`。三个字段占用 DumpBufferState 既有 padding；sizeof256，原字段 offset 不变。没有新增模式标签、公开 Worker API、版本查询或 PreparedCall。
- 普通 host credit = 本次基线 + 本次已写盘 payload 数。只有 epoch 匹配才归还额度；retained 仍消费 lifetime receipt/discard credits。普通 acknowledgement 的一次窄读由8字节变为48字节；retained 读仍为8字节，不据此宣称性能收益。
- 普通 leftover recovery 要求 lane/buffer 都属于本次 epoch、shared state 和 held buffer 的复制都成功。失败时跳过不可证明的恢复；已有 host-owned 记录仍按既有 best-effort 导出，retained 错误合同不变。
- dump-init 仍在既有设备初始化边界；未进入该边界时，由 host epoch 拒绝前次 accounting，不靠 host 清零伪造一次初始化。

这推进 C1 的实际 eager producer/consumer 协议，不宣称 capture DFX、图 source/module owner 或整个 C1–C4 已验收。#2633 的 init 预热改动未覆盖这套 accounting 机制；没有复制 #2625/#2626/#2646/#2647 的 capture/HBG 执行器。

## 回归与负对照

1. 修改前四个 arch/runtime 组合均复现：admission 改写设备 lane、第二次普通调用把 payload cursor 归零。
2. epoch 门控前的中间实现复现：调用92未进入 device init，恢复出旧调用91的 held record 并生成文件。它是该中间版本的证据，不冒充对已合入 #2663 的新缺陷证明。
3. 新 tests 覆盖三次普通 payload 提交，检查 lifetime publication/credit、未收到记录前不超前 ACK、实际 args.bin 内容，以及未初始化调用拒绝旧基线/记录。
4. Device init 测试从饱和 lifetime drops 和非零 credits 开始，验证只清本次 accounting；既有 timed refusal test 钉住本次 drop 增量。
5. 复制故障测试在两架构强制使用独立 host shadow，并分别拒绝 shared-state/held-buffer 复制。毒化 shadow 的 epoch 故意匹配本次，使复制结果检查不能被 epoch 检查代替。恢复复制后只导出本次 scalar42。
6. 两架构各五个独立临时变体：忽略 shared read rc、忽略 held read rc、删除 epoch 门控、丢失 payload baseline、丢失逐调用 drop 清零；共10个变体均被断言拦截，exit1。仅编译临时对象和链接产物，没有修改运行中的 SDK。

## 当前验证

- C++ UT 最终386/386；完整 Python UT3525 passed /192 skipped；pre-commit 和严格产品文档构建通过。
- A3sim/A5sim 普通 sweep 通过，分别65/61 resource sessions，包含两 runtime L2 组。TMR partial/hybrid/full 两模拟器共6项串行通过，A3 HBG full 通过。
- 两架构/两 runtime 的 DFX-off 独立构建通过，未覆盖正常 SDK。提交后 SDK 已重建。
- 初审 head 的 task-submit `task_20261022_034950_259325520765` 锁定4设备；A3 public args-dump4项、RTS publication probes6项与启用采集全部通过。完整 sweep 的首次 prepare 映射失败及状态分歧见下文，未宣称全绿。日志、源码/加载产物 hash、设备/队列采样独立保存。
- 最终 head 的 Claude 复核已 approve code；完整A3 sweep和最终CI已通过；A5硬件UT来自CI，仍不据此代签capture。

## 相关但独立的问题

1. 首轮启用采集 smoke 在同一 checkout 并发运行 A3sim/A5sim，出现期望输入5却读到42。真实 `build_output_prefix` 在同名/同秒产生同一路径，独立最小实验复现覆盖；六项串行运行在不修改 runtime 的情况下通过。原始失败与最小实验均保留，没有归因为设备/AIV。
2. 普通错误收尾调用 recovery 时没有消费 producer-stop proof，既有 quiesce 也不停止 ACK 线程。epoch 和成功复制只能证明数据身份，不能证明设备静止。本笔未修该前置条件缺口，没有声称可安全恢复正在运行的生产者；另记后续 proof-aware 错误边界。
3. 既有 PMU owned-HBG AIV 仍未分类，不由本笔绿色结果宣布修复。

公共 args-dump 测试扩为 owned/borrowed × 三个既有受支持组合（A3 TMR/HBG、A5 TMR），同时验证 OFF/PARTIAL/HYBRID/FULL 和跨调用非零唯一 epoch。A5 HBG 的既有 per-task args-dump unsupported 合同不变。

## 独立审阅与本轮失败

[Claude 初审](claude-review-7448ed4e5.md)认可完整协议交付，要求修正未启用 lane 误报、注释前置条件和 CI 定位；[处置记录](review-disposition.md)逐项维护。初始 CI A3 sustained-refill 断言失败，两条 overlap 边不成链；本地完整 sweep 的不同失败发生在首次 prepare 驱动映射 ret13/errno16，并已记录同一 physical device 的其他进程占用。两者分别诊断，当前没有完整 sweep/CI 绿色主张。

## 审阅修正与失败最小化

- **unused lane 误报**：host collector2 lanes、device init1 lane，初审版本在4个 arch/runtime target 全部失败；修复只对存在 held buffer/本次 drop 的旧 epoch lane 报错，仍拒绝错误归属。完整 C++386/386重新通过，注释明确普通恢复目前不检查 stop-proof。
- **CI 的 HBG sustained-refill**：真实日志显示四个结果正确，但两条 established edge为 `((1,0),(2,1))`、`((3,0),(4,1))`，不满足链式观测断言。在独立、未改源码的 main `2522f02bf`，仅限制2个CPU核，复现相同断言和同样两条边；4核臂通过。task-submit `task_20261022_041302_35814497607`，设备6。它证明既有测试依赖 host 时间窗口，不证明实现或测试问题已修复。
- **正常资源对照**：同设备6交替运行 main2522与初审候选7448，refill/outstanding-close各4次/树，共16项全部通过；不据此给出自发失败率或“不能回归”的统计结论。task-submit `task_20261022_040707_34307098614`。
- **本地驱动映射失败**：首次 prepare 的ret13/errno16日志与故障附近occupancy一并保存；physical1上可见非本次子进程的43GiB占用。支持竞争调查，尚不构成具体归因，也不是AIV。服务记录该任务exit1，客户端另记录daemon重启/137；两份状态均保留，没有当作普通通过。
- **final head2894**：SDK重新构建；Python3525/192、DFX-off与定向上板已通过，Claude复核approve；4设备完整普通sweep重跑和新CI已通过。初始CI失败不会被新head记录覆盖。

相关的 loss-accounting 未知值问题已经加入本地 KNOWN_ISSUES，并写入产品文档限制：已有host-owned数据可best-effort导出，但读取失败后的dropped_records0不能证明没有丢失。本笔未引入假“完整”标志。


## 最终独立复核

Claude 在最终head `2894ac39c` [复核通过代码](claude-recheck-2894ac39c.md)，合入仍以最终CI为门槛。新的测试cleanup小建议与NPU映射勘误见[处置](review-disposition.md)。首次失败、原始logger、main对照和CPU-affinity最小化已归档于本地 `probes/args-dump-run-accounting-20261022/raw-evidence/`，索引有SHA-256；raw archive不进入产品PR或设计备份分支。

## 最终验收记录：2894ac39c

| 范围 | 结果与边界 |
| --- | --- |
| C++ / Python | 386/386程序；3525passed /192skipped；pre-commit、严格文档构建通过 |
| 最终SDK / DFX-off | 提交后重建正常SDK；两架构、两runtime的DFX-off独立构建通过 |
| A3定向上板 | owned/borrowed args-dump4项、真实库RTS探针6项、TMR partial/hybrid/full和HBG full共4场景通过 |
| A3普通完整sweep | task-submit四设备，154resource sessions全部rc0；L2HBG43passed/1skip，TMR60passed；649.18秒，源码/加载产物hash前后一致 |
| A5硬件 | 最终CI ut-a5：86passed，含owned/borrowed args-dump2项，非本地silicon成绩 |
| 最终CI | run39769502567 attempt1；18个CheckRun success、1个deploy skip，另CodeRabbit status success，合计19success/1skip；最终head没有重跑或放宽预算 |
| 独立审查 | Claude最终复核approve代码；3个初审阻断已修正或完成main对照归因；全局测试cleanup小建议与既有错误边界仍单独记录 |
| GitHub反馈 | 最终head无待处理inline或review summary；CodeRabbit无可操作意见的正文固定于初审head7448，不能当作最终head独立审阅；最终全量范围由Claude复核覆盖 |

最终4设备NPU任务、原始日志和identity证据已保存在本地raw archive；文字报告和设计单独备份。初审失败与最终通过分别保留，最终通过不意味着既有AIV已修复。PR仍Ready，未合入；C1/C4和I1–I3总验收仍开放。
