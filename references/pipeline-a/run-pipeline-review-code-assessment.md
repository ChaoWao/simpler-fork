> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# Pipeline review 意见的源码核查

> **历史设计 / 评审快照**：保留文中的原始基线、结论与验证记录，不代表当前待办或支持范围。当前进度统一见 [路线与阶段表](run-pipeline-track-a.md) / [HTML 看板](run-pipeline-track-a.html)。

基线：`748c39fbe`。对象：`run-pipeline-redesign-review.md` 与原设计第六版。
这是静态核查和设计建议，未修改 runtime、未执行测试，也未验证另一 checkout 的 RTS 探针。
本文不将 review 中“用户已明确”的记录直接当作本分支已有能力或已经批准的兼容性变更。

## 结论

review 对资源版本、构建/执行职责、参数缓存和 caller stream 顺序的修正值得融入。
但它同时引入了 HBG 能力收缩、kernel 接口变更和跨调用 tiling 缓存，不能全当作解除两槽位的必需前置。
其中 RTS 大 args 与 timeline 回收依赖另一实现，不能取代当前 program 路径的资源管理。

| Review | 处理建议 | 源码核查后的限定 |
| --- | --- | --- |
| R1：初始化不必接收 caller stream | 融入依赖原则，细化接口变更 | 当前 init 已无 stream；prepare 则明确要求非空 stream 且承诺不 sync |
| R2：按版本、最后使用者分类 | 融入 | 修正原设计的 iff 判据；不据此立即合并 Runtime/handshake 存储 |
| R2：保留 host 并发、有序提交 | 融入，明确并发层次 | 单次构图内部已有并行 recorder；同一 SO 跨 run 并行构图不具备相同安全条件 |
| R3：区分内存池、提交处背压 | 融入分类；恢复方案有条件采用 | 当前图像是 simpler 自管分配，非整包 RTS args；队列满的实际行为未验证 |
| R3：用 timeline sync + 一次重试替换预算 | 不直接采纳 | 可作为特定提交实现的恢复策略，不能覆盖当前 prepare 分配和保留池 |
| R4：HBG 全面禁止读取 device 内容 | 不按存放位置直接采纳；按入口值与本次图内 producer 区分 | 当前允许读取已就绪的入口 tensor；本次图内存在 producer 的读取已报错，orch 不得依赖本次 device 执行来完成 |
| R5：不做中途回读续投、不合并构建与 launch | 融入本次范围 | 当前 HBG 本就完整构图后执行；无需为取消两槽位新增分段执行 |
| R6：纯 host tiling + 运行层绑定 | 融入目标结构，细化拆分 | 当前 prepare 混有分配、H2D、host 读写、构图及地址重定位；不是抽出函数即可 |
| R6：PyPTO 管 tiling 缓存 | 作为接入层策略；不是 pipeline 前置 | 当前无完整顶层可重绑定产物；现有 Graph Definition 缓存是另一层 |
| R7：返回时建立 caller 顺序、区分 capture | 融入 | 当前 program 的 accepted/staged 不等于 kernel 接口承诺的 enqueued；capture 引用可能跨多次 replay |

## 1. R1：stream 表达顺序，不能只按“是否接触 device”划分

源码：

- `src/common/worker/runtime_c_api.h:603`：kernel init 已经不接收 stream，并声明不做 stream/device sync。
- `src/common/worker/runtime_c_api.h:623`：prepare 在 capture 外，可向 caller stream 提交初始化，承诺不 sync。
- `src/common/platform/include/host/kernel_entry_validation.h:66`：prepare 的空 caller stream 被明确拒绝。
- `src/common/platform/onboard/host/device_runner_base.cpp:428`：kernel 模式禁止已提交 arena 扩容/释放，依据是捕获图仍保存地址。

应融入：新资源的创建不必等待无关前驱；初始化必须有 ready 条件；重建共享资源必须等待其真正的使用者。
原 §10.4 对 malloc 等一律等待，是沿用整体 FIFO 的保守策略，不是所有新分配都天然依赖前驱。

需细化：若改为无 stream 的 prepare，必须同步更新声明、验证、调用者和 ready 协议；冷路径同步也是现有接口承诺的变更。
已经捕获的图持有资源时，仅等待本次执行结束不足以允许扩容。不能把 eager 的最后一次提交完成与 graph 最后引用混为一谈。

## 2. R2：资源版本原则成立；“可共享”不能由一个字段推及整个结构

原设计 §1 的“host 碰它 iff PER_RUN”与自己的只读 callable 共享条目矛盾，应替换。
建议规则：后继准备是否可能覆盖仍有使用者的版本；最后使用者包括 device、回拷、状态读取、event wait 和 captured graph。

源码：

- `device_runner_helpers.cpp:60,95`：当前 device Runtime 与 KernelArgs 本就是独立分配、上传；后续可以池化，但必须保留独立参数版本。
- `device_runner_base.cpp:224`：Graph Definition block 和 staging 按 slot 保留，扩容会释放旧 device block。
- `device_runner_base.cpp:402`：共享 arena 扩容会释放旧地址，失败还可能回滚其他旧 region。
- `src/common/platform/include/host/run_stream_pair.h:31`：AICPU/AICore 是两条相互配合的流，单条 FIFO 不能证明全部共享状态安全。

应融入：不可变代码/方案、每次参数、执行期 scratch、结果和同步对象分别描述寿命。
“不可变参数 + 串行共享执行状态”值得作为布局目标，但 Runtime/handshake 拆字段前仍保留当前隔离；共享化需要逐字段读写和复位证明。

## 3. R2/R6：保留内部并行不等于同一 callable 可重入构图

这是 review 尚未具体处理的代码约束。

- `src/a2a3/runtime/host_build_graph/orchestration/common.cpp:34` 和 A5 同文件：`g_current_runtime` 是 SO 内普通全局变量。
- `src/a2a3/runtime/host_build_graph/host/runtime_maker.cpp:707`：每次构图调用 `entry_points->bind(rt)`，然后调用同一已加载入口。
- `src/common/host_build_graph/graph_recorder_pool.h:25`：recorder 依靠任务所属 SO 的这个全局变量找到当前 runtime；不是每个 job 独立携带完整绑定。
- 同文件 `:125`：wait 等待整个池清空，并非只等待某个 build 的任务。

因此，保留一次构图内部的多线程是现有能力；两个 build 同时重绑定同一个 SO，则可能把 N 的 recorder 导向 N+1 的 runtime。
这是扩大并发后的风险，不是声称当前串行 bind 已出现故障。

建议先明确合同：同一已加载 callable 的 build 串行，保留其内部 recorder 并发，build 仍可与前驱 device 执行重叠。
只有确实要求跨 run 并行 build 时，才增加显式 BuildContext、job 绑定和按 build 完成等待。
不能简单把全局改成 TLS：recorder 线程也需要绑定，而且代码记录了选择非 TLS 的动态加载原因。

## 4. R3：内存分类应补上当前的 simpler 自管图像；timeline 不能替代回收

源码实际路径：

1. `runtime_maker.cpp:1200`：host-backed Tensor 先 device_malloc/H2D。
2. `runtime_maker.cpp:485,510`：Definition block 由 simpler 分配并单独上传。
3. `runtime_maker.cpp:851,875`：构图后计算 image/heap 大小，提交 simpler 自有 arena。
4. `device_runner_base.cpp:1542`：AICPU launch 传 `sizeof(KernelArgs)`。
5. `device_runner_base.cpp:1778`：AICore launch 参数只是指向 device KernelArgs 的指针。

因此本分支没有“整个 HBG 图包随 launch args 交给 RTS 管理”的当前路径。
review 的 RTS 大 args 行应标为目标或外部分支实现；当前资源表还必须列出 simpler 自管的 runtime/SM image、Definition block、staging 和结果。
`src/common/task_interface/callable_protocol.h:31` 当前 callable id 上限是 64，不是 review 的 8192；2 GiB、2 MiB、80 MiB 和约 2048 条目均不能移作本分支常量。

预算与回收应分开讨论：

- 代码证明需要安全回收和压力出口，不证明必须引入完整的字节预算系统。可保留池化、回收空闲块、等待相关完成、有限重试及最终错误，不必暴露 depth。
- 当前 retained pool 不会仅因 stream sync 自动释放，必须显式 finalize/回收/淘汰。RTS timeline 回收策略覆盖不了这些块。
- 原两阶段预算是对“构图前已做 staging”的当前顺序推导。如果真正拆成纯 host build，再在 bind 阶段统一分配，则很多大小可以在第一次 device 分配前确定；两阶段不应写成永久契约。
- actual launch 遇队列压力会阻塞还是返回哪类错误，本轮没有设备证据；不能承诺靠 CANN 自动排队就覆盖全部背压。

OOM 一次重试可作为有条件的运行层机制，但需列出支持的错误和状态转换。
`src/common/worker/native_run_execution.h:126` 已区分 NotStarted/Partial；`device_runner.cpp:562` 是 AICore 先、AICPU 后。
AICPU 提交失败后，已排队的 AICore 可能等待它；把 timeline event 排在后面不能产生进展。
即使恢复证明设备静止，也不自动证明业务未产生副作用。只在可证明未执行或有专门安全恢复协议时重试，不重新运行 host 编排。

## 5. R4：入口值可读，本次图内 producer 的结果不可读

按用户澄清和代码核查，HBG 的关键不变量是：orch 必须在本次 device 执行开始前独立完成。
入口 tensor 存放在 device，不等于其读取依赖本次 device 执行；构图读取本次任务的结果才会造成无法前进的依赖。

- `src/common/host_build_graph/host/host_tensor_access.cpp:123`：child-memory 可映射到 host；`:134` 读不到映射时调用 copy_from_device。
- `src/a2a3/runtime/host_build_graph/host/runtime_maker.cpp:1182`：主动注册 child-memory region，供构图取值。
- `tests/st/a2a3/host_build_graph/paged_attention/test_paged_attention.py:121`：`small1_child_memory` 明确包含 context_lens/block_table 的 host 构图读取。
- 该用例 orchestration `paged_attention_orch.cpp:155,181` 实际读取这些值。
- `src/common/host_build_graph/host/runtime_core.cpp:121` 的 require_no_producer 同时检查 owner_task_id 和 TensorMap 中的重叠区域 producer。入口 tensor 如果已经被本次图记录的任务写入，也会被拒绝，不因其入口身份而放行。
- `tests/st/host_build_graph_validation/kernels/orchestration/validation_orch.cpp:50,65` 分别覆盖本次 task 输出的读取，以及本次 task 写过的入口 tensor 的 host 写入拒绝；读写共用同一 producer 守卫。

应融入：保留已就绪入口值的读取能力；本次图的中间结果或已记录 producer 的入口区域，构图取值直接报错，不插入 sync、分段 launch 或 host 续投。
这里的“写过”是已经在图中记录 producer，并不需要等到 device 实际写入；当前正是还没执行，所以等待本次任务不能产生进展。
已有设备 predicate 不受影响，因为它在执行期消费数据，不要求 host 构图提前取得结果。

上一轮把“orch 不依赖本次 device 执行”与“任何 device 内容都禁止读”放在一起讨论过于宽泛。
review 的全面禁读是比现有不变量更强的限制，不能作为这条原则的必然推论采纳；如果另要求构建函数完全不调用 device API，应单独定义入口值快照如何提供。
前序调用产生的入口值是否已就绪，属于调用前的输入顺序问题；不能据此要求 HBG 支持等待本次图内 producer。
dsv4 arg 91 在 `examples/a2a3/tensormap_and_ringbuffer/deepseek_v4_flash_decode/main.py:678` 的确分配 host buffer 并写入 `[T] * n_ranks`，这个具体例子无需新增等待。

## 6. R4/R6 的遗漏：set_tensor_data 是可观察的构建副作用

review 对读值的分析还不足以建立可缓存纯构建。

- `src/common/host_build_graph/host/runtime_core.cpp:177`：set_tensor_data 调用 host_tensor_write。
- `src/common/host_build_graph/host/host_tensor_access.cpp:151`：写 caller host buffer 后推送 device，或直接写映射；无映射时调用 copy_to_device。
- `tests/st/a2a3/host_build_graph/available_aicore_counts/kernels/orchestration/available_aicore_counts_orch.cpp:75`：构图把实际核数写入外部 shape tensor。

仅把 H2D 延后仍未解决问题：缓存命中跳过 orchestration，会跳过本次应发生的外部写入；对新 buffer 的调用可能没有初始化。
若 host 控制 Tensor 在 build 内被改写，构图前缓存 key 的快照也不能代替写入效果。

设计必须选择并记录：纯 tiling 禁止修改外部输入，或者把支持的写入变为返回产物中的显式初始化描述，由每次 bind 按序执行。
后一种方案还要定义 host 可观察写入的时机、别名、读后写及失败语义；不能用复制一份图像来代替。
初版可只缓存确认无此类副作用的构建，其他调用继续每次构图；禁止静默跳过写入。

## 7. R6：构建/绑定拆分有代码支撑，整图缓存仍是独立工作

已有结构可利用：

- `runtime_maker.cpp:637` 用虚拟 heap 构图，真实 heap 在后面申请、重定位；规划与提交已有可分离的阶段。
- `src/common/host_build_graph/graph_execution.h:53,95,114` 已有 Tensor 地址、source ref 和 Scalar 继承描述。
- `src/common/host_build_graph/host/orchestrator.cpp:1923` 对 Graph Definition 的复用检查布局、范围和 alias partition；只看 shape 不足。
- `runtime_maker.cpp:613,657` 借用 runner 保留的 SM mirror 和 Definition staging；产物寿命不能等同于一个返回对象的寿命。

应融入：构建返回独立拥有存储的不可变方案与绑定描述；运行层生成本次参数版本，持有到最后使用者结束。
完整 key 要覆盖 callable/ABI、目标配置、相关布局/alias、构图 Scalar 与 host 控制值，同一份输入快照同时用于 key 和 build。
不能只修 GraphTensor 地址字段：顶层 task、子图、predicate、Scalar 和 heap 地址都要有可检查的重绑定规则。

PyPTO 管算子级跨调用 tiling 缓存是合理的接入策略，但当前仓库源码不能单独证明这个组织选择必需。
它不要求删除 simpler 内部本次构图的 Graph Definition 去重/缓存，也不要求 program 路径依赖 PyPTO 才能执行。
先提供可每次调用的构建/绑定接口即可推进 queueing；跨调用 cache 可以后置，不必成为解除深度 2 的前置条件。

## 8. R7：统一的是执行机制，接入合同仍须分别保留

- `src/common/worker/runtime_c_api.h:639`：kernel 返回 0 明确表示已入队，参数是外部 device 地址。
- `python/simpler/worker.py:3262`：program 的 FRAME_STAGED 可以只是 VALIDATED_ONLY，尚未 native prepare，更不等于入队。
- `src/common/hierarchical/orchestrator.cpp:315`：任务可 dispatch 仍受 active run 限制；`:554` 的直接控制也有独立完成等待。
- `src/common/platform/onboard/host/c_api_shared.cpp:1253`：当前 kernel capability 返回 0，后续接口是拒绝执行的占位实现。
- `src/common/worker/runtime_c_api.h:535`：已有模式、设备/ACL 所有权及禁止 sync 等合同，不因复用代码自动消失。

应融入：kernel 路径返回前建立 caller 前序 → 内部执行 → caller 后序的依赖；program 保留 accepted/staged/enqueued/completed 的区别。
两条内部流互相 join 不等于已经与 caller stream 对接。
共用执行核心是合理目标，但不能描述成复用本分支一个已经成熟的 kernel_launch 实现。
保留 program 的设备/进程管理与 kernel 借用资源的不同接入层；不由实现统一推导同进程共存。

ACLGraph 需独立规定 captured graph 的引用寿命、replay 复位、参数更新、同一共享 scratch 的排序和初始化限制。
原每次 execution 的 fence 回收规则不能直接覆盖图的下一次 replay；eager 的 OOM sync 也不能直接套用 capture。
这些是目标 kernel 接口的实现条件，不是当前 program queueing 已验证通过的属性。

## 9. 建议如何更新原设计

立即融入：资源版本规则、分开描述内存所有者、accepted/enqueued 区分、caller stream 顺序，以及“构建计划/每次绑定”的目标结构。

实施前细化：

1. 构建的读写合同：保留已就绪入口值读取，拒绝本次图内 producer；另行处理 set_tensor_data 的初始化副作用。
2. Build 产物所有权、可重绑定字段和不支持缓存的条件；初版不强制缓存。
3. 同一 callable 的构图并发合同；保留内部 recorder，不默认支持跨 run 重入。
4. program/kernel 共同执行核心与各自接入合同；完整 caller stream join。
5. 当前 simpler 分配与未来 RTS args 各自的回收/压力出口；错误白名单和部分提交状态表。

暂不融入为既定事实：8192/2 GiB/2 MiB/2048/80 MiB 参数、timeline 同步必然回收、全平台 eager/capture 恢复保证，以及旧 HBG 功能可无代价切 TRB。

原 S0/S1 的共享资源安全、event 引用及失败回收仍有价值；S2 应按最终 build/bind 顺序重写，不能继续把两阶段预算当唯一方案。
L3 顺序和 mailbox 生命周期改造仍需按真实 program 路径处理，不会因命名为 kernel_launch 自动消失。
先明确哪些工作服务于 program 自然排队，哪些服务于新 kernel 接入，避免把完整 tiling cache 和所有 ACLGraph 组合捆成同一个交付前置。

建议验收补充：host/child-memory 入口值可读、图内 producer 结果拒绝；缓存命中时 Scalar/host 控制值/alias 变化；每次初始化写入；同 SO 并发或显式串行保证；caller 前后序；各类分配压力与部分提交失败；捕获图跨 replay 的资源寿命。
