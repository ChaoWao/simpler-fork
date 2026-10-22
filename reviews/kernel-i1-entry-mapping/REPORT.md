# I1 真实产物与 kernel eager 消费链核对

核对日期：2026-10-22。只读调查；没有构建、测试、上板或远端写操作。

## 结论

**下一笔建议是 PyPTO 的真实 kernel eager 调用接线：消费现有 shared artifact、CallFrame 和 #3100 生命周期，从 JIT/预编译 artifact 进入同一 Torch 有序队列的 callback，再调用公开 `simpler.Worker.submit(..., caller_stream=...)`。** 这一功能可独立于 Pipeline A 后续工作、TMR capture #2625 和 HBG capture 栈推进；但不能重复 #3100 已有初始化/注册/关闭 owner，也不能在它尚未验收时把其能力当作 main 已交付。

本次发现了旧路线快照未列出的 **PyPTO #3100**。它明确把 callback/Worker.submit 接线留作后续，因此存在可交接的完整功能边界。建议先由现 owner 明确接续/stack 方式；本调查没有替其认领任务。

整个 I1 仍未完成：A5、capture、显式 HOST 控制数据、DFX、旧 integration 迁移各自还有门槛。下面推荐的是一个可完整验收的 A2/A3 双 runtime eager PR，不据此关闭整个 #2666。

## 1. 固定快照与证据

| 对象 | 本次实际读取的身份 |
| --- | --- |
| Simpler 生产代码 | `fa6528f4e45c42242e2162c2ba0192d09cbd3b88`；新 detached 只读 worktree `.claude/worktrees/kernel-i1-audit` |
| PyPTO 当前 remote main | `cecb2e3dcac815671c88fc9a50c75cf61bda1a23`；通过 GitHub GraphQL 读取具体 blob |
| PyPTO 本地 checkout | `8ccd97de3ffd42aa570a08ee1abb763e15d25a16`，缺 handoff `390a7536` 和 #3001/#3084 对象；**不能代表当前 main** |
| PyPTO #3100 | OPEN、非 Draft，`acfc982005625c6954066d43af99a933b8a11e42`，base `fdda228af1f26f723a016d427a8365dfff4520c5`，作者 YunjiQin |
| Simpler PR 列表缓存 | `LOCAL_PATH`，文件时间 2026-10-22 05:57 PDT |
| 远端 tracker | 新读 #2666/#2670，保存为 `LOCAL_PATH`、`LOCAL_PATH` |

安全 GitHub 读取均用 parent `.venv` 的 Python 调用 `LOCAL_PATH`；GraphQL 均为 `query`。原始返回及请求：

- `LOCAL_PATH}`、`LOCAL_PATH`：当前 PyPTO main、#2975/#3001/#3084、Simpler #2493/#2583。
- `LOCAL_PATH}`：main artifact、torch 入口树与开放 PR 名单。
- `LOCAL_PATH}`：#3100 正文/文件、main signature/metadata/JIT、旧 integration 源码。
- `LOCAL_PATH}`：#3100 session/context/pending/native bridge 与 main interop/registration。
- `LOCAL_PATH`：为逐行检索保存的返回 blob；`bridge.py` 内实际为 C++ 原文，没有执行这些文件。

设计核对依据：`docs-kernel-mode/ROADMAP.md` §2、§3、§8 I1–I3；`runtime-design-principles.md` §1–§16；`kernel-eager-open-work.md` K2c/S3/C1 与当前边界。规范要求共同 Worker/executor、每次 eager 准备一次、caller 前后序、host 值快照、partial/unknown 保留；不恢复旧 dedicated executor 或 PreparedCall。

## 2. 实际编译产物与 target

### PyPTO main 已有，不应重写

固定源码链接前缀：`https://github.com/hw-native-sys/pypto/blob/cecb2e3dcac815671c88fc9a50c75cf61bda1a23/`。

1. `python/pypto/jit/decorator.py:3667` 的 `JITFunction.__call__` 调 `_resolve_compiled`，然后仍调用 `compiled(*ordered_args, config=...)`。`compile()` 也返回既有 `CompiledProgram`；**没有 kernel eager 分流**。
2. `python/pypto/ir/compile.py:306` 的公开 `compile()` 当前按 `ArtifactExecutionMode.PROGRAM` 进入 `_compile_to_files`；`:532` 已有内部 `_compile_artifact(..., contract=...)`，`:646` 的 KERNEL 分支运行 logical/lowered signature 校验和 HBG host-access 校验。`:682` 的 `_make_single_chip_artifact` 生成 shared `CompiledArtifact`。
3. `python/pypto/ir/compiled_artifact.py:25` 的 `CompiledArtifact` 只拥有 metadata、lazy callable/binary；不拥有 Worker、stream 或本次参数。`load()` 调 `runtime/device_runner.py:_compile_and_assemble`（或已有 `ArtifactRuntime.load`）。不能重新造一套 kernel artifact store。
4. `python/pypto/ir/_artifact_metadata.py` 的 `compiled_meta.json` **schema=3**，保存 logical `params`、`lowered_params`、`return_aliases`、`supported_execution_modes`、platform/backend。旧 metadata 不能证明 KERNEL capability。`_compile_contract.py` 保证返回 alias 外部 tensor，禁止 kernel 隐式新增返回 allocation，约束 scalar direction、外部形状/layout 和 lowered slot 顺序。
5. `runtime/_artifact_sources.py:23` 的 `read_kernel_config` 读取 `kernel_config.py` 的 `KERNELS`、`ORCHESTRATION`、`RUNTIME_CONFIG`；打包逻辑也已存在。
6. `runtime/device_runner.py:481` 的 `_compile_and_assemble` 持目录锁，并检查 runtime/PTO-ISA/build cache context。`:553` 读取上面三项；`:565` 创建 `KernelCompiler(platform)`；`:666` 建 `CoreCallable`，`:676` 将 orchestration signature、function_name、SO bytes 和 `(func_id, CoreCallable)` children 装成 `ChipCallable`。
7. Simpler `simpler_setup/kernel_compiler.py:581` 的 `compile_incore` 生成 core ELF；`:709` 的 `compile_orchestration` 明确区分 **HBG host g++ SO** 与 **TMR onboard aarch64/AICPU SO**。它们不能互换 runtime header/ABI。

**三种 mode 不得混淆：** PyPTO #2975 的 `ExecutionMode.SIM/ONBOARD` 是运行位置；`ArtifactExecutionMode.PROGRAM/KERNEL` 是消费合同；TMR/HBG 是 runtime ABI。将 `execution_mode="kernel"` 从旧 integration 原样搬到当前 target API 会混用不同维度。

### 框架输入已有的真实值对象

`python/pypto/torch/interop.py` 已在 main：

- `TensorMetadata` 保存 logical view 与 backing 的 data/storage pointer、offset、capacity、shape/strides/dtype、device、format。
- `CallSignature.describe_call`（`:286`）完整绑定显式参数，验证 NPU device、布局、alias、typed scalar，并取得当前 Torch stream，返回 `CallFrame`。
- `CallFrame`（`:79`）按值保存 metadata/scalar，并强持有 torch tensor/storage/stream；`alias_result()` 返回外部实际对象。
- 文件明确说明 **不编码 native ABI、不初始化 Worker、不注册、不 enqueue，Python 引用也不等于异步 allocator 防复用**。
- `torch/registration.py:RegistrationSignature` 已提供 schema/fake/return-alias 合同，应消费它而非重写。

因此不应把普通 `torch_interop.make_tensor_arg` 当 kernel 路径：Simpler `simpler_setup/torch_interop.py:134` 明确拒绝非 CPU torch tensor；PyPTO program 的 `runtime/tensor_arg.py` 仍服务 host/worker-owned DeviceTensor 路径。新的借用 NPU 转换应消费现有 CallFrame，走 Buffer/Tensor/TaskArgs。

## 3. register → public/native Worker → kernel eager 的具体链

以下 Simpler 代码均固定在 `fa6528f4e`，而非旧 integration 分支：

```text
CompiledArtifact.load / .chip_callable
  → public Worker(level=2).init(borrowed_context_generation=...)
  → public Worker.register(ChipCallable) → CallableHandle
  → 本次 CallFrame → Buffer.wrap(location=worker.device_location)
                 → Tensor(backing, shapes/dtype/strides/byte_offset)
                 → TaskArgs.add_tensor/add_scalar
  → public Worker.submit(handle, args, config, caller_stream=...)
  → Worker._submit_chip_direct
  → Worker._materialize_l2_args / task_interface._materialize_chip_task_args
  → _ChipWorker._submit_chip_run_direct [nanobind]
  → ChipWorker::submit_chip_run → ChipRunLane::submit
  → ChipWorker::prepare_native_run_on_slot → simpler_prepare_run
  → ChipWorker::launch_native_run / launch_native_run_joined
  → simpler_launch_run_on_stream
  → DeviceRunnerBase::launch_run → DeviceRunner::launch_execution_impl
  → caller Start → AICPU/publication → Fork → AICore
  → 两侧完成/Exit → caller 后序
  → RunHandle.done / wait → native finalize / per-run retirement
```

这里 **CallFrame→TaskArgs→framework callback** 是缺失的 PyPTO 消费接线；其后的 Simpler 主链已经存在，不能把整段画图当作当前 PyPTO 已跑通。

具体生产定位：

| 责任 | 源码/符号 |
| --- | --- |
| 公开 borrowed init | `python/simpler/worker.py:8875` `Worker.init`；`:9092` `_init_level2` 直接创建 `_ChipWorker` |
| 公开注册 | `worker.py:7870` `Worker.register`；post-start L2 消费既有 registration/生命周期 owner |
| native 自动 ID 注册 | `src/common/worker/chip_worker.cpp:825` `ChipWorker::register_callable(blob,size)` → `simpler_register_callable_v2`；`src/common/platform/onboard/host/c_api_shared.cpp:1179` |
| submit 的公开合同 | `worker.py:12974` `Worker.submit`；`:13425` `_submit_chip_direct` 校验 caller/current-context、拒绝 borrowed H2D、快照 TaskArgs、保留 source uses，然后 materialize/native submit |
| 共同 wire 转换 | `python/simpler/task_interface.py:151` `_materialize_chip_task_args`；不是第二套 kernel POD |
| native 准备与 lane | `chip_worker.cpp:1033` `prepare_native_run_on_slot`；`:1157`/`:1168` `submit_chip_run` |
| 共同 C 准备 | `c_api_shared.cpp:1665` `simpler_prepare_run`，含 `validate_current_context`、kernel args HOST/NONE 或 DEVICE/NONE 校验、原 bind/publication/prepare owner |
| caller stream launch | `chip_worker.cpp:1375` `launch_native_run`；`:1430` joined 版本；`c_api_shared.cpp:2017` `simpler_launch_run_on_stream` |
| 双侧执行 | `src/a2a3/platform/onboard/host/device_runner.cpp:717` `launch_execution_impl`；A5 同名实现；保留 descriptor/结果/完成/错误/资源协议 |
| 完成/退休 | `worker.py:4980` `RunHandle`、`:5182` `wait`，共同 native run/lane/close owner |

已有 `tests/ut/py/test_worker_caller_stream.py` 直接走公开 Worker，用真实 RTS 分配和 caller copy/event 链验证 TMR/HBG；它没有经过真实 Torch task queue，所以不能冒充 framework adapter 验收。

**不要用 `simpler_kernel_mode_supported()==0` 误判共同 eager 未交付。** 当前 C API 中旧 dedicated launch 仍拒绝；`ChipWorker::init` 也解释该位不回答共同 eager 能力。公开 borrowed init → 共同 native run 才是应接入口。

## 4. 现有 owner / 候选，不重复实现

| PR | 固定 head / 状态 | 真实范围与处置 |
| --- | --- | --- |
| Simpler #2625 | `71f5b4fc`，OPEN 非 Draft，qweasdzxcht | TMR capture/replay；A5 ~28s timeout、17.28s long-wait、macOS allocation-failure test仍是本 PR blockers。旧路线 `7848e06e` 已过时。不要另写 TMR capture，也不等待它才能接 eager。 |
| Simpler #2626 | `f701832b`，OPEN Draft，zxx0288 | shared fixed resources / launch contracts；不是共同 eager framework adapter。 |
| Simpler #2646 | `6cb08c73`，OPEN Draft，zxx0288 | HBG graph build/packet/restore 模块。 |
| Simpler #2647 | `4c0eacce`，OPEN Draft，zxx0288 | 依赖 #2626→#2646 的 A2/A3 HBG dedicated launch/graph lifecycle；A5 resident、TMR dedicated、dedicated DFX仍不支持。不能当作 main 已验收，也不要另造 HBG graph owner。 |
| Simpler #2605 | `591f003b`，OPEN，Crane-Liu | 固定形状 vLLM eager。短序列通过、128 token不一致；正文已有 projection/RMSNorm/BF16 rounding诊断，下一修正在 decode artifact 数值合同。不能据此宣称 kernel框架合格，不重复其 driver/数值排查。#2666正文把它标成Program证据，而PR措辞含borrowed/caller，最终模式应以实际源码/构建为准；本次未重审其diff。 |
| PyPTO #3100 | `acfc9820`，OPEN 非 Draft，YunjiQin | **新增核实的现 owner。** 在shared artifact #3077上提供显式 `pypto.torch.init()`、单个public Worker、current-context/generation、注册复用、pending/cleanup/shutdown。正文明确 JIT仍Program dispatch、kernel init后拒绝；Torch callback + Worker.submit是后续步骤。可消费，不能竞争重写。 |
| PyPTO #3001 | `ed612fbd`，OPEN，YunjiQin | target旧 `feat/kernel-mode-integration-test`；kernel SDMA init/artifact requirement；依赖旧 Simpler #2583，不是 main eager通用前置。 |
| PyPTO #3084 | `f6b22488`，OPEN，TaoZQY | 同旧target，明确**包含#3001全部feature变化**加prefetch修正；必须去重。依赖Simpler #2583 `97343062`，其A3成绩不是当前main链证据。 |
| Simpler #2583 | `97343062`，OPEN，TaoZQY | 旧integration kernel SDMA ABI/workspace、TMR/HBG prefetch；不是新 eager第一笔必须合入的整栈。 |
| Simpler #2493 | `536cf3ee`，OPEN，nalinaly | 旧 HBG dedicated DFX registration 从88到96字节/version2及multi-callable swimlane；不得把它的ABI/可变窗口直接搬到主线逐调用descriptor。最终DFX仍须迁移验收。 |

上表PR边界是正文/源码静态核对，**不是完整diff审查或CI验收**。原始PR数据见上述缓存。PyPTO #2975已经合入，提供target轴选择，不等于kernel调用接通。

## 5. TMR / HBG 的实际缺口

| 维度 | TMR | HBG |
| --- | --- | --- |
| main共同native/public eager | 已有caller stream + borrowed DEVICE链；不是#2625才提供 | 同样已有；每次bind运行fresh host orchestration，不走capture packet |
| PyPTO main真实framework eager | 缺JIT/kernel artifact分流、queue callback、CallFrame→TaskArgs、allocator/device期保活 | 同一adapter缺口；还必须保持每次fresh host准备，不复用整次计划 |
| capture | #2625候选未验收、当前A5 blocker | #2626/#2646/#2647候选，主要A2/A3；A5不可外推 |
| HOST控制 | TMR device orchestration不接受HOST tensor；typed host scalar可按值提供 | Simpler共同eager允许显式HOST/NONE，但PyPTO现KERNEL compiler `validate_kernel_host_access` 会拒绝host tensor读写，CallSignature也只接受NPU tensor。**最终设计要求的显式HOST控制消费尚未在此adapter闭合**；不能偷回拷DEVICE。 |
| SDMA | Simpler fa6528 的 borrowed `ChipWorker::init`明确拒绝enable_sdma | 同左；旧prefetch栈不能绕过此限制。普通不prefetch eager无需它。 |
| DFX | 继续消费共同逐调用配置/collector，不等于capture DFX | A5 HBG args_dump等现限制也要保留，不能由A3结果外推。 |

#3100的具体API证明：

- `runtime/kernel/session.py:_BorrowedWorkerSession._init` 建一个普通 `simpler.Worker(level=2, ...)`，通过 `borrowed_context_generation` 初始化；`prepare()` 在owner thread/context调用 `worker.register`。
- `runtime/kernel/context.py:_ProcessKernelState.ensure_callable` 消费 `CompiledArtifact` 的KERNEL capability、platform/runtime，load真实callable后得到注册handle；`submit()`先保存ticket再`ticket.enqueue()`。
- `runtime/kernel/pending.py:PendingTicket` 的文档明确是 **future queue adapter** 来publish/fail；wait/cleanup债务机制已有，不应再造一个future owner。
- `torch/launch.py` 明确是lifecycle query；C++ `python/bindings/torch/torch_npu_adapter.cpp`仅导出 `framework_alive`、`borrow_context`、`bind_context`、`retain_until_exit`、`check_call`、`storage_shape`，**没有Torch队列enqueue、Worker.submit callback或allocator recordStream**。
- `KernelConfig`当前只准A2/A3 TMR/HBG。#3100没有构建optional adapter或上板，1138个mock-based CPU成绩不证明callback/GIL/device正确。

## 6. 推荐下一PR：A2/A3 PyPTO kernel eager真实接线

建议标题：`feat(torch): execute kernel eager calls through public Worker`。

前置是消费/协调#3100，而不是重新实现其初始化；shared artifact/interop/registration已经在当前PyPTO main。此PR不改Pipeline A lane/admission/backend，也不接旧dedicated launch/capture ABI。

一个完整的可观察结果：用户显式kernel init后，同一真实JIT或恢复的KERNEL artifact接收caller NPU输入/显式Out/InOut，经过本次Torch stream的同一有序framework队列调用共同Worker，返回原外部alias输出；下游Torch算子可正确读取结果。重复调用每次使用新的metadata/scalar值，HBG每次fresh构图。

必要实现一起交付：

1. 在现有JIT分流点消费process mode；kernel分支用现有`_compile_artifact(contract=KERNEL)`和shared metadata/cache，不把Program artifact伪装成kernel，也不新增平行artifact对象。JIT与prebuilt恢复均覆盖；cache身份不能混用PROGRAM/KERNEL或runtime/target。
2. 消费`CallSignature.describe_call`和注册handle；将storage/view与typed scalar映射为公开`Buffer.wrap`、`Tensor`、`TaskArgs`，精确保留storage bounds、view offset/layout、direction/alias；不能走CPU-only torch helper或隐式tensor搬运。
3. **实际Torch queue适配**：以同一framework有序机制送入callback，再从正确current context/实际caller RTS stream调用`Worker.submit`。仅在Python调用线程取raw stream直接submit不满足TQ=1。现optional native bridge可扩充framework职责，但不链接第二份Simpler执行实现。
4. 复用#3100 `PendingTicket`，在enqueue前持有frame/storage/source/registration，并在callback中publish真实RunHandle或失败；证明host排队期和device使用期的allocation防复用。retain Python对象不代替allocator recordStream或相应实际保证；external storage按明确caller合同保留。
5. 前置拒绝保持可重试；partial/unknown与callback异常保留ticket/source/run债务并停止新调用；close先结束framework排队callback及真实run退休，禁止reset caller context/stream；成功完成后才关闭borrowed Buffer源引用。

**可完整验收的门：**

- 固定PyPTO head、Simpler `fa6528`之后的集成head、PTO-ISA pin、SDK/torch/torch_npu与optional adapter构建身份；不要混用旧integration `_kernel_abi` pin。
- A2/A3 × TMR/HBG × `TASK_QUEUE_ENABLE=0/1`：真实JIT和预编译恢复；caller前序写→完整双侧executor→caller后序读。读caller结果在首次RunHandle wait/poll之前，毒值/漏依赖负对照必须失败。
- 变化输入、scalar、输出alias/InOut与连续调用；cold/warm注册；HBG fresh host准备计数；不增加公开PreparedCall/repeated submit。
- 排队后立即丢弃/修改临时host参数容器、强制allocator再分配压力、两stream显式桥接；不能用同步整个device掩盖缺少queue顺序。
- 错context/generation/capture前置拒绝后的恢复；callback/enqueue失败、partial/unknown、close/retry、caller context/stream不被销毁。受控CPU注入与安全真实设备验证分开；不要求做危险设备挂死注入。
- Program路径回归、optional torch_npu缺失时lazy import/清楚拒绝；最终head真实framework/onboard证据必须补齐，mock-only CPU通过不算功能验收。

首笔保留当前#3100的A2/A3与现有NPU tensor合同，并明确拒绝capture/SDMA/不支持输入；**A5、显式HOST控制、完整DFX与capture仍是#2666强制后续交付，不是删除目标**。没有现成owner授权前，不应把这些后续自动分给#3100作者。

## 7. 跟踪与本次限制

#2670当前默认L2/kernel接收者为zxx0288；#2666实际assignee仍空。它明确不覆盖已有PR作者owner，也不要求每个helper建Issue。推荐范围先在#2666内明确实际接收者与#3100交接，确需独立负责人时再拆子项。

本次没有重审#2605实际资源模式、没有编译optional framework bridge、没有运行任何新CPU/模拟器/上板测试，没有性能结论。所有green/timeout数字仅归原PR正文的原head；本文只建立当前源码/依赖映射与下一功能边界。原tests worktree未再写入。

## 主执行接收

已核对报告的shared artifact/CallFrame、pending ticket与launch callback占位源码证据，接受为静态入口/依赖映射。未把候选正文当完整diff审查或上板成绩。下一实现必须协调既有#3100 owner并消费其生命周期；本轮不启动竞争初始化或框架adapter实现。
