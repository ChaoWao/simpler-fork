# 目标来源与覆盖口径

> **发布快照：2026-10-15。** 设计、实现状态与历史调查须按正文各自基线阅读；不代表当前 main 全量验收。参见[阅读入口与已知更正](README.md)。
>
> **2026-10-13 通信定点核验**：`upstream/main@68d137fdfd435d8067f0efd9836cb428da3e700a`；启动采用 mpirun；PTO 通信从原生 CANN ACL Runtime 接口构建。A3 使用 ACL/Fabric V2 共享窗口；SHMEM 是实现参考，不依赖或计划引入 `libshmem`。证据与层次见 [通信方案](l3l4/cluster-comm.md#d4-remote-l3-通信mpirun--共享窗口)。本次不推进全目标扫描基线。已核实面向 main 的持续双机 PR CI：近期三个具名测试 SHA 的五项 NETWORK1 用例实际 PASS；逐项覆盖、失败样本与 NaN 验金缺口见通信方案 D.4。
> **验证范围**：9 项文档结构/投影守卫通过，桌面与手机渲染通过。两项 PR 覆盖守卫尚未通过：merged 检查缺少新 PR 的目标分支元数据；open 检查仍有未登记条目；本次仅补充通信 CI 样本引用，未完成全量 PR/issue 登记。此处不宣称 PR 登记已刷新或全目录状态一致。
>
> 源码基线：`upstream/main@01bf24bfa8a997c89210258c5954c4105843d783`（2026-10-07）。PR 只采用 baseRefName=main；2029 个 PR 元数据中 27 OPEN/DRAFT，422 个 issue 中 68 OPEN。34 个实施项，286 个来源编号，76 个文档载体；编号有家不等于目标已完成。状态真源为 [work-items](work-items.md)。

## 定点合入证据（2026-10-08）

\#2096 已合入 main `ae234fad000cd6bfa9b67865e336d7e90058f7ad`，三个核心文件与已审 head 一致。RA-11 计部分交付；通用同步、既有 Buffer/slice 绑定、RA-4/RA-7 验收仍保留。登记增量包含 #2500–#2508，当前 27 个 main OPEN PR、69 个 OPEN issue；以下来源全集与全量源码证据保留 2026-10-07 口径，本节不冒充新一轮全目标扫描。

## 1. 来源与编号全集

goal-sources.json（本地参考：`goal-sources.json`） 保存 16 组本地提取规则与外部正文 SHA256。本轮重读全部外部来源，#2429 正文有更新，其他既有来源正文及 Step 3 评论 hash 保持；新 acceptance A 编号按 checklist 顺序生成，A2RET-R 来自原文 numbered list。PR-S3-1 属 EX-7；#2495 的子目标分别落 RA-2/RA-3 或范围外 runtime/DFX，不另造总账实施线。

| 来源 | 编号全集 | 归属 |
| ---- | -------- | ---- |
| P1/P2 stages：[feature_roadmap.md](feature_roadmap.md) | `P1-A` `P1-B` `P1-C` `P1-D` `P1-E` `P1-F` `P2-A` `P2-B` `P2-C` `P2-D` `P2-E` | work-items 吸收列 / §5–§7 |
| W resource capability：[cpu-npu-shared-memory/work-breakdown.md](cpu-npu-shared-memory/work-breakdown.md) | `W0` `W1` `W1lite` `W2` `W3` `W4` `W5` `W6` `W7` `W8` | work-items 吸收列 / §5–§7 |
| L3/L4 scenarios：[l3l4.md](l3l4.md) | `A.1` `B` `C.1` `C.2` `C.3` `D.1` `D.2` `D.3` `D.4` `D.5` `E` | work-items 吸收列 / §5–§7 |
| Audit AR/AF：review/2026-08-11-code-vs-design-audit.md（本地参考：`review/2026-08-11-code-vs-design-audit.md`） | `AF1` `AF2` `AF3` `AF4` `AF5` `AR1` `AR10` `AR2` `AR3` `AR4` `AR5` `AR6` `AR7` `AR8` `AR9` | work-items 吸收列 / §5–§7 |
| P1 gates：tasks/task-p1a-close-merge-gates.md（本地参考：`tasks/task-p1a-close-merge-gates.md`） | `G1` `G2` `G3` `G4` `G5` `G6` | work-items 吸收列 / §5–§7 |
| ST characterization：tasks/task-scenetest-singleton-alias.md（本地参考：`tasks/task-scenetest-singleton-alias.md`） | `ST-1` `ST-2` | work-items 吸收列 / §5–§7 |
| Graph boundary requirements：[worker-memory-model/graph-execution-contract.md](worker-memory-model/graph-execution-contract.md) | `GE-R1` `GE-R2` `GE-R3` `GE-R4` `GE-R5` `GE-R6` | work-items 吸收列 / §5–§7 |
| DFX T1-T9：others-ref/worker-async-pipeline/trace-v1.md（本地参考：`others-ref/worker-async-pipeline/trace-v1.md`）、dfx/c2a-hierarchical-trace.md（本地参考：`dfx/c2a-hierarchical-trace.md`） | `T1` `T2` `T3` `T4` `T5` `T6` `T7` `T8` `T9` | work-items 吸收列 / §5–§7 |
| Qwen Q1-Q4：others-ref/worker-async-pipeline/qwen-integration.md（本地参考：`others-ref/worker-async-pipeline/qwen-integration.md`） | `Q1` `Q2` `Q3` `Q4` | work-items 吸收列 / §5–§7 |
| Pipeline delivered / planned chapters：others-ref/worker-async-pipeline/implementation-record.md（本地参考：`others-ref/worker-async-pipeline/implementation-record.md`）、others-ref/worker-async-pipeline/implementation-plan.md（本地参考：`others-ref/worker-async-pipeline/implementation-plan.md`） | `PIPE-A0` `PIPE-A0b` `PIPE-A1` `PIPE-B1` `PIPE-B2` `PIPE-B3` `PIPE-B3a` `PIPE-B3b` `PIPE-B4` `PIPE-B5` `PIPE-B6` `PIPE-B6a` `PIPE-B6b` `PIPE-B6c` `PIPE-D1` `PIPE-E1` `PIPE-E2` `PIPE-H1` `PIPE-H2` `PIPE-N1` `PIPE-W1a` `PIPE-W1b` `PIPE-W1c` | work-items 吸收列 / §5–§7 |
| Kernel principles：docs-kernel-mode/kernel-mode-plan.html（本地参考：`docs-kernel-mode/kernel-mode-plan.html`） | `KERNEL-Principle-01` `KERNEL-Principle-02` `KERNEL-Principle-03` `KERNEL-Principle-04` `KERNEL-Principle-05` `KERNEL-Principle-06` `KERNEL-Principle-07` `KERNEL-Principle-08` `KERNEL-Principle-09` `KERNEL-Principle-10` `KERNEL-Principle-11` | work-items 吸收列 / §5–§7 |
| Pipeline board extension：[README.md](README.md) | `PIPE-C2a` `PIPE-E1′` `PIPE-W1d` | work-items 吸收列 / §5–§7 |
| P1 ABI extra gates：tasks/task-p1a-close-merge-gates.md（本地参考：`tasks/task-p1a-close-merge-gates.md`） | `P1A-A1` `P1A-A2` | work-items 吸收列 / §5–§7 |
| Historical pipeline W aliases：others-ref/worker-async-pipeline-through-q1-summary-and-remaining-three-prs.md（本地参考：`others-ref/worker-async-pipeline-through-q1-summary-and-remaining-three-prs.md`） | `PIPE-v1-W1` `PIPE-v1-W2` `PIPE-v1-W3` `PIPE-v1-W4` | work-items 吸收列 / §5–§7 |
| Trace/log source requirements：others-ref/host-trace-log-design/trace-and-log-requirements.md（本地参考：`others-ref/host-trace-log-design/trace-and-log-requirements.md`） | `LOG-C2` `LOG-C4` `LOG-C5` `LOG-C6` `LOG-C7` `LOG-D5` `LOG-F1` `LOG-F2` `LOG-F3` `LOG-F4` `LOG-F5` `LOG-F6` `LOG-F7` `LOG-G1` `LOG-G10` `LOG-G11` `LOG-G11b` `LOG-G12` `LOG-G13` `LOG-G14` `LOG-G15` `LOG-G16` `LOG-G17` `LOG-G18` `LOG-G19` `LOG-G2` `LOG-G20` `LOG-G21` `LOG-G22` `LOG-G23` `LOG-G3` `LOG-G4a` `LOG-G4b` `LOG-G6` `LOG-G6b` `LOG-G7` `LOG-G8` `LOG-G9` `LOG-N1` `LOG-N2` `LOG-N3` `LOG-N4` `LOG-T1` `LOG-T2` `LOG-T3` `LOG-T4` `LOG-T5` | work-items 吸收列 / §5–§7 |
| Kernel memory proposals：docs-kernel-mode/kernel-mode-memory.html（本地参考：`docs-kernel-mode/kernel-mode-memory.html`） | `KERNEL-Memory-D1` `KERNEL-Memory-D2` `KERNEL-Memory-D3` `KERNEL-Memory-D4` `KERNEL-Memory-D5` `KERNEL-Memory-D6` | work-items 吸收列 / §5–§7 |
| [2460](https://github.com/hw-native-sys/simpler/issues/2460) · SHA256 `20f03b873343` | `U1a` `U1b` `U2a` `U2b` `U3` `U4` | work-items 吸收列 / §5–§7 |
| [2429](https://github.com/hw-native-sys/simpler/issues/2429) · SHA256 `18baec5daa81` | `VLLM-1` `VLLM-2` `VLLM-3` `VLLM-4` `VLLM-5` `VLLM-6` `VLLM-7` `VLLM-8` `VLLM-9` `PR-S3-1` | work-items 吸收列 / §5–§7 |
| [2450](https://github.com/hw-native-sys/simpler/issues/2450) · SHA256 `196447309114` | `KV-R1` `KV-R2` `KV-R3` `KV-R4` `KV-R5` | work-items 吸收列 / §5–§7 |
| [2316](https://github.com/hw-native-sys/simpler/issues/2316) · SHA256 `1363133e1088` | `TrackA-P1` `TrackA-P2` `TrackA-P3` `TrackA-P4` `TrackA-P5` | work-items 吸收列 / §5–§7 |
| [2429-step3](https://github.com/hw-native-sys/simpler/issues/2429#issuecomment-5862270515) · SHA256 `f74554b81b5e` | `VLLM-S3-1` `VLLM-S3-2` `VLLM-S3-3` `VLLM-S3-4` `VLLM-S3-5` `VLLM-S3-6` `VLLM-S3-7` | work-items 吸收列 / §5–§7 |
| [2425](https://github.com/hw-native-sys/simpler/issues/2425) · SHA256 `9de305a340f6` | `EE-QA-1` `EE-QA-2` `EE-QA-3` `EE-QA-4` `EE-QA-5` | work-items 吸收列 / §5–§7 |
| [pr2466](https://github.com/hw-native-sys/simpler/pull/2466) · SHA256 `76b7022839f4` | `CAP-R1` `CAP-R2` `CAP-R3` | work-items 吸收列 / §5–§7 |
| [1792](https://github.com/hw-native-sys/simpler/issues/1792) · SHA256 `f48d50e6e8ac` | `LOG-I1` `LOG-I2` `LOG-I3` `LOG-I4` `LOG-I5` `LOG-I6` `LOG-I7` | work-items 吸收列 / §5–§7 |
| [2496](https://github.com/hw-native-sys/simpler/issues/2496) · SHA256 `36baae0d0d1a` | `A5SVM-A1` `A5SVM-A2` `A5SVM-A3` `A5SVM-A4` `A5SVM-A5` `A5SVM-A6` | work-items 吸收列 / §5–§7 |
| [2497](https://github.com/hw-native-sys/simpler/issues/2497) · SHA256 `fdee7dc54897` | `XPSVM-A1` `XPSVM-A2` `XPSVM-A3` `XPSVM-A4` `XPSVM-A5` `XPSVM-A6` `XPSVM-A7` `XPSVM-A8` | work-items 吸收列 / §5–§7 |
| [2498](https://github.com/hw-native-sys/simpler/issues/2498) · SHA256 `198cb83f2594` | `SIMT-A1` `SIMT-A2` `SIMT-A3` `SIMT-A4` `SIMT-A5` | work-items 吸收列 / §5–§7 |
| [2492](https://github.com/hw-native-sys/simpler/issues/2492) · SHA256 `dcfda1b4e9bc` | `A2RET-R1` `A2RET-R2` `A2RET-R3` `A2RET-R4` | work-items 吸收列 / §5–§7 |
| P3 delivery | `P3①` `P3②` `P3③` `P3④` `P3⑤` `P3⑥` | work-items 吸收列 / §5–§7 |
| P0 milestones | `P0.1` `P0.2` `P0.3` | work-items 吸收列 / §5–§7 |
| P0 followup | `P0.2-f1` `P0.2-f2` `P0.2-f3` `P0.2-f4` `P0.2-f5` `P0.2-f6` `P0.2-f7` | work-items 吸收列 / §5–§7 |
| P0.2 batches | `P0.2-a` `P0.2-b1` `P0.2-b2` `P0.2-c` `P0.2-d` | work-items 吸收列 / §5–§7 |
| P0.2 teardown subunits | `P0.2-D0` `P0.2-D1` `P0.2-D2` `P0.2-D3` | work-items 吸收列 / §5–§7 |
| P0.3 acceptance | `P0.3-R1` `P0.3-R2` `P0.3-R3` `P0.3-R4` | work-items 吸收列 / §5–§7 |
| Qwen integration aliases | `C2b` `V1` | work-items 吸收列 / §5–§7 |
| Kernel stack | `KERNEL-H1` `KERNEL-H2` `KERNEL-H3` `KERNEL-K2` `KERNEL-K3` `KERNEL-K4` `KERNEL-K5` `KERNEL-K7` `KERNEL-2a` `KERNEL-2B` `KERNEL-10a` `KERNEL-Binder` | work-items 吸收列 / §5–§7 |
| Explicit exclusions | `PIPE-C1` `RequestSession` | work-items 吸收列 / §5–§7 |

## 2. 实施、约束与排除

原有 P1/P2/P3、W、RA/AF、ST/GE、Pipeline、DFX、Qwen 与 kernel 排除编号均保留唯一归属。新增 #2496 六条验收归 RA-2，#2497 八条归 RA-3（权限/寿命/可见性联验 RA-8/RA-15/RA-4），#2498 五条与 #2492 四条明确范围外；不把外部能力需求登记当作实现交付。

访问与退休契约的源/目标、三种完成事实、部分提交、关闭/grant、预算、capture 与六组反例仍分别归 work-items §6。kernel 第四线与契约 D 两条裁决仍有对象：本轮 merge 不完成 Buffer/domain owner/锁序/journal 组织。#2497 的 per-process grant 加强既有寿命验收，并不替用户选择 registry 数量。

## 3. 具名代码证据

以下全部核到 `01bf24bfa8a997c89210258c5954c4105843d783`，读取采用 git show upstream/main:path。对未变领域另比较上一扫描后的相关文件 diff；不以提交标题或 issue 勾选代替源码。本轮不执行产品 UT/硬件测试，CI 日志引用限定 head。

| 结论范围 | 当前源码 / 阳性对照 |
| -------- | ------------------- |
| RA-1 | a2a3 device_runner.cpp:1255 的注册仍传 device_id_；acl_to_hal_device_id 0 处，对照 halHostRegister 16 处 |
| RA-2/RA-3 | A5 DFX register_cb=nullptr；host/host_tensor_access.cpp:135 先探测 mapping、181 copy fallback；HOST_SVM_MAP 5 处均在 comm_endpoints，注释明确尚无 producer。#2496 与 #2497 分开验收 |
| RA-4 | worker.py:2623–2635 顺序 carve 并 offset += nbytes；#2273 隔离验收仍独立 |
| RA-5/DS-3 | entry TensorTransfer 已有；Remote v4 仍有限 request 表达，#2460 U3 成对迁移合同未变 |
| RA-6/RA-13 | tensor.h:120–125 仍逐维比较 stride；#2489 program_tensor_args.h 的整调用预检拒绝不支持的 HOST layout，不修改 singleton 连续性定义 |
| RA-7/EX-5 | 三个目标 arena 名在 src/python 0 文件，HeapRing 16 文件；Worker.alloc_shared_tensor:11886 仍由 _orch._o.alloc 供给 |
| RA-8/RA-12 | worker.py 的 local_window_bases 2 处（字段/赋值）；detach_persistent_domain 0 处、retain_after_run 3 处；#2120 同 head OPEN |
| RA-9/RA-10 | 现有 Region/backing 仍不等于 ResourceBundle 的组合与用户可观测 plan/completion；本轮增量集中 ChipWorker consumer/launch/DFX，不新增该组合 API |
| RA-11 | #2096 三个核心文件与旧 head 相同，UT +33/−20 是阳性对照；新 head 对 main merge-tree 成功，双 arch 具名 CI SPSC PASS；每次仍物化新 Region |
| RA-13/RA-14 | task_interface.py:1759 起接受 TaskArgs；snapshot/preflight/import/native run，异常保留 imports；#2486/2489 已合入，standalone DEVICE source 接入不在范围 |
| RA-15/RA-16 | Buffer.close 的 source-registration 与普通 host-backed 分支有别，不能整体称统一 logical close；workspace C-init 统一安装已由 #2473 合入；#2487 的 launch image 不等于用户 Buffer/domain owner 归一 |
| DS-1/DS-7/DS-9 | remote_l3_worker.py:163 起明确 host_tcp 无 ACK/lease；GLOBAL_DOMAIN_MAX_RANKS=64；GlobalDomainPhase 已有 PREPARE_EXPORT/IMPORT/COMMIT/ABORT |
| DS-2/DS-4/DS-5/DS-6/DS-8/DS-10/DS-11 | 本轮 main 增量未改 session/domain/endpoint 生产契约；#2026/#1888/#2059 等仍 OPEN，backend 原型与 production lease/freeze/heterogeneous 退出条件分开 |
| EX-1/EX-2/EX-4 | worker_manager.cpp:1198–1219 中 NEXT_LEVEL 传 task_frame_count，SUB 使用默认；长期任务/control/STOP 和 callable 身份范围仍独立 |
| EX-3/EX-6 | #2476 的 retention 证据保留；_teardown_worker_tree 全集 journal 与 transport 屏障保持，不能以 U4 新目标重开 P0.2-f4..f7 |
| EX-7 | A5 TMR joined_native_launch_supported_impl=1，prebuilt cache hit 才可复用；失败状态只读本 run terminal record；#2471 只新增 29 行 native UT 与 1 行 ST 注释，真实 Qwen clone adapter 尚未由其交付 |
| 外部 Qwen / DFX | Qwen README:182 起明确 requested 2 / effective 1；#2490 host graph publication 以 host capture 完成为证，不要求 device success；TMR device record 仍有独立静止义务 |

## 4. 全文档归属清单

每个活跃 Markdown/HTML 载体均列入 manifest；新增文件可达而无归属会使 single-owner 失败。此机械清单证明载体有归属，不替代对正文目标的语义复核。

| 文档 | 职责归属 |
| ---- | -------- |
| P0.2/c-design.md（本地参考：`P0.2/c-design.md`） | P0 已交付证据；EX-6 终态回收 |
| P0.2/done-followup.md（本地参考：`P0.2/done-followup.md`） | P0 已交付证据；EX-6 终态回收 |
| P0.2/done.md（本地参考：`P0.2/done.md`） | P0 已交付证据；EX-6 终态回收 |
| [README.md](README.md) | 入口、约束、归属/里程碑或历史证据；不创建独立实现目标 |
| [cpu-npu-shared-memory.md](cpu-npu-shared-memory.md) | RA-2…RA-12；DS-8/DS-9/DS-10；EX-2 |
| [cpu-npu-shared-memory/domain-member-vs-attachment.md](cpu-npu-shared-memory/domain-member-vs-attachment.md) | RA-2…RA-12；DS-8/DS-9/DS-10；EX-2 |
| [cpu-npu-shared-memory/domain-membership.md](cpu-npu-shared-memory/domain-membership.md) | RA-2…RA-12；DS-8/DS-9/DS-10；EX-2 |
| [cpu-npu-shared-memory/hardware-constraints.md](cpu-npu-shared-memory/hardware-constraints.md) | RA-2…RA-12；DS-8/DS-9/DS-10；EX-2 |
| [cpu-npu-shared-memory/w2-w5-inversion.md](cpu-npu-shared-memory/w2-w5-inversion.md) | RA-2…RA-12；DS-8/DS-9/DS-10；EX-2 |
| [cpu-npu-shared-memory/work-breakdown.md](cpu-npu-shared-memory/work-breakdown.md) | RA-2…RA-12；DS-8/DS-9/DS-10；EX-2 |
| deleted-closed-branches.md（本地参考：`deleted-closed-branches.md`） | 入口、约束、归属/里程碑或历史证据；不创建独立实现目标 |
| dfx/c2a-hierarchical-trace.md（本地参考：`dfx/c2a-hierarchical-trace.md`） | DFX 已交付证据 / 外部 follow-up；T7 跨 host 不归三线 |
| dfx/host-swimlane-delivered.md（本地参考：`dfx/host-swimlane-delivered.md`） | DFX 已交付证据 / 外部 follow-up；T7 跨 host 不归三线 |
| dfx/host-swimlane.md（本地参考：`dfx/host-swimlane.md`） | DFX 已交付证据 / 外部 follow-up；T7 跨 host 不归三线 |
| dfx/l3-l2-thread-topology.html（本地参考：`dfx/l3-l2-thread-topology.html`） | DFX 已交付证据 / 外部 follow-up；T7 跨 host 不归三线 |
| docs-kernel-mode/kernel-mode-memory.html（本地参考：`docs-kernel-mode/kernel-mode-memory.html`） | 范围排除：kernel / HBG 硬件；仅保留约束证据 |
| docs-kernel-mode/kernel-mode-plan.html（本地参考：`docs-kernel-mode/kernel-mode-plan.html`） | 范围排除：kernel / HBG 硬件；仅保留约束证据 |
| done-P0.1.md（本地参考：`done-P0.1.md`） | P0 已交付证据；EX-6 终态回收 |
| [feature_roadmap.md](feature_roadmap.md) | 入口、约束、归属/里程碑或历史证据；不创建独立实现目标 |
| [goal-coverage.md](goal-coverage.md) | 目标来源、范围与证据清单；不另维护实现状态 |
| hbg-requirements.html（本地参考：`hbg-requirements.html`） | 范围排除：kernel / HBG 硬件；仅保留约束证据 |
| [l3l4.md](l3l4.md) | 入口、约束、归属/里程碑或历史证据；不创建独立实现目标 |
| [l3l4/cluster-comm.md](l3l4/cluster-comm.md) | 场景验收：RA-13/RA-14、DS-5…DS-11、EX-1…EX-5 |
| [l3l4/inventory.md](l3l4/inventory.md) | 场景验收：RA-13/RA-14、DS-5…DS-11、EX-1…EX-5 |
| [l3l4/lazy-init.md](l3l4/lazy-init.md) | 场景验收：RA-13/RA-14、DS-5…DS-11、EX-1…EX-5 |
| [l3l4/memory-kinds.md](l3l4/memory-kinds.md) | 场景验收：RA-13/RA-14、DS-5…DS-11、EX-1…EX-5 |
| [l3l4/remote-startup.md](l3l4/remote-startup.md) | 场景验收：RA-13/RA-14、DS-5…DS-11、EX-1…EX-5 |
| [l3l4/scheduling.md](l3l4/scheduling.md) | 场景验收：RA-13/RA-14、DS-5…DS-11、EX-1…EX-5 |
| [logical-physical-separation.md](logical-physical-separation.md) | 入口、约束、归属/里程碑或历史证据；不创建独立实现目标 |
| maintainer-loop-prompt.md（本地参考：`maintainer-loop-prompt.md`） | 入口、约束、归属/里程碑或历史证据；不创建独立实现目标 |
| others-ref/host-trace-log-design/README.md（本地参考：`others-ref/host-trace-log-design/README.md`） | DFX 已交付证据 / 外部 follow-up；T7 跨 host 不归三线 |
| others-ref/host-trace-log-design/host-trace-span-naming.md（本地参考：`others-ref/host-trace-log-design/host-trace-span-naming.md`） | DFX 已交付证据 / 外部 follow-up；T7 跨 host 不归三线 |
| others-ref/host-trace-log-design/log-design.md（本地参考：`others-ref/host-trace-log-design/log-design.md`） | DFX 已交付证据 / 外部 follow-up；T7 跨 host 不归三线 |
| others-ref/host-trace-log-design/trace-and-log-requirements.md（本地参考：`others-ref/host-trace-log-design/trace-and-log-requirements.md`） | DFX 已交付证据 / 外部 follow-up；T7 跨 host 不归三线 |
| others-ref/memory-model.md（本地参考：`others-ref/memory-model.md`） | 入口、约束、归属/里程碑或历史证据；不创建独立实现目标 |
| others-ref/worker-async-pipeline-landing.md（本地参考：`others-ref/worker-async-pipeline-landing.md`） | 已交付流水线编号；执行契约由 EX-1/EX-7 承接 |
| others-ref/worker-async-pipeline-through-q1-summary-and-remaining-three-prs.md（本地参考：`others-ref/worker-async-pipeline-through-q1-summary-and-remaining-three-prs.md`） | Qwen 集成轨；EX-7 消费程序侧要求 |
| others-ref/worker-async-pipeline/contracts.md（本地参考：`others-ref/worker-async-pipeline/contracts.md`） | 已交付流水线编号；执行契约由 EX-1/EX-7 承接 |
| others-ref/worker-async-pipeline/implementation-plan.md（本地参考：`others-ref/worker-async-pipeline/implementation-plan.md`） | 已交付流水线编号；执行契约由 EX-1/EX-7 承接 |
| others-ref/worker-async-pipeline/implementation-record.md（本地参考：`others-ref/worker-async-pipeline/implementation-record.md`） | 已交付流水线编号；执行契约由 EX-1/EX-7 承接 |
| others-ref/worker-async-pipeline/qwen-integration.md（本地参考：`others-ref/worker-async-pipeline/qwen-integration.md`） | Qwen 集成轨；EX-7 消费程序侧要求 |
| others-ref/worker-async-pipeline/trace-v1.md（本地参考：`others-ref/worker-async-pipeline/trace-v1.md`） | 已交付流水线编号；执行契约由 EX-1/EX-7 承接 |
| review/2026-08-11-code-vs-design-audit.md（本地参考：`review/2026-08-11-code-vs-design-audit.md`） | AR/AF 审计：work-items 吸收列 |
| [review/2026-09-28-cross-plan-lessons.md](review/2026-09-28-cross-plan-lessons.md) | 只读来源评估；现行约束见访问与退休契约、用户面及分线 C/D |
| review/p0.2-design.md（本地参考：`review/p0.2-design.md`） | P0 已交付证据；EX-6 终态回收 |
| [review/pr-2096.md](review/pr-2096.md) | RA-11 的具名评审与 main 集成门；实施状态见工作项 |
| review/pr-2474.md（本地参考：`review/pr-2474.md`） | RA-13/RA-15 的具名历史评审；不单设工作项 |
| review/pr-2475.md（本地参考：`review/pr-2475.md`） | RA-13 consumer 的具名历史评审及 P1 处置；API 剩余见工作项 |
| [roadmap.html](roadmap.html) | 入口、约束、归属/里程碑或历史证据；不创建独立实现目标 |
| runtime-v2-hbg.md（本地参考：`runtime-v2-hbg.md`） | 外部 HBG 实现；GE-R1–R6 交接约束保留 |
| runtime-v2-hbg/graph-execution.md（本地参考：`runtime-v2-hbg/graph-execution.md`） | 外部 HBG 实现；GE-R1–R6 交接约束保留 |
| tasks/README.md（本地参考：`tasks/README.md`） | 入口、约束、归属/里程碑或历史证据；不创建独立实现目标 |
| tasks/g2-design.md（本地参考：`tasks/g2-design.md`） | RA 资源/访问/ABI；GE 交接约束按 §6 |
| tasks/task-device-addr-guard2.md（本地参考：`tasks/task-device-addr-guard2.md`） | RA 资源/访问/ABI；GE 交接约束按 §6 |
| tasks/task-p0.2a-admission-fence.md（本地参考：`tasks/task-p0.2a-admission-fence.md`） | P0 已交付证据；EX-6 终态回收 |
| tasks/task-p0.2b1-fake-chip-harness.md（本地参考：`tasks/task-p0.2b1-fake-chip-harness.md`） | P0 已交付证据；EX-6 终态回收 |
| tasks/task-p0.2b2-eligibility-hook.md（本地参考：`tasks/task-p0.2b2-eligibility-hook.md`） | P0 已交付证据；EX-6 终态回收 |
| tasks/task-p0.2c-cancellation-journal.md（本地参考：`tasks/task-p0.2c-cancellation-journal.md`） | P0 已交付证据；EX-6 终态回收 |
| tasks/task-p0.2cd-teardown-convergence.md（本地参考：`tasks/task-p0.2cd-teardown-convergence.md`） | P0 已交付证据；EX-6 终态回收 |
| tasks/task-p0.3-daemon-send-isolation.md（本地参考：`tasks/task-p0.3-daemon-send-isolation.md`） | P0 已交付证据；EX-6 终态回收 |
| tasks/task-p0.3-deadline-hardening.md（本地参考：`tasks/task-p0.3-deadline-hardening.md`） | P0 已交付证据；EX-6 终态回收 |
| tasks/task-p0.3-zero-residual-regression.md（本地参考：`tasks/task-p0.3-zero-residual-regression.md`） | P0 已交付证据；EX-6 终态回收 |
| tasks/task-p1a-close-merge-gates.md（本地参考：`tasks/task-p1a-close-merge-gates.md`） | RA 资源/访问/ABI；GE 交接约束按 §6 |
| tasks/task-public-python-tracing-api.md（本地参考：`tasks/task-public-python-tracing-api.md`） | DFX 已交付证据 |
| tasks/task-scenetest-singleton-alias.md（本地参考：`tasks/task-scenetest-singleton-alias.md`） | ST-1 → RA-13；ST-2 → RA-6 |
| [track-boundaries.md](track-boundaries.md) | 入口、约束、归属/里程碑或历史证据；不创建独立实现目标 |
| [w2-p1b-endpoint-convergence.md](w2-p1b-endpoint-convergence.md) | RA 资源/访问/ABI；GE 交接约束按 §6 |
| [work-items.md](work-items.md) | 入口、约束、归属/里程碑或历史证据；不创建独立实现目标 |
| [worker-memory-model.md](worker-memory-model.md) | RA 资源/访问/ABI；GE 交接约束按 §6 |
| [worker-memory-model/access-retirement-contract.md](worker-memory-model/access-retirement-contract.md) | 无编号的接纳/完成/退休/预算/capture/六组联验 → work-items §6 的 RA-3/RA-13/RA-15/RA-16、DS-3/DS-4、EX-7；kernel/DFX 实现仍范围外 |
| [worker-memory-model/backend-matrix.md](worker-memory-model/backend-matrix.md) | RA 资源/访问/ABI；GE 交接约束按 §6 |
| [worker-memory-model/bufferhandle-abi.md](worker-memory-model/bufferhandle-abi.md) | RA 资源/访问/ABI；GE 交接约束按 §6 |
| [worker-memory-model/graph-execution-contract.md](worker-memory-model/graph-execution-contract.md) | RA 资源/访问/ABI；GE 交接约束按 §6 |
| [worker-memory-model/p1b-corrected-design.md](worker-memory-model/p1b-corrected-design.md) | RA 资源/访问/ABI；GE 交接约束按 §6 |
| [worker-memory-model/p1b-review-findings.md](worker-memory-model/p1b-review-findings.md) | RA 资源/访问/ABI；GE 交接约束按 §6 |
| [worker-memory-model/user-surface.md](worker-memory-model/user-surface.md) | RA 资源/访问/ABI；GE 交接约束按 §6 |

## 5. 本轮验证口径

11 条 check-*.py 检查目标归属、状态、登记、依赖、撤回与渲染结构。非空 main 区间 `c4ffb02f2..01bf24bfa` 含 18 条 PR，最近定点 `8aae172e5..01bf24bfa` 含 10 条；#2447 提交标题没有 PR 后缀，mergeCommit.oid 与祖先链仍能识别。每个 OPEN PR 的 head 与目标分支、68 个 OPEN issue 均有登记。

\#2096 最新 head `2297a9a4f29a` 的两架构 SPSC 场景有 PASS 日志；旧三处冲突已不存在。#2429 的“#2471 待合并”是过期叙述；其合入内容只有程序身份/lease 测试增量，不代表真实 Qwen clone 端到端完成。11 条守卫全部通过。正对照：非空区间识别 18 条 main PR，包含无编号标题的 #2447；负对照：移除 #2447/#2489 登记、新增 A5SVM-A6 的归属或 #2447 merge 元数据分别触发缺登记、缺归属或不能判定。浏览器在 1500/390 宽度均渲染 65 阶段与 65 表格行，无脚本错误或页面横向溢出；已目检桌面、窄屏与 RA-11 新 head 提示。核验期间新增 #2499、#2096 再评审时另补登记 DFX 文档 PR #2500，27 个 main OPEN PR 与 68 个 OPEN issue 全部登记；#2499 是范围外诊断 PR，正文明确不可合并。最终 fetch 基线仍为本报告 SHA。

## 6. 上一全扫描之后的 main 合入清单

| PR | main merge SHA | 归属 |
| -- | -------------- | ---- |
| #2465 | `02fd1f63e061f3a108807609ef4f216c621aa014` | 外部 DFX |
| #2480 | `4d0610fb748bd5e41a338c653bb470ca692e79de` | RA-14 |
| #2473 | `35e9331b58d999f08c831acbc9468954cd91ab40` | RA-16 |
| #2481 | `5eb913c39b34ae53990784749c86d67a196c5ef4` | EX-7 |
| #2482 | `fcbf4f1d17090fa59a6f4a5b1dcba65f90fa012c` | RA-13 |
| #2483 | `92a14e4e7371bdd087e60d5893889e7bb127149f` | 外部 DFX |
| #2472 | `3fa8b540c9899bb012725fbf2cc53510a2912949` | 外部 HBG runtime |
| #2484 | `8aae172e556b94160cb7b7df54eb4e3f454db555` | 外部 DFX |
| #2447 | `9b283ab1e45d69c354adb9ed5b0610aea28dd6e9` | 外部 Qwen / Step 2 |
| #2486 | `22c009811e439aaa628b65abc25c6e963eee7956` | RA-13 consumer / RA-14 |
| #2485 | `02f1b1f6c223bc89302565a133774354634e5d3f` | 外部 CI / #1425 |
| #2488 | `d2cc4a58dd060b026de942961895f4fdacd6b683` | 外部依赖/CI |
| #2456 | `b57629cf9f81f7e7e595a4b5eb95fa195075137b` | 外部 Qwen / safe fallback |
| #2487 | `ef38a0ed8a0d975f8feb46d1fddde5d41db40550` | 外部 HBG runtime |
| #2491 | `67bce6291b1924775530dcfc25d3c52f1c5c14ec` | EX-7 |
| #2489 | `833327f60ea8f4772bf1673ea8edb349314a350d` | RA-13 / RA-3 访问约束 |
| #2471 | `8e4dac671bae96ec99e7f8bbcac48ee160d7ac36` | EX-7 |
| #2490 | `01bf24bfa8a997c89210258c5954c4105843d783` | 外部 DFX |
