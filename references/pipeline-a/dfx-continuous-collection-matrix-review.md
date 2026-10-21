> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# §2.4 矩阵复核

> **历史设计 / 评审快照**：保留文中的原始基线、结论与验证记录，不代表当前待办或支持范围。当前进度统一见 [路线与阶段表](run-pipeline-track-a.md) / [HTML 看板](run-pipeline-track-a.html)。

## 第二轮：修订矩阵与身份验证方案

本轮对象：[身份验证方案](dfx-run-identity-verification-plan.md) 及修订矩阵。
仍仅静态核查，未实现或运行测试。

**载体选择通过：复用 cpput 制造消费滞后，设备场景只验身份接线，保持生产准入不变。**
scope_stats 去重与 residency 输出时序两项错误推断已经撤回；buffer 每 run 领取、
AICore 独立程序、输出格式与 #2267 的边界也已纠正。旧评审保留在后面作为历史。
实施前还需收紧以下三项，不必因此扩大到会话改造。

### 1. 让断言落在生产逻辑上，并区分 host 单测与设备可见性

`test_profiler_device_engine.cpp` 确实执行真实 `DeviceProfilerEngine<FakeModule>`。
但现有引擎只提供 enqueue/pop/switch；没有 run 身份、run-end flush、
累计 published 记账和 host 复制/归还逻辑。只给 FakeBuffer 手填 epoch、再由测试分组，
证明的是测试自己维护的标签，而不能发现某个 collector 漏写或晚写 epoch。

给 A1–A5/B/C 标出具体调用的生产函数、真实 wire 类型或复用 helper，以及 fake 的边界。
至少有真实 producer 的领取/盖章/尾 flush 和实际 host 解析路径接受同一组输入；
其余 collector 明确标为尚未覆盖。现有 `test_dep_gen_collector_aicpu.cpp` 实际链接
device writer，也不是方案表格所称的 host 侧测试，可作为一个真实适配器的候选。

`CommitUsesReservedBufferGeneration` 适合 E2，但它手工写 head，只执行 AICore reserve/commit。
它不能验证 E1 的 AICPU prime/rotate，也不能证明缓存可见性。E1 分成：
生产发布代码的写入/提交顺序核查（有条件则单测观察）和设备上的身份传递证据。
不要把 host 上通过等同于 device release/flush 协议已证实。

### 2. 明确 C4 的完成标记，或从本轮验收中移出

方案 C4 要求“完成标记不因 drop 丢失”，D 又规定空 run 没有 ready entry，
但尚未指定标记的存储、身份、发布者和消费方。
如果标记放在普通 ready queue，queue 满时为何不会一起丢失，需要具体协议。
如果指 #2267 的执行终态，它不证明 DFX 记录已经完整消费。

本轮可只验证数据/drop 的身份和守恒，把 DFX 会话完成控制记录留到第二步；
若保留 C4，就先定义空 run、全丢弃、尾 flush 失败和异常退出时标记的独立保证。
不得仅在 fake 添加一个永远成功的 complete 布尔值。

### 3. 覆盖真实计数与归还复用，别只断言“不是旧公式”

场景 B 应读取生产记账结果：两次各 1 条分别可消费，总计 2，drop=0；
在每次写入、成功/失败提交和归还后检查合同。
删除“断言新值不等于 2−B”的纯算术断言，用场景注释说明它为什么能捕获旧公式即可。
ready queue 满和 free pool 空分开设置：前者是尚未转移所有权，后者可能已发布旧 buffer，
因此 drop 和 current_ptr 的预期不能混为一类。

在 A/D 后增加第三次非空 run：消费并复制 N 的 B0 → 归还 B0 → 新 epoch 再领取 B0。
验证旧 host 副本保留 N 身份、新 buffer 获得新身份。现有“只滞后不归还”只能覆盖寿命的前半段。
顺序设备 artifact 检查也不能单独证明它实际复用了同一 buffer。

### 其他文字与范围修正

- epoch setter 前移可用；保留参数校验，并说明它会移到 affinity filter 之前，
  被过滤线程也可能执行相同原子写入，不能直接声称动态执行成本绝对为零。
  同步两架构，并与 lexical 对该 setter 的修改衔接。
- E3 不应写成“单调 seq 必然破坏 cached_buf_seq”；不等比较可以支持单调代际。
  本次不改语义即可。若下一 run 从 0 重启，则测试还应验证 AICore local cache 已初始化，
  不能把同一 local state 下 0→0 当成会自动重新加载。
- 第一层只覆盖身份与所有权，保留原 begin_run/quiesce 和对账行为。
  若改生产计数公式，应在同批提交保留并验证当前逐 run 口径，不先开启连续会话。
- Layer 3 已能输出并核对 epoch 时，优先用它完成来源验证，不再另加每条记录的设备日志。
  若仍需一次性 probe，只在冷路径观测；不向 AICPU 记录热路径加打印。

完成以上补充后，可开始身份字段与对应新增 case 的同批实现；无须再重开载体选择。

## 第一轮评审记录（对应旧矩阵）

对象：[五 collector 矩阵](dfx-continuous-collection-matrix.md)。仅静态分析，未跑用例或 probe。

## 结论

先建立归属与 buffer 所有权协议、再解除逐 run 清理，方向成立。
但现存缺陷推断和若干实施细节需要修订；不建议把 residency 失败作为身份改造的第一步探针。

## 必须纠正的结论

### 1. scope_stats 的现有去重键没有被证明跨 run 泄漏

`src/common/platform/shared/host/scope_stats_collector.cpp:157-165` 的 `begin_run()`
同时清零 `recovered_current_buf_` 和 `recovered_current_total_`。
这也是矩阵 §1.1 自己记录的事实。仅凭设备 total 每 run 清零，不能得出下一 run 被旧键误去重。

将 §1.5 和实施步骤 6 改成“取消逐 run 重置后需要明确去重身份”，而非已发现现存缺陷。
如仍怀疑今天有 bug，需要另给绕过 begin_run 或跨窗口并发写回去重状态的具体路径。

### 2. 当前 run 返回前会同步完成正常采集收尾

`src/a2a3/platform/onboard/host/device_runner.cpp:735` 在 reap 中调用采集 teardown；
`device_runner_base.cpp:2685` 起按顺序 quiesce、reconcile、export。
`ProfilerBase::quiesce():918-945` 明确等待设备队列与 host shard 消费完成；
`chip_swimlane_collector.cpp:1339` 附近在导出函数返回前关闭输出文件。
这不能证明没有 collector bug 或导出错误，但排除了“当前 run 本来就只保证执行完成”的解释。

residency 测试实际先顺序执行三个 run，再逐个检查三个 artifact，并非每次返回立即检查。
今天它要求完整文件符合现有实现；未来引入连续会话后才改变输出完成合同。
不要先把断言延后到 close 来掩盖未解释的已有失败。

### 3. buffer 身份、轮转代际与记录计数必须分开

矩阵 §1.4 选择每 run 尾 buffer flush 并换 buffer，步骤 5 却要求同配置后继不再 pop，
两者冲突。服务/池不重建不等于不能从同一池领取新 buffer。
已经提交给消费者的尾 buffer 必须等待归还，不能由后继继续写。

跨 run 单调 seq 是可选方案，不是身份成立的必要条件：`run_epoch + producer + local_seq`
也能唯一标识 buffer。若 AICore 观察 seq 作为轮转代际，需独立保持该协议要求。

`chip_swimlane_collector_aicpu.cpp:712-745` 的 `total - seq * capacity`
依赖前面的轮转都是满 buffer。跨 run 的未满尾 buffer 也递增 seq 时，不能照搬该公式。
例如容量 B、两个 run 各产 1 条，累计记录 2，第二个 buffer 的 seq=1，`2-B` 已经错误。
应明确当前 buffer 的有效记录数、累计已发布记录数、drop 和 buffer 代际各自如何记账。

### 4. epoch 的可达性论证未覆盖全部实际入口

`src/a2a3/platform/onboard/aicpu/kernel.cpp:87` 调用 `set_platform_scope_stats_base()`，
而 `:129` 才设置本 run 的 result epoch。矩阵声称五个 init 都晚于该设置，不符合调用顺序。
这不说明当前代码读错 epoch（它尚未读取），但在这些初始化函数直接新增读取会有风险。

swimlane 还有 `src/a2a3/platform/onboard/aicore/kernel.cpp` 的独立 AICore producer，
不能用 AICPU SO 内的 getter 直接证明它已获得身份。
可由 AICPU 在发布 buffer 前固化身份供 AICore 使用，但需写明具体写入点与可见性顺序。
应逐 producer 核对：epoch 来源 → 写入不可变 buffer 元数据 → ready 发布 → host 复制 → 归还。

### 5. JSONL 只是可参考的追加方式，不能一律替换五种输出

`HostPhaseRecordStore::write_records_jsonl(path)` 是“向调用方传入路径追加当前 pass”，
不保证不同 run 的 path 相同；其 invocation_id 也不是未经映射就等于 native run_epoch。
矩阵尚未定义会话输出目标和既有 per-run output_prefix 的关系。

swimlane JSON、PMU CSV、args dump 的二进制数据与 manifest、DepGen 图各有消费者。
先定义会话容器/manifest 和身份键，再决定哪些格式需要变化；不要求统一 JSONL。
明确持续落盘/分块及回收，避免仅在会话关闭导出导致 host records 无限累计。

### 6. #2283 不能充当成功终态

§2.2 的“run 结果完成由 fence + #2283 结果区保证”缺少 #2267：
#2283 只保留失败诊断。目标应引用 #2267 的双流边界与有效终态协议；
当前成功/失败判定仍有整流同步。不得让 DFX 设计提前假设错误通道已完成。

## 第一份新增 case/probe 应验证什么

先修订协议，然后设计有针对性的最小场景，而非直接重跑 residency：

1. 同配置 N/N+1 重复 task_id，各只写未满尾 buffer；消费 N 人为滞后。
2. 验证 N 的 payload 和身份在归还前不变，N+1 使用另一可用 buffer；两 run 均归属正确。
3. 空 run 与 ready/free 队列受压时，记录数、drop、尾 buffer 和完成标记仍可对账。
4. 同时验证 AICPU 和 AICore producer 的身份来源；先保留生产准入和逐 run 屏障。
   要制造跨窗口滞后，需独立低层 harness 或 buffer 协议测试，不能简单提前释放生产 claim。

residency 的已有失败另行定位具体 artifact/count/timestamp 断言与对应日志；
它可以帮助诊断现有 collector，但目前没有证据将其归因为“两个完成被混用”。
以上仅为新增验证方案，本次不执行。
