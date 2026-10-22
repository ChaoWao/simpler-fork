# Kernel mode 实施路线

**设计判断先看 [Kernel mode 运行时设计原则](runtime-design-principles.md)。** 原则说明必须保证什么，本路线说明当前怎样分步交付；阶段计划不能替代所有权、顺序和语义证明。

**主线：Pipeline A 共同执行基础 → Kernel eager → Capture / Replay；消费者按 I1 PyPTO 适配 → I2 pypto-lib 真实用例 → I3 serving 请求逐层验收。** K1/K2 的共同 native/public eager 接线、S1/S2 与 S3 native module owner 已交付；完整 eager 资格和 C1–C4 继续闭合。四仓接管已纳入本路线的依赖图、总交付表与验收门；[#2658](https://github.com/hw-native-sys/simpler/issues/2658) 的 HBG × 普通 Async 系统验收作为 V1 并行推进。Pipeline A 后续后端与性能范围仍分别记账。

本页是实施顺序与交付状态的入口。阶段按能力划分，不与 PR 一一对应；具体协议见[共同执行设计](kernel-mode-prepare-submit-design.md)，可视化见[路线图 HTML](kernel-mode-roadmap.html)。

全路线历史核查：**2026-09-23，main `9ca91c522`**。该次读取主线源码、已合入 PR 和 Pipeline A 的批准合同；未重跑测试。历史测试仍归属于各自 head，不移挂到当前 main。下方历史实现/验证记录保留原基线；本节和交付表给出当前判断。

2026-09-25 设计修订：参数与复制按 [跨 endpoint 合同](endpoint-access-contract.md) 统一，当前计划以 [U1–U4](argument-domain-policy.md) 为准。此修订不重新核定全部 PR 合入状态；上述实现核查仍保留原日期。

2026-09-28 公开接口修订：正式 view 构造为 `Tensor(buffer, ...)`；`buffer.tensor(...)` 仅迁移期兼容。源侧 Buffer.wrap/分配/import 与目标 Worker 接入分离，由 submit 内部验证并绑定，同 backing 跨 Worker 身份不变。#2454 与 #2467 已交付各自范围；#2475 `959f84a9` 是 Worker 级借用构造的实现快照，公开入口须按此决定调整，不把它当最终接口。具体步骤见 [统一设计的 U1a–U4 步骤](argument-domain-policy.md) §6.1。这不是全路线源码状态的重新审计。

历史交付快照（当前状态见末尾更新）：#2478 的 native 部分保留与 re-borrow 修复已合入 main `3121da33`；#2475 已 rebase 至 `1c095ade`，剩 7 个 Python/测试/文档文件。正式 Tensor 构造独立为 [#2479](https://github.com/hw-native-sys/simpler/pull/2479)（已合入 main `7b614764b`）：原有 Tensor 类型接受 Buffer，兼容方法转发，共用几何校验；不分配/搬运/改变 identity。87 项 Buffer 测试、完整 Python 2774 passed / 55 skipped、两种模拟器各 4 项及 task-submit 板上 4 项通过。Buffer.wrap 源登记与 Worker 接入仍在 #2475 待实现，不能据此标记 U1b 完成。AIV 排查与接口推进独立：30 轮六用例测试及同一 Worker 的 18,000 次正常提交均通过，尚无稳定复现，未宣布修复。证据保留在 probes/aiv-borrowed-repro/REPORT.md。

历史进度（2026-09-29）：#2480 已合入；direct L2 source Buffer.wrap/自动接入实现为 [#2482](https://github.com/hw-native-sys/simpler/pull/2482) `a938898b`，最终完整单测 2833 passed / 65 skipped。当前 provider 仍是 Program Worker 的 live context；跨 Worker/Remote grant 与外部 RTS context attach 未交付，U1b/K1 不因此标记完成。


构造接口收尾：[ #2480](https://github.com/hw-native-sys/simpler/pull/2480)（已合入 main `4d0610fb`）统一 `shapes=` 与正规导入 `simpler.task_interface.Tensor`，迁移入门示例 21 处，使用绑定函数持有的弱引用做懒加载类型缓存。完整 Python 2815 passed / 63 skipped，10 个 L2/L3 模拟器用例通过；L4 远端真机未验证，本轮无空闲 HBM 设备，未运行板测。主线已统一为 `shapes=`。


## 1. 现在做什么

**Kernel 远端总跟踪：[Issue #2666](https://github.com/hw-native-sys/simpler/issues/2666)；仓库重点工作总览为 [#2670](https://github.com/hw-native-sys/simpler/issues/2670)。** 2026-10-22 已将本路线的设计约束、K2/C1–C4、I1–I3、V1、现有候选与最终验收刷新到该 Issue。Issue 作为远端执行入口，本页与 HTML 保留详细依赖和证据解释；设计判断仍以 principles 为准。后续状态同次刷新，不新建另一条按 PR 编号推进的路线。

**本轮源码核对基线：main `fa6528f4e`（#2671 合入）。** #2660 的 args-dump 选择和 #2624 的 TMR eager 已合入；此前各项测量仍保留各自原 head。#2625 capture 候选已审阅并评论：部分 enqueue 后 host 异常的所有权记账存在实测缺口，A3 cold registration 的缓存新鲜性另待证明，不能按候选存在宣布 capture 已验收。

**已合入 [#2663](https://github.com/hw-native-sys/simpler/pull/2663)（`2522f02bf`）：C1 scope-stats 设备入口复位。** 普通路径与 retained 路径共用 AICPU leader 初始化；移除 host admission 的 producer counter 写入和 held-buffer 读回/重盖章。保留 queue cursor、持有指针及 terminal bank 寿命；本次初始化 epoch 使用既有 padding，需要 producer 时跳过初始化或读回失败禁止导出前次统计；HBG 沿用既有 capacity 合同保留 metadata-only 输出。它与 #2633 冷预热、#2625 图 owner 并行，不开放 capture DFX。最终 head `97aaed615` 的 Claude 独立审查无剩余 Must/Should，CI 全绿（A5 首轮因排队耗尽等待预算，原代码单项重跑通过）；完整验证与边界见[本笔报告](reviews/device-scope-stats-run-init/REPORT.md)。

<a id="c1-args-dump-accounting"></a>

**已合入 [#2671](https://github.com/hw-native-sys/simpler/pull/2671)（main `fa6528f4e`）：普通 args-dump 的逐调用 accounting 与 lifetime transport。** Host admission 只清自己的窗口；设备 init 建立本次 baseline、drop counter 和 epoch，普通 ACK/恢复要求本次身份及成功读取，retained transport 合同保留。新字段使用既有 padding，DumpBufferState 仍256字节；普通ACK窄读由8增为48字节，不宣称性能收益。最终 head `2894ac39c`：C++386/386、Python3525/192；A3 task-submit 的154个resource sessions和两runtime L2组通过，源码/SDK hash不变；A5硬件UT在CI通过；最终CI **19 success /1 deploy skipped**（含CodeRabbit状态），Claude独立复核approve代码。修正了host闲置lane误报。初始CI的refill观测断言已在未修改main上以2核affinity复现，原始失败不抹除。它是已核实的C1协议交付，不另造执行器。普通错误恢复缺少stop-proof消费、manifest未知loss表达与输出同秒冲突分别记录，不宣布capture DFX或整个C1完成。完整证据见[报告](reviews/args-dump-run-accounting/REPORT.md)。

<a id="c1-args-dump-current"></a>

**当前 C1 交付：逐调用 args-dump 选择（[#2660](https://github.com/hw-native-sys/simpler/pull/2660) 已合入 `ef17033ba`）。** 本笔基于 main `3b5836465`（含 #2630/#2639）；把共享 pool header 的可变级别与同步读回移出 host admission，让本次级别随既有 descriptor 异步发布，六个 scheduler init 显式消费。HBG 使用一字 reserved，TMR 使用对齐空隙，四组合大小/握手地址不变；普通 counter reset 与 retained 队列/游标寿命保持原合同。沿用共同 executor，不重复现有 TMR/HBG capture 候选。初筛候选范围不等于全量审查完成；I1 映射仍是下一步。验证、负对照和边界见[本笔报告](reviews/invocation-local-args-dump/REPORT.md)。下方已合入记录保留各自基线。

**下一步（接管后统一安排）：先做双 runtime 候选审查，同时梳理 I1 的真实消费者入口。**

**2026-10-22 并行执行安排（原 #2666 内部拆分，不新建按 agent 划分的业务 Issue）：** 参考 multi-agent 原则的新 S3 主执行方案，由一个主 agent 负责验收、组合验证与交付；各子项使用隔离 worktree，completed 不直接视为 accepted。

| 工作项 | 依赖 / 写入边界 | 当前产物与验收 |
|---|---|---|
| 普通 args-dump stop proof | 主执行改 ordinary close 与两后端；子 agent 仅新增真实 runner/producer 回归 | 基线四 arch/runtime 组合中 proof controls 通过、unknown close 可复现违规恢复与 over-credit；[#2677](https://github.com/hw-native-sys/simpler/pull/2677) 合成head `d9808101e` 已集入stop-proof、late-publication退出与loss完整性；8个close/read-failure目标通过，完整重建/Claude/CI继续。首head19ed的C++390、Python3525/192与A3定向成绩不移挂 |
| ordinary manifest loss 完整性 | 子 agent 独立改统计与 manifest，保留 retained 合同；集成时核对与上一项的 collector 重叠 | [并行成果21537980b](reviews/args-dump-loss-accounting/REPORT.md) 已通过初轮组合验收：16目标160cases；独立审阅确认unknown manifest属于同一关闭行为，现整体集入#2677并重验，不另拆PR；未读取或读取失败不能将未知 loss 表示为已证明的零 |
| I1 实际入口与缺口映射 | 完成回归子项的 agent 转只读调查，不改已有 owner 的候选实现 | 已接受[静态入口报告](reviews/kernel-i1-entry-mapping/REPORT.md)：PyPTO main 已有 artifact/CallFrame；#3100 占初始化 owner，缺真实 queue callback→Worker.submit 接线；无新增上板成绩 |

输出目录冲突已有 #2655 output_root/path 工作，先对齐该 owner，不重复实现。上述 stop-proof、loss 项和回归最终组成同一个完整功能 PR：未知停机既不读取producer-held数据，也不输出已证明零丢失的假象。内部子项不机械对应 PR。无新增 WudiTask/Hub 或已部署执行锁的声明；硬件由主执行统一通过 task-submit 调度。


**本轮候选刷新仅核对 GitHub 元数据，未据此完成新 diff 审查：** #2626 仍为 Draft `f701832b`；HBG #2646 已更新为 `6cb08c73`，#2647 为 `4c0eacce`，均 Draft，须按新 head 全面审阅。#2625 当前公开 head 仍是 `7848e06e`；新 [#2669](https://github.com/hw-native-sys/simpler/issues/2669) 的 A5 fatal replay 超时发生在未公开的本地 restack 候选，尚未建立在未修改 main 或公开 PR head 上。该故障须在受影响候选验收前分类修复，不把它改写为当前 main 的已证实故障。


1. 以已合入 #2624 为 TMR eager 基线，对照共同 executor 继续闭合 #2625 的已评论问题，并审 shared resources #2626、HBG #2646/#2647：注册/参数、stream、完成、close 和 graph 引用。TMR 两笔已完成本轮审查；其余候选与 I1 映射尚未整体验收。
2. I1 从真实 compile/load 产物到 public Worker/native bridge 建立调用映射，核实旧 integration 的迁移与重叠。I2/I3 可并行准备真实输入、参考输出、KV/MTP 状态和请求测试；eager 与 capture 按各自 runtime 门槛接通，不等所有 capture 工作完成才做 eager 消费者。
3. V1 按 #2658 验收现有普通 Async 业务组合；它不等待 kernel capture。输入权限、长序列输出差异和性能归因独立闭合。

本路线统一维护上述顺序与退出条件；[接管说明](kernel-mode-cross-repo-handoff.md)保存来源、四仓固定快照、原作者联系人和候选归属。这里没有替其他作者认领实现，也没有将 OPEN PR 作为已验收能力。

**2026-10-21 / main `7503761a6`：#2619、#2620、#2628、#2600、#2638、#2640、#2645、#2643、#2588、#2649、#2648 与 #2653 已合入。** 共同 eager 已按 AICPU stream 顺序异步发布 descriptor 和 A5 HBG scheduler metadata；HBG bulk execution image 仍在 prepare 上传，以保留与前驱执行重叠。注册共用入口与失败初始化拒绝、PMU/DFX 资源回滚按各自 PR 范围消费，不把它们当作 capture 完成。

**已合入 [#2638](https://github.com/hw-native-sys/simpler/pull/2638)：S3 的实际模块保活。** 同一 `RuntimeModule` 由 ChipWorker binding 和每个预分配 native-run slot 共同持有；正常清理先结束 context/carrier，再退出最后的代码引用；失败 close 保留重试，终端 abandonment 保留到进程退出。沿用全部 C ABI，不增加版本/能力查询或另一套执行器。338 个 C++ 测试程序、3434 个 Python UT、两套定向 sim 与 task-submit 上板 22 项通过；具体矩阵和负对照见 [S3 当前交付](kernel-eager-open-work.md#s3-current-delivery)。合入提交 `2a6d2d50c`；graph 引用和 replay 仍由 C2–C4 验收。

**已合入 [#2640](https://github.com/hw-native-sys/simpler/pull/2640)（`63e651fcc`）：C1 的逐调用计时复位同流发布。** 启用 device-phase/task timing 时，prepare 只分配并保存复位值；launch 按 scheduler metadata → timing reset → descriptor → entry fork 排队。复用已有 slot pinned source，持有到完成/隔离；复位或 descriptor 入队失败阻止 kernel，保留 Partial，不读取前次计时。prepare 回滚清除未消费快照。两架构真实库探针、338 C++ 测试程序与 3434 Python UT 通过；本地 A3 task-submit 的 16 次计时/caller 调用通过；CI network1 与独立 main Daily 对照均在同一 peer 的 dispatcher 初始化失败，已登记 #2642，红项仍未解决，未据此宣布 capture 或全量 DFX 完成。具体边界见 [C1 当前交付](kernel-eager-open-work.md#c1-timing-publication)。#2624/#2625 的 TMR eager/capture、#2626 资源迁移和 #2633 collector prewarm 仍各自审阅，不重复它们的范围。

**已合入 [#2645](https://github.com/hw-native-sys/simpler/pull/2645)（`ed421bf33`）：结果区首次初始化同流发布。** cold slot 的 `published=0` 保存为八字节快照，launch 经已有 pinned source 在 AICPU stream 发布，再 fork 到两侧 kernel；未成功发布时拒绝读取该 allocation，即使其中恰好有本次 epoch 和 success。warm 复用不增加 marker 复制。TMR/HBG × 两架构真实库桩探针通过；338 C++、3434 Python、两套 sim 各 2 项和 task-submit A3 板测 34 项通过；负对照抓住 prepare 同步初始化与未初始化结果误读。这些数字保留原 head 归属，无本地 A5 silicon 或 capture 完成主张。见 [本笔边界](kernel-eager-open-work.md#c1-result-publication)。


**已合入 [#2649](https://github.com/hw-native-sys/simpler/pull/2649)（`0cf537df1`）：本次调用的 swimlane level。** prepare 将 `CallConfig` 的级别写入本次 descriptor；AICPU 显式接收该值，host admission 不再改共享 header。A5 HBG phase 存储规划、RunControl 和导出同取 descriptor。#2588 已让 HostApi 选择按调用快照，不再将此前的“查询前驱级别”当作当前 main 缺陷；本笔保留该快照和 PMU 布局逻辑，统一设备侧级别来源。字段使用原 padding/reserved，原偏移与大小不变；level 变化使既有前缀缓存失效并重发，没有独立 level H2D。head `e768fab28` 已 rebase #2588；C++ 378/378、Python 3501 passed；A3 task-submit 的 9 项实板与 6 项 RTS 桩探针、A5 的 6 项桩探针及 3 项 DFX sim 通过。最终 head CI 19 项成功、1 项预期跳过；macOS A5 首轮总 session 超时，未修改代码或预算，仅重跑后通过。旧 sweep 仍保留原基线，不代替本次集成验证。见 [本次记录](kernel-eager-open-work.md#c1-swimlane-invocation)。

```text
CallConfig level → 本次 Runtime descriptor → 稳定快照 / pinned async publish → AICPU init(level)
                       └→ A5 HBG phase layout / RunControl / 本次导出
host admission → 自己的 export level / pool 检查；record queue 所有权保持原协议
```

**已合入 [#2653](https://github.com/hw-native-sys/simpler/pull/2653)（`7503761a6`）：逐调用 PMU 选择与 device accounting 复位。** effective event group 从已有 `PreparedExecution.dfx` 的一次解析结果写入 descriptor，保留 `SIMPLER_PMU_EVENT_TYPE` 的既有覆盖语义；host 导出与设备使用相同快照。AICPU 显式接收该组并在 producer 启动前复位本次 counters，host admission 不再改共享 header 或清 device counters；retained 和普通收集共用此规则。两种 runtime、两架构、onboard/sim 和 A5 HBG resident 一起改，队列/terminal bank/driver lease 仍按现有寿命合同管理。它继续收口 C1 的实际设备输入，不代表 capture 完成；不重复现有 graph/launch PR。 最终 head `dcdb55ef2` 的两架构 DFX-off/on 修正已验收，CI 19 success / 1 deploy skipped；原单发 AIV 仍未分类。见[实施和证据](kernel-eager-open-work.md#c1-pmu-invocation)。

```text
CallConfig + 既有 PMU env override
  → PreparedExecution.dfx（只解析一次）
      ├→ collector 本次 frozen export 配置
      └→ Runtime.pmu_event_type → 稳定快照 / pinned async publish
           → AICPU init(group) → 本次 accounting reset → producer
共享 pool / queues / terminal banks / driver lease：仍各自保活到最后使用者
```

**四仓交接已纳入本路线。** 原 manager 目录计划移除；[接管入口](kernel-mode-cross-repo-handoff.md) / [HTML](kernel-mode-cross-repo-handoff.html)保存原提示词、Pipeline A/DFX 正文和证据索引。依赖见 §2，总交付见 §3，I1–I3/V1 的具体范围见 [§8 跨仓交付](#cross-repo-delivery)。原作者分支、自动化和未承诺任务不由文档迁移接管。

#2646/#2647 已有 HBG graph 模块与执行/capture 接线，分别依赖 #2626；与 #2624/#2625 的 TMR 工作一起审阅，不复制另一套实现。 #2624 已 squash 合入 `fdcbdcc95`；#2625 的本轮审阅固定于 `7848e06e`，不能把该 stacked snapshot 当作当前 main。#2626/#2646/#2647 当前均为 draft，描述的完整实现不能算主线已验收。#2605 vLLM 固定 eager 的两轮 16 请求短序列已通过，但 128-token 与参考输出不一致；K2c 业务资格不能据此标记完成。#2633 collector prewarm 独立推进；#2643 的 PMU driver lease 按其本身范围记账。

#2587 的环境/source 身份和 #2589 的 direct L2 原生 owner 已交付。#2589 验证 head `27098ab9` 的完整 17 项 CI 已通过；其合入提交为 `a8c682a32`，历史测量不改挂。#2579 的 HBG host/device surface 分离也已包含在此主线；它没有替代 eager/capture 的剩余接线。

[PR #2593](https://github.com/hw-native-sys/simpler/pull/2593) 和 [#2595](https://github.com/hw-native-sys/simpler/pull/2595) 已合入：child serve loop 保留控制句柄和本地 registry，direct L2 与 child 均直接持有原生 `_ChipWorker`，共用初始化。已合入的 **[PR #2597](https://github.com/hw-native-sys/simpler/pull/2597)** 将 direct L2、child 串行/暂存提交和兼容 `ChipWorker` 的描述符解析→POD 构造收口到一个私有函数。各入口的快照、授权/source 引用和 ImportRegistry 寿命仍各归原 owner。已合入的 #2599 交付 K2c 的公开 borrowed init/submit；不增加 PreparedCall、整次准备缓存或重复 submit。

已合入 #2599 的交付范围：同一 `Worker.init(borrowed_context_generation=...)` 与 `submit/run(..., caller_stream=...)` 接既有 native owner/lane；沿用 Buffer/Tensor/TaskArgs/RunHandle。stream/capture 与 H2D 请求在 Python 逻辑引用发布前拒绝；有效 borrowed close 保留 caller context/stream，失败 close 保留债务供重试。A3 上 TMR/HBG × owned/borrowed × SDK `TASK_QUEUE_ENABLE=0/1` 的 8 组合已通过；这不是框架后台发射队列证明，也不宣布 K2c 完整验收。修订 head `12304f0a` 的 CI 已完成：19 通过、1 跳过，Ubuntu/macOS UT 与 A3/A5 各 8 个公共组合均通过；评审线程为 0。实际框架 adapter、部分 enqueue/故障与 DFX 业务资格仍须按原门槛闭合。见[本轮证据](reviews/public-borrowed-worker/REPORT.md)。

已合入 [PR #2604](https://github.com/hw-native-sys/simpler/pull/2604) 的修复范围：TMR 先等 device orchestration 发布 final count，HBG 先等 runtime 初始化发布 host-built final count；final count=0 也是正常完成。两架构真实 cold-path 单测先失败后通过。公共调用沿用一个 DEVICE INOUT Buffer，交替空／写／空／写／空并检查未写区域；HBG 另外使用 HOST/NONE 控制 Tensor，submit 返回后修改源值不能改变本次决策。HBG borrowed 的 arena 首次 commit 后仍禁止增长，需先以足够大的非空调用建立容量；空调用先 commit 后再增长的缺口单独记录，不靠本次 completion 修复放宽。验证和边界见[当前验收记录](reviews/empty-eager-completion/REPORT.md)。框架后台发射、故障/DFX 和 capture 仍分别验收。

已合入 [PR #2606](https://github.com/hw-native-sys/simpler/pull/2606) 的 K2c 验收增量：同一 public Worker 的三次调用按 caller stream A → B → A 连接。每条边由 caller record/wait 事件和 D2D 复制承接前次 DEVICE 输出；后继输入先写入毒值，漏掉复制必须得到错误结果。全部 caller 结果在首次成功 RunHandle wait/poll 前读取；caller copy/event wait 在释放 raw allocation/event 前须另有完成证明。已有 #2605 负责固定形状框架接入，本笔不重复该 driver，也不据显式流链通过宣布后台发射队列合格。见[当前证据](reviews/public-caller-chain/REPORT.md)。

[PR #2607](https://github.com/hw-native-sys/simpler/pull/2607) 已关闭，未合入。其 version/size/flags 查询仅提前拒绝现有入口已能拒绝的操作，并重引入历史已删除的内部版本协商；测试通过不构成功能交付。S3 不再要求查询或版本协议，模块寿命按实际使用者推进。已合入 [PR #2608](https://github.com/hw-native-sys/simpler/pull/2608) 以 [borrowed eager arena 增长](reviews/borrowed-eager-arena-growth/REPORT.md) 闭合真实缺陷：空图 → 非空／更大图 → 缩小／空图，无预热，保持外部状态与 caller 顺序；分配失败不破坏旧 bank。

已合入 [PR #2610](https://github.com/hw-native-sys/simpler/pull/2610) 的 [borrowed workspace 预算接线](reviews/borrowed-workspace-budget/REPORT.md) 复用同一 manager、既有 `workspace_budget_bytes` 与 report：两种设备所有权策略都管理 simpler 内部 arena；可选预算限制内部四类 region，绝不计 caller tensor 为内部 workspace。实测拒绝路径暴露既有公开 native 异常 source 保留缺口：没有返回 RunHandle 时，close 不会释放这份未证实 source；当前保守保留，不能宣称立即可重试或可释放。后续须传递真实未提交／已完成证明，不能凭异常或 reset 推断。#2596 是按 endpoint 资源证明决定 admission 的已交付增量，不能据一笔合入宣布 Pipeline A 全路线已完成。

已合入的 [准备拒绝后的 source 退休](reviews/prepare-refusal-source-retirement/REPORT.md) 以本次 run_epoch 的实际清理结果跨 native/绑定/Python 传递：C ABI 保留原错误码，clean refusal 只释放本次引用；deferred wait 与兼容同步入口消费同一事实。缺失／旧证明、清理失败或部分入队继续保留，不能靠消息文本或 carrier 清零推断。证明不撤销 host 编排或已完成同步复制的副作用。#2612 已通过原 head 的完整 CI 并合入；本次 NotStarted 扩展仍须独立验收，不把异常分类本身当功能。

初版 `149c8997` 的 Linux 验证：完整 Python UT 3275 passed / 91 skipped；初版最终提交重建后 253 项相关测试通过；A2/A3 与 A5 通信 sim 4/3 场景；task-submit 上板 5 个通信场景加 1 个 HBG domain-cleanup/FIFO 场景通过。原始证据、失败注入边界和任务记录见[报告](reviews/chip-child-communicator/REPORT.md)。本笔不是完整板测 sweep，也不开放 public borrowed eager。此前 #2589 的[证据](reviews/direct-native-chip-worker/REPORT.md)继续保留原 head。

历史 #2593 review 修订基于 main `cc6c4d51`；本轮 API 源码图更新为 `e994e106d` 的固定审查快照。修订补全 SharedMemory 写入边界，并以精确大小/16 KiB 对齐的真实共享内存覆盖相同控制路径；macOS 结果单独看 CI，不把 Linux 通过当作跨平台验证。


本轮按入口、准备、launch、错误恢复和退休读取主线；没有重新宣称全路线验收。[核查与下一 PR 范围](reviews/kernel-eager-main-e0a5e9a.md)给出源码符号和测试门槛；[AIV 现场规划](aiv-evidence-plan.md)独立保留，不继续扩大主动复现矩阵。

**[PR #2557](https://github.com/hw-native-sys/simpler/pull/2557) 已合入 main `55eb52246`**（原审阅 head `26d8abc3`，验证基线 `600540064`）：本次执行持有实际 stream 绑定，Program 两架构消费；借用 context 出错不做全设备同步。公开 kernel launch 仍关闭。8 个真实库桩探针、313 个 C++ 目标、3039 个 Python 用例、两套普通 sim sweep 通过；单独叠加 #2536 DFX 的 A2/A3 常规全量上板通过，本轮无 AIV。这不是 kernel eager 已验收。见[交付与验证记录](reviews/run-execution-streams/REPORT.md)。

**[PR #2561](https://github.com/hw-native-sys/simpler/pull/2561) 已合入 `5f4f9525d`：A2/A3 cleanup 在 discharge 之后读取 poison。** 真实库 RTS 桩负对照在 TMR/HBG 均失败，A5 对照通过；旧值会在失败隔离之后仍选择正常退役分支。此修复不声称解决 AIV 或开放 kernel eager。见[验证记录](reviews/cleanup-wait-poison/REPORT.md)。#2536 DFX 也已合入（`86e77362`），后续板测直接使用主线诊断。

当前 **caller fork/join + native eager** 复用已有 native prepare/launch/poll/wait/finalize：A2/A3 仍使用带代码代际保护的 run stream pair，A5 借用 context 的私有双流。caller → AICPU → AICore 为入口链；AICPU 等 CoreDone 后才 record CpuDone，caller 等 CpuDone 后 record Tail。已有两份 kernel 完成事件继续唯一地证明执行完成，新增三个按 slot 懒分配事件只负责 caller 顺序与等待消费证明。

PR #2567 验证（原始 head 见记录）：真实 runner 桩四组合通过；A2/A3 TMR/HBG 各四次实际调用通过（caller H2D → launch → caller D2H，轮换两条 caller stream）。Worker 公开接线、context generation 消费、地址/handle 保留和完整业务验收还未交付。此处不把 native 冒烟通过写成 K2 完成。详见[本轮记录](reviews/kernel-caller-order/REPORT.md)。Pipeline A 的其他扩展仍可并行。

已合入的 #2572 沿用原 run handle、slot generation、borrow、join、poll/wait/finalize。带 caller 的 direct submit 在 admission/prepare 前拒绝 capture；可 join 就立即入队，否则 drain 前驱后普通 launch；失败在返回前抛出。Host output copy-back 仍需要 wait/finalize。普通无 caller 提交保持既有 prepared-successor 行为。该 PR 只交付内部共同适配层，测试使用 owned ChipWorker；已合入 #2578 将这条路径用于外部 RTS context。公开 Worker 仍未交付。见[本轮证据](reviews/chip-caller-submission/REPORT.md)。

先读[设计原则](runtime-design-principles.md)，再按下面的依赖实施。

| 工作线 | 当前下一步 | 完成后解锁什么 |
| --- | --- | --- |
| Pipeline A 共同基础 | #2411/#2419/#2426 的初始交付后，#2446 DEVICE、#2449 多 worker、#2481 A5 HBG、#2491 A5 TMR、#2514 受限泳道及 #2532 结果保留均已合入 | 按各自已批准范围消费；不要求三份容量等后续扩展先完成 |
| 我们当前推进 | **共同 public eager 已接通；先审双 runtime capture 候选与共同 owner，再按 I1 → I2 → I3 闭合消费者；V1 并行** | eager/capture 分开验收；实际框架资格、长序列正确性与跨仓安装组合仍待闭合；U2b/Remote 非前置 |
| API 模块收口 | **S3 module owner / 既有导出合同，可与 K2 并行，C2 前完成** | 图、callable 和 run 持有正确模块寿命；不另建执行器或全 API get_api 表 |
| Kernel eager 验收 | **caller 前序写入 → TMR/HBG kernel → caller 后序读取，保留错误与寿命合同** | K2 通过才标记可用；A 的 HOST staging 证明不能覆盖借用 Device tensor |
| Kernel eager 通过后 | **C1 设备闭包 → C2 图所有权 → C3 双 runtime replay → C4 完整验收** | 不重新执行 host 编排的、可正确重复运行的捕获图 |

**当前代码状态：** HBG publication（#2342 / #2353）与 descriptor seam（[#2358](https://github.com/hw-native-sys/simpler/pull/2358)）均已合入，不再列为本地待交付功能。#2360 的接口范围澄清也已合入 `c0d2c52b`。descriptor 发布保护 [#2367](https://github.com/hw-native-sys/simpler/pull/2367) 已合入 `eeedcaa4`，审阅 head `964d3a73`。两架构真实 launch 拒绝探针已进仓库并接入 onboard UT。完整回归、审阅补充验证与合入状态分别记录，规划材料仍留本地。

原 descriptor 本地提交 `e6cd39d4` → PR head `7220dd01` → merge `4e14364f`。代码与日志保留在[迁移验证](reviews/descriptor-main-0dbf12ae.md)。原本地分支和更早的 image 实验均为历史记录，不重复叠加。路线图、HTML 和规划证据继续只保存在本地。

S1 已以 `Runtime { dev; host_; }` 落地主线；S2 以该布局为起点，保留现有 host lease 接口。S2 不修改 A 的 lane / fence / drain，合并时与 A 共享的上下文、release 接口要对齐。

## 2. 依赖关系

```text
已交付 A + S1/S2 → K1/K2 共同 native/public eager 接线 → K2 完整资格
                         ├→ I1 eager → I2 eager → I3 eager 请求验收
                         └→ C1 → C2 → C3(TMR/HBG) → C4 runtime 完整验收
                                  ↑       └→ I1 capture → I2 capture → I3 capture 请求验收
                    S3 module owner（已交付；图长期引用由 C2 接入）

I1 的产物/入口映射、I2/I3 的输入/参考和测试准备可现在并行；
生产 capture 消费不得越过对应 runtime C1–C3 门槛，最终还须通过 C4。

已交付 A + 现有 Program/模型/serving 路径 → V1 HBG × 普通 Async (#2658)
                                                 （不依赖 C1–C4）
U1b → U2a → U2b Program IO ─┐
    └────────→ U3 Remote ────┴→ U4 统一关闭（按各消费者实际需要接入）
```


S1/S2 与 K1/K2 的共同 eager 生产接线已交付；K2 的实际框架/失败/DFX 完整资格仍待闭合。**C1 已有异步与逐调用 DFX 增量，不等于整个捕获闭包通过；生产 capture 接线须消费对应 eager 合同。** S3 native module owner 已交付，C2 接入图长期引用后再验收图寿命。I1 的 eager 资格回填 K2，capture 资格消费 C2/C3；I2/I3 按模式逐层消费。U2b/Remote 不整体阻塞 K2，V1 不以 kernel capture 作为前置。

不为 kernel 接入再要求 A 完成更深 mailbox、所有性能优化或 DFX 连续会话。A 可沿用诊断 run 的双向独占保护；最终 kernel/capture 的 DFX 能力仍须在 C4 完成交付，不能由这条过渡规则豁免。

## 3. 总交付表

| 阶段 | 可验收交付物 | 前置 | 当前状态 / 对应事项 | 退出条件 |
| --- | --- | --- | --- | --- |
| **A · 共同执行基础** | Program 的自然排队、逐 run 结果和安全退休 | Pipeline A 内部 P1/P2/P3 | **基础与后续受限扩展已交付**：初始 #2411/#2419/#2426，随后 #2446/#2449/#2481/#2491/#2514/#2532；范围以 Pipeline A 看板及对应合同为准，不能概括为所有模式均开放 | native 与 L3 均有两个真实 LAUNCHED，持续补位、错误归属和回收正确 |
| **S1 · device image 边界** | HBG 明确的 device descriptor；host tail 不上传 | 可独立推进 | **已验收**：#2309 缩短上传；[#2315](https://github.com/hw-native-sys/simpler/pull/2315) / [#2312](https://github.com/hw-native-sys/simpler/issues/2312) 的显式 descriptor 已合入；[验证记录](reviews/pr2315-26165696.md) | 两架构及 A5 legacy 的 reader / cache 范围正确；真实 onboard 验收 |
| **S2 · 单次准备／发布分离** | 本次构图产物留在执行上下文；metadata 发布可单独提交 | S1 的布局收口；与 A 并行 | **已完成同步合同验收；#2372 已合入 `767618b3`**；#2342 / #2353 的 HBG 发布、#2358 的公共 descriptor seam、#2367 的覆盖拒绝与 launch 检查。初始 seam 调用相邻；#2432 已将发布后移，#2438 仅跳过相同 TMR 配置前缀；异步保活另属 C1 | Program 仍每次构图，输出、get/set 副作用、错误、copy-back 与 release 不回退 |
| **S3 · 动态模块与能力合同** | module owner、既有导出与真实调用合同 | 已有 ChipWorker loader；可与 K2 并行 | **#2638 已合入 `2a6d2d50c`**：集中 loader 所有权与 typed lookup，context/slot 共持 module；保留既有导出合同。图的长期引用仍须由 C2 接入，不据本笔标记整阶段完成 | run/有效 callable/graph/replay 最后引用退出前不 unload；不预设查询或版本协商；C2 前完成 |
| **K1 · 借用环境与参数** | Kernel 接入共同执行层，无 tensor staging 和设备所有权 | A + S2 | **native 与 public Worker eager 接线已交付；完整资格分别验收**：#2501/#2507/#2510 均已合入。#2557/#2561/#2567 已接实际 stream、失败退休和共同 native caller launch；#2572/#2578 已接 lane/caller 与 borrowed ChipWorker；generation/source 已由 #2587 交付；public Worker 由 #2599 接入，#2604/#2606/#2608/#2610–#2618 补齐相应语义与寿命。U2b/Remote 非前置。旧 supported()=0、旧 kernel launch 仍拒绝。AIV 独立跟踪，详见 [当前记录](kernel-eager-open-work.md) | TMR/HBG device 地址参数直达执行；close 不 reset caller 设备；program 回归通过 |
| **K2 · caller 顺序与框架接入** | Native 和框架可用的 kernel eager | K1 | **生产接线已交付，完整资格待验收**：#2567/#2572/#2578、#2580–#2599 与后续语义修复接通共同 caller/环境/public Worker。I1 补实际框架调用；#2605 短序列通过但 128-token 输出不一致，不能据此关闭 K2。见 [剩余范围](kernel-eager-open-work.md) | caller 前序 → 双流执行 → caller 后序；TQ=0/1、保活、跨流及失败路径通过 |
| **C1 · 可捕获设备闭包** | 异步发布、独立握手复位、两侧完成、device 错误门 | K2 + S2 | **共同异步 descriptor/scheduler、#2640 timing reset、#2645 result marker、#2649 本次 swimlane level 与 #2653 逐调用 PMU/device accounting 复位已交付**；[#2660](https://github.com/hw-native-sys/simpler/pull/2660) 逐调用 args-dump 选择已合入 ef17033ba；[#2663](https://github.com/hw-native-sys/simpler/pull/2663) 的 scope-stats 入口复位已合入 `2522f02bf`；[#2671](https://github.com/hw-native-sys/simpler/pull/2671) 的普通 args-dump accounting 已合入 `fa6528f4e`，独立复核/最终CI与完整A3验证通过；独立握手 ABI、图闭包及完整设备错误门仍待验收 | 真实 executor 中提前发布不破坏前驱；无同步 metadata memcpy；失败不误放行 |
| **C2 · 图资源所有权** | capture wrapper、generation、bank/event/source 的图引用 | C1 + S3 | **未合入；已有候选实现**：#2625 TMR，#2626/#2646/#2647 共同资源与 HBG；须审查与共同 native owner 的一致性 | 每图 cold-path 预检；module/code 与 bank/event/source 均由图保活；失败/销毁无悬空引用 |
| **C3 · TMR 与 HBG replay** | 两种 runtime 的真实 executor 可重复运行 | C2 | **主线未交付；端到端候选 PR 未合入**：TMR #2625；A2/A3 HBG #2647（draft），A5 HBG 仍需单独资格化 | TMR 每次 reset/orchestrate；HBG 每次 restore；逐次独立结果正确 |
| **C4 · runtime 完整验收** | 换参、多图、DFX、超时/故障、关闭与容量完整行为 | C3 | **未完成；是必交付阶段** | 下文验收矩阵全通过，不能只以单图普通模式宣布完成 |
| **I1 · PyPTO 产物与框架适配** | 真实 compile/load → callable → 共同 Worker/native bridge；eager/capture 分别接线 | eager 消费 K1/K2 合同；capture 消费 C1–C3；契约映射可先做 | **待源码审查和验收**：PyPTO #2975 已合入；#3001/#3084 仍属旧 integration 候选；simpler #2493/#2583 须迁移/去重，#2605 eager 数值缺口仍在 | 产物/target/构建组合固定；caller 顺序、参数与句柄寿命、错误和关闭走同一 executor；实际框架矩阵通过 |
| **I2 · pypto-lib 真实用例** | 真实算子链/模型及显式 KV、权重、MTP 状态；每模式独立正确性 | 对应模式 I1 + runtime 已验收能力；数据/参考可先准备 | **待验收**：已接收仓库基线和业务任务，未完成真实调用链源码审计；无据此宣布新实现已交付 | changing-content、alias/INOUT、状态跨调用与长序列参考一致；capture 无中途 host 续发 |
| **I3 · serving 请求交付** | 实际请求入口、warmup/bucket、eager/capture 选择、关闭与可安装四仓组合 | 对应模式 I2；capture 最终还须 C4 | **待验收**：serving #274 是配置候选，不能当作端到端能力；现有 #2605 不是完整业务资格 | 固定四仓 SHA/依赖、平台/模式和真实请求结果；连续请求、取消/失败/close、正确性/吞吐/长尾均有证据 |
| **V1 · 普通 Async 系统验收** | HBG × 普通 Async 的四仓真实业务组合，复用现有 Program 路径 | 已交付 A + 现有模型/serving 路径；不等待 kernel capture | **待验收；对应 [#2658](https://github.com/hw-native-sys/simpler/issues/2658)**，不新建重复父任务 | 连续自动准入/clone/DEVICE 反馈正确；补齐输入/参考与长序列差异；同有效工作量、资源和数值质量下比较性能 |

上述阶段统一由 [#2666](https://github.com/hw-native-sys/simpler/issues/2666) 跟踪；本次未为每个阶段或 helper 新建子 Issue，独立归属的交付再分别建立并回链。[#2270](https://github.com/hw-native-sys/simpler/issues/2270) 已关闭，[#2285](https://github.com/hw-native-sys/simpler/pull/2285) 已关闭且未合入，不继续用其旧 reuse 验收代表 S2。

## 4. A：我们接收什么，不重复实现什么

Pipeline A 的[路线入口](references/pipeline-a/run-pipeline-track-a.html#roadmap)是其自身阶段和分工的依据。这里只定义后续 kernel 的接收门槛。

Pipeline A 的 manager 目录已交接并计划移除；来源冻结在本目录的 `references/pipeline-a/`，后续状态由本路线与[四仓接管说明](kernel-mode-cross-repo-handoff.md)维护。主线曾交接至 [#2429](https://github.com/hw-native-sys/simpler/issues/2429)；旧 manager 的跟踪/独立分析现作为冻结来源，后续由本路线维护。其历史[下一步建议](references/pipeline-a/review/mainline-independent-next-step.md)要求先闭合同域串行执行归属、A→device tensor→B→C 和提交/完成/退休边界。K1 接入应复用这套合同，不因 program/kernel 各有私有 stream 就推定跨入口有序；与接手方协调实现归属，避免平行 runner 或重复开发。Qwen 基线是业务证据，不替代设备链与寿命验收。

- **执行**：native 三道门和 L3 两道门都已接通。两个真实 LAUNCHED，不是一个执行加一个 prepared。
- **顺序**：保留父 run 的全部 dispatch 顺序，同 callable 的 host 构图串行；host get/set 的就绪和冲突合同不因 device FIFO 而消失。
- **完成**：逐 run 双流边界、业务终态、设备健康分别判定。正常 drain 不等待后继整条流；a5 HBG legacy 有明确覆盖或受限策略。
- **退休**：DMA、kernel、copy-back、outstanding event wait 的最后使用者分别记账。partial enqueue 和未证明静止的故障不能提前释放资源。

当前已合入的 fence、终态、输入／回拷／释放接口分别提供基础。**[#2298](https://github.com/hw-native-sys/simpler/pull/2298) 终态记录不等于 [#2267](https://github.com/hw-native-sys/simpler/issues/2267) 已完成，[#2303](https://github.com/hw-native-sys/simpler/pull/2303) callback owner 也不等于健康消费者已完成。** Kernel 继承这套状态和退休机制，不再建第二套 runner。

## 5. S1 / S2：现在与 A 并行的工作

### S1：device image 布局已收口

**已合入：[#2315](https://github.com/hw-native-sys/simpler/pull/2315)，提交 `2085182a`。** HBG 采用 `Runtime { DeviceRuntimeLaunchDesc dev; HostOnlyState host_; }`；device reader 使用 `dev`，复制与三处 cache invalidate 以 descriptor 大小为界，const/mutable tensor lease 接口保留。

#2309 已交付减少 **34,048 B/run** 的上传；#2315 将既有边界表达为明确类型，不能重复计算节省。合入前审阅 `26165696`：本地相关 C++ 用例 **44/44**；两架构 Runtime/上传大小、对齐和 11 个公开 device 字段偏移保持不变；GitHub A2/A3、A5 onboard 检查全部通过。详见[完整审阅](reviews/pr2315-26165696.md)。

对应原则 7/11/13：host-only 不进 device 描述、复制范围明确、owner 不变。本阶段保留每槽 handshake、prepare 上传时机与准入。`dev` 内仍混有参数、常量与握手，**S1 完成不代表共享 workspace、异步 publication 或 replay 已交付**；这些仍按后续协议分别推进。

### S2：一次调用内，先准备，再发布

> 本节保存 #2353–#2372 的同步合同与当时验收，不是当前上传路径清单。main `f50f90076` 已有 #2620 的 descriptor 异步 eager 发布；Graph Definitions 已随 launch envelope 传值。当前 #2628 进一步移动 A5 HBG scheduler metadata，bulk image 仍在 prepare。最新生产流程与验证以[本轮报告](reviews/hbg-async-launch-metadata/REPORT.md)为准；这些交付均未开放 capture/replay。

#### 已合入的同步接口及当前使用边界

| 内容 | 已合入实现 | 当前使用边界 |
| --- | --- | --- |
| HBG compact execution image | #2342：slot grow-only staging、host-only pending record、bind 后 publish | 源保活到同步复制返回 |
| Graph Definition / A5 resident scheduler | #2353：同一个 pending record，Definition → scheduler → image 发布 | device owner 不变；A5 legacy 无独立 scheduler region |
| TMR/HBG Runtime descriptor | #2358：`prepare_runtime_args` 建立值快照，`publish_runtime_args` 同步消费 | 唯一生产调用点紧邻调用；#2367 已加入独立 launch 发布状态检查，没有中间 Runtime 修改 |
| HBG trace 和失败 | owner 检查、逐项失败即停止、消费整条 pending record | 不允许覆盖未发布的 HBG bind |
| Descriptor 失败和寿命 | copy 错误清空 run view，保留 slot；source 成功／失败均消费 | pending 时拒绝 re-prepare，保留旧源和目的；两架构 launch 检查本次 Published 状态 |

**确定实现：一个 HBG pending publication，一次调用准备、一次发布。** prerequisites 按 Definition → resident scheduler 的顺序保存，最后才发布 execution image；没有 Definition 或 resident scheduler 时省略对应区域。记录可含借用的 slot source 和自行持有的 host source，两者都必须活到同步复制返回。准备失败清除部分记录；未消费的记录不能被 rebind 覆盖；失败发布不能重试为同一次成功调用。

#### 当前 program 调用链（均已合入）

```text
Program Worker / ChipRunLane（沿用主线 admission）
  → simpler_prepare_run
      → bind_callable_to_runtime_impl
          → tensor 输入 staging、host-view window、合法 get/set
          → HBG 每次 host orchestration
          → prepare Definition / A5 scheduler / compact image
          → 保存本次 pending publication，关闭 host-view
      → publish_run_image_impl
          → Definition H2D → A5 scheduler H2D → execution image H2D
          → 记录实际 GraphUpload / ArenaH2d → 消费 pending record
      → prepare_execution（以下两步紧邻执行）
          → prepare_runtime_args：slot 目的地址 + descriptor 值快照
          → publish_runtime_args：同步 descriptor H2D
  → 原有 launch / 完成判定 / copy-back / DFX / release
```

TMR 不做 HBG host 构图，其 `publish_run_image_impl` 仍是 no-op；公共 descriptor 快照／发布同时服务两种 runtime。TMR 的冷路径 arena 初始化不因这个 no-op 消失。

**#2358 是准备性的接口边界。** 当前 `init_runtime_args_with_metadata` 在 prepare 成功后立即 publish；没有生产中的晚修改窗口需要快照保护。单测修改 host Runtime 后仍上传旧值证明了接口性质，不证明生产正在利用该窗口。每次 host allocation 与 CPU copy 的开销已经发生；异步／capture 尚未获得收益。

S2 要求同步 source 存活到复制返回，以及未发布覆盖拒绝和基于本次发布结果的 launch 检查，#2367 已补齐后两项。K1 接入时可以使用已有内部接口，仍须保留全部 owner；C1 改成异步复制时才扩展 source 寿命到 DMA 完成。不得把 C1 的异步要求倒置为 S2 的退出门。prepare 的非空目的指针不等于已发布：#2367 对漏调用也明确拒绝。

Kernel eager 的目标是 adapter 经共同准备／发布层，再接 caller 的设备顺序；**这些改动没有接通 kernel launch**。C1 才把发布改成受支持的异步流内命令，C2 再建立图对 source 的长期引用；当前 slot loan 和同步 source owner 不能直接延长为 capture 寿命。

#### 已合入：descriptor 发布保护

[#2367](https://github.com/hw-native-sys/simpler/pull/2367)（合入 `eeedcaa4`）在现有 `KernelArgsHelper` 内记录 descriptor 状态：未发布时拒绝 re-prepare 且保留原源和目的；同步复制成功才可 launch；release、失败、fresh prepare 撤销旧成功状态。两架构 launch 入口在任何 stream 创建／DFX arming／kernel 入队前检查，拒绝保留 prepared owner，不 poison device。

本项只保护 descriptor，HBG image 和 late DFX 保留各自的返回码路径；上传时机仍在 prepare 内，不改变重叠或准入。它完成前述“未发布覆盖策略与 descriptor launch 检查”的主线实现，**没有交付异步源保活或独立调度窗口**；两者不是本次同步合同的性能收益。

C++ 163/163、helper 22 用例、ASan、两套 sim 和 A2/A3 onboard sweep 均通过；四种真实 host launch 入口的拒绝探针及移除 guard 的负对照也通过预期判据。本地未测 A5 真机；审阅 head `964d3a73` 的 [CI](https://github.com/hw-native-sys/simpler/actions/runs/35434816191) 已通过两架构 onboard UT、scene sweep、sim 与 packaging，不据此宣告 A5 legacy 专项验收。详见[本地实现及证据](reviews/publication-contract-main-2d366151.md)。

#### 验证归属与下一步

- **已合入 HBG**：#2353 的审阅 head `7fb6d1df` 通过 GitHub 两架构 onboard、sim、UT；最终 `a1b78b2a` 只补文档和 parser 注释，再合入 `0dbf12ae`。不把这份 CI 当成本地 descriptor 的验证。
- **历史双提交栈**：`fd5bd5a1` 的 C++ 161/161、两套 sim、A2/A3 onboard 等结果保留在[旧验证记录](reviews/s2-main-d49730f3.md)。它们不冒充这次基线的测试。
- **Descriptor 本地验证 `e6cd39d4`**：C++ **163/163**、生产 helper **12/12**（含 ASan）、两套 sim 和 A2/A3 onboard 正常 sweep 均通过。独立构建、故障／寿命测试和 program 回归见[本次记录](reviews/descriptor-main-0dbf12ae.md)。本机仅 A2/A3，历史本地结果未覆盖 A5 onboard；#2358 的 `7220dd01` 已通过两架构 onboard、sim、UT 的 CI。受控性能和 A5 legacy 的明确专项覆盖未由本页宣告。模拟器传 host Runtime 指针，不经过 onboard descriptor H2D，只能作为原 program 回归。

以下是 `eeedcaa4` 时的历史接收核查（当前状态见文首），见 [S2 / Pipeline A 接收记录](reviews/s2-pipeline-a-acceptance-eeedcaa4.md)。

| 接收项 | 当前结论 | 下一步归属 |
| --- | --- | --- |
| S2：HBG 单次准备／同步发布、descriptor gate | 生产机制已合入；本地补齐普通→GRAPH legacy→MIX legacy→普通及各发布位置失败后的恢复验证 | [#2372](https://github.com/hw-native-sys/simpler/pull/2372) 已合入 `767618b3`；同步合同收口 |
| A：正常 drain 不依赖整流 sync | `wait_run_fence` 仍调用 `sync_stream_pair`；#2354 仅记录健康证据 | P2 消费者、drain／退休接线与 A5 legacy 终态证明 |
| A：native／Worker 连续入队 | `active_native_run_` 与 `activate_fifo_head` 仍只允许单 active | P4 / P5；保持原工作线所有权 |
| K1 / K2：共同执行 adapter 与 caller 顺序 | kernel launch 仍为拒绝 stub | A 与 S2 验收后接通；不另造 runner |
| C1：异步 source 与设备复位闭包 | 未交付 | kernel eager 之后；不反向阻塞 S2 同步验收 |

本地提交 `7da788ef` 只改一个测试文件，166/166 C++ targets、两架构 HBG 48 用例和 ASan/leak 均通过；负对照证明能抓住漏登记 scheduler source 与漏写 legacy 模式。这组测试经 [#2372](https://github.com/hw-native-sys/simpler/pull/2372) 合入 `767618b3`，S2 同步验收完成。A 与 S2 均通过接收门槛后进入 K1；既不重做 HBG publication，也不把 host 发布验证当成 A5 legacy 的设备完成证明。

#### 开销与验收

- #2342 / #2353 的 compact image 继续复用 slot staging，不复制旧分支的大图 vector。
- 已合入的 Definition 借用既有 retained staging；A5 scheduler vector 转移所有权，避免第二次大块 host 拷贝。
- 公共 descriptor 为建立不可变边界，新增一次 **descriptor 大小的 CPU copy 和 host allocation / run**；没有减少 H2D 字节，也没有据此宣称加速。
- 保留每次 HBG 编排、tensor get/set 副作用、bank 绑定、输出回拷和原有 device allocation owner。发布失败不撤销之前的 tensor 副作用。
- 本地验证结果与限制见[descriptor 验证记录](reviews/descriptor-main-0dbf12ae.md)。旧分支的实验只作参考，不充当迁移后已通过的证据。

**DFX 计时口径：** #2353 的 `graph_upload` 仅计 publication copy；Definition 打包／绑定不在 `host_orch + graph_upload + arena_h2d` 中。三段之和是部分成本，旧新计时不能直接比较，更不能据此宣布 host 加速。公共 descriptor 仍在 `chip.run.prepare_execution` 范围内。

S2 同步合同退出要求 TMR/HBG program 回归、两架构的发布失败和同步 source 寿命正确，A5 host 发布覆盖 resident 与 legacy。设备 legacy 终态归 A/P2，异步 source 归 C1。性能只有在提出收益主张时另做同口径测量，不将它变成准备性 seam 的新增退出门。**S2 的同步边界完成，也不等于 kernel eager、共享 handshake ABI 或 capture 完成。**

## 6. K1 / K2：把 kernel eager 做完整

### 当前交付顺序（2026-10-15，融合 API 层次审查）

共同 native 执行、失败清理、caller lane 和 borrowed ChipWorker 已由 #2557/#2561/#2567/#2572/#2578 交付。#2580/#2581/#2582 已合入；#2584 已合入，完成汇聚校验入口和 native 内存保护；K2a 结项。

1. **K2a：共同拒绝与失败合同。** native/binding 保持可机器判断的环境拒绝分类，公开层消费同一判定；拒绝保留状态，真实设备失败发布终态，partial/unknown 保留。#2581 已替换 callback；#2582 已闭合 Python submit/注册/close 的状态承诺边界；#2584 已收口共同 lease 校验并补齐 native 内存保护，K2a 结项，下一步 K2b。
2. **K2b：环境/generation/source 与内部接入。** #2587 环境/source 与 #2589 direct L2 已合入；#2593 child 控制句柄已合入；PR #2595 原生调用已合入；#2597 已收口共同 materialization；已合入 #2599 交付 K2c 公开接线；已由 #2604 补齐零任务与持久 INOUT 语义，#2606 已验证显式跨 caller-stream 数据链；当前修复 borrowed HBG 的 eager arena 增长。目标仍是同一生命周期绑定 RTS identity、进程、context generation、source；收口 Worker → 原 `_ChipWorker`/lane 接入和共同 materialization。公开 ChipWorker 兼容；旧 init C ABI 留适配，不再造执行入口。
3. **K2c：公开调用与 eager 验收。** 同一 Worker.init/submit 接 owned/borrowed 和 caller 顺序；真实 public Buffer/Tensor/TaskArgs → RunHandle 通过 TMR/HBG、A2/A3/A5 的寿命、失败、跨流与 Program 回归；框架后台队列另须证明有序 native 发射。
4. **S3：可并行 module owner 与能力合同。** 按真实消费者集中 loader 与代码保活，不预设能力查询或版本协议；不迁移全 API 函数表。K2 可先消费现有 loader；C2 必须消费已验收的模块/代码保活。
5. **K2c 通过后进入 C1→C2→C3→C4。** C1 沿实际 producer/device reader 完成 ABI 与异步闭包；C2 同时接图资源与 S3 引用；两 runtime 的 replay 和完整产品矩阵继续必交付。

详细 PR 边界、拒绝矩阵与 R1–R7 的唯一阶段映射在 [kernel-eager-open-work](kernel-eager-open-work.md)。本页只维护全路线状态；[API 层次图](current-api-layers.html)生成相同映射，不另设一条重构路线。Buffer/IO/Remote façade 委托分别归 U1b/U2b/U3/U4；不做一次性 Worker 重写，不扩大 Pipeline A，不引入 PreparedCall/cache。

### K1：借用设备与 caller device 地址

**改哪里**：kernel 初始化/关闭入口、运行模式检查、参数绑定、共同执行层的 adapter。复用 [#2176](https://github.com/hw-native-sys/simpler/pull/2176)/#2177 的资源基础，按当前代码补缺口。

- Program 保留 DeviceOwner、tensor staging / copy-back 和 future；kernel 借用外部 context/device 和 tensor 地址。两者进入同一内部提交层，不要求 program 绕经 public kernel API。
- 内部 stream、arena、metadata 由 runtime 管理；caller 不必逐项分配。Kernel 不接管设备 reset，不在析构时隐式 teardown。
- 每次调用重新准备并提交一次。注册、容量和代码 generation 在冷路径建立；旧引用仍在时不原地销毁。
- PyPTO→simpler host 数据按单次 API 值语义交付，返回后不借读调用方 host 容器；HOST/NONE 控制 Tensor 与 Program H2D 参数绑定分开。Callable 通过 handle 持有，共用注册/显式 unregister 生命周期；有效 handle 与未退休调用不得自动淘汰。
- HBG host 构图只能读取合法的 host 控制信息，不能解引用 caller device 地址。依赖 host get/set 的 callable 必须有明确的 kernel 参数合同；不满足时在发布前拒绝，不能偷偷搬 tensor。原 program 的合法 get/set 能力保留。

退出条件：原 host 控制容器在 API 返回后修改／销毁不影响已提交执行；两 runtime、两架构的 device 参数路径正确；输出地址由 caller 提供并正确写入；无额外 tensor H2D/D2H；错误输入不留下半发布调用；显式 close / 失败保留资源不影响 caller 环境。

#### 当前并行工作：统一已有参数合同

2026-09-27 确定对外只有 Buffer 与 Tensor；`AddressSpace` 仅 HOST/DEVICE，本次 transfer 在 TaskArgs 参数项；不新增 Tensor/ChipTensor 权限字段，复用 Buffer/import grant 与 tag/signature；Remote 与 H2D/D2H 共用源/目的、完成与使用保留。#2444 `1761d130` 是混合枚举的历史实现，不能继续作为目标；保留已有用例迁移并按 U1 修订。

当前顺序：已交付的统一参数与 #2501 HOST 消费 → K1 借用设备的共同调用准备 → caller 顺序与 kernel eager 验收。U2b Program IO 记录整理暂缓，U3 Remote 与后续 U4 仍独立跟踪；它们不作为本次 kernel 接线的前置。每条生产复制路径必须当场闭合寿命，不能留待 U4。详见 [统一设计](argument-domain-policy.md)、[复制细节](endpoint-access-contract.md)、[参数 HTML](simpler-argument-design.html)。

L2 复用已有 Worker 的 TaskArgs<Tensor> → materialize 路径；外部指针在源侧包装成借用 Buffer，以 Tensor(buffer, ...) 建 view，Worker 在 submit 内检查接入。ChipTensor 退为内部 ABI/兼容适配，不增加第二套公开构造合同。先贯通参数项请求与全调用预检，再交付 HOST 消费、HBG/TMR 的共同绑定及 IO 记录。授权关联留在本次绑定，详细范围和验收见统一设计 §6.1。

K1/K2 消费 U1/U2 中所需能力，不等待全部 Remote 后端。Capture 的图保留依赖 C2，普通 host RPC 不因统一接口就变得可捕获。以上为设计计划，不宣称实现/验收已完成。


## 7. C1 / C2：建立可重放的命令与资源合同

### C1：发布、复位、执行形成设备闭包

**改哪里**：host serializer / enqueue、Runtime device ABI、AICPU/AICore reader 与 reset/cache 协议。沿用 A 的结果与健康通道，增加 replay 所需的 device 身份和错误门。

确定的目标布局：默认两个 publication bank；独立、按 cache line 隔离的 handshake；可共享 scratch 一份；reset 范围外的 DomainControl。HBG 逐调用 image/heap 仍按真实内容隔离，不把它们都当共享 scratch。

```text
publication：Root + 本图 bank 上次 SerialTail（若有）→ async publish → Ready
caller：    前次 SerialTail + 本次 Ready → fault gate / reset → Start
AICPU：     wait Start → AicoreStart → launch CPU → wait AicoreDone → AicpuDone
AICore：    wait AicoreStart → launch Core → AicoreDone
caller：    wait AicpuDone → SerialTail
```

沿用主线五事件链 caller ↔ AICPU ↔ AICore；AicoreStart 必须在 AICPU launch 之前 record。PrepareTail 已删除，prepare 不发布捕获外 tail。

先交付布局与两侧 reset/read 协议，再接异步发布和闭包；各步独立回归，不把整个 capture 功能塞进同一 PR。Reset 必须早于本次 AICore startup report，不能放到较晚启动的 AICPU 中擦掉该报告。

退出条件：实际 executor 的 B 发布与 A 执行重叠而不覆盖握手；两侧读取相同参数版本；检查每次异步 API 返回值。同步 memcpy 被拒绝（原语观测 107030）时不能继续生成缺失 publication 的图。受控 fault 在 Done 前发布，后继不得执行错误输入；挂死/后端异步错误由继承的健康和有界超时处理，不能指望卡在 Done 后面的 fault gate 自救。

### C2：graph owner 与捕获局部依赖

**改哪里**：capture wrapper、资源 generation、bank/event 引用和关闭路径。Cold path 做注册、容量预留、必要 host 工作；图保留不可变 source、code、Definition、bank、event 和 tensor 使用引用。

每张图 capture 前检查执行域静止；建立本图 Root 与 `last_use[bank]`。本图首次使用 bank 只依赖 Root，不等待捕获外旧 Done；后续使用等待本图上次 Done。这里的 Done 指五事件链的 SerialTail；所有侧流在 CaptureEnd 前按链式依赖 join。Replay 的 Root 通过执行域顺序排在前图/eager 完整完成后。

退出条件：捕获内无隐式扩容、注册和同步发布；每张图首次 bank 使用合法；原 host 临时对象释放后 replay 仍有有效 source；捕获失败、图销毁、换 generation、destroy/free 失败和未完成执行均有正确的 owner。Graph owner 不等于可重复 submit 的 host 准备对象。

## 8. C3 / C4：TMR 和 HBG 都必须走到终点

### C3：真实 executor replay

| runtime | Replay 每次做什么 | 必须证明的性质 |
| --- | --- | --- |
| TMR | 发布本次输入 → reset scheduler/runtime → device orchestration → 执行 | 上次 task/ring/error 状态不污染本次；不回到 host 重编排 |
| HBG | 发布/恢复 pristine image、必要 heap 内容 → attach → 执行 | 恢复所有被消费的状态；bank 地址、rebase、内嵌 descriptor、保留 slice 正确 |

HBG 一次调用必须在提交前形成完整可执行计划，不在中途回 host 依赖设备结果后再续发；业务确需 host 决策时由调用方显式拆调用。MTP 的持久状态作为外部 INOUT 保留，不能随 workspace reset。

两条实现可在 C2 之后并行，最终共同验收。HBG eager 的 host 每次填充 bank 本来就正确；capture 缺的是 replay restore，不能从 eager 正确推定 replay 正确。C3 先交付固定地址绑定的完整循环，再进入 C4 的图更新与多图集成；后者仍是确定方案的一部分。

退出条件：同地址输入内容持续变化，TMR/HBG 每次结果独立校验；重复 replay 后 host orchestration 计数不增加；不同大小/拓扑、cold/warm、bank 轮转和失败路径均正确。普通算子探针通过不替代真实 executor。

### C4：完整能力的交付门

| 验收组 | 交付与退出条件 |
| --- | --- |
| 显式换参 | 图外不可变版本 → 稳定 device slot → 图内 slot 到 bank；HBG 重建必要内嵌参数/拓扑。整批预检预算；部分更新失败时图不能 replay |
| 多图与定序 | 多 bucket、不同节点数、少于 bank 数、非整倍数、图/eager 交替、跨流显式桥接、局部销毁和重捕；逐次结果正确 |
| DFX | 接续 A 的 collector 身份/保活；device 每次执行产生独立序号，replay 不靠 host run_id 自增。验证开关切换、PMU、swimlane、args_dump、dep-gen 的已声明输出语义；不在 replay 重跑 host-only 导出逻辑 |
| 故障与超时 | fault-before-Done、后继拒绝、event record 失败、挂死及迟到通知。沿用 RuntimeTimeoutConfig 的有效超时层级，合法排队时间不能当作执行超时；不增设独立随意 deadline |
| 容量与关闭 | publication banks、图槽、host/device 版本、staging、events 分别计费。超限在发布前拒绝；最后 DMA / kernel / graph / consumer 使用者完成后才回收；close 失败可按所有权合同处理 |
| 实际收益 | 对比 program / kernel eager / replay 的结果、host 时间、真实 DMA 字节/区间、设备空档和完整吞吐/长尾；不把插桩合入当作性能提升 |

默认两个 bank 的提前发布窗口只有 **B−1 = 1**；加大 host 在途数不会增加该窗口。图事件先采用每图独占的明确预算 `1 + 6 × 节点数`（五个执行链事件 + 每节点 Ready + 每图 Root），再加 eager/adapter 的同时存活事件；本路线不以未经完整验收的跨图 event 共享作为正确性基础。

<a id="cross-repo-delivery"></a>

### I1–I3：四仓消费者是完整交付的一部分

**运行时完成与业务可用分别签收。** C4 负责 runtime 完整语义；对外声明某平台某模式可用，还必须有该模式 I1 → I2 → I3 的真实证据。eager 可先闭合自己的消费者，不等待 capture；capture 最终必须同时通过对应 C1–C4 和 I1–I3。TMR/HBG、A2/A3/A5 分别列结果，不能由一个组合外推另一组合。

| 阶段 | 必做工作与边界 | 验收留下什么 |
| --- | --- | --- |
| I1 · PyPTO 适配 | 找到真实产物 schema/target、load/register、参数转换和调用文件；沿用一个 Worker/Buffer/Tensor/TaskArgs/共同 executor。核实 #3001/#3084 重叠、#2493 DFX ABI 与 #2583 prefetch 的实际依赖；它们不自动成为所有路径前置。不得用 dedicated launch 或能力查询候选覆盖既定原则 | 各仓 commit 与构建依赖、源码符号到调用链的映射、真实 adapter 的 eager/capture 分开测试。覆盖 generation、caller stream、capture 拒绝、partial/unknown failure 保留和 close；I1 eager 结果回填 K2 框架资格 |
| I2 · pypto-lib | 选择真实链式算子/模型，记录 HOST 控制值、DEVICE 输入/输出、alias 与显式持久 INOUT；覆盖权重/KV/MTP。HBG eager 每次 fresh host 准备，capture 每次 device restore，禁止一次 kernel 内回 host 决策续发 | 可重复输入/参考、长短序列逐次对比、changing-content 和状态延续结果；capture 中间临时 host 容器退出后仍正确，多次 replay 不增加 host orchestration 次数 |
| I3 · serving | 把实际请求/调度入口接到已验收 I2；核实模式选择、warmup、bucket/cache 身份、shape 变化和资源保活。#274 配置与 #2605 driver 只是候选/局部证据，不是请求交付 | 可安装/复现的四仓固定组合、平台/模式支持表、连续请求及取消/错误/关闭测试；正确性与性能分开报告，记录真实请求数、序列长度、输入来源、资源和质量条件 |

**现在能并行做：** I1 的源码/契约映射、I2 的真实输入与数值参考、I3 的请求测试和版本组合清单。实际发起对应 capture 前必须先具备 runtime 的可捕获闭包与图 owner；这些准备工作不授权绕过生产前置。现有 PR 原作者仅是联系人，本路线不替他们承诺上述新增工作。

### V1：HBG × 普通 Async 系统验收，与 capture 分开推进

对应 [#2658](https://github.com/hw-native-sys/simpler/issues/2658)，消费已交付 Pipeline A 和现有 Program/模型/服务路径，**不等待 I1 kernel adapter 或 C1–C4**。沿用现有 Qwen/driver 和自动资源准入，不恢复多套公开 depth 旋钮，也不另造 kernel 执行器。

1. 固定四仓版本、真实输入权限与参考来源；长序列数值差异先分类并解决，不能只报短序列通过。
2. 验证连续 refill、clone、DEVICE 反馈和跨调用状态；记录提交/执行/完成/退休边界与输出，不能用 prepared-successor 数量充当实际 LAUNCHED。
3. 同有效工作量、输入/序列、数值质量与资源条件比较同步/普通 Async；按真实 host/DMA/device 区间解释吞吐与长尾。性能目标仍是待验收目标。

V1 与 I2/I3 可共用数据、请求驱动和证据，但结果必须注明资源模式/执行方式/平台；普通 Async 通过不覆盖 kernel eager 或 replay，反之亦然。

## 9. 已有工作怎样计入进度

| 已有事项 | 为本路线提供什么 | 还不能据此宣称什么 |
| --- | --- | --- |
| [#2176](https://github.com/hw-native-sys/simpler/pull/2176) / [#2177](https://github.com/hw-native-sys/simpler/pull/2177) 已合入 | 持久资源与 kernel 初始化的部分基础 | K1/K2 已完成，或已有 capture |
| [#2277](https://github.com/hw-native-sys/simpler/pull/2277) 已合入 | context-owned 寄存器表 | 所有 DFX 字段都可冻结。旧设计里 PMU 表逐 run 分配的代码事实已过时 |
| [#2297](https://github.com/hw-native-sys/simpler/pull/2297) 已合入 | AICore 参数改为 launch 传值 | 所指 Runtime/image 可提前释放。旧“更新 device KernelArgs 就能改 AICore 参数”的论证不能沿用 |
| [#2295](https://github.com/hw-native-sys/simpler/pull/2295) 已合入 | 输入 staging、输出回拷、binding release 的独立接口 | S2 metadata 发布已抽离，或正常 drain 已去掉整流同步 |
| [#2298](https://github.com/hw-native-sys/simpler/pull/2298) 已合入 | 每 run 终态，当前与同步结果旁路核对 | A/P2 完成 |
| [#2307](https://github.com/hw-native-sys/simpler/pull/2307) 已合入 | prepare_execution 直接计时 | 已得到新基线或已有收益 |
| [#2309](https://github.com/hw-native-sys/simpler/pull/2309) 已合入；[#2315](https://github.com/hw-native-sys/simpler/pull/2315) 已合入 | HBG 缩短复制与显式 descriptor 均已完成 | shared handshake、async publication 或 replay restore 已完成 |
| [#2293](https://github.com/hw-native-sys/simpler/pull/2293) / [#2313](https://github.com/hw-native-sys/simpler/pull/2313) 已合入 | 部分 DFX 身份与 swimlane buffer 保留/归还 | C4 连续采集和 replay 身份验收已完成 |
| [#2303](https://github.com/hw-native-sys/simpler/pull/2303) / [#2321](https://github.com/hw-native-sys/simpler/pull/2321) 已在固定 main | 进程级 fault callback owner；dep-gen 按 run 归属及保留未发布 buffer | 无整流同步的健康消费者、capture DFX 已交付 |
| [#2270](https://github.com/hw-native-sys/simpler/issues/2270) CLOSED；[#2285](https://github.com/hw-native-sys/simpler/pull/2285) CLOSED 未合入 | 旧实验与反例可作证据 | 可复用 PreparedCall 是当前交付目标 |

## 10. 如何维护这份路线

- 状态只有“待实现 / 部分完成 / 待验收 / 已验收”；PR 的 OPEN / MERGED 是另一个维度。完成阶段时填写实际验收证据，不只换一个颜色。
- 一份实现 PR 交付一个可回归的边界。准入、借用参数、device ABI、capture owner、replay restore 分开提交；跨 host/device 必须一起改变的协议读写两侧放在同一个完整改动中。
- 路线从能力出发，现有 Issue/PR 仅作映射；没有匹配项时再立对应范围，不能为已有 PR 恢复已撤销的设计。
- 本 Markdown 是路线内容源，HTML 与它同次更新；协议解释留在设计正文，历史论证和撤回结论留在实验/审阅记录。

**最近的交付顺序：共同 eager 接线已交付 → 双 runtime 候选审查和 I1 入口映射 → 闭合 K2 完整资格与 C1/C2 → C3（TMR/HBG）→ C4；消费者按模式接通 I1 → I2 → I3。V1（#2658 普通 Async）独立并行。**


### 2026-09-29 合入与构建阻塞更新

#2482 已合入 main `fcbf4f1d`，源登记/自动接入的已验证 head 仍为 `a938898b`，不把历史测试移挂到新主线。#2475 已关闭并由它替代。下一项仍是 U1b 的 ChipWorker 公共参数入口收口；跨 Worker/Remote grant 和外部 RTS context attach 尚未交付。

推进时发现 main `92a14e4e7`（#2483）无法构建 A5：boundary-wait discharge 错误分支仍调用单参数 `emit_device_dep_gen_graph`。先提交独立 [#2484](https://github.com/hw-native-sys/simpler/pull/2484) `82af70f5`，补本次 epoch 与 false 完成判定。完整 editable/runtime 构建和三个 A5 DepGen C++ 单测目标通过；本机为 A2/A3，没有 A5 板测。此 PR 只修构建，未交付新的 TaskArgs 接口；参数入口测试草稿留在本地，尚未通过验证。


### ChipWorker 共同参数入口：#2486 已合入

构建修复 #2484 已合入 main `8aae172e5`。[#2486](https://github.com/hw-native-sys/simpler/pull/2486) 已合入 main `22c009811`；验证 head 为 `cacc1415`。它使公开同步 `ChipWorker.run` 接受同一 `TaskArgs<Tensor>`，复用快照、ImportRegistry 和 native POD materializer，不引入第二种 Tensor 或执行器。

- 接受标量和 canonical row-major HOST/H2D 视图，包含非零 offset；DEVICE descriptor 仍交给拥有 live-source/provenance 合同的 `Worker(level=2).submit`。HOST/NONE 与非连续 H2D 在 import 前拒绝。旧 POD 入口保留兼容。
- 本次导入映射持续到 native 返回及回写完成；bind 失败立即清理，native 调用失败保留并阻止新的公开 run，只有 native finalize 成功后清理，失败则继续保留。公开 run/finalize 串行。此合同只保留映射；调用者仍须保持 backing allocation 有效，不传递 host allocation 所有权。
- 验证 head `cacc1415`：完整构建；Python 2853 passed / 65 skipped；HBG 两架构模拟器各 7 passed，其中新用例经真实 ChipWorker.run 连续提交 6 次。主线旧入口、去快照、失败提前 unmap 三种负对照均命中测试。板测未运行：架构是 A2/A3，但查询时 16 张卡全部有 HBM 占用，空 task-submit 队列不代表设备空闲。
- U1b 不标全部完成；跨消费者/grant、外部 RTS attach、旧公开 POD 的余下迁移仍需单独闭合。U2a 的 HOST/NONE 消费、本地 IO 和 Remote 工作未由本 PR 交付。只提交代码、测试及 API 合同文档，设计资料留本地。

### U2a 生产预检：HOST/H2D 布局（#2489 已合入）

基线 main `22c009811`。#2486 的 Python 同步入口已限制非连续 H2D，但
Worker.submit 和旧 POD 入口仍能绕过该限制。四个 binder 都按 numel × element_size
分配和搬运，却保留 strides/start_offset；因此先在共同原生预检拒绝非连续
HOST/H2D 和非零原生 start_offset，覆盖 IN/OUT/INOUT。公开 Tensor.byte_offset
已折算进导入地址，连续子区域仍合法；DEVICE/NONE 的跨步和偏移继续透传。

本项是 U2a 的生产消费者安全前置；HOST/NONE 消费、HBG 双侧显式传参迁移、
TMR/core 的访问判定仍需共同交付，不能把本项标为 U2a 完成。

本项已提交 [#2489](https://github.com/hw-native-sys/simpler/pull/2489)，
head `6c319db0`，base `22c009811`，5 文件 +128/−6。两个 HBG/两个 TMR
真实 binder 目标共 138 项通过；Python 参数测试 45 项通过；HBG readiness
两种模拟器各 7 项、TMR vector 各 1 项通过。新拒绝测试在修复前两个 HBG
目标因读取前项故障地址失败，两个 TMR 目标错误放行并发生分配；回归测试已提交。
当前 16 个设备 HBM 非零，未做本项板上验证。预提交检查全部通过。
规划文档及 HTML 仍在仓库外，不在 PR 内。下一项是 HOST/NONE 的真实消费
与 HBG 双侧参数用例迁移，不能只开放 API 而保留隐式跨侧读取。


### U2a：显式 HOST 消费与消费者迁移（#2501 待审阅）

2026-10-07：#2489 已合入；本次以 main `01bf24bfa` 为基线，提交
[#2501](https://github.com/hw-native-sys/simpler/pull/2501)，head `e89204e5`。
本项实现 HOST/NONE 的真实 HBG 消费，移除隐式 DEVICE 主机访问，未改 Pipeline A 的准入与完成机制。

- HBG 在复制任何参数前登记 HOST/NONE 的范围；get/set 仅按 IN/OUT/INOUT 访问。
  Python 的直接/子进程 chip 入口在解析地址前检查 Buffer grant 能覆盖 callable 方向。
- H2D 产生独立 DEVICE 参数；需要两侧数据就传两个 Tensor。HOST 写入不反向更新已复制的 DEVICE 值。
  要在 host 使用上次 device 结果，先显式等待与复制，再传入独立 host 参数。
- TMR、device task、graph 参数及 predicate 拒绝 HOST。HOST/NONE 支持有效跨步视图；
  HOST/H2D 保留连续布局约束；两份 H2D 副本重叠且存在写入时在分配前拒绝，只读重叠允许。
- PA、BGEMM、DSv4 host 元数据、FIFO/early enqueue、拓扑与 readiness 用例随接口共同迁移。
  本项不引入 PreparedCall、缓存、映射视图或自动 D2H 标签。
- 验证：294 个 C++ 测试目标；Python 2907 passed / 57 skipped；A2/A3 sim 69、A5 sim 67；
  task-submit A2/A3 板上 19 项通过，grant 检查后复测相关 readiness/FIFO 路径也通过。
  A5 板上和完整大规模 DSv4 尚未运行；日志隔离问题记录在工作树 KNOWN_ISSUES.md。

本 PR 待审阅，不能标为已合入。后续为 U2b 的本地显式 COPY 与完成/保留合同，
U3 Remote producer/receiver 的对称升级可并行。Kernel eager 的外部 context/caller 顺序仍需单独接线与验收。
代码 PR 只含产品代码、API 文档和测试；本节和 HTML 留在仓库外。


最新依赖修正见 [Kernel eager 剩余问题与接线顺序](kernel-eager-open-work.md)。


## 2026-10-08：当前接线与剩余问题

当前状态统一维护在 [kernel-eager-open-work.md](kernel-eager-open-work.md)。#2501 已合入；
Program IO / TensorLease 整理不作为 kernel eager 的前置。当前本地工作是 TMR/HBG 借用设备下的
共同 prepare/finalize，之后接 caller-stream launch 与公开入口，再做 capture。
