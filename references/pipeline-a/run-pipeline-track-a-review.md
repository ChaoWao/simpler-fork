> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# A 轨设计 review

> **历史设计 / 评审快照**：保留文中的原始基线、结论与验证记录，不代表当前待办或支持范围。当前进度统一见 [路线与阶段表](run-pipeline-track-a.md) / [HTML 看板](run-pipeline-track-a.html)。

## 当前处理状态

本页下方保留各次 review 的原始意见；其中行号对应当时的设计版本。
**2026-09-16 决定：native host 访问采用调用方合同。** 调用方保证 prepare 所需入口就绪、
host get/set 无冲突；依赖前次运行时先显式等待（含必要回拷）再提交。
本次图内 producer 检查不变。历史 review 中要求新增 native 跨 run 冲突识别、
自动等待相关前驱及将其列为 P4 前置门的建议已撤回；simpler 不承诺检测合同外的冲突。
验收改为就绪入口、共享只读重叠、显式等待后的正确读写，以及图内依赖拒绝。

最新一轮意见已融入设计正文及 HTML：

- §1 / §2.1 / §9.3：P0 补公共设备时间坐标，P1 保留 per-run 结果；明确首个指标为
  AICPU RunWall 间隔，不冒充双流完整边界的空档。
- §3.3 / §5.2 / §9.1：逐流记录 kernel、fence 和 wait 引用，区分空流与仅有 wait 的流，
  加入 wait 已提交后发生 launch / record 失败的验收。
- §5.2：补充 native 到 lane 的执行静止、pair 健康和业务结果分类要求。
- §3.2 / §5.4 / §1：修正“当前故障”措辞、caller stream 残留及跨负载收益判据。

这些设计缺口已关闭；对应运行时代码与设备验证仍属于实施任务，本次仅修改文档。

## 最新复核：2026-09-15 20:50 版设计

对象：`run-pipeline-track-a.md`（文件时间 20:50:13）；代码 `748c39fbe`。
本次只做代码与设计核查，未运行设备测试，未改设计正文。

### 结论

上轮主要问题已经吸收：三个 native 门、pair/run 状态分离、LAUNCHED 后继不能提前释放、
stale 后等待全部在飞退休、native accessor 在 P4 放开之前验证，以及两槽里程碑和性能目标
分开。HBG 就绪入口可读、本图 producer 结果拒绝的边界也保留正确。

当前方向可以继续推进。还需补齐一处测量能力，以及收紧一处失败释放条件。

### 1. [P2] 开 TIMING 能拿到单 run 时长，现有输出不能直接计算跨 run gap

设计位置：`run-pipeline-track-a.md:34-37,481,490-517`。

§9.3 对开关的判断正确：`device_phase_capture_enabled()` 不依赖重型诊断。
但这只解决“测量会不会把排队关掉”，尚未解决“输出里有没有跨 run 的公共时间坐标”：

- `src/common/platform/onboard/host/c_api_shared.cpp:630` 把每次
  `chip.run.runner_run.device_wall` 明确输出为起点 **0**，只保留 duration。
- `src/common/platform/onboard/host/device_runner_base.cpp:2014-2023`
  每次重新选取 `origin`，对子阶段保存 `start_cycles[p] - origin`。
- `simpler_setup/tools/containment.py:108-113,426-429` 同样说明这是 run 内相对时间，
  `device_wall` 自身的真实起点并未发布。

所以从当前 TIMING 日志直接做 `start(N+1) - end(N)` 得不到实际空档。
底层 `AicpuPhaseRecord` 已有原始 start/end cycle，无需重新设计采集器：
可在轻量输出中保留同一设备/时钟代际的原始边界、频率及 run 身份，或另定等价的公共时基。
同时明确测的是 AICPU RunWall 间隔，还是双流完整执行边界间隔；两者不能不加区分。
将这项输出能力放入 P0/P1，并用于 P5 的一致口径比较。

### 2. [P2] Partial 的单流完成证明，需要显式覆盖另一条流已入队的 wait

设计位置：`run-pipeline-track-a.md:139-141,343-349`。

§3.3 已正确指出 `NotStarted` 不等于没有 wait 入队，但 §5.2 的汇总表写为
“该流 event fired 且另一条流从未提交”，没有区分 kernel 提交与 stream 命令提交。
结合 §3.2 的 `kernel_submitted` 状态，实现时很容易按“另一条流没提交 kernel”解释。

按照 §3.4 的新序列，另一条流可能已经成功 enqueue `StreamWaitEvent`，随后 kernel
launch 失败。某条流的本次 fence 完成，并不证明另一条流的 wait 任务已完成；不能据此
一次释放本 run 的全部 committed event 引用。SDK `acl_rt.h` 的 `aclrtDestroyEvent`
契约也要求相关 wait/synchronize 任务已完成。

建议把表格改成明确条件：

- 无 kernel 的那条流也没有任何已入队命令，才可视作空流；
- 若已入队 wait，则其引用单独保留，直到覆盖该 wait 的完成证明或设备静止证明；
- 本 run 参数资源与它持有的前驱 event 引用可有不同退休时刻。

补一个“两条流的 wait 已成功，后续 kernel/record 失败”的故障注入场景，验证不会
提前 destroy event，也不会等待永远不存在的本次 fence。若“从未提交”本意就是
没有任何命令，应在正文直接写明；这是规则精化，不推翻 per-run fence 方案。

### 实施时保留的细节（不作为新增架构阻塞）

- §5.2 将业务失败定为 run 级，是目标行为。当前 `chip_run_lane.cpp:102-108`
  对任意 finalize 异常都会 poison lane；`chip_worker.cpp:877-914` 将非零 finalize rc
  抛为普通异常。实现清单需包括错误分类从 native 到 lane 的传递，避免仅改准入门却
  继续把业务失败升级为队列故障。
- §3.2 表头“今天”应改为“直接将 record 错误并入现有 callback 时”：当前事务没有
  接入这些 per-run event，不能把重构陷阱当成当前已观察到的 record 失败缺陷。
- §5.4 的 enqueued 仍写 caller stream，与 §3.4 冲突；A 轨应写 runner 内部执行流。
- §1 的历史跨负载数字只能作背景，不能重新作为 §7 已撤回的容量结论或收益阈值。

已有 `aicore_done` host 清零路径和 watchdog 有效预算的待核实项继续保留在
`KNOWN_ISSUES.md`，本轮未新增设备实验结论。

---

## 历史复核（以下问题的处理状态以上面的最新复核为准）

对象：`run-pipeline-track-a.md`。源码基线：`748c39fbe`。
范围：现有 program 路径的 prepare / launch / completion / finalize 与持续提交；不审 B 轨实现。
本轮只读源码和文档，未运行 benchmark 或设备测试，未修改设计正文。

## 阅读依据

- `docs/task-flow.md:255`：资源按 lease 选择，stream pair 不属于 slot；`:305` 整个父 run 的 FIFO 准入；`:367` frame staging；`:592` 回拷完成后才通知上层。
- `docs/worker-manager.md:114`：WorkerThread 是 endpoint lane 对象，统一由 Scheduler 驱动，自己没有独立线程；ACTIVE/STAGED 是不同用途的 lane。
- `docs/hierarchical-level-runtime.md:107`：L3 Python orchestration、Scheduler 和子进程的职责，不等同于 L2 HBG 的 host 构图。
- `src/a2a3/runtime/host_build_graph/docs/RUNTIME_LOGIC.md:1`：完整构图、上传、执行、回收的先后关系；Definition/SM image 是本次内容。
- `docs/investigations/2026-08-hbg-consumer-wait-cannot-observe-device.md`：禁止构图等待本次 task 的结果，不限制读取已经就绪的入口值。
- `docs/investigations/2026-08-hbg-graph-definition-single-upload.md:90`：测量区分冷启动与稳态，其数值属于该历史 qwen 场景。

这些文档中有关 A5 depth、frame 数、TRB/诊断 successor 是否只能验证的部分已经过时。本 review 以当前 runtime contract、chip lane 和 mailbox 代码为准，未将旧描述当作当前限制。

## 总结

去掉 tiling cache、纯 host 构建接口、ACLGraph，保留现有 program 的外部接口，是正确的 A 轨范围收缩。
但当前稿不能直接作为完整实施清单：它重新把 2 设为最终限制，遗漏 stream pair 的单 owner 状态改造，并把 native 构图访问误当成已经受 L3 控制 gate 保护。
建议以双在途验证作为第一个增量，同时保留取消固定槽位的最终目标；先解决设备队列的所有权和 native prepare 副作用，再放开准入。

## 1. [P1] 深度 2 可以是首个增量，不能未经测量认定已取得全部收益

设计位置：`:14-18`、`:291-309`、`:370-374`。

此前目标包含解除人为固定两槽限制。优先做 program 不等于撤回这个目标。新稿将其改为“同样深度 2”，并把 mailbox 拆分限定为抗抖动优化，缺少依据。

源码确实允许复用现有两套资源先验证两个 launched，这是合理的小步。但 `chip_run_lane.cpp:439-446` 的第三次 submit 仍要 drain_front，才能得到空闲 lease 并开始 prepare；L3 `begin_run` 的资源准入也发生在 callback 之前。

在两个槽均被占用、无预建第三个请求的情况下：N 完成后，N+2 要等 N 的 finalize / slot 回收，再经过 prepare / upload / launch。要使 N+1 后面没有空档，需要这条补位路径能被 N+1 的设备执行窗口覆盖。代码没有保证这个时间关系。
两槽让 N→N+1 不再等待 N 的 host 尾巴，不代表持续运行时每个边界都不等待 host。更深队列也不能消除长期 host 吞吐瓶颈，所以应测量，而不是对任一深度作普遍保证。

建议：

- 把“两 launched”写为首个验证里程碑，不写成 A 轨最终深度规格。
- 连续提交多轮，分别观察启动、稳态和收尾；记录第三次及后续 submit 的阻塞、slot 回收和设备 gap。
- mailbox/status 与资源容量的扩大可以分步做，但将“何时需要做”交给目标和测量，不以“2 必然足够”预先排除。

## 2. [P1] submit token + lane 不足以解开 RunStreamPair 的单 owner

设计位置：`:94-95`、`:243-250`、`:324`。

源码有具体的第三处状态约束：

- `src/common/platform/include/host/run_stream_pair.h:83`：只要 owner 非空，ensure 就失败。
- 同文件 `:112`：mark_submitted 保存单一 owner，submitted/complete 也只有一份。
- 同文件 `:148`：retire 比较这个 owner，未证明完成时可能销毁 stream。
- `src/a2a3/platform/onboard/host/device_runner.cpp:558`：每次 launch 先 ensure；poll/drain 同样经这一个 pair。

因此，释放 C API 的 native claim 后，N+1 仍会在 ensure 被拒绝；若只删检查，则 N+1 覆盖 owner，N 的 retirement 和 N+1 的在途状态混淆。

建议将以下作为 P2/P4 的明确产物，而非只写“换 poll API”：

1. pair 只持有 stream、代码代际和整个队列的健康状态。
2. 每个 run 持有自己的提交事实、完成 fence、结果及退休状态。
3. 正常 finalize 只退休本 run；不能清掉其他 run 的状态。
4. code stale 的替换和失败 teardown 必须考虑所有已入队使用者。
5. 任一 run 失败导致共享 stream 不可用时，要定义已入队后继如何终止、何时可回收，而不只处理未 launch 的后继。

当前 `ChipRunLaneState::finish` 会在 finalize 失败时 poison lane；多 launched 后，poison 不等于已取消后继设备任务。资源释放必须以真实静止为准，不能沿用“后继尚未执行”的隐含假设。

## 3. [P1] native 构图的入口访问需要在 P4 前有明确顺序规则

设计位置：`:197-222`、`:288-289`、`:340`。

“今天 await_run_admission 把它们挡住”对 native HBG `set_tensor_data` 不成立。

- `python/simpler/orchestrator.py:255-269` 的 gate 针对 Python direct_control。
- `src/common/host_build_graph/host/host_tensor_access.cpp:134,151` 直接读写映射或调用 HostApi copy，不经过上述 Python gate。
- `src/common/worker/chip_run_lane.cpp:157` 在前驱 LAUNCHED 时即可 prepare successor。
- `tests/st/a2a3/host_build_graph/available_aicore_counts/kernels/orchestration/available_aicore_counts_orch.cpp:73-83` 本就先记录一个 task，再 set 另一个没有 producer 的入口 tensor；Python 首次 submit 的限制不能套到这里。

`require_no_producer` 正确地拒绝本次图内 producer，保持不动。但它不证明另一在途 run 已不再访问同一入口 backing。

建议：在 native prepare/access 层明确安全规则，P4 直连路径也必须覆盖。
不必新增全局依赖推断：可以使用已有且可验证的访问声明决定提前 prepare，无法证明安全的访问保守等待；或在实际访问时等待相关前驱的原生完成/必要回拷。
若选择访问时等待，还要明确等待路径能推进前驱完成且不持有会阻止该进展的锁；不要在 accessor 内无条件调用高层 drain。

“每次写都发生”的验收不足。应验证前驱仍读取 X 时后继构图写 X 不提前发生；前驱产生入口值时后继读取到正确值；独立且已就绪入口不增加无关等待。

## 4. [P2] caller stream 入口/出口仍混入了 A 轨

设计位置：`:145-157`、`:271`、`:337`。

`docs/task-flow.md:283` 明确 runner 自有一对执行流；当前 `Worker.submit(callable,args,config)` 不接受 caller stream，工作发生在 chip 子进程。caller stream 是 kernel 接口的接入合同。

A 轨所需的是：

- AICPU/AICore 的跨 run join；
- program 的输入就绪和 native 构图访问顺序；
- per-run completion 后的输出回拷、状态收集和 RunHandle 终态。

建议把 caller 前序/后序两组 join 和普通 PyTorch 算子顺序验收移到 B 轨；A 轨 enqueued 指已提交到 runner 的内部执行流。
同样，captured graph 扩容规则可保留为公共代码不能回退的约束，不作为 A 轨新能力或验收前置。

## 5. 可保留但要具体化的资源与测量条目

- **HBG 私有 image 先保持现状。** `runtime_maker.cpp:93-115` 已按 lease 隔离 host 写入的 arena；A 轨不必先做 Runtime 字段共享化或 heap 合并。这样也避免重复引入 v7 中 image 与 heap 复用条件混淆的问题。
- **TRB 兼容性 probe 留在 prepare 前。** `tensormap_and_ringbuffer/host/runtime_maker.cpp:434` 的 probe 防止 bind 时覆盖共享缓存；只防“容量增长”不覆盖相同容量下的布局变化。
- **TMR 错误快照需要设备有序产出。** 如果后继已排队，不能靠 host 在两 run 间抢读共享 header。P1 应明确 per-run device 结果区或等价机制，并让完成 fence 覆盖快照，而不只写“开跑前快照”。
- **device_wall 每 run 一份是必要的；重型 DFX 可以继续限 1。** 但不能用强制限 1 的 chip-swimlane 配置验收多 launched 的 gap。可使用独立的 device-phase timing：`device_phase_capture.cpp:20` 由 TIMING/STRACE 开关控制，`dfx_run_config.h:49` 的 diagnostics_any 则包含 chip-swimlane 等重型诊断。P0 与 P5 应明确测量模式。
- **历史 qwen bind 数值不作为 dsv4 判据。** 原调查的稳态/冷启动数据有清楚限定。P0 应使用目标 workload 的连续 Worker.submit，比对端到端吞吐、设备 gap、补位延迟及资源峰值；不用“gap 小于 bind 的零头”单独判定项目价值。
- **event 表的“今天”需改名。** 当前 launch 尚未记录这些 per-run event；把 record 错误并入 callback 是重构时的潜在误判，不是已观察到的当前故障。

## 建议的 A 轨最小推进顺序

1. 明确首个增量与最终目标，先用现有两套 lease/image 做基线和双在途验证。
2. 拆 pair 级与 run 级状态；实现 event、失败静止及多在途资源退休；device_wall/TMR 状态结果独立。
3. 补 native prepare/access 的冲突规则，保留 HBG 图内 producer 拒绝及 TMR cache probe。
4. 放开直连 lane 的后继 launch，验证两次实际设备提交先于前驱完成。
5. L3 按父 run 提交顺序、旁路完成规则放开，不动其对外接口和内部 HBG recorder 合同。
6. 用持续序列决定并验证资源/状态容量扩展；若仍以取消固定 2 为目标，不能以双在途里程碑代替最后这步。

本轮未发现必须为了 A 轨新增 host 进度线程的理由；现有 Scheduler/child loop 和 direct submit 的 drain 已有进展路径。保留这些机制，但用持续提交与失败用例验证，不能由“有人 finalize”推导“host 尾巴永远能被隐藏”。
