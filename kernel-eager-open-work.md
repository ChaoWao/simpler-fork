# Kernel eager 剩余问题与接线顺序

## 1. 统一路线：API 收口嵌入 K2，不另起执行器

**当前进度基线 main `63e651fcc`；交付状态以 [ROADMAP](ROADMAP.md#1-现在做什么) 为准。** #2619/#2620/#2628/#2600/#2638/#2640 已合入；当前 [#2645](https://github.com/hw-native-sys/simpler/pull/2645) 将结果区首次初始化接入既有同流发布，未初始化时禁止认证结果。#2624–#2626 分别已有 TMR eager/capture 与共同 invocation 工作，不重复其设备协议。

设计判断唯一来源仍是 [runtime-design-principles.md](runtime-design-principles.md)；全路线以 [ROADMAP](ROADMAP.md) 为入口。[API 层次审查](current-api-layers.html)记录代码事实与其在本路线的位置，不再独立维护另一条交付顺序。

```text
Pipeline A 已验收基础 + S1/S2
  → 已交付共同 native prepare/launch/完成/退休、caller lane、borrowed ChipWorker
  → K2a 拒绝与失败合同
  → K2b 借用环境/generation/source + 内部接入收口
  → K2c 公开 Worker init/submit + 双 runtime eager 验收
  → C1 可捕获设备闭包
  → C2 图资源所有权（同时依赖 S3 module owner）
  → C3 TMR/HBG replay
  → C4 换参/多图/DFX/故障/容量完整验收

S3 动态模块 owner 与能力合同 ─────────────────────→ C2
U2b Program IO / U3 Remote / U4 统一关闭：按原路线独立推进
```

K2a/b/c 是原 K2 的确定子范围，不是三个新公开对象。S3 可与 K2 并行；共同改到 ChipWorker 时协调文件与契约，不各自复制 loader 或 close 状态。S3 不是开始 K2 的前置，C2 的图保活必须消费已完成的 module owner。与 Pipeline A [总体原则](references/pipeline-a/runtime-design-principles.md)的 G1/G2/G4/G9 对齐：本线不把完整 API 分层、模块重构或全部 capture 放回 A 轨前置。Pipeline A 的未完成扩展继续独立记账，不能把本路线写成 A 已全部完成。

### 已交付的基础

- #2572：caller stream 贯穿原 ChipWorker/lane，带 caller 的 successful submit 已建立实际 enqueue 与 caller exit；不能返回 prepared-only 后放任后序读取超车。
- #2578：同一 ChipWorker.init 接 borrowed context；注册/关闭复用共同执行。错误 current context 不消耗 native run；rollback/close 失败保留绑定；close 不 reset caller。它尚未开放 Python 公共 Worker 借用入口。
- #2580：Python wait/result 的前置校验修正，保留 run id、参数快照和 source uses。已合入的 main 已由 #2581 替换回调；#2580 的证据按原 head 保存。见[验证记录](reviews/run-context-preflight/REPORT.md)。

### K2a：共同拒绝与失败合同

共同 C++ 执行层与 binding 对环境拒绝给出稳定、可机器判断的分类，沿用现有阶段与提交进度，不让上层解析异常字符串。公开 façade 仍负责自己的逻辑引用与状态变更；在提交/关闭承诺之前调用同一环境检查，native 也保护自己的状态。一个共同判定可以在两个状态 owner 的入口重复执行，不能各自发明一份规则。

| 情况 | 调用状态 / 资源 | 对上层的合同 |
| --- | --- | --- |
| 环境、参数或 capture 前置拒绝；未接受该操作 | 原有 run/handle/source 引用不变；本次未入队取得的临时引用可按证明退回 | 恢复环境或修正参数后可重试；不能 poison 或把旧 run 终结 |
| 真实执行失败，已有完整完成证据 | 发布该次终态；按各类最后使用者回收 | 重复 wait 得到同一执行错误；不能伪装成环境拒绝 |
| 部分入队或设备完成未证明 | 保留已入队资源和使用引用，隔离执行域 | 失败不是撤销许可；不能因异常已经抛出就 free/unload |
| close 已接受但 teardown 未结束 | Worker 逻辑关闭保持终态，保留环境和 cleanup debt | 后续 close 可重驱债务，不能回 READY 或换设备重初始化 |

K2a 必须覆盖 wait/result/done、submit/admission、register/unregister 和 close 的相关拒绝；不重写全部设备错误码，按真实状态 owner 拆分：

1. **#2581（已合入）：共同分类和等待消费。** `ChipWorker::validate_execution_context` 抛 `ExecutionContextRefused`，nanobind 保留 `RuntimeError` 子类，规范公开导入为 `simpler.task_interface.ExecutionContextRefused`。Python wait/result 识别该分类，不发布终态；只有这些相同负例通过后才移除 #2580 callback。C++ phase/poll/wait/close 共用已有 guard；错误消息相同的普通 RuntimeError 仍是终态，native owner 销毁后的 lane poison 也不是可重试拒绝。
2. **#2582（已合入）：Python 状态承诺边界。** 在公开 submit/admission、register/unregister、close 修改各自逻辑状态之前消费同一环境判定；不得让未接纳的 current-context 拒绝 poison source，或先关闭 façade 再发现拒绝。检查 done 的传播及 native/C ABI 注册返回路径，补齐拒绝后的恢复负例。部分入队与已经接受的关闭债务按原证明保留，不能用 blanket `except ExecutionContextRefused` 回退已经接纳的状态。

3. **#2584（已合入）：结构性汇聚。** `_operation_lease` 在持有临时 lease、释放生命周期 CV 后调用同一 native 谓词，再进入 API body；拒绝经 finally 退回 lease。所有新 leased API 默认覆盖，只有 `device_location` 显式豁免 current RTS context（它仍持有生命周期 lease）；close 保持独立承诺点。native malloc/free/copy_to/copy_from/device_memory_info/flush 也守自己的 backend entry；缓存 host counters、地址与 workspace/teardown reports 仅作观察。裸 C ABI 与 legacy HCCL borrowed 能力未扩充，不能据此声称完整 borrowed API 已资格化。

该历史节点第 3 笔已合入并完成本地验收，K2a 结项；当时 K2b 仅部分交付（当前交付状态见下节）。#2582 验证为 3222 Python UT、317 C++ targets、L2 sim 6/3 项和 task-submit 44 项；最终提交重建后 206 项通过。故障脚本独立保留失败预算，已接受的 close debt 在错误环境下保留、恢复后可清理。没有扩大 capture 或公开 borrowed 资格声明；之后进入 K2b。K2a 不新增 caller API、不交付 generation、不重排 Pipeline A 的准入和双侧完成协议。

### K2b：借用环境与内部接入——身份必须有消费者


当前 #2587 已合入：native 记录 process、非重复 incarnation、context generation、device 与 owned/borrowed 策略；source 的 wrap/descriptor/acquire 比较同一 native 身份。成功 teardown 清除，失败 rollback/teardown 保留供 cleanup retry。当前 RTS identity 仍由既有 runner 判定，不能把逻辑 generation 当 RTS reset 探测器或地址所有权证明。此笔交付时 source provider 仍是 Program Worker；后续 #2589/#2593/#2595/#2597 已闭合 K2b 的内部接入。公开 borrowed provider 已由 #2599 接线。
#2589 已合入（验证 head `27098ab9`）：direct L2 Worker 直接持有 `_ChipWorker`，既有 Worker registry、operation lease、imports、source uses 和 close journal 继续负责原有状态。此交付时公开兼容 ChipWorker 和 forked chip child 仍走 wrapper。`_initialize_chip_runtime` 仅共用日志/路径/原生 init 参数转换，不另建 executor 或生命周期 owner。#2593 已合入，沿真实 child 控制消费者收口通信句柄状态：init 成功才发布新值，失败保留旧值，alloc/release 显式消费；native 保留物理资源所有权。#2595 已移除 child 的 wrapper 穿透；#2597 已统一四处 materialization，保留快照、入口前置授权和原 registry 寿命。

在原 Worker/ChipWorker 的生命周期中建立一份环境绑定，关联 RTS context identity、进程、非零逻辑 context generation 与 owned/borrowed 权限。所有环境使用和 source attachment 都验证同一份绑定。设备 ordinal 只是选择信息，不能作为外部地址授权。

- context generation、pipeline lease generation、run_epoch、Buffer generation 分别保留语义。旧环境 source/handle 不因同 device、同地址或新 Worker 而复活。
- source-side Buffer.wrap 继续使用统一 Buffer/Tensor；submit 在目标侧授权并取得使用保留。外部 allocation 的物理所有权仍归 caller；不用 Python Tensor 引用计数冒充 allocator 保活。
- Worker 内部收口到既有 `_ChipWorker`/ChipRunLane 的一个芯片执行 interface；统一 direct L2 与 chip child 的本地 materialization 入口。公开 ChipWorker 留兼容适配，不再作为新增功能的第二个公共主接口；不添加纯转发 executor。
- 初始化内部使用共同环境请求和资源策略；既有 `simpler_init` / `simpler_kernel_mode_init` 保留兼容接入，register、run phase、退休共用。不为收口再增加第三套执行 C ABI，旧 kernel launch stub 不恢复成另一套 runner。
- K2b 不单独开放一个只能 init、不能按 caller 顺序执行的公共 borrowed Worker；K2c 接完整公开调用。

### K2c：公开接入与完整 eager 验收

同一 public Worker 的 init/submit 接资源环境和本次 caller 顺序；环境属于初始化，stream 属于一次提交。owned 默认保持既有行为，direct L2 borrowed 提交要求显式 caller stream；L3+/Remote 不能接一个本进程 RTS 指针并跨 fork/网络传递。

统一 TaskArgs/Tensor/Buffer，使用 K2a 的拒绝合同和 K2b 的环境/source 生命周期。每次 eager 重新准备并提交一次；不引入 PreparedCall、重复 submit 或整次准备缓存。HBG 每次 fresh host 编排，TMR device 编排；kernel 拒绝 tensor H2D，不能隐藏 device→host 决策再续发。

验收必须经过真实 **public Worker.init → Buffer.wrap/Tensor/TaskArgs → Worker.submit → RunHandle**，不能以 private binding 或手工桥接代替。TMR/HBG × A2/A3/A5 覆盖：caller 前序写→两侧 kernel→caller 后序读、两 caller stream 的显式连接、context/generation 拒绝与恢复、外部地址/source 关闭竞争、host 值寿命、有效 callable 保留、部分 enqueue、close debt 与不 reset caller。Program 同一路径回归；权重/KV/MTP 状态显式 IN/INOUT，不进 workspace reset。

框架有后台发射队列时，adapter 必须在同一有序队列进入 native enqueue，并保持排队期参数与 allocation 引用；调用线程直接提交的测试不代替此验收。TQ=0/1、真实 enqueue 返回合同和 caller 后序均须成立。K2c 完成后才交付公共 kernel eager 可用状态，之后进入 capture。

### S3：动态模块与能力合同——可并行，C2 必须依赖

#2607 的静态能力查询已撤回。S3 以实际代码寿命问题和消费者为交付边界，不再把版本/大小结构、能力位或 loader profile 单独作为下一笔功能。沿用现有执行入口的明确拒绝；module owner 若重构，须同时证明最后使用者退出前不会卸载、失败清理可重试。

把既有 dlopen/dlsym、required/conditional 符号、函数指针和 DSO 寿命收口到 ChipWorker 内部 module owner。它是资源 owner，不是另一个 runner，也不要求另打包一份 C++ SDK so。

静态支持与每次“现在可否 join”仍需区分，但不由此推导必须增加查询接口。同仓、同构建的边界不加 `abi_version`/`struct_size`；跨 `.so` 本身不是兼容协议的理由。旧 kernel supported 位只描述旧入口，不能作为公共 K2 交付门。

本次选择保留现有执行 C symbols，并在内部按职责分组；**不新增一张覆盖全部 API 的 get_api 函数表**。沿用既有导出与明确的操作返回值；符号存在不等于操作成功，也不因此增加一层兼容协商。run、有效 callable、graph 和在飞 replay 持有 module 资源；卸载早于最后引用是验收失败。unknown completion / close 失败须保留正确的 module 与环境，不随 C++ wrapper 析构误 unload。

<a id="s3-current-delivery"></a>

### S3 当前交付：#2638（已合入 `2a6d2d50c`）

代码提交：`aed5afd85`；基线：main `f7dbe323b`。本笔是 loader/code 生命周期的生产接入，复用原 context、lane、slot、callable cache、完成与退休，不移动整张函数表、不增加 C ABI 查询协议。51 个必需符号、18 个可选符号的名称和签名与基线逐项相同。

```text
ChipWorker.init
  → RuntimeModule：RTLD_NOW | RTLD_LOCAL + typed required/optional lookup
  → local module owner 保护 init；回滚先清未提交 binding，再退出局部 owner
  → context binding / 每个 native-run storage slot 共持同一 module
  → 原 prepare / launch / completion / retire
  → 正常 close：native carrier 结束 → context 销毁 → storage 释放 → 最后引用可 unload
  → close 失败：保留 binding，可重试
  → 析构仍失败 / carrier abandonment：代码与未知状态保留至进程退出
```

引用在 init 的预分配 slot 上建立；运行热路径不新增共享计数操作。代价是一个冷路径 module/control-block 分配和每 slot 一个 shared reference；没有性能加速主张。模拟器的进程级 GLOBAL context registry 保持独立所有权。

验证归属：构建时 HEAD 是上述 main，测试的是随后提交为 `aed5afd85` 的代码树。最终重建扩展与板测产物 SHA256 相同（`60cd7502efadcba683a94f5f98a74438131d915597b6a74be8a5ae1d180de756`）；不把构建宏误写成后生成的 commit。

- 完整非硬件 C++：338/338 test programs；完整 Python：3434 passed / 147 skipped；最终重建后相关 Python：172 passed / 82 skipped。
- 两套 callable/L2 定向 sim：A2/A3 7 passed、A5 6 passed，各自 9 deselected；不是完整 sweep。
- task-submit `task_20261019_205215_12612273481`，A3 device 0：22 passed / 107 deselected，覆盖 TMR/HBG、owned/borrowed、native depth 1/2、public TQ=0/1、mode isolation 和自动注册；无本地 A5 silicon/macOS 通过主张。
- 负对照：忽略 terminal retention 会使 DSO 单测和真实 `_ChipWorker` 析构测试失败；移除 production storage 的 module 引用会使移动/寿命测试失败。恢复后通过。编译轴与 pre-commit 全部通过。

不宣布 S3/C2/C3 完成：captured graph、有效图绑定及 replay 的代码/设备资源引用仍须在对应消费者接入。未知完成的保留不证明物理停止；本笔也不关闭 AIV 议题。产品 PR 只有实现、测试与已有开发文档，规划及日志独立备份。


<a id="c1-timing-publication"></a>

### C1 已交付：#2640（已合入 `63e651fcc`）

head `ba033efc8`，基线 main `2a6d2d50c`。这是实际 eager launch 的行为改动：删除逐调用计时复位的同步 H2D，复用 #2620/#2628 的同流 publication 与 pinned-source 生命周期。两架构与两 runtime 共用实现，HBG bulk image 仍保持 prepare 重叠，不改 Pipeline A 准入。

```text
prepare：已有 scheduler 快照 + 完整 timing reset 快照 → reserve slot pinned source
launch： caller entry → AICPU 异步 scheduler / timing / descriptor → entry fork → 两侧 kernel
完成：  实际 drain → timing readback → 原 slot 退休
失败：  enqueue error → Partial、禁止 kernel、保留 source；不读取 stale timing
回滚：  未发布的快照清空 → 同 slot 重试只携带新内容
```

可选 timing 分配失败仍仅关闭本次计时；异步 enqueue 失败属于整个 launch 的不确定入队，不能吞掉继续。启用计时时每 slot pinned 容量增加 A2/A3 2,512 字节或 A5 5,840 字节；没有时延收益测量主张。buffer/record 设备 ABI 不变，不增加 DFX flag 或公开 API。

- 非硬件 C++ 338/338、Python 3434 passed / 147 skipped；扩展最初旧基线触发收集拒绝，重建后上述全量通过。最终提交标记扩展另已重建。
- 真实 onboard 库 + RTS 桩：每架构 6 个 probe cases 覆盖两 runtime；新增探针覆盖延迟 DMA、host snapshot 先释放、复位/descriptor 失败无 kernel、未读结果拒绝覆盖、分配失败与 prepare 回滚重试。
- 基线负对照在 pending reset 断言失败。全部 pre-commit 与编译轴通过。
- 最终 head `ba033efc8` 的本地 A3 task-submit `task_20261020_002605_205382813747` / device 0 已通过：TMR/HBG × owned/borrowed，各 4 次 depth-two caller 调用，16 次 tensor 结果均正确且各日志有 4 份 device-wall 记录；四个 runtime library hash 与开测前相同。首次脚本没有初始化 TIMING logger，第一组 tensor 调用后仅在缺失 marker 检查上退出，该次不计 timing 验收；显式调用已有 logger 初始化后复测通过。没有本地 A5 silicon 验证。
- CI 的 Ubuntu/macOS UT/sim 与 A3/A5 UT/ST 均通过；network1 两次在 peer dispatcher bootstrap 初始化阶段报 `507018 / execute kernel param invalid`，还未进入本次 prepare/publication。已读取 remote plog；第三次重跑仍失败；独立 main Daily `d0d4d0eb` / run `39322278397` attempt 2 / job `118508105787` 在相同 runner、peer 上执行，5 例同样 bootstrap 失败，remote plog 也为 `execute kernel param invalid`。故失败不依赖 #2640；已登记 [#2642](https://github.com/hw-native-sys/simpler/issues/2642)，停止盲重跑。红项仍未解决，需 peer 侧继续检查，不能把 generic 507018 等同设备卡坏或已确定驱动根因。旧 merged PR 的 rerun 因 merge ref 已删除而 checkout 失败，不提供设备执行对照；不据此宣称主线也有同样设备故障。

此笔不开放 capture；首次 run-result 清零由下节 #2645 推进，其他 collector 配置和图 restore/reset 继续按真实消费者推进。#2633 的 collector prewarm 独立，不因本笔接入就视为已交付。


<a id="c1-result-publication"></a>

### C1 当前交付：#2645（待合入）

head `3753da44b`，基线 main `63e651fcc`。cold slot 的结果区原本在 prepare 同步 H2D 清零；现在 prepare 仅保存八字节 `published=0` 快照，launch 使用已有 slot pinned source，在 AICPU stream 发布，并经 entry fork 排序到 AICPU/AICore。成功 enqueue 记录该 allocation 初始化事实，读取仍须先有实际 drain；失败或跳过发布时不读未知 allocator bytes，不污染本 epoch 的 one-shot read ledger。后续 fork event 失败不撤销已经接受的初始化。warm 复用靠 epoch 区分，不新增 marker 复制。

```text
prepare：scheduler / timing / cold result marker 快照 → 原 pinned source reserve
launch： caller entry → 同流发布 metadata + descriptor → fork → 两侧 kernel
失败：  marker/descriptor 失败 → Partial，无 kernel，无未初始化结果认证
读取：  已初始化且实际 drain → 原 D2H / ledger / 终态；否则保守 fallback
复用：  cold abort 清 pending，重试新 marker；warm allocation 无额外 marker
```

每个尚未初始化的 slot 仅增加八字节 pinned source；无稳态性能主张。分配仍在 capture 外；不宣布 capture、完整 DFX 或 AIV 修复。

- 非硬件完整 C++ 338/338、Python 3434 passed / 147 skipped；两架构完整真实库 RTS 探针各 6 passed，覆盖两 runtime。新增有/无 caller、延迟 DMA、helper 快照先释放、allocation/marker/descriptor/fork 失败、prepare abort/retry、warm 复用和伪造成功结果。
- 基线在 prepare-only 初始化状态断言上失败；另删去 production read guard 后，真实库探针在匹配 epoch 的 forged-success 臂失败。恢复源码并重建后，上板两份 A3 library SHA256 与测试前一致。提交后扩展重建，A3 关键探针再次 2 passed；不把构建时 HEAD 与随后生成的提交混同。
- 定向 vector sim：a2a3sim、a5sim 各 2 passed，均覆盖 HBG/TMR，非 sweep。
- task-submit `task_20261020_040538_32659199043` / A3 device 1：34 passed，覆盖 native/public caller、owned/borrowed、depth 1/2、public TQ=0/1、prepare/finalize 与 clean refusal/retry；无本地 A5 silicon/macOS 通过主张。首笔临时脚本因 daemon 内嵌 task-submit 查询在 pytest 前被拒，查询移到任务外后上述正式任务通过。
- 编译轴和全部适用 pre-commit 通过。CI 刚启动；#2642 的历史 network1 根因仍未关闭，若再次失败须读取本次日志，不能沿用旧分类直接忽略。


## 2. API 审查与原路线的对应

| 审查项 | 归入原路线 | 必须交付的改动 / 退出门 |
| --- | --- | --- |
| R1 公共入口与 _impl 穿透 | K2b → K2c；参数部分沿 U1b | 一份内部芯片接入；public Worker 完整调用；公开 ChipWorker 仅兼容；不复制 kernel 后缀接口 |
| R2 拒绝与设备失败混同 | #2580 → K2a；C1/C4 继续消费 | 拒绝不消耗状态；真实失败终态；partial/unknown 保留；binding 保持分类；禁止字符串解析 |
| R3 owned/borrowed 与旧 kernel ABI 并存 | K2b；能力归 S3 | 共同环境策略、执行/注册/退休；旧 C symbols 兼容；不恢复第二套旧 launch，不以旧 supported 位代替 eager 验收 |
| R4 context generation 未消费 | K2b → K2c；图代际归 C2 | 环境/source/调用关联；stale 与错误进程拒绝；close debt 保留；不混用 slot/Buffer generation |
| R5 loader 与能力散落 | S3，可与 K2 并行；C2 前完成 | DSO owner + 既有导出和实际失败／保活合同；无全 API get_api 表；graph 引用阻止早卸载 |
| R6 façade 职责过宽 | K2a/b 的 run/环境；U1b/U2b 的 Buffer/IO；U3/U4 的 remote/关闭 | 随真实 owner 移交实现，保留唯一 admission/close owner；不一次性重写 Worker，不要求 U3 完成才接 kernel |
| R7 公开参数与内部 wire 混淆 | U1b/K2b；实际 device ABI 改动归 C1 | 一份公开 Tensor/Buffer；内部 ChipTensor/POD 保留；未使用 header 不冒充生产协议；producer/reader 同步迁移 |

本表只维护审查项的归属；API 层次 HTML 从本节生成同一映射。详细步骤在 §1；全路线状态在 ROADMAP，设计原则不在多处重写。

## 3. PR 拆分和可并行边界

| 顺序 | 一份 PR 的合同 | 不并入本笔的范围 |
| --- | --- | --- |
| 已合入 #2580 | 保留已复现的 Python wait/result 拒绝保护 | 不因新审查扩大此 PR；保留其代码与真实验证 |
| #2581 已合入：K2a 等待边界 | common native / binding current-context 分类，Python wait/result 消费并移除 callback；保留原 guard | 无 public borrowed 选项、无 generation、新错误码体系或完整 K2a 完成声明 |
| #2582 已合入：K2a 状态承诺边界 | submit/admission、注册与 close 相关拒绝在逻辑状态承诺前消费；未接纳拒绝恢复，已接纳失败保留 | 不撤销部分 enqueue；不为每个操作复制判定，不全面重写 Worker |
| #2584 已合入：K2a 结构性汇聚 | 共同 operation lease 在锁外校验，描述符显式豁免；native 内存/IO/query/flush 拒绝在 backend entry 前 | 无公共 borrowed init、无 generation；不改变 Pipeline A 排序 |
| #2587 已合入：K2b 环境/source | native 身份/generation 的 source 比较、失败保留及 native process 拒绝 | 公开提交等待 K2c；不改 Program IO 复制和 Remote wire |
| #2589 已合入：K2b direct L2 | Worker 直接持有原生 binding；共用 init 路径与 options；不增加 executor | child/公开兼容 wrapper 暂留；无公开 borrowed init 或 caller API |
| #2593 已合入：K2b child 控制状态 | base communicator 的控制句柄归 serve loop；init 返回值与 alloc/release 显式传递 | 不改 native 缓存/资源所有权、mailbox wire 或排队协议 |
| #2595 已合入：K2b child 原生接入 | child 直接构造 `_ChipWorker`、共用 init、原生注册/通信/提交/退休；保留 Host log flush 与 teardown 观察 | 不改 mailbox/准入/完成协议；不开放 public borrowed |
| #2597 已合入：K2b 参数 | direct L2 / child 串行和暂存提交 / 兼容 ChipWorker 共用 `_materialize_chip_task_args`；本次地址解析与 POD 构造不关闭 registry | 保留各入口快照、权限/source 引用与关闭时机；不复制 tensor 内容、不授权外部 allocation |
| 当前 PR #2599：K2c 公开接线 | 同一 public init/submit/run；借用初始化、caller stream、前置拒绝、source 保活与 close 重试 | 修订 head CI 19 通过/1 跳过、A3/A5 各 8 组合通过；框架后台队列/故障与 DFX 完整资格另记 |
| 并行：S3 | 按实际消费者交付 module/code 保活，不预设能力查询 | 不改变运行排序；不把整个函数表迁移和所有 façade 拆分塞入一笔 |

同一行若仍太大，沿真实生产消费者拆 PR，并逐笔保留不变量；目标能力与验收不能降级为可选扩展。S3 与 K2 即使逻辑可并行，仍需协调共同文件、loader/环境保留和 close 路径。U2b/Remote 不作 K2 前置，但每条新增生产复制/网络路径当笔就必须证明保活；不能拖到 U4 再补。

C1/C2/C3/C4 沿原设备布局、异步 publication、五事件闭包、图所有权、TMR reset/HBG restore、换参/多图/DFX/故障完整矩阵推进。S3 加入 C2 的 module/code lifetime 前置；不因 API 审查重做 Pipeline A、独立 prepare 缓存或现有执行器。

AIV 继续按[现场取证规划](aiv-evidence-plan.md)独立跟踪。板测使用主线 DFX、task-submit、持续外部占用采样和独立设备日志；未分类故障不因规划融合而被宣布修复。

---



## 历史：2026-10-08 的借用 RTS context 验证

基线 main `97f8b6c92`。#2507 的共同 prepare/finalize 已合入；它不开放 launch，
`supported()=0` 保持不变。原始 AIV 故障仍未定因，历史干净重跑不作为修复证据。

- 独立修复 [#2509](https://github.com/hw-native-sys/simpler/pull/2509)：scope-stats UT 在第三次准入前等待前两份发布；生产两槽合同不变。
- [PR #2510](https://github.com/hw-native-sys/simpler/pull/2510) 已更新为 `3c8cad19` 并转为 Ready for review：实际 RTS context identity 校验保留；共同 prepare/finalize 与 context close 统一执行环境前置检查，callable release 在 runner 内检查，架构清理由基类非虚 finalize 入口检查后委派。接口不再为这项校验自行分支 `is_kernel()`。
- 所有权与 Program 线程绑定的既有分支保留，本次不宣称所有 runner 模式分支已统一。Context identity 在 streams/events 关闭后仍保留；调用者须保持其活到所有资源成功 close。
- 原 head `a5a10c51` 有两个实际 RTS context 的负对照、29 项上板 lifecycle 和完整 UT/sim 验证。其两轮 Program full sweep 各有 4 个 AIV UB 故障；随后 exact-base full sweep 和 branch full sweep 通过，不能据此归因或宣称修复。主动 AIV 复现已按用户要求暂停，见[调查记录](probes/hbg-aiv-trigger-20261008/REPORT.md)。
- 修订基于 main `60167bcc`（含 #2509）。当前 297 项 C++、2990 项 Python、两套 sim sweep、29 项上板 lifecycle 通过；正常 Program 全量回归仍出现 4 个同签名 AIV 故障，后续 L2 HBG/TMR 通过。已封存现场及产物，23 份 runtime hash 与开测时一致。新 head 的 CI 重新开始，合入由用户 review 后决定。见[本次修订记录](reviews/pr2510-context-refactor/REPORT.md)。设计与现场资料留本地。
- 之后才是 caller-stream 实际 launch、每次调用的保留与退休、context generation 消费和公开入口；capture 仍在 eager 验收之后。

以下是此前各固定版本的证据与缺口，不把旧版的“当前”措辞当作本节的新主线状态。

## 2026-10-08：#2501 后的依赖修正与未完成工作

核查 main `121d807bb`。#2501 已合入。U2b 的 Program IO 记录整理不是 kernel eager 的前置：
DEVICE/NONE 不生成自动搬运记录；现有 TensorLease 继续作为 Program 本次 HOST/H2D 绑定记录。
不新增公开 Lease/Tensor/Buffer，不以通用 COPY 框架替代真实生产接线。

| 工作 | 处置 / 验收 |
| --- | --- |
| **当前：借用设备下的共同调用准备** | TMR/HBG 接已有 NativeRunContext、workspace、参数绑定和 prepare/finalize；复用现有 run identity；拒绝 kernel H2D；不 reset caller 设备；准备失败正确清理。工作树 borrowed-kernel-preparation。 |
| **随后：实际 launch 与 caller 顺序** | 仍有拒绝 stub；定义 context generation 的保留/验证消费者（不能混同 slot generation）；接 caller→双侧执行→caller 完成及错误/退休，复用现有执行归属，不能仅翻 supported 位。 |
| **随后：公开 kernel 入口** | 统一 Tensor/Buffer/TaskArgs；外部 context/source 接入、host 值寿命、有效 handle 保留。现有 ChipWorker 仅加载 kernel 函数指针，尚无生产调用。 |
| Program IO / U2b | 暂缓独立结构整理；现有输入/输出复制与失败规则可随真实消费者收口。HBG HOST/NONE 可改同源 backing，不能无证明地把 H2D 从 host 编排前移到后。 |
| Remote / U3 | producer/receiver/version、identity/grant/location/transfer 同步升级；网络→H2D 的完成与保留仍待验收；非 kernel eager 全量前置。 |
| 生命周期 / U4 | 各接入路径当场保证安全，最终统一 owner/import/attachment 关闭与未知完成隔离；有效 handle 与 graph 保留不可省。 |
| Capture / C1–C4 | eager 通过后接异步 metadata、设备闭包、图地址/源保留、双 runtime replay、换参与错误；不复活旧 PreparedCall/cache 方案。 |

当前代码证据：kernel launch 恒拒绝；HBG kernel sizing 恒 UNSUPPORTED；prepare_execution
调用 ensure_device_initialized，而 attach_current_thread 拒绝 kernel；kernel init 的 contract
校验后丢弃且 context_generation 未保留。现有 persistent device KernelArgs 没有 launch 消费者，
不把它当下一步 ABI 的强制前提。KernelEventKind 五事件也尚无生产 record/wait 链。

规划和 HTML 保持仓库外。此记录是状态与依赖修正，非宣称实现已完成。

## 实现进度

[草稿 PR #2507](https://github.com/hw-native-sys/simpler/pull/2507)，提交 `2ac0d906`（rebase 至 main `f16b6bb42`），分支 `feat/borrowed-kernel-preparation` 已接通两个 runtime 的共同 `simpler_prepare_run` / `simpler_finalize_run`。
A2/A3 实板两臂通过：DEVICE/NONE、HBG HOST/NONE 每次重新编排、H2D 无副作用拒绝、metadata 复制失败清理、
错误 current device 下 finalize 保留并可重试、阻止 outstanding run 期间 close，且无设备所有权 API 调用。
294 个 C++ UT、2909 个 Python UT、29 个 A2/A3 生命周期用例通过；A2/A3 sim 和 A5 sim 全 sweep 通过。
Program 上板 full sweep 的四个 HBG 用例发生 AIV UB 地址越界/随后 scheduler timeout；
同一组设备上四用例对比，#2501 基线与本分支都通过。原始故障仍未定因，见本地 worktree 的 KNOWN_ISSUES.md；
同一旧 head `d0cb7503` 的第二轮 full sweep 已通过，另有 10 轮四用例（40/40）通过、无目标故障签名；原始故障仍未修复、没有稳定复现。
新 head 重建后 294 个 C++ UT、54 个相关 Python UT 和 29 个上板生命周期用例通过；全 sweep 数据对应 rebase 前版本。
[复现调查](probes/hbg-aiv-sweep-repro-20261008/REPORT.md) 保留脚本、逐轮结果与设备日志。

现有 kernel arena 首次 commit 后禁止增大/释放仍保留；本步不声称已实现容量扩展、caller-stream launch 或 capture。
init contract 仍只验证、不保留；`context_generation` 仍只验证非零。两者的真实执行消费者归入接下来 launch 的范围，
本步没有为“保存一个值”新增无消费者的状态字段。

当前只校验 current device；外部 RTS context identity 的接入、context generation 的实际消费，
以及图/调用地址保留须在 launch 接线前闭合。这一步并没有交付完整 kernel eager。

[完整验证与原始日志](reviews/borrowed-kernel-preparation-d0cb7503/REPORT.md)。

## 上板故障排查约束（2026-10-08）

下次出现 AIV/507018 等设备故障，须核对是否有绕过 task-submit 的进程占用相同设备。
测试前、复现期间及故障当时保留带时间戳的队列与 NPU 实际进程快照，并核对设备映射、PID/PPID 与自身 Worker 子进程树。
持有 task-submit 锁不能替代这项核对；退出后的空进程表也不能排除当时的抢占。
原始四个故障缺少当时的 PID 对照，尚不能排除或确认该原因。


### #2612：准备拒绝的 source 退休（已合入）

原验证 head `e18ff1064deaa976267b5c75004517fc73ae4da4` 从原 native host slot 返回本次 epoch 的清理证明，经同一 lane、公开 Worker 和兼容 ChipWorker.run 消费。干净拒绝只释放本次引用；未知/失败清理保留引用并拒绝后续 native admission。Linux Python 3415 passed、C++ 327 targets passed；两 runtime 实际 C ABI 与同 Worker/Buffer 重试的 6 个 A3 用例通过。最终提交双 sim 全量通过（61/57 个 Resource 场景及各自 L2）；A3 完整 C ABI 38 passed，普通 sweep 的 151 个 Resource 场景及两套 L2 均通过，锁已释放。CI 未完成，不将本地 A3 结果当作 A5 上板结论。详见[证据](reviews/prepare-refusal-source-retirement/REPORT.md)。


2026-10-17：#2612 已 rebase 到 main `0eda81c9`（#2611），新 head `de709728`。只合并两段 C API 合同说明，并为主线新增的两条干净失败测试补充 exact-epoch 证明消费；不改变原 PR 产品语义。新 head 的 Python 3415 passed / 115 skipped、重新构建的 C++ 327 targets、A3 状态/预算 12 场景与 C ABI 38 项均通过。新 CI 重新运行；旧 head 的完整 sim / onboard sweep 保留原归属。


### #2614：NotStarted launch 的 source 退休（已合入）

准备成功后的启动 setup 失败也可能明确没有入队任何执行任务。已合入实现复用同一 host-slot epoch 证明、RunPreparationRefused 和 source 消费者，闭合真实 event 创建失败后的同 Worker/Buffer 重试；保留原错误，partial/unknown/清理失败不获得新证明。TMR/HBG × owned/borrowed 一起验证，详见[本轮证据](reviews/unstarted-launch-source-retirement/REPORT.md)。#2605 继续负责框架 adapter，#2588 继续负责 DFX，不重复各自 driver 或 collector 工作。


### 已合入 #2615：启动异常后的执行所有权

CPU 负对照确认：launch backend 若在入队后、返回 ActiveExecution 前抛异常，不能从空 active 指针推断无设备工作。当前 A3/A5 返回前的 ActiveExecution 分配是需审计的真实抛异常位置；应在 launch 提交前建立 owner，或在异常路径保留并隔离已提交事务的所有权。#2614 只保证此类异常拿不到新的 no-submission 证明，不宣称修复旧 finalization 的全部保留行为。已合入 #2615 覆盖真实消费者、异常前后进度与资源保留负对照；该模型不是 AIV 根因证据。


当前实现与验证见 [启动异常所有权报告](reviews/launch-exception-ownership/REPORT.md)。共同 wrapper 在后端前分配 ActiveExecution；具体 backend 只借用 PreparedExecution，返回事务结果。Partial/异常仍由原 drain 路径决定实际完成，不能误发未提交证明。


### 已合入 #2618：fork 后的 run/lane 拒绝

主线 `ce493639` 的三个确定性 C++ 回归均被子进程 watchdog 终止：父进程线程在真实 lane 内执行 blocking wait，fork 时 lane mutex 被继承，子进程的 RunHandle 操作、lane 操作和 Worker 析构分别卡住。既有 native 环境检查发生在这些锁之后，不能防止该死锁。

实现 head `996628a9` 保存 lane 的不可变创建 PID，在读取可变 worker 指针和获取 lane mutex 前拒绝外来进程；noexcept stop/detach 留下继承副本，既有 Worker 析构放弃路径保留 carrier/DSO，不调用父进程的设备清理。覆盖 live/terminal handle、两类 submit、查询/等待/关闭以及析构；父进程继续完成并提交下一调用。它是 K2 进程拒绝的实际缺口，不是 S3 loader 全面重构或 capture 支持。验证及边界见[本轮报告](reviews/inherited-chip-run-lane/REPORT.md)。


### 已合入：共同 Runtime descriptor 的异步 eager 发布

PR [#2620](https://github.com/hw-native-sys/simpler/pull/2620) 已合入 main `f50f90076`，审阅 head `e23f46e8` 的实现将 TMR/HBG 的 descriptor 发布都置于共同 launch 事务，使用 slot 保留的 pinned host source。可选 caller Start → AICPU wait → descriptor async H2D → Fork record → AICore wait → 两个 kernel；没有 caller 也必须建立真实复制后的 AICore 依赖，cache 命中且没有 caller 则不增加 fork。DMA 尝试标记 Partial，资源仍由原 active owner、drain 和 quarantine 处理；不增加接口版本、能力查询或另一个 runner。

这是普通 eager 的实际行为交付，不提前宣告 C1/C2/capture 合格。HBG 大图像仍在 prepare 同步上传并可与前驱重叠，DFX 原路径不变。完整框架资格继续归 #2605，DFX 归 #2588。源代码核查同时更正了历史文档：TMR workers 已在尾部，稳态 prefix 不再写握手，不应重复要求改一次物理 ABI。见[实现与验证报告](reviews/async-runtime-descriptor/REPORT.md)。

#2620 复审修订 `e23f46e8` 补齐三份运行时编译探针的 async/pinned RTS 桩与新签名，更新 arming 失败前尚未发布的相位断言，并删除已闭合 A5 helper cell 的 waiver。完整非硬件 UT 与硬件标记探针是不同覆盖面；原 head 的 sweep 结果不重标为修订 head。详见同一报告的 Review correction 小节。


### 当前 #2628：A5 HBG scheduler metadata 进入共同 launch

每次 HBG bind 的 resident scheduler metadata 经 HostApi 按值保存，prepare 预留 slot pinned 源，launch 以 metadata→descriptor→Fork 顺序入队。大块 execution image 保持 prepare 上传；Graph Definitions 不另做 H2D。失败与源保活消费 #2620 同一事务与 completion proof，不增加接口版本、公开准备句柄或执行器。PR、成本、板测与 CI 的边界见[本轮报告](reviews/hbg-async-launch-metadata/REPORT.md)。
