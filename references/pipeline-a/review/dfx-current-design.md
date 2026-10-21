> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# DFX 当前整体设计

> 当前更新 2026-10-08T06:44:03.912023+00:00：用户已批准[交接身份方案](dfx-handoff-identity-assessment.md)，原temporal在最新main121d807bb上实施一个PR：ready entry携带run_epoch/count、按run数字丢失和ACK不确定状态的安全处理。旧一律null/global撤回；本PR先建立交接保证，不开放诊断提前入队/capture。30分钟兜底与完成触发已恢复；尚无新PR或测试结论。

[此前在制记录（已归档）](dfx-design-historical-status-before-20261008.md)

核对基线：maina711032ed（仅新增API说明，运行时仍与01bf相同），ChipSwimlane、ORCH_PHASES、PMU、ArgsDump、ScopeStats、TMR DepGen与HBG主机图后台能力已分批合入。本文描述普通多次运行的整体DFX，不要求当前接通capture/replay。

## 1. 对外模型和职责

Worker/context拥有可复用collector、buffer池、host服务线程。每个run拥有身份、采集配置、记录归属、输出目标和完整性结论。封存后export对象拥有自己的记录与元数据，writer不再读取下一run的可变collector状态。不要求用户新建“session”；公开选项为collect_across_runs，旧dfx_session和输出中的session_id仅为兼容保留，不能解释为额外用户生命周期。

### 实例究竟属于谁

共享的是实现，不是全局实例。ChipWorker从选定runtime的host库加载create_device_context；该函数new一个DeviceRunner。DeviceRunnerBase以普通成员拥有ChipSwimlane、ArgsDump、PMU、ScopeStats collector，DepGen在对应架构子类中。因此准确粒度是每个runner/device context各一套，而不是每个runtime类型全局一个，也不是每张物理卡全局一个。同一个runner的多次run复用；不同runner/context的池、队列、记录、预算相互独立。

```text
ChipWorker A -> HBG host库 -> DeviceRunner/context A -> collectors A -> run N / N+1
ChipWorker B -> TMR host库 -> DeviceRunner/context B -> collectors B -> run M / M+1
```

这只是所有权示意，不表示允许两个算子同时占设备执行。平台差异由内存分配/映射/复制回调解决；runtime差异由生产端和配置解决（HBG host阶段 vs TMR设备阶段、各自task编码），输出metadata.runtime供工具正确解码。每run配置在execution claim下发布；改变核数/AICPU线程数则先finalize排干旧采集再建池；缺TMR编排池的跨run等级升级安全拒绝。同一collector复用的前提是兼容配置和正确寿命，不是无条件接受任意runtime切换。

代码：common/worker/chip_worker.cpp:215,346；a2a3/a5/platform/onboard/host/runtime_c_api.cpp:27；common/platform/onboard/host/device_runner_base.h:2471；a2a3/platform/onboard/host/device_runner.cpp:1419。

DFX包括性能/时间线、硬件计数、参数内容、依赖图、资源占用及日志/错误。共享buffer传输框架不代表所有模块已经具有相同跨run导出能力。

| 数据 | 主要写入者 | 消费结果 |
|---|---|---|
| task时间和调度/编排阶段 | AICore、AICPU；HBG构图阶段在host | ChipSwimlane时间线与阶段分析 |
| PMU硬件计数 | AICore采样、AICPU交接 | PMU结果 |
| ArgsDump | AICPU记录参数及payload描述 | 参数文件及任务复现工具 |
| DepGen | 编排/依赖构建路径 | 依赖图 |
| ScopeStats | runtime作用域统计路径 | 资源峰值统计 |
| host/device日志与运行错误 | 对应runtime层 | 日志和调用错误；不依赖swimlane文件发布才传播 |

## 2. 写入方与services交互

常规设备collector链路：writer写当前buffer → ready队列发布buffer → host drain读取/复制 → collector解码归类 → done/recycled回收 → drain补充free队列 → writer获取替换buffer。不是每条记录都向host做一次RPC。

services是host运行中的收取、补充和回收职责：drain线程负责所属队列，collector线程负责解析记录，replenish负责回收路由及允许的池补充。一个设备free队列只有一个运行期host写者。设备可见字段只按所有权更新，避免整体拷回覆盖设备刚写的字段。A3共享可见内存与A5显式复制通过平台适配层实现，并非假设CPU指针天然可被双方安全读取。

AICore task记录写进自己的已预留位置；AICPU负责相关轮转和交接。设备任务完成信号、记录提交、host取走是不同事件，buffer必须满足最后写者与读取者寿命才能重用。HBG的host阶段记录直接按run交给collector，不绕到设备再取回。

### services怎样调用具体collector

这里的services是ProfilerBase中管理线程、队列和生命周期的公共实现。ChipSwimlaneCollector继承ProfilerBase<ChipSwimlaneCollector, ChipSwimlaneModule>；公共collector线程从host ready队列取出buffer后，由consume通过static_cast<Derived*>(this)->on_buffer_collected(info, shard_index)直接调用具体类型。这是CRTP编译期绑定，不是给全局服务注册函数指针。回调返回后，公共基类推进处理计数并notify_copy_done，交给回收路径。

运行期回调用在不同位置：MemoryOps以std::function提供分配/注册/释放/设备双向复制；ThreadFactory以std::function提供线程创建。前者解决平台操作差异，后者保留线程环境；二者不是记录解码的分派机制。代码：profiler_base.h:2034、chip_swimlane_collector.h:360、buffer_pool_manager.h:127。

## 3. 普通多次run如何收集

collect_across_runs=False保留原逐run收尾/导出行为。显式启用时，当前跨run持留与后台发布已覆盖ChipSwimlane、PMU、ArgsDump、ScopeStats及TMR DepGen，范围为本地L3的chip子进程；不能推广为DepGen全部后台化，也不推定远程/L4/kernel/capture已经支持。

以下流程以ChipSwimlane为例；PMU的容量和成功规则差异见后文。一次run：
1. 占用未发布run名额并登记身份；没有名额时等待writer释放。启动前collector各分片必须认识该run。
2. 设备运行时持续收取，buffer按身份进入所属run；晚到的N记录不能塞进N+1。
3. N设备停止后，尚持有执行独占时读取本run终态，并为每条ready队列建立有限边界。这个边界圈定N应当交出的内容，不等待所有后续run让全局队列清空。
4. 边界交接完成后可运行N+1；host继续接收/处理N，确认对应队列进度以及collector引用均已退出，才封存N。
5. writer取独立导出对象、写文件、释放保留存储和名额。后台自动进行，不依赖用户每run调用flush。

设备仍按完整算子串行，诊断独占仍在。允许的是N的host接收/封存/写盘与N+1设备执行重叠；并未消除每run设备完成与边界交接。

## 4. 完整性为什么不能只看文件存在

分开统计：producer成功交接的buffer/record数与结尾live数；host实际接收数；预算内实际保留数；最后发布结果。成功交接只在ready发布成功后计数；计数接近极限时拒绝继续，不能回绕后伪造闭合。host预算拒绝保留时仍回收传输、继续记接收数，不把“收到了但未保留”伪装成设备没交出。

终态读取失败、producer覆盖不完整、transport边界未知、记录/元数据缺失均要在结论中区分。可安全发布部分结果时带partial及完整性字段；不能证明引用释放时隔离存储。ChipSwimlane的flush成功证明截至边界有对应可读产物，不保证每条产物完整无缺。PMU CSV没有完整性字段，因此不完整、边界未知或写盘失败都会使flush持续失败；证明为空的run不写文件且仍成功。写文件失败等不产生文件的结果让flush/close失败，不只写一条日志了事。

## 5. 预算和背压

有三种独立压力，不能笼统叫“满了就丢”或“永不丢”：
- 设备ready/free不足：AICPU在本lane交换点等待host进展，有超时兜底；这会干扰被测时序。
- host保留记录超过预算：停止保留相关内容但继续运输回收，结果显式不完整。
- 两个未发布run名额都在用：ChipSwimlane准入等待后台发布；PMU、ArgsDump与ScopeStats在launch前拒绝新run，调用者可先flush。不同collector不能混写为同一种背压。

当前默认每个retained ChipSwimlane collector的host预算256MiB，最多2个未发布run，包含正在封存或写出的run。它不是整个Worker/多卡/设备的总内存上限；设备池、对应host-shadow及其他固定存储有各自边界，多设备需分开计算。两名额是保留资源上限，发布后可持续补位，不限制总共只能跑两次。PMU另有独立的两未发布run名额和初始配对池字节数两倍的增长上限；不继承swimlane的256MiB主机预算，也不承诺整个DFX内存或磁盘总额封顶。

## 6. 用户可观察边界

| 操作 | 承诺 |
|---|---|
| run完成（跨run收集开启） | 计算及该run要求的完成/回拷边界已完成，不能据此断言大记录文件已经写完；本run时间对齐文件仍在返回前完成 |
| 等全部run结束，再flush_diagnostics | 等截至关闭水位的文件发布；swimlane允许带partial结论的产物，PMU/ArgsDump/ScopeStats不完整必须报错；PMU证明为空允许无文件；后台失败/等待超时会报错 |
| Worker.close | 排干工作并收尾诊断，之后停止线程/释放资源；不能靠先杀collector再丢弃晚到记录制造成功 |

flush不得从构图回调内部调用，公开入口要求没有未结束run；它通过已有父子control通道完成。timeout用于相应等待，不是包括所有锁获取在内的严格整调用墙钟上限。

## 7. 已交付与差距

已经形成：run身份与最终写者保证、成功交接对账、独立导出所有权、ChipSwimlane跨run后台发布、有界保留与显式完整性。#2451补齐PMU按run冻结配置、有限cut、后台CSV、sticky错误和双collector共享flush等待预算。#2445只是把编排阶段接入这条已有链路：TMR设备编排、HBG主机构图按run输出并正确对齐时间。TMR低等级collector缺编排池时拒绝运行中补池升级；首次正确初始化或正常形状重建可创建所需池。

剩余：其他collector尚未统一跨run持留/发布语义；更广运行模式未完成支持；池形状/等级变化仍可能要求排干重建或拒绝；256MiB不是全DFX总预算；日志/错误到外层框架的整体合同独立演进。端到端吞吐和诊断扰动仍需实测，不能从异步写盘推导性能收益。

ArgsDump后台输出已由 [#2455](https://github.com/hw-native-sys/simpler/pull/2455) 合入 `8cf4ed4c0`。参数主机副本交接完成后允许后继串行设备运行，payload与配对清单在后台发布；本地L3显式启用，独立256MiB主机保留额度、最多两个未发布run，超额准入拒绝。途中丢失、D2H失败或写盘失败持续报错，设备完成未知时不额外抢救未交接payload，旧payload文件不自动清理。设备完成事实与诊断错误分别传递：诊断失败仍报错，但不阻止已证明完成设备调用的工作区引用退休。当前提交审查、反馈和CI19成功/1跳过均闭合。[当前提交审查](pr2455-53a271a17-manager-review.md)。

ScopeStats 后台输出已由#2465合入02fd1f63e，最终审查head72c7ae74b。保留设备完成后的接收排干，仅将独立主机快照整理和写盘交给后台；未知完成不产新文件，记录丢失或写盘失败持续报错。每collector独立256MiB预算（含固定记录块及1MiB写入暂存），最多两份未发布run数据，含当前采集run；满额launch前拒绝，任一份发布后可继续。设备计数饱和而不回绕。设备仍串行、诊断独占；首步仅本地L3 TMR。保留buffer的重用只重置计数与身份，不改变指针和队列归属。[已交付合同](temporal-scopestats-background-decision.md)。

TMR DepGen已由#2483合入92a14e4e7；HBG主机图后台输出已由#2490合入01bf24bfa。前者在设备完成与完整性核对后移交记录，后者在主机构图后移交独立快照；HBG满额同步回退，同名文件不覆盖，读文件前flush。当前原作者核查普通L3剩余边界，无新实现授权；外部#2493 kernel采集修复不重复开展。

## 代码依据

- temporal worktree docs/dfx/profiling-framework.md、backpressure-design.md：通用传输与服务职责（legacy文档不是新支持范围证明）。
- src/common/platform/onboard/host/device_runner_base.cpp::start_shared_collectors_for_run / flush_diagnostics：swimlane和PMU分别走retained路径，默认逐run分支保留。
- src/common/platform/include/host/chip_swimlane_collector.h::EpochBucket / RunExport / run_close：身份、所有权与阶段。
- src/common/platform/shared/host/chip_swimlane_collector.cpp::run_close / seal_and_publish_run：有限cut、后台封存与输出。
- src/common/platform/include/host/chip_swimlane_runs.h：默认边界与结论。
- python/simpler/worker.py::flush_diagnostics：公开完成、错误和timeout限制。

## 当前实施合同

ScopeStats已合入；当前仅推进DepGen后台输出边界设计。其他已交付能力保持，完整方案经审查和具体批准后才开始新实现。
