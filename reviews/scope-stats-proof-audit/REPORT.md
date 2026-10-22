# Ordinary ScopeStats producer-stop proof audit

基线：`d9808101e9b4080bcbd3752f913794b41e1b8f39`（#2677最终候选）。2026-10-22只读调查；未构建、未运行测试、未上板、未修改生产、未写远端。结论是静态可达性/合同缺口，不是设备race复现。

## 结论与下一完整范围

**确认存在与#2677相邻、但尚未消费的ordinary ScopeStats stop-proof缺口。** 两个runner都收到`device_execution_complete`和`device_producers_stopped`，但ordinary分支无条件`quiesce → reconcile_counters → write_jsonl`。Onboard真实fence失败路径会以`false/false`到达该分支；epoch/成功读取只证明身份/可读性，不能授权读取仍由producer持有的buffer。

下一完整功能建议是：**普通ScopeStats在未知完成时停止追赶device publication、join host readers，保留并导出本次已host-owned记录，同时明确统计不完整；有完成或独立producer-stop证明时保留正常恢复与统计。** 使用#2677已有`ProfilerBase::stop(false)`；不要再造通用stop协议，不涉及args arena-credit。

不能只给runner加一个`if`：`write_jsonl`目前强依赖device snapshot且重新从共享host镜像提取metadata；直接跳过reconcile会让已收集记录无法导出，伪造snapshot又会把未知total/drop/fatal写成完整零。正确host-only导出与消费者的unknown表达是同一错误close合同的一部分。

## 1. 新读开放PR与去重

安全读取：parent项目`.venv`运行`LOCAL_PATH`，GET `repos/hw-native-sys/simpler/pulls?state=open&per_page=100&page=1`。返回**50项**，未触及100项分页上限，不能沿用旧缓存冒充当前全量。

原始结果：`LOCAL_PATH`。另读#2633、#2655当前files/patch，分别保存`LOCAL_PATH`、`LOCAL_PATH`（15项/51项，均未触及分页上限）。

| 当前候选 | 本次核对 | 与本范围关系 |
| --- | --- | --- |
| #2677 / ChaoWao | OPEN、`d9808101e9b4` | args-dump close与common stop(false)；正文**明确ordinary ScopeStats另行修复**。本功能依赖其common实现，不扩大该PR。 |
| #2633 / doraemonmj | OPEN、`810e268c6c97` | 初始化/线程prewarm；15文件中没有scope_stats collector cpp/header、retained文件、common profiler或两个base cpp的ordinary close改动。它修改架构runner init/ensure调用和base声明，存在附近文件集成关系，但没有实现本stop-proof功能。须保留prewarm后下一run restart的语义。 |
| #2655 / doraemonmj | Draft、`c3f46c5e9d7f` | output_root/路径及Python诊断用例，51文件没有scope_stats collector/common profiler/base cpp；不是本修复。测试可能共用`test_worker_diagnostic_selection.py`，不重复路径设计。 |
| #2625 / qweasdzxcht | OPEN、`71f5b4fc6b8a` | TMR capture/replay及A5 blocker；不把ordinary错误close扩成capture DFX。 |
| #2626/#2646/#2647 / zxx0288 | Draft、`f701832b`/`6cb08c73`/`4c0eacce` | shared固定资源/HBG graph候选，非ordinary ScopeStats owner。 |
| #2493 / nalinaly、#2583 / TaoZQY | 旧integration target | HBG dedicated泳道/prefetch，不能用其旧ABI覆盖本次main collector生命周期。 |

从新读50项标题/正文及相关patch未发现另一个专门关闭ordinary ScopeStats stop-proof的开放PR。此结论是去重检查，非50笔完整diff审查。已有DFX owner仍保留；应按#2670默认接收/既有交接规则确认下一实际owner，不因发现缺口自动接管他们的分支。

## 2. 真实调用链：unknown路径静态可达

### A2/A3 onboard

`src/a2a3/platform/onboard/host/device_runner.cpp:964` `DeviceRunner::reap_run`：

```text
wait_run_fence(prepared) != 0
 → log_run_failure
 → recover_device_or_mark_unusable(rc)
 → teardown_shared_collectors_after_run(dfx, slot, epoch, false) [line991]
 → DeviceRunnerBase::close_scope_stats_run_boundary(..., false, false)
 → quiesce / reconcile / export
```

`recover_device_or_mark_unusable`（`:1077`）在borrowed/kernel模式只poison并返回，不等待/重置caller设备；Program模式做best-effort bounded drain后仍保持unusable。它没有生成“本run producers已停止”的事实。因此不能从这个调用名字或timeout rc推导stop证明。

### A5 onboard

`src/a5/platform/onboard/host/device_runner.cpp:765` `DeviceRunner::drain_execution`：fence失败后recover，再于`:799`把`false`传给teardown；boundary-wait discharge失败也于`:828`传`false`。后者可能保守地丢弃实际已有completion，仍必须按调用方实际提供的proof处理，不能collector自行升级。

共同onboard base `src/common/platform/onboard/host/device_runner_base.cpp:4917`的teardown在所有ScopeStats调用处硬编码`device_producers_stopped=false`；`:4896`的`close_scope_stats_run_boundary`普通分支完全不读两个proof参数。成功路径传complete=true，不能覆盖失败路径资格。

### Sim

两架构sim `DeviceRunner::drain_execution`先经`stop_run_producers(epoch, active_run_->join)` join真实AICPU/AICore host线程，再按first_error调用`settle_run_after_producer_stop(..., complete=false)`，产生**false/true**。A2/A3见`:744`、`:808`；A5见`:750`、`:814`。因此真实sim错误路径是安全恢复的关键正控，不应因complete=false一概丢弃held记录。

`SimDeviceRunnerBase`的epoch proof见`sim/host/device_runner_base.h:844`/`:864`；其close实现位于`sim/host/device_runner_base.cpp:1514`，同样无条件ordinary quiesce/reconcile/write。实际sim正常drain已经join生产者，所以不能把sim错误用例通过当作onboard false/false安全证明。false/false可通过真实base boundary的CPU seam进行回归，不需要在硬件制造挂死。

## 3. 缺口机制：读了什么、还没有证明什么

1. `ScopeStatsCollector::begin_run`（`shared/host/scope_stats_collector.cpp:176`）只清host records/counters/recovery记账，令`device_snapshot_valid_=false`；它从既有CapacityMetadata只计算`expected_producer_epoch_`，**ordinary没有保存这份容量值**。
2. 普通close先`quiesce()`。其合同要求producer停止；producer仍持续发布时，mgmt/collector的“完整空sweep”ack可能不断被推迟。它不是join，也不停止device publication追赶。
3. `reconcile_counters`（`:433`）调用`read_terminal_checked`（`shared/host/scope_stats_retained_runs.cpp:102`）。后者把**整个shared region**复制到host镜像，执行`snapshot_unchecked()`，仅校验初始化epoch。live producer的counters、fatal、held pointer可能变化；bulk刷新也不能与仍活着的host transport写者随意并发。
4. reconcile随后按`state->current_buf_ptr`解析映射，复制`sizeof(ScopeStatsBuffer)`、读`count`并`append_buffer_records`。这个buffer尚未ready handoff，可能仍由producer写入。`recovered_current_buf_/total`去重不能建立producer静止。
5. `append_buffer_records`（`:204`）把record、`run_epoch`、`local_seq`复制进host `RecordBlocks`，并更新received/refused；其`records_mutex_`保护host累积，所以**不应套用args-dump无锁vector/arena-credit的描述**。但mutex不保护device buffer、producer counters或全局quiesce条件。
6. `write_jsonl`（`:651`）要求`device_snapshot_valid_`，持records锁后仍用`snapshot_unchecked()`读当前host共享镜像的total/drop/fatal/capacity，而非专有、不可变export值。ordinary传给render的`extra=nullptr`，没有`counts_unknown`/collection verdict；buffer恢复失败或snapshot未认证则没有artifact。

**ScopeStats没有payload arena，也没有`published_payload_count/completed_payload_count`信用方程。** 这里的风险是unsealed buffer读、未知/变化的统计、追赶live队列及不完整证据被当完整统计；不要添加args transport credit代码。

## 4. #2663 已交付的协议必须保留

Merge `2522f02bf`的设备入口复位是正确且不同的功能：

- 两个TMR `aicpu_executor.cpp:328/330`由leader在handshake/config可能失败前调用`scope_stats_aicpu_begin_run`。
- `shared/aicpu/scope_stats_collector_aicpu.cpp:303`设备侧复位total/drop/published records/buffers；重盖仍持有buffer的count/epoch/local_seq；不清pointer、不动free queue；wmb后发布state.run_epoch。
- host admission不改producer state。OFF调用不碰resident pool；mapped terminal bank不决定谁初始化producer。
- marker证明**本次init执行过**，不证明producer已完成或停止。`state.run_epoch == admitted_epoch`与copy成功仍不足以授权held恢复。
- HBG的`scope_stats_capacity()`是`{required=false, nullopt}`（`common/host_build_graph/runtime.h:573`），没有ScopeStats record producer；#2663专门保留其正常metadata-only输出。未知/required仍严格校验epoch。

下一修复不能重新把reset搬回host，不能清current pointer来“避开race”，不能用host线程join冒充device stop，不能破坏HBG producerless/OFF/未到init不导出前驱的控制。

## 5. Unknown时需要保留的host证据与统计合同

`stop(false)`后还应完成**已交给host队列的有限buffer**和当前in-progress handoff，join collector；从此只消费host所有的数据：

- `RecordBlocks`中本次epoch的样本及其site、phase、ring、资源采样值、`run_epoch/local_seq`；缺少end时保留原begin样本，不虚构scope配对。
- host received/refused/retained数；refused是host已知损失，不能丢弃已有计数或当作device dropped。
- 运行开始/prepare时已拥有且属于本run的CapacityMetadata；需要在begin时按值保存，不能在unknown close读live header补容量，或借用随后可变的Runtime指针。
- 既有host诊断错误应继续保留；不覆盖触发close的计算/设备错误。

Unknown不能认证：device attempted `total`、最终`dropped`、最终fatal是否为false、producer-held buffer剩余记录。**不能用host received冒充device total，也不能把没有观察到loss/fatal写成完整零。** 若没有本次host记录，不应复活前驱artifact；是否写纯host unknown metadata需明确合同，并保留现#2663直接read-failure/unstarted API的拒绝语义。

最小实现可以复用现`scope_stats_runs::Collection`的`CutUnknown/counts_unknown/received/retained`及host `DeviceSnapshot`结构，但ordinary unknown导出必须独立于`device_snapshot_valid_`和`snapshot_unchecked()`的device读取。字段选择可实现时决定；要求是能区分“确知零”与“未知”，并覆盖fatal/capacity的可信度，不能仅添加注释。

普通render当前只有七字段；retained的`extra`已能加`collection_verdict/counts_unknown/host_received_records/host_retained_records`。复用这些含义时要明确ordinary语义，成功普通文件保持兼容。`simpler_setup/tools/tmr/scope_stats_plot.py:368–370`只看fatal/drop，并在二者为0时显示**Healthy**，不看unknown。若发布partial unknown artifact，这个消费者须在同一功能PR正确标注未知，不能展示健康或把缺失容量当有效百分比分母。应新增对应plot回归（例如`tests/ut/py/test_scope_stats_plot.py`；该文件当前不存在）。

## 6. 最小完整修复边界

建议内部`ScopeStatsCollector`统一ordinary close策略，由两个runner普通分支调用：

| proof | ordinary动作 |
| --- | --- |
| complete=true | 保持正常quiesce、checked snapshot/held恢复、稳定host导出；不要无故退化每次线程驻留 |
| complete=false、producers_stopped=true | sim已join的失败运行可恢复held、读取最终普通统计并导出；保留原计算错误 |
| false/false | 调既有`stop(false)`，不quiesce、不terminal/bulk read、不held read；导出本次host-owned记录及明确unknown统计；不追赶stop后的device publication |

同笔包括host-owned metadata/容量快照、JSONL可信度、plot消费、real runner回归及docs合同。普通diagnostics仍best-effort，不能为了输出unknown而凭空改变计算错误优先级。低层`reconcile_counters`/直接`write_jsonl`的前置条件需清楚，不能让外层proof被内部fallback读回破坏。

生产范围主要是collector header/cpp（必要时复用retained文件中的checked-read/helper）、sim/onboard base两个close分支、scope_stats JSONL/plot及其测试/docs。不应扩大到新executor、capture owner、公共API/ABI版本、硬件reset、SDMA、output_root或其他collectors。

依赖#2677 **完整common stop(false)实现**：`:1281`设置mode并请求stop；entry boundary停止继续取新entry，`:2327`unknown跳过final device drain；`start()`重置mode以便后继正常采集。仅依赖其早期`19ed`版本不够（旧stop仍追赶最终device队列）。边界仍不承诺中断已阻塞RTS读或取消已进入`process_entry`的操作。

## 7. Retained与finalize影响

Retained `run_close_locked_path`（`shared/host/scope_stats_retained_runs.cpp:316`）**在complete=false时已先quarantine并返回**：不读shared、不恢复held、不发artifact，后续flush/close报错。stopproof=true只通过既有settlement释放本run terminal bank，不授权读取未发布terminal，也不取消host quarantine。应保持这条更严格合同，不把ordinary host-only partial导出推广到retained。

Complete retained unmapped路径已有quiesce+checked shared read/held recovery；mapped路径只读本run terminal bank与receipt/capacity，不读当前live shared header，使用`live_at_close`记held。下一修复不重写该协议；保留现retained测试及per-epoch bank bookkeeping。本文不据此宣布全部retained并发/capture已验收。

`ScopeStatsCollector::finalize`（collector.cpp`:684`）先finish retained、`stop()`join，再丢quarantine/host records、释放current/free/recycled/shared/shadow。它不导出普通文件，所以不能把unknown host证据推迟到finalize。完成unknown boundary后再次stop无在跑线程，不应重新消费device queue；直接finalize/析构仍是其原调用者的设备所有权合同，不能从join推导可free。

架构runner `finalize_collectors(abandon_device_resources)`传入已有free/unregister policy：abandon时callback不释放device分配。下一修复需保留这一策略，不因host导出成功提前free设备资源；不把本次静态读到的finalize语句扩写成新device-free race主张。

## 8. 可用真实回归seam与验收

**无需新增生产测试开关，也无需硬件故障。** 新建ordinary ScopeStats runner测试可复用`tests/ut/cpp/common/platform/CMakeLists.txt`的`CAPACITY_REFUSAL_SOURCES`和hbg/tmr四目标闭包，照#2677 subclass `SimDeviceRunnerBase`只暴露自有collector初始化/观察与真实`close_scope_stats_run_boundary`。

- 真实producer TU `scope_stats_collector_aicpu.cpp`已在HBG/TMR object库；勿重复链接。调用`set_scope_stats_enabled/base/run_result`、`scope_stats_aicpu_begin_run`、`scope_stats_set_pending_site`、`scope_stats_begin/end`、`scope_stats_aicpu_flush_buffers`产生/发布真实样本。
- complete=true与false/true两个正控：真实held非空record得到恢复；false/false负控：已host-owned样本保留，未发布held样本不导出，host线程已退出，统计明确unknown。HBG normal producerless metadata-only单独保留，四个编译target不等于四种runtime都有实际producer。
- 精确no-read检查：`test_scope_stats_read_failure.cpp`已有自定义`profiling_copy_*`+detached shadows（两架构）可拦截bulk terminal和held地址，不用CPU不可访问内存制造崩溃。其当前目标只有collector，runner目标可调整闭包去掉真实profiling_copy TU以使用同样copy provider，仍需实际runner boundary；不要用toy state machine。
- Late publication：复用#2677 gate首drain线程、现`ProfilerBaseTestPeer` acquire观察mgmt_running=false后再真实flush有限第二buffer、释放gate；unknown不推进late queue head、不导出late样本；默认stop(true)最终drain仍通过。已有`ScopeStatsCollector::total_collected()`在records_mutex下读取，可作为host已复制的确定性predicate；不用sleep排序或无限producer。
- 元数据：相同epoch但无stop、未到init、terminal/held copy失败、读后retry、counter saturation/mismatch、hostrefusal、capacity冻结、unknown不显示Healthy。不同run/producerless/OFF→ON控制继承#2663；begin_run清host-only unknown状态，不能污染下一次。
- Retained：false close不读/不导出、银行settlement按原proof、本run receipts保持独立，现`test_scope_stats_retained_runs.cpp`及`test_run_terminal_stop_settlement.cpp`回归。
- 正常真实sim的failed-compute已先join设备producer，应继续恢复；安全onboard public selection/normal scope scene验证只证明正路径，不应为修复强做设备挂死注入。

本次没有运行上述red loop；它们是源码确认可用的下一测试设计。诊断skill通常要求先red再下根因结论，但本任务明确限定只读和静态reachability，因此只签收静态缺口与修复范围，不声明race重现、发生率或性能损失。

## Parent acceptance

Accepted as a static next-work scope, not tested behavior or shipped functionality. It does not expand #2677. The producer-stop, host-only export and unknown consumer interpretation belong together in a later complete fix, consuming the final common stop(false) implementation.
