# Simpler Tensor 管理领域术语

## 存储与参数

**Backing**：一份实际存储，具有稳定身份、容量、所在位置和负责其物理释放的 owner。两个地址相同不代表同一 backing，两个地址不同也可能指向同一 backing。

**Buffer**：对 backing 的受管理引用，描述其身份、位置、容量和授权。它不是 Tensor 的形状，也不是调用参数的搬运要求。

**Tensor**：backing 上具有类型、形状、步长和偏移的视图。L3+、Remote、L2 对外使用同一种 Tensor；host/device 只是所引用 Buffer 的物理位置。Tensor 自身不保存某次调用的 transfer。正式构造为 `Tensor(buffer, ...)`，不分配或复制数据；`buffer.tensor(...)` 仅作迁移期兼容转发。
_Avoid_：将 HostTensor、DeviceTensor、RemoteTensor、ChipTensor 当成并列的公开资源模型。

**BufferDescriptor**：同一 Buffer 的可传输描述形式，不是第二种公开存储对象。按 backend 携带定位信息或受限地址，必须经消费 endpoint 验证才能使用。

**ChipTensor**：现有内部地址/几何 ABI；公开入口迁移期保留兼容适配，不继续扩充第二套用户构造合同。

**本次绑定**：Tensor 在实际 endpoint 的解析结果及使用保留、复制记录。解析地址的需要不构成另造公开 Tensor 的理由。

**参数搬运要求**：TaskArgs 参数项中，描述这次调用如何从源 Tensor 建立执行参数绑定的 transfer 请求。它不修改 Tensor 视图或源 backing 的物理位置；与同一参数项已有方向/tag 共同决定 IO 动作。

**Scalar**：按值传递的定型标量，其类型、位宽和位模式共同确定含义。设备上会变化的一个数仍可以是 Tensor。

## 访问与归属

**Endpoint**：一次访问实际发生的位置，包括所属机器、进程或设备执行环境。Remote 是两个 endpoint 的关系，不是第三种存储位置。

**Attachment**：某 endpoint 访问一份 backing 的已建立机制，包括可用范围与权限。可执行复制的 attachment 不一定提供可解引用地址。

**映射视图**：通过目标 endpoint 的有效映射访问原 backing 的新 Tensor 视图。它共享数据，不是数据副本，也不修改原 Tensor。

**复制**：把数据写入目的 backing 的操作。目的可以已经存在；为副本新分配存储时才产生新身份，后续写入不会自动互相传播。

**使用保留**：为已接受的访问、有效映射或可重放图保留 backing 和必要 attachment 的义务。它不等于数据已经就绪。

**逻辑释放**：结束一个公开句柄接纳新使用的资格。物理回收须等其余有效使用保留全部结束。

**就绪**：本次读取所依赖的写入已经完成并满足目标 endpoint 的可见性要求。地址可达、权限足够和生命周期有效都不能替代就绪。

**跨 endpoint 访问**：从一个执行位置使用另一执行位置管理的存储。跨机器与 host/device 访问共用这一概念，具体访问机制可以不同。

**访问请求**：调用方明确要求的消费、映射或复制行为，包含源、目标、范围与授权约束。它不因平台存在另一种机制就自动改变语义。

**复制路径**：为一次已授权复制连接源和目的的一个或多个传输步骤。网络复制与 H2D/D2H 可以出现在同一路径中。

**源读取完成**：某次复制已不再读取源数据的事实。它不代表目的数据已经可消费。

**目的可用**：本次复制的目的数据已经完整写入并满足目标消费者的可见性要求。它不代表资源的所有使用者都已结束。

**访问授权与调用方向**：访问授权说明 Buffer 或导入、映射允许的读写，调用方向说明本次调用所需读写。调用需要不得超出已获授权。


## 2026-09-28 参数接口与交付

#2454 已交付 TaskArgs transfer，#2467 已交付直接 L2 的参数接受/校验与保留。#2475 `959f84a9` 的 Worker 借用构造是已提交实现快照，其公开入口按新决定待修订：源 Buffer 登记与目标 Worker 接入分离，正式 view 入口为 Tensor(buffer, ...)。此为最初实现快照；当前 #2478 已合入，#2475 为 `1c095ade`，统一 Tensor 构造 #2479 已合入 main `7b614764b`，源登记/自动接入仍待 #2475 完成。

后续步骤与验收以 [统一设计](argument-domain-policy.md) §1.4、§6.1 为准；构造写法不改变 caller-owned 内存的实际寿命责任。旧 #2444 与 [原 #2454 head 验证](reviews/pr2454-transfer-split.md) 只保留历史证据意义。

构造接口收尾：[ #2480](https://github.com/hw-native-sys/simpler/pull/2480)（已合入 main `4d0610fb`）统一 `shapes=` 与正规导入 `simpler.task_interface.Tensor`，迁移入门示例 21 处，使用绑定函数持有的弱引用做懒加载类型缓存。完整 Python 2815 passed / 63 skipped，10 个 L2/L3 模拟器用例通过；L4 远端真机未验证，本轮无空闲 HBM 设备，未运行板测。主线已统一为 `shapes=`。

### 2026-09-29：direct L2 source registration 的第一种 context provider

代码提交 `a938898b`（[#2482](https://github.com/hw-native-sys/simpler/pull/2482)，待合入）新增 `Buffer.wrap(address=..., nbytes=..., location=worker.device_location)`。
source 登记独立于 consumer provenance/import cache；submit 在原有 chip/provenance 锁内获取 source 使用保留，再安装消费快照。缓存 live 检查与 acquire 间发生 close 时，acquire 必须在 source 锁内拒绝已失效的既有 attachment。未知 native 完成保留引用并作废 context token；close 不是 device fence。

当前 context provider 是拥有设备的 direct L2 Program Worker，token 绑定进程与 Worker incarnation。它不是 device ordinal，也不是外部 RTS context 的 attach API。其他 Worker / Remote 仍明确拒绝，既不重新 mint 同一 backing 的 identity，也不绕过 owner/provenance 校验。跨消费者 grant/provider 接入仍未完成；不能把这一笔标为 U1b 全部完成或 kernel eager 已接通。

正式 Tensor 构造继续用 `Tensor(buffer, shapes=..., dtype=...)`；外部 allocation 的物理寿命与 IO 由原 allocator 负责。本笔不引入 Worker.borrow_device_buffer，不自动复制 source Buffer，也不为 HOST/Remote 制造地址。U2b 再把 COPY 接到同一使用保留合同，U4 交付失败后的明确回收/恢复。

验证：123 项 Buffer/L2 测试通过；HBG 两种模拟器各 6 项、task-submit A2/A3 6 项通过。最终 head 完整 Python 单测 2833 passed / 65 skipped。#2475 的旧接口提案由本笔替代，其原始 AIV 证据留在独立 worktree 与 probes/aiv-borrowed-repro/REPORT.md，未声称已修复。


### 2026-09-29 合入与构建阻塞更新

#2482 已合入 main `fcbf4f1d`，源登记/自动接入的已验证 head 仍为 `a938898b`，不把历史测试移挂到新主线。#2475 已关闭并由它替代。下一项仍是 U1b 的 ChipWorker 公共参数入口收口；跨 Worker/Remote grant 和外部 RTS context attach 尚未交付。

推进时发现 main `92a14e4e7`（#2483）无法构建 A5：boundary-wait discharge 错误分支仍调用单参数 `emit_device_dep_gen_graph`。先提交独立 [#2484](https://github.com/hw-native-sys/simpler/pull/2484) `82af70f5`，补本次 epoch 与 false 完成判定。完整 editable/runtime 构建和三个 A5 DepGen C++ 单测目标通过；本机为 A2/A3，没有 A5 板测。此 PR 只修构建，未交付新的 TaskArgs 接口；参数入口测试草稿留在本地，尚未通过验证。


### ChipWorker 共同参数入口：#2486 已合入

构建修复 #2484 已合入 main `8aae172e5`。[#2486](https://github.com/hw-native-sys/simpler/pull/2486) 已合入 main `22c009811`；验证 head 为 `cacc1415`。它使公开同步 `ChipWorker.run` 接受同一 `TaskArgs<Tensor>`，复用快照、ImportRegistry 和 native POD materializer，不引入第二种 Tensor 或执行器。

- 接受标量和 canonical row-major HOST/H2D 视图，包含非零 offset；DEVICE descriptor 仍交给拥有 live-source/provenance 合同的 `Worker(level=2).submit`。HOST/NONE 与非连续 H2D 在 import 前拒绝。旧 POD 入口保留兼容。
- 本次导入映射持续到 native 返回及回写完成；bind 失败立即清理，native 调用失败保留并阻止新的公开 run，只有 native finalize 成功后清理，失败则继续保留。公开 run/finalize 串行。此合同只保留映射；调用者仍须保持 backing allocation 有效，不传递 host allocation 所有权。
- 验证 head `cacc1415`：完整构建；Python 2853 passed / 65 skipped；HBG 两架构模拟器各 7 passed，其中新用例经真实 ChipWorker.run 连续提交 6 次。主线旧入口、去快照、失败提前 unmap 三种负对照均命中测试。板测未运行：架构是 A2/A3，但查询时 16 张卡全部有 HBM 占用，空 task-submit 队列不代表设备空闲。
- U1b 不标全部完成；跨消费者/grant、外部 RTS attach、旧公开 POD 的余下迁移仍需单独闭合。U2a 的 HOST/NONE 消费、本地 IO 和 Remote 工作未由本 PR 交付。只提交代码、测试及 API 合同文档，设计资料留本地。

### U2a 生产预检：HOST/H2D 布局（#2489 已合入）

基线 main `22c009811`。#2486 的 Python 同步入口已限制非连续 H2D，但
Worker.submit 和旧 POD 入口仍能绕过该限制。四个 binder 都按 numel × element_size
分配和搬运，却保留 strides/start_offset；因此先在共同原生预检拒绝非连续
HOST/H2D 和非零原生 start_offset，覆盖 IN/OUT/INOUT。公开 Tensor.byte_offset
已折算进导入地址，连续子区域仍合法；DEVICE/NONE 的跨步和偏移继续透传。

本项是 U2a 的生产消费者安全前置；HOST/NONE 消费、HBG 双侧显式传参迁移、
TMR/core 的访问判定仍需共同交付，不能把本项标为 U2a 完成。

本项已提交 [#2489](https://github.com/hw-native-sys/simpler/pull/2489)，
head `6c319db0`，base `22c009811`，5 文件 +128/−6。两个 HBG/两个 TMR
真实 binder 目标共 138 项通过；Python 参数测试 45 项通过；HBG readiness
两种模拟器各 7 项、TMR vector 各 1 项通过。新拒绝测试在修复前两个 HBG
目标因读取前项故障地址失败，两个 TMR 目标错误放行并发生分配；回归测试已提交。
当前 16 个设备 HBM 非零，未做本项板上验证。预提交检查全部通过。
规划文档及 HTML 仍在仓库外，不在 PR 内。下一项是 HOST/NONE 的真实消费
与 HBG 双侧参数用例迁移，不能只开放 API 而保留隐式跨侧读取。


### U2a：显式 HOST 消费与消费者迁移（#2501 待审阅）

2026-10-07：#2489 已合入；本次以 main `01bf24bfa` 为基线，提交
[#2501](https://github.com/hw-native-sys/simpler/pull/2501)，head `e89204e5`。
本项实现 HOST/NONE 的真实 HBG 消费，移除隐式 DEVICE 主机访问，未改 Pipeline A 的准入与完成机制。

- HBG 在复制任何参数前登记 HOST/NONE 的范围；get/set 仅按 IN/OUT/INOUT 访问。
  Python 的直接/子进程 chip 入口在解析地址前检查 Buffer grant 能覆盖 callable 方向。
- H2D 产生独立 DEVICE 参数；需要两侧数据就传两个 Tensor。HOST 写入不反向更新已复制的 DEVICE 值。
  要在 host 使用上次 device 结果，先显式等待与复制，再传入独立 host 参数。
- TMR、device task、graph 参数及 predicate 拒绝 HOST。HOST/NONE 支持有效跨步视图；
  HOST/H2D 保留连续布局约束；两份 H2D 副本重叠且存在写入时在分配前拒绝，只读重叠允许。
- PA、BGEMM、DSv4 host 元数据、FIFO/early enqueue、拓扑与 readiness 用例随接口共同迁移。
  本项不引入 PreparedCall、缓存、映射视图或自动 D2H 标签。
- 验证：294 个 C++ 测试目标；Python 2907 passed / 57 skipped；A2/A3 sim 69、A5 sim 67；
  task-submit A2/A3 板上 19 项通过，grant 检查后复测相关 readiness/FIFO 路径也通过。
  A5 板上和完整大规模 DSv4 尚未运行；日志隔离问题记录在工作树 KNOWN_ISSUES.md。

本 PR 待审阅，不能标为已合入。后续为 U2b 的本地显式 COPY 与完成/保留合同，
U3 Remote producer/receiver 的对称升级可并行。Kernel eager 的外部 context/caller 顺序仍需单独接线与验收。
代码 PR 只含产品代码、API 文档和测试；本节和 HTML 留在仓库外。


#### #2501 CI 修正（2026-10-07）

最新 head `f632d25a`，rebase 基线 `a711032ed`。初次 CI 的 A2/A3 early_enqueue
六例段错误来自漏迁共享 FIFO 编排的调用方：编排读取第 4 个 HOST/NONE 参数，
调用方却仍声明、传入 3 个 tensor。已补齐签名和实参；没有改入队/完成逻辑。
两个无设备参数构造回归测试在修复前均失败；修复后 Python 全套 2909 passed / 57 skipped，
task-submit 六个板上用例全部通过（另四个 test_run 占位例按设计 skip）。
先前模拟器通过不能覆盖这些 onboard-only 用例。新 head 已推送，CI 待重跑结果。


最新依赖修正见 [Kernel eager 剩余问题与接线顺序](kernel-eager-open-work.md)。


## 2026-10-08：当前接线与剩余问题

当前状态统一维护在 [kernel-eager-open-work.md](kernel-eager-open-work.md)。#2501 已合入；
Program IO / TensorLease 整理不作为 kernel eager 的前置。当前本地工作是 TMR/HBG 借用设备下的
共同 prepare/finalize，之后接 caller-stream launch 与公开入口，再做 capture。
