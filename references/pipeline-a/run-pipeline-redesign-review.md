> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# Run-pipeline 重构设计 Review：program/kernel 归一化与 API 边界

> **历史设计 / 评审快照**：保留文中的原始基线、结论与验证记录，不代表当前待办或支持范围。当前进度统一见 [路线与阶段表](run-pipeline-track-a.md) / [HTML 看板](run-pipeline-track-a.html)。

更新时间：2026-09-16 00:21

审视对象：[run-pipeline-redesign.html](ARCHIVE-INDEX.md#ref-05437c7e2213d7ea)（v6）。

状态：讨论稿；“纯 host 构建与带 stream 的设备执行分离”已由用户明确，本轮补充 HBG 作为 host tiling、由 PyPTO 管理重新 tiling 与缓存的职责划分。具体 API 签名、交互产物布局及绑定/释放协议仍待细化。记录原则、源码事实、撤回建议和剩余问题，不表示目标能力已实现。本次只修订本文，不修改 HTML 或 runtime，不运行设备测试。

记录方式：保留讨论过程、原来的疑问和候选方案，另行标明当前结论及撤回原因。保留某个观点不表示它仍是本轮实施要求；历史过程见 §8 和 §9.9。

## 1. Review 结论与讨论范围

原提案关注解除 program pipeline 的固定槽位限制；本轮进一步要求 program 与 kernel 共用设备提交及资源管理逻辑，同时保留 program 的 host 多线程编排。

本次已明确的大原则是：**构建负责产出设备执行描述，运行负责消费执行描述。HBG 的 host orchestrator + build_graph 归入纯 host 构建层；kernel_launch 保持带 stream 的设备提交职责，不为运行 host 编排而合并两层 API。**

对 PyPTO 接入，HBG 更适合视为一次额外的 host tiling 调用：PyPTO 决定是否调用、如何匹配和缓存结果；simpler 提供构图算法与明确的输入/产物契约，并负责后续绑定和设备执行。TRB 跳过这次 host tiling。这里类比的是职责，不是说 HBG graph 与 CANN TilingData 的格式相同。

HBG 构建如果需要读取 device Tensor 的内容才能继续，就报错并提示使用 TRB。不隐式 D2H、sync、分段 launch，也不自动切换后端。这个限制适用于前序已完成、已提交未完成、本 run 尚未提交等所有 device 值来源；并非“只拒绝本 run 中间结果”。

TRB 将需要读取 device 数据的编排控制放到设备端执行，不以新增 host/device 交互式续投协议作为本次统一路径的前提。HBG 已表达在图中的设备侧 predicate 不受影响：限制的是 host 构图取值，不是设备任务正常读取数据。

讨论状态如下：

| 事项 | 当前状态 |
| --- | --- |
| program/kernel 共用设备提交、资源串行化与失败处理 | 用户明确的本轮目标 |
| 保留 simpler 内 program orchestrator 的 host 多线程并发 | 用户明确要求 |
| 正常路径异步；队列压力在提交处承接；大 args OOM 通过 timeline sync 后重试一次 | 已明确的设计方向，恢复机制有适用边界 |
| 纯 host 能力独立提供给 PyPTO；simpler 负责自己的 device 资源 | 用户明确的职责边界 |
| HBG 作为额外的 host tiling，TRB 不调用该步骤 | 本轮讨论的接口组织方向；不新增普通用户的显式 tiling API |
| shape 等输入变化时的重新 tiling 判断、缓存 key 构建/比对与缓存生命周期 | 归 PyPTO；simpler 定义完整构图契约并提供构图能力，不在 launch 内另设隐式缓存策略 |
| 可缓存调度方案与每次执行参数 | 必须分开；现有 graph 的地址、Scalar 和借用存储不能直接当成跨调用缓存 |
| HBG 构图需要读 device 值 | 已明确：报错并提示使用 TRB；不自动 D2H、同步、切换后端 |
| 初始化、callable 准备一概要求 caller stream | 已修正：内部需要 stream，不等于 API 必须接收 caller stream |
| 在完整 HBG 构图中插入 sync，读取本 run 尚未执行的 task 结果 | 不成立：生产者尚未提交，sync 无法产生进展 |
| 构图中途分段 launch、回读、继续构图 | 已撤回为本轮方案，不纳入实现要求 |
| 为中间 device 值增加设备侧调度求值能力 | 非本轮必需项；已有 predicate 保留，额外表达能力可另议 |
| 一次 launch 内运行 host/device 交互式流式编排 | 不纳入本轮方案；不能与 TRB 的设备端编排混为一谈 |
| 将每次 run 的 prepare 与 kernel_launch 合并 | 已撤回该建议；本轮采用纯 host 构建与设备提交分离 |

设计从客观职责出发，不以当前 kernel 路径的函数拆分或 demo 写法为约束。历史候选方案保留为讨论记录，但不再与已明确的分层原则并列为待选方案。

## 2. 已明确的目标与不能顺带改变的边界

### 2.1 本轮目标

- program 和 kernel 共享底层执行机制，通过 kernel launch 实现 host/device 异步并发。
- 编译、HBG host 构图等纯 host 工作独立于设备提交；需要 device 值才能完成的 host 构图直接报错，使用 TRB 表达该编排。
- 保留 host 多线程编排与提前构建能力，不能把多线程 orchestrator 简化成调用线程内的串行函数。
- 共享可变 device 资源的访问在 device 上串行；host 提前准备不等于多个 run 同时修改同一 workspace。
- 不再把固定两槽位当成统一提交契约；也不把“取消两槽位”理解为任意资源可无限积压。
- CANN 队列深度压力在实际提交处承接；可恢复的大 args 分配失败在 simpler 内处理。
- 整网 decode 继续面向一次逻辑调用覆盖模型执行，不要求修改 vLLM/PyTorch 执行层。

此前“program 保留两槽位且不共用 kernel_launch”的方案已被本轮目标取代，不能继续作为新设计的前提。

### 2.2 不因本轮归一化而自动改变的边界

- 普通算子用户仍通过 PyPTO 调用；lazy init、加载等复杂性尽量内部处理，不新增必经的显式编译、prepare 或 warmup 仪式。
- 算子的 AICore binary、orchestration SO 的编译及编译产物缓存归 PyPTO；simpler 负责运行时加载与执行资源，不接管算子编译工程。
- Scalar 是每次调用的数据，不得因为构图或缓存而默认为首次调用常量；动态 shape 沿用 PyPTO 前端已有表达。
- 输入、输出由外部传入；不新增面向算子用户的 workspace allocate API。内部动态 heap 分配仍由 runtime 管理。
- PyTorch 适配层负责框架 stream/提交顺序及外部 Tensor 接入；simpler 不接管外部 allocator、stream、设备或 Tensor 的所有权。
- 本次 run 内有界运行的执行器，与跨调用常驻的 AICPU 服务是不同概念；kernel 模式不引入跨调用常驻服务，也不新增 host 运行中续投的必经流程。
- 目标组合仍包括 A2/A3、A5，HBG/TRB，eager/ACLGraph。能力目标不等于每个组合已经成立。
- 代码归一化不自动意味着 program/kernel 已可在同进程共存。此前的资源及 ACL 所有权限制是否解除，需要另行确认，不能由共用函数推导。

## 3. 先消除术语歧义：prepare 和 launch 各指哪一层

### 3.1 编译、构图、加载与执行分别是什么

| 工作 | 产物或行为 | 职责归属 |
| --- | --- | --- |
| 编译 AICore kernel、host/AICPU 编排源码 | 机器码、编排 `.so` | PyPTO 主导编译与产物缓存，可使用 simpler 的 SDK 能力 |
| HBG 在 host 运行已编译的编排程序 | graph、参数布局、临时内存需求 | 纯 host 构建层，独立 API，不读取 device 内容 |
| 加载 callable、执行资源首次初始化 | 设备驻留状态、ready 依赖 | 运行时生命周期管理，不等于业务执行或编译 |
| 绑定/上传本次执行描述，提交计算 | device 任务及完成状态 | 带 stream 的设备运行层 |
| TRB 执行过程中按 device 值继续编排 | AICPU 上的运行行为 | 设备执行阶段，不是 host 构图阶段 |

之前讨论的 `kernel_prepare_callable` 是加载准备，不应与 HBG 的每次构图共用一个含糊的 prepare 定义。保持独立构建和提交，不要求取消加载缓存，也不要求普通算子用户手工调用这些内部 API。

### 3.2 “一次 launch”也有三个层次

1. 框架看到的一次 PyPTO 算子调用，例如整轮 decode。
2. PyPTO 向 simpler 发起的一次逻辑执行请求。
3. simpler 实际调用 CANN 提交的设备任务，可能包含配合工作的 AICore/AICPU 分支及 event。

本轮不应把第 2 层“一次执行”误写成“任何平台上只能调用一次底层 CANN API”。反过来，也不能用第 2 层只有一次函数调用来掩盖新增的 HBG 分段执行。

program/kernel 是接入及运行模式，HBG/TRB 是构图/调度方式。保留 program 的 host 多线程，不等于把 TRB 的 AICPU 编排改成 host 读回值再持续投递；本轮不新增这种 host/device 交互协议。

### 3.3 HBG 的 graph 与 AICPU `.so`：职责可类比，格式不能混同

```text
HBG：编排源码 → host 编排 .so → host 执行 build_graph → graph → AICPU 执行器消费
TRB：编排源码 → AICPU 编排 .so → AICPU 运行编排并调度任务
```

从分层上，两条路径都先提供 AICPU 可消费的执行产物，再进入带 stream 的执行；因此可以把 HBG 构图与代码编译一起归到“host 构建职责”。但 `build_graph` 的产出是图数据，不是重新编译一个 AICPU ELF `.so`。

当前 PyPTO 交给 simpler 的核心产物是 `ChipCallable`：编排 `.so`、子 kernel 二进制与签名元数据组成的自定义连续二进制包；每次 `TaskArgs` 和 `CallConfig` 另外传。它本身不是构建完成的 HBG 图，见 [PyPTO 组装位置](ARCHIVE-INDEX.md#ref-3ab645dfdd28047f)。

当前 [HBG build_graph](ARCHIVE-INDEX.md#ref-edde00644f735beb) 接收本次 args，调用已加载的编排入口；[后续调用处](ARCHIVE-INDEX.md#ref-2669cbc2e4ff5eca) 再执行上传。此处源码仅用于区分产物和阶段，不表示当前整个准备路径已经满足新的纯 host 契约。

**纯 host 构建不等于一次性编译。** HBG 图可能依赖每次 shape、host Scalar、host 控制输入及参数绑定，需要重新构建或绑定；不能仅因 callable 相同就永久复用首次图像。图模板、每次参数快照、设备驻留代码的缓存与寿命必须分开。每次实际发生的构图成本仍应计入端到端调用耗时，不计入 device kernel span；ACLGraph replay 不重新运行这些 host 构建代码。

重新构建、命中已有 tiling 结果和仅重新绑定本次参数，是不同动作；PyPTO 的触发与缓存职责见 §9.4–9.8。

## 4. R1：stream 入参应表达调用顺序，不按“是否碰 device”机械划分

最初把“准备执行资源、上传 callable、提交设备初始化”统一列为需要 caller stream，过于宽泛。应按依赖划分：

| 操作 | 是否需要 caller stream 入参 | 责任 |
| --- | --- | --- |
| 纯 host 解析、构图、参数整理 | 不需要 | 不隐式提交任务、拷贝 device 值或同步；构图需取 device 值则报错并提示 TRB |
| 新建 simpler 自有分配 | 不因 malloc 本身而需要 | 明确 device/context 归属，正确处理初始化失败 |
| 加载新 callable、初始化新执行资源 | 不必需要 | simpler 保证初始化完成后才能被执行使用 |
| 单次执行提交、与前序业务工作有依赖的拷贝 | 需要 | 建立 caller stream 与内部执行的先后关系 |
| 独立、显式的业务回读操作（非 HBG 构图） | 需要相应顺序信息，通常是 caller stream | 拷贝及 host 读取必须发生在生产者完成之后；不能作为 HBG 隐式取值的补救路径 |
| 扩容、重建、释放正在被使用的 simpler 自有资源 | 不应依靠调用者随便传一条流来证明安全 | simpler 等待所有相关自有使用者，或保留新旧版本直到旧版可回收 |

初始化可以采用“内部准备 stream + ready 依赖”，也可以在冷路径完成同步后再返回。两者都不必要求用户传入业务 stream；这不代表热路径可以使用未完成初始化的资源，也不要求现在选定其中一种实现。

CANN 本身也不是“内部访问 device 就一定有 stream 参数”：`aclrtBinaryGetFunction` 没有 stream 入参，但官方文档说明首次获取函数时会把关联二进制拷贝到当前 context 的 device。参见 [CANN aclrtBinaryGetFunction](https://www.hiascend.com/document/detail/zh/CANNCommunityEdition/900/API/runtimeapi/aclcppdevg_03_0150.html)。

必要的 device/context 标识、初始化防重入和 ready 协议不能因为“不传 caller stream”而省略。lazy init 也不应偷偷执行一次业务算子，否则会额外修改 Out/InOut 状态。

构建层仅使用 host 已知的目标配置、元数据、数值与不透明 buffer 引用，规划任务和内存偏移；真实 device 分配、地址重定位、上传及共享状态修改属于运行层。内部初始化不要求 caller stream 的例外，不能被用来把 device 副作用重新塞回纯 host build。

## 5. R2：资源归属、版本与顺序应先于解除 pipeline 限制

### 5.1 不接受“当且仅当 host 碰它，所以 PER_RUN”作为充分必要判据

HTML §1 的判据应改为：**多个在途执行是否需要同时保留不同版本，以及最后一个使用者何时结束。**

- host 初始化一次的只读代码/配置可以共享，不必每 run 复制。
- 每次 launch 的参数、标量、地址快照需要避免被后续准备覆盖。
- 只由 device 写的结果/错误状态，如果随后还要被 host 或采集线程读取，也不能立即交给后继覆盖。
- heap 可以共享，条件是所有使用同一份 heap 的 device 执行有序，且不存在未声明的跨 run 活数据。
- `Runtime`、handshake 等结构应按字段拆清：哪些是每次参数，哪些是共享执行状态，哪些是结果。不能仅因结构整体在 prepare 中被写过，就永久规定整结构每 run 分配一份。

可选布局包括“不可变的每次参数快照 + 串行复用的执行状态”；具体字段布局待设计，不在本 review 中指定。

### 5.2 至少区分三种寿命

1. host 构建临时数据：后续上传、延迟提交或 runtime 不再引用时才能回收，不能笼统按函数返回判断。
2. 每次执行记录及参数版本：保留到 device、回拷、错误收集等最后使用者结束。
3. 共享资源及同步对象：按实际引用与代际管理，不机械地随某个 run 的 finalize 一起释放。

HTML 对 event 被后继 wait 引用、失败路径仍可能持有引用的提醒应保留。但“每个 run 必须永久新建一组 event”不是唯一实现；复用必须有明确的 record/wait 代际和最后使用者规则。

跨流排序 event 与用于 RTS 大 args 回收的 timeline event 职责不同，不能因都叫 event 就混用。

### 5.3 所有权边界不等于可以忽略调用顺序

原则是：simpler 管自己分配的资源，不跟踪或释放外部 Tensor，不替用户推断任意跨 stream 数据依赖。

但是，接受了 caller stream 上的一次调用，就必须遵守该 stream 的顺序。即使输入输出完全是外部 Tensor，也不能因“不归 simpler 所有”而让后继 PyTorch 算子越过本次执行。

对 simpler 自有资源的 H2D、清零、重建也必须纳入顺序。只排序 kernel，而让准备阶段的拷贝提前覆盖旧 run 正在读的内容，仍然不正确。

### 5.4 保留 host 并发，不等于放任设备提交乱序

host 可以并行准备 N 和 N+1；N+1 先构建完成，不代表它可以越过同一有序执行域内的 N。应区分：

- host 纯构建及 program orchestrator 的并发度；
- 逻辑执行请求的既定顺序；
- 共享 device 资源的占用区间；
- 设备完成后的结果收集与回收。

HTML §3–4 的五层串行与 A→B/C 乱序例子是有价值的风险清单，但不应直接得出“必须做外部 tensor 的全局跨 run 依赖跟踪”。优先落实已承诺的逻辑 run 顺序与自有资源串行化。

如果使用多条内部 stream，必须有 caller 前序到内部执行、内部完成到 caller 后序、以及不同 run 之间的完整依赖。两条流各自 FIFO、host 互斥或 AICore 的软件返回都不能单独代替这个契约。

program 的 host 工作线程可以继续并行调用同一份构建库，再按逻辑顺序向共同的设备运行层提交。保留该并发能力不要求 `kernel_launch` 内运行 HBG orchestrator，也不要求让 device 等待 host 生成后续任务。

## 6. R3：背压与内存回收，不能混淆三个内存池

### 6.1 深度压力与内存压力分开

用户要求队列深度问题在 kernel launch 提交处承接，不额外公开一个 PyPTO pipeline depth。讨论中的约 2048 是当前场景的 CANN stream 任务条目概念，不是新的 simpler 固定容量规格，也不能把一个逻辑 run 恒等于一个 RTS 条目。

设备队列的有限容量、单次执行的工作量、host 构建耗时和内存分配失败，是不同来源。取消旧槽位限制不保证它们自动消失；本轮不把运行中 host/device 交互引入正常路径。

### 6.2 三类核心 device 内存必须独立说明

| 对象 | 管理与复用方式 | 约束及回收意义 |
| --- | --- | --- |
| callable 条目、驻留代码等 | simpler 自管，跨多次调用复用 | 此前确定的 8192 条目、2 GiB 总容量、按需以 2 MiB 粒度申请，约束这类驻留资源 |
| HBG build 结果通过 launch args 下发的 device 副本 | RTS 按在途 task 或 captured graph node 管理 | 同一个 callable 多次提交也会积压；不占新 callable 条目，不套用其 2 GiB 配额 |
| 执行 heap/scratch | simpler 管理，串行执行时复用 | 复用 heap 不会消除 RTS 已为每次提交保留的大 args；heap 本身不足另行处理 |

此外，program 路径若实际接收 host-backed Tensor，device staging 及回拷仍可能有每次成本；外部 device Tensor 则不应被无故复制。不能用 demo 的 staging 成本代表 vLLM 接入，也不能反过来忽略 program 的真实成本。

既有调查中 Qwen3-14B 的特定 B1/B16 HBG 图包为 83,400,640 bytes，对应每个待执行副本约 80 MiB HBM。此处仅引用历史测量说明风险，不作为所有模型/版本的固定规格，见 [HBG 参数内存调查](ARCHIVE-INDEX.md#ref-cf9a5e1521bacf16)。

### 6.3 已决定的 eager 恢复方向及其前提

```text
实际提交成功
    → 异步返回
实际提交遇到可恢复的大 args 分配失败
    → 如存在部分提交，先按协议取消/汇合，确认可以安全重试
    → 在正确的提交顺序之后记录并同步 timeline event，推动旧 args 回收
    → sync 成功后，用同一份参数快照重试一次
    → 清理、record、sync 或重试失败，则返回最终失败
```

必须保留的限制：

- 不是对所有非零返回码无条件重放整个业务调用；业务已经执行或执行状态不明时，不能重复修改 InOut cache。
- 不重新运行可能有副作用的 host 编排，不重新读取可能变化的 Scalar/Tensor 元数据来构造第二份调用。
- 多分支提交时，AICore 已排队而 AICPU 未提交的状态必须单独处理。不能把回收 event 排在一个等待失败分支、永不退出的执行器之后，再期待 sync 救活它。
- event 记录失败与 kernel 提交失败分别记账。已提交但无完成 event，不能当成“未执行”并释放资源。
- 首次可恢复失败不应提前把 context 永久置坏；只有恢复失败才向上层给出最终错误。不能因此掩盖真实设备故障。
- 不增加无限重试、不主动 free RTS 的 args 指针，也不把代码 arena 配额改造成在途 args 的预算。
- timeline 回收效果来自已调查的 RTS 实现与本机探针，不是已经证明适用于所有芯片/版本的公开内存回收保证。

历史依据见 [timeline 回收调查](ARCHIVE-INDEX.md#ref-c216eefd50cfe75c)。本轮没有重新运行探针。

HTML §8 的两阶段字节预算不能直接成为本轮主方案：首先应落实上述已选择的提交处背压与恢复。simpler 自有 staging、共享资源扩容等问题仍要处理，但是否另设内部容量策略需要另议，不能重新引入用户尚未同意的完整准入系统。

## 7. R4：HBG 纯 host 构建的取值边界——已明确

### 7.1 判断的是取值行为，不是有没有 device Tensor 入参

| 构建时需要的信息 | HBG 构建是否允许 | 责任或处理 |
| --- | --- | --- |
| shape、dtype、stride 等 host 已知元数据 | 允许 | 按已有前端参数语义读取，不读取 Tensor 内容 |
| host Scalar、调用前已就绪的 host-backed 控制输入 | 允许 | 调用方保证本次内容和存活期 |
| device Tensor 的地址或不透明引用 | 允许作为图参数 | 仅记录/传递，不解引用其内容；不因此触发数据拷贝 |
| device Tensor 的内容，哪怕由前序已完成工作产生 | 不允许 | 构图报错并提示使用 TRB，不提供隐式回读路径 |
| 本次图中尚未执行的 task 结果 | 不允许 | 同样报错并提示 TRB，不插入 sync 或分段 launch |

caller tensor 只说明传入/所有权，不自动说明内容已经就绪；host-backed 也不是任意异步写入下的完成证明。调用者提供的已就绪 host 控制输入可以使用，但 HBG 不能为了制造这个输入而自行回读 device。device 数据可被 host 映射访问，也不是绕开这条职责边界的理由。

这里与上一版的区别是：不再讨论“前序 device 值如何在 prepare 中同步读回”。不论生产者是否已执行，只要 host 构图必须读取 device 内容，当前设计就选择 TRB。

建议在编译能够确定时尽早诊断，否则在构图取值入口报错；错误包含取值对象及用途，并明确提示使用 TRB。必须在本次图的业务 kernel 提交之前失败，不上传不完整图，不静默使用常量，不自动切换后端。

### 7.2 `ext_num_tokens_per_owner`：读取位置在中途，生产时间不在中途

这次源码核实得到：

- [共享 driver 的 `_allocate_whole`](ARCHIVE-INDEX.md#ref-27a5da505eede9f9) 在调用前分配 host buffer，并在第 694 行写入 `[T] * n_ranks`。
- [HBG orchestration](ARCHIVE-INDEX.md#ref-f18a679c52d382a7) 将 arg 91 绑定为 `ext_num_tokens_per_owner`。
- [第 6440 行读取](ARCHIVE-INDEX.md#ref-216fbdbf927a4776) 位于前四个 task 的记录语句之后；这些 task 不生产该 tensor。

因此，本例可以提前读取的依据是 driver 确实提前写好了输入，而不只是 README 写了 caller tensor。不能因读取语句出现在 task 中间，就认定数据是前序 task 执行得到的。

HTML 的“目标负载已验证干净”只说明该输入约定满足 host 构建的取值前提。如果以后 arg 91 改为必须由 host 读取的 device 数据，应按新契约报错并选择 TRB，而不是给 HBG 增加同步例外。

### 7.3 真正的例子是 `recv_count_out[expert][0]`

[HBG README](ARCHIVE-INDEX.md#ref-280ebced3fa9ec7a) 记录了原先根据接收数量决定 MoE tile 循环的情况：

- `recv_count_out` 是前序 task 产生的中间结果。
- 示例现在使用静态 tile 网格和设备侧 dispatch predicate。
- `valid_rows` 等数据计算移入消费该计数的 device kernel。
- 剩余 arg 91 影响 `set_block_num`；现有 predicate 决定 task 是否执行，并不等价于动态改变 task 的执行宽度。

这说明特定逻辑可以表达为设备执行时的 predicate，host 构图无需读值。已有这种表达的 HBG 图可以继续使用；如果构图仍必须读 device 值才能确定循环或 block 数，则报错并使用 TRB，不要求本轮把所有控制流扩展成 predicate。

## 8. R5：已撤回或移出本轮的建议

下面保留讨论演变，避免后续重新把旧建议当作实现要求。

| 旧建议 | 当前处理 | 理由 |
| --- | --- | --- |
| 构建 A → launch A → 回读 → 构建 B → launch B | 不采用 | 把完整 HBG 改成分段执行，破坏纯构建边界，并增加跨段资源及 capture 问题 |
| 先启动执行器，host 根据 device 回读结果不断续投 | 不纳入本轮 | 用户已选择 device 值相关编排走 TRB；无需为 HBG 新增 host/device 交互协议 |
| 因交互式编排而合并 prepare 与 kernel_launch | 撤回 | 其前提已被 HBG 纯 host 约束排除，构建和设备提交应独立 |
| 为了兼容所有 host 读值，必须新增动态 block_num 等设备求值表达 | 不作为前置要求 | 现有设备 predicate 保留；额外表达能力另议，无法纯 host 构建的情况选择 TRB |

TRB 的 AICPU 编排与“host 读回后继续投递”不是同一模型。前者在设备执行阶段处理 device 数据及依赖；后者会引入 host 线程驱动、通信、取消及 replay 协议，本轮没有要求新增。

若以后单独讨论 host/device 交互，仍需处理生产者完成和数据可见性、同 stream 回读排在整次执行器之后的死锁、执行器等待 host 的超时、失败退出与 replay 驱动。当前不能用这些尚未设计的机制扩大 HBG 的契约。

已有 [HBG consumer-wait 调查](ARCHIVE-INDEX.md#ref-dcca5f95317e920a) 解释了旧等待为什么不能观察设备进展；本轮不恢复该等待，也不以增加 D2H 来绕开纯 host 构建原则。

## 9. R6：纯 host 构建与带 stream 的运行分离——已明确

### 9.1 CANN 的参考契约

CANN 的公开示例将编译、二进制加载/获取函数、参数准备、计算任务提交分开；`aclrtLaunchKernelWithConfig` 接收函数句柄、执行规模、stream 和参数句柄，被定义为异步计算任务提交接口。参见 [CANN 编译与运行示例](https://www.hiascend.com/document/detail/zh/CANNCommunityEdition/900beta2/opdevg/Ascendcopdevg/atlas_ascendc_10_00040.html) 和 [aclrtLaunchKernelWithConfig](https://www.hiascend.com/doc_center/source/zh/CANNCommunityEdition/920beta2/API/runtimeapi/aclcppdevg_03_1793.html)。

不能据此声称 CANN launch 内部没有任何 host 工作、参数上传或慢路径；比较的关键是，它的公开契约不要求调用者在 kernel 执行中继续运行一段模型 host 编排来补全任务。

### 9.2 采用的职责划分

| 维度 | 纯 host 构建层 | 设备运行层 |
| --- | --- | --- |
| 输入 | 已编译产物、目标配置、host 已知的参数及元数据 | 已准备的 callable、执行描述、本次参数及 caller stream |
| 核心工作 | 编译支持、HBG 构图、参数布局与内存需求规划 | 生命周期级加载准备、地址绑定/上传、设备提交、顺序与回收 |
| device 值 | 不读取；构图需要取值时报错并提示 TRB | 由设备 task 或 TRB AICPU 编排在依赖满足后读取 |
| stream | 纯构建 API 不需要 | 单次执行 API 需要；加载初始化按 §4 的 ready 规则处理 |
| 返回 | 完整 host 产物/执行描述，或者构建失败 | 提交及排序已经成立，device 可以尚未完成 |
| 错误 | 编译/构图错误，不进入本次业务执行 | 加载、提交、部分提交及异步设备错误分别处理 |
| 并发 | program host 线程和 PyPTO 可以调用共享构建能力 | 同一共享资源域在 device 上串行，host 可以提前入队 |

这样保留了接近 CANN 的“准备好产物和参数，再提交执行”的边界。simpler 仍需负责自己的动态调度资源、内部多 stream、错误与回收，但不把模型 host 编排或 host/device 中途交互扩展成 `kernel_launch` 的职责。

### 9.3 内部 API 的组织方向

以下仅表示职责，不是已经定稿的函数签名，也不是新的普通算子用户 API：

```text
PyPTO 普通调用 / simpler program host 工作线程
    → HBG：按需调用纯 host tiling，生成可重新绑定的调度方案；TRB 跳过 host 构图
    → 共用的 kernel_launch：绑定本次执行参数，提交到本次 stream
    → device 执行；simpler 管理自有资源和完成回收
```

- HBG 构图算法可以继续由 simpler 的 host 库提供，PyPTO 主动调用；不要求 PyPTO 重写调度算法。
- program 保留已有 host 多线程来调用相同构建能力，构建完成后进入相同设备提交层，避免两套实现。
- TRB 提交已编译的 AICPU 编排产物与本次输入，由 device 执行编排，不强制先生成一张 host 完整图。
- 运行层接收 HBG 图描述或 TRB 运行描述；是否共用外层封装、具体 ABI 和参数绑定方式待细化，不强行把 graph 数据变成 ELF `.so`。
- 构建产物用 func_id、buffer 引用、内存偏移/需求等描述执行；真实 device 资源和需要实际地址的重定位由运行层负责。
- callable 的代码/加载缓存与每次执行描述分开，HBG host build 不复用会被在途 run 读取的可变快照。
- PyPTO 的普通算子调用仍可在内部串起 lazy 编译、构建和 launch；用户看到一次调用，不等于 simpler 必须把这些职责合成一个 API。

**已定结论：保持独立的纯 host 构建与设备提交契约。PyPTO 接入的 tiling 缓存职责按下面的分工确定；具体产物 ABI、绑定字段及引用释放协议仍需细化，不再把“是否为了 host 读 device 值合并 API”列为待决问题。**

### 9.4 HBG 的重新 tiling 与缓存：PyPTO 决策，simpler 提供能力

本轮讨论结论是：**重新 tiling 的触发判断、缓存 key 的构建与比对、缓存的生命周期和淘汰，都由 PyPTO 管理。simpler 负责执行 tiling，以及使用结果完成设备执行。**

| 工作 | 责任方 | 边界 |
| --- | --- | --- |
| 按前端规则选择编译特化、管理编译产物 | PyPTO | 动态 shape 不等于每次重新编译 |
| 整理 tiling 输入、构建/比较 key、命中或新增缓存 | PyPTO | 在调用 host 构图 API 之前完成缓存决策 |
| 构图算法、输入和产物兼容性契约 | simpler 的 host 构建库 | 依赖的信息必须显式进入约定；不让 PyPTO 猜测内部隐藏输入 |
| 当前参数绑定、资源兼容性检查、上传和提交 | simpler 的运行层 | 不在 kernel_launch 内另建一套算子级缓存或偷偷重新构图 |
| 缓存淘汰与在途执行寿命 | PyPTO 管缓存；simpler 管本次执行持有的数据与引用 | 淘汰缓存项不能提前释放仍被执行使用的数据 |

这是 PyPTO 接入这条路径的职责划分，不要求把 simpler 的构图算法搬到 Python，也不取消 program 的 host 工作线程。program 驱动仍可调用同一份 host 构建库，再进入共同设备运行层；不因此在公共 device launch 中重复设置缓存策略。

以下为目标流程，不是已经存在或定稿的 API：

```text
PyPTO 收到本次调用
    → 选定编译产物，整理本次 tiling 输入
    → 查询 PyPTO 的 tiling 缓存
        ├─ 命中：取得已有的不可变调度方案
        └─ 未命中：调用 simpler host tiling，保存成功结果
    → simpler.kernel_launch：绑定本次参数和资源，提交到 caller stream
```

shape 从 A → B → A：首次 A 构建并缓存；首次 B 若无适用缓存则重新构建；再次 A 在缓存仍有效时直接复用。shape 变化先触发匹配判断，并不意味着无条件重新构图。

**重新 tiling 不等于重新编译。** 若同一动态 shape 编译产物支持 A 和 B，只需要分别取得 tiling 结果，不必重新编译 AICore 或 orchestration 代码。是否选择新的编译特化，仍由 PyPTO 前端已有规则决定。

### 9.5 key 由 PyPTO 构建，但必须建立在完整的 tiling 输入契约上

双方需要定义完整的 `TilingInputs`（暂用名）。PyPTO 根据该契约整理 key；simpler 将构图所依赖的目标配置、参数语义和兼容性要求显式提供出来。不能一边把 key 放在 PyPTO，一边让 simpler 的构图结果依赖未声明的可变全局状态。

建议缓存归属于对应的编译产物和目标配置。以下条件可以由缓存所属对象固定，也可以进入 key，但都必须参与有效性判断：

| 信息 | 匹配要求 |
| --- | --- |
| 编译产物/特化身份、构图及运行时 ABI | 不同产物或不兼容版本不能误复用 |
| Tensor shape、dtype、stride/layout | 影响构图的部分必须包含 |
| 对齐属性、别名关系、可访问范围及依赖语义 | 若影响任务划分、内存规划或依赖关系，必须包含；不能只比较 shape |
| Scalar | 影响构图的值必须包含；仅用于执行且明确支持重新绑定的值可以排除 |
| host-backed 控制输入 | 构图读取的内容必须按本次值区分，不能只比较该 Tensor 的地址 |
| 架构、可用核数/cluster 数及构图配置 | 影响调度或资源需求的配置必须匹配 |

初版可以保守地包含全部 Scalar，以及允许构图读取的 host 控制值，不要求先做复杂的最小依赖分析。以后只有在契约明确证明某项不影响构图、且执行时可正确更新时，才从 key 中排除。

Scalar 仍然是每次调用的运行时变量。将影响构图的 Scalar 值纳入 tiling key，只是区分不同调度方案，不是把它提升为编译期常量。

### 9.6 “相同入参”指相同的 tiling 相关输入，不是 Tensor 对象或数据完全相同

- 两次 Tensor 地址不同，但布局、构图控制值和其他影响调度的属性相同：目标是复用同一方案，再绑定各自地址；裸指针不应默认成为调度方案的 key。
- 同一个 host 控制 Tensor，地址没变，但 `num_tokens` 从 32 变成 64：若其值影响构图，就不能误命中。
- device Tensor 内容变化，但 host 构图不依赖内容：不应导致重新 tiling，也不为比较 key 而扫描 device 数据。
- 地址变化可能同时改变别名关系或对齐属性；这些属性若影响构图，仍必须匹配，不能以“地址每次重绑”为由忽略。

对于可变的 host 控制输入，**key 与实际构图必须使用同一份值快照**。不能比较时读到一个值，构建时又重新读取已改变的值。调用者仍负责输入就绪及读取期间的合法使用；缓存逻辑不会使并发修改自动安全。

如果 host 构图必须读取 device 数据才能继续，按 §7 报错并提示使用 TRB。不能为了构建 key 新增隐式 D2H、同步或 device 内容读取。

### 9.7 缓存调度方案，不缓存可被下一次调用覆写的执行状态

目标上应分成两类对象：

| 对象 | 内容与寿命 |
| --- | --- |
| 可缓存的 tiling/graph 方案 | 不可变，拥有稳定的 host 存储，以逻辑引用及明确的绑定元数据描述任务、依赖、参数布局和内存需求；由 PyPTO 管理缓存 |
| 每次执行描述与参数快照 | 当前 Tensor 地址、执行期 Scalar、真实 heap 地址及运行状态；由 simpler 的运行层绑定，并保留到实际使用结束 |

缓存命中不允许两个在途调用共同修改同一份图像、Scalar 池或 runtime/event 状态。绑定应生成本次执行需要的描述，不回写破坏共享的缓存方案。失败后重试使用本次已经确定的快照，不重新查缓存或运行构图。

当前源码提供了需要注意的事实，而不是已经完成该缓存设计的证明：

- [graph_execution.h](ARCHIVE-INDEX.md#ref-58007180014fd00a) 的 `GraphTensor` 包含 `buffer_addr` 等字段；`GraphScalarInheritance` 区分固化 Scalar 与从当前边界参数继承的 Scalar。已有 `GraphTensorSourceRef` 和 predicate 重绑定描述，但不能据此认定整个 HBG 图包已可无条件跨调用复用。
- 同一文件的 `GraphBoundarySignature` 包含布局、buffer 范围及 `alias_rep` 等信息，说明 shape 相同并不是图复用的充分条件。
- [host_graph_build.h](ARCHIVE-INDEX.md#ref-835c1eaf6601b9b9) 的 `GraphBuild` 借用 caller 的 SM mirror 和 Definition staging；构建/上传要求独占工作区，相关存储不能在结果仍使用时被下一次构建覆盖。因此，仅把现有 `GraphBuild` 对象放入缓存，并不自动得到拥有稳定存储的缓存项。

落地前需要明确哪些字段固定、哪些字段重新绑定，以及缓存产物如何脱离临时构建工作区。如果某类地址或数值尚不能安全重新绑定，就不能声称支持只按元数据匹配的跨调用复用。源码事实用于说明必须满足的契约，不把现有布局固化为新设计的唯一选择。

### 9.8 缓存不能替代异步执行、内存回收和 ACLGraph 的契约

- host tiling 缓存省掉的是重复构图。即使命中同一个方案，RTS 仍可能为每个在途 launch 保存独立的大 args device 副本；不会因此消除 §6 的排队内存风险。
- tiling 缓存不与 simpler 的 callable 驻留缓存合并，也不把 RTS 大 args 计入 8192 条目、2 GiB 的驻留资源规格。
- program 的 host 多线程能力继续保留；共享缓存项只读，本次绑定/快照独立，构建完成后仍按逻辑顺序进入设备提交层。
- 缓存淘汰与本次执行的最后使用者是不同事件。运行层仍需保留在途提交、上传或设备执行依赖的数据与引用。
- eager 或新 capture 的 host 调用会经过 PyPTO 的匹配逻辑；ACLGraph replay 不重新运行该逻辑。影响已捕获执行的 shape/控制值变化仍由调用者按 ACLGraph 契约处理，不能期待缓存自动触发重新捕获。

归纳：**PyPTO 决定“这次是否需要算 tiling”；simpler 负责“怎么算、怎样绑定和执行”。不在 kernel_launch 内再维护第二套重新构图和算子级缓存策略。**

### 9.9 保留讨论过程：从构建/执行分层到 tiling 缓存归属

以下记录问题如何演进，不用最终结论覆盖前面的疑问：

1. 最初讨论 HBG 的 host orchestrator + build_graph 应算执行的一部分，还是像 AICore/AICPU 编译一样属于 host 构建。随后区分了“职责可归入构建”与“产物并非重新编译一个 AICPU `.so`”。
2. 对“构建中途需要 device 值”的顾虑，曾讨论分段 launch、回读后恢复，以及合并 prepare/launch。用户随后明确：HBG 需要读 device 数据就报错，只能改用 TRB。因此采用纯 host 构建与设备执行分离；旧候选及撤回原因保留在 §8。
3. 用户进一步提出：HBG 更像 PyPTO 需要额外调用的 tiling 函数，TRB 不需要这一步。由此把 PyPTO 的调用组织与 simpler 提供的构图能力分开，而不是把编排重新塞进 kernel_launch。
4. 接着提出两个具体问题：shape 入参变化时，谁负责重新 tiling？两次执行入参相同时，tiling_data 的缓存 key 构建与比对归谁？本轮建议两者均归 PyPTO，simpler 明确完整输入契约、提供构图算法并处理本次绑定与执行。
5. 推导中补充了两个限制：“相同入参”应按 tiling 依赖匹配，而非按 Tensor 对象身份或 device 内容匹配；现有 graph 含地址、Scalar 及借用存储，不能直接当成可跨调用复用的不可变缓存产物。相关 ABI 和生命周期仍需进一步设计。

上一版的待决问题也保留如下，便于看出本轮解决了哪一部分：

> HBG 图模板、每次参数快照、TRB 编排 `.so` 和已加载 callable 分别由谁持有、如何缓存、何时释放？哪些变化要求重新构建，哪些只需重新绑定？

当前进展：PyPTO 接入的 tiling 缓存决策归属已有本轮结论；具体输入字段、重绑定能力、缓存产物布局和最后引用释放协议，仍列在 §12，不能把职责划分当成实现已经完成。

## 10. R7：异步返回、重试与 ACLGraph 是接口成立的条件

### 10.1 返回时必须已经建立调用者可依赖的顺序

采用分离契约后，框架侧看到的调用返回时，仍必须保证：

```text
caller stream 前序工作 → 本次 PyPTO 执行 → caller stream 后序工作
```

device 可以尚未完成，但不能仅把工作放进 host 线程队列就返回，让随后提交的普通 PyTorch 算子抢在本次执行之前。

设备提交前，本次 HBG 图必须已经完成；不设计 `kernel_launch` 返回后 host 还在补完同一张图的正常路径。program 若保留 host 队列的“已接受”句柄，该状态必须与“设备已入队”区分；不能把尚未完成构建的 host 请求冒充已经占据 caller stream 顺序的位置。

正常路径异步、host 构建耗时、首次 lazy 初始化成本、队列深度阻塞和 OOM 恢复同步应分别描述，不笼统承诺 launch 永远立即返回，也不把它们合并成每轮 stream sync。HBG 构图不再含 host 读 device 值等待这一项。

### 10.2 重试留在设备提交层，不重新构图

重试围绕已确认可恢复的实际提交步骤进行，使用同一份完整执行描述和参数快照；不退回纯 host 构建层重新运行编排，也不重放整个 PyPTO 业务调用。构建失败直接报告构建错误，不借 timeline sync 改变 HBG 的取值边界。

返回错误应保留：失败阶段、是否已有设备分支提交、是否有完成凭据、是否已安全取消、重试是否发生。错误状态和 DFX 记录需要归属到正确的 run，不能被后继覆盖。

### 10.3 ACLGraph 不能照搬 eager 语义

- 普通 host 构图代码不会在每次 device replay 时自动再次执行。
- HBG 禁止在构图时读取 device 内容，也不能在 capture 时读一次 device 值后把它固定到图里。
- 已就绪的 host Scalar/控制输入若在 capture 时决定图结构，其变化不自动传入 replay；按照已有前端与 ACLGraph 契约由调用者考虑重新捕获，不静默冻结普通 eager 调用的新值。
- 需要执行时读取 device 值来完成编排的逻辑，使用 TRB 的设备端编排；HBG 已有设备 predicate 可继续使用，但这不等于承诺新增任意动态调度表达。
- 不新增 replay 期间 host 读回与续投协议；TRB 的具体 replay 复位、地址和资源语义仍需满足目标组合的实现要求。
- eager 的 timeline 同步恢复不能直接放进 capture；captured graph 持有的参数图像也不会因为一次 replay 同步而自动释放。
- kernel 模式不控制框架的 capture 边界，不能要求框架为隐藏的分段构图重新划 capture 区间。

纯 host 构建与提交 API 独立，不等于要求普通用户在 capture 外显式 prepare。仍由 PyPTO lazy 路径组织；首次加载与 capture 的具体限制保留真实错误并内部处理，不增加必经的用户仪式。

## 11. 对原 HTML 的具体修改建议

本次不直接修改 HTML，下面作为后续修订清单。

| 原位置 | Review 意见 |
| --- | --- |
| 标题、目标及 §12 总结 | 从“没有任何深度”改为“取消人为固定槽位，以真实提交能力及资源状态承接背压”；明确 program/kernel 归一化与保留 host 并发是共同目标 |
| §1 资源分类 | 删除“当且仅当 host 碰它”的绝对判据，按版本、并发访问和最后使用者分类 |
| §1 每 run 成本 | 分清外部 device Tensor、host staging、可复用 heap、RTS 大 args 和驻留 callable；不要从一种 demo 推导全部负载 |
| §2 host 读值 | 明确 HBG 构图不得读取任何 device Tensor 内容，需要取值则报错并提示 TRB；元数据、host 已就绪输入和不透明 device 引用分别说明 |
| §2 arg 91 示例 | 补充 driver 提前写 `[T] * n_ranks` 的事实；若改成 host 必须读取的 device 数据则按新契约报错，不追加同步例外 |
| §3–4 host 并发与跨 run 顺序 | 保留 program host 多线程调用共享构建库、按逻辑顺序进入统一设备提交层；不做全局外部 tensor 来源跟踪，不新增 host 中途回读续投 |
| §4 device 控制旁路 | 按资源所有权和实际依赖排序；不能认为所有新分配/加载都必须等待前一 run 完成，也不能允许覆写共享资源越过前序使用者 |
| §5–7 生命周期、event、部分提交 | 保留；进一步区分 args 上传引用、event 代际、kernel 已提交但未记录 event、以及 OOM 恢复的可重试边界 |
| §6 跨流 join | 同时覆盖 caller stream 的入口/出口和 simpler 内部共享资源顺序，不仅是两条内部流之间的 join |
| §8 两阶段预算 | 与已选定的提交处背压、timeline sync + 一次重试重新对齐，不默认启动新的全量字节准入系统 |
| §9 watchdog 证据 | 原文实验结论只代表对应探针，不推广为全部平台保证；本轮不据此引入 host/device 交互式执行 |
| §10 实施阶段 | 按已定的构建/执行分层细化交互产物、返回契约、线程调用关系及取值报错位置，再解除不必要的串行限制 |
| 新增 API 章节 | 明确独立的纯 host 构建 API 与带 stream 的执行 API，记录不合并的决定；保留 lazy 用户体验和初始化 ready 规则 |
| 新增产物章节 | 区分 host/AICPU `.so`、ChipCallable、每次 HBG graph 和参数快照；构图可按每次参数发生，不能等同于一次性编译 |
| 新增 tiling 与缓存章节 | HBG 是 PyPTO 按需调用的 host tiling，TRB 跳过；PyPTO 负责重新 tiling 判断、key 构建/比对及缓存生命周期。明确完整输入契约、不可变方案与本次绑定的区别，不把缓存命中等同于消除 RTS 大 args |
| 新增 HBG/TRB/ACLGraph 能力章节 | 明确 device 取值相关编排走 TRB、已有 HBG device predicate 保留、不自动切换、不分段 launch、不新增 host 交互续投 |

## 12. 下一轮建议先回答的问题

1. 按 §9 的职责划分，纯 host tiling API 的输入、输出 ABI 如何定义？`TilingInputs` 如何涵盖完整的构图依赖，产物如何携带图、绑定描述及资源需求，而不是直接借用可覆写的构建工作区？
2. PyPTO 管理 tiling 缓存的前提下，初版具体纳入哪些输入、支持哪些重新绑定字段？如何细化不可变缓存方案、每次参数快照、PyPTO 编译产物和 simpler 已加载 callable 的引用及释放时机，保证缓存淘汰不破坏在途执行？
3. 构图取值入口如何区分 host 已知元数据/数值、不透明 device 引用和禁止的 device 内容读取？如何在本次业务提交前报告对象、用途和 TRB 提示？
4. PyPTO 和 simpler program host 工作线程如何调用同一份构建能力？构建并行、结果交接和有序提交之间的具体接口是什么？
5. `kernel_launch` 返回时如何保证 caller stream 上前序、本次执行和后序的顺序？program host 请求“已接受”与设备“已入队”如何明确区分？
6. 真实 device 分配、地址重定位及共享状态归运行层后，串行域按 device、context 还是某个内部执行域划分？外部跨 stream 依赖仍由调用者提供，如何在接口中说明？
7. timeline 回收等待落在哪条实际持有大 args 的队列上？部分提交时如何先获得进展，哪些错误能安全重试一次？
8. HBG/TRB 在 A2/A3、A5 的 eager/ACLGraph 路径如何落实参数快照、replay 状态及资源复用？首次 lazy 加载遇到 capture 限制时，如何保持真实错误而不增加用户 prepare 仪式？
9. 初始化防重入、共享资源扩容和 callable ready 如何实现，同时不要求 init/prepare 携带 caller stream？
10. 此次仅统一实现，还是同时重新审视同进程 program/kernel 共存限制？不能默认为后者已经获准。

先细化 1、2、4、5，再展开实现拆分。“HBG 是否允许 host 读 device 值”“是否因此合并 prepare/launch”“是否引入 host 续投”，以及“PyPTO 接入的重新 tiling、key 构建/比对和缓存生命周期归谁”已有本轮结论，不再作为未定归属重复讨论。具体字段、ABI 与释放协议仍需细化。

## 13. 资料与记录边界

- 本 review 的直接对象是本目录 HTML v6。HTML 引用的 `.docs/run-pipeline-redesign.md` 未在当前 `pto_eager` 下定位到，本文不把未读取的配套内容当成依据。
- arg 91 的生产与读取位置、README 中 MoE 读值改写，来自本地源码阅读；不是本轮设备验证结论。
- `ChipCallable`、host 编排 `.so` 与 HBG graph 的区别，以及当前 build/upload 的调用关系，来自本地源码阅读；具体新 ABI 尚未实现。
- `GraphTensor` 地址字段、Scalar 继承语义、边界签名与 `GraphBuild` 借用工作区，来自本地源码阅读。它们用于说明 tiling 缓存必须满足的绑定和存储契约，不代表当前已经实现跨调用的完整缓存能力。
- 大 args 大小、timeline 回收效果来自此前已经记录的调查，本次仅复用结论并保留平台/版本限制。
- CANN 对照使用文中链接的官方 API 与调用示例，不声称已穷尽全部 CANN 变体或内部实现。
- 较早的 [PyPTO 正式接口变更范围文档](ARCHIVE-INDEX.md#ref-68ce5b3ef3a9ad4d) 仍含“program 两槽位、不共用 kernel_launch”等旧方向；相关部分已被本轮目标取代。本次未修改该文件，其他约束不因本 review 自动失效。
- 本次修订把纯 host 构建、device 取值报错并提示 TRB、构建与提交分离更新为已明确原则；动态 `block_num` 扩展、host 交互续投、分段 launch 和合并 API 不列为本轮实施要求。
- 本轮进一步记录 HBG 的 host tiling 定位，以及 PyPTO 负责重新 tiling 判断、缓存 key 构建/比对和缓存生命周期的分工；未新增公开用户 API，未修改实现或开展性能/设备测试。
- 本文不宣称当前 HBG 已经拒绝全部 device 取值路径，也不宣称所有平台的恢复与 replay 支持已完成。后续实现应对齐这里的职责，而不是把本次文档修订当成代码修复。
