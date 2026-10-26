# 两套方案的借鉴评估

> **发布快照：2026-10-26。** 设计、实现状态与历史调查须按正文各自基线阅读；不代表当前 main 全量验收。参见[阅读入口与已知更正](../README.md)。

日期：2026-09-28。性质：来源评估与取舍记录；当前吸收的目标约束以 [访问与退休契约](../worker-memory-model/access-retirement-contract.md)、[用户面](../worker-memory-model/user-surface.md) 和 [分线 C/D](../track-boundaries.md) 为准。下文保留评估时的建议口径；工作项状态只见 [work-items](../work-items.md)。

## 口径与结论

代码对照固定为 `upstream/main@05b5c19a036b3e077bb947d59393cd6baae4beca`，用 `git show upstream/main:path` 读取。本次是定点设计对照，不推进全目标扫描基线。

用户给出的 `../hw-native-sys/kernel-mode` 在本环境不可达；实际参考目录为 `../hw-native-sys/docs-kernel-mode`，其中参数政策、访问契约与准备/提交设计均可读。Pipeline A 的参考 `.docs` 是独立目录，按只读资料使用；维护者记录只写主 checkout 的 `.docs`。

这些设计文件是本地资料，不能用所属仓库 HEAD 证明内容版本。文末记录逐文件 SHA256；其中 K 代码证据自报代码基线为 `c605b03cad7be2a80700efbdf3bd00dbc79812a9`，A 内存原则自报 `47de5bb168f3b9c9351317b8c1b7517ed6873272`，均不能直接充当当前实现结论。

**最值得借鉴的是跨阶段的证明义务，而非新增管理器或实施线。** 完成事实、最后使用者、部分提交与实生产者验证可直接细化既有验收。源侧 `Buffer.wrap` 的正式接口以及契约 D 的组织方式属于需明确采纳的设计建议，外部文档的“决定”不自动变成本看板的裁决。

## 建议及唯一归属

 | 借鉴点 | 主要来源 | 归属与交接 | 建议性质 |
 | ------ | -------- | ---------- | -------- |
 | 完成事实分别证明，未知静止时保留资源 | K 访问契约 §3；A 执行协议 §3、§5 | DS-3 的复制；RA-15 的使用保留；EX-7 的提交/退休；RA-16 的 workspace | 既有验收细化 |
 | 源登记与目标 attachment 分离，view 无搬运副作用 | K 参数政策 §1.4、§6.1.2 | RA-13 公共 ABI、RA-14 调用迁移；Remote 接入由 DS-3 承接 | 公共接口建议，待采纳 |
 | 生命周期合同统一，allocation owner 与 use/grant 引用分清 | K 映射契约；A 内存原则；本地 GE-R1/R2 | 契约 D：RA-15 ↔ RA-12，域保留交 DS-4 | 对待决问题的建议答案，非裁决 |
 | 按最后消费者与实际字节记账，容量不授予复用权 | A 内存原则；K 准备/提交；A 原则 G3/G4 | RA-16；EX-7 消费账本；RA-7 保持 HeapRing 边界 | 既有验收细化 |
 | 全调用/全路径预检，部分副作用不可假装回滚 | K 参数政策 §6.2；K 访问契约 §3；A 执行协议 §3 | RA-3/RA-13 的本地 consumer、DS-3 的远端路径；退休交 RA-15/RA-16 | 随 consumer 交付的安全门 |
 | 用真实 producer 与滞后 consumer 验证复用边界 | A DFX 验证；A DFX 矩阵；K 访问契约 §6 | EX-7/RA-16 联验；DFX 自身交付保持 work-items §7 外部归属 | 验证方法，不新开 DFX 工作项 |

### 完成不能压成一个成功标志

对多段复制明确三个逻辑事实：

- `SourceReadDone`：这一段不再读取源，只允许解除这一段的源保留。
- `DestinationReady`：目的完整且对消费者可见，允许依赖它的数据消费。
- `OperationQuiescent`：所有已发起步骤不再碰资源，允许失败清理；并不证明数据成功。

另行记录 run 业务结果与执行域健康。业务失败、资源已静止、执行域可继续接单，可以是不同组合；`event` 完成也不自动证明数据成功。三个事实不要求新增三个物理 event。

对 `H_A → network → H_B → H2D → D_B`，send 返回不足以放行 H2D；H_B 保留到 H2D 不再读它；D_B 的使用保留还覆盖真正的 device consumer 与必要的输出回拷。每个完成证明绑定本次 operation 和 endpoint/session/context generation，不能拿上一轮同地址的完成记录放行本轮；backing generation 不因每次复制递增。

**关键反例**：第二段失败、接收端 commit 滞后、旧完成记录晚到。与之配对的正例是成功接收后放行、H2D 结束后释放 H_B，以及连续两次复制保持同一 backing identity。提前 enqueue 只有在数据可见性与失败阻断均有证明时成立；否则在既有 host 调度处等可用后再提交。

### 公共入口：借鉴的是身份与接入分层

K 参数政策最新文本提出 `Buffer.wrap(address, nbytes, location)` 在源侧登记、`Tensor(buffer, ...)` 构造 view、目标 Worker 在 submit 时校验并建立 attachment。它与 #2475 评审所针对的 `Worker.borrow_device_buffer` 入口存在明确接口差异。

我建议采纳这个分层方向：同一 backing 跨 Worker 保持 identity，目标地址和 grant 属于 attachment；构造 view 不分配、不复制、不 mint 新 backing。`location` 必须携带足以区分源 endpoint/context generation 的事实，只有 device ordinal 不够。

**它不是方法搬家。** 仍需有效源登记、授权校验、与 close 原子协调的使用保留，以及外部分配寿命合同。裸 VA 包装不会自动保活调用方的 allocation，Python wrapper 存活也不是 device 完成证明。`buffer.tensor` 是否仅保留迁移期兼容、移除的边界，都应随正式接口裁决明确。

\#2475 的 mixed owned/external 失败路径问题是独立安全缺口，见 固定 head 评审（本地参考：`pr-2475.md`）（`959f84a96f5c`）；不能用等待 API 裁决或等待 U4 代替当下 consumer 的安全要求。本报告不把外部资料中的接口修订决定追认为本地已获批准的要求。

### 契约 D：建议统一权威，不强求一张物理表

建议把 D 的选择标准写成：**一套逻辑关闭、接纳使用、最后使用证明与失败重试规则；清理义务可组合且恰好履行一次。** allocation owner 仍是原 allocator/session/context；map/import handle 管自己的 attachment/grant；graph 与 replay 只是持有相应使用引用。

内部可以有不同类型的索引或 registry，但不得各自发明互不协调的 owner lifetime 与 close journal。反过来，强行塞进一张表也不能证明关闭正确。这是从两套方案推导出的建议，不是来源已经替本板答完 D。

必须在 D 中明确的反例：源逻辑关闭后哪些独立 grant 仍可用；关闭一个 map handle 不得撤销另一个有效 grant；unmap/free 失败保留原平台句柄和清理义务；graph 仍持有使用时不能因一次 eager stream 完成就释放。独立 grant 的行为须受各自授权合同约束，不能概括为“源关了所有访问都失效”或“所有旧 view 都能继续 submit”。

这与本地 GE-R1/R2 的 owner 边界一起核对；graph 的更长使用期不自动建立第三种 physical owner。D 继续保持待裁决。

### 内存与流水深度：容量、占用和最后使用者分开

A 内存原则的分类比“每 run 一块 workspace”更适合作为评审清单：提交值快照、构图 scratch、DMA upload 源、异步控制与完成记录、常驻 collector/cache，各自写入者和最后使用者不同。同一 frame 内的数据字段与状态字段也可能在不同边界退休。

建议 RA-16 的账本检查同时覆盖：活跃块、旧版本、被 graph 保留的块、释放失败块、对齐开销，以及适用的 HOST/pinned 与 DEVICE 独立预算。统一核算不要求 provider 分配都来自同一物理池，也不把用户 Buffer 或 RA-7 HeapRing 收编为 workspace。

EX-7 则分别说明 accepted 请求数、host prepare 容量、native prepared 数、已 enqueue 数和实际执行并发。一个“depth”值不足以说明资源何时可覆盖。K 准备/提交的 publication bank 数与每 graph/node event 公式是该后端的机制选择，不能提升为所有 Worker 的全局常量。

当前 main 的 `src/common/platform/include/host/workspace_manager.h` 已有 `BlockState`、`ReleaseOutcome`、`RunFact`，明确区分 quarantine、free 失败、drain 尝试/完成、copyback 与 bindings 释放；它是复用起点。该事实不证明所有 consumer 已接入，也不能写成“缺一个 workspace manager”。

### 预检及部分提交：先记录清理义务，再发布异步工作

全调用预检覆盖所有参数的结构、方向、身份、权限与几何；多段路径在首个 DMA/send 前确认下游必要资源已有保留。随后在受保护的绑定点重核 live/generation 并取得使用引用，避免预检与 close 之间的竞争。

预检通过仍不能消除执行期失败。已发送数据、已 enqueue 的 wait、已 launch 的 kernel 都是不可撤销的事实，释放路径必须据实处理。A 方案特别有价值的反例是：**本 run 的 kernel 尚未 launch，但等待前驱 event 的命令已提交**。此时仍需保留前驱 event；`NotStarted` 不代表没有在飞引用。

建议验收包含：后一个参数不合法时前一个参数尚无数据搬运副作用；kernel 已发出但 fence record 失败；只提交 wait 后 launch 失败；混合 owned/external 后完成未知；取消后旧 DMA 仍在运行。对应正例必须证明正常完成后确实可以回收，防止只靠永久泄漏“通过安全测试”。

当前 main 的 `python/simpler/worker.py` 已有 `_cleanup_journal.add_once` 以及 teardown 的 native/remote/child 义务登记；不因提出这些验收就重开 EX-6 已完成的全集 journal 工作。

### 验证：实际生产者与慢消费者，比手填结果更有用

A 的 DFX 验证方案要求调用真实 producer/collector 入口和实际 wire layout，不能让测试直接写入期望字段。复用验证至少走三轮：第一轮产物未消费时第二轮不得覆盖；消费释放后第三轮能够复用；持有的第一轮 host 副本仍保留原身份与内容。

建议把该方法用在 DS-3 中间 buffer、RA-16 bank 与 EX-7 请求快照的联验上。CPU fake 能证明协议顺序、引用和拒绝路径，不能证明 NPU cache visibility；需要硬件证明的结论单列，不把模拟器与板上 allocator 内/外地址视为同一种证据。DFX 产物何时可读取也要明确：若 API 已承诺 run 返回时产物完整，不能借异步化暗改为“后台最终会写完”。

## 已有覆盖与不宜照搬的部分

单一 Buffer/Tensor、TaskArgs 的调用级 transfer、整组快照、关闭后保留已接受使用、资源按最后消费者退休，已有对应 RA/DS/EX 归属。应补强交接与反例，不新建平行参数模型、执行器或资源管理层。

A 的历史 `run-pipeline-redesign.md` 仍把 tiling cache/build API 列为 B 轨方向；K 当前原则明确选择每次 eager 重新 prepare、一个执行上下文只 submit 一次，不引入 PreparedCall 的重复 submit 或整次自动缓存。应以当前目标原则界定借鉴范围，不能把两个年代的选项拼成共同前置。

Capture 可以提供反向约束：某操作在 eager 可用，不代表 host RPC、接收回调或 Python 调度可被 device replay。未具备合法 bridge 的网络工作在图外完成，并显式证明输入就绪。该约束不改变 kernel 模式仍在三线外的裁决，也不要求本板承接 kernel 的 event 编排实现。

## 来源快照

以下链接是只读来源；摘要只锚定这些内容哈希。来源里的 PR 状态、测试数字及旧代码缺口不作为本板当前交付证据。

 | 来源 | SHA256 |
 | ---- | ------ |
 | A 原则（本地参考：`../../.claude/worktrees/pipeline-a-manager/.docs/runtime-design-principles.md`） | `2db712250850cd48c22c53147fefdf0f69be35c3c1abf1d26a72d7e00be37cb3` |
 | A 内存原则（本地参考：`../../.claude/worktrees/pipeline-a-manager/.docs/workspace-memory-principles.md`） | `be29720196e5fbba72a3830d1d67754dc0602b30af7124563d5e9bcfac0ab76c` |
 | A 执行协议（本地参考：`../../.claude/worktrees/pipeline-a-manager/.docs/run-pipeline-track-a.md`） | `45c2786d349ad864ee4b2a6bd4bc9fba51f15aee8eb5eb32c71943aee470bc82` |
 | A 历史重设计（本地参考：`../../.claude/worktrees/pipeline-a-manager/.docs/run-pipeline-redesign.md`） | `915316c7ce4d5842aa1a7b0d50f9d70885d79a6c8d6dde0999577184523636c4` |
 | A DFX 验证（本地参考：`../../.claude/worktrees/pipeline-a-manager/.docs/dfx-run-identity-verification-plan.md`） | `8d6b1c3ef2853e2e0139064a0a496eb88d10f2fc549a511badd28625db80ed51` |
 | A DFX 矩阵（本地参考：`../../.claude/worktrees/pipeline-a-manager/.docs/dfx-continuous-collection-matrix.md`） | `2db54067fd669ab15869a44fa32a40a72a07fae10e5d0bd7d32886c36c2cf3fe` |
 | K 原则（本地参考：`../../../hw-native-sys/docs-kernel-mode/runtime-design-principles.md`） | `c05d5c2f3413664c46d994ead4e8cb0f853ed971285f1f168f88ef671901ad0b` |
 | K 参数政策（本地参考：`../../../hw-native-sys/docs-kernel-mode/argument-domain-policy.md`） | `45e58e33c22e87814163a2cd19c5926c5c042fd156315d9e2f77a4aecfe0646e` |
 | K 访问契约（本地参考：`../../../hw-native-sys/docs-kernel-mode/endpoint-access-contract.md`） | `0ab846216ef5be6ebe5368fb7d9d0a880d81b329afaa80d1d97d4134bda9a90b` |
 | K 映射契约（本地参考：`../../../hw-native-sys/docs-kernel-mode/argument-mapping-contract.md`） | `0b30e79e873e5c8d1d45efc081ea41e5926f0db324df49e293e37e3ddccb5625` |
 | K 准备/提交（本地参考：`../../../hw-native-sys/docs-kernel-mode/kernel-mode-prepare-submit-design.md`） | `bdc74f8059844732fb28905c1dee3d77bc94a07f58dc31277d78be17cc400405` |
 | K 代码证据（本地参考：`../../../hw-native-sys/docs-kernel-mode/argument-design-evidence.md`） | `eeb486e7a09a3abe00e04e9c4da50fdaab2b64b9fd0bbcc01405b079558c1e36` |
