# 目标来源与覆盖口径

> **发布快照：2026-10-26。** 设计、实现状态与历史调查须按正文各自基线阅读；不代表当前 main 全量验收。参见[阅读入口与已知更正](README.md)。
>
> **2026-10-26 范围补充**：#2716 的 16 节点原生树、每条正文 checkbox 与具名提案编号已纳入 goal-sources 和 work-items §6；[分析与证据](review/track-2716.md)。这是定点来源扩展，不改写下面 10 月 18 日的全目标审计日期与历史计数。
>
> **核验基线：`upstream/main@937c57efc71ad01a01c3a72072df075d2a5dd24f`（2026-10-18）。** 34 个实施项（16 RA / 11 DS / 7 EX），364 个来源编号，77 个文档载体。28 个 main OPEN/DRAFT PR、60 个 OPEN issue；相对 `01bf24bfa` 的 79 条 main 合入已逐 merge SHA 核祖先链。全部编号有实施、约束、历史或范围外归属；这不是全部目标已完成的声明。

本轮读 main 代码、合入 diff、GitHub 正文和设计来源；没有运行实现测试或硬件。既有 CI/硬件记录只在其具名测试 SHA 下有效。SSH fetch 认证失败后使用 HTTPS/HTTP1.1 成功读取同一 upstream，未切换共享分支。

## 1. 本轮判断与剩余工作

- U2a 显式 HOST 消费已由 #2501 交付；仍未完成的 mapping/跨进程能力继续归 RA-2/RA-3。
- 统一 chip materialization 已由 #2597 交付；统一 Buffer 的全部 IO/远端/close 契约仍分属 RA-13、DS-3、RA-15。
- #2571/#2596 提供 grant 与自动准入；真实 Qwen 的 clone/DEVICE/持续请求已有 #2511/#2568/#2592。自动模式与真实模型组合、任意动态到达及性能证明继续归 EX-7/RA-16，不用旧“三套资源上限”描述当前通用能力。
- #2594 的七项验收归 DS-6 及其 owner/Buffer/close 交接；#2598 只提供三种 session cleanup 结果观测，不关闭 DS-1/DS-4。
- `simpler-pr-list.html` 的 42 项和本地别名纳入来源全集；ABORT poison、RDMA 空 provider 两条旧判断已证伪。候选页保留历史排期标记，当前状态只查工作项。
- #2450 CLOSED/COMPLETED 不等于 KV alias 已实现：main 仍拒绝同任务重叠写。KV-R1–R5 保留在 RA-6。#2425 历史故障由关闭评论列出的 #2426/#2435/#2476 收口；#2559 是独立新现象，不能假设同因。
- kernel 范围排除与契约 D 的 registry 组织仍待用户裁决；保持既定边界，不因此阻止 consumer 保护或本地 owner 工作。

## 2. 来源与编号全集

每组来源均非空提取。新增编号是带命名空间的来源索引，避免与 P1/P2、W、G 或外部规划同名；唯一归属见 work-items 吸收列及 §5–§7。

 | 来源组 | 具名来源 | 编号全集 |
 | ------ | -------- | -------- |
 | P1/P2 stages | [feature_roadmap.md](feature_roadmap.md) | `P1-A` · `P1-B` · `P1-C` · `P1-D` · `P1-E` · `P1-F` · `P2-A` · `P2-B` · `P2-C` · `P2-D` · `P2-E` |
 | W resource capability | [cpu-npu-shared-memory/work-breakdown.md](cpu-npu-shared-memory/work-breakdown.md) | `W0` · `W1` · `W1lite` · `W2` · `W3` · `W4` · `W5` · `W6` · `W7` · `W8` |
 | L3/L4 scenarios | [l3l4.md](l3l4.md) | `A.1` · `B` · `C.1` · `C.2` · `C.3` · `D.1` · `D.2` · `D.3` · `D.4` · `D.5` · `E` |
 | Audit AR/AF | review/2026-08-11-code-vs-design-audit.md（本地参考：`review/2026-08-11-code-vs-design-audit.md`） | `AF1` · `AF2` · `AF3` · `AF4` · `AF5` · `AR1` · `AR10` · `AR2` · `AR3` · `AR4` · `AR5` · `AR6` · `AR7` · `AR8` · `AR9` |
 | P1 gates | tasks/task-p1a-close-merge-gates.md（本地参考：`tasks/task-p1a-close-merge-gates.md`） | `G1` · `G2` · `G3` · `G4` · `G5` · `G6` |
 | ST characterization | tasks/task-scenetest-singleton-alias.md（本地参考：`tasks/task-scenetest-singleton-alias.md`） | `ST-1` · `ST-2` |
 | Graph boundary requirements | [worker-memory-model/graph-execution-contract.md](worker-memory-model/graph-execution-contract.md) | `GE-R1` · `GE-R2` · `GE-R3` · `GE-R4` · `GE-R5` · `GE-R6` |
 | DFX T1-T9 | others-ref/worker-async-pipeline/trace-v1.md（本地参考：`others-ref/worker-async-pipeline/trace-v1.md`）、dfx/c2a-hierarchical-trace.md（本地参考：`dfx/c2a-hierarchical-trace.md`） | `T1` · `T2` · `T3` · `T4` · `T5` · `T6` · `T7` · `T8` · `T9` |
 | Qwen Q1-Q4 | others-ref/worker-async-pipeline/qwen-integration.md（本地参考：`others-ref/worker-async-pipeline/qwen-integration.md`） | `Q1` · `Q2` · `Q3` · `Q4` |
 | Pipeline delivered / planned chapters | others-ref/worker-async-pipeline/implementation-record.md（本地参考：`others-ref/worker-async-pipeline/implementation-record.md`）、others-ref/worker-async-pipeline/implementation-plan.md（本地参考：`others-ref/worker-async-pipeline/implementation-plan.md`） | `PIPE-A0` · `PIPE-A0b` · `PIPE-A1` · `PIPE-B1` · `PIPE-B2` · `PIPE-B3` · `PIPE-B3a` · `PIPE-B3b` · `PIPE-B4` · `PIPE-B5` · `PIPE-B6` · `PIPE-B6a` · `PIPE-B6b` · `PIPE-B6c` · `PIPE-D1` · `PIPE-E1` · `PIPE-E2` · `PIPE-H1` · `PIPE-H2` · `PIPE-N1` · `PIPE-W1a` · `PIPE-W1b` · `PIPE-W1c` |
 | Kernel principles | docs-kernel-mode/kernel-mode-plan.html（本地参考：`docs-kernel-mode/kernel-mode-plan.html`） | `KERNEL-Principle-01` · `KERNEL-Principle-02` · `KERNEL-Principle-03` · `KERNEL-Principle-04` · `KERNEL-Principle-05` · `KERNEL-Principle-06` · `KERNEL-Principle-07` · `KERNEL-Principle-08` · `KERNEL-Principle-09` · `KERNEL-Principle-10` · `KERNEL-Principle-11` |
 | Pipeline board extension | [README.md](README.md) | `PIPE-C2a` · `PIPE-E1′` · `PIPE-W1d` |
 | P1 ABI extra gates | tasks/task-p1a-close-merge-gates.md（本地参考：`tasks/task-p1a-close-merge-gates.md`） | `P1A-A1` · `P1A-A2` |
 | Historical pipeline W aliases | others-ref/worker-async-pipeline-through-q1-summary-and-remaining-three-prs.md（本地参考：`others-ref/worker-async-pipeline-through-q1-summary-and-remaining-three-prs.md`） | `PIPE-v1-W1` · `PIPE-v1-W2` · `PIPE-v1-W3` · `PIPE-v1-W4` |
 | Trace/log source requirements | others-ref/host-trace-log-design/trace-and-log-requirements.md（本地参考：`others-ref/host-trace-log-design/trace-and-log-requirements.md`） | `LOG-C2` · `LOG-C4` · `LOG-C5` · `LOG-C6` · `LOG-C7` · `LOG-D5` · `LOG-F1` · `LOG-F2` · `LOG-F3` · `LOG-F4` · `LOG-F5` · `LOG-F6` · `LOG-F7` · `LOG-G1` · `LOG-G10` · `LOG-G11` · `LOG-G11b` · `LOG-G12` · `LOG-G13` · `LOG-G14` · `LOG-G15` · `LOG-G16` · `LOG-G17` · `LOG-G18` · `LOG-G19` · `LOG-G2` · `LOG-G20` · `LOG-G21` · `LOG-G22` · `LOG-G23` · `LOG-G3` · `LOG-G4a` · `LOG-G4b` · `LOG-G6` · `LOG-G6b` · `LOG-G7` · `LOG-G8` · `LOG-G9` · `LOG-N1` · `LOG-N2` · `LOG-N3` · `LOG-N4` · `LOG-T1` · `LOG-T2` · `LOG-T3` · `LOG-T4` · `LOG-T5` |
 | Kernel memory proposals | docs-kernel-mode/kernel-mode-memory.html（本地参考：`docs-kernel-mode/kernel-mode-memory.html`） | `KERNEL-Memory-D1` · `KERNEL-Memory-D2` · `KERNEL-Memory-D3` · `KERNEL-Memory-D4` · `KERNEL-Memory-D5` · `KERNEL-Memory-D6` |
 | Candidate PR plan | simpler-pr-list.html（本地参考：`simpler-pr-list.html`） | `SPL-1.1` · `SPL-1.2` · `SPL-1.3` · `SPL-1.4` · `SPL-1.5` · `SPL-1.6` · `SPL-1.7` · `SPL-1.8` · `SPL-1.9` · `SPL-2.1` · `SPL-2.2` · `SPL-3.1` · `SPL-3.10` · `SPL-3.2` · `SPL-3.3` · `SPL-3.4` · `SPL-3.5` · `SPL-3.6` · `SPL-3.7` · `SPL-3.8` · `SPL-3.9` · `SPL-4.1` · `SPL-4.2` · `SPL-4.3` · `SPL-4.4` · `SPL-4.5` · `SPL-4.6` · `SPL-5.1` · `SPL-5.2` · `SPL-5.3` · `SPL-5.4` · `SPL-5.5` · `SPL-5.6` · `SPL-5.7` · `SPL-5.8` · `SPL-L.1` · `SPL-L.2` · `SPL-L.3` · `SPL-L.4` · `SPL-P.1` · `SPL-P.2` · `SPL-P.3` |
 | 2460 | [来源](https://github.com/hw-native-sys/simpler/issues/2460) · 正文 SHA256 `20f03b873343` | `U1a` · `U1b` · `U2a` · `U2b` · `U3` · `U4` |
 | 2429 | [来源](https://github.com/hw-native-sys/simpler/issues/2429) · 正文 SHA256 `c272c6ebbbc9` | `VLLM-1` · `VLLM-2` · `VLLM-3` · `VLLM-4` · `VLLM-5` · `VLLM-6` · `VLLM-7` · `VLLM-8` · `VLLM-9` · `PR-S3-1` · `TrackA-P6` |
 | 2450 | [来源](https://github.com/hw-native-sys/simpler/issues/2450) · 正文 SHA256 `196447309114` | `KV-R1` · `KV-R2` · `KV-R3` · `KV-R4` · `KV-R5` |
 | 2316 | [来源](https://github.com/hw-native-sys/simpler/issues/2316) · 正文 SHA256 `1363133e1088` | `TrackA-P1` · `TrackA-P2` · `TrackA-P3` · `TrackA-P4` · `TrackA-P5` |
 | 2429-step3 | [来源](https://github.com/hw-native-sys/simpler/issues/2429#issuecomment-5862270515) · 正文 SHA256 `f74554b81b5e` | `VLLM-S3-1` · `VLLM-S3-2` · `VLLM-S3-3` · `VLLM-S3-4` · `VLLM-S3-5` · `VLLM-S3-6` · `VLLM-S3-7` |
 | 2425 | [来源](https://github.com/hw-native-sys/simpler/issues/2425) · 正文 SHA256 `9de305a340f6` | `EE-QA-1` · `EE-QA-2` · `EE-QA-3` · `EE-QA-4` · `EE-QA-5` |
 | pr2466 | [来源](https://github.com/hw-native-sys/simpler/pull/2466) · 正文 SHA256 `76b7022839f4` | `CAP-R1` · `CAP-R2` · `CAP-R3` |
 | 1792 | [来源](https://github.com/hw-native-sys/simpler/issues/1792) · 正文 SHA256 `f48d50e6e8ac` | `LOG-I1` · `LOG-I2` · `LOG-I3` · `LOG-I4` · `LOG-I5` · `LOG-I6` · `LOG-I7` |
 | 2496 | [来源](https://github.com/hw-native-sys/simpler/issues/2496) · 正文 SHA256 `36baae0d0d1a` | `A5SVM-A1` · `A5SVM-A2` · `A5SVM-A3` · `A5SVM-A4` · `A5SVM-A5` · `A5SVM-A6` |
 | 2497 | [来源](https://github.com/hw-native-sys/simpler/issues/2497) · 正文 SHA256 `fdee7dc54897` | `XPSVM-A1` · `XPSVM-A2` · `XPSVM-A3` · `XPSVM-A4` · `XPSVM-A5` · `XPSVM-A6` · `XPSVM-A7` · `XPSVM-A8` |
 | 2498 | [来源](https://github.com/hw-native-sys/simpler/issues/2498) · 正文 SHA256 `198cb83f2594` | `SIMT-A1` · `SIMT-A2` · `SIMT-A3` · `SIMT-A4` · `SIMT-A5` |
 | 2492 | [来源](https://github.com/hw-native-sys/simpler/issues/2492) · 正文 SHA256 `dcfda1b4e9bc` | `A2RET-R1` · `A2RET-R2` · `A2RET-R3` · `A2RET-R4` |
 | 2594 | [来源](https://github.com/hw-native-sys/simpler/issues/2594) · 正文 SHA256 `2d5a670bbb99` | `DOMAIN-A1` · `DOMAIN-A2` · `DOMAIN-A3` · `DOMAIN-A4` · `DOMAIN-A5` · `DOMAIN-A6` · `DOMAIN-A7` |
 | 2429-final | [来源](https://github.com/hw-native-sys/simpler/issues/2429#issuecomment-6186920862) · 正文 SHA256 `e12011641273` | `VLLM-S3F-S1` · `VLLM-S3F-S2` · `VLLM-S3F-S3` · `VLLM-S3F-S4` · `VLLM-S3F-S5` · `VLLM-S3F-A1` · `VLLM-S3F-A2` · `VLLM-S3F-A3` · `VLLM-S3F-A4` · `VLLM-S3F-A5` · `VLLM-S3F-A6` |
 | pr2598 | [来源](https://github.com/hw-native-sys/simpler/pull/2598) · 正文 SHA256 `c38efc5b115c` | `SESSION-R1` · `SESSION-R2` · `SESSION-R3` |
 | P3 delivery | 具名集合见 goal-sources.json | `P3①` · `P3②` · `P3③` · `P3④` · `P3⑤` · `P3⑥` |
 | P0 milestones | 具名集合见 goal-sources.json | `P0.1` · `P0.2` · `P0.3` |
 | P0 followup | 具名集合见 goal-sources.json | `P0.2-f1` · `P0.2-f2` · `P0.2-f3` · `P0.2-f4` · `P0.2-f5` · `P0.2-f6` · `P0.2-f7` |
 | P0.2 batches | 具名集合见 goal-sources.json | `P0.2-a` · `P0.2-b1` · `P0.2-b2` · `P0.2-c` · `P0.2-d` |
 | P0.2 teardown subunits | 具名集合见 goal-sources.json | `P0.2-D0` · `P0.2-D1` · `P0.2-D2` · `P0.2-D3` |
 | P0.3 acceptance | 具名集合见 goal-sources.json | `P0.3-R1` · `P0.3-R2` · `P0.3-R3` · `P0.3-R4` |
 | Qwen integration aliases | 具名集合见 goal-sources.json | `C2b` · `V1` |
 | Kernel stack | 具名集合见 goal-sources.json | `KERNEL-H1` · `KERNEL-H2` · `KERNEL-H3` · `KERNEL-K2` · `KERNEL-K3` · `KERNEL-K4` · `KERNEL-K5` · `KERNEL-K7` · `KERNEL-2a` · `KERNEL-2B` · `KERNEL-10a` · `KERNEL-Binder` |
 | Explicit exclusions | 具名集合见 goal-sources.json | `PIPE-C1` · `RequestSession` |
 | Candidate local aliases | 具名集合见 goal-sources.json | `SPL-C4` · `SPL-C5` · `SPL-C6` · `SPL-C7` · `SPL-C8` · `SPL-C9` · `SPL-C10` · `SPL-C11` · `SPL-C12` · `SPL-C13` · `SPL-C14` · `SPL-M2` · `SPL-E1` · `SPL-E2` |

## 3. 当前 main 的可复核代码证据

下列路径均以本页 SHA 的 `git show` 为准。源码存在、测试定义存在和执行通过是不同证据层。

 | 承载项 | 路径 / 检查点 | 结论边界 |
 | ------ | ------------- | -------- |
 | RA-1 | `src/a2a3/platform/onboard/host/device_runner.cpp::register_device_memory_to_host` | DEV_SVM_MAP_HOST 仍传 device_id_；符号 acl_to_hal_device_id 0、halHostRegister 16 为阳性对照 |
 | RA-2/RA-3 | `src/common/host_build_graph/host/host_tensor_access.cpp；python/simpler/comm_endpoints.py` | HOST/NONE 显式访问已在；旧 acquire_child_memory_host_view 在 accessor 0，HOST/NONE 条件各 1；HOST_SVM_MAP 注释仍明确无 producer |
 | RA-4/RA-11 | `python/simpler/worker.py::_chip_child_allocate_domain；src/common/platform/include/common/region_template.h 与 SPSC 入口` | window offset 顺序累加，未按 cache line padding；模板存在不等于通用 Buffer slice 绑定完成 |
 | RA-5/DS-3 | `src/common/task_interface/task_args_wire.h；python/simpler/remote_l3_protocol.py` | 本地 TaskArgs transfer 已有；Remote PROTOCOL_VERSION=4，仍不能表达全部新 entry request |
 | RA-6 | `src/common/hierarchical/orchestrator.cpp；src/common/task_interface/task_args_wire.h` | lookup_overlapping/tensor_footprint 基座保留；同任务重叠写拒绝分支仍在 |
 | RA-7/EX-5 | `python/simpler/worker.py::alloc_shared_tensor` | self._orch._o.alloc 仍分配 FORK_SHM，scope heap 使用者未迁完；三种候选 arena 名 0，alloc_shared_tensor 2/实际 alloc 1 为对照 |
 | RA-8/RA-9/RA-10/DS-8 | `python/simpler/comm_endpoints.py；python/simpler/global_comm_domain.py；worker.py::_global_domain_attachment_matrix` | 共同 capability、typed attachment 与 copy 访问已有；ResourceBundle 多 backing/host 组合及用户可见 completion 要逐路径联验 |
 | RA-12/DS-6 | `python/simpler/task_interface.py::CommDomainHandle/GlobalCommDomainHandle；worker.py::_release_global_domain_handle` | 两类公开 handle 仍分开；detach_persistent_domain 0、retain_after_run 4；后续构建 run fence deferral 已有 |
 | RA-13/RA-14 | `python/simpler/task_interface.py::_materialize_chip_task_args；worker.py::_materialize_l2_args` | 三类 chip 入口使用同一转换，callable/source retention 仍归调用者；singleton stride 旧规则仍在 tensor.h |
 | RA-15 | `python/simpler/buffer.py::Buffer.close；src/common/worker/chip_run_lane.cpp；native_run_storage_tag.h` | host close 直接 unmap/unlink；verified prepare rollback 和 unstarted launch proof 已在，launch 异常 owner 后续 #2615 尚 OPEN |
 | RA-16/EX-3/EX-7 | `src/common/platform/include/host/auto_admission.h；workspace_manager.h；python/simpler/worker.py` | 四种 onboard program 的 auto admission、内部预算与源保留边界；caller allocation 不转移 ownership，自动模式排除 topology 必须保留 |
 | DS-1/DS-2/DS-4 | `worker.py::_release_remote_sessions/_close_mpirun_groups；python/simpler/mpi_l3_group.py` | main session_cleanup_reports 0、两种既有清理方法各 2；#2598 的新观测不在 main；lease/epoch/全员确认仍是退出条件 |
 | DS-5/DS-11 | `src/a5/platform/onboard/host/comm_hccl.cpp::init_rdma_workspace；examples/workers/l4/global_tload_mpirun_l3/main.py` | RDMA bootstrap/workspace 已有；MPI 验金仍为 max_diff > 1e-3；#2026 未合入，不从旧 CI 绿推导全平台验收 |
 | DS-7 | `python/simpler/global_comm_domain.py；src/common/platform_comm/comm_context.h` | 全域 rank 布局仍有 64 上限，规模扩展独立于小规模组合 |
 | DS-9/DS-10 | `worker.py::_allocate_global_domain` | 普通/MPI ABORT 失败都 record_unreclaimable，PREPARE/IMPORT/COMMIT/ABORT 已有；生产 lease 和异构组合不能由该事务单独证明 |
 | EX-1/EX-2 | `src/common/hierarchical/worker_manager.cpp::make_next_level_threads/make_sub_threads；worker.py::_run_mailbox_loop` | 请求帧与 run entries 分离；NEXT_LEVEL 传 granted_sets，SUB 默认；常驻 STOP/drain 与各拓扑长期 control 仍单独验收 |
 | EX-4 | `python/simpler/worker.py callable registry；tests/st 的 dynamic_register` | #1938 对不同 callable 输出的可失败观测仍 OPEN，改名/统一入口不代替身份验收 |
 | EX-6 | `worker.py::_teardown_worker_tree；tests/ut/py/test_cancellation_journal.py` | add_once(kind, identity, cleanup) 与 release-before-transport journal 仍在；新增 Buffer/domain/预算目标另归 RA-12/RA-15/RA-16 |
 | EX-7 / 外部 Qwen | `examples/a2a3/host_build_graph/qwen3_14b_serving_effective/continuous_worker_submit.py` | #2592 有固定形状/有界请求/独立存储/DEVICE feedback driver；auto admission 与 vLLM 实测不能自动继承 |

## 4. 文档载体归属

77 个 Markdown/HTML 载体均入清单；历史调查、范围外实现与当前设计分开，不把副本中的旧“未做”作为实施状态。无编号的身份/接纳/完成/关闭/预算/capture 契约由 work-items §6 明确承接。

 | 文档 | 归属与用途 |
 | ---- | ---------- |
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
 | simpler-pr-list.html（本地参考：`simpler-pr-list.html`） | 候选排期投影；SPL 编号按 work-items §6/§7 映射，源码证据与状态仅取工作项；本地 C/M/E 编号不得自成实施线 |
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

## 5. 非空登记与验证口径

11 条 check-*.py 覆盖章节、依赖、可达性、GitHub 状态、main 合入与 open 登记、投影状态、渲染结构、撤回、单归属、引用同步。merged 守卫保留 `01bf24bfa..937c57efc` 的 79 条非空区间，避免基线前移变成空检查。阴性案例必须配阳性：删实际新合入 PR 的全部登记、删新编号归属，守卫应失败；完整输入必须通过。本轮 11 条守卫全绿；移除 #2616 登记、移除 DOMAIN-A7 归属两个负对照均被守卫拒绝，完整输入通过。浏览器 1500/390 宽度均有 65 行、无脚本错误或横向溢出；DS-6 提示与 changelog 展开通过。仅核源码、现有 CI 证据与文档，本轮未重新执行硬件组合验收。

## 6. 本轮 main 合入清单

 | PR | main merge SHA | 归属 |
 | -- | -------------- | ---- |
 | #2616 | `937c57efc71ad01a01c3a72072df075d2a5dd24f` | 范围外 CI/测试/文档/打包 |
 | #2614 | `8b466791f374e1286b0936f8eaebffb9bf32d44b` | RA-13 / RA-15 |
 | #2613 | `2ee57567d01d0d86d5afb605f15e644a06a16200` | 范围外 CI/测试/文档/打包 |
 | #2612 | `49ae1ef1be8ab1e995ab12e1967721ef1421b86e` | RA-13 / RA-15 |
 | #2611 | `0eda81c931f982a4f8149b1cac4cda272ec3d8fb` | EX-7 |
 | #2610 | `aa91a9d60e0e7ce22cdae3cf70533ece46875546` | RA-16 |
 | #2609 | `0d05dfd545591964d2995cffe5154d6d349716cb` | 范围外 CI/测试/文档/打包 |
 | #2608 | `e6d546d4c6a98e1a87bc80cd0418806630c027f9` | 范围外 kernel/eager；共用执行寿命与 EX-7/RA-13 联验 |
 | #2596 | `a2b0ad2ade8a3cecb1517f2ee937626773f7aa85` | EX-7 |
 | #2606 | `17129ba496c6d7d710b0e1320b3d1c25aeb9d1e1` | 范围外 kernel/eager；共用执行寿命与 EX-7/RA-13 联验 |
 | #2604 | `4aedb1f4318f024f9d50392bb312673523841632` | 范围外 runtime/HBG 实现 |
 | #2601 | `289b3016463f60c159fb1255a581868daf04a104` | 范围外 kernel/eager；共用执行寿命与 EX-7/RA-13 联验 |
 | #2599 | `905d336e41d324ba8c8dbf87fe138965d84e74e2` | 范围外 kernel/eager；共用执行寿命与 EX-7/RA-13 联验 |
 | #2597 | `e994e106d8020c049dc419a67498db37bc6fcd3d` | RA-13 / RA-14 |
 | #2595 | `8c3b0e174cb41acb13bcb2d49baeb68b38ad82ba` | RA-13 / RA-14 |
 | #2592 | `1a8a3ee221968ec397588db070089354806c84c5` | EX-7；外部 Qwen 集成 |
 | #2593 | `3351950f4b0fdbd60523ea63266b321ab12f5f2e` | DS-6 / RA-12 |
 | #2576 | `cc6c4d51f79e98af5ef07829e3ca71c36f752a43` | 范围外 runtime/HBG 实现 |
 | #2575 | `e2e7405493417eaa78f0a03ee8afe25a8c947f63` | 范围外 CI/测试/文档/打包 |
 | #2566 | `e9e04fe3b0942e4652e2e4b3f5f6ced3d66a00ac` | 范围外 runtime/HBG 实现 |
 | #2589 | `a8c682a3217593d8ad7562c0a6ef1a03d61d6670` | RA-13 / RA-14 |
 | #2579 | `b74b5f27b4c1d161aab3998c5d9dd8489308e4c1` | 范围外 runtime/HBG 实现 |
 | #2587 | `11eb5d2c9ef8fe5a484a3d7c686d408ae5ff3e3b` | RA-13 / RA-15 |
 | #2586 | `b95785db12a9f9de4d86f78688291411c0f10b77` | 范围外 CI/测试/文档/打包 |
 | #2584 | `653a24c4a61fde1ef8c28838b67a4a6dd37a916b` | EX-7 |
 | #2571 | `78365f79ccd263e2e37cef827b69a0c4fa5f2eb5` | EX-7 / RA-16 |
 | #2540 | `c1f89ea431f5306f78c0e4ab9573c829756d7f1e` | 范围外 runtime/HBG 实现 |
 | #2582 | `7e19db4df679278d8bc4f9eea06c8e8fda4a492e` | EX-7 |
 | #2581 | `620807d2dd2dad83e8529788d7998a77b7c14e2d` | EX-7 |
 | #2577 | `7c7ec43d51ae1bdbeb955dd7ad13896fd5e0025f` | EX-7 |
 | #2580 | `f688613966341adcecb1f26c3da00254e97d977f` | EX-7 |
 | #2578 | `d793c89c32c438de6b411595a4485b2943aabf44` | 范围外 kernel/eager；共用执行寿命与 EX-7/RA-13 联验 |
 | #2572 | `946f49acbbd4df72b333784cfad60b1573e70b40` | 范围外 kernel/eager；共用执行寿命与 EX-7/RA-13 联验 |
 | #2568 | `127ec6e05765cdb4fc0b4e908b4136449e94048d` | EX-7；外部 Qwen 集成 |
 | #2567 | `852f6ff169f8b8a32e327c3c2ad0c105e1b5f9e3` | 范围外 kernel/eager；共用执行寿命与 EX-7/RA-13 联验 |
 | #2558 | `accab1cc54adff486c82f8367976ae8343c09ff0` | 范围外 CI/测试/文档/打包 |
 | #2543 | `d13a254ae473fadf2f45c689f15b3b15200e0e68` | 范围外 runtime/HBG 实现 |
 | #2564 | `c5cdd1b37a2e1907feeeceb82b4e70939c9c96d0` | 范围外 DFX/log |
 | #2562 | `e7cc287398593450cd18c3cb811996d2e14c1f40` | 范围外 CI/测试/文档/打包 |
 | #2563 | `c67b0eff6e6c7840d373f8c3db8146871ea91bae` | 范围外 DFX/log |
 | #2561 | `5f4f9525d3fd1163c9fed89661473b0734e271ae` | EX-7 |
 | #2560 | `a89c1170c87ae6c776a977bce2dcabea5fd74b7a` | EX-7 |
 | #2546 | `68d137fdfd435d8067f0efd9836cb428da3e700a` | 范围外 runtime/HBG 实现 |
 | #2541 | `dc73516d24c97ece6867a48775d9163ef7c593df` | 范围外 CI/测试/文档/打包 |
 | #2557 | `55eb522468fe702d564a131ee910ac1b5326858a` | EX-7 |
 | #2511 | `e39d35af8d1efcdc2faae90a7ccfc9743759cab3` | EX-7；外部 Qwen 集成 |
 | #2536 | `86e77362b6999b25ae8143a880b7d004407a2bca` | 范围外 DFX/log |
 | #2534 | `c2a58948190dcd7d122af09438944dcdc146f476` | 范围外 DFX/log |
 | #2531 | `600540064cc99f99006c11524561d2568ea2bb06` | EX-7 |
 | #2537 | `e0a5e9a16ce2df88643fbd20c4e31406f4062e95` | 范围外 DFX/log |
 | #2555 | `90fcb27027a000eac2f1a4d34b7083a53d2a9599` | 范围外 CI/测试/文档/打包 |
 | #2554 | `436856277dff3acd2b8b1237848075ffab9f8a1a` | 范围外 CI/测试/文档/打包 |
 | #2551 | `3620d76b3a92b513fb448be83aaefaf8f04b870f` | 范围外 runtime/HBG 实现 |
 | #2550 | `abd15cc8017c0734a8443f2ec15112a5404379f7` | 范围外 DFX/log |
 | #2424 | `b694fc948693394829c6ab4c9ca85d7992995fa1` | 范围外 DFX/log |
 | #2544 | `1d82e7f50d193b219c5a472f01e2ef86a1a89f3d` | 范围外 runtime/HBG 实现 |
 | #2529 | `6e224fc1b4f6a2b1febfd09aa7ebcb15802d40b1` | 范围外 runtime/HBG 实现 |
 | #2542 | `172682a6be554592293c957acf5d9410b904f11f` | 范围外 CI/测试/文档/打包 |
 | #2513 | `7f2c3af4444dcecb316c860d5daff7b95cbf7c71` | 范围外 CI/测试/文档/打包 |
 | #2539 | `190c235e2c85f9f84e2ceef2e853e7bffe313f1a` | 范围外 runtime/HBG 实现 |
 | #2535 | `1d539d3c36e7b2bf3cbd2d332d683c322daf8a15` | 范围外 runtime/HBG 实现 |
 | #2532 | `fc8b069fd4d3c6488006fb792e770de652dcf911` | EX-7 |
 | #2528 | `b58622478764433408ac3d06deeb401614df11bb` | 范围外 CI/测试/文档/打包 |
 | #2516 | `e6fd8c608a7271b76d06a4a598fab6fcf2ab9d2c` | 范围外 runtime/HBG 实现 |
 | #2506 | `5a111b2f2f648cb5209b77bb7ffd9cd5f28054ca` | 范围外 runtime/HBG 实现 |
 | #2518 | `65a675572fd9d452c0d980c386ebceef8d26e3a7` | 范围外 runtime/HBG 实现 |
 | #2521 | `f365ae97ea3d61b4d5c71d496df8a11a397c870c` | 范围外 CI/测试/文档/打包 |
 | #2388 | `c0b33b0eda671116ab6c949843789fe4decfdf19` | 范围外 runtime/HBG 实现 |
 | #2503 | `b3bbcbb93c4dc58c0b9a15d77807ab16b6b3454d` | RA-13 / RA-15 |
 | #2515 | `085306edb0e5d2da262c9a4b01c674fce5667144` | 范围外 runtime/HBG 实现 |
 | #2510 | `27e13ce2ed221cf641b1e8fd767e4cc408f2f5e3` | 范围外 kernel/eager；共用执行寿命与 EX-7/RA-13 联验 |
 | #2514 | `a97cfaa81d76b747343a48fb090c6a75fbf99940` | 范围外 DFX/log |
 | #2509 | `60167bcc1ea898cc0bf861b2e3aa0aa6f70ba40d` | 范围外 DFX/log |
 | #2507 | `97f8b6c92d9cbd6b8270dfa918272c964a7c25e1` | 范围外 kernel/eager；共用执行寿命与 EX-7/RA-13 联验 |
 | #2096 | `ae234fad000cd6bfa9b67865e336d7e90058f7ad` | RA-11 |
 | #2505 | `daa1670c0f85ec60acdf6bbf6551a63d7410f8d2` | 范围外 DFX/log |
 | #2502 | `f16b6bb420f3f1ed384c7a41a9df49e3a174531a` | 范围外 runtime/HBG 实现 |
 | #2501 | `121d807bb988af39761c577d4e23ad2b17d6f69f` | RA-3 / U2a |
 | #2500 | `a711032ed3a582d241b94af5a045bdb3d5046869` | 范围外 DFX/log |
