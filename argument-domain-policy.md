# Simpler Tensor 管理统一设计

更新：2026-09-28。状态：**选定设计，尚未完整实现**。原则来源仍是 [runtime-design-principles.md](runtime-design-principles.md)；本文负责将参数原则落实为 L3+、Remote、L2 共用的对象和流程。

源码事实基线：main `c605b03cad7be2a80700efbdf3bd00dbc79812a9`。PR #2444 分支 `1761d1304eec8b1ea7836d8d2f6812acbc5b2498` 单独对照，不能当作主线。符号、差异及验证见 [代码证据](argument-design-evidence.md)。[HTML 阅读版](simpler-argument-design.html) 从本文和子文档生成。

## 1. 总体决定：一个公开 Tensor，一个公开 Buffer

**L3+、Remote、L2 对用户统一为 `Buffer + Tensor`。** Buffer 表达存储身份、位置、容量与归属；Tensor 是该 Buffer 的几何视图；一次调用的 transfer 属于 `TaskArgs` 参数项。用户不需要先把 Tensor 转成另一种 ChipTensor，也不维护 HostControl / DeviceOperand 两份参数列表。

统一公开对象不要求把所有内部结构做成同一块内存：`BufferDescriptor` 是 Buffer 的可传输描述，已解析的地址和使用保留是本次绑定结果。现有 `ChipTensor` 可在迁移期保留为内部 ABI 编码，HBG/TMR 自己的调度 Tensor 也可保留；这些都不成为第二套公开资源模型。

```text
用户：一个有序参数列表（Tensor + Scalar）
                     │
L3+ / Remote：Buffer 身份 + Tensor 几何 + 调用要求 + attachment 引用
                     │ 统一跨 endpoint 请求：NATIVE / MAP / COPY
                     │ 授权、依赖、backend adapter、目标端解析
L2 host prepare：本次参数绑定 + 使用保留 + Program IO 记录
                     │
HBG host 编排 / TMR device 编排 → KernelLaunch → 双侧完成 → IO 完成 → 退休
```

**Remote 访问与 host/device 访问是同一种跨 endpoint 问题。** 底层统一 source/target、权限、范围、复制或映射语义、完成与使用保留；网络、SHM、H2D/D2H 由具体 adapter 实现。Remote 关系与 HOST/DEVICE 位置正交，同一路径可以先网络复制再 H2D。

Buffer owner 继续负责物理分配与回收；目标 endpoint 负责 attachment；本次调用负责绑定与实际复制步骤。完整共用接口、双端能力判断、多段路径、完成与失败规则见 [跨 endpoint 访问合同](endpoint-access-contract.md)。不为 Remote 和 device 分别维护一套参数生命周期。

### 1.1 六个正交维度

| 维度 | 归属 | 确定含义 | 不能推导什么 |
| --- | --- | --- | --- |
| backing 身份与 placement | Buffer owner | owner incarnation + buffer_id + generation；机器、owner endpoint、HOST/DEVICE | local/remote 不是固定的 location 值 |
| 访问授权 | 既有 Buffer / import grant；映射授权归 mapping grant | READ、WRITE、READWRITE；提交时与调用方向检查兼容 | 不新增逐 Tensor 权限字段；授权不保证平台可达或数据就绪 |
| 视图几何 | Tensor | dtype、shape、stride、相对 backing 的 byte_offset | 不改变存储位置和 owner |
| 本次参数搬运 | TaskArgs 的 tensor 参数项 | NONE、H2D；D2H 已有设计位置但当前拒绝 | 不塞进 AddressSpace 或 READ/WRITE |
| endpoint attachment | 目标端 registry | 实际 profile、目标环境代际、有效范围、权限、局部地址或复制通道 | 存在 copy 通道不等于存在直接地址 |
| 本次使用 | TaskArgs / callable / 操作入口 | INPUT/OUTPUT/INOUT、实际 host/device 消费、依赖与 completion | 不因 NO_DEP 就免除权限、范围、寿命检查 |

`AddressSpace` 只保留 HOST / DEVICE。H2D 是独立字段，不保留 #2444 的 `AddressSpace::HOST_TO_DEVICE`。READ / WRITE 是访问权限；H2D / D2H 是搬运要求，两者不是同一个枚举。

### 1.2 对象关系与存储位置

| 对象 | 保存什么 | 保存在哪里 / 谁拥有 |
| --- | --- | --- |
| canonical backing record | 不变身份、容量、物理位置、owner、backend、最大授权 | 分配方或外部注册方的 registry |
| Buffer handle | 指向 backing 的有效 owner/grant 引用；公开关闭状态 | 用户持有；runtime 验证私有登记，不能相信可修改的公开字段 |
| Tensor descriptor | Buffer 的描述引用、几何、可选 attachment grant 引用 | 视图按值复制；所有层级使用同一公开类型 |
| TaskArgs 参数项 | Tensor 值、既有 tag、独立 transfer 请求 | 本次调用；同一 Tensor 可在不同参数项使用不同请求 |
| attachment record | backing 身份、目标 endpoint/context generation、profile、映射范围与地址、授权、使用计数 | 目标 endpoint；owner 另保留对应 backing lease |
| per-run binding | endpoint 可用地址与范围、源/目的关联、IO 记录、使用保留；按需编码内部 ChipTensor | 本次调用；不引入可重复 submit 的 PreparedCall |
| Scalar | 固定类型解释下的值位模式 | 参数值快照；不借用 Python 标量对象或其容器 |

Buffer 的物理位置不因 host 映射而变成 HOST。本次绑定的地址必须另有**该地址可在哪个访问域使用**的含义。当前无跨侧映射时两者相同；未来映射实现不能继续把这两个事实混在一个字段里。

目标描述的字段归属如下（语义布局，不宣称二进制 ABI 已交付）：

```text
BufferDescriptor = identity + backing_location + total_bytes + owner_access
                   + owner_route + backend/owner payload
Tensor           = buffer_ref + byte_offset + shape/stride/dtype
                   + optional attachment_grant_ref
TaskArgs entry   = Tensor value + existing direction/tag + transfer request
Attachment       = backing_identity + target EndpointIdentity/context_generation
                   + owner_range + granted_access + profile + local resolution
RunBinding       = 参数序号 → 原 Tensor + target address/span + access_space
                   + leases + IO actions
ChipTensor       = RunBinding 的内部 ABI 投影；不另设公开构造合同
CopyRecord       = 源/目的绑定 + 实际传输步骤 + 中间存储 + 本次完成/清理记录
```

不增加 Tensor.view_access 或 ChipTensor.access/effective_access。授权只来自既有 Buffer/import grant；本次所需读写来自 TaskArgs tag、callable signature 和实际 get/set/copy 操作。普通几何 view 沿用源授权；映射/import 的收窄权限保存在对应 grant，不在 Tensor 上复制成另一份可修改状态。没有跨侧映射时 attachment_grant_ref 为空。

绑定阶段检查所需读写是否为授予权限的子集；需要供 host get/set 继续检查的约束保存在本次 host 侧绑定记录，记录关联原 Buffer/grant 与调用方向。L3 materialize 不能丢掉这一关联，但不要求把权限再写进 ChipTensor POD。上述 access_space 只说明已解析地址的 HOST/DEVICE 访问域，不是 READ/WRITE 权限。L2 外部指针先包装成借用 Buffer，登记显式 endpoint、容量、位置和 caller lifetime 合同，再通过同一个 Tensor 入口提交。登记为同一个 Buffer 的视图共用身份；地址整数不作为稳定身份。外部重叠 allocation 不能靠重复包装自动识别 alias，调用方必须复用或明确关联同一登记。借用登记不接管 allocation，也不自动取得长期 map/graph 保活能力。

Buffer 的 owner_route 是 locator，不是身份；跨机器 endpoint 解析由 registry 提供 node/context 信息，不在 address_space 增加 REMOTE_HOST 等值。变长 transport payload 放既有封包区域，以受检查的 offset/index 引用；公开调用仍只有一份 Tensor 列表。

`BufferDescriptor` 的 canonical 部分与 attachment 的 transport payload 必须区分。不能把同一 backing 的两个导入地址、不同授权范围写成两个互相冲突的 canonical descriptor。现有 ImportRegistry 的“同身份、不同 descriptor 拒绝”保护保留；新增 attachment 表承接可变的端点事实。

### 1.3 地址在哪里，为什么还要解析

Buffer 的 backend 载荷按机制保存地址或定位信息：本地 DEVICE_MALLOC 可以携带 device VA，POSIX_SHM 保存可导入名称，Remote 保存 owner 路由或引用。**统一 Buffer 不等于每份 Buffer 都有当前进程可解引用的 `void *`。**

Tensor 通过 Buffer 引用定位存储，再加自己的 byte_offset 和几何。只有在实际消费 endpoint 验证身份、授权、范围及就绪条件后，绑定层才得到可用 base；Tensor 的起点为该绑定覆盖区中的对应地址。Remote copy-only profile 没有本地 direct 地址，不能解析成一个假指针。

当前代码已经有可复用路径：`Worker._materialize_l2_args` 在 L2 接收 `TaskArgs<Tensor>`，经 ImportRegistry 和 `materialize_task_args` 转成内部表示，再走 `run_materialized`。改造以此为共同入口，收拢低层 ChipWorker 的公开构造和借用包装，不重做另一套 materializer。

当前 Tensor 按值嵌入 BufferDescriptor，并不强持有 Python Buffer 对象。目标也不靠 wrapper 引用计数解决所有寿命：已接受调用取得使用保留；有效 owner/import/map handle 各有合同；普通 Tensor 副本不能阻止显式 close。先保留现有在飞 free 拒绝，再按 §6 收口延后物理回收。

### 1.4 正式构造入口与内部接入

**正式入口固定为 `Tensor(buffer, shapes=..., dtype=..., strides=..., byte_offset=...)`。** Buffer 管存储；Tensor 构造器负责 dtype、几何和范围检查。构造 view 不分配、不复制、不搬运，不创建新 backing identity；位置来自 Buffer，transfer 只在 TaskArgs 参数项中。

- simpler 分配的内存由现有 `worker.create_buffer` / `worker.malloc` 返回同一种 Buffer；外部本地存储通过 `Buffer.wrap(address=..., nbytes=..., location=...)` 登记，Remote 通过有授权的 export/import 取得同一种 Buffer。
- `location` 表达源存储的 HOST/DEVICE 与所属 endpoint/context generation，不是当前消费者；单独一个 device ordinal 不足以覆盖跨进程、跨机器和 reset 后的新代际。这里规定语义，不假称已有这个完整 placement 构造器。
- `Buffer.wrap` 在源侧建立有效登记，不自动向每个 Worker 注册。Worker 在 submit 中验证来源登记和授权，建立或复用自己的 attachment，解析地址并保留本次使用。用户不额外调用 `register_buffer`。
- 同一 backing 在不同 Worker 上使用仍保留同一个 identity；目标地址、attachment 和 grant 可以不同。不能简单把现有方法移到 Buffer 类，再取消 Worker 的 live/provenance 检查。
- Tensor 在逻辑上引用同一 backing 记录；内部可保存描述符/句柄，跨进程经 registry 解析，不要求保留 Python Buffer wrapper。有效性及已接受使用的保留由登记合同控制，不能只复制裸地址，也不能仅靠 wrapper 引用计数。
- 外部借用不转移 allocation 所有权、不自动保活调用方 Python owner。调用方保证容量、真实位置、地址稳定和实际使用期间存活；异常不是完成证明，关闭描述符也不是 device fence。
- 目标关闭语义：逻辑关闭拒绝新使用，已接受的使用按完成边界保留内部记录；这不自动延长外部物理分配寿命。现阶段在飞 free/release 继续拒绝，延后物理回收按 U4 交付。

迁移期 `buffer.tensor(...)` 仅转发到同一 Tensor 构造与检查，不维护另一套状态或权限规则。仓内公开用例迁移到 `Tensor(buffer, ...)` 后移除旧入口；外部兼容需求须明确记录，不默认永久保留两个等价接口。`worker.borrow_device_buffer(...)` 也不是最终公开入口，#2475 的实现状态与调整要求见 §6.1.2。

## 2. 访问能力怎么判断：一个入口，分层证明

复用当前 `buffer_adapter_candidates`、`DefaultRegionAccessService`、`BufferCapability` 的判断体系。扩展它们覆盖实际操作，不另做一个“看 location 就准许访问”的旁路。

```text
参数结构及几何合法
 → backing/句柄/owner incarnation 有效
 → target endpoint 与具体 chip 获授权
 → 由调用方向/实际操作导出的读写 ⊆ Buffer/import/mapping grant 的既有授权
 → 当前产品策略允许 location × attachment × transfer × operation
 → adapter 候选及实际实现支持
 → 建立/复用 attachment，核对其范围与 endpoint generation
 → 原子取得本次使用保留
 → 依赖及可见性满足后执行本次操作
```

前半段是静态合法性，最后一项是时间条件。检查与取得引用必须对 free/unmap 原子；拿到引用后仍可能要等待 producer，但不在持有 registry 锁时等待。

### 2.1 谁执行，谁决定所需能力

| 操作 | 判断的真实 endpoint | 必需的能力和附加条件 |
| --- | --- | --- |
| L3 SUB Python 读取/写入 HOST buffer | 运行该 SUB 的 host 进程 | DIRECT_LOAD / DIRECT_STORE；权限、范围、依赖 |
| L2 准备映射 HOST SHM | ChipWorker 的 host 进程 | HOST_SHM_MAP；**不代表 AICPU/AICore 能读它** |
| HBG get_tensor_data / set_tensor_data | host orchestration | 已登记 HOST 绑定的 direct 读/写；不得临时 map DEVICE 或自动 copy |
| TMR orchestration / core task operand | 真正使用地址的 AICPU / AICore | device 可执行绑定；需要的每类消费者都可访问 |
| Worker.copy_to / copy_from | 有控制通道的 owner host + 所属 chip | COPY_TO / COPY_FROM；不授予 host 直接解引用设备地址的能力 |
| remote copy | 源/目的 session 及其实际 backing owner | 明确复制通道、两端权限、range、完成确认 |
| map_view | 指定 target endpoint | 真正已实现的映射 profile + lifetime + coherence 合同 |

现有 `AccessMode` 值是 READ=0、WRITE=1、READWRITE=2，不能直接对整数做位与；转成所需读写集合再求交。Remote 的 flags 是另外的位掩码，wire 解码时显式转换。

### 2.2 当前已实现能力与当前策略

| 关系 / backend | 当前共享能力判断 | 本设计的处理 |
| --- | --- | --- |
| 同机 HOST → HOST：POSIX_SHM | HOST_SHM_MAP，可得到 host 地址 | 保留，正常用于 L3+ / SUB / L2 host prepare |
| fork 继承 HOST | FORK_INHERITED_VA；FORK_COW 只读 | 保留，不能用 WRITE 声明把 COW 变共享写 |
| DEVICE_MALLOC / VMM_WINDOW → owner chip | DEVICE_LOCAL，另需私有 provenance 精确 chip 校验 | 保留；同 Worker nonce 不足以证明同 chip |
| owner host → DEVICE | OWNER_DEVICE_COPY / HOST_VMM_COPY | 保留显式 copy API；host get 不得降级到 copy |
| VMM_SHAREABLE / VMM_WINDOW → peer device | 候选存在；默认 service 拒绝未实现 peer import | 明确失败，不能把枚举存在写成已支持 |
| HOST_SVM_MAP | 共享 service 缺已实现 probe/producer，拒绝 | 当前跨侧 mapping 统一拒绝；平台私有 hook 存在也不自动放开 |
| REMOTE_COPY / DEVICE_FABRIC_V2_PEER_IMPORT | 默认 service 拒绝未实现 backend | 有独立 remote control transport 的路径单独验证，不能虚构通用 materializer |
| REMOTE_SIDECAR → 本地 materialize | 拒绝，需由 remote session 解析 | 保留强制入口；迁移后仍无“把远端 VA 当本地地址”路径 |

**合同和操作记录统一，backend 的能力与完成机制保持真实。** 默认 service 描述它实际能兑现的操作；remote session 的复制以限定 profile/endpoint 的 adapter 接入，不把 REMOTE_COPY 全局设为 true。COPY 同时检查源读和目的写；不能把“host 能驱动 device copy”误当成“host 有 device 地址”。

H2D 参数标签只是 Program 入口的局部要求，共同层使用源/目的 endpoint 表达复制，不增加 REMOTE_H2D 一类组合值。只有显式网络请求加当地 H2D 请求才授权网络→H2D；不能看目标是 remote 就自动搬运。

## 3. L3+ 到 L2：参数和依赖如何流动

### 3.1 本地 L3+ 流程

1. `Worker.create_buffer` / `alloc_shared_tensor` 建 HOST backing；`alloc_child_tensor` 建指定 chip 的 DEVICE backing。owner 登记不变 descriptor 与授权。
2. 用户由 Buffer 建 Tensor；slice 只变几何。同一 Tensor 可以作为两个参数项传入：NONE 用于 HOST 编排，H2D 用于 Program device 绑定。
3. `TaskArgs` 按既有顺序容纳所有 Tensor 和 Scalar；每个 tensor 参数项保存本次 transfer。L3 tag 定义依赖与使用方向，callable signature 定义 L2 读写；两者必须兼容，不能悄悄放宽。
4. submit 校验 target、权限、几何，登记 backing 使用；生成依赖。HOST 来源有前驱时，**读取/快照内容**须在相应 producer 完成后进行。
5. child 收到 Tensor 描述；不能把其中的 backend 地址载荷直接当成本地指针，ImportRegistry 在该 endpoint 验证并解析。HOST SHM 在 host prepare 映射；DEVICE 仅在授权 chip 解析为 device VA。
6. materialize 生成本次绑定，参数项的 transfer 独立保留至 Program adapter 消费。授权来源及 attachment 关联留在 host 侧记录，由既有 tag/signature 检查所需读写；内部必要时编码为 ChipTensor。
7. HBG host 编排或 TMR device 编排消费自己的合法绑定；提交内核；最后一个访问者及必要回拷完成后释放本次保留。

L3 的 `TaskArgs` 值复制主要复制描述与 scalar，**不是把所有 HOST backing 内容变成调用瞬间的快照**。共享 HOST 数据有真实 producer/consumer 依赖；提前 memcpy 会读到前驱旧值。Kernel 的 host 控制参数快照则在其显式 host 准备入口完成，返回后不再借用调用方 host 容器。当前 `SimplerKernelInvocationHeader.host_copy_tensor_count` 的接收合同仍要求为零：开放该入口时必须同一改动更新封包生产者、消费者与容量检查，不能只增加 Host tensor snapshot 工具。

### 3.2 分配者与中间结果不能混为一类

| backing 来源 | 当前实际机制 | 统一方案中的回收责任 |
| --- | --- | --- |
| create_buffer | HOST POSIX_SHM，用户显式释放 | owner handle + 使用保留；Worker 管物理关闭 |
| alloc_shared_tensor / orch.alloc | HOST HeapRing / FORK_SHM，scope 和 consumer 引用管理 | runtime 自动回收，不能当普通 malloc 手工 free |
| alloc_child_tensor / L2 malloc | 指定 chip DEVICE_MALLOC | chip allocator 负责物理 free，登记身份与实际 chip |
| CommDomain window | VMM/window 生命周期归 domain | 普通 Buffer.free 拒绝；domain teardown 负责 |
| 外部指针包装 | caller-owned，wrapper 不拥有 allocation | caller 保证寿命；无可保活 lease 时不得建立长期 map/graph 引用 |
| remote owner/import | remote owner backing / importer attachment | 分别 free owner / release import，不相互替代 |

HeapRing 中间结果的 Python Buffer wrapper 活着不代表 slot 永不复用。派生 Tensor 只在 scope/依赖保留内有效；要跨 run 保存结果，使用显式持久 Buffer 并复制/写入，不能让返回 wrapper 自动提升分配寿命。没有可延长 ring allocation 寿命的 allocator 合同，就拒绝把它导出为长寿命 mapping 或捕获到长期 graph。

本地现有 OUTPUT 的分配时机不能从旧注释推断：`reserve_outputs_and_slot` 现在只 `alloc(0, ...)`；需要存储时先用显式分配入口。Remote 输出同样先 remote_malloc/import，HOST_INLINE 只有读权限、不能作 OUTPUT/INOUT，也不隐式回传修改后的值。输出返回通过显式远端结果 Buffer + copy_from。

### 3.3 L2 位置、搬运、方向矩阵

| 源 | transfer | Program 绑定 | Kernel 直接入口 |
| --- | --- | --- | --- |
| HOST | NONE | HOST 参数供 HBG host 编排；TMR device 编排不能消费 | 供显式 host 准备；调用内复制需要保留的控制值，不给 device 地址 |
| HOST | H2D | 建独立 DEVICE 参数绑定，进入 Program IO 管理 | 拒绝；kernel adapter 不承担用户 tensor 搬运 |
| DEVICE | NONE | 借用已有 DEVICE 存储 | 借用已有 DEVICE 存储 |
| DEVICE | D2H | 当前拒绝此参数请求 | 当前拒绝 |
| HOST | D2H | 来源与方向不匹配；失败 | 失败 |
| DEVICE | H2D | 来源与方向不匹配；失败 | 失败 |
| 任意 | 未知值 | 解码/入口失败 | 失败 |

D2H 不支持只限制**参数的自动搬运标签**，不删除显式 `copy_from` / `remote_copy_from`。

**H2D 在此参数接口表示“为 HOST 参数建立 Program 管理的 device 绑定”。** 它不是 copy 指令计数：INPUT 复制到 device；OUTPUT 不读取未初始化 host 内容，完成后回拷；INOUT 复制进去并回拷。回拷是 Program IO 合同，不是 DEVICE tensor 被 host get 直接读取。实际 copy-in/copy-out 动作放在本次 IO 记录，不能再从 H2D 一字推断所有动作。缺失方向时保留原 Program 的保守 INOUT；不把它改成只入不出。

这保留既有 Program 输入输出行为，也明确命名的限度：H2D 是参数绑定策略，单独 `copy_to` 才是严格单向复制操作。后续若希望 API 只表达单向复制，应迁移全部 Program IO 合同，不能只偷偷改变一个枚举解释。

H2D 交给编排后的参数是 DEVICE；不得由这个参数找回原 host 指针。两侧都要用时，用户明确传两个参数。可以是同一 HOST buffer 的两个参数视图，或两个独立 HOST / DEVICE buffer；两份内容一致性必须由调用方或显式复制关系保证。

### 3.4 用户接口：Tensor 是视图，TaskArgs 表达本次使用

以下展示**选定的目标构造 API**，尚未完整实现；TaskArgs 的 transfer 已由 #2454 交付，但独立 Buffer.wrap 与 Tensor(buffer, ...) 的完整登记/接入合同仍待实现，不增加新的参数容器。

```python
h = worker.create_buffer(nbytes)          # HOST Buffer
x = Tensor(h, shapes=shape, dtype=dtype)    # 只有 Buffer 引用与几何
args.add_tensor(x, TensorArgType.INPUT, transfer=TensorTransfer.NONE)
args.add_tensor(x, TensorArgType.INPUT, transfer=TensorTransfer.H2D)

# 同一 x 不修改；两个参数分别得到 HOST 绑定与 Program DEVICE 副本。
device_buffer = Buffer.wrap(address=ptr, nbytes=capacity, location=device_location)
y = Tensor(device_buffer, shapes=shape, dtype=dtype)
args.add_tensor(y, TensorArgType.INOUT, transfer=TensorTransfer.NONE)
```

L2 直入通过源侧 `Buffer.wrap` 包装外部指针，再用 `Tensor(buffer, ...)` 和 `args.add_tensor(...)` 提交；Worker 在 submit 内完成目标接入。源登记不依附于某个消费者，目标接入必须保留现有 owner/provenance 检查。不要求用户构造 ChipTensor；现有 ChipTensor 调用由兼容适配器转换，移除前迁移公开示例与调用者。

**transfer 不进入 Buffer 身份或 Tensor 几何，也不由建立 view 决定。** 同一 Tensor 在不同调用中的使用互不修改。TaskArgs 内部按既有参数序号保存请求；不拆成 host/device 两组，设备 ABI 也不需要机械复制这份 host 侧请求。

兼容期，旧入口省略 transfer 时保留已定义的默认行为：Program HOST/H2D、DEVICE/NONE；显式 NONE 与省略必须可区分。旧 `child_memory` 只在兼容入口解释为位置，不能成为另一份持久状态。新例子必须明确传 transfer；转成规范参数项后，不允许下游或远端再次猜默认值。Kernel 入口仍拒绝 H2D，不能继承 Program 自动搬运责任。

HOST+NONE 的 get/set 权限按实际操作检查；readonly 不允许 set。HOST 控制值最终编码成 device metadata，不等于原 HOST tensor 被交给 device 读取。

### 3.5 几何和 alias 的约束

- bounds 检查用可达字节范围与溢出保护；非空 tensor 的末尾由 offset、shape、stride、dtype 决定，不能只用 `numel * element_size`。
- 目标模型允许空视图不解引用；rank、dtype、枚举、身份、transfer 仍检查，空视图可保留构造器生成的零 stride/null。当前 L3 validate_tensor 拒绝零 shape/stride、L2 可表达空 Tensor；迁移要统一这条规则及 codec，不能宣称现状两层已经一致。
- 当前 H2D 的 `t.nbytes()` 分配/连续 memcpy 不能覆盖任意 stride。**本轮支持契约明确拒绝非连续 H2D/managed copyback**，保留原生 HOST/DEVICE 合法 strided view；用户先打包或使用显式复制。后续通用 strided copy 必须同时定义 scatter 回写与空洞保护。
- 两个 HOST+H2D 参数若需要写入且范围重叠，独立副本会改变 alias 语义。本轮在全参数预检时拒绝这类重叠；只读复制可以独立。不得通过简单保留源 identity 假装 device 副本共享存储。
- 本次 Program IO 记录源/目的的对应关系，L3 完成时间覆盖回拷。源 H 和副本 D 是不同 backing，依赖由复制边连接；同 backing 的 view 才共享 alias 身份。

## 4. Remote 进入同一模型，而不是第三种 Tensor

### 4.1 先区分两种“远端”

**远端执行**：将调用发到另一机器，由那里的 Worker 使用当地 HOST / DEVICE backing。**跨 endpoint 数据访问**：目标使用源 endpoint 管理的数据，可明确选择复制到目的或映射同一 backing。远端直接解引用只是其中的 MAP 情况；TCP 连接不等于 peer memory 可达。

当前 host_tcp 的 `REMOTE_DEVICE` owner entry 实际是 HOST POSIX_SHM。`EXPORT_BUFFER/IMPORT_BUFFER` 传 SHM 名称并在 importer open：它要求同一可访问 SHM namespace，不能作为跨机器共享内存证据。跨机器可用的传值路线是 inline/RPC copy 到目标 backing；不静默把 import 改成 copy。

### 4.2 五条 Remote 数据流

| 使用场景 | 统一流程 | 身份与生命周期 |
| --- | --- | --- |
| 小 HOST 控制值传到远端 | HOST_INLINE 值 → 接收端新 HOST Buffer → transfer=NONE → host 编排 | 新 backing、新 identity；内容在发送入口快照，接收端保留至消费结束 |
| HOST 数据发远端跑 device | inline 或显式 remote_copy_to → 远端 HOST Buffer → transfer=H2D → 当地 DEVICE 绑定 | 网络复制与 H2D 是两个独立步骤；两端 completion 不混用 |
| 已在远端的持久 HOST/DEVICE | owner-issued reference → session registry 验证 → 当地解析 | 保留该 backing 的真实 identity/location；不重新声明成 HOST_INLINE |
| 同机可共享 remote import | export 原 identity/授权范围 → importer 建 HOST_SHM_MAP attachment → 新 Tensor view | 同 backing，新 attachment；不从不同 shm name/地址生成新身份 |
| 真正跨机器 peer mapping | 真 transport profile + target grant + coherence + retain | 当前无相应实现的组合直接报不支持；显式 copy 是另一操作 |

HOST_INLINE 不默认代表“host-only”，也不默认代表“H2D”。**载荷如何过网络**与**接收后如何绑定**是两个字段；transfer 必须穿过 codec 和 `_materialize_task_args`，不能由接收端硬编码默认值。

### 4.3 Remote wire 与身份迁移

保留现有 RemoteBufferHandle / RemoteTensorRef 作为兼容门面；新的内部语义统一为 Buffer/Tensor/reference，不要求使用者立刻重写为另一套顶层 API。TaskArgs 仍只有一份用户参数列表；编码器按 tensor index 管理变长 transport 表，这是协议实现，不能让用户维护第二份角色 vector。

新 wire 必须明确携带：

1. canonical backing identity，包括真正 owner incarnation；node/session/owner route 独立存放。不能继续仅把 worker_id 填进 owner nonce 来跨 session 识别 backing。
2. backing location、容量、最大授权；import 的 grant_id/generation、允许访问的 owner-relative 范围与收窄权限。
3. Tensor 几何，以及独立参数项中的 transfer、调用 tag；scalar 的位模式及类型解释合同。
4. transport kind（inline、owner reference、import reference）与版本化 opaque payload；地址只由对应 endpoint 解码。
5. endpoint/session generation 以及协议版本/feature 协商。不认识新字段/组合必须拒绝，不按旧默认静默执行。

owner 分配成功即生成 canonical identity，export/import 全程传递；导入不另造 backing 身份。HOST_INLINE 为新副本，必须每个独立值有独立身份；同一次调用中需要 alias 时显式复用同一传输 backing，不能碰巧靠相同 offset 关联。

**所有 tensor offset 采用 owner backing 坐标。** attachment 另存覆盖区间 `[begin,end)` 和对应 local_base，解析公式为 `local_base + (tensor.byte_offset - begin)`。导入切片不能既缩小 Buffer 容量又保留原 identity、再把偏移减掉一次；这会混淆 descriptor、bounds 和依赖坐标。

### 4.4 依赖与授权不在网络边界丢失

统一 dependency key 为 canonical backing identity；entry 带视图 footprint。Remote 不再把 offset 放在 key 中。同一 backing 不同 import_id、host/device mapped 地址仍相互可见；复制生成的新 backing 由显式传输边连接。

本设计沿用现有 L3 run 内依赖推导，不新增跨独立 run 的全局依赖追踪器；外部 stream、跨 run 与应用级一致性由调用方有序化。`NO_DEP` 仅跳过依赖推导，不能跳过生命周期和权限。

保留现有 tag 语义：INPUT 查前驱，INOUT 查前驱并登记新写者，OUTPUT/OUTPUT_EXISTING 只登记写者。统一 identity/footprint **不自动补上所有 WAW/WAR 顺序**：覆盖既有数据需使用相应 INOUT 或显式依赖，读后覆盖也要显式保证先前读者完成。不能把此方案误读成任意相交读写自动全序化。

Remote 发送端、接收端、L2 使用端都不能放宽权限。接收端不可把 READ import 重建为 READWRITE 参数，再寄希望于发送端检查；grant 必须跟着参数传到下一层。

## 5. 映射视图、释放与数据可见性

**为 device tensor 建 host view：返回新 Tensor，不修改原 Tensor，不复制数据。** 新视图引用原 backing 和目标 attachment，地址可不同；源 location 仍 DEVICE。完整接口与状态机见 [映射合同](argument-mapping-contract.md)。当前跨 host/device 的两个方向均拒绝。

| 操作 | 修改的对象 | 不改变的事实 |
| --- | --- | --- |
| 普通 slice | 新 Tensor 几何描述 | backing 身份、位置、原授权上限 |
| map_view(t, target, access) | 新目标 attachment/grant + 新 Tensor | backing 内容、原 Tensor、原 owner |
| HOST+H2D Program 绑定 | 新 DEVICE 存储 + 本次 IO 关系 | 源 HOST backing |
| free/release owner | 关闭该 owner handle 新使用资格 | 已接受使用和独立有效 mapping 的保留义务 |
| unmap_view | 关闭该 view grant 新使用资格 | owner buffer、其他有效 grant |

### 5.1 统一释放目标与现状迁移

选定目标为**逻辑关闭立即生效、物理回收延后**。Remote 已有 slot/import refs 与 pending-free journal；本地当前 host release 拒绝在飞、device free 受 control admission 约束。它们现在还不是统一语义，必须先有保留记录再修改行为，不能直接删检查。

物理回收条件是：owner handle 已关闭，所有有效 mapping/import grant 已关闭，所有 accepted submission、copy、host 读取、event wait、graph 使用保留均结束。普通 Tensor descriptor 不是无期限保活 token；用户若释放 owner 后还想独立使用别名，须有显式有效 grant。

public Buffer.close 也须服从 owner registry，不能绕过 Worker 直接 unlink/free；底层物理 close 留作 owner 内部动作。DEVICE_MALLOC、POSIX_SHM、CommDomain window 仍由各自 allocator 物理释放，不能统一成一个 rtFree。

释放 RPC 失败或调用部分成功时，保留 cleanup journal 与相关引用，禁止地址/代际复用。只有已确认的 teardown 或释放完成能解除；网络超时不证明设备已不再访问。沿用现有“不确定资源保留到受控 teardown”的方向，不引入基于超时的分布式自动回收。

### 5.2 Host 数据、Pipeline A 和 capture

- 外部 producer 的 HOST/DEVICE 数据由用户保证就绪；L3 已声明 producer 的依赖由 runtime 执行。prepare 可提前进行描述校验、资源规划；依赖未满足的内容读取不能提前。
- kernel host-control 值在同步 host 准备入口完成快照，不能把用户 host vector 的生命周期交给异步任务。Program 大 tensor IO 可持有受管理 Buffer lease 至 DMA/copyback 完成；借用外部裸指针时需显式 caller lifetime 合同，不能靠 Python wrapper 引用保活 allocation。
- HBG 一次 kernel 调用必须在 launch 前完成 host 决策；不得等待本次尚未 launch 的图产生 control value。动态决策放 device 或由上层拆调用。
- 每个 A3 逻辑调用涉及 AICPU/AICore 两次 launch，回收按两侧实际最后使用和后续 IO 完成；单一 kernel task 结束不够。
- capture 记录 host 准备产生的完整 device 效果及必要发布；replay 不调用 host orchestration。仅改变 HOST 控制值不能假定 replay 重算图。改变编排拓扑必须重建；已定义的参数更新只能更新允许的数据。
- graph 持有引用与稳定地址；view 公开关闭不破坏此前 graph 已取得的保留。context reset 使旧 generation 失效，不能继续 replay。
- MTP 的 KV cache、权重、持久 token 状态为显式 DEVICE IN/INOUT；host control 是显式 HOST 参数。一个 device scalar 若运行期变化仍是 Tensor，不把它当固定 Scalar 快照。

## 6. 落地顺序、拒绝行为与验收

### 6.0 2026-10-14：API 审查接回既有参数路线

U1b 的单一公开 Buffer/Tensor 与现有 ImportRegistry/materialize 是 K2b 内部接入的基础；K2b 为 borrowed source 增加真实环境 generation 消费，K2c 才开放完整 public 调用。ChipTensor/ChipStorageTaskArgs 留作内部 ABI，不通过再造公开参数类型收口 layer。

API 审查 R6 的职责委托分别沿 U1b（Buffer/import）、U2b（Program IO）、U3（remote/transport）、U4（关闭/cleanup）落地；唯一 admission/close owner 不复制。U2b/Remote 不变成 kernel eager 前置，但各条新路径当笔就须闭合保活。图保留仍归 C2，module/code 保留增加 S3 依赖。执行/错误与阶段次序见[融合后的 K2/S3 步骤](kernel-eager-open-work.md)，不在此维护第二张状态表。

### 6.1 实现步骤与 PR 边界（2026-09-27）

**先修正尚未合入的公开合同，再统一入口，再交付实际消费与回收。** 不为实现分层另造第二套公开 Tensor/Buffer，也不把内部 ABI 清理当作第一笔的前置。

```text
U1a  #2454 修订：TaskArgs 参数请求贯通
  ↓
U1b  统一公开 Buffer/Tensor 与本次解析、保留边界
  ├─ U2a 显式 HOST 消费 + 旧用例迁移 → U2b 本地共同 IO 记录 ─┐
  └─ U3  Remote 协议、身份/授权、COPY 接入 ──────────────────┤
                                      联合网络→H2D 验收 → U4 关闭语义收口
Kernel K1/K2 消费 U1、U2 所需能力，不等待全部 Remote 后端。
Capture 的图保留随 C2 接入；不重新实现 Pipeline A 的排队/完成/退休。
```

以下每一行按一笔 PR 组织；若某行的代码量仍过大，只沿同一合同的生产消费者拆分，不改变最终方案。

| 顺序 | 范围 / 代码入口 | 必须证明的完成条件 | 本笔边界 |
| --- | --- | --- | --- |
| **U1a：修订 #2454** | `TaskArgs.add_tensor` 的本次 transfer；本地封包、Worker/chip child materialize、re-export、四个 binder 的组合预检 | 同一 Tensor 两个请求互不污染；转发不丢请求；非法末项在任何源读取/复制前失败；旧 Program IN/OUT/INOUT 保持 | 不交付 HOST/NONE chip 消费；不扩充公开 ChipTensor 构造合同；Remote v4 无法表达的请求继续拒绝 |
| **U1b：一个公开对象模型** | 以 Tensor(buffer, ...) 为正式入口；源侧 Buffer.wrap 与目标 Worker 接入分离，迁移旧 buffer.tensor / ChipWorker 参数入口；复用 `Worker._materialize_l2_args` / ImportRegistry；本次绑定保留身份、grant、endpoint、地址和使用引用 | L3 child 与 L2 direct 提交同一类 TaskArgs；slice 共用 Buffer；错误 chip、过期登记、越界及 READ 配 OUTPUT 拒绝；借用 Buffer 不误 free | ChipTensor 只作内部 ABI/兼容适配；不重写设备调度 Tensor；未具备延后回收时保留在飞 free 拒绝 |
| **U2a：显式 HOST 语义** | HBG host get/set、Program H2D 绑定及用例；TMR/core 参数检查；PA/FIFO 等双侧用例迁移 | HOST/NONE 留 host；HOST/H2D 在编排中只有 device 绑定；DEVICE host get/set 拒绝；需要两侧的调用显式传两项；连续性/写重叠限制生效 | 移除 HBG 隐式跨侧 map/copy 必须与全部相关用例迁移同笔交付；未支持跨侧 view 继续报错 |
| **U2b：本地 IO 记录统一** | HBG tensor loop 与 TMR `copy_in_device_args` 的规则和 `tensor_leases` 接到共同源/目的/完成记录；接现有 prepare/finalize | H2D、OUTPUT 回拷、INOUT、失败回滚、A3 两侧最后使用及借用内存保留都有真实路径验证 | 保留复制时机与 Pipeline A admission；同步复制不冒充异步；无 PreparedCall/cache |
| **U3：Remote 接入** | 在 U1b 的共同边界上成对升级 producer/receiver/version、session materialize、owner/import 与 network COPY | location/tag/transfer/grant/owner incarnation 不丢；不把 host_tcp 的 HOST backing 伪装 DEVICE；非零子区间与身份一致；网络→H2D 的延迟/失败验收等待 U2b | 与 U2a/U2b 可并行；不增加 RemoteTensor 公共模型，不把 send 返回当目的 ready，不自动网络搬运 |
| **U4：统一关闭语义** | 统一 owner/import/attachment 的使用保留与 cleanup journal；Buffer.close 路由 owner registry；物理释放仍由原 allocator 负责 | logical close 拒绝新使用；accepted uses / 独立有效 grants / graph 保留结束后才回收；submit-close 竞争和未知设备状态安全；地址复用不通过旧 generation | U2/U3 引入每条路径时即须安全保留，不能拖到 U4 补洞；普通 Tensor 不延长 HeapRing scope；graph 验收依赖 C2 |

U1b 固定的是现有两类消费者需要的解析与保留边界，不是先造一个无人使用的传输框架。U2b/U3 的共同 COPY 记录按真实 H2D 与网络路径接入。内部分配器、传输机制、设备 POD 可以不同，公开对象含义和完成/所有权合同必须相同。

### 6.1.1 U1a 已合入与 U1b 当前交付

**#2454 已按下表修订并合入，merge `ec6a33dac915024f76115a4897878537935ad4a8`。** transfer 归属 TaskArgs 参数项，公开 Tensor 保持视图语义。下表记录已交付边界与后续范围。

| 处理 | 具体内容 |
| --- | --- |
| 保留 | `AddressSpace` 仅 HOST/DEVICE；独立 `TensorTransfer`；兼容旧 Program 行为；四个 binder 全调用预检；Remote 不能表达时明确失败；对应回归测试 |
| 修改 | 移除新公开的 `Buffer.tensor(..., transfer=...)` / `Tensor(..., transfer=...)` / ChipTensor 新 transfer 构造选项，将请求放进 `TaskArgs.add_tensor(..., transfer=...)`。更新本地封包与 materialize/re-export，使用显式参数序号贯通 |
| 内部 ABI | ChipTensor 可以暂留为内部编码；若私有搬运请求仍由它携带，须明确只在绑定期消费，不能把字段包装成新的公开 Tensor 语义。检查固定 POD、同构 mailbox 与跨版本 Remote 的各自兼容要求，不宣称换字段无需 ABI 审计 |
| 继续明确拒绝 | HOST/NONE chip 消费待 U2a；D2H 参数标签、DEVICE/H2D 及未知值；Remote v4 无法表达的新请求。HBG 旧跨侧行为直到 U2a 才删除，不宣称这一笔已完成访问策略 |
| 后续交付 | Buffer owner/借用包装与公开 ChipWorker 收口放 U1b；完整 HOST 用例放 U2a；共同 IO 与 Remote 升级放 U2b/U3；不扩成整套重构 |

新增的关键验收：同一个 `Tensor x` 连续用于 NONE/H2D/NONE 三个参数项后，x 的描述和前两项均不改变；TaskArgs 复制/转发后每项请求保持；显式 NONE 不被旧默认值覆盖；最后一项非法时前项的 host 地址可置为不可读地址以证明没有提前复制。测试走真实参数入口与 binder，不能只测试一个尚无消费者的工具类。

2026-09-28：U1a 已合入。U1b 第一笔 #2467 已合入，merge `255d78568`：修复现有 `Worker(level=2).submit(TaskArgs)` 的接受边界，而非新增独立参数模型。

- 先保存本次 TaskArgs 描述、方向、transfer 与 scalar 值的快照；整组预检通过后才解析。
- 复用 L3 的 live identity/generation、完整 descriptor、目标 chip 与 grant 校验；缓存的 import 不等于仍有使用资格。
- 校验身份和登记在飞引用与 device free 使用同一把 chip 锁；登记后释放锁，不把 native submit 的阻塞带进此临界区。
- free 在同一锁内重查在飞引用；绑定失败撤销本次登记，成功调用保留至 finalize。
- 这不是整个 U1b 完成：公开借用 Buffer 包装、旧 ChipWorker 参数入口迁移仍是下一笔；HOST/NONE chip 消费仍归 U2a。

验证：新增 13 项直接 L2 测试；修复前已有 7 项失败；去掉 free 锁内重查时竞态测试失败。完整 Python UT 2766 passed / 55 skipped；HBG readiness 在 a2a3sim、a5sim 各 4 passed。硬件设备全被占用，未运行板上验证。规划、HTML 与实验材料留在仓库外；代码提交只包含实现、必要测试与 API 参考。

### 6.1.2 U1b 第二笔：借用 device Buffer 的现有实现与待修订接口

已提交实现快照：PR #2475（`959f84a9`），分支 `feat/l2-borrowed-buffer`，基于 #2467 的合入点。以下记录该 head 的实现与验证，不是最终接口的验收。`Worker.borrow_device_buffer(ptr, nbytes, device_id=..., access=...)` 返回同一个 Buffer，所有 view 和 TaskArgs 复用既有路径。

- 注册已有地址，不分配，不转移 allocation 所有权，也不自动保活调用方 Python owner。调用方保证实际 device/capacity、地址稳定和真实使用期间的存活；异常不等于完成证明。
- 本地 live 登记范围不能重叠；同 backing 的多视图必须从同一 Buffer 派生。该检查不声称能识别 Worker 之外的所有别名。
- 私有 borrowed 标记决定不能 allocator free，不增加 wire 字段。`release_buffer` 与 submit/copy 使用同一 chip 锁，拒绝在飞撤销，撤销后旧 TaskArgs 不能复活。
- 仍是 Program Worker：close 可能 reset 设备；借用内存不是借用 context。调用方拥有的内存跨 Worker.close 的存活不在此合同中。
- 两种模拟器均使用 native allocator 表之外的外部地址跑实际 producer/consumer；onboard 分支通过低层 allocator 提供地址，验证公开 Worker 不误认其为自有登记。
- ChipWorker 的旧参数入口迁移仍待下一笔；这笔不宣称 U1b 全部完成。

验证：Python UT 2781 passed / 55 skipped；两种模拟器各 6 passed；静态检查通过。15 项新增 UT 覆盖借用合同、重叠、旧身份拒绝、释放与登记失败回滚。硬件 16 卡均占用，未运行板上；CI 以 PR 检查为准。

**2026-09-28 接口修订决定：#2475 的 Worker 级构造不固化为正式入口。** 保留借用不误 free、重叠检查、在飞保护、回滚及执行测试；调整为源 Buffer 登记独立、submit 内部接入。不是只重命名或搬移函数；这条设计决定的当前实现进度如下。

当前交付更新：#2478 的 native 部分保留与 re-borrow 修复已合入 main `3121da33`；#2475 已 rebase 至 `1c095ade`，剩 7 个 Python/测试/文档文件。正式 Tensor 构造独立为 [#2479](https://github.com/hw-native-sys/simpler/pull/2479)（已合入 main `7b614764b`）：原有 Tensor 类型接受 Buffer，兼容方法转发，共用几何校验；不分配/搬运/改变 identity。87 项 Buffer 测试、完整 Python 2774 passed / 55 skipped、两种模拟器各 4 项及 task-submit 板上 4 项通过。Buffer.wrap 源登记与 Worker 接入仍在 #2475 待实现，不能据此标记 U1b 完成。AIV 排查与接口推进独立：30 轮六用例测试及同一 Worker 的 18,000 次正常提交均通过，尚无稳定复现，未宣布修复。证据保留在 probes/aiv-borrowed-repro/REPORT.md。

构造接口收尾：[ #2480](https://github.com/hw-native-sys/simpler/pull/2480)（已合入 main `4d0610fb`）统一 `shapes=` 与正规导入 `simpler.task_interface.Tensor`，迁移入门示例 21 处，使用绑定函数持有的弱引用做懒加载类型缓存。完整 Python 2815 passed / 63 skipped，10 个 L2/L3 模拟器用例通过；L4 远端真机未验证，本轮无空闲 HBM 设备，未运行板测。主线已统一为 `shapes=`。


U1b 剩余交付按依赖排列：

1. #2479 先交付 Tensor(buffer, ...) 的同一构造与检查路径，buffer.tensor(...) 在迁移期仅兼容转发。该步骤不依赖新的源 registry；现有 Buffer 已可使用。多个 view 的 alias 判断共享 backing identity。
2. #2475 再交付源侧 Buffer.wrap 登记与目标 attachment 分离，Worker 在 submit 内验证并接入；同 backing 跨 Worker 不重新 mint identity，Remote 不接受裸 VA 伪造本地 Buffer。必须一次闭合 source liveness、endpoint/context generation、全调用预检、在飞/失败保留和关闭失效，不把方法搬家当作交付。
3. ChipWorker 公开参数入口收口到同一 TaskArgs；迁移仓内构造、示例和测试，再移除旧 buffer.tensor 和公开 ChipTensor 构造依赖。全程沿用 Pipeline A 的执行、完成和退休机制。

新增验收：构造 view 不产生 allocation/copy/transfer 副作用；正式入口与兼容入口的几何/范围检查一致；不同消费者不改变 canonical identity；源关闭后新 submit 拒绝，已接受使用按既定完成合同保留；裸地址声明不能绕过 source/provenance 验证。后续 HOST、IO、Remote 与 U4 的交付顺序不变。

### 6.2 错误与资源副作用

全调用结构/组合/权限预检在 host 内容复制、DMA、KernelLaunch 前完成；已建立资源失败后有逆序回滚记录。上报包含 arg index、backing identity/generation、source location、target endpoint、operation、transfer/profile、拒绝原因，不输出 tensor 内容或私有 transport key。

Python 入口抛异常，C++ 返回现有错误状态，HBG/TMR 走现有 fatal 通路。`LOG_ERROR` 不是成功返回；不允许失败后返回正常零值或继续 launch。错误分类至少区分 INVALID_ARGUMENT、UNSUPPORTED_ACCESS、STALE_REFERENCE、PERMISSION_DENIED、OUT_OF_BOUNDS，最终映射到仓库现有可表达的错误码，不需要另建错误生命周期。

### 6.3 必须通过的验收矩阵

| 组别 | 正例 | 反例 / negative control |
| --- | --- | --- |
| 本地 L3 → L2 | HOST NONE/H2D、DEVICE NONE；一个 backing 两个不同 transfer 参数 | HOST 送 device、DEVICE host get、错误 chip、同身份被改 descriptor |
| 权限与几何 | READ HOST 控制；合法 native stride；空 view | READ set、WRITE get、越界/溢出、未知 enum；非连续 H2D、写重叠 H2D |
| Remote wire | transfer/tag/权限/身份 roundtrip；inline 与 owner/import ref | 旧版本、漏 transfer、remote VA 本地 materialize、过期 session/owner |
| Remote 子区间 | 非零 export.begin + tensor offset；两次导入不同范围仍同 backing | 双重减 offset、权限升级、两范围互换；不同起点重叠不漏依赖 |
| 复制与映射 | 复制到已有目的保留其 identity；新分配副本才有新 identity；SHM import 保留源 identity | copy 冒充 alias；跨机器 SHM 名称伪装 peer；未实现 profile 被候选存在放行 |
| 统一 COPY | H2D/网络同一状态合同；网络→H2D 两段；既有目的重复更新 | send 返回被误当目的 ready；第二段失败却放行；中间存储提前复用 |
| 生命周期 | submit/close 竞争、多个 grant、公开 unmap 后已接受任务完成 | 在飞提前 free；丢失释放 reply 后复用地址；关闭一 grant 撤销另一 grant |
| host 准备 | L3 producer 完成后取 HOST；kernel 返回后源容器可销毁 | 提前 prepare 读前驱旧 HOST；HBG 等自身未 launch 的 producer |
| capture / MTP | graph 保活稳定地址；DEVICE 持久状态跨调用 | replay 假装重跑 host；workspace reset 清 KV；仅 AICPU 完成就 free |

当前代码证据与单测只验证现状，不作为新 wire、映射、capture 或跨机器硬件合同已交付的证明。

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
