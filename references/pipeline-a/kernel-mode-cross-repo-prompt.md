> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# Kernel mode（eager / capture）跨仓交付提示词

核对日期：2026-10-21 UTC。用途：复制到协调者或接收仓库的任务中，推进真实用户功能落地。本文是任务交接与证据快照，不宣称下列 open PR 已合入或完成跨仓验证。本次核对未运行构建、测试或设备任务。

## 1. 先统一边界与已核验事实

**kernel mode 是借用调用方已有 device/context，并把整个算子的执行排到调用方 stream 上的生命周期；eager 与 capture/replay 是它的两种调用路径。** 普通 program mode 的异步 submit、HBG 内部 Graph、编译器图都不能据此称为 native CANN capture。HBG 与 TMR 是两个 runtime，不能互相套用专有 ABI。PyPTO 提供编译器及其框架适配层，不能把它写成 Simpler runtime 本身。

### 当前基线

| 仓库 | 核验的 main SHA | 使用方式 |
| --- | --- | --- |
| simpler | `7503761a6c01a6d07ccd8d9c82b8060da843d1af` | 下列 main API 结论的基线 |
| pypto | `390a75363f06dd5e538dcb749b92742a20962bde` | 已查 main tip 与相关 open PR；接收方仍须审计实际适配入口 |
| pypto-lib | `d2a3aaa850e13f043bae50682f9d5d20c32cf335` | 已查 main tip 与关键词匹配 PR；未宣称完整集成 |
| pypto-serving | `2d4b7e371360c4f54cf4b0dce5a34c17e0dbe5f5` | 已查 main tip 与关键词匹配 PR；未宣称完整集成 |

接收任务时重新读取各仓 main、全部相关 open issue/PR、CLAUDE/AGENTS、包 pin、子模块与 ABI 检查。不要把此快照的版本号设成产品硬编码要求，也不要把关键词搜索当完整 open PR 清单。

### 证据矩阵

| 能力/问题 | 当前来源与负责人 | 能证明什么 / 交付限制 |
| --- | --- | --- |
| 统一注册、execution mode identity | [#2619](https://github.com/hw-native-sys/simpler/pull/2619)，作者 zxx0288；已合入 `f7dbe323bb9ab6321c6c8b0d688f87d773f9ac8b` | 当前 main 的既有所有权，不得另造注册表抢占 |
| 借用 context 的 native eager | [main C ABI](https://github.com/hw-native-sys/simpler/blob/7503761a6c01a6d07ccd8d9c82b8060da843d1af/src/common/worker/runtime_c_api.h#L1339) | main：kernel init；`simpler_prepare_run` → `simpler_launch_run_on_stream` → poll/wait/finalize。prepare 在 capture 外，外部 tensor/stream 保活到 finalize |
| legacy capability 的含义 | [main capability 注释](https://github.com/hw-native-sys/simpler/blob/7503761a6c01a6d07ccd8d9c82b8060da843d1af/src/common/worker/runtime_c_api.h#L1394) | `simpler_kernel_mode_supported` 描述 legacy dedicated launch，main 仍为拒绝 stub；零不等于 native eager 不存在。sim 不支持 kernel init |
| TMR eager invocation 校验 | [#2624](https://github.com/hw-native-sys/simpler/pull/2624)，qweasdzxcht；OPEN；head `e9c16b047179059e5256e7faae22d08e2f025ef2` | [不可变实现](https://github.com/hw-native-sys/simpler/blob/e9c16b047179059e5256e7faae22d08e2f025ef2/src/common/tensormap_and_ringbuffer/kernel_invocation.h)：snapshot tensor 描述符/scalar，校验 ID/generation/Runtime/epoch；不要把新增强化说成 main 从未有 eager |
| TMR native capture/replay | [#2625](https://github.com/hw-native-sys/simpler/pull/2625)，qweasdzxcht；OPEN；head `7848e06e020fb8fc60fb55167c2336a8ba622a1f` | [契约文档](https://github.com/hw-native-sys/simpler/blob/7848e06e020fb8fc60fb55167c2336a8ba622a1f/docs/design/tmr-kernel-capture.md)：依赖 #2624；onboard TMR、slot 0、diagnostics off、一个 lease/一个 model、同一 caller stream。框架适配、多图并发、跨流 replay、capture DFX 不在该 PR |
| 共用固定资源 | [#2626](https://github.com/hw-native-sys/simpler/pull/2626)，zxx0288；OPEN；head `f701832b091290d3db9f20eabe678700a4e7a043` | [资源头文件](https://github.com/hw-native-sys/simpler/blob/f701832b091290d3db9f20eabe678700a4e7a043/src/common/platform/include/host/kernel_device_resources.h)：prepare/freeze/close、binding snapshot；基础设施不等于生产 launch |
| HBG 构图、packet、restore 模块 | [#2646](https://github.com/hw-native-sys/simpler/pull/2646)，zxx0288；OPEN；实际 head `be63699b446aadba387580f8d188236b0bd7a22b` | [容量契约](https://github.com/hw-native-sys/simpler/blob/be63699b446aadba387580f8d188236b0bd7a22b/src/common/host_build_graph/kernel_resource_requirements.h)：模块层不独立提供生产 launch；PR body 仍写旧 `1f19b009`，必须核对新 head 与依赖包含关系 |
| A2/A3 HBG dedicated 生产闭环 | [#2647](https://github.com/hw-native-sys/simpler/pull/2647)，zxx0288；OPEN；head `8f1a054d96fc7cbd46458905e07105f77f4758d5` | [C ABI](https://github.com/hw-native-sys/simpler/blob/8f1a054d96fc7cbd46458905e07105f77f4758d5/src/common/worker/runtime_c_api.h#L1516)：dedicated launch、retain/release graph；caller 拥有图与 tensor；diagnostics 拒绝；A5 dedicated 与 TMR dedicated 不在该支持范围 |
| kernel DFX | [#2493](https://github.com/hw-native-sys/simpler/pull/2493)，nalinaly；OPEN；head `536cf3eea9e4324351891a8059d0879a1cf7b4f5` | base 是旧 integration 分支 `17e342455e2e8d9112c0613b6c9cd6eca79e71d0`，不是 main；[注册 ABI 改动](https://github.com/hw-native-sys/simpler/blob/536cf3eea9e4324351891a8059d0879a1cf7b4f5/src/common/host_build_graph/kernel_callable_registration.h) 88→96 字节、v1→v2，必须迁移/重审，不能直接宣布 #2647 已支持 DFX |
| kernel async prefetch | [#2583](https://github.com/hw-native-sys/simpler/pull/2583)，TaoZQY；OPEN；head `97343062d08ad2ce4fac73cc17e42180972da603` | 同样基于旧 integration `17e342455`；[init ABI](https://github.com/hw-native-sys/simpler/blob/97343062d08ad2ce4fac73cc17e42180972da603/src/common/worker/runtime_c_api.h) 有变动；范围为 A2/A3 onboard HBG/TMR，A5/sim 不可外推 |
| HBG scope mode 并发 | [#2654 issue](https://github.com/hw-native-sys/simpler/issues/2654) → [#2652 PR](https://github.com/hw-native-sys/simpler/pull/2652)，qweasdzxcht；OPEN；head `f2bacd9b1f7d02cac79763cbb35c65ae59fce912` | [scope 回调](https://github.com/hw-native-sys/simpler/blob/f2bacd9b1f7d02cac79763cbb35c65ae59fce912/src/common/host_build_graph/host/orch_ops.h)：Host AUTO/MANUAL 请求隔离；这里的 Graph recording 不能当 native capture 实现证据；Host/orchestration 配套重建、重启进程 |
| 输出目录 | [#2655](https://github.com/hw-native-sys/simpler/pull/2655)，doraemonmj；OPEN；head `f99aabafb9eb8f67146b8dccaf3131dc1e46c357` | [Worker 改动](https://github.com/hw-native-sys/simpler/blob/f99aabafb9eb8f67146b8dccaf3131dc1e46c357/python/simpler/worker.py)：`output_root` / `capture_output_prefix` 是 PR 接口，不可写成当前 main 保证 |
| 历史 cold second-callable capture | [#2255](https://github.com/hw-native-sys/simpler/issues/2255)，报告者 lyfne123；无 assignee | 旧 integration branch reproducer；当前约定注册在 capture 外，不能要求接收方在 capture 内首次注册而称为既有契约。是否扩展为产品需求应明确另开范围 |
| DFX 生命周期设计 | [#2202](https://github.com/hw-native-sys/simpler/issues/2202)，报告者 ChaoWao；无 assignee | 历史设计议题；不把旧树诊断当当前 main 缺陷证明，也不把“resident services”当 capture DFX 已交付 |

上表 PR author 是已有工作联系人，**不等于新增任务已获其承诺**。无 assignee 的 canonical issue 由协调者指定接手人后记录，不能擅自接管现有作者分支。

### 特别需要刷新的一组跨仓依赖

- [PyPTO #3001](https://github.com/hw-native-sys/pypto/pull/3001)，YunjiQin，OPEN，head `ed612fbdb7e321013fc4324bd2a6febd86df2234`，target integration branch，公开 `pypto.torch.init(enable_sdma=True)` 是这个分支的事实，不能当当前 main API 示例。
- [PyPTO #3084](https://github.com/hw-native-sys/pypto/pull/3084)，TaoZQY，OPEN，head `f6b22488404c5a542806a660f88c137047f3ea5a`，target integration branch。最新 body 明确已 squash 包含 #3001 feature，依赖 simpler #2583。因此禁止机械执行旧的“#3001 后再合一个 fix-only #3084”描述；先协调重叠再决定保留哪组提交。
- [serving #274](https://github.com/hw-native-sys/pypto-serving/pull/274)，lyfne123，OPEN，head `a416406cffe5ce8209d7e9570713694cb27ae699`，描述 explicit backend/execution mode 配置；其提及的 [PyPTO #2975](https://github.com/hw-native-sys/pypto/pull/2975) 已 MERGED。显式配置本身不证明 kernel capture 端到端可用；接收方重新审计。
- 所有既有验证数字归属于 PR 描述中的源码/tree 与环境。本交接不转述性能数字，也不把旧组合栈验证转换成当前 published head 的通过证明。

## 2. 可直接复制：总协调者提示词

关联：[四仓 HBG × Async 整体验收 #2658](https://github.com/hw-native-sys/simpler/issues/2658)。该 Issue 负责系统验收，kernel eager/capture 的实现依旧由各仓原作者及既有 Issue/PR 承载；不要给相同实现再开第二任务。

> 你负责把 kernel mode 的 eager 与 native capture/replay 做成跨仓真实用户功能，涉及 simpler、pypto、pypto-lib、pypto-serving。先读取各仓当前 main、规则、相关 issue/PR 和版本依赖，建立“main 已有 / open PR / 历史 integration / 待实现”的矩阵。以本文件为发现线索，以最新源码为裁决依据。
>
> 用户交付目标：调用方已经有 device/context、输入输出 NPU tensor 与 stream；能够通过已核验的公开适配入口编译/加载算子、eager 调用，或在支持的目标上 capture 并多次 replay；输出与状态正确，流间依赖明确，异常与关闭不会释放仍被使用的资源。普通 program Async、内部 Graph、单算子 toy 成功不等于此目标完成。
>
> 保持现有作者边界。先关联 canonical issue 与现有 PR owner，再分派缺口；不得复制另一作者整条分支或重做其正在实现的注册、资源、ABI 所有权。必要的跨仓新 issue/PR 仅在当前授权范围内创建，不能以本文作为自动远端写入授权。
>
> 分开推进两条 runtime 路线：TMR native eager 强化 #2624 → native capture #2625；HBG common #2626 → modules #2646 → production #2647。这些是逻辑依赖，不是已核验的新 head 祖先关系。先核验真实 commit graph，尤其 #2646 更新后的 head 和 #2647 是否包含它；以实际合并后的 main 为集成基线。
>
> 单独处置旧 integration #2493 DFX、#2583 prefetch 的移植与 ABI 冲突；它们不能绕过新主线生命周期。与 PyPTO #3001/#3084 作者协调重叠。在基础 eager/capture 功能未具备时，不以 DFX、prefetch、性能工作掩盖缺失路径。
>
> 输出一张任务表：仓库、canonical issue、已承诺 owner、所需上游能力与 immutable SHA、文件边界、公开入口、产物/ABI/stream/内存契约、验证矩阵、消费者准备条件、阻塞项。没有确认 owner 的写“待指定”；没有已核验命令的写“先发现现有测试与 help”，不发明 API、CLI flag、版本下限。
>
> 对每条承诺能力记录完整证据：source commit/tree、编译器及工具链版本、wheel/binding/Host/AICPU/AICore 身份、设备架构、runtime、eager 或 capture、实际命令、原始日志、输出比较结果、异常/释放结果、未验证范围。更新后的 commit 不自动继承旧 CI 结论。
>
> 完成标准是消费者在受支持的 main/发布组合上完成真实用例，而非每仓各有一个 open PR。若只能使用临时组合栈，准确标为集成候选；不得宣布 main-ready 或全平台支持。

## 3. 跨仓必须共同签收的契约

| 契约 | 必须回答的具体问题 |
| --- | --- |
| 编译产物 | PyPTO 产出哪种 orchestration/AICore artifact，谁打包 ChipCallable，func_id 与参数 schema 如何关联？artifact 清单带 target/runtime、编译配置与来源身份；不把裸源码等同已注册 callable。Simpler [layout 实现](https://github.com/hw-native-sys/simpler/blob/7503761a6c01a6d07ccd8d9c82b8060da843d1af/src/common/task_interface/chip_callable_layout.h) 是核对入口 |
| ABI 配套 | C ABI header、binding、Host runtime、AICPU/AICore image、generated orchestration 与缓存键配套。旧 ABI artifact 如何拒绝/失效？#2493/#2583/#2652 各有不同配套变动，不能只更新 pip pin |
| 注册与身份 | 使用既有统一注册与自动 ID；显式 ID、callable generation、context generation、run/replay epoch 不混为一个计数。错误/未完成注册不能发布可用 handle；旧 graph 不得通过复用 ID 复活 |
| context 与 stream | 谁提供 current context，谁拥有 caller stream？提交前后怎样排序整个算子？内部 AICPU/AICore stream 与 caller join 覆盖何时完成？成功 enqueue 不等于 output ready；借用路径不得重置调用方 device |
| 输入输出 | 参数方向、dtype、shape、stride、scalar 位宽、alias/in-place、输出分配者逐项声明。native eager 不隐式搬 tensor；TMR capture prepare 固定 descriptor/scalar，replay 可改变同地址内容，不能假定 Python 对象变动会改已录制参数 |
| capture 生命周期 | 初始化/编译/注册/容量规划放在允许的 capture 外阶段；TMR 用 prepare→record→wait→release 的 lease；HBG 使用自己的 graph token，不互换 token/API。外部 model/graph 构建销毁由适配方负责 |
| 释放与错误 | graph 不能再 replay且完成已证明后才 release，然后关闭 context/释放 tensor。失败 wait、partial enqueue、timeout 不证明物理停止；保留 poisoned 资源并按契约重试，不以报错作为 free/reset 许可 |
| 资源与并发 | 固定容量超限的明确拒绝、稳定地址、准备就绪与 launch 就绪区分、同 context 的 native/dedicated 排他；TMR 单 lease/同流限制可见。并发/多 graph/跨 stream 是独立未交付能力 |
| 可观测性 | eager 诊断与 capture 诊断分开声明；基础 capture 当前 diagnostics off。DFX 产物包含源码/运行归属，目录跟随实际版本的公开接口，#2655 合入前不能强制 `Worker(output_root=...)` |
| 平台矩阵 | A2/A3、A5、simulation 分列；Host 架构与 CANN/torch_npu 组合单列。sim refusal、CPU ABI 检查、PR 作者硬件结果、接收方端到端实测分开记账 |

**TMR PR 已核验的 C API 名称**：`simpler_kernel_capture_supported`、`simpler_kernel_capture_prepare`、`simpler_kernel_capture_record`、`simpler_kernel_capture_wait`、`simpler_kernel_capture_release`。来源：[完整头文件](https://github.com/hw-native-sys/simpler/blob/7848e06e020fb8fc60fb55167c2336a8ba622a1f/src/common/worker/runtime_c_api.h)。这些是 PR C ABI，不承诺存在同名 Python 函数。

**HBG PR 已核验的 C API 名称**：`simpler_kernel_mode_launch`、`simpler_kernel_mode_retain_graph`、`simpler_kernel_mode_release_graph`。来源：[完整头文件](https://github.com/hw-native-sys/simpler/blob/8f1a054d96fc7cbd46458905e07105f77f4758d5/src/common/worker/runtime_c_api.h)。retain/release 保护资源生命周期，不代替外部图的创建/销毁。

## 4. 可直接复制：各仓接收者提示词

### simpler：运行时能力交付包

> 阅读当前 main 与 #2619、#2624、#2625、#2626、#2646、#2647 真实 diff。与既有作者协调，完成 runtime-specific 的 native eager/capture 契约及公开能力发现；不能只凭 legacy supported 位判断 eager，也不能用 HBG 模块存在替代 launch 已可用。
>
> 提供两个可消费的最小调用样例与精确测试入口：TMR lease、HBG token，各自明确 initialization/registration/capture/replay/wait/release 顺序与支持平台。将已有 C ABI 映射到实际 Python/框架适配入口；没有映射则登记为适配缺口。
>
> 合并前核对 stacked diff、真实祖先和当前 main 增量；单独处理旧分支 DFX/prefetch ABI。交付 normal program 兼容性、borrowed eager、first capture、重复 changing-content replay、stale ID/context、失败释放与后续 context 恢复证据。能力拒绝必须可预测且不破坏调用方设备。
>
> 消费者准备条件：所选 API 已存在于消费者使用的确切构建，配套 binary ABI 一致，支持矩阵与失败语义可由适配方执行，不能仅转交 PR 链接。

### pypto：编译与框架适配交付包

> 先审计当前 main 的 compile/load/runtime adapter、framework bridge、版本/pin 检查，以及 #2975、#3001、#3084。区分编译器 IR/代码生成和 borrowed-runtime 执行。不要直接复制旧 integration 的 `pypto.torch.init` 示例到 main 文档。
>
> 输出生成 artifact → schema/target 校验 → Simpler 注册 → caller stream eager/native capture 的实际入口映射。若某段不存在，把它作为本仓待实现功能并给出文件边界；不得靠 mock 通过就宣称端到端完成。
>
> 实现或完善 adapter 对 input/output/alias、stream/current-context、artifact/cache ABI、capture 生命周期与关闭的约束；预编译 artifact 与 JIT artifact 都要覆盖。确定多 callable 是 capture 前统一准备，还是新增 cold-inside-capture 需求；后者关联 #2255，不默认已有支持。
>
> 对 #3001/#3084 去重并重基于批准的 runtime 组合；只有用户场景确需 prefetch 才把它纳入该 feature 的必要依赖。拒绝不支持的 SDMA/platform 组合。交付 eager 与 replay changing-input/state 正确性、错误参数与 stale artifact 拒绝、关闭/失败重试，保留普通 program 路径回归。
>
> 消费者准备条件：pypto-lib 可通过文档化实际入口消费 artifact 和运行模式，使用配套 simpler 构建；测试依赖中不能暗藏未记录 integration branch。

### pypto-lib：真实算子与模型状态交付包

> 读取本仓 current main、依赖版本、现有模型/算子入口和相关 open PR，确认已有作者后选定一个真实消费者用例。不要把运算库变成新的 native runtime 或另造图/注册所有者。
>
> 以当前实际 PyPTO adapter 接入 eager 与支持的 capture/replay；列清输入、输出、临时 buffer、cache/KV/state 的所有权。数据依赖与状态更新必须在 replay 间正确，descriptor/shape/地址变化触发哪些重建要明确拒绝或实现。
>
> 使用同一算术和初态比较普通基线、kernel eager、capture replay。至少覆盖真实链式多算子、重复 changing-content、in-place/state、输出 guard、artifact reload、失败退出；不能只以一个 vector toy 代替用户场景。性能测量若另有授权再做，本任务没有性能承诺。
>
> 消费者准备条件：提供 serving 能调用的稳定入口、shape/布局/状态 contract、capture 前准备步骤、输出就绪语义、准确的平台矩阵；尚不支持的动态路径有明确行为。

### pypto-serving：请求级闭环交付包

> 读取本仓 current main、配置/CLI/model runner、依赖 pin 与 #274 等相关 open PR。复用既有作者的 backend/mode 配置工作，核对 PyPTO #2975 已合入的实际能力；配置可选不等于 native capture 可跑。
>
> 把已验证的 pypto-lib/adapter 入口接入一个实际请求路径。清楚定义 worker/context 生命周期、warmup 与 capture 时机、graph cache key、batch/shape 变化、KV/state 更新、输出消费、请求失败/取消、服务关闭。取消请求不能提前 free 仍在飞行的设备数据。
>
> 交付真实请求端到端 eager 与 capture 结果一致的测试：重复请求与不同内容、状态隔离、动态 shape 策略、异常和关闭。单设备先建立证据；多卡/多进程、并发 graph、跨流 replay 未验证的必须保持未支持状态。
>
> 消费者准备条件：发布组合有可复现安装与启动说明；用户无需手动替换私有 .so 或依赖未记录分支。若暂时只能用指定 integration SHA，明确标成候选并记录退出该临时依赖的 issue。

## 5. Merge gates、发现命令与完成标准

### 依赖门禁

1. **源码门**：刷新 main/open，记录准确 head/base；确认已合入的 #2619 不被重复携带；修正 #2646 旧 body 与新 head 的差异。
2. **runtime 门**：TMR #2624→#2625 与 HBG #2626→#2646→#2647 各自独立完成必要验证；接口与支持范围无歧义。两个栈组合后的文件交集需审阅，不能假定独立 PR 都绿就组合无冲突。
3. **ABI 门**：若迁移 #2493/#2583/#2652，审计组合 ABI、artifact cache、配套构建与进程重启；新二进制不得混用旧 in-memory callable。
4. **adapter 门**：PyPTO 的真实 adapter 对批准 runtime 组合完成 eager/capture，而非仅 compiler CPU 单测；#3001/#3084 去重后再排合并顺序。
5. **消费者门**：pypto-lib 用例、serving 请求各自通过输出/状态/生命周期验收；未合上游能力使用明确候选 pin，不伪装 main-ready。
6. **发布门**：当前 main/发行依赖可重现，公开文档与命令和代码一致；平台未测项保留可见，DFX/prefetch 作为分别验收的能力。

### 已验证可用的只读发现命令

以下为本文实际使用的 GitHub 读取形式。替换编号/仓库后先核对目标；它们只发现事实，不会构建或跑设备：

```bash
gh api repos/hw-native-sys/simpler/commits/main --jq '{sha:.sha,date:.commit.committer.date}'
gh pr view 2625 --repo hw-native-sys/simpler --json number,title,state,url,body,headRefOid,baseRefOid,baseRefName,author,files
gh pr view 2646 --repo hw-native-sys/simpler --json headRefOid,baseRefOid,body
gh issue view 2255 --repo hw-native-sys/simpler --json number,title,state,url,body,assignees
gh pr view 3084 --repo hw-native-sys/pypto --json state,headRefOid,baseRefName,body
```

运行命令由接收方从其 checkout 的测试、技能、CLI help 提取并先验证参数。本文没有执行端到端测试，因此不提供猜测的 pytest 路径或运行 flag。设备任务遵循各仓架构预检/隔离与资源队列规则。

### 完成定义

- 每个 feature 有 canonical issue、确认的 owner、批准的目标平台/runtime 与用户入口，缺口不会散落在聊天里。
- eager 与 capture/replay 在对应入口真实运行；输入输出与跨调用状态正确，enqueue/completion 区别可见。
- 每种已宣称支持的平台均有对应证据；拒绝的组合有明确结果，不能用 simulation/别的平台替代硬件证明。
- 初始化、注册、artifact reload、重复 replay、错误传播、失效 ID/graph、partial enqueue、失败释放/重试、正常关闭均有与实际风险匹配的验证。
- 上游 merge 或 ABI 变更后，配套构建和消费者测试更新到最终候选；实际执行的版本、命令、原始结果可复查。
- 真实 serving 请求完成后，图、buffer、worker/context 按契约回收；不能以“无异常返回”替代结果和生命周期验证。
- 所有未完成项列为明确问题：当前 Python adapter 的真实入口、#2646/#2647 栈同步、旧 integration ABI 移植、capture DFX、A5 HBG dedicated、多 graph/跨流、cold second-callable、动态 shape、distributed 支持与发布策略。未验证不是已失败，也不是已通过。

来源说明：上文不可变源码链接及 PR/Issue 链接构成公开证据入口；完整读取快照保存在本地审计记录中。链接中的 SHA 固定本次事实来源，接收任务时需刷新。
