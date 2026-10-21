> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# A 轨设计 —— 让现有 program 路径自然排队

基线 `748c39fbe`。归属 issue #1853。
**不含 B 轨**（纯 host build API、tiling 缓存、ACLGraph）。

**设计与评审依据：[运行时总体原则 G1–G8](../../runtime-design-principles.md)。A 轨的应用与范围见 [§0.4](#principles-application)，不能把总体架构工作全部列为本轨前置。**

**路线入口：[HTML 当前进度与下一步](../../run-pipeline-track-a.html#roadmap)。**

**实施分工：[P1 §2.3 / P2 #2267 双 worktree 交付计划](../../run-pipeline-p1-p2-coordination.md)。**

## 用户明确的 stream 设计前提

simpler 在进程粒度持有一个串行逻辑工作 stream，禁止 PyPTO 算子在 NPU 上并发；允许多个 run 提前排队。A3 用 AICPU 主控的两条 RTS stream 协作完成同一个算子，AICore 经 event record/wait 附着；A5 可聚合一条。kernel 模式附着 caller stream；program 异步返回表示完整提交已被接受，是否接收 PyPTO stream 仍待议。[完整语义与待议边界](../../review/stream-architecture-user-contract.md)。这是目标合同，不代表现有进程级所有权和 A5 聚合已经实现。

## 当前阶段与 Issue / PR 对应

总目标由 [Issue #1853](https://github.com/hw-native-sys/simpler/issues/1853) 跟踪。
增量核对：2026-09-19 UTC，#2330（`9803d9ae8`）与 #2343（`6acd7a867`）已合入；其他支线保留原核对状态。
P0–P6 是设计阶段，不与 PR 一一对应。当前仍保持单 launched；P4、P5 尚未交付。
“尚未关联”表示本路线图尚无独立跟踪项，不表示完成；无独立 Issue 的工作暂列总 Issue。

| 阶段 | 交付什么 | 跟踪 Issue / 实现 PR | 状态与剩余工作 |
| --- | --- | --- | --- |
| P0 · 收益基线 | 量出当前流水线还剩多少空档 | [Issue #1853](https://github.com/hw-native-sys/simpler/issues/1853)；[PR #2262](https://github.com/hw-native-sys/simpler/pull/2262) | 已完成立项测量；整体吞吐仍需解释长停顿。 |
| P1 §2.1 · 计时 | 每 run 独立计时存储与读回 | [Issue #1853](https://github.com/hw-native-sys/simpler/issues/1853)；[PR #2268](https://github.com/hw-native-sys/simpler/pull/2268) | 已合入；不包含 TMR 错误结果。 |
| P1 §2.2 · arena | 私有 bank 隔离；共享 bank 兼容性准入 | [Issue #1853](https://github.com/hw-native-sys/simpler/issues/1853)；[PR #2275](https://github.com/hw-native-sys/simpler/pull/2275) | 已合入；P4 保留现有保护。 |
| P1 §2.3 · 结果保存 | N+1 开始后，仍能读取 N 的错误结果 | [Issue #1853](https://github.com/hw-native-sys/simpler/issues/1853)；[PR #2283](https://github.com/hw-native-sys/simpler/pull/2283) | 诊断基础 #2283 已合入（`44dccbef8`）；#2298 已在同一结果区增加每 run 终态，复用 epoch 与保留寿命。共享 header 后备在 P4 前重新论证或移除。 |
| P1 §2.4 → P4 · DFX | 记录归属与连续采集；过渡期诊断 run 独占 launch | [Issue #1853](https://github.com/hw-native-sys/simpler/issues/1853)；[PR #2293](https://github.com/hw-native-sys/simpler/pull/2293) | #2293 已合入（`639d0c0b0`）：scope_stats / swimlane 的 buffer 身份与 host 值副本。swimlane 计数/归还由 [#2313](https://github.com/hw-native-sys/simpler/pull/2313) 交付（已合入 `14918c37c`）；dep_gen 身份与未发布 buffer 保留由 [#2321](https://github.com/hw-native-sys/simpler/pull/2321) 交付（已合入 `5048dfff5`）；args_dump [#2325](https://github.com/hw-native-sys/simpler/pull/2325)（`235353966`）与 PMU [#2328](https://github.com/hw-native-sys/simpler/pull/2328)（`2193fb941`）身份 / buffer 保留已合入；[#2343](https://github.com/hw-native-sys/simpler/pull/2343) 已交付显式记账与无 buffer 丢弃修复；轮转交接覆盖、设备可见性验收、会话生命周期和合并输出仍待完成；独占 launch 仍是 P4 约束。 |
| P2 · 完成边界 | 双流 fence、Partial 记账、wait 引用寿命 | [Issue #2267](https://github.com/hw-native-sys/simpler/issues/2267)；[PR #2272](https://github.com/hw-native-sys/simpler/pull/2272) | 已合入这一部分；不代表 P2 完成。 |
| P2 · 每 run 终态 | 成功 / 失败 / 未决记录，与同步结果核对 | [#2267](https://github.com/hw-native-sys/simpler/issues/2267)；[PR #2298](https://github.com/hw-native-sys/simpler/pull/2298) | 已合入（`72a4cc035`）。两架构 × TMR/HBG 普通入口覆盖；同步仍是最终判据；a5 HBG legacy 入口仍未决。 |
| P2 · callback 生命周期 | 进程级 owner、reset / fork / 迟到通知 | [#2267](https://github.com/hw-native-sys/simpler/issues/2267)；[PR #2303](https://github.com/hw-native-sys/simpler/pull/2303) | 已合入（`c6af44fe5`）；仅通知与日志，不含健康判定消费者，不改变准入或 run 结果。 |
| P2 · 晚读验证与判定规则 | N+1 执行时读取 N，不等待后继 | [#2267](https://github.com/hw-native-sys/simpler/issues/2267)；[PR #2330](https://github.com/hw-native-sys/simpler/pull/2330) | 已合入（`9803d9ae8`）；成功路径探针与纯规则，生产同步未移除；故障变体与接线由节点 E 交付。 |
| P2 E1 · 设备健康消费者 | 消费故障通知，停准入、恢复及 reset 代际隔离 | [#2267](https://github.com/hw-native-sys/simpler/issues/2267)；通知基础 #2303 已合入；实现 PR 待关联 | 当前下一项；不从 stream/task ID 或通知顺序归属 run，不以通知缺席判成功；保留正常 drain 同步。 |
| P2 E2 · drain / 退休接线 | 正常 drain 不再同步整条 stream | [#2267](https://github.com/hw-native-sys/simpler/issues/2267)；drain PR 尚未关联；相关接口 [PR #2295](https://github.com/hw-native-sys/simpler/pull/2295) 已合入 | 依赖 E1 健康消费者；交付生产 poll/drain、未决恢复及资源退休接线；复用 #2330 成功路径探针并补故障覆盖。#2295 分开 staging / 回拷 / 释放，不自行移除同步。 |
| P3 PR1 · 父 run FIFO | ready 分区重建、构图重入拒绝、staging / terminal 边界 | [Issue #2316](https://github.com/hw-native-sys/simpler/issues/2316)；[PR #2314](https://github.com/hw-native-sys/simpler/pull/2314) | 已合入（`0a68792ed`）；测试与文档，未改生产准入。P5 改变放行边界时须重验。 |
| P3 PR2 · build 所有权 | SO binding、recorder 收尾与独立 build 等待 | [#2316](https://github.com/hw-native-sys/simpler/issues/2316)；[#2356](https://github.com/hw-native-sys/simpler/pull/2356)、[#2399](https://github.com/hw-native-sys/simpler/pull/2399) | 已合入：正常/异常入口 guard 保住 job 尾部寿命；等待按 RuntimeContext 归属，保留内部 recording 并行。 |
| P3 PR3 · host 访问合同 | 显式等待/回拷、就绪输入、producer 拒绝与合法 early prepare | [#2316](https://github.com/hw-native-sys/simpler/issues/2316)；[#2356](https://github.com/hw-native-sys/simpler/pull/2356)、[#2406](https://github.com/hw-native-sys/simpler/pull/2406) | 已合入：真实 bind host 覆盖与公开 Worker/RunHandle 场景。无跨 run 自动冲突检测或隐式等待。[验收与交接](../../review/p3-delivery-and-handoff.md)。 |
| P4 · native 提前入队 | 放开三个 native 门，插入跨流 join，安全退休 | [Issue #1853](https://github.com/hw-native-sys/simpler/issues/1853)；PR 尚未关联 | 待 P1/P2 及 P3 相关合同检查就绪；交付两个 LAUNCHED。尚未关联独立 Issue/PR。 |
| P5 · Worker.submit | 放开 L3 两道门，验证生产路径连续补位 | [Issue #1853](https://github.com/hw-native-sys/simpler/issues/1853)；PR 尚未关联 | 待 P4 与 P3 顺序验收；交付用户可见收益。尚未关联独立 Issue/PR。 |
| P6 · 背压与吞吐验收 | 核验实际队列压力、资源水位与持续提交吞吐 | [Issue #1853](https://github.com/hw-native-sys/simpler/issues/1853)；PR 尚未关联 | P5 后验收；取消固定槽位本身属于目标，不由测量决定。 |

**当前顺序：#2272 fence + #2298 终态 + #2303 callback owner + #2330 晚读探针已合入 → #2267 E1 健康消费者 → E2 生产 poll/drain 与退休接线 / 验证 → P4 native 提前入队 → P5 Worker.submit → P6 背压与吞吐验收。**

#2298 已扩展 #2283 的同一个结果区：`epoch + verdict + completion_code + code_source`，
区域 280 B，payload offset 24。成功也发布，host 不再只在失败时读回；目前仅与同步结果旁路核对。
#2303 已合入通知 owner 和日志（`c6af44fe5`）；设备健康消费者另行接入。
callback 无法归属某个 run，也不能以未收到通知证明成功。
#2330 已验证成功路径的晚读与 D2H 不等待 N+1，并提供读回测量；生产接线、故障变体和设备发布成本仍待交付。
a5 HBG legacy 入口未发布终态，须补覆盖或保留该配置的同步。
run verdict、device health、retirement 独立判定；已合入的 #2295 为回拷和 binding 释放提供分离接口。
当前 main 的 `wait_run_fence` 仍调用 `sync_stream_pair`，P2 尚未完成，准入继续保持单 launched。

**并行支线 #2254（不要求全部完成才进入 P4）：**

| 工作 | 跟踪 Issue | 实现 PR / 状态 |
| --- | --- | --- |
| AICore KernelArgs 传值 | [#2257](https://github.com/hw-native-sys/simpler/issues/2257)，子项归 #2254 | [PR #2297](https://github.com/hw-native-sys/simpler/pull/2297) 已合入（`21bb4a37b`），#2257 已关闭。普通 / DFX 路径的独立参数 H2D 分别 1→0 / 2→0；不含 Runtime 等既有传输，不代表实测延迟收益。 |
| context 常量 Ctrl / PMU 表 | [#2254](https://github.com/hw-native-sys/simpler/issues/2254) | [PR #2277](https://github.com/hw-native-sys/simpler/pull/2277) 已合入（`31f348eb3`） |
| prepare_execution 直接插桩 | #2254 | [PR #2307](https://github.com/hw-native-sys/simpler/pull/2307) 已合入；独立 STRACE span。插桩完成不等于已有固定版本的收益测量。 |
| HBG host-only Runtime 尾部 | [#2308](https://github.com/hw-native-sys/simpler/issues/2308)，归 #2254 | [PR #2309](https://github.com/hw-native-sys/simpler/pull/2309) 已合入（`8726d7e13`）；每 run 少上传 34,048 B，不含 TMR，不代表实测延迟收益。 |
| Runtime 按方向拆分 / TMR args 按有效长度发布 | #2254 | 独立后续工作；共享握手区拆分需先证明初始化顺序，TMR args 另定承载与 ABI。尚未关联实现 PR。 |
| 内部 device 内存归一化 | #2254 追加评论，见 §0.2 | 待交付：M1 需求清单 → M2 workspace 大块池 / M3 kernel_bin 常驻上限 → M4 地址与寿命验收；不接管外部 tensor 内存。 |
| collector 重建集合缩小 | #2254 | 已否决，不合入；[调查记录](../../ARCHIVE-INDEX.md#ref-7670f1a82aa01f57) |

## 0. 目标与非目标

**目标：把 host 工作从两个相邻 run 之间的关键路径上摘掉。**

今天 `try_acquire_native_run` 在 launch 时取得（`c_api_shared.cpp:940`），在 finalize 里
`drain_execution` 之后才归还（`:1015`/`:1065`，释放在 `:1123`）。所以 run N+1 必须等到
run N **既 drain 完又 finalize 完**才能提交，设备在这段 host 尾巴里空转。

深度 2 今天的语义是 `{1 个在执行 + 1 个已 prepared}`。A 轨要把它变成
`{1 个在执行 + 1 个已 launched}`。

> **交付物是"同样的数字，不同的划分"**——而整个 host 往返的收益全在这个划分里。
> 无界深度不是 A 轨的目标（见 §7）。

**非目标**，一条都不做：

| 不做 | 为什么不属于 A 轨 |
| --- | --- |
| 新的公开 API / program 与 kernel API 分层 | A 轨改现有 program 路径；API 统一是独立架构工作，已承诺的调用合同仍须保持 |
| tiling 缓存 | 排队不需要它，而且它被 `set_tensor_data` 的构建副作用卡着 |
| ACLGraph / replay 参数更新 | 完整支持独立交付；A 轨仍须尊重已有引用寿命，不能借此提前回收资源 |
| 纯 host build / 设备提交拆分 | B 轨的形状；A 轨只需"每次参数版本不被后继覆盖" |
| 跨 run tensor 依赖跟踪 | §4.3 采用调用方访问合同，不新增自动冲突检测或隐式等待 |
| RTS 大 args / timeline 回收 | 本设计不引入该机制；#2297 后 AICPU launch 传 KernelArgs，AICore 通过 flat scalar payload 传所需字段，所指资源仍需寿命保护。 |
| 字节预算准入系统 | 代码只证明需要安全回收与压力出口，不证明需要完整预算系统 |

### 0.1 子项 #2254：提交参数发布与资源拆分

[#2254](https://github.com/hw-native-sys/simpler/issues/2254) 属于 A 轨的 host 提交成本与
资源归属子项，也可为后续 kernel mode 提供基础。issue 的尺寸及 `SlotPersistentArgs`
分析基于 `22385d2b0`；本文运行时基线仍是 `748c39fbe`，后者尚未采用该 slot 持久分配结构。
两版均存在 Runtime / AICore KernelArgs 的独立 H2D，不能把两版的分配次数直接混作基线。

| #2254 工作 | 与 A 轨的衔接 |
| --- | --- |
| 为 `prepare_execution` 添加 host span | 纳入 P0；和原始 RunWall 边界、finalize 计时一起补齐成本分解，优化前先有基线 |
| AICore KernelArgs 经 launch payload 传值（[#2257](https://github.com/hw-native-sys/simpler/issues/2257)） | 已由 #2297 合入：arming 后构造 flat scalar 参数，删除独立 device KernelArgs 缓冲及 prepare/DFX refresh H2D；a2a3 参数块 32 B，a5 40 B。Runtime 布局和所指资源寿命不变。 |
| 拆分一次性配置、callable 常量、device 握手区与 host-only 状态 | 衔接 P1 的资源归属；明确大小、版本与寿命，HBG 的 host vector 不再进入 device wire image |
| orch args 按有效长度发布 | 减少 prepare/upload 工作；HBG 可随现有私有 image 发布，TMR 的承载方式及 launch 参数容量需验证 |
| PMU 地址表缓存 | 独立诊断路径优化；注明资源归属及 device reset 后的失效规则，不作为普通路径排队的必要前置 |

实施进度：**P0 的 RunWall 插桩及立项测量、#2257 参数传值已完成；prepare_execution 直接插桩已由 #2307 合入。HBG host-only 尾部已由 #2309 合入；其余 Runtime 拆分按收益推进，共享握手区拆分先解决初始化顺序。**
P1/P2 的安全基础可以独立推进；#2254 全部完成不是首个双 launched 里程碑的必要条件。
减少同步上传缩短的是 `P`，提前排队隐藏的是可重叠的 host 时间，二者需分别计量。

边界保持明确：

- launch 参数传值只版本化 payload 本身，不替代其指向的 Runtime/image、结果与 scratch
  的寿命管理。三个 native 门、跨流 join、per-run fence 和资源退休仍按 P1–P5 实现。
- 若共享握手区，初始化必须在前驱两条流不再使用它之后，且在本次 AICore 首次发布
  `aicore_done` 之前。当前 AICore 先 launch 且无需等待 AICPU 就发布该字段；不能直接在
  后启动的 AICPU boot 中清零，否则可能擦掉本次报告。沿用私有区或增加明确的初始化顺序。
- callable 地址表只能在对应代码代际内复用，注销/替换须保留仍被在飞 run 引用的旧版本。
- 更新资源声明时区分平台管理的 launch payload 与应用持有的 device 存储，精确描述
  大小和复制数；完整 kernel-mode 容量清单与 ACLGraph 验收仍属后续范围，不扩大 A 轨首版。

#### 长度 A/B 测量报告：#2319（已合入）

[PR #2319](https://github.com/hw-native-sys/simpler/pull/2319) 于 2026-09-18 11:22 UTC 合入，
merge commit `1a0d46763`，最终 head `c8148df5d`。最后两句关于统计前提和系统偏差的确定性表述已修正，评审问题全部关闭。

报告以 `8726d7e13` 为基线，仅改变 Runtime 复制长度（51,584 / 17,536 B），
a2a3 benchmark_bgemm 按 B1、A1、A2、B2 运行，每批次去掉前两次后 398 个样本。

| 批次 | 复制长度 | prepare 中位数 |
| --- | --- | --- |
| B1 | 17,536 B | 77.1 µs |
| A1 | 51,584 B | 75.7 µs |
| A2 | 51,584 B | 59.1 µs |
| B2 | 17,536 B | 65.0 µs |

**报告交付完成，真实延迟收益仍未辨识。** #2309 的字节缩减成立，本实验不能界定真实收益或证明固定调用成本主导。

- 四批次只能作描述统计；逐 invocation bootstrap 未处理时序相关和批次差异。
  批次 t 计算的独立性、共同方差前提未建立，±44 µs 也不能当作真实收益区间。
- 占卡快照不同，不能排除环境混淆；自相关说明时间依赖，具体原因未识别。
- 受控总 span A/B 可以估计整体收益；H2D 子 span 帮助分解成本，不能替代实验控制。
- 首次约 601 µs、warm 约 69 µs 只说明冷暖差异，未测出分配/初始化/上传等项的贡献。
- #2297 不是实验臂，不能由本实验排名其收益；报告合入不使这些未知量变成已验证结论。

后续：需要延迟结论时，再进行受控独立批次与必要的子阶段测量；不阻塞普通路径 P4。
#2315 的具名 device 描述符是另一项交付。后续复现须核对实际两臂长度和对象容量；
本报告的数字仅归属原测量基线，不凭过去版本的字段删除描述推断新布局。

### 0.2 #2254 后续：统一 device 内存管理

按调用方确认的三类边界整理主线；依据 [#2254 追加评论](https://github.com/hw-native-sys/simpler/issues/2254#issuecomment-5726628274)。
以下是待实施方向，不表示主线已完成池化。代码核对基于 main `0a68792ed`；评论中
#2289 的 `917f5c68` 布局及约 514.83 MiB 数据是该分支观测，不能作为 main 的测量。

| 类别 | simpler 的责任 | 增长与复用 |
| --- | --- | --- |
| 外部框架管理的 device 内存 | 借用 PyPTO / 内外部框架传入的 tensor、输出、cache 地址，不接管分配释放 | 就绪性与无冲突按调用方合同；simpler 自行创建的 staging 副本仍计入内部资源 |
| Workspace 性质的内部内存 | 统一管理 device 执行 scratch、提前发布的 image/staging 及晚读结果，按内容寿命划分子区；host 构图内存另列 | init 确定总上限；每个有保证的串行执行域在上限内按需追加大块并复用，保留旧块和地址；遵守 §0.3 |
| kernel_bin 性质的常驻内存 | 代码、函数表和相关常驻资源按有效身份复用；context 常量按 context 寿命管理 | 按需增长，设总字节与条目上限；超限拒绝新注册，保留已有有效资源 |

RTS 管理的 launch 参数存储单独计量，不转为 simpler workspace，也不占 kernel_bin 预算。
结果、计时、DFX buffer 可由统一 owner 管理，但复用要等最后一个读者；不能在 device
执行结束时把整池游标一律清零。统一的是 backing storage 与预算，不是所有内容的寿命。

**当前主线的具体差距：**

- `MemoryAllocator::alloc()` 仍直接 `rtMalloc`；统一记账不等于合并 backing allocations
  （`src/a2a3/platform/onboard/host/memory_allocator.cpp`）。
- `setup_static_arena()` 的 GM heap / GM SM / runtime pool 独立分配；program 模式扩容先
  release 旧区，kernel 模式对已提交区增长 / 释放直接拒绝
  （`src/common/platform/onboard/host/device_runner_base.cpp`）。
- `acquire_graph_definition_block()` 扩容会换块并释放旧块；`RetainedTempBump::begin()`
  同样 free-old / allocate-new。现有 retained 不满足新增的“旧地址长期保留”要求。
- `upload_chip_callable_buffer()` 已按内容 hash 复用，但 miss 仍独立分配；该函数未实行
  常驻总字节预算。评论里的 8,192 条 / 2 GiB 不在此直接认定为 main 已实施的限制，
  要结合实际注册入口、现有 ID 容量和代码存储重新确认。

**Workspace 的实施约束：**

1. 先形成需求清单：bytes、alignment、所有者、内容寿命、重置方式、capture 引用。
   HBG 的 heap / Definition / scheduler 按各自需求定容；TMR 的动态 heap / ring / 依赖池
   仍有明确容量。一个总预算须包括元数据，不能把 tensor heap 大小冒充总占用。
2. 使用追加式 backing blocks，块内划分类型化区域。在 init 确定的总上限内补足容量，超限拒绝；已发布地址不搬迁、
   不因增长释放。要求连续地址的区域必须在一块内满足；非连续块不能假装拼成一个 base。
   先分配并验证新布局，成功后发布，失败不能破坏旧布局。块大小策略避免每次小幅增长都
   保留一个新的整块；分别报告累计保留容量、峰值同时使用量和 capture 保留量。
3. 串行指实际执行顺序有保证的域，不是进程全局，也不是 host 串行 submit。
   同时存活的 tensor / scheduler / control 不别名；域内 scratch 仅在前驱双流停止使用后
   复用，初始化必须有设备顺序。首版可以统一池而保留 §2.2 的 bank 隔离，随后再共享区间。
4. 提前 prepare 写入的 per-run image / staging 在与前驱内容同时存活时需要独立版本，
   也可以改为前驱最后读取后的有序覆盖；finalize 才读的结果则保留到消费结束。
   不同版本可从统一池获取子区，不要求各自 `rtMalloc`。地址不变也不代表内容可以覆盖：
   graph replay 依赖的 immutable image / 常量保留原内容，可重建 scratch 按设备协议重置。
5. 不在 capture / launch 热路径临时扩容；容量不足在准备 / 注册阶段处理。现有 kernel-mode
   禁止扩容的 guard 要到新合同可替代时才改。旧块保留至拥有者生命周期安全结束，graph
   仍能 replay 时不释放；一次 stream sync 只证明当次使用结束，不解除 graph 的地址引用。

**可独立交付的任务：**

| 任务 | 依赖 | 交付判据 |
| --- | --- | --- |
| M1 资源需求与归属清单 | 本节分类与 §0.3 原则 | 覆盖实际 main 的 TMR / HBG、program / kernel 入口，分别列逻辑需求和实际分配；明确 init workspace 总上限与独立的常驻上限 |
| M2 workspace 追加大块池与迁移 | M1 | init 上限内小区间共同分配、稳定地址、高水位复用，超限或增长失败不破坏旧块；先保留现有隔离，真正共享还需初始化顺序证明 |
| M3 kernel_bin 有界常驻管理 | M1，可与 M2 并行 | 去重命中不重复占预算；新项在分配 / 发布前检查容量，失败不污染计数；已注册交付项不透明淘汰，释放走明确生命周期合同 |
| M4 地址、寿命与容量验收 | M2 + M3 | 同需求 warm run 无新增 backing 分配；增大后缩小复用水位；旧 graph 在增长后仍有效；晚读结果无覆盖；超限无部分发布 |

四项归 #2254，尚未关联实现 PR。M4 同时检查分配次数和实际保留字节，不以减少调用次数
推导显存或碎片收益。完整内存归一化可并行推进，不作为 A 轨 P4 的新增全量前置；
任何取消已有 bank / 版本隔离的变更都必须满足 P1/P2/P4 的寿命与排序合同。

### 0.3 原则：内部 workspace 在 init 定上限，容量内复用

这是跨 TMR / HBG、program / kernel 的总体资源原则，正文归入
[总体原则 G3 / G4](../../runtime-design-principles.md)。具体预算、对象和最后使用者见
[Workspace 分类核查](../../workspace-memory-principles.md)。

Init 定总上限，限内按需追加大块、按水位复用，旧块保留，超限明确失败；
launch / replay 不扩容底层块。统一的是容量和所有权，不是所有内容的寿命。
完整迁移由 §0.2 的 M1–M4 独立交付，**不是 P4 的新增全量前置**。
A 轨即使保留现有 bank 和 allocator，也必须保证后继不覆盖前驱尚在使用的内容。

<a id="principles-application"></a>

### 0.4 总体原则在 A 轨的应用与交付边界

A 轨目标是在现有 program 路径允许后继提前入队，减少 run 间 host 交接空档。
总体原则约束正确性；下表才是本轨必须兑现的合同，不要求先完成全部总体架构改造。

| 总体依据 | A 轨必须落实 | 设计 / 验收位置 |
| --- | --- | --- |
| G4 / G5：复用与排序 | 后继初始化排在前驱全部使用者结束后；当前双流后端插入必要 join，保留父 run 的 dispatch 顺序 | §2.2、§3.4、§4.2、P4 / P5 |
| G2 / G4：参数与存储寿命 | 检查 N+1 prepare 对 N 参数、image、输入 staging 的覆盖；使用独立子区或有序上传，保护上传源 | §2.2、§4.4、§9.1 |
| G4 / G7：完成与退休 | N 的完成证明不依赖等待 N+1；执行结果、设备健康和资源可退休分别判断，覆盖 Partial 与异常退出 | P2、§5.2、§9.1 |
| G4 / G7：晚读结果 | N+1 reset 前保存 N 的终态、错误和计时；后继开始后 host 仍能读取 N | §2.1、§2.3、P2 |
| G8：诊断归属 | 诊断路径必须安全：连续采集未就绪时，P4 保留诊断 run 的独占 launch 保护 | §2.4、P4 |
| G2 / G5：调用方数据合同 | 调用方保证跨 run host 访问无冲突、入口数据就绪及必要回拷；HBG 拒绝本次图内 producer 的值作为构图输入 | §4.3、P3 / P5 |
| G8：验证 | 验证确实提前入队，分别报告残余 gap、稳态吞吐、stall 和新增成本 | P0、§9、P5 |

**§2.4 的必需项是安全保护，不是完整连续采集。** 身份和 buffer 协议 → 会话 → 合并输出
可以独立推进；普通路径 P4/P5 可在诊断仍独占的条件下交付。取消诊断独占才需要相应连续采集验收。
“单 launched 今天自然安全”不能代替 P4 放开后的显式保护，也不能重新限制已允许的安全 prepare。

| 独立工作，不自动作为 P4 前置 | 与 A 轨的交界 |
| --- | --- |
| Program / kernel API 分层、准备接口 | A 轨保持已有入口及调用合同，不复制新 API 家族 |
| 全量 workspace 统一 allocator、常驻代码容量 / handle 管理 | 可并行迁移；A 轨必须修复具体覆盖和回收问题，不必先完成整个管理器 |
| Capture / replay、参数更新和动态构图合同 | 尊重已有长期引用；完整 capture 能力及逐次 replay 身份另行交付 |
| A5 mixed launch | 独立后端演进；当前双流方案仍须证明全部使用者的顺序 |
| Prepare 减拷贝、减分配优化（#2254） | 不是提前排队的全量前置；其中共享状态初始化若与排队冲突，必须先解决该冲突 |
| 完整 DFX 连续采集与合并输出 | 独立支线；普通路径用诊断独占保护满足当前安全合同 |

**前置判定：某项是否直接阻止安全提前 launch，而不是它是否属于总体原则。**
例如 P4 必须修掉“prepare N+1 清除 N 的握手状态”，但不因此要求先完成全量 workspace 重构。
状态和具体 Issue / PR 仍以顶部跟踪表及 HTML DAG 为准；本节不改变其完成状态。

## 1. P0 · 先量收益，再决定做不做

优先量**相邻 run 的设备侧交接间隔**，同时记录端到端吞吐和 host 补位时间。
轻量 device-phase 采集不触发诊断限流；[#2262](https://github.com/hw-native-sys/simpler/pull/2262)
已输出原始 RunWall 边界。`Worker.submit` A/B 基线已完成（§10.1），P0 的立项判断结束。

- 首个测量口径为 **AICPU RunWall 间隔**：同一设备时钟代际下，
  `min_start_cycle(N+1) − max_end_cycle(N)`，先作整数差再换算时间
- 该口径不自动代表 AICore/AICPU 双流完整边界的空档；报告中必须注明测量边界
- **必须走 `Worker.submit`**，不是直连 `ChipRun`；基线与改后使用相同负载、计时口径和配置
- 同时测量 finalize、prepare/upload、launch、稳态吞吐及资源峰值，覆盖第三次及后续补位

将同负载结果放进 issue，再判断收益及所需容量。历史 qwen bind 与 dsv4 device 数字
仅作各自场景的背景，不拼接成容量结论，也不单用"gap 小于 bind 的零头"否定收益。

**P0 已完成，三轮。** 首轮是 `chip.run` 内部的 host/device 拆分（§10.4）；当时拿不到
inter-run gap，因为 `device_wall` 只有 duration 没有设备时间戳。第二轮补了插桩
（§10.2，已随 #2262 合入 main `4860af980`），拿到 blocking 路径上的设备空档（§10.3）。
**第三轮走 `Worker.submit`，做了 blocking / async 两臂 A/B（§10.1），这是 P0 的结论：**

| | blocking | async（depth-2 能重叠） |
| --- | --: | --: |
| 设备 run 间空档 | 706–770 µs | **242–355 µs** |
| 设备占空比 | 31.6%–33.8% | **50.2%–60.2%** |
| D（`device_wall`） | 356–363 µs | 357–366 µs |

**今天的 depth-2 已经吃掉 54%–66% 的空档。A 轨要动的是残余那 242–355 µs**——它和 D
（约 360 µs）同量级，所以空档归零的吞吐上限约 **1.7–2.0×**。这是直接测量，不是从 host
分解推的。三次复现，含一次 AIV 异常失败和一个两臂都有的周期性 stall，见 §10.1.1/§10.1.2。

## 2. P1 · 共享资源安全（行为不变）

还不排队。把资源改成"排队开始那一刻是安全的"。四项，互不依赖。

**前三项已全部合入**（§2.1 [#2268](https://github.com/hw-native-sys/simpler/pull/2268)、
§2.2 [#2275](https://github.com/hw-native-sys/simpler/pull/2275)、
§2.3 [#2283](https://github.com/hw-native-sys/simpler/pull/2283)）。§2.4 的临时独占门是一条 P4 约束，
连续采集支线的首项身份基础已由 #2293 合入。所以 P1 在保留诊断独占的前提下收口；**这不等于**连续采集已实现，
也不等于 P4 的前置条件都已满足。

### 2.1 `device_wall` 每 run 一份

**已合入 [#2268](https://github.com/hw-native-sys/simpler/pull/2268)，commit `0f6e032`。**
device buffer、读回结果 `DeviceRunTiming` 与 armed 状态按 pipeline slot 保存；本 run
finalize 消费后释放 armed 状态，再复用 slot。reset H2D 失败时跳过读回，避免发布旧计时。
本项不改变 launch 准入，也不代表其他资源保护已完成；DFX 的过渡保护随 P4 接入（§2.4）。

原始基线是 runner 级单缓冲（`device_runner_base.h:1430`，`:1849` 分配、`:1865` 每 run 重置、
`:1997` drain 后读回）。设备写、host 在 drain 后读——**两个在飞 run 会静默写坏彼此的
timing/phase 记录**。不崩，只是 DFX 数据错。

不能靠"host 抢在下一个写者之前读"：后继在前驱 fence 之后立刻开跑，那个间隙里没有 host。

P0 新增的原始 RunWall 起止 cycle、run 身份和时钟代际也随本 run 保留到输出完成（§9.3），
不能在 per-run device 缓冲读回后又落到会被后继覆盖的 runner 单份结果中。

### 2.2 arena bank 隔离与共享 bank 准入（#2275 已合入）

**已合入 [#2275](https://github.com/hw-native-sys/simpler/pull/2275)，commit `d20ecfd8f`。**
只有其他 live reservation 使用相同 `arena_bank` 时，后继 prepare 才调用兼容性 probe；
查询排除自身 reservation。现有两种 runtime 的安全策略如下。

| 情况 | 已有保护与 #2275 后的准入 |
| --- | --- |
| HBG：每 slot 私有 bank | 后继扩容 / 回滚只作用于自己的 bank；因为不共享而跳过 probe |
| TMR：共享 bank 0，布局兼容且缓存命中 | prepare 前强 probe 通过；bind 使用已有缓存地址，不重建共享 arena |
| TMR：共享 bank，布局不兼容或 cache miss | 后继 prepare 延后，等前驱退休后才改变布局 |
| `requested_size <= cached_size` | `commit_region` 已短路为 no-op |
| kernel 模式已有捕获图引用 | grow / release 在首个 `commit_region` 之前拒绝，避免部分修改 |

`commit_region` 本身仍会在增长时释放旧分配，失败回滚也会释放本 bank 的区；安全性来自
上述隔离 / 准入，而非 allocator 内新增的通用在飞引用保护。**A 轨可保留这两种策略，
不要求先实现旧代引用计数。** P4 放开多 launched 时仍须保留共享 bank 在 bind 前的检查，
并将 reservation 保留到资源不再被引用，不能把检查挪到 launch 或提前释放 reservation。

当前 TMR 有强 probe；weak 默认仍返回兼容。未提供 probe 的未来共享-bank runtime
仍需合同加固，这不是 #2275 已解决的事项。核查细节见 [本地 review](../../review/pr-2275.md)。

### 2.3 TMR 的 SM header 错误字段按 run 保全（#2283 已合入）

**已合入 [#2283](https://github.com/hw-native-sys/simpler/pull/2283)，commit `44dccbef8`。**

后继 AICPU 的 `sm_reset` 会清掉共享 header。两架构 TMR 在末线程收尾、拆除 SM 前，
将连续 112 字节错误尾部发布到本 run 所属结果区；`KernelArgs` 携带 base 与 epoch，
host 校验身份后在 finalize 消费，槽位寿命保留到本 run 结束。

以上描述 #2283 的原始诊断能力。**#2298 已进一步扩展同一结果区**，加入每 run 终态，
成功也发布，host 无条件读回并与旧同步判据核对；两架构 × TMR/HBG 普通入口已覆盖。
a5 HBG legacy 入口仍未决。终态有效性、设备健康与资源退休是三个独立结论，见 §3。

未发布快照时仍回读共享 header，以保留 watchdog 等路径的诊断。该后备今天依赖
claim 持续到 finalize 和单 launched；**P4 放开准入前必须重新证明后继未覆盖，或移除后备**。
失败结果已知也不代表设备引用可回收，仍须满足双流边界及异常退休条件。

### 2.4 DFX：连续采集、合并输出；限一在飞是过渡保护

**目标：同配置的多个 run 复用常驻采集服务和设备 buffer，连续采集并合并输出。**
保留轻量 run 身份用于数据归属，不要求每 run 单独文件，也不要求 host 在相邻 run 之间
等采集排空。以下是待实现设计，不能据此解除当前 collector 的独占前提。

**交付更新：#2293 已合入（`639d0c0b0`）。** scope_stats / swimlane 已在 buffer 上保存
run_epoch/local_seq，host 按值保留身份，sim 也发布 epoch。最终合入版补了真实
init → complete_task → flush → collect 的短 run 回归，覆盖旧 epoch 的池化 buffer。
只有 scope_stats artifact 新增身份字段，swimlane 输出 schema 保持原样。

**交付更新：#2313 已合入（`14918c37c`），完成 swimlane 准确计数与未发布 buffer 的保留复用。**

**交付更新：#2321 已合入（`5048dfff5`）。** dep_gen buffer 携带 run_epoch/local_seq，host 按 epoch 保存值副本；空 / enqueue 失败 buffer 保留复用，成功发布后归 host。零记录窗口仍输出空 deps.json，多 epoch 窗口拒绝混图。逐 run 清空和计数核对仍保留；空窗口返回的 epoch 0 不是实际 run 身份，会话改造仍需显式登记空 run。

**交付更新：#2325 / #2328 已合入（`235353966` / `2193fb941`）。** args_dump 将 run_epoch 写入每条 manifest 记录，PMU 将它追加到 CSV 尾列；两者均保留空 / enqueue 失败 buffer，成功交出后才释放设备指针。

**本支线下一步：** 完成设备身份可见性验收与 swimlane 累计分账协议，保留逐 run 清空和单 launched。之后再推进会话生命周期与合并输出。
计数和归还必须一并修，不能只删 TASK_TIMING 的满容量 fallback；buffer 归还须遵守现有
ready/free queue 的生产者归属。连续采集不阻塞保留诊断独占的普通路径 P4/P5。

#### 现状：服务已常驻，采集窗口仍按 run 重置

`chip_swimlane / dump_args / pmu / scope_stats` 的线程和池已经复用；
`ProfilerBase::start()` 对已启动线程直接返回，`quiesce()` 排空但不销毁线程。
然而 `start_shared_collectors_for_run()` 仍逐 run 调用破坏性的 `begin_run()`，
清记录、重置计数或发布配置；`teardown_shared_collectors_after_run()` 仍逐 run
执行 quiesce / 对账 / 导出。后者要求设备 producer 已停止，不能在 N+1 继续写入时
用“队列暂时空了”证明 N 的采集完成。

因此，**常驻服务的内存和线程复用已经实现，连续采集尚未实现**。单例 collector、
mgmt/poll 线程和全局采集级别本身不构成障碍：同配置可以共享，配置改变时才切换会话。
以函数实现为准，调用点中描述每 run 启停线程的旧注释不能作为寿命依据。

代码依据：[`device_runner_base.cpp`](../../ARCHIVE-INDEX.md#ref-533eaf26ec9bc7f6)
的上述两个函数，以及 [`profiler_base.h`](../../ARCHIVE-INDEX.md#ref-b1575256c15e04fd)
的 `start / quiesce / stop`。

#### 身份：复用 run_epoch，随 buffer 固化

[`NativeRunDescriptor`](../../ARCHIVE-INDEX.md#ref-99d8644f28be57ce) 已有进程内唯一的 `run_epoch`，
它在 prepare 时分配；prepare 后未 launch 的 run 也有编号，因此不是硬件 launch 成功计数。
一个 native run 的 AICPU、AICore 两次 launch 共用这个逻辑身份，需要区分时再加 producer 类型。
两架构 `KernelArgs` 已有 epoch；#2293 已接通 scope_stats / swimlane 的 buffer 与 host 副本。
dep_gen / args_dump / PMU 身份接线分别由 #2321 / #2325 / #2328 完成；显式记账已由 #2343 交付；轮转交接覆盖与设备可见性尚待验收，会话生命周期未改，不能据此解除全部 collector 的逐 run 屏障。

- 把 `run_epoch` 经 launch 参数传到 AICPU / AICore producer，并更新两架构的 wire 布局及布局校验；
  与 #2257 的 KernelArgs 传值改造协调。
- 优先放进 **buffer 元数据**，一个 buffer 只装一个 run 的记录。run 结束时发布未满的尾 buffer；
  后继领取可用 buffer。发布后的 payload 和身份在消费者归还前均不可改写。
- buffer 轮转序号只表示 buffer 代际，不能代替 `run_epoch`；host 也不能用可变的
  “当前 run”给迟到记录补身份。
- 导出及分析工具按“采集会话 / producer、run_epoch、原 task/core 身份”归属；原来的 task_id、
  reg_task_id 可能跨 run 重复。跨进程输出还需保留进程 / worker 身份，slot 号不能作唯一键。

#### 生命周期：run 完成与采集会话结束分开

```text
固定配置的采集会话：初始化 / 发布配置一次
  device：N 写 buffer[N] → 发布尾 buffer → N+1 写 buffer[N+1] → …
  host：  常驻 mgmt / poll 消费 → 按 run_epoch 归属 → 持续写入会话输出
  runtime：N 的双流完成 → finalize N；不等待整个采集队列排空
会话结束：停止向旧会话准入 → 等全部 producer 停止 → quiesce → 对账 / 完成输出
```

“合并输出”是同一采集会话的数据集，可以按 collector 保留各自格式，也可以分块落盘；
不要求一个无限增长的文件或把所有记录一直存在内存中。run 起止标记可用于展示与统计，
不把它变成逐 run 的 host drain 屏障。

| 边界 | 合同 |
| --- | --- |
| 同配置后继 | 不清空前驱记录，不重置共享队列；沿用当前采集会话 |
| level / PMU event / buffer shape / 输出目标改变 | 停止旧会话准入并排空其引用，再发布配置或重建池；不能让后继 prepare 改掉前驱配置 |
| finalize N | 按 run fence 完成执行收尾；已移交 DFX 自有 buffer 的数据可继续异步输出。若仍借用 run 资源，必须等复制确认后才能退休该资源 |
| 输出完成 | 单个 run 的 result 不再隐含合并文件已完整；会话关闭才保证最终对账和输出结束。实现需接入明确的收尾入口，如 worker drain/close 或配置切换 |
| 失败 / partial launch | 保留不完整标记，沿用 runtime 的完成 / 静止证明；不能无限等待可能永远不产生的 run-end 标记 |

**不能只删掉 `begin_run()` 或 `quiesce()`。** 还要一起核查 device 初始化中的 current-buffer、
seq、计数重置，以及 host `start()` 包装层的 shard 清理。会话累计计数与设备 per-run 计数
必须采用一致的对账口径：选连续累计，或在设备顺序中封存带 run 身份的统计，不能由 host 抢读。
swimlane 的 host phase、时钟关联、JSON extensions，以及 dump 的元数据排序与分析工具也须
按 run 追加，不能继续清空或只按 task_id 合并。已有 dump arena 单调写偏移可复用，
但不能据此推断其他计数与队列也已经支持连续运行。

#### 实施位置与验收边界

今天 `active_native_run_` 的单 owner claim 已强制一个 launched，直到 finalize 才释放；
此时不另加永远不会触发的 DFX 门。**P4 放开 claim 时，若连续采集尚未完成，保留诊断
run 的 launch 独占作为过渡保护**：前驱正常 run 退休后才能开启诊断窗口，诊断窗口收尾前
也不得 launch 后继普通 run。只限制“两个诊断 run”不够。
**继续允许 prepare 重叠，不回退 #2201**；原有更窄的时钟关联限制仍按其合同处理。

连续采集可独立推进，不阻塞普通路径的 P4/P5。实现顺序为：身份和 buffer 协议 →
会话配置与重置 / 对账 → 合并 writer 与分析工具 → 解除对应路径的临时独占。
四个 collector 之外，device DepGen 也有自己的窗口与收尾，解除整个诊断门前须另行核查；
HBG 的 host 构图导出不能直接套用 device producer 协议。

验收须覆盖：消费者滞后时 N+1 仍可执行且 N 不被覆盖；跨 run 重复 task_id 正确归属；
未满 buffer 与空 run；长时间运行的有界内存和队列背压；配置切换；partial / 失败后
有限收敛；会话关闭后各 run 的计数与输出完整性。这里列的是后续实现验收要求，本次仅更新设计。

## 3. P2 · 完成判据换成 per-run（行为不变）

实施 issue：[#2267 — Use per-run dual-stream completion fences](https://github.com/hw-native-sys/simpler/issues/2267)。
保留单 launched 准入；P2 定义并测试后继 wait 的引用合同，生产跨 run join 与放开排队在 P4 接入。

**进度：#2272（`3b2bf1a6b`）与 #2298（`72a4cc035`）已合入，P2 部分完成。**
已有双流完成 event、Partial 记账、wait 引用寿命，以及与原同步判据并行核对的每 run 终态。
正常 `wait_run_fence` 在 event 完成后仍调用 `sync_stream_pair` 获取设备错误。
#2303 的 callback owner 与 #2330 成功路径晚读探针已合入；E1 先接入设备健康消费者；E2 在 E1 就绪后交付
健康 / 未决恢复 / 资源退休接线，再移除正常整流同步；错误码从 ACL 错误转为 runtime 状态的
调用方语义变化须明确验收。缺失终态的入口补覆盖或继续同步。
**解决这项前不放开 P4 的双 launched 准入**；P4 的其他准备工作可并行。


`aclrtSynchronizeStreamWithTimeout(stream)` 的语义是"迄今提交的全部"。一旦后继入队，
为 N 同步会连 N+1 一起等——**问错了问题**。

本阶段**仍然一个 launched**。终态旁路核对不改变原同步判据；后续切换 drain
还涉及错误码语义、设备健康与退休条件，不能简化为只替换栅栏或只提前释放 claim。

### 3.1 每条流一个 event，在它自己那次提交之后立即 record

- `poll_execution` → `aclrtQueryEventStatus`
  （**不是** `aclrtQueryEvent`，`acl_rt.h:1514` 标 deprecated）
- `drain_execution` → 等本 run 自己的 event

不是新赌注：`aclrtCreateEvent` / `aclrtRecordEvent` / `aclrtEventGetTimestamp` 已在
`clock_correlation.cpp:42-56` 使用，两种硅都验过。

### 3.2 kernel 提交与 event 记录分账

| kernel 已提交 | event 已记录 | 新完成协议应表达的状态 |
| :-: | :-: | --- |
| ✗ | ✗ | kernel 为 `NotStarted`；仍须单独检查已入队 wait |
| ✓ | ✓ | 本流有 fence；整次提交是否 `Complete` 还取决于另一条流 |
| **✓** | **✗** | **`Partial`**；设备已持有 kernel，不能立即回滚 |

陷阱机制：`progress = Partial` 是在 `submit_aicore` **返回 0 之后**才赋的
（`native_run_execution.h:152-160`）。所以把 `aclrtRecordEvent` 的错误当作该 callback
的返回值，会被判成"首次提交之前的失败"。**这是直接将 record 接入现有 callback 时的
重构陷阱；当前事务尚未记录这些 per-run event，不是当前已观察到的 record 失败故障。**

配套三条：

- 每条流分别记录 `{kernel_submitted, event_recorded}`，另记已提交的 wait 引用；
  `Partial` 覆盖 kernel 已提交而 event 未记录的组合
- **已提交但无 fence 的工作要有不依赖 event 的有界 drain/teardown**。
  watchdog 不是替代：AICPU 提交失败后已排队的 AICore 可能在等它，把回收 event 排在后面
  不能产生进展
- 即使证明设备静止，**也不自动证明业务未产生副作用**。只在可证明未执行时重试，
  **不重新运行 host 编排**

### 3.3 event 的引用寿命与失败退出

SDK 明写：销毁 event 前，涉及 `aclrtSynchronizeEvent` / `aclrtStreamWaitEvent` 的任务
必须已完成（`acl_rt.h:1458-1464`）。而 wait 是在 launch **之前**入队的：

```
N+1:  StreamWaitEvent(my_stream, N_event)   ← 成功，任务已在队列
      rtKernelLaunch(...)                    ← 失败
```

按 N+1 回滚释放会销毁队列仍引用的 event；等"N+1 自己的 fence"则永不触发。

- **预留 / 提交 / 撤销**：入队前预留，成功时提交，入队失败则撤销
- 已提交的引用**恰好释放一次**——覆盖该 wait 的完成证明或 §3.2 的静默证明，
  两条路汇到同一次释放；另一条流的 fence 不自动覆盖本流 wait
- **`NotStarted` ≠ 没有任何 wait 入队**；未被设备引用的本 run 参数可回滚，
  已入队 wait 持有的前驱 event 引用仍须保留
- 参数资源与前驱 event 引用允许分开退休；若本 run 没有 fence，不能等待一个永远不会出现的 fence

跨流排序 event 与用于大 args 回收的 timeline event 职责不同，**不能因都叫 event 就混用**。

### 3.4 跨 run join —— caller stream 的入口/出口不在 A 轨

一个 run 占两条流（AICPU Run kernel 在 handshake 里自旋等 AICore），两条流各自 FIFO，
之间没有顺序（`run_stream_pair.h:31`）。用 `aclrtStreamWaitEvent`（`acl_rt.h:1464`）
显式表达 **run 间**的 join：

```
aclrtStreamWaitEvent(后继 aicore 流, 前驱 aicpu event)
aclrtStreamWaitEvent(后继 aicpu 流,  前驱 aicore event)
```

**早先我还写了 caller stream 的入口/出口 join——那属于 kernel 接入，移出 A 轨。**
现有 program 接口**没有 caller stream**：`ChipWorker::run` 的签名只有
`(callable_id, args, config)`（`chip_worker.h:105-108`），执行流由子进程 runner 自己
创建和持有。`caller_stream` 只出现在 kernel-mode 家族
（`simpler_kernel_mode_prepare_callable`，"borrowed for this call only"，
`runtime_c_api.h:623`）。

A 轨要保留的三样：

| 要保留 | 内容 |
| --- | --- |
| AICPU / AICore 的**跨 run** join | 上面那两行 |
| **输入访问顺序** | §4.3 的 Python 旁路规则与 native accessor 调用方合同 |
| 每次执行的**完成、回拷、`RunHandle` 终态** | 每个 run 自己的 fence 决定，不共用整流同步 |

（早先拿 a2a3 的 teardown 协议当跨流序的证明**已撤回**：a5 没有 return gate，
`a5/platform/shared/aicpu/platform_regs.cpp:39-58` 在 AICore 已返回后才把 `DATA_MAIN_BASE`
写回 idle；a2a3 上也证不出 AICPU kernel 最后退出。）

## 4. P3 · 顺序合同、所有权审计与回归覆盖

由 [#2316](https://github.com/hw-native-sys/simpler/issues/2316) 跟踪。三类交付已经合入：

1. 父 run FIFO：#2314，当前后继可 staging，前驱 terminal 前不能激活。
2. build 所有权与收尾：#2356 修复正常/异常入口 recorder job 寿命；#2399 将等待缩到本次 RuntimeContext，避免无关 build 相互拖住。
3. host 输入就绪：#2356 提供真实 bind/accessor/copyback 基础覆盖；#2406 补公开 Worker.submit/RunHandle 路径的前驱值读取、独立/共享只读 early prepare。

当前 main 核查及验证限制见 [P3 交付与 P4/P5 交接](../../review/p3-delivery-and-handoff.md)。#2316 仍 OPEN，正文待办未更新；本地交接已整理，外部验收登记与原始日志/task ID 完整性仍待对齐。P3 当前交付完成，不等于未来多 launched 已通过验收；不另开重复实施任务。

P5 将 terminal 放行改成更早的有序提交边界时必须重验：`submission_closed`
只表示声明结束，不能证明所有任务已按序提交；独立 endpoint 仍需显式依赖 / 同步。

P5 会解开 L3 的两层，那一刻起 ready 队列可以跨 run 交错。**这是正确性问题，必须先定，
不能边写边想。**

### 4.1 危险形状

```
run N   :  A → B      B 写 X，只在 A 完成后 ready
run N+1 :  C          C 读 X，提交时已 ready

多 active ⇒ ready 队列可给出  A → C → B   ← stream FIFO 忠实执行
```

C 在 B 写之前读 X。**无报错、无超时、无诊断——只是结果错。**

而且没有任何现成机制能恢复：`TensorMap` 按 `(RunId, TensorKey)` 隔离
（`tensormap.h:13`），run N 的 producer 在解析 run N+1 的依赖时不可见；
`validate_explicit_deps` 对跨 run 显式依赖直接抛
（`orchestrator.cpp:1176-1186`）。

### 4.2 选定：保持父 run 的提交顺序

**run N 的每个 dispatch 都在 run N+1 的任何 dispatch 之前提交。**

保留今天的语义，不需要新依赖机制，而且**仍然消掉 host 往返**——因为
**排序"提交"不等于等待"完成"**。

另一个选项（扩 `TensorMap` 作用域、允许跨 run 显式依赖）能力更强，但要动 key、
producer 寿命规则和 `validate_explicit_deps`，**不属于 A 轨**。

**per-execution 的 event join 替代不了这一层**：§3.4 排的是 run 边界，它不表达
"哪个 run 的 task 本该先提交"。

### 4.3 Native 入口访问采用调用方合同

`await_run_admission` 的注释写明：直接设备控制 —— malloc / free / copy_* / domain 与
region 创建 —— **没有 TaskSlot**，ready-queue FIFO 不排它
（`orchestrator.cpp:554-563`）。

**但它只保护 Python 直连控制这一条路。** 全仓只有两个调用点：nanobind 绑定
（`worker_bind.h:314`）和 `python/simpler/orchestrator.py:269`。
`runtime_core.cpp` 与 `host_tensor_access.cpp` 里**零引用**。

native HBG accessor（`get_tensor_data` / `set_tensor_data`）直接访问 host view、映射或
调用 `HostApi`，不经过上述 gate。native prepare 可以与前驱设备执行重叠，因此：

```
run N   :  kernel 已提交，仍在读 X
run N+1 :  copy_to(X, 新数据)              ← 走 mailbox，被 await_run_admission 挡住
           set_tensor_data(X, ...)         ← host prepare 中发生，不受设备 FIFO 排序
```

**A 轨首版由调用方保证这类 host 访问的就绪性和无冲突。** simpler 不维护用于此目的的
跨 run tensor 读写集合，无法从本次 accessor 的地址推断哪个前驱仍在读写它。
不新增自动冲突检测、相关前驱等待或跨 run 依赖跟踪，也不承诺违反合同一定能报错。

| 场景 | 合同 |
| --- | --- |
| host orch 读取入口 tensor（包括 device 数据） | 调用方保证值在本次 prepare 前已就绪，并且读取期间无并发写入 |
| host orch 写入入口 tensor | 调用方保证写入期间没有尚未结束的冲突读写 |
| host orch 所需的值由前次 run 产生，或写入会与前次 run 冲突 | 调用方先等前次运行完成，再提交本次运行；涉及 host 回拷时，等待须包含回拷完成 |
| 独立入口，或已经就绪且没有并发写入的共享只读入口 | 可直接提交，保留 prepare 与前驱设备执行重叠 |
| 本次图内已有 producer 的 tensor | 保持 `require_no_producer` 报错；orch 不等待本次 device 的结果 |

合同按**实际底层存储**适用，包含别名、切片和 host-backed staging 的原始 host buffer。
不同 run 的 device staging 地址不同，不意味着原始 host 存储没有冲突；用于 staging 的
输入同样必须就绪，不能和前驱尚未完成的 D2H 回拷竞争。

这不要求所有共享 device tensor 都在 prepare 前完成其生产：**只被设备 kernel 访问**的
共享数据仍可由已建立的提交顺序和双流 join 排序；不同 endpoint 则继续需要显式依赖。
host orch 的 get/set 以及 host staging 不在设备执行队列里，不能借用这个顺序保证。

验收验证守约用法：就绪入口可读、独立或共享只读入口不增加全局等待、调用方显式等待
后跨 run 读写结果正确，以及本次图内 producer 访问仍报错。未同步的冲突用法不要求
运行时检测或自动串行化。**“Native 跨 run 冲突保护”不再作为独立实现任务或 P4 前置门。**

| 工作 | P5 的处理 |
| --- | --- |
| 同一设备队列能排序的 dispatch | 可提前入队 |
| `copy_to` / `copy_from` / `free` / `malloc`、domain / region | 保留完成等待，或给明确依赖 |
| **native `set_tensor_data` / `get_tensor_data`** | 遵守上述调用方合同；保留实际读写及本次图内 producer 检查，不新增隐式跨 run 等待 |
| 跨不同 endpoint 的依赖 | 两个 child 的流之间毫无顺序，提交先后什么也不构成 |

有一条相邻不变量把 **Python 侧**范围框住：同一个 run 内，直接设备控制必须先于该 run 的
首次提交，否则直接报错（`python/simpler/orchestrator.py:255-262`）。**native accessor
不受这条约束**——它就在 orchestration 中途被调用。

反过来也要注意：原来"malloc 等一律等待"是沿用整体 FIFO 的保守策略，
**不是所有新分配都天然依赖前驱**——新资源的创建不必等待无关前驱。

### 4.4 要保留的不变量：同一 callable 的构图串行

**这是约束，不是要做的工作。** 写下来是为了 P5 不要顺手打破它。

`g_current_runtime` 是 orchestration SO 内的**普通全局**
（`a2a3/runtime/host_build_graph/orchestration/common.cpp:39`），每次构图
`entry_points->bind(rt)` 重绑（`runtime_maker.cpp:707`），recorder 依靠这个全局找
runtime（`graph_recorder_pool.h:25`），`wait` 排空**整池**（`:125`）。注释还写明为什么
不是 TLS：glibc TLSDESC stale-resolution 崩溃（BZ #32412）。

> **合同：同一已加载 callable 的 build 串行；保留 build 内部的 recorder 并发；
> build 仍可与前驱的 device 执行重叠。**

最后一句是收益所在。今天 graph 回调本来就序列化（`_submit_mu.exclusive()`），所以
**这个合同是现状**——P5 放开 multi-active 时不能连带放开并发 build。

## 5. P4 · claim 提早释放 + lane 放开非队首（直连路径排队）

进入本阶段前先看两条**从 P1 继承下来、必须在这里处理**的约束，它们不在上面三个机制里，
放开 claim 时不会报错，只会静默失真：

- **§2.3 的共享 header 后备只在单 launched 下成立。** `validate_runtime_impl` 优先读本
  run 自己发布的快照，缺快照时回落去读共享 SM header —— 该读法的前提是本 run 仍持
  claim、后继未 launch、共享 SM 未因 reset/rebuild 失效。**放开 claim 后这条分支必须重做**，
  否则会把后继的 header 报成本 run 的错误。没有快照时允许保留原始执行错误并报诊断缺失，
  不允许未经证明地读可能属于后继的 header。
- **§2.4 的诊断 run launch 独占是过渡门。** 连续采集未完成前，诊断 run 在本阶段仍要独占
  launch：前驱普通 run 退休后才开诊断窗口，窗口收尾前不得 launch 后继普通 run。
  只限制"两个诊断 run"不够。**继续允许 prepare 重叠，不回退 #2201。**

"一个 launched"由公共 claim、lane 及架构各自的执行状态约束：a2a3 使用 RunStreamPair，a5 使用 run_poll_slot_ / run_poll_state_。两者是架构分支，不是同一次运行经过四道门。

| 机制 | 位置 | 改法 |
| --- | --- | --- |
| runner execution claim | `c_api_shared.cpp:940` 取，`:1123` 在 drain 之后还 | 拆成 **submit token**（微秒级，只包 arming + `mark_submitted` + 两次 launch）+ per-run 资源（P1 已完成） |
| chip run lane 单队首 | `chip_run_lane.cpp:196-203`，后继的 `launch_front()` 排在 `finish(前驱)` 之后 | 允许在队首处于 LAUNCHED 时 launch 第二个 FIFO 条目 |
| **a2a3 `RunStreamPair` 单属主** | `run_stream_pair.h:67-99` | a2a3 架构门 |
| **a5 poll 单属主** | `src/a5/platform/onboard/host/device_runner.cpp:526-604`（978123bbd） | `run_poll_slot_` / `run_poll_state_`；须与 a2a3 分支一并迁移 |

### 5.1 `RunStreamPair` 的状态必须拆

`ensure()` 在 launch 时调用，而它对未退休的属主**直接返回错误**：

```cpp
if (aicore_ != nullptr) {
    // A run that has not retired still owns the pair ...
    if (owner_ != nullptr) return PTO_RUNTIME_ERR_INTERNAL;   // ← 拒绝第二次 launch
    if (!stale_) { submitted_ = false; complete_ = false; return 0; }
    ...
}
```

而且 `owner_` / `submitted_` / `complete_` **各只有一份**，由 `ensure()` 重置；
`retire()` 清这些状态，并在 `Unproven` 时销毁 AICore stream。

我早先说"per-run event 化解了单属主问题"——**那是错的**。event 让**完成**变成 run 的
属性，但 `ensure()` 的属主门问的是"**pair 在 launch 时是否空闲**"，event 碰不到这个。

要拆成两层：

| 层 | 内容 |
| --- | --- |
| **pair 级** | stream 句柄、`stale_`（代码发布）、健康/不可用状态、**准入闸** |
| **run 级** | 本 run 的 fence、提交与退休状态 |

### 5.2 失败状态转换规则

今天 `drain_front` 的 poison 分支只处理 **QUEUED / PREPARED** 后继
（`chip_run_lane.cpp:206-239`）；LAUNCHED 的那个一定是队首，先 `wait_native_run` 再
`finish`。多个 launched 之后这套不够，所以规则要写成表而不是提醒。
Phase 取值是 `QUEUED / PREPARED / LAUNCHED / TERMINAL`（`:26`）。

**① 哪些失败停掉整个 pair 的准入**

| 失败 | 作用域 |
| --- | --- |
| `Partial`（kernel 已提交、执行状态不明） | **pair 级**——拒绝新准入直到静止证明。目标合同；当前 `poisoned()` 为 `progress == Partial`，恢复路径设置 `device_unusable_`，`can_accept_run()` 检查它。当前处理强于仅暂停 pair，放宽须另证 |
| stream sync 失败 / 设备不可用（`recover_device_or_mark_unusable`） | **pair 级** |
| 单个 run 的业务失败（校验失败、图内错误码） | **run 级**，不停 pair 准入 |

这是目标分类。当前 `chip_run_lane.cpp:102-108` 对任意 finalize 异常都会 poison lane，
`chip_worker.cpp:877-914` 将非零 finalize rc 抛为普通异常。P4 必须把**执行是否静止、
pair 是否仍可用、业务结果**分别从 native 传到 lane，再按上表决定准入；不能只删除
poison 调用，也不能继续把所有业务失败升级为 pair 故障。业务失败仍须正常上报，
不会因 pair 可复用而变成成功结果。

**② 已入队后继如何进入终态**

| 后继状态 | 处理 |
| --- | --- |
| QUEUED / PREPARED，前驱毒化 | 仅对没有任何执行相关命令入队的后继：记 poison → TERMINAL → 出队。若 launch 尝试已提交 wait，先转交独立引用退休记录，不能按“未提交 kernel”直接释放全部资源 |
| **LAUNCHED** | **不能直接 TERMINAL**。它已在设备队列里，撤不回来——必须等**自己的** fence 或静止证明。提前 TERMINAL 会在设备还在读时释放资源 |

**③ 哪个证明允许释放它的资源**

| 情况 | 证明 |
| --- | --- |
| 正常 | 该 run 自己的 event 对都 fired，且各 event 位于本流相关 wait / kernel 之后 |
| `Partial`，只有一条流有 fence，另一条流完全为空 | 该 fence fired，且另一条流**没有任何命令入队，包括 wait** |
| 某流没有 kernel，但有已提交 wait | 保留该流的前驱 event 引用，直到覆盖这些 wait 的完成证明或有界静止证明；另一条流的 fence 不足以释放它们 |
| `kernel_submitted && !event_recorded` | 该流没有本次 kernel 的 fence；必须有不依赖缺失 fence 的有界静止证明（§3.2），**不能靠 watchdog** |

回收按资源分别记账，**每项恰好释放一次**，与 §3.3 的预留 / 提交 / 撤销规则一致。
“kernel 没提交”“某条流完成”“整个 run 的资源均可释放”不能共用一个布尔状态。

**④ 代码更新何时可以替换旧 stream**

`stale_` 标记之后：拒绝新准入 → 等**所有在飞 run 退休** → destroy + create。
今天 `ensure()` 的 `owner_ != nullptr` 守卫正是这件事的单 run 版本；多在飞就要变成
"在飞集合为空"。期间必须拒绝新准入，否则新 run 会排在即将被销毁的 stream 上。

### 5.3 谁驱动 finalize —— 不需要新线程

早先我担心"没人 finalize 会死锁"，**那是错的**。lane 已经在
`progress()` → `finish()` → `worker->finalize_native_run` 里驱动 native finalize，
由 `ChipRun::done`/`wait_until`（`chip_run_lane.cpp:280,289`）和准入的 `drain_front`
（`:446,489,500`）拉动。准入缺容量时自己会 drain，**bank 一定回来**。

在等待线程里跑的是 Python handle 的 CommDomain / L3-L2 / remote-slot 清理，
是另一回事。**不要加进度线程**——它会把 host 拆成两级流水，反而让深度 2 不够用。

### 5.4 四态必须分开

program 的 `FRAME_STAGED` 可以只是 `VALIDATED_ONLY`，此时尚未 native prepare，
**更不等于已入队**（`python/simpler/worker.py:3262`）。

| 态 | 含义 |
| --- | --- |
| accepted | host 队列已接受 |
| staged | 帧快照已就位（可能仅 `VALIDATED_ONLY`） |
| **enqueued** | **已提交到设备，占据了 runner 内部执行流的顺序位置** |
| completed | 设备完成 |

不能把尚未完成构建的 host 请求冒充已占据 runner 内部执行流顺序的位置。

**到这里直连 `ChipRun` 路径排上队了**，但生产负载还没有。

## 6. P5 · L3 两层（生产路径排队，收益真正落地）

| 层 | 位置 | 门 |
| --- | --- | --- |
| ① L3 Orchestrator | `orchestrator.cpp:183-200` | `activate_fifo_head()` 在 `active_run_id_ != INVALID_RUN_ID` 时直接 return；只有 active run 终止才清 |
| ② WorkerThread 车道 | `worker_manager.cpp:497-511` | 固定 `ACTIVE`/`STAGED` 一对；`activate_prepared` 在 `active.occupied` 时拒绝 |

**这两层在 `Worker.submit` 路径上，生产负载从这里进来。** 只做 P4 而不做 P5，直连路径
排上队而 L3 仍然串行——所以验收必须走 `Worker.submit`。

放开时落实 §4 的提交顺序、Python 旁路完成等待与 native host 访问的调用方合同，
保留 §4.4 的构图串行；不为 native accessor 增加跨 run 冲突推断。

## 7. 取消固定槽位，以真实资源承接背压

**目标设计不保留固定两槽准入上限。** N+2 不应仅因 N 尚未 finalize 而等待一个人为编号的槽位；只要输入已就绪、参数版本有独立存储且资源预算允许，host 可以继续准备并提交。共享可变 device workspace 仍须按执行顺序串行访问。

目前实现仍有 `PTO_PIPELINE_MAX_DEPTH = 2`、按槽分配的结果区和 bank；这是要迁移的现状，不是最终合同。每次参数、结果、event 与捕获引用仍需按最后使用者管理，取消槽位不取消这些寿命。背压来自实际提交队列与资源预算，不能靠无界分配隐藏。

P4/P5 的设计须列明固定槽位的迁移路径；“至少两个 run 提前 launched”仅是最小正确性用例，不代表自然排队已经交付。P6 衡量实际资源压力和吞吐，不再决定是否取消固定两槽。

### 历史模型：固定两槽时为何可能补位不及时

以下公式和测量解释仅限旧两槽模型，不再作为目标设计的时序约束。

早先这一节写的是"2 正好就够拿到全部收益"。**收窄了目标，撤回。**

两槽确实让 **N→N+1** 无 host 等待。但 **N+2 仍要等 N 释放槽位后才能 prepare**——bank 从
prepare 持到 finalize。所以"持续无空档"要求：

```
F(N) + P(N+2) + launch  ≤  D(N+1)
```

（F = finalize，P = prepare/upload，D = 设备执行窗口）

**代码没有任何东西保证或测量这个关系。它是工作负载属性。**

⚠️ 早先我用"hbg host bind 稳态约 1.5 ms vs dsv4 decode 亚毫秒"断言这个关系不成立——
**那个对比不成立**：1.5 ms 出自 qwen 的历史 bind 测量，亚毫秒出自 dsv4 的 device 时间，
**跨负载、跨测量**。

§10.1.3 给出了同负载上的墙钟窗口，但 **P 必须是完整 prepare/upload，不能用 bind 替代**：

| 量 | 实测 | 边界 |
| --- | --: | --- |
| F（drain 后至 finalize 结束） | ~50 µs | `chip.run` 与 `runner_run` 结束时刻之差 |
| bind（P 的一部分） | ~105 µs | `chip.run.bind` |
| prepare 起点至 launch 计时起点 | blocking 145–163 µs；async 298–346 µs | 包含 prepare 工作、prepared 后等待及调度，不能分离出 P |
| D | ~360 µs | AICPU RunWall |
| launch | 未单独拆出 | `runner_run − D` 还含 arming、poll、drain 和调度成本 |

因此不能代入 `50 + 105 + launch ≤ 360` 判断两槽够用。即使拿 blocking 的
145–163 µs 作当前无前驱排队时的参考窗口，也不能当作改造后的 P 实测值。
真实模型的 P 与 D 会同时变化；容量结论仍待同负载、逐次补位验证。

当前 lane 在前驱 finalize 后才 launch 后继，**F 尚未被后继设备执行隐藏**。
async 的 bind 与前驱 runner 窗口有重叠，但这些 host span 未定位前驱真正的设备执行区间，
不能量出 bind 被设备执行覆盖的精确时长。

所以：

| | 定位 |
| --- | --- |
| 两 launched | 最小正确性用例；不能代替取消固定槽位的完整交付 |
| 持续提交 | 不按固定两槽限流；受实际队列与资源预算约束，并做同负载测量 |

**验收要分成两类**（§9），不能把"持续无空档"这种性能目标和"提前入队且不死锁"这种正确性
目标混在一条里。

### 7.1 当前 mailbox 的固定容量也必须纳入迁移

`MAILBOX_TASK_FRAME_COUNT = 2` 是编译期常数，`frames_` 是该尺寸的 `std::array`，
`MAILBOX_SIZE` 从它推导（`worker_manager.h:98-101,533`）。所以在飞数封在 2。

拆法是明确的——frame 里绑了两样寿命和大小差三个数量级的东西：TaskArgs blob
（256-tensor 上限占 36 KiB）只需活到 `FRAME_STAGED`（child 那时已 "retained an immutable
snapshot"），而 `state` / `error` / acceptance word / identity 几十字节要活整个 run。
拆成"少量请求帧（`FRAME_STAGED` 即回收）+ 状态数组"，状态数组的容量是**普通背压**而不是
误配置，错误文本另置存储（现有 `MAILBOX_ERROR_MSG_SIZE = 256`，`worker_manager.h:104`）。

这涉及**跨进程 shm 布局变更**。必须在 P4/P5 设计中交代迁移与兼容边界；具体布局尚待评审，不能把固定两帧继续绑住整个 run 的限制留作可选性能优化。

## 8. 分步与依赖

逐任务 DAG 见 [HTML Now](../../run-pipeline-track-a.html#roadmap)：灰色表示已完成；箭头表示交付前置条件，多个入边须全部满足。状态或验收可以独立变化的任务拆格；P4 的三道 native 门与跨流 join、P5 的 L3 两道门分别联合验收，因此列在同一格。

### 独立支线与 P4 的边界

- 完整 DFX 连续采集不作为普通路径 P4 的前置；连续采集未就绪时，DFX 过渡独占门必须与 P4 一同交付，双向隔离诊断与普通 run 的 launch，保留安全 prepare 重叠。
- #2254 上传优化不作为 P4 的全量前置。若采用共享握手区改造，该实现须证明跨流排序、初始化与发布可见性、设备区域寿命；P4 联合验收实际采用的协议，不能依赖今天单 launched 的 host 顺序。epoch 不替代旧写入者停止证明。
- #2254 内存归一化 M1–M4 是另一独立工作包，尚未分配实施目录；dynamic 当前承接上传优化。P4 可继续使用现有 bank 隔离与共享准入保护，不等待统一池完成。
- DAG 的箭头只表示交付前置；上述条件合同不画成“整条支线 → P4”，避免误读为必须先完成全部优化。

P2 内部有两条可并行的路径：终态 + fence → 无同步读回验证；callback owner → 设备健康消费者。它们与已合入的回拷 / 释放接口汇合到正常 drain。P4 另外要求共享 header 后备收口与 DFX 过渡独占门；完整 DFX 连续采集和 #2254 优化不作为 P4 的必需前置。

```
P0 量收益 ──┬─→ P1 资源安全 ──┐
            │                  ├─→ P4 claim + lane ──→ P5 L3 两层 ──→ P6 背压 / 吞吐验收
            └─→ P2 per-run fence ┘        ↑
                                           │
                P3 合同 / 审计 / 回归─────┘
```

- P1 / P2 / P3 可分工推进；**P1 §2.3 已由 #2283 交付诊断基础，P2 的终态旁路核对已由 #2298 在同一结果区交付，lexical 继续 callback / drain**，不能分别设计两套相冲突的发布机制
- P1、P2 **行为不变**，可以独立合入，风险低
- P3 由 #2316 跟踪：#2314/#2356/#2399/#2406 已交付 FIFO、build 所有权和 host 输入合同；[集成交接与证据限制](../../review/p3-delivery-and-handoff.md)已整理。外部 issue 验收登记仍需对齐，P4/P5 改门时重验。
- P4 需要 P1（资源已隔离或受共享准入保护）+ P2（per-run fence 与不依赖整流同步的正常 drain）+ P3 的 build / host 访问合同检查。
- P5 需要 P4 + P3 合同覆盖，并在改变父 run 放行边界后重新验证 FIFO。

## 9. 验收

`concurrent_prepare_stress` 的四个臂在本改动下**全部继续通过**——它断言的是 *prepare*
重叠，一个本来就成立的性质。**不能当验收。**

### 9.1 正确性验收（是门，必须通过）

失败模式都是"无报错、时间不变、结果错"——gap 测量一条都盖不住。

| # | 断言 | 阶段 |
| - | --- | --- |
| 1 | host / child-memory 入口值可读；图内 producer 结果被拒 | P1 |
| 2 | 两条流的 wait 已成功入队后，注入 kernel launch / event record 失败；已提交 kernel 判 `Partial`，wait 引用不提前释放，无 fence 路径有界退出且每项资源恰好回收一次 | P2 |
| 3 | §5.2 的状态转换：pair 级 vs run 级错误分类、已 LAUNCHED 后继走自己的完成证明、逐流 kernel / wait 引用退休、`stale_` 后的替换时机 | P4 |
| 4 | 调用方合同回归：就绪入口可访问；独立/共享只读入口保留重叠；调用方显式等待前驱及必要回拷后，get/set 结果正确。不要求检测未同步的跨 run 冲突 | P4 / P5 |
| 5 | **确实提前入队**：至少 2 个 run 同时 LAUNCHED（不是 prepared） | P4 / P5 |
| 6 | **连续补位不死锁**：持续提交 N 轮，每一轮都能推进到终态 | P4 / P5 |
| 7 | A→B / C 顺序正确 | **P5 前置** |
| 8 | 同 callable 并发 build 被显式串行 | **P5 前置** |
| 9 | 各类分配压力与部分提交失败 | P1 / P2 |

### 9.2 性能验收（是测量，不是门）

在**同一负载**上测，然后再决定容量：

| 量 | 用途 |
| --- | --- |
| AICPU RunWall 间隔（§9.3 的公共设备时基） | P0 测基线，P5 测结果；不将此口径冒充双流完整边界的空档。**基线已有**：blocking 706–770 µs / async 242–355 µs（§10.1） |
| 吞吐 | 同时报典型周期与含全部长停顿的批次速率、样本数和测量边界。现有长停顿占窗口 82%–96%，暂不能宣称稳态端到端提升；P5 应诊断停顿、拉长序列、交替两臂并记录首尾 host 时间 |
| **补位时间**：`F(N)`、`P(N+2)`、`D(N+1)` | 已有 F 尾部窗口与 D；bind 只是 P 的一部分。prepare 工作与 prepared 后等待未分离，launch 也未单独测出，§7 不等式仍待验证 |
| 相位窗口：prepare 起点至 launch / launch 至 drain / drain 后至 finalize | 可直接由现有 span 计算。窗口增长提示有等待或执行成本变化，不能独自归因于准入。单 run 三段相加等于该 run 墙钟；不同 run 窗口会重叠，不能据此计算提交周期 |

**必须走 `Worker.submit`**，不是直连 `ChipRun`——否则量的是一条生产负载不走的路。

"持续无空档"是**性能目标，不是 P5 的门**。P5 的门是 9.1 的第 5、6 条（确实提前入队、
连续补位不死锁）。

### 9.3 轻量计时开关与跨 run 公共时基

**轻量计时不触发诊断限流，但公共时间坐标还需补充。** 现有采集开关为：

```cpp
bool device_phase_capture_enabled() {
#if SIMPLER_HOST_STRACE                                   // 编译期，默认开
    static const bool enabled = [] {
        const char *v = std::getenv("SIMPLER_DEVICE_STRACE_ENABLE");
        return v == nullptr || std::strcmp(v, "0") != 0;   // 运行期 env，默认开
    }();
    return enabled && HostLogger::get_instance().is_enabled(LogLevel::TIMING);
#else
    return false;
#endif
}
```

（`src/common/platform/onboard/host/device_phase_capture.cpp:20-30`）

三道门全是 **STRACE 宏 + env + TIMING 日志级别**，对 `diagnostics_any()`、`CallConfig`
诊断位、chip-swimlane 级别**零引用**。而 `arm_device_wall_buffer` 在 launch 里是
**无条件**调用的，与 `arm_collectors_for_run` 分开
（`device_runner_base.cpp:1856-1857`）。

开 TIMING 日志级别、env 用默认且编译期开启 STRACE，即可采集单 run 的轻量 timing，
与 §2.4 的诊断过渡限流互不影响。**下面四项是 P0 需要的公共时基要求；第 1–3 项已实现
（§10.1），落地状态与缺口见 §10.4。** 实现前的状况是：

- `c_api_shared.cpp` 将每个 `device_wall` 的起点固定输出为 0，只保留 duration
- `device_runner_base.cpp` 对每个 run 重新选取 origin，子阶段只输出相对偏移
- `containment.py:108-113` 明确这些记录只有 run 内位置，未发布 RunWall 的真实起点

`ts=0` **保留不变**（它是 depth-3 子阶段定位的原点），绝对边界改走 marker 属性。
P0 在已有 `AicpuPhaseRecord` 上补轻量输出，不启用重型诊断；P1 将读回结果归属迁到 per-run：

1. 每个 run 保留并输出完整 RunWall 记录的 `min(start_cycle)` / `max(end_cycle)`，
   同时输出 native run 身份、device 标识、时钟代际及换算频率；不减去各自 origin。
2. 仅比较同设备、同计数器域和同一时钟代际的相邻运行，先做 cycle 整数差再换算。
   缺失、未完成或跨设备 reset 的记录标为不可用，不能把 0 当作有效边界。
3. 首个指标明确定义为 **AICPU RunWall 间隔**。该记录不证明 AICore kernel 的退出时刻；
   若要报告双流完整执行边界的空档，必须另补两侧可比较的边界采集与时基证明。
4. P0/P5 用同一负载、同一口径对比，验证输出能恢复跨 run 时间距离；保留现有相对时间
   输出供单 run 分析使用。host 日志落盘时刻不能代替设备边界。

## 10. 已实测 / 未验证

**已实测（本分支）**

- **op-execute watchdog 只算执行时间，不算排队时间。** a2a3 device 1，经 `task-submit`，
  探针在 `.docs/wdprobe/`。臂 A（1 s op / 3 s 预算）`sync_rc=0`；臂 B（6 s op）
  **`507018` 被收割**；臂 C（3 × 2 s 排一条流）**3/3 完成**，末尾 op 从提交起悬约 6 s 未被
  收割。**臂 B 是让臂 C 成为结论的那一步。此结论只代表这个探针。**
- **GM heap 是 host 不碰的**：`acquire_pooled_gm_heap()` 的结果只用来造 `HeapRebase`
  （`runtime_maker.cpp:905-931`），回拷只走 `tensor_pairs_`（`:1440-1445`）。
- **lane 已驱动 native finalize**，不需要进度线程（§5.1）。
- **`device_wall` 没有可用的设备时间戳**，所以 §1 那条"还不能直接计算跨 run 间隔"由推断
  变为实测：每条 `device_wall` 标记都是 `ts=0 ... clk=dev`，只有 duration。跨 run 的设备
  间隔仍要等 §9.3 的公共时基。
- **一个 run 内部 host 与 device 的占比（§7 那三个数的其中两个）**，见下表。

### 10.1 P0 第三轮：`Worker.submit` 上的 A/B —— **P0 结束**

插桩已随 [#2262](https://github.com/hw-native-sys/simpler/pull/2262) 合入 main
（`4860af980`），所以这一轮不再是本地改动。**而且这一轮走的是 `Worker.submit`**，
补上了 §9.2 那条"必须走 `Worker.submit`"的要求。

harness：`tests/st/a2a3/host_build_graph/p0_submit_gap/`（未提交的量测脚手架，不断言时间）。
同一负载、同一进程、同一计时口径，两臂：

| 臂 | 调用方行为 | depth-2 能不能重叠 |
| --- | --- | --- |
| blocking | `submit(i).result()` 逐个等 | 不能 —— 没有后继可叠 |
| async | `[submit(i) for i in range(12)]` 再统一 drain | 能 |

**每个 run 用独立的 (a, b, out) 缓冲三元组。** 输入初始化在提交前完成，避免
跨 run 的 host 访问及回拷冲突，满足 §4.3 的调用方合同。

a2a3，`--log-level timing`，warmup 3 + 每臂 12 run，取各臂中位数，µs：

| run | blocking gap | 占空比 | **async gap** | **占空比** | D |
| --- | --: | --: | --: | --: | --: |
| 2 | 770.3 | 31.6% | **355.0** | **50.2%** | 356 |
| 4 | 705.8 | 33.8% | **242.2** | **60.2%** | 363 |
| 5 | 747.7 | 32.3% | **323.1** | **52.8%** | 359 |

**正对照**：async 臂的 `chip.run` 之间 host gap 是**负数**（−260 ~ −295 中位，最小 −832），
即相邻 run 的 host span 在时间上**重叠**。这证明那一臂真的在重叠，不是我假设它在重叠。
两臂执行相同 callable 和形状，D 中位数接近，支持典型周期差异主要来自交接间隔。
每臂 12 个 run 只比较臂内 11 对相邻 run，排除 warmup 和两臂切换的间隔。
表中“占空比”是 `median(D) / (median(D) + median(gap))`，仅代表典型周期的
AICPU RunWall 占比，不是整批设备利用率，也不是 AICore 忙碌比例。

三条结论：

1. **今天的 depth-2 已经吃掉了 54%–66% 的设备空档**（706–770 → 242–355 µs），
   占空比从 ~32% 抬到 50%–60%。**所以 A 轨不是从零开始，它要动的是残余那一半。**
2. **残余空档 242–355 µs，而 D 只有 356–366 µs** —— 即使流水线在正常工作，
   设备空着的时间仍和它干活的时间差不多。空档归零的吞吐上限约 **1.7–2.0×**。
3. 这是**当前 async 路径残余间隔的直接测量**，足以支持进入 P1/P2；还没有测量
   A 轨实现的收益，也没有证明整个残余都能消除。§10.3 的 L2 阻塞分解不能直接套到
   这一臂；具体可隐藏的成本及持续提交能力由 P5 前后对比确认。

### 10.1.1 两臂都有的周期性 stall，不能被中位数藏掉

每 11 个 run 里有**一个** run 的设备间隔暴涨，三次三中：

| run | blocking 臂 max | async 臂 max |
| --- | --: | --: |
| 2 | 50.3 ms | 85.6 ms |
| 4 | 48.4 ms | 182.4 ms |
| 5 | 60.3 ms | 81.7 ms |

**两臂都观察到了长停顿，因此它不是 async 独有现象；这不能排除 async 对停顿幅度的影响。**
量级 48–182 ms，比正常间隔大两个数量级。
成因**未诊断**——可能是 host 侧周期性事件（GC、日志落盘）或设备侧 housekeeping。
在拿 P5 的吞吐数字之前必须先定性：一个每 11 次出现一次的 80 ms stall 会主导任何
端到端吞吐对照，而中位数看不见它。

复算各臂从首个 RunWall 开始到最后一个结束的设备时钟窗口（包含全部长停顿）：

| run | blocking 窗口 | async 窗口 |
| --- | ---: | ---: |
| 2 | 62.49 ms | 93.77 ms |
| 4 | 59.82 ms | 190.92 ms |
| 5 | 72.29 ms | 89.95 ms |

三次 async 的整批窗口反而更长；这些窗口不含首尾 host 开销，仍不是端到端耗时。
因此 1.7–2.0× 只能作为典型间隔归零的模型上限。harness 目前也没有检查输出值；
固定 blocking→async 顺序、每臂 12 run 的结果可作立项基线，正式 P5 验收应补齐
计时区外的结果校验、长序列、两臂顺序交替和含长尾的整体吞吐。

### 10.1.2 harness 本身撞到一次 AIV 异常

五次运行里一次失败（`run3`）：

```text
fftsplus aivector error, core id 49, error code = 0x4000000000000000
  → 507018 → bounded device drain failed: 507015 → SCHEDULER_TIMEOUT
```

和 [#1489](https://github.com/hw-native-sys/simpler/issues/1489)（allreduce L3 的
UB 越界）以及 [#2264](https://github.com/hw-native-sys/simpler/issues/2264) 模式 2
是同一族。**它挂在 `run_epoch=3`，也就是顺序 warmup 阶段**（每次都 `.result()`），
不在异步臂里——所以不是异步提交模式引发的。测量时 `[3,5,7,9]` 四张卡被我自己其他
session 占着（`.docs/p0/submit-gap/load.txt`）。

### 10.1.3 相位分解：prepare / launch+drain / finalize

**不需要新插桩。** `chip.run` 的 ts 在 `simpler_prepare_run` 里设
（`c_api_shared.cpp:835,857`），`runner_run` 的 ts 在 `simpler_launch_run` 里设
（`:976`），`runner_run` 在 drain 后收、`chip.run` 在 finalize 收。所以三段窗口是
现有 span 的 ts/dur 算术：

| 相位 | 窗口 |
| --- | --- |
| prepare | `runner_run.ts − chip.run.ts`（含 `bind`） |
| launch + poll + drain | `runner_run.dur`（含 `device_wall` = D） |
| finalize | `(chip.run.ts+dur) − (runner_run.ts+dur)`（含 `validate`/`claim_release`） |

这也**避开了 §0.1 里 #2254 归别人的 prepare/finalize 插桩**，不重复占位。

中位数，µs，三次运行的范围：

| 相位 | blocking | async |
| --- | --: | --: |
| prepare（含 bind） | 145–163 | **298–346** |
| 其中非 bind | 46–51 | **200–240** |
| launch + poll + drain | 474–492 | 529–615 |
| launch_drain − D | 119–128 | 159–257 |
| **finalize** | 44–62 | **42–60** |

代码核查后的读法：

1. **F 时长近似不变不代表 F 已被隐藏。** 当前 `ChipRunLane::finish()` 先调用
   `finalize_native_run()` 再弹出前驱，调用方随后才执行 `launch_front()`。
   `simpler_finalize_run()` 在尾部才释放 execution claim。三份日志中，async 臂后继
   `runner_run.ts` 均晚于前驱 `chip.run` 结束，差值中位数分别是 **41.79、36.69、51.64 µs**。
   因此前驱 F 尚未与后继设备执行重叠；当前单 launched 约束下，也不能与更早前驱的执行重叠。
2. **prefix 是 prepare 起点到 launch 的墙钟窗口，不是 prepare 函数耗时。** 它包含
   `simpler_prepare_run()` 内的工作，以及返回后等待前驱 finalize、lane 调度等时间。
   减掉 bind 后仍含 `prepare_execution()` 的参数发布等工作；差值增大不能全算作五道门等待。
   L3 等待还可能发生在 `chip.run.ts` 之前，本窗口不覆盖所有准入门。
3. **`runner_run − D` 不是等槽位的计时。** `runner_trace_start_ns` 在取得 execution
   claim 后才记录；残差混合 arming/launch、完成检测、drain、调度，以及两种边界的差异。
   当前不存在后继提前 launch 后等待前驱槽位的执行形态。
4. async 臂 bind 与前驱 runner 窗口的交集中位数为 **108.78、96.27、102.84 µs**。
   这是 host 时间域的重叠证据；runner 窗口包含 launch/poll/drain，不能将交集直接
   当作 bind 与设备执行的重叠，也不能据此说 bind 约 105 µs 已全部从关键路径消失。

这三个相邻窗口对**同一个 run**恰好分割 `chip.run` 的墙钟区间；不同 run 的窗口会重叠，
不能把各相位中位数相加当作稳态提交周期。现有 span 足以算窗口，尚不足以分离所有成本归因。

### 10.1.4 有限批次的典型周期与整体速率

| | blocking | async | 比值 |
| --- | --: | --: | --: |
| **中位周期** | 1066–1126 µs（888–938 runs/s） | **608–712 µs（1404–1644 runs/s）** | **1.56–1.75×** |
| **均值周期**（含 stall） | 5335–6463 µs（155–187 runs/s） | 8075–17251 µs（58–124 runs/s） | **0.31–0.66×，更差** |
| 最坏 gap 占窗口 | 82%–85% | 92%–96% |

§10.1.1 那个每 11 run 一次的 stall **占掉 82%–96% 的测量窗口**，所以均值吞吐几乎只在
量 stall，而不是量工作。

这两种统计量回答不同问题：中位周期描述典型交接，包含全部长停顿的批次速率描述本次
有限窗口的整体表现。**可以同时如实报告，不应择一宣称 A 轨收益**；当前没有稳态样本量、
跨环境复现及端到端首尾计时，不能称为端到端吞吐或泛化的稳定加速比。

P5 验收应保留长停顿、交替两臂顺序、拉长序列并诊断停顿来源；若仍未解释，明确保留
不确定性，不能用中位周期替代整体吞吐。

### 10.2 插桩改了什么（已合入 main）
**§9.3 第 1 项已实现**，所以这个数不再是"测不了"。改动三处：

| 改动 | 位置 |
| --- | --- |
| 保留 RunWall 的 `min(start)` / `max(end)` 原始 cycle，不减 origin | `device_runner_base.cpp` `read_device_wall_ns()` + 两个 accessor |
| `device_wall` marker 带上 `dev_id` / `dev_start_cycle` / `dev_end_cycle` / `dev_cnt_hz` | `c_api_shared.cpp` `emit_device_phase_markers()`，新增 `STRACE_DEV_SPAN_AT_A` |
| 语法文档补"跨 run 比较"一节 | `docs/dfx/host-trace.md` |

`ts` 仍然是 0：它是本 run 的 origin，depth-3 子阶段都相对它定位，挪成绝对值会让包含关系
倒过来。所以绝对边界走属性，**先做 cycle 整数差再换算**（§9.3 第 2 项的要求）：

```
gap = (dev_start_cycle(N+1) − dev_end_cycle(N)) / dev_cnt_hz
```

**正对照**：inv=1 的 `(3878755425019 − 3878755391459) × 20 ns = 671,200 ns`，与同一条 marker
的 `dur=671200` **精确相等**；逐 run 无负 gap。

a2a3，经 `task-submit --device auto`，`--rounds 30 --skip-golden`，`inv > 3` 中位数，µs。
原始日志 `.docs/p0/g2-*`，解析 `.docs/p0_parse.py`（其 host 侧输出与官方
`strace_timing --rounds-table` 在 tmr 用例上逐值吻合）。

| 负载 | runtime | D | **设备 gap** | 设备周期 | **占空比** |
| --- | --- | --: | --: | --: | --: |
| paged_attention | hbg | 186 | **458** | 644 | **28.9%** |
| bgemm | hbg | 274 | **2677** | 2950 | **9.3%** |
| spmd_paged_attention_highperf | tmr | 96 | **13839** | 13935 | **0.7%** |
| mixed_example | tmr | 70 | **17662** | 17732 | **0.4%** |

**设备在一个周期里有 71%–99.6% 的时间是空的。**

### 10.3 blocking 臂上的分解 —— **上界推断，已被 §10.1 的实测取代**

> ⚠️ **本节原来把"`chip.run` 内的 host"标成"A 轨可动"，那是高估，收回。** 两个原因：
> 一是那部分 host 工作**不会消失**，排队只是把它藏到前驱的设备执行后面，设备窗口装不下
> 时就藏不住（`spmd_pa_highperf` 的 13 ms bind 对 95 µs 设备窗口正是如此）；二是这个推断
> 已被 §10.1 的直接测量取代——真实可动量是 async 臂的残余空档 **242–355 µs**，不是
> `chip.run − D` 的 645 µs。本节保留为 blocking 臂上的**上界**与测量链正对照。

gap 可以按 `chip.run` 边界拆成两段，而且加起来对得上（这是整条测量链的第二个正对照）：

| 负载 | `chip.run` **内**的 host（可动量的**上界**） | `chip.run` **外**的 host | 相加 | 实测 gap |
| --- | --: | --: | --: | --: |
| paged_attention hbg | 360（79%） | 99 | 459 | 458 |
| bgemm hbg | 685（26%） | 1995 | 2680 | 2677 |
| spmd_pa_highperf tmr | 13239（96%） | 207 | 13446 | 13839 |
| mixed_example tmr | 1412（8%） | 15824 | 17236 | 17662 |

- `chip.run` **内**的那段是 runtime 自己的 host 关键路径。**它是上界，不是收益**——
  §10.1.3 确认 bind 与前驱 runner 窗口重叠；finalize 仍阻挡后继 launch，
  不能从各相位时长直接推导已隐藏或可消除的成本。
- `chip.run` **外**的那段是 scene-test 的 round 间张量复位、golden 路径，加上 Python 提交
  路径。**大部分不是 runtime 成本**，不能算作 A 轨收益。mixed_example 的 `validate` 单独
  就有 674 µs，host gap 15.8 ms——那个用例的空档基本全是测试框架。

限定：

- **不是 `Worker.submit` 路径。** 这些用例走 L2 阻塞 `worker.run`，量到的是**零重叠**下的
  串行分解。§10.1 已在 `Worker.submit` 上补齐。
- **不是生产规模。** 设备窗口只有 70–274 µs；真实模型单 run 的 device 时间大得多。
- **F 现在有窗口了**（§10.1.3 的 `finalize`，44–62 µs），虽然仍没有专属 span——它由
  `chip.run` 与 `runner_run` 的 ts/dur 相减得到。`F(N) + P(N+2) + launch ≤ D(N+1)`
  中 F ≈ 50、D ≈ 360 µs；bind ≈ 105 µs 只是 P 的一部分，完整 P 与 launch 尚未独立量出。

### 10.4 首轮的 host 侧细分（仍然有效）

第一轮量的是 `chip.run` 内部的分解，被 §10.1 的设备口径补充而不是取代。同口径、同方法，
`--rounds 30 --skip-golden`，µs。paged_attention 与 spmd_pa_highperf 每 round 发**两次**
`chip.run` 且分成两个清楚的档（中间停 189 ms），故分行；档的成因**未确证**（用例声明了
AIC/AIV 两个 in-core 变体、各有 workspace 尺寸，能解释，但没验）。

| 负载 | runtime | `chip.run` | `bind` | D | `chip.run` − D |
| --- | --- | --: | --: | --: | --: |
| vector_example（无 skip-golden） | hbg | 585 | 93 | 196 | 389 |
| bgemm | hbg | 759 | 183 | 274 | 485 |
| paged_attention 档 A / B | hbg | 546 / 420 | 107 / 35 | 181 / 171 | 365 / 248 |
| mixed_example | tmr | 899 | 140 | 73 | 826 |
| spmd_pa_highperf 档 A / B | tmr | 9825 / 14217 | 9458 / 13801 | 72 / 102 | 9753 / 14115 |

1. **host 在关键路径上从来不小。** hbg 是 device 的 1.4–2.0 倍；tmr 是 11 / 135 / 138 倍。
2. **`bind` 不等于 host 成本。** bgemm 的 485 µs host 里 `bind` 只占 183（38%）；
   `runner_run` 减 D 还有 139，`validate` 41，另有约 120 µs 落在任何现有 span 之外。
   **只把构图挪出关键路径拿不到大头**——直接影响 P4/P5 的收益估计。
3. **10 ms 级的 `bind` 是单个用例的属性，不是 TMR 的属性。** 那里面 `bind.args` 占 99%
   （`bind.prebuilt` 1.3 µs，预构件命中缓存），而 `bind.args` 罩的是 `stage_device_args`
   ——**每 run 对 host-backed tensor 做 device_malloc + H2D**
   （`tensormap_and_ringbuffer/host/runtime_maker.cpp:509`）。同 runtime 的 mixed_example
   只有 140 µs。host 侧还对整机负载敏感：同一用例在更忙的机器上从 9.4 ms 变 12.3 ms。

### 10.5 §9.3 四项的落地状态

| 项 | 状态 |
| --- | --- |
| 1 · 输出 RunWall 原始 `min(start)`/`max(end)` + 换算频率 | ✅ 已实现 |
| 1 · run 身份 | ✅ 由 marker 的 `(pid, inv)` 提供，`chip.run` 那条带 `run_id`/`slot_id`/`generation` |
| 1 · device 标识 | ✅ `dev_id`，解析器按它拒绝跨设备相减 |
| 1 · **时钟代际** | ❌ **未做。** device reset 后 cycle 不再可比，而现在没有任何东西能发现这件事。需要单独设计一个失效判据 |
| 2 · 只比同设备同时钟域、先整数差 | ✅ 解析器如此实现，文档写明 |
| 3 · 指标定义为 AICPU RunWall 间隔，不冒充双流边界 | ✅ 文档与 §10.1 都注明；**它不证明 AICore kernel 的退出时刻** |
| 4 · P0/P5 同负载同口径对比 | 待 P5 |

另外 **sim 平台没有跟上**：`sim/host/c_api_shared.cpp` 的 `device_wall` 由 `steady_clock`
推导，只发 `ts`/`dur`。语法文档已注明这个平台差异，没有半做。

**未验证，且各自卡住一条结论**

| 项 | 卡住什么 | 何时要答 |
| --- | --- | --- |
| **A 轨可消除的残余间隔、长停顿来源及整批吞吐** | §10.1 已测当前 async 的典型残余，尚未证明改造收益 | P5 验收 |
| 实际 launch 遇队列压力是阻塞还是返回哪类错误 | 自然排队必须有明确背压出口 | P4/P5 合同确定时 |
| watchdog 的有效预算 | 三臂都回报 `actual = UINT32_MAX µs`（请求 3 s），臂 B 却在 3.93 s 被收割。若钳制是下限，则低于约 4.29 s 的 `SIMPLER_OP_EXECUTE_TIMEOUT_US` 静默失效，**含 CI 的 2 s** | 与 §3.2 的 partial 终止路径相关 |

## 11. 一页回顾

| 问题 | 答案 |
| --- | --- |
| A 轨的交付物 | **划分变了**：从 `{执行 + prepared}` 变成 `{执行 + launched}`。取消固定两槽准入；至少两个 launched 是最小正确性验证 |
| 收益在哪 | 将前驱 host 收尾与后继设备执行重叠；实际交接间隔和端到端收益由同负载测量确认 |
| 最先做什么 | **P0 已完成**（§10.1）。插桩合入 main（#2262），`Worker.submit` 上的 A/B 三次复现：depth-2 今天已吃掉 54%–66% 的设备空档（706–770 → 242–355 µs，占空比 32% → 50%–60%）。**A 轨要动的是残余 242–355 µs**，与 D（~360 µs）同量级，空档归零上限约 1.7–2.0×。#2268 已完成 P1 计时隔离；P2 的 #2272 已合入，下一步解决 #2267 的正常 drain 错误通道，#2275 已完成 arena 准入规则明确化，TMR 错误诊断保存已由 #2283 合入；公共终态接口已由 #2298 合入；#2303 已合入，lexical 继续 #2267 的健康与 drain 接线；DFX 临时独占随 P4 接入，连续采集独立推进（§2.4） |
| 持续无空档的条件 | 提交准备速度、设备消费速度与可用资源共同决定；§7 的 F/P/D 不等式仅适用旧两槽模型，不约束目标设计 |
| 需要新 API 吗 | 不需要 |
| caller stream join | **不在 A 轨**——program 接口没有 caller stream，流由子进程 runner 持有 |
| 需要改 mailbox 吗 | 当前两帧与整次执行寿命绑定的上限须纳入 P4/P5 迁移；具体跨进程布局另行评审 |
| 最大的风险 | 多 active 父 run 的跨 run 乱序——无报错，只是结果错。P3 先定合同 |
| "一个 launched"谁钉的 | 公共 runner claim、chip lane，加架构门：a2a3 `RunStreamPair` 或 a5 poll 单属主；两架构均须迁移 |
| native 访问如何保证顺序 | 首版采用调用方合同：host orch 访问前保证入口就绪且无冲突；有前驱依赖时先等待再提交。不自动识别/等待跨 run 冲突；本次图内 producer 仍报错 |
| 需要加线程吗 | **不需要**。lane 已在驱动 finalize |

## 12. 实施范围（保守版）

先**保留**现有 HBG 私有 arena / image 和构图方式，只做：

```
多在途状态（三个机制）+ 资源退休（native 访问采用调用方合同）
    → 直连排队（P4）
    → L3 排队（P5）
    → 持续提交验证 → 按实测决定容量扩展
```

**不必先做**纯 tiling、字段共享化、跨调用缓存。

另外三条明确保留：

- **TMR 的兼容性 probe 留在 prepare 前**——缓存 miss 时 arena 在 bind 内就建好上传
  （`tensormap_and_ringbuffer/host/runtime_maker.cpp:853-860`），挪到 launch 门来不及
- **错误状态快照不能依赖 host 在相邻 run 之间抢读**——后继在前驱 fence 之后立刻开跑，
  那个间隙里没有 host（§2.1、§2.3 都是这个理由）
- 性能验收使用不会触发"诊断限一在途"的计时路径，并保留跨 run 公共时基（§9.3）


## 当前续办 — 2026-09-20T09:58:37Z

#2401 已开2f10b4cc6，原会话59.78%接生产回归反馈；a5 CI初始化507033证据已保存，不归因、不重试。#2400 新head65001a45c静态PASS，格式golden及所有权反馈闭合，CI仍运行。dynamic54251/P4 86846通过上下文门禁继续最终源码合同修订，仅设计。ready0，2399保持已合入，10分钟timer继续。


## 定时续办 — 2026-09-20T10:04:09Z
#2400 macOS新增golden失败已定位字段存在性差异，原82867修测试；a5初始化507033证据保留，不归因或重试。#2401新heade3fadc181更正测试声明，原96728尝试直接生产snapshot回归。dynamic63268/P4 44090仍在原会话完成设计修正。ready0，10分钟继续。


## 方案交付 — 2026-09-20T10:06:25Z
Dynamic最终初始化前缀方案已审，用户批准问题已发送；原63268置闲待答，不提前实施。P4原11049补具体pending budget与retained RunState边界；其余两PR仍原会话修复，10分钟timer保留。


## 定时续办 — 2026-09-20T10:13:56Z
2400新head3235c9575的macOS golden修复静态PASS，等CI。2401新head78519d39b直接生产snapshot回归已接受，发现GNU-only链接参数，原32630修Apple兼容。P4原84236补可用public配置；dynamic仍等用户批准。ready0，10分钟继续，无手动CI重试。


## 当前核对 — 2026-09-20T10:23:47Z
2401新head22b35bc0静态PASS，Apple链接修复已审；2400同head3235c9575静态PASS。两者CI仍运行且当前无失败，ready0。P4可用公开配置方案已审并询问用户批准；dynamic另项批准仍待答。所有原Claude已交付置闲，manager继续10分钟CI观察；无重试或合并。
