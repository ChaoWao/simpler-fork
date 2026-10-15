# §2.4 连续 DFX 采集：五 collector 代码核查矩阵与会话设计

设计归档快照：状态与阅读顺序见[索引](README.md)。设计约束、历史合同和当前实现须分别阅读。

> **历史设计 / 评审快照**：保留文中的原始基线、结论与验证记录，不代表当前待办或支持范围。当前进度统一见 [路线与阶段表](run-pipeline-track-a.md) / [HTML 看板](run-pipeline-track-a.html)。

核查基线：`44dccbef8`（含 #2283）。本文只做**代码核查 + 设计**，没有跑用例、没有改代码。
行号为核查时的位置，作为定位线索，不作为长期引用。

**合入更新：** #2293（`639d0c0b0`）已实现 scope_stats / swimlane 的 buffer 身份与 host 值副本，
并补齐 sim epoch 接线和真实 AICPU 首 buffer 回归；#2313（`14918c37c`）已完成 swimlane 准确计数与未发布 buffer 保留；
\#2321（`5048dfff5`）已完成 dep_gen 身份与保留复用；#2325（`235353966`）args_dump；
\#2328（`2193fb941`）pmu，身份落成 CSV 末列。
**五个 wire 结构现已全部带身份，五个 collector 的未发布 buffer 全部保留复用** ——
下文“没有 run 身份”是 `44dccbef8` 基线的历史核查结论，已不再描述当前代码。

**#2343（`6acd7a867`）已换掉记账推导**：`live_record_count` / `published_record_count`
直接记数，两处 capacity-as-count（flush 尾部、`publish_aicore_pending_buffer`）都已去掉，
无 buffer 的 dispatch 计入 dropped 而非 live。记账等式 `published + live + dropped == total`
是**检查点等式，不是运行期不变量** —— 轮转到 ACK 之间旧 buffer 的计数存在 AICPU 私有 pending 槽里，
两边都不属于；等式只在「flush 且全部发布成功」之后成立。

**第一步的设备探针已跑，结论分三条（`.docs/arms/probe_onboard_identity.sh`，
a2a3 onboard，task-submit 单卡租约，artifacts 见 `.docs/arms/probe_20260918_195838/`）：**

五个通道在真硅上全绿（scope_stats 1、pmu 1、args_dump 1、dep_gen 2、chip_swimlane 5），
设备日志无 `free_queue is empty` / `silent_loss` / `HandleTaskTimeout` / 死锁签名。

1. **盖章时刻读到的 epoch 属于本 run —— 已证。** 同一进程内 scope_stats 的两个 case
   相隔 4 ms 各自建目录，records 分别只带 epoch 1 和 epoch 2，互不混入，每条都有该字段。
   这是 host 单测结构上做不到的那一条（stub 里 `get_platform_run_result_epoch()` 是假的）。
2. **epoch 写入先于 buffer 发布可见 —— 只有否证侧支持。** 若盖章晚于发布，host 收到的
   记录会带 epoch 0 或上一 run 的值；本次全部通道零条这种记录。这是「没观察到反例」，
   不是顺序证明。
3. **AICore 读到的是刷出后的元数据 —— 已跑受控探针，见下节。**

**已知未覆盖（review 记录，不是遗漏）：**

- 失败轮转（free queue 空）后的溢出计入 dropped：要触发得等满 30 秒背压预算。
- args_dump run-end enqueue 失败后的复用分支：同样是 30 秒门，且 `dump_args_flush` 先校验
  自己的 thread index，越界注入进不去。要覆盖就得让四个 collector 在「index 在哪校验」上取齐。

轮转→ACK 交接窗口**已覆盖**（#2352）：`s_aicore_pending_enqueue` 读不到，但它的每一个
效果都落在 host 读的共享内存里，`chip_swimlane_aicpu_on_aicore_ack` 又在公开头文件里，
所以按效果钉——交接中 buffer 两个队列都不在、非 gate ACK 不释放、gate ACK 按轮转时
捕获的计数发布、重复 ACK 不二次发布，并把记账缺口量成「恰好等于被暂存 buffer 的计数」。

## AICore 轮转可见性探针（instrumented 构建，`45a13f6aa`）

**先说最重要的一条：已检查的现有 ST 没有提供默认容量下的设备轮转覆盖。**
`PLATFORM_AICORE_BUFFER_SIZE = 1024`，而这些 swimlane st 用例每核只产生 1–4 条记录
（实测 `max_per_core` 最大 4），远达不到轮转边界。所以 #2313 / #2343 / #2352 改的轮转路径，
在**这些用例**里没有设备侧覆盖，其覆盖来自 cpput。
这不等于「设备上从未发生过轮转」—— 小负载用例证不了别的真实负载没走过这条路径。

探针构建把容量降到 16，并在 `copy_aicore_buffer` 里打印每个 buffer 的
`run_epoch` / `local_seq`，用 589 任务链（全部落在一个核）把轮转压出来。
容量只决定轮转多频繁，不改机制本身；但**被测配置不是出厂配置**，这点不能含糊。

构建隔离：`rm -rf build/cache/a2a3/onboard` 后重建，六个产物 md5 全变，
含两个 `aicore_kernel.o`（ccec），确认改动确实进了 AICore 工具链；
跑完切回 `upstream/main` 重建，md5 与探针构建不同，已复原。
artifacts：`.docs/arms/vis2_20260918_202741/`。

四个 run、60 个 buffer、922 条记录，逐 run 对账（链长 `n` → `n+1` 任务 = 期望条数）：

| epoch | buffers | 满 16 | 尾部 | 记录 | local_seq | 期望 |
| ----- | ------- | ----- | ---- | ---- | --------- | ---- |
| 1 | 5 | 4 | 1 | 65 | 0..4 无缺口 | 65 ✓ |
| 2 | 5 | 4 | 2 | 66 | 0..4 无缺口 | 66 ✓ |
| 3 | 13 | 12 | 9 | 201 | 0..12 无缺口 | 201 ✓ |
| 4 | 37 | 36 | 14 | 590 | 0..36 无缺口 | 590 ✓ |

合计 922 / 期望 922，**silent loss = 0**；`dup_reg_task_ids` 全为 none、
`zero_timestamp_rows` 全为 0、`start_time=0` 跳过告警一次未出现、
`free_queue empty at rotation` 一次未出现。

**可见性现在是正面证据，不只是「没见到反例」。** 单核连续 36 次轮转，
每个满 buffer 恰好 16 条、尾部恰好是余数、序号无缺口、总数闭合到派发数。
若某次轮转后的 `dcci(head)` 把**新 seq 配上旧 ptr**，该核会继续写进 AICPU 已暂存并发布的
buffer：host 收完之后被覆写，表现为该 buffer 计数 16 但内容被改、后继 buffer 短缺、
或 reg_task_id 重复/丢失。四个 run 一次都没出现。

顺带修正一处框架：AICore 从 head 行只读 `current_buf_seq` 和 `current_buf_ptr`，
**不读 `count`，也不读 `run_epoch`**。所以「AICore 读到刷出后的元数据」这条，
被测对象就是 seq/ptr 这一对的配对，不是 count/epoch。

**身份在 AICore 池上也直接观测到了**：epoch 1–4 各自成段，60 个 buffer 零个 epoch 0，
且其中 56 个是**轮转边界上中途发布**的（不是 flush 尾部）—— 这把「盖章先于发布可见」
的否证面从「只有尾部」扩到了中途发布。

**边界**：以上仍是「没有观察到反例」，不是顺序证明；容量 16 ≠ 出厂 1024；a2a3 TMR 单卡单次，
不是受控 A/B，也不能外推到其他架构或出厂容量。探针本身按「已完成」记账，
结论范围限于容量 16 / a2a3 TMR。默认容量下 >1024 条/核 的轮转覆盖可另补。

**两次把日志缺失当成结论的错误，记下来：** 第一轮 0 条 PROBE 行是 pytest 在用例通过时
丢弃 captured stderr；加 `-s` 后仍 0 条，是 host 日志默认阈值 `timing`(25) 压掉了
`INFO`(20)。两次「缺失」都不是发现。脚本现在固定先打印导出器自己那条无条件 INFO
作为**日志通道的阳性对照**，通道不通就不允许对 PROBE 行的有无下任何结论。

## swimlane 导出身份链路（#2355，已提交待评审）

原缺口有两层，不只是少一列：

1. **导出器**保存了带身份的采集记录，写 AICore / scheduler task / orchestrator phase
   三个流时把身份丢掉。
2. **解析器**按 `(core_id, reg_task_id)` 关联。光在 JSON 里加 epoch 不够 ——
   `reg_task_id` 每 run 从 0 重来，图重跑时 task id 也重复，两个 run 在同一文件里
   必然撞键。

现在两层都带身份：`aicore_tasks` 7 列（末列 run_epoch）、`scheduler_tasks` 与
`scheduler_records` schema_version 2、`aicpu_orchestrator_phases` 加键；
关联键改为 `(run_epoch, core_id, reg_task_id)`；图执行实例按
`(run_epoch, outer_task_id)` 分组（否则重跑的图会并成一个跨两 run 的实例）。
deps.json 关联**不**按 epoch —— 它是与 run 无关的静态图，按 task_id 关联，
run 边界属于执行实例分组那一层。

**HBG extension 已核查**：只有 a5 host_build_graph 自己产这些流（从 host 侧
`SchedulerTaskTrace` 构造，没有逐记录 epoch），改为从新增的 `HostApi::run_epoch()`
盖章；a2a3 HBG 不发布 extension，走平台 collector，自动继承。
`host_orchestrator_phases` / `host_device_uploads` 是 host 侧捕获、无设备 epoch，
本次未动，已在 PR 里点名。

**兼容**按列数与声明版本判定，不按值：旧文件解析为 `run_epoch = None`，
表示「该文件没有记录身份」，**刻意不取 0** —— 0 是设备真能拿到的 epoch，
默认成 0 会让旧文件冒充 run 0 并与真 run 相撞。

**验收**走真实导出器：两个 run 同核、复用同一批 reg_task_id、时间不同，
经真实 `export_swimlane_json()` 导出、真实 converter 解析后仍各归其主。
反控保留：同一 epoch 内重复身份仍报错；其补集（不同 run 的相同 core+reg）合法且产出两条。

st 验证器现在逐次采集断言身份（7 列、每次采集恰好一个 epoch、不为 0、两个流 epoch 一致）。
**sim st 抓到了 pyut 抓不到的两处**：converter 对 phase record 的精确键集校验，
以及验证器自身写死的 `schema_version == 1`。

逐 run 清空未删除，会话生命周期与合并输出未完成，不能据此解除诊断独占。
本轮的单测与探针都不解除逐 run 屏障。

**每个 collector 各自的边界（不要当成统一结论）：**

- dep_gen：host 按 epoch 保存 records 值副本；计数核对成功的空窗口仍输出空 deps.json，
  多个有记录的 epoch 不混成一张图。空窗口的 epoch 0 表示没有从记录获得身份；
  未来会话需另行登记空 run 与输出归属，不能把有记录的 map 当完整 run 名单。
- args_dump：payload 不需要独立身份 —— `arena_write_offset` 跨 run 单调且 host 从不重置，
  所以 `payload_offset` 本身就唯一。**run-end enqueue 失败后的复用分支仍无直接测试**：
  `dump_args_flush` 先校验自己的 thread index，越界注入进不去，另一条路是 30 秒背压自旋。
  要覆盖就得让四个 collector 在「index 在哪校验」上取齐，那是生产改动，待维护者定。
- pmu：记录内容在 host 单测里是 fixture —— a2a3 走 MMIO、a5 由 AICore 写 staging ring，
  两边都不是 host 可调函数。身份是 CSV 末列（`event_type` 之后），六列前缀契约未动。

## 0. 初始基线结论（历史，当前状态见上方合入更新）

**常驻层已经做完，归属层还不存在。** 线程常驻（`ProfilerBase::start()` 有幂等守卫）、
buffer 常驻（collector 自己用 `mem_alloc_` 分配，跨 run 存活）、尾 buffer 五个都有设备侧
run-end flush。缺的不是这些，而是：

> **今天五个 collector 的 wire 结构里没有任何 run 身份字段。**
> `git grep -lE "run_id|run_epoch|invocation_id" src/common/platform/include/common/` 只命中
> `device_run_result.h`（#2283 刚加的）和 `host_api.h`，**没有命中**
> `chip_swimlane_profiling.h` / `args_dump.h` / `scope_stats.h` / `dep_gen.h`。

所以**「`begin_run()` 清空」本身就是归属机制**：因为记录里没写自己属于谁，host 只能靠
「我在 run 开始时把一切清空了，所以现在读到的都属于本 run」来推断。
`device_runner_base.cpp:2642-2646` 的注释把这个依赖写明了 ——
*"begin_run() drops the previous run's records and republishes the device level, which is only
safe while this run holds the execution claim"*。

**因此实施顺序不能调换**：先加身份，才能删清空。反过来做不会报错，只会静默错配。

## 1. 五 collector 矩阵

六个轴：host/device 重置点 · run 身份 · 尾 buffer · 计数对账 · 输出与工具键 · finalize 后仍借用的资源。

## 1.1 host 侧每 run 重置点

| collector | `begin_run()` 清什么 | 是否写回设备 |
| --------- | -------------------- | ------------ |
| chip_swimlane | `json_extensions_`、`reset_collector_shards()`（4 组 per-core/per-thread record vector + per-shard 镜像 + `collector_counters_` + 三个 `total_*_collected_` + `has_phase_data_` + `collector_shards_merged_`），再 `publish_run_config()` | **是，而且是四个里最宽的**：`publish_run_config()` 除了重发 level，还把**每个池**的 `total_record_count` / `dropped_record_count` 写回清零，遍历 `PLATFORM_MAX_CORES` 而非本 run 的核数（`chip_swimlane_collector.cpp:877-892`） |
| args_dump | `reset_collector_shards()`、`total_dropped_record_count_`、`total_truncated_count_`、`last_progress_ms_`、`written_payload_counts_[]` | **是**：`dump_args_level` 一个窄字段 + **每 thread 的 payload 计数三连**写回 |
| scope_stats | `records_`、`total_collected_`、`recovered_current_buf_`、`recovered_current_total_`、`execution_complete_=false` | **是**：`total_record_count` / `dropped_record_count` 写回清零 |
| dep_gen | `records_`、`total_collected_` | **是**：`total_record_count` / `dropped_record_count` / `total_overflow_record_count` 三个窄写回 |
| pmu | run-scoped CSV shard（在 `start()` 里重置）、csv_path、event_type | 事件配置经 MMIO 编程，非共享内存写回 |

位置：`device_runner_base.cpp:2640-2665`（统一调用点）；
`chip_swimlane_collector.h:419-425` / `chip_swimlane_collector.cpp:506-530`；
`args_dump_collector.cpp:65-95`；`scope_stats_collector.cpp:157-182`；
`dep_gen_collector.cpp:159-181`；`pmu_collector.h:237`。

**四个 collector 每 run 往设备写回清零**（第一版写成"三个"，漏了 swimlane —— 它的
`publish_run_config()` 被当成只重发 level，实际也清每个池的两个计数）。这是比 host 侧
`clear()` 更硬的阻塞：连续采集下后继的 `begin_run` 会在前驱记录还没被消费完时把设备计数清掉。
scope_stats / dep_gen 的注释自己说明了为什么现在是对的 —— *"the caller has quiesced the
device, so the AICPU is not writing them concurrently"*（`dep_gen_collector.cpp:171-172`）。
**这句话在放开准入后不再成立。**

**这条同时解释了 §1.4 那条公式为什么今天是对的**：`total_record_count` 和
`current_buf_seq` 每 run 一起归零，所以 `total - seq × BUFFER_SIZE` 在 run 内自洽。
**取消清零和替换记账必须同批落**，否则第二个 run 的 `live` 立刻算错。

## 1.2 device 侧每 run 重置点

| collector | 设备侧重置 | 触发时机 |
| --------- | ---------- | -------- |
| chip_swimlane | `s_aicore_dispatched_count[] = 0`、`s_aicore_pending_enqueue[] = {}`；**从 free_queue 重新 pop 当前 buffer**，`current_buf_seq = 0`，`buf->count = 0`；AICore head channel 同样重新 prime，seq 归 0 | `chip_swimlane_aicpu_init()`，在 `pre_handshake_init` 里**每 run** 调用：`scheduler_cold_path.cpp:1134`(a2a3 tmr) / `:841`(a2a3 hbg) / a5 `:1106` / `:814` |
| args_dump | mask 表与 scalar dtype 表按 run 重建 | `args_dump_aicpu.cpp:89,108-127` |
| scope_stats | collector-local 静态量：`scope_stats_depth=-1`、pending site、`s_scope_site_*`、`s_scope_ring`、`s_heap_wraps` | `set_platform_scope_stats_base()`，`scope_stats_collector_aicpu.cpp:222-242` |
| dep_gen | `dep_gen_aicpu_init()` | `aicpu_executor.cpp:740`，每 run |
| pmu | `g_pmu_saved_ctrl0[]` 由 init 填、由 finalize 消费（**MMIO 寄存器存档/还原对**） | `pmu_collector_aicpu.cpp:200`(a5) / a2a3 同名 |

**最关键一条：`current_buf_seq` 每 run 归 0**（`chip_swimlane_collector_aicpu.cpp:355-356`，
scope_stats 同理）。所以 **seq 不是跨 run 单调的**，不能直接当排序键。
host 的三桶对账里那个 `total_record_count - current_buf_seq * BUFFER_SIZE`
（`chip_swimlane_collector_aicpu.cpp:735-745`）**按构造就是 per-run 的** —— 它算的是
「当前 buffer 里的活跃记录数」，前提是 seq 之前的每一次轮转都写满了 BUFFER_SIZE。
run 内成立（只有写满才轮转），**跨 run 不成立**：详见 §1.5。

## 1.3 run 身份

| 通道 | 今天有什么 | 能不能直接复用 #2283 的 epoch |
| ---- | ---------- | ----------------------------- |
| 五个 DFX wire 结构 | **没有任何 run 身份字段** | 不能直接复用——epoch 目前只到 `KernelArgs.run_result_epoch`，各 producer 拿不到 |
| `KernelArgs` | `run_result_epoch`（#2283）、`run_result_data_base` | **可以作为来源**：已是 per-run、进程内唯一、已过两架构 kernel 入口 |
| AICPU SO 全局 | `get_platform_run_result_epoch()`（#2283，relaxed 原子） | **可以直接读**：五个 collector 的设备侧都在同一个 AICPU SO 内 |
| host phase records | `invocation_id`，`HostPhaseRecordStore::finish(submitted_tasks, invocation_id)` | **这是要照抄的先例**，见 §2 |

`run_result_epoch` 的可达性**逐 producer 核过，结论是不能统一假设**：

| producer | 能否读 `get_platform_run_result_epoch()` | 依据 |
| -------- | ---------------------------------------- | ---- |
| 五个 collector 的**记录时刻**（AICPU 侧） | **能** | 记录发生在 `aicpu_execute` 内，晚于 kernel 入口 `:129` |
| 五个 collector 的 **init/base-set 时刻** | **不能** | `kernel.cpp:76-87` 发布五个 base，`:129` 才发布 epoch —— **早 53 行**。此刻读到的是上一个 run 的 epoch，首个 run 读到 0 |
| **swimlane 的 AICore producer** | **不能** | `chip_swimlane_aicore_record_task` 是 `__aicore__` 内联，只 include `common/chip_swimlane_profiling.h` + aicore 头，**不链接 AICPU SO**。AICore 是独立程序，取不到 AICPU 全局 |
| HBG 的 host 构图导出 | 不适用 | host 侧编排，不走 AICPU kernel 入口 |

两个后果：

1. **不能在这些 base-setter 里新增 epoch 读取** —— 此刻读到的是上一个 run 的 epoch。
   **但也不需要读**：它们只发布 base 指针、重置本地静态量，都不需要身份。
   **所有盖章点都在 `aicpu_execute`（`kernel.cpp:132`）之内**，因此全部晚于
   epoch 发布（`:129`）—— swimlane 的 AICore prime 在
   `chip_swimlane_aicpu_init` → `pre_handshake_init`（`aicpu_executor.cpp:287`）里，
   scope_stats 的 buffer 是首次 append 时懒领，dep_gen / pmu 的 init 同样在 executor 内。
   **所以调用顺序本来就是对的，不需要把 `set_platform_run_result` 前移。**
   （第一版提出前移，已撤回 —— 见 §5.1 第 7 条。）
   要固定的是一条**不变量**，不是一次移动：
   > base-setter 阶段 epoch 尚未发布；身份只能在 `aicpu_execute` 内的盖章点读取。
2. **AICore 的身份必须由 AICPU 代为固化**：在把 buffer 发布给 AICore 之前，把 epoch 写进
   **buffer 的不可变元数据**（不是 head channel 的可变字段），并写明可见性顺序。
   候选写入点是 `chip_swimlane_aicpu_init` 给 AICore prime head 的那一段
   （`chip_swimlane_collector_aicpu.cpp:375-395`），以及 `aicore_rotate`（`:466`）发布新 buffer 时。

**逐 producer 要核的链条**（实施第一步的核对清单，不是已完成的结论）：
`epoch 来源 → 写入不可变 buffer 元数据 → ready 发布 → host 复制 → buffer 归还`。

## 1.4 尾 buffer

**五个都已经有设备侧 run-end flush** ——这一轴基本不欠工作：

| collector | flush 入口 | 调用点 |
| --------- | ---------- | ------ |
| chip_swimlane | `chip_swimlane_aicpu_flush()` / `_flush_sched_phase_buffer` / `_flush_orch_phase_buffer` | `scheduler_dispatch.cpp:1531,1535`；`scheduler_cold_path.cpp:1415` |
| scope_stats | `scope_stats_aicpu_flush_buffers()` | `aicpu_executor.cpp:770`(a2a3) / `:775`(a5) |
| pmu | `pmu_aicpu_flush_buffers(thread_idx, cores, n)` | 两架构 `pmu_collector_aicpu.cpp` |
| args_dump | `dump_args_flush(thread_idx)` | `args_dump_aicpu.h:440` |
| dep_gen | `dep_gen_aicpu_flush()` | `dep_gen_collector_aicpu.h:127` |

**但 flush 之后 `current_buf_ptr = 0`**（`scope_stats_collector_aicpu.cpp:268`），
下一个 run 必须重新领取。**这不是要消除的行为** —— 已交给消费者的尾 buffer 必须等归还，
不能让后继继续写。所以正确的形状是：

> **池常驻、buffer 仍每 run 领取。** 「服务不重建」不等于「不从同一池领新 buffer」。
> 要去掉的是池的**重建**和计数的**清零**，不是领取动作本身。

**身份不必依赖跨 run 单调 seq。** `run_epoch + producer + local_seq` 已经能唯一标识一个
buffer，local_seq 允许每 run 从 0 开始。这比改 seq 语义安全，因为 **AICore 把 seq 当轮转代际用**
（`chip_swimlane_collector_aicore.h:43,57-58`：`cached_buf_seq` 初值 `UINT32_MAX`，
靠 `head->current_buf_seq != cached_buf_seq` 触发重新加载 buffer）。动 seq 语义会连带动这个协议。

**三件事必须分账，不能互相推导：**

| 量 | 归谁 | 今天 |
| -- | ---- | ---- |
| 当前 buffer 的有效记录数 | producer 写在 buffer 内 | AICPU 侧有 `buf->count`；AICore 侧靠公式推导 |
| 累计已发布记录数 | 按 enqueue 时的实际 count 累加 | **不存在**，靠 `total - seq × capacity` 反推 |
| buffer 代际 | `current_buf_seq` | 兼作上面两者的推导输入 —— 这就是耦合点 |
| drop | `dropped_record_count` | 已独立 |

替换口径：`总产出 == 累计已发布 + 当前 buffer 有效数 + drop`，**式子里不出现 capacity**。
`chip_swimlane_collector_aicpu.cpp:735-745` 的 `live = total - seq × BUFFER_SIZE` 要改成读
累计已发布量。AICore 路径还有个更省的选项：host 的 `copy_aicore_buffer` 本来就跳过
`start_time == 0` 的尾槽（`chip_swimlane_collector.cpp:691` 的 WARN 就是它），
所以 AICPU 可以一律按 capacity 标记、让 host 扫 —— 代价是多一趟扫描，
按该处注释「over-stating count costs only a scan pass — never spurious records」。

## 1.5 计数对账

| collector | 对账口径 | 连续采集下会怎么坏 |
| --------- | -------- | ------------------ |
| chip_swimlane | 设备三桶 vs host `total_*_collected_`；`reconcile_counters()` | 两边都 per-run 清零，只要有一侧不清就失配 |
| args_dump | per-thread payload 三连（published/completed/dropped）vs `written_payload_counts_` | 同上；**但 `arena_write_offset` 故意不清零** |
| scope_stats | `total_record_count` / `dropped_record_count`，加 `recovered_current_buf_` + `recovered_current_total_` 的去重键 | 去重**状态本身**也在 `begin_run()` 里清零（`scope_stats_collector.cpp:163-164`），所以今天不会跨 run 误去重。**取消逐 run 重置后这个键就失去唯一性**（设备 total 每 run 归零，`(buf_ptr, total)` 可重复），必须换成带身份的键 |
| dep_gen | `total_record_count` / `dropped` / `total_overflow`；`reconcile_counters()` 失败则**整张图不落盘**（`emit_device_dep_gen_graph` 的完整性门） | 连续采集下"一个 run 的完整性"要重新定义 |
| pmu | CSV shard 合并 + flush 失败计入 dropped（`pmu_collector.cpp:495,506`(a2a3) / `:528,545`(a5)） | flush 是唯一数据路径，失败必须记账 |

**已经连续的一个先例，值得照抄口径**：args_dump 的 `arena_write_offset` 是
*"a monotonic cursor the host reads modulo arena_size, so it stays correct across runs"*
（`args_dump_collector.cpp:90-91`）。**但不能据此推断其他计数与队列已支持连续运行** ——
同一个 `begin_run()` 里紧挨着就在清 per-thread payload 计数。

## 1.6 输出与分析工具键 / finalize 后仍借用的资源

| collector | 输出 | 键 | finalize 后仍借用 |
| --------- | ---- | -- | ----------------- |
| chip_swimlane | `export_swimlane_json()` + `publish_host_phase_records_to_swimlane()` | core_id / thread_idx / task_id | shm 与 rotation table 都是 collector 自己经 `mem_alloc_` 分配的**runner 寿命**资源（`device_runner.cpp:1335-1345`），不随 run 退休 |
| args_dump | `export_dump_files()` | task_id + arena offset | dump arena 同上 |
| scope_stats | `write_jsonl(output_prefix)` | scope site (file:line) | shm 同上 |
| dep_gen | `emit_device_dep_gen_graph()` → `deps.json`，**reconcile 不通过就不写** | task_id / edge | shm 同上 |
| pmu | CSV（`make_pmu_csv_path(output_prefix)`） | core_id + counter | **MMIO CTRL0 存档**：init 存、finalize 还原，是五个里唯一的设备寄存器状态 |

**好消息**：buffer / shm 全是 collector 自有、runner 寿命，**不是 run 拥有的资源**，
所以「finalize 后仍借用 run 资源」这一栏在设备内存上基本是空的。
真正跨 run 的资源耦合只有两处：**PMU 的 MMIO 存档/还原对**，和
**输出路径来自 `dfx.output_prefix`**（每 run 一个前缀，见 §2 的冲突）。

`teardown_shared_collectors_after_run`（`device_runner_base.cpp:2685-2719`）每 run 调
`quiesce()` + `reconcile` + `export`，**但不调 `stop()`** —— 这印证了常驻：
`stop()` 只在 finalize 走。所以要拆掉的 per-run 屏障就是这里的 `quiesce()`。

## 2. 会话状态设计

## 2.1 可参考的先例：`HostPhaseRecordStore`

`host_phase_records.h:129-146,171` 是一个**追加式输出**的在跑先例，可以借它的口径：

- 按 pipeline slot 一份，`arm()` / `finish()` 每 pass 一次；
- 每 pass 往**调用方传入的**路径追加一行 JSON Lines；
- 记录里带 `invocation_id`，*"so an artifact can be joined to that run's span tree"*；
- `write_records_jsonl` 对同一 pass 的第二次调用是 no-op，所以两条收尾路径都能无条件写。

**但它不是可以直接套用的方案，两处必须先解决：**

1. **路径由调用方给，不保证跨 run 相同。** 今天每 run 一个 `output_prefix`，
   会话输出目标与 per-run 前缀的关系**尚未定义** —— 这是会话协议要先定的事，
   不是格式问题。
2. **`invocation_id` 不等于 native `run_epoch`**，两者之间要有明确映射才能当身份用。

**五种格式不统一成 JSONL。** swimlane JSON、PMU CSV、args dump 的二进制 + manifest、
DepGen 图各有消费者和工具。正确顺序是**先定会话容器/manifest 与身份键，再逐个决定哪种格式要改**。
另外必须定**持续落盘/分块与回收**：只在会话关闭才导出会让 host 侧 records 无界累计。

## 2.2 两个"完成"必须分开定义 —— 但今天它们**不是**混用的

| 完成 | 谁保证 | 触发 |
| ---- | ------ | ---- |
| **run 结果完成** | 本 run 的双流 fence（#2272 / **#2267 未完成**）；#2283 只保存失败诊断，**不是成功终态** | `finalize_native_run` |
| **采集输出完成** | 会话关闭：停止准入 → 等全部 producer 停 → `quiesce()` → 对账 → 落盘 | worker close / 配置切换 |

**今天这两者实际是同步在一起完成的，不存在混用问题。**
`reap_run` 在返回前就调 `teardown_shared_collectors_after_run`
（`device_runner.cpp:726,735`），里面顺序执行 `quiesce()` → `reconcile` → `export`，
而 `ProfilerBase::quiesce()` 明确等设备队列和 host shard 都消费完
（`profiler_base.h:932-945`），导出函数在返回前关文件。
所以**当前 run 返回即保证该 run 的采集输出已完整**。

这一栏是**引入会话之后**才改变的合同，不是对现状的描述。
推论：不能用"两个完成被混用"解释任何**现存**失败，也不该把现有断言延后到 close
去掩盖未解释的失败。

## 2.3 会话状态机

```text
IDLE ──begin_session(config)──> COLLECTING ──同配置后继──> COLLECTING
                                     │
                          配置变更 / close
                                     ↓
                             DRAINING（停止准入，等 producer 停）
                                     ↓
                        quiesce → reconcile → 合并落盘 → IDLE
```

| 边界 | 合同 |
| ---- | ---- |
| 同配置后继 | **不清记录、不重置共享队列、不写回设备计数**；沿用当前会话 |
| level / PMU event / buffer shape / 输出目标变更 | 先停旧会话准入并排空其引用，**再**发布新配置或重建池；后继 prepare 不得改掉前驱配置 |
| finalize N | 只按 run fence 收执行；已移交 DFX 自有 buffer 的数据继续异步输出。仍借用 run 资源的（PMU MMIO 存档）必须等确认后才退休 |
| 失败 / partial launch | 保留不完整标记，沿用 runtime 的完成/静止证明；**不能无限等一个可能永不产生的 run-end 标记** |

## 3. 分步实施

## 第一步：身份与 buffer 协议

**当前状态：** 身份实现与未发布 buffer 保留已由 #2293 / #2313 / #2321 / #2325 / #2328 分批交付。
设备身份可见性探针与第 4 项累计分账尚未完成；下列序号保留原设计顺序，不代表全部待实现。

1. **固定一条不变量，不移动调用**：base-setter 阶段（`kernel.cpp:76-87`）epoch 尚未发布，
   身份只能在 `aicpu_execute`（`:132`）内的盖章点读取。所有盖章点本来就在其内，
   所以**不需要前移 `set_platform_run_result`** —— 前移会把它挪到 affinity gate
   （`:100`，`return 0` 在 `:103`）之前，让被 gate 过滤掉的线程也执行写入，**不是零成本**，
   而且换不到任何东西。
2. 逐 producer 核对身份链条，**先用探针确认再改结构**：
   `epoch 来源 → 写入不可变 buffer 元数据 → ready 发布 → host 复制 → buffer 归还`。
   AICPU 侧盖章时刻可读 getter；**AICore 侧不行**，必须由 AICPU 在发布 buffer 前固化
   （§1.3）。HBG 的 host 构图导出单独给身份。
3. 五个 wire 结构各加身份字段（`run_epoch` + producer + `local_seq`），**布局断言同步加**。
   `local_seq` 允许每 run 从 0 开始；**不改 `current_buf_seq` 的语义** ——
   AICore 把它当轮转代际用（§1.4）。**身份必须随记录复制进 host 副本**，
   不能靠"当前 buffer 的 epoch"推断，否则 buffer 归还复用后旧副本身份会被改写。
4. 记账改成分账三元组：累计已发布、当前 buffer 有效数、drop；
   删掉 `total - seq × capacity` 这条推导（§1.4）。
5. **两架构同步改，并与 lexical 衔接** —— 身份字段和 #2283 的结果区共用 epoch 来源，
   lexical 的终态协议也在扩同一个基础，不能各加一套。
6. **以上做完才允许删 `begin_run()` 的清空与设备写回清零。**

验收见[验证方案](README.md#archive-boundary)：连续两 run 身份不串、
已提交尾 buffer 不被覆盖、归还后复用时旧 host 副本身份不变、计数为 1/1/2。

## 第二步：会话生命周期

1. `begin_session` / `end_session` 取代 per-run `begin_run` 的清空语义；`begin_run` 退化为
   「登记本 run 身份 + 必要时发布配置」。
2. 配置变更走 DRAINING：**先排空再重建**。这里核查的是
   `chip_swimlane_aicpu_init` 是否**重建池**、是否**清计数** —— 同配置后继两者都不该做，
   但**仍然要从池里领一个可用 buffer**（§1.4）。
3. scope_stats 的 `(buf_ptr, total_record_count)` 去重键换成带身份的键。
   **这不是修现存 bug** —— 今天 `begin_run()` 把去重状态一起清零，所以不会跨 run 误去重；
   是取消逐 run 重置**之后**这个键才失去唯一性。

验收：消费者滞后时 N+1 仍可执行且 N 不被覆盖；配置切换正确排空；未满 buffer 与空 run。

## 第三步：合并输出与工具适配

1. **先定会话容器 / manifest 与身份键**，以及会话输出目标与既有 per-run `output_prefix`
   的关系（§2.1）。**然后**逐个决定哪种格式要改 —— 五种格式不统一成 JSONL，
   swimlane JSON / PMU CSV / args dump 二进制+manifest / DepGen 图各有消费者。
2. 定持续落盘 / 分块与回收：只在会话关闭导出会让 host 侧 records 无界累计。
3. 分析工具与 `strace_timing` 的键随之更新；dep_gen 的「整张图或不落盘」完整性门
    重新定义为「每 run 完整或标记不完整」。
4. 会话语义落地后，再调整那些「run 返回即断言输出完整」的测试。
    **注意这是合同变更，不是为了让现有失败变绿** —— 今天 run 返回确实已保证该 run
    输出完整（§2.2）。

验收：长时间运行的有界内存与队列背压；partial/失败后有限收敛；会话关闭后各 run 计数与输出完整。

## 4. 与 P4 的关系

**普通路径 P4 不等本项全部交付。** 若届时仍有诊断路径未满足连续运行合同，由 P4 在 launch
处接入双向独占（普通→诊断、诊断→普通、诊断→诊断），继续允许合法 prepare 重叠，不回退 #2201。
当前单 launched 已强制独占，**不提前加冗余门**。这条已记在 `run-pipeline-track-a.md` §5 开头。

「缩小 collector 重建集合」是已否决的优化，不属于本项
（[调查记录](https://github.com/hw-native-sys/simpler/blob/11eb5d2c9ef8fe5a484a3d7c686d408ae5ff3e3b/docs/investigations/2026-09-collector-rebuild-narrowing.md)）。

## 5. 本次核查的边界

## 5.1 第一版被复核纠正的七处

由 [矩阵复核](README.md#archive-boundary) 两轮提出，均已逐条对代码核实后改正。
留在这里是因为其中四处会诱导实施顺序出错。

| # | 第一版写错了什么 | 实际 |
| - | ---------------- | ---- |
| 1 | 断言 scope_stats 的去重键"今天已跨 run 碰撞" | `begin_run()` 把去重状态一起清零（`scope_stats_collector.cpp:163-164`）—— 这是**我自己在 §1.1 记录过的事实**，却在 §1.5 得出相反结论。今天不碰撞；取消逐 run 重置后才失去唯一性 |
| 2 | 假设 residency 失败可能是"两个完成被混用" | `reap_run` 返回前已同步完成 quiesce→reconcile→export→关文件；residency 也是三个 run 全返回后才检查。**解释不了任何现存失败** |
| 3 | 同时主张"每 run 换 buffer"和"同配置后继不再 pop" | 自相矛盾。池常驻 ≠ 不领 buffer；已交给消费者的尾 buffer 必须等归还 |
| 4 | 主张跨 run 单调 seq 并沿用 `total − seq × capacity` | 未满尾 buffer 直接破坏该式（容量 B、两 run 各 1 条：累计 2、seq=1，`2−B` 已错）。且 AICore 把 seq 当轮转代际 |
| 5 | 声称"五个 collector init 都晚于 epoch 发布，epoch 对它们全部可见" | **不符合调用顺序**：`kernel.cpp:76-87` 发布五个 base，`:129` 才发布 epoch。且 AICore producer 是独立程序，取不到 AICPU 全局 |
| 6 | 把 JSONL 当统一替换方案；把 #2283 当 run 结果完成的保证 | JSONL 只是追加输出的参考；#2283 只保存失败诊断，成功终态等 #2267 |
| 7 | 提出把 `set_platform_run_result` 前移到五个 base 之前，并称"代价为零" | **已撤回。** 所有盖章点本来就在 `aicpu_execute`（`:132`）内，全部晚于 epoch 发布（`:129`），所以前移换不到任何东西；而它会把调用挪到 affinity gate（`:100`）之前，让被过滤掉的线程也执行写入 —— 不是零成本。正确做法是固定不变量，不是移动调用 |

## 5.2 方法边界

- 只读代码，**没跑用例、没跑 sweep、没做设备探针**。行号是核查时的位置。
- 「五个 wire 结构没有 run 身份」已带正对照验证：同一条
  `grep -lE "run_id|run_epoch|invocation_id"` 在 `src/common/platform/include/common/`
  下命中 `device_run_result.h` 和 `host_api.h` 两个文件，而同一条 `grep -c` 在四个 wire
  头文件上全部零命中。所以零结果不是 pattern 写错。
- epoch 可达性是**按 `kernel.cpp` 的调用顺序逐行核对**的，不再是推断：base-setter 在
  `:76-87`、epoch 在 `:129`、`aicpu_execute` 在 `:132`，而所有盖章点都在 executor 内。
  但「盖章时刻确实读到本 run epoch」仍未在设备上打印验证，
  归[验证方案](README.md#archive-boundary) §5 的探针。
- **host 单测证明不了设备缓存可见性**，所以可见性顺序类断言不在 cpput 覆盖范围内。
- a5 按同名文件核对，**没有在 a5 硅上验证任何一条**。
- 未核查：sim 平台的 collector 路径是否与 onboard 同形（sim 有自己的
  `device_runner_base.cpp:871-876` 调用点）；`--rounds` 路径。
- **residency 与本项脱钩**：它此前被记为「baseline 与改动树同样 6/8 失败」，
  该数字**已撤回** —— 那六轮是 `/tmp` 日志重定向被拒、pytest 未启动，脚本把未运行
  记成了失败；用 worktree 内的 harness 重测 baseline 为 8/8 + 3/3 全通过。
  所以它既不是已知既存失败，也**不作为身份改造或计数/归还修复的判据**，
  只能作为集成回归。
