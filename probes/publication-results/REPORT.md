> **历史实验记录。** 以下测量保留原环境/基线，设计解释可能已撤回；当前结论见[证据索引](../../kernel-mode-design.md)，规范见[原则](../../runtime-design-principles.md)。本文旧源码行号不指向当前 main；原始日志未修改。

# Publication、参数版本与设计 review 核对

日期：2026-09-15。更新后的 [设计文档](../../kernel-mode-prepare-submit-design.md)。

## 范围与环境

代码基线固定为 simpler `a02d96ccb62b6cc4499d513a1214668143d191bc`，包含 #2225 / #2168。C++ 实验使用未修改的工作树 `3e08ede7`。NPU 实验使用 a2a3、CANN 9.0.0、torch 2.7.1+cpu、torch_npu 2.7.1；经 onboard-arch-precheck，再通过 task-submit 分配 device 3。

- [探针源码](../../LOCAL-EVIDENCE.md#artifact-6e4dbf27ff27)，SHA256 `9ef0e6537ca0d8e60e07043c699b6009e1bfa6bed61e35865236665851073da6`。
- [TQ=1](../../LOCAL-EVIDENCE.md#artifact-b8877159573e) 和 [TQ=0 重跑](../../LOCAL-EVIDENCE.md#artifact-b4090e070c54) 均 exit=0，所有断言通过。
- [TQ=0 首次运行](../../LOCAL-EVIDENCE.md#artifact-c7bd8db579dc) 已输出全部实验结果，但 task-submit 随后报告 daemon 停止/重启、exit=137；不将这次计作完整通过，重跑未复现。
- [Orchestrator 编译日志](../../LOCAL-EVIDENCE.md#artifact-ffce64bb2f77)、[测试结果](../../LOCAL-EVIDENCE.md#artifact-5f12e14f2863)。

探针使用 torch_npu 算子模拟两条计算侧流，publication 用实际 pinned-host H2D；最后一臂直接调用 raw ACL。它不运行 Simpler AICPU executor，不验证其 handshake/cache/fault 协议，也不是吞吐基准。

## 新实验证据

| 问题 | 实验与对照 | TQ=0 / TQ=1 |
| --- | --- | --- |
| host 串行改同一 staging 是否足以隔离参数 | 两次 replay 均在未放行的 ExternalEvent 后；先写 71、提交 A，再写 99、提交 B，分别保存结果 | 期望 `[80,108]`，实际 `[108,108]`；首末 completion 都 pending，明确复现 A 读到 B 的源 |
| 两份 bank 能否让 B 发布不等 A 完成 | publication 侧流，1/2 个 bank；每个 H2D 为 **16 MiB**，8 个调用；A 在计算前被 gate 阻住，slot 重用等对应 Done | 1 bank：A 未完成、B 未发布；2 banks：A 未完成、B **已发布完成**。放行后 8 个调用的所有输出元素正确 |
| 上述重叠依赖能否进入 capture | capture 入口 Root fork publication；Ready join caller；slot 重用等前次 Done；再使用两条计算侧流。每种 bank 数捕获 8 个调用，连续 enqueue 8 次 replay | 两种 bank 数均通过，64 个全输出 checksum 正确；首末 replay completion 在 gate 放行前都 pending |
| 同步 H2D 是否必然毁图 | 私有 raw ACL stream 内 CaptureBegin → aclrtMemcpy H2D → GetInfo → aclrtMemcpyAsync H2D → CaptureEnd | 同步返回 **107030**，status **ACTIVE (1)**；后续异步操作和 CaptureEnd 都 **0**，graph 非空 |

### 这些结果如何支持设计

**bank 实验按因果判定，不以耗时猜测。** 2-bank 臂在放行 A 之前同步等待 B 的 publication event，然后确认 A 的 completion 未完成；1-bank 臂仅等待 A 的 publication，确认 B 的 publication 仍未完成。两臂均在 finally 放行 gate。它证明的是 B 的上传可以先完成，不是 DMA 与 AICPU/AICore 计算在硬件上同时占用资源的性能测量。

每个 image 是 4,194,304 个 float32，即 16 MiB。eager 对全部输出元素检查；replay 保存每次每个输出的 sum 再核对，覆盖全输出但不是逐元素等价证明。publication 只有 1/2 份 bank，而 8 个调用都已 enqueue；bank 个数不限制 host 的在途 Submission 个数。

raw ACL 臂验证的是 **aclrtMemcpy 的 H2D**。既有 Probe C 的同步 memcpy 臂是 D2H；本次补上与 metadata publication 直接对应的方向。后续 async 操作只验证捕获接受及图创建，未在这条 raw ACL 臂执行 replay；H2D replay 由前面的 torch capture 臂验证。没有测试 raw rtMemcpy 的单独错误映射。

## Orchestrator 的现有行为

独立 `/tmp` 构建中运行原有测试，未改生产源码或测试预期：

```text
OrchestratorFixture.BuildingSuccessorActivatesAfterPriorRunIsTerminal
OrchestratorFixture.PreparedRunWaitsForActiveRunAndThirdAdmissionBlocks
```

2/2 通过。前者验证后继等待前驱 terminal 才激活；后者验证一个 active、一个 prepared，第三次 begin_run 等待 lease。结合 `a02d96cc` 的 `begin_run:69` / `activate_fifo_head:183`，确认 L3/Python 自己保留了两道 gate；此前 direct ChipRunLane 测试不能替代这个证据。

## Review 意见逐项处理

| 意见 | 核对与文档落点 |
| --- | --- |
| 1：遗漏 Orchestrator；误用 direct lane 描述 Python | 接受。§1 分开 Orchestrator、direct/leased lane；§4 明确上层准入与调度职责，§6 单列 L3 验收 |
| 2：参数/handshake 分离是 device ABI 变更 | 接受。§4 单列三个 device 区域、不同写入顺序和 cache 范围。当前有效 handshake 清除来自 prepare 的 Runtime H2D；launch 的再次清零仅写 host 副本，之后没有 Runtime 重传 |
| 3：单 image 串行 DMA 可能损失 HBG 重叠 | 接受风险及多 bank 方向，并补足独立 publication stream。§3/§4 采用默认 ≥2 bank；**不接受“只双缓冲小 LaunchState 就足够 HBG”**：HBG 当前 GM_HEAP/RUNTIME_IMAGE 是 HOST_PER_RUN，SM 位于后者尾部，直接执行的 image 也需 bank 或串行恢复 |
| 4：同步上传是硬约束 | 接受异步 MUST；纠正“107030 必然毁图”，本次 raw H2D 实验显示仍 ACTIVE / End=0。错误不可忽略 |
| 5：AICPU 稳定地址 envelope 应为 MUST | 接受。§3 要求两侧从同一发布 bank 读取可更新字段；保留动态 inline blob 就需要另行 task update |
| 6：device-source 更新缺少选择理由 | §5 明确：device 更新可由 stream 排序；直接改 host source 需读者完成 fence。新增负对照验证无版本隔离会错；不作更快的无测量承诺 |
| 7：RunStreamPair::ensure 仍要求 idle | §1/§2 移到冷路径，warm submit 不调用带 owner guard 的旧 ensure，stream epoch 被任务/图保活 |
| 8：#2225 的 device DFX reset | §6 纳入其先例；区别 writer cache 的逐调用失效与未消费 record queue 的游标。其 capture 是 clock-anchor，未误写成 ACLGraph |
| 9：fault latch 太简略 | §4 单列 DomainControl，置于 SM/arena/mailbox reset 之外；定义 Execute/Skip、共同退场、部分提交失败与硬故障边界 |
| 10：capacity 未定义 | §2 区分 workspace 上限、publication bank 空间、host/在途/event 预算；Program 另有 tensor IO 预算 |
| 11：PreparedExecution 关系不清 | §3 说明 seal 的演化路径；run identity/slot/retirement 归 Submission，保留已解析数据并补资源 owner。不是简单减字段就完成 |
| baseline / #2168 / 机械链接 | 固定到 a02d96cc，纳入 HBG retained temp slice 到 copy-back 的所有权。runner 链接改 2160、launch_run 改 549；**该 commit 的 a2a3 run_host_orchestration 仍在 619，603 是 graph_context 赋值，不按意见机械改为 603** |

参考另一组更新文档时保留其准确的代码事实，但没有继承“改常驻 pinned source 即可连续换参”“DFX begin_run 只是起线程”“HBG capture 无解”等结论。固定 HBG prepared 结果可设计成 pristine replay；影响图的输入变化需要重新 prepare，完整 runtime 路径仍未验证。

## 后续 review：现有 HBG banking、图内首次使用与挂死边界

本轮重新读取两个 runtime contract、`reap_run`、Probe I 源码，以及另一组设计文档更新后的 §16.1 / §M.3 和 107030 更正。以下是代码核对与设计补充，**没有新跑 NPU 实验**。

| 意见 | 核对与设计更新 |
| --- | --- |
| HBG 双 bank 已是现有契约 | 接受。a02d96cc 的 HBG contract 在 102–126 行声明 depth 2、GM_HEAP/RUNTIME_IMAGE HOST_PER_RUN，并明确支持写 unpublished arena bank；TMR 在 76–98 行声明 TASK_ARGS HOST_PER_RUN、三块 arena DEVICE_SCRATCH。§3 改为以现有 HBG banking 为先例，推广到 TMR LaunchState，并解释 eager attach 正确而 replay 需要恢复的原因 |
| 图内首次 B 项没有可等待的本图旧 Done | 接受。§4 明确每张图独立的 last_use 表：首次使用只依赖 Root，后续轮转才等本图 Done(i−B)；跨图/eager 使用权由图外执行域顺序保证。Probe I 的 enqueue 已用 `if i >= bank_count` 实现此规则，既有日志验证同图重复 replay；没有冒充多图交替实验 |
| bank 数与 host 在途数不能混为重叠深度 | 接受。§2 明确 B 个 bank 至多提前发布 B−1 项；增加 max_inflight_submissions 只扩大 host 准备/排队窗口 |
| fault-before-Done 与 hang 不是同一机制 | 接受。§4 增加可见性不变量；核对 `device_runner.cpp:646–669` 的 sync-time 错误路径后，单列 host/框架完成监测、期限、停止提交、错误兑现、资源隔离及设备所有者恢复。A 不产生 Done 时不能声称 B 会进入 fault gate；仅在 host 暴露的错误也不保证后继没有已发生的副作用 |
| 每 bucket 捕获都需要检查静止条件 | 接受。§5 明确逐图入口检查及累计冷路径成本；已有静止证明可复用，公共 registration 可共享，replay 无逐次 host 同步 |
| hbg-maker 619 → 603 | 再次按固定 commit 核对后保留 619。`git show a02d96cc:src/a2a3/runtime/host_build_graph/host/runtime_maker.cpp` 的 619 是函数定义，603 是 graph_context 地址表达式；不同基线的行号不能套到这个固定链接 |

主文档验收新增多 bucket 图交替、图间穿插 eager，以及挂死/延迟后端错误与受控 fault 分别测试。它们仍是未验证的完整实现要求。

## 后续 review：超时层级、事件计量与版本存储

本轮继续更新主设计，未修改生产代码，也未新增 NPU 运行。重新读取固定基线的 `runtime_timeout_config.h`、AICPU 参数消费者和 device phase buffer 路径；核对另一组更新后的 H2 源码及 [TQ=0](../../LOCAL-EVIDENCE.md#artifact-423e746ea3f1)、[TQ=1](../../LOCAL-EVIDENCE.md#artifact-6262a91c2a85) 日志。两份新日志中臂 2–5 均正确且 exit=0，臂 1 均错 31/32；旧版的 31/30 不作为新日志的统计。

| 意见 | 处理 |
| --- | --- |
| CompletionMonitor 不能引入独立第四层 deadline | 接受。§4 明确接替 stream-sync 外层职责，消费原有 stream_sync_timeout_ms，继承三条严格大小关系、1500 ms arming guard 及三个具名校验错误；保留原有三个环境配置入口。另明确排队等待不能误算执行超时，整图监测须按图内调用和 producer 窗口定容，不能只靠三条下限估计图时长 |
| event_budget 缺单位和公式 | 接受。§2 按事件数量计量：5×同时在途 eager 调用数 + Σ(每图 1 个 Root + 5×图内调用数) + 适配器额外事件。图内事件按常驻图计，图外 replay completion 按在途次数计；attach 按声明预分配，CaptureBegin 前整笔预留 |
| envelope 改造范围应收窄 | 接受稳定 runtime_args 指针是正确先例，也明确其冻结支撑静态 bank 绑定。**不接受“今天只有 enable_profiling_flag”这个穷尽判断**：device_runner_base.cpp:1846/1883 会为禁用或 reset 失败将 device_wall_data_base 置零，kernel.cpp:120 从 blob 读取该值。设计规定固定地址加发布区使能/有效状态，或将动态地址本身发布；不能丢掉 phase capture 的逐调用语义 |
| host/device 版本的取舍应按 HBM 与带宽表达 | 接受。§5 区分不可变版本、graph-owned 稳定 device 槽、publication bank；host 版本异步 H2D 更新槽不要求逐轮 host fence，device 版本用 D2D 并占每版本 HBM。引用 H2 臂 3/5，仅声称无逐轮显式 Event 等待，不推断 API 内部无阻塞或大 image 带宽优劣 |
| bank 数不能在已有图上切换 | 接受。§2 明确 attach/generation-time 决定，改变 bank 数必须新 generation、新 graph |
| hbg-maker 改 603 | 仍保留固定基线的 619；本轮再次读取 a02d96cc，619 为 run_host_orchestration 定义，603 为 graph_context 地址表达式 |

结合上一轮对其他文档的 review，主设计还显式画出“图外版本 → 槽、图内槽 → bank”，并区分源 DMA 读取 fence 与 H2 臂 2 实际使用的整图完成 fence。超时监测和事件资源模型仍需完整 executor 实现验收；这轮代码核对不等于那些路径已通过实验。

## 后续全面 review：PMU 生命周期、换参内存与 handshake reader

本轮按相同的 `a02d96cc` 基线读代码并更新主设计。没有修改生产代码或新增 NPU 运行；机械检查和预算算例见 [核查记录](../../LOCAL-EVIDENCE.md#artifact-7cb73086c9a8)。

| 意见 | 核查与处理 |
| --- | --- |
| pmu_reg_addrs 不能与 regs 一并当作常驻 | 接受。这是此前生命周期检查的遗漏。device_runner.cpp:303–305 每 run 初始化 PMU 地址表；host_regs.cpp:221–235 实际 alloc/H2D；cleanup :455–459 free；AICPU kernel.cpp:81 内联读地址。§3 明确必须迁到 context/generation owner，图保活，run cleanup 不再释放；仅移动指针字段不能防悬空。补入常驻 metadata 预算和 PMU-on capture/cleanup/replay 验收 |
| 可更新 graph 槽与版本/staging 没预算 | 接受。ExecutionCapacity 增 graph_binding_device_bytes、device_version_bytes、update_staging_host_bytes/device_bytes；资源表同步，捕获前预留槽、更新前整批预留源与 staging。不可变版本若能直接 DMA，不强制另造一份 staging；按物理 allocation 计费，直到最后读取完成才可回收。固定绑定 replay 不承担可更新槽开销 |
| handshake 的 reader 与实际 allocation | 接受。AICore 直接取 runtime->dev.workers，AICPU pre_handshake_init 接收同一个 Runtime；Runtime 前缀分配在 SlotPersistentArgs.runtime_args。launch_run 注释的 pooled arena 归属错误。加 reader 和 allocation 的固定链接；dcci CACHELINE_OUT 支撑 cache-line 隔离要求 |
| HBG 实际上传量已有仪表 | 接受。copied_bytes + image_bytes、compacted==image_bytes、BindArenaH2d bytes/copied/sm 都已存在，优先解析 STRACE。字节和旧同步耗时可复用；新异步路径的重叠仍需要 device DMA/执行区间，不能用 host enqueue 耗时替代 |
| event_budget 可能 K 倍保守 | 接受为验收问题。保留逐图独占公式作基线，增加按节点位置共享的候选公式 1+5×max(N)。Probe D 是共享的正面证据，但未覆盖新闭包及逐次结果；通过多图、重复 replay、重捕/销毁验收后才降低预算 |
| timeout-config 锚点 | 改到 L34 的 RuntimeTimeoutConfig 定义；其上方 producer-window/arming-guard 注释仍在同一引用上下文 |

预算算例：48 图×8 节点×16 MiB = 384 槽、6 GiB；双 bank 为 32 MiB，比例 192。图内独占事件为 1968，共享候选为 41，均不含 eager/adapter 额外事件。16 MiB 只是本算例的声明容量，不当作真实 HBG image 大小。

## 复现

从工作区根目录：

```bash
simpler/.claude/skills/onboard-arch-precheck/check.sh a2a3
task-submit --device auto --device-num 1 --max-time 180 --timeout 300 \
  --env TASK_QUEUE_ENABLE=1 \
  --run 'source simpler/.venv/bin/activate && python docs-kernel-mode/probes/probe_i_publication_contracts.py'
```

另用新进程设置 TASK_QUEUE_ENABLE=0；既检查断言，也检查 task-submit 的最终退出码。未重建或替换已安装 runtime 动态库。

## 未验证边界

没有证明真实 Simpler 的 HBG 吞吐增益、完整 serializer 改造、AICPU/AICore reader/cache 协议、device fault latch、挂死恢复、不同 captured graph 共享 bank 的生命周期、DFX 多次 replay 消费队列或 AICPU task update 已可用。方案落地仍须满足主文档 §6 的设计验收。a5 未跑。
