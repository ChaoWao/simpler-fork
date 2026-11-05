# 执行目标交接：历史 EX 索引退役

> **发布快照：2026-11-05。** 本轮为资源、部署通信、执行与跨仓目标的跟踪交接；不代表当前 main 全量验收。源码事实按各自 SHA 阅读，资源进度查 GitHub。参见[阅读入口](../README.md)。

2026-11-05 核对 GitHub 正文、关闭与交接评论。此页是一次性目标对照和具名历史证据，不维护独立进度。评论中的设计问题仍待负责人确认；本轮没有新增代码、CI 或硬件验收。

## 来源与交接

- [#2721](https://github.com/hw-native-sys/simpler/issues/2721)：观察 open，正文 SHA-256 `cbcafe38b02e1d1077023b8302354c526b0fdf8df0d46bd6d6c453b4976e266d`，更新时间 `2026-11-05T06:16:41Z`。
- [#2723](https://github.com/hw-native-sys/simpler/issues/2723)：观察 open，正文 SHA-256 `6ec53ee86b84aed483177ad1d464cf46a5325369b3190aa5830414fb0a586a0d`，更新时间 `2026-10-26T08:28:41Z`。
- [#2658](https://github.com/hw-native-sys/simpler/issues/2658)：观察 open，正文 SHA-256 `a140154bfa04b897199e0075de6caffb31a160dc6bc5059e8a375330916a1172`，更新时间 `2026-11-04T07:42:14Z`。
- [#2429](https://github.com/hw-native-sys/simpler/issues/2429)：观察 open，正文 SHA-256 `9f631b7d3e27a2bf00b91586ecbbf0d6a4f83932198443a4e83490ca1b371bee`，更新时间 `2026-10-27T01:44:11Z`。
- [#2666](https://github.com/hw-native-sys/simpler/issues/2666)：观察 open，正文 SHA-256 `1c3aba3ae61fd15897592af20bb448905d781e9190e5938934ff574e6da81859`，更新时间 `2026-11-04T07:24:11Z`。
- [#2316](https://github.com/hw-native-sys/simpler/issues/2316)：观察 closed，正文 SHA-256 `1363133e1088f176836aed1ed80c3c673ed0f3382bb0533baa965180dbdbac2c`，更新时间 `2026-10-22T00:51:29Z`。
- [#1853](https://github.com/hw-native-sys/simpler/issues/1853)：观察 closed，正文 SHA-256 `0a48785c1f1a232ee23bea81ccb939a6d55a6c8d218568bb1eb0ba4b10a461bb`，更新时间 `2026-10-22T00:51:36Z`。
- [#2721 交接评论](https://github.com/hw-native-sys/simpler/issues/2721#issuecomment-6653477675)：已发布并回读，正文未改；评论 SHA-256 `128dd1ac78bf361a18588aea7fda72a60b6cc7cc61cf9070686f645488bb3ec9`。
- [#2723 交接评论](https://github.com/hw-native-sys/simpler/issues/2723#issuecomment-6653480374)：已发布并回读，正文未改；评论 SHA-256 `43c3103767e3aaaf8616c008c69aace1613a5552b27c8c49b0b7bd8d4db92999`。
- [#2658 交接评论](https://github.com/hw-native-sys/simpler/issues/2658#issuecomment-6653482776)：已发布并回读，正文未改；评论 SHA-256 `35e8982dbf82c2776fb9466ee97863f22c2d4156bebb948ef1c10db4f4d235ea`。

\#2316 的 [P3 收口](https://github.com/hw-native-sys/simpler/issues/2316#issuecomment-6329970806)明确既有 FIFO/build/access 已交付，后续组合验收转 #2658；#1853 关闭评论同样交接。二者不能重新充当流水容量的活跃入口。负责人决定到来时更新对应合同，不恢复本地排期。

## 七项目标对照

| 旧编号 | 目标 | GitHub 入口／历史证据 | 验收与边界 |
| ------ | ---- | --------------------- | ---------- |
| EX-1 | responsive local edge：覆盖面与验收 | [#2721](https://github.com/hw-native-sys/simpler/issues/2721)、[#2724](https://github.com/hw-native-sys/simpler/issues/2724) | 公共控制响应归 #2721；多机部署组合归 #2724。不预设双帧或线程结构；长任务中取消、health、进展按支持拓扑验收。 |
| EX-2 | 单 L2 Run 常驻：多轮 work/completion ＋ STOP/drain | [#2723](https://github.com/hw-native-sys/simpler/issues/2723)、[#2721](https://github.com/hw-native-sys/simpler/issues/2721) | 单次 L2 Run 多轮 work/completion、缓冲区复用、背压、STOP/drain 与异常退出归 #2723；旧 AF1 idle fork 唤醒归 #2721，不能代替常驻验收。 |
| EX-3 | admission · 定向调度 · **零 heap 运行验收** | [#2721](https://github.com/hw-native-sys/simpler/issues/2721) | 定向/FIFO 已交付证据保留；有界资源与调度合同归 #2721。零 heap 是待论证方案，不是独立必做目标；真实模型自动准入组合归 #2658。 |
| EX-4 | 执行接口与 callable 身份收敛 | [#2721](https://github.com/hw-native-sys/simpler/issues/2721) | run/orch/callable 共同边界、注册快照与执行身份一致性；改名不能代替身份验证。 |
| EX-5 | scope 回收机制的必要性与迁移 | [#2721](https://github.com/hw-native-sys/simpler/issues/2721) | 无提前回收、无无界滞留；零 heap、两套 Arena 或删除 scope 回收须有必要性证据。确认替换后再迁移 shared allocation/null OUTPUT/描述符消费者；L2 TMR GM heap 不自动纳入删除。 |
| EX-6 | 终态回收 · child freeze · nested-shm 枚举 | [PR #1689](https://github.com/hw-native-sys/simpler/pull/1689) 历史交付 | P0.2-f4–f7 已交付的历史证据归档，不恢复待办；新增公共关闭与 workspace 分别引用 #2460/#2666。 |
| EX-7 | 连续 run 共享 stream 的顺序契约（Track A） | [#2658](https://github.com/hw-native-sys/simpler/issues/2658)、[#2429](https://github.com/hw-native-sys/simpler/issues/2429)、[#2666](https://github.com/hw-native-sys/simpler/issues/2666) | #1853/#2316 已关闭：基础机制静态归档。剩余真实模型自动准入/clone/DEVICE feedback、持续 refill/性能归 #2658 M2/M3；vLLM 消费者归 #2429，执行资源归 #2666。credit、结果观察及最后使用寿命分开。 |

## 保留的交接边界

公共执行 #2721、单 Run 常驻 #2723、多机部署 #2724 通过具体控制、完成和退出结果联验；#2720 提供区间与必要同步，不接管应用 queue。#2658 的组合验收消费 #2666 的资源安全合同，原 VLLM-S3/GE-R4/EE-QA/CAP 编号保留原义，不用自动模式或 Graph 组件成绩替代完整场景。有限资源背压和完成后复用须配对，未知停止状态不能提前释放。

## 交接前的具名证据快照

以下表格的状态和依赖只代表原日期／原 SHA，不作为当前进度。EX-6 的 f4–f7 为既有交付，后续 U4／workspace 不倒灌为其残留。代码观察基线保持 `63ffeb8a8bd9b6527912fee87f6a8defdf82d5e0`，常驻示例保留 `daf2f8418907`。

## 原执行工作表与旧 HeapRing 方案（仅作历史）

### 原执行工作表

| ID | 工作项 | 吸收的旧编号 | 依赖 | 状态 |
| -- | ------ | ------------ | ---- | ---- |
| **EX-1** | responsive local edge：覆盖面与验收 | `l3l4 E` · `P1-F` · `AF2` · `AF4` | — | 🟡 **local mailbox/control 与 grant 传输已有，扩展组合验收未完成**。#2571 将固定请求帧与按 grant 的 run-state entries 分离；NEXT_LEVEL 构造传 entry.granted_sets，SUB 构造仍使用默认值。旧 task_frame_count 双帧描述已不适用于当前构造。#2116 OPEN `6b8d56d4b54f` 承接 control extension；nested/remote/sim 的长期任务、取消、health 按组合验收。 |
| **EX-2** | 单 L2 Run 常驻：多轮 work/completion ＋ STOP/drain | `W7` · `AF1` | EX-1 | 🟡 **单 L2 Run 多轮交互已有示例，通用常驻能力未验收**。10 月 26 日 main `daf2f8418907` 的 worker_chip_message_queue 只 submit_next_level 一次，随后多轮 enqueue；chip orchestration 的 for (;;) 处理 DATA/STOP，停止前 finish_pending_inputs 并检查 drained。它是 #2723 的现有基座，不等于完整公共方案：work/completion 关联、槽位寿命与复用、背压/progress、停止准入/drain/异常退出及支持矩阵须共同验收。L3 进程/mailbox 多次接任务、重复 Worker.run 和单 L2 Run 常驻分别计证；AF1 的 idle 阻塞唤醒保留为历史交接，不再代指常驻目标。派发路径不得 sleep/backoff；#2720 不预先批准 SPSC 公共 ABI。 |
| **EX-3** | admission · 定向调度 · **零 heap 运行验收** | `l3l4 C.3` 的 admission 半 | — | 🟡 **定向与 whole-run FIFO 已交付；零 hierarchical heap 的运行验收仍单列**。这是 feature_roadmap §2.5 第 5 步，不要求先删除结构。#1137 已 CLOSED 且原属 L2 polling 优化。#2476 `dfa63eb81` 已合入：retention 测试使用 1024/4096/8192 递增窗口、无在飞 read 基准及诊断；#2470 已 CLOSED。合入测试不等于所有 overlap 或性能目标完成，EX-7 的真实 enqueue/refill 仍按其独立验收。 |
| **EX-4** | 执行接口与 callable 身份收敛 | `l3l4 C.2` 的 `run`/`orch`/callable 半 · `AF3` | — | 🟡 **公共 callable/执行接口仍需收敛验收**。AF3 的 pre-init live-object 与 digest 快照契约由 #1938（OPEN，`47e60a866df1`）提供可失败的观测；run/orch/callable 统一不得靠改名替代身份验证。 |
| **EX-5** | scope 回收机制的必要性与迁移 | `l3l4 C.3` 的 heap 结构半 | 接口 #2721〔临时／持久／异步使用寿命合同，具体机制待设计确认〕 · EX-3 | ⛔ **具体机制待 #2721 设计讨论确认**。验收要求是无提前回收且无无界滞留；不再把两套 Arena 或删除所有层级 heap 视为既定目标。若确认需要替换现有 scope 回收机制，必须先迁移使用者并完成运行验收；L2 TMR device GM heap 不因该讨论自动纳入删除范围。 |
| **EX-6** | 终态回收 · child freeze · nested-shm 枚举 | `P0.2-f4` · `P0.2-f5` · `P0.2-f6` · `P0.2-f7` | — | ✅ **f4–f7 已交付，当前基线复核**。#1689 `f8106e24e` 已提供 `_teardown_worker_tree`：pre-transport 全集先入 post-success journal，失败保留 transport；remote frees 与 session shutdown 分相；child/shm 有独立重试。`add_once(kind, identity, cleanup)` 的数据表含 region/domain/provenance/remote/buffer，不能只 grep `.add("child")`。`test_cancellation_journal.py::test_both_teardown_paths_retry_each_resource_class` 与 namespace/child 顺序案例在树上（本轮读测试，未重跑）。既有 f5 双锁、f6 survivor 重试、f7 确定名与 namespace sweep 均保留；新增 U4/device-workspace 的寿命目标分别归 #2460/#2666，不倒灌为 P0.2 遗留。 |
| **EX-7** | 连续 run 共享 stream 的顺序契约（Track A） | `TrackA-P1` · `TrackA-P2` · `TrackA-P3` · `TrackA-P4` · `TrackA-P5` · `TrackA-P6` · `VLLM-1` · `VLLM-2` · `VLLM-3` · `VLLM-4` 的 admission 半 | EX-3 | 🟡 **四个 onboard program backend 的限定 early enqueue、可配置资源与自动准入已有交付**。#2560 `a89c1170c` 补 A3 TMR；#2571 `78365f79c` 拆分 transport/run lifetime；#2596 `a2b0ad2ad` 对未显式配置的 startup-root 本地 L3 direct program endpoints 按完成资源 commit 增长容量；nested/remote/MPI、L2、kernel、borrowed、sim 不在自动模式范围。#2611 `0eda81c93` 拒绝同线程递归 native prepare。#2511/#2568/#2592 已交付 Qwen clone、同请求 DEVICE feedback、限定持续多请求 driver；自动准入与真实 Qwen 组合、动态到达和性能验收仍独立。TrackA-P6 归本项，资源部分联验 #2666；#2588 DFX 已合入 `545c05a43469`，#2605 vLLM eager 仍为范围外交接。#2028 CLOSED 未合入，#2425 历史故障已关闭，新 HBG 卡住由 #2559 承接；#2615 launch 异常 owner 修复已合入 `ce493639b45f`。 main `63ffeb8a8bd9` 含 #2829 `313499d61d`：物理完成可归还执行 credit，结果观察、原 stream/carrier 与 Program 输出各有生存期；#2844/#2845 补 finalization 异常/一次激活，#2866 `63ffeb8a8b` 提供 backlog 计数。不能把 pipeline_depth 当全部保留内存上限。#2429 当前收窄为 vLLM 消费者；跨仓组合归 #2658，capture 归 #2666，旧 VLLM/TrackA 编号仍保留原合同归属。 |

### 原 HeapRing 六步方案

可以移除 hierarchical HeapRing 的通用 buffer 职责，但不能删除
shared allocation 能力。

当前 hierarchical Ring/heap 的耦合点包括：

- Orchestrator.alloc()；
- null OUTPUT 自动分配；
- L3-L2 message queue 通过 Orchestrator.alloc() 暂存的三个描述符张量；
- Ring 自身承载的 task slot 和 scope lifetime accounting。

L3-L2 queue 的通信区不由 HeapRing backing。simulation 使用独立 POSIX
shared memory，onboard 使用独立 device VMM region；HeapRing 只与上述描述符
暂存存在弱依赖。

Ring 已支持 heap bytes 为零的 slot-only allocation。建议迁移顺序：

1. 拆分 SlotArena 和 SharedBufferArena。
2. 增加 Worker-owned shared buffer allocation。
3. null OUTPUT 改为 fail-fast，要求用户提供 Tensor。
4. 迁移 Orchestrator.alloc()，并把 L3-L2 queue 的三个描述符改用显式
   shared buffer；队列通信区无需迁移。
5. 默认用零 hierarchical heap 跑测试。
6. 所有使用者迁移后删除 heap-coupled scope reclaim。

这项工作不能顺带删除 L2 device-side TMR GM heap。L2 device heap 与
hierarchical host HeapRing 的职责和生命周期不同。
