# 跨 endpoint 访问与复制：共同合同

更新：2026-09-27。状态：选定设计，尚未完整实现。本文落实 [Tensor 统一设计](argument-domain-policy.md)，由 [运行时原则](runtime-design-principles.md) 约束。源码事实仍使用 2026-09-24 核查的 main `c605b03c`，本次没有把旧证据重新标为最新主线。

## 1. 同一种问题，不同访问机制

Remote 和 DEVICE 都可能是消费者当前不能直接解引用的存储。统一抽象是 **source backing → target endpoint 的访问请求**；网络、H2D/D2H、同机共享内存、设备地址解析是不同机制。

```text
                                一份 Tensor 参数列表
                                         │
             本地 Program / kernel / remote callable / 显式 copy 或 import
                                         │ 入口转换，不推断新授权
                             统一访问请求与合法性判断
                           ┌─────────────┼─────────────┐
                      NATIVE 消费     MAP 同一存储    COPY 到目的存储
                           │             │             │
                   已有本地 attachment   │     源/目的引用 + 复制动作记录
                           │             │             │
              backend adapter：local / SHM / H2D-D2H / remote transport
                           └─────────────┼─────────────┘
                             目标绑定 + 顺序 + 使用保留
```

公开对象始终是 Buffer 与 Tensor，正式 view 构造为 `Tensor(buffer, ...)`；源侧 Buffer 登记与目标 attachment 分离，目标 Worker 在 submit 内验证接入，同 backing 身份不随消费者改变。TaskArgs 参数项提供本次 transfer，绑定层把它翻译为下面的内部请求。BufferDescriptor 只负责传输存储描述，ChipTensor 可作为绑定结果的内部 ABI 编码。

这三个 operation 是内部请求语义，**不是新增用户逐参数角色表，也不替换 AddressSpace / AccessMode**。

位置拆成 node/endpoint 与 HOST/DEVICE；remote 是 source/target 关系。机器 B 的 DEVICE 同时具有“远端”和“device”属性。H2D 不隐含跨机器；一次显式跨机器复制可以包含 NETWORK → H2D 两段。

| 源 → 目的 | 确定的语义 | 机制例子 / 支持条件 |
| --- | --- | --- |
| A.HOST → A.DEVICE | 复制 | H2D；源读、目的写、device 顺序 |
| A.DEVICE → A.HOST | 复制 | D2H；device producer 完成、host 可见 |
| A.HOST → B.HOST | 复制 | 网络传输；接收端完整提交后可读 |
| A.HOST → B.DEVICE | 复制 | 网络到 B.HOST，再当地 H2D；或经过验收的直接传输 profile |
| A.DEVICE → B.DEVICE | 复制 | 已实现直接路径；否则明确已支持的 D2H → 网络 → H2D 组合；无完整路径就拒绝 |
| A 的 backing → B 的映射视图 | 同一 backing 的别名 | 必须有可兑现的 mapping/import profile，不能自动换成上述复制 |

后两行规定合同，不宣称当前已有这些网络/peer 后端。当前 host_tcp 的 SHM import 要求共享 SHM namespace，仍不能跨机器充当映射。

## 2. 一个内部接口，两类结果

在现有 Buffer/endpoint 判断与运行绑定之间建立共同接口。名称是目标内部接口，非当前已存在的函数：

```text
AccessRequest {
    operation: NATIVE | MAP | COPY
    source: Tensor reference + backing/grant identity
    target_endpoint: EndpointIdentity + device context generation
    destination: existing Tensor reference | explicit allocation request  // COPY only
    required_access: derived from existing tag/signature/operation
    source_range, destination_range, layout
    execution_scope: host operation | program invocation | kernel invocation
    capture_policy, ordering_context, budget
}

validate_access(request) -> ValidatedAccess | error
bind_access(validated)    -> BoundAccess | error
issue_copy(bound_copy)    -> per-operation completion state | error
retire_use(binding, completion_proof)
```

`required_access` 是内部从已有调用方向或 copy/map 操作导出的检查输入，不是新的用户参数，也不序列化为 ChipTensor.access。授予权限查既有 Buffer/import/mapping grant；需要延续到 host 访问点的约束保存在本次绑定记录。不增加独立 Tensor.view_access / ChipTensor.effective_access。

`ValidatedAccess` 只是本次请求的检查结果，不是可缓存或多次 submit 的 PreparedCall；静态检查不取得永久授权。`bind_access` 必须在 registry 保护下重验 generation/状态并取得使用引用，然后才可异步执行。异步间隔不能靠先前的一次校验跨过 free/unmap。

**两类结果：**

- NATIVE/MAP 返回同一 backing 的目标 attachment、有效范围、权限和使用保留。NATIVE 只解析该消费合同允许的 native/same-side 导入，不暗中创建跨 HOST/DEVICE 映射；MAP 明确申请新 grant。
- COPY 返回源/目的绑定、实际传输步骤、临时 buffer 记录和完成状态。COPY 不产生源 backing 的 direct-load 权限，复制通道不伪装成可解引用地址。

负责 allocator、remote session、device context 的既有 owner 不转移给共同接口。接口统一校验和资源记账，adapter 调用各自 owner 的原有分配、发送、RTS copy、查询和清理操作。不为跨 endpoint 传输新建一套全局 scheduler。

### 2.1 请求、backend profile、完成机制各司其职

| 层 | 必须处理 | 不应处理 |
| --- | --- | --- |
| 入口转换 | 将已授权 API/参数合同翻译为 request | 因检测到远端就偷偷发网络复制 |
| 共同判断 | 身份、精确 endpoint、范围、方向、产品策略、可用机制 | 用 Python 可达对象代替 allocation lifetime |
| 共同绑定/记录 | 每一步源/目的/临时存储及使用引用；失败清理记录 | 将地址整数作为跨 endpoint 身份 |
| backend adapter | 机制-specific 执行、进度、可见性和清理证明 | 放宽上层 grant；改换 COPY/MAP 语义 |
| 原有 run/stream 调度 | producer/consumer 顺序、完成查询、退休 | 把远端 ACK 假装成 RTS event |

复用 `BufferAccessQuery`/`buffer_adapter_candidates`/`DefaultRegionAccessService`。对复制，判断必须覆盖源读取和目的写入两端，不能只查“consumer 能否访问源”。`COPY_TO/COPY_FROM` 始终相对于被操作 backing：H2D 要求读 HOST 源、写 DEVICE 目的。

每个 profile 必须明确：支持的 endpoint 关系、operation、layout、执行及可见性机制、是否能提供 device-side 等待、capture 能力，以及哪些完成证明可用于回收。仅在枚举中出现不算支持。

### 2.2 公开入口如何归一

| 现有/目标入口 | 转成什么请求 | 保持的用户合同 |
| --- | --- | --- |
| HOST+NONE / DEVICE+NONE 参数 | NATIVE 到实际消费者 | 不授权额外复制，不允许隐式跨侧 get/set |
| Program HOST+H2D 参数 | 明确分配 DEVICE 绑定；IN/INOUT 的 COPY 入，OUT/INOUT 的 COPY 回 | 保留 Program IO；纯 OUT 不读初始 host 内容 |
| Kernel DEVICE 参数 | NATIVE 到 caller 指定执行域 | 不接管 tensor allocation，不隐式 H2D |
| Worker.copy_to / copy_from | COPY 到/从既有 chip backing | 保留各 API 已有同步/异步返回合同；不假装已经新增 async API |
| remote_copy_to / remote_copy_from | COPY 到/从指定远端 backing | 显式网络操作；远端实际位置决定后续需要哪种当地步骤 |
| HOST_INLINE | 发送入口值快照，再 COPY 到接收端新 HOST backing | 只读输入；接收后的 NONE/H2D 标签独立保留 |
| remote_import / map_view | MAP 到指定 endpoint，返回同 backing 的新视图 | 不支持就失败，不能改成副本 |

H2D 只保留为 Program 的局部参数绑定要求。共同 COPY 层使用 source/destination endpoint 与 location，不扩张成 `REMOTE_H2D`、`NETWORK_D2H` 等组合枚举。上层明确要求发送到 remote worker，并且参数明确要求当地 H2D，才授权对应两段路径。

## 3. 多段复制的确定流程

以 A.HOST 的输入，经网络送到 B.DEVICE 为例：

```text
H_A ── NetworkCopy ──▶ H_B ── H2D ──▶ D_B ── device consume
        │                 │                │
  SourceReadDone_A  DestinationReady_HB  DestinationReady_DB
                          │                │
                 H2D SourceReadDone_HB    consume Done
                          │                │
                  可回收内部 H_B       可退休本次 D_B 使用
```

1. 入口确认网络发送与目标 device 绑定均获授权；校验整条路径、预算、权限、几何，未支持的一段在传输前失败。
2. 绑定阶段取得源与目的引用，分配必要中间存储。先登记清理义务，再公开异步工作；不能只有裸指针列表。
3. 等待源 producer；NetworkCopy 执行后，只有 `DestinationReady_HB` 才能启动读取 H_B 的 H2D。
4. 只有 `DestinationReady_DB` 才能启动 device consumer。若后端不能形成合法等待边，则在既有 host 调度位置等完成再提交，不在 device kernel 中途回 host 补工作。
5. H_B 最早在网络写与 H2D 读取均已结束后回收；本例两者顺序已建立，因此 H2D SourceReadDone 是最后一步。D_B 还要覆盖真正 device 消费及 Program 输出回拷。
6. 网络 send 返回不等于目的可用。发送缓冲可以保留到 send API 定义的源读取完成；若没有更细证明，保守保留至整次复制已证明完成。

**这些顺序约束不要求逐段 host 等待。** 后端若提供合法的 stream/event 顺序和失败阻断，可以提前 enqueue 后继，让设备等待前驱。`DestinationReady` 限制实际消费开始，不限制 host 提前提交的时刻。只提供 host completion 的网络后端，由既有 host 调度在接收完成后提交下一段；不得用忙等 host ACK 的 device kernel 伪造 bridge。

提前排队还必须证明异步失败不会把不完整目的交给 consumer；单纯 record 一个 event 不自动具备成功门控。复用已有执行域错误/停止机制，未能证明该合同的 profile 不开放提前 enqueue，采用已证明安全的完成后提交路径。未确认静止的已排队任务继续保留资源。

直接网络→device 后端若将来可用，可以替换 COPY 内部步骤，前提是复制的范围、布局、授权、返回值、可见性与源/目的寿命合同不变。当前选择已验收路径；不因一次 RPC 失败就临时换路径重写目的。

### 3.1 三个完成事实，禁止合并成一个布尔值

| 事实 | 证明什么 | 允许的动作 |
| --- | --- | --- |
| SourceReadDone | 该复制不再读取源 | 释放这一段的源使用引用；其他任务仍可能保留源 |
| DestinationReady | 该复制目的完整且对目标可见 | 触发依赖它的消费；不表示 consumer 已完成 |
| OperationQuiescent | 所有已发起步骤都不再访问资源 | 允许失败清理或最后退休；不等于操作成功 |

以上是逻辑事实，不要求三个新物理 event。同步 memcpy 可在成功返回时同时满足；RTS 异步 copy 由对应 stream 完成证明；网络 ACK 必须按 receiver 合同解释。原有 Pipeline A 的 run 完成不能替代尚未结束的复制证明，反之复制完成不能替代 AICPU/AICore 完成。

完成引用限定 endpoint/session/context generation 与本次 operation 标识。Backing generation 描述 allocation 代际，**不随每次复制递增**。不得用上次相同地址的完成记录放行本次数据更新。

### 3.2 预算与背压

每次路径绑定记录实际目的新分配 bytes、临时 HOST/pinned bytes、临时 DEVICE bytes、在飞 transport 数量，以及实际需要的 completion 对象。按 placement 归属计费，共用 backing 不重复计算物理字节。

预算不足在接纳前阻塞或明确拒绝，取决于外层既有 API 合同；不是依赖 RTS 队列提供 WouldBlock。资源绑定允许失败回滚，但任何一步 DMA/send 发生前须保证其后续必要存储已有保留。中间 buffer 不能因“已进入网络队列”就提早复用。

### 3.3 失败、取消和重试

状态至少区分未提交、部分提交、已证明完成、失败且已静止、失败但静止性未知。某一步失败后不提交后继消费；已提交写入无法回滚成“从未写过”。对已有目的 Tensor，相关范围标为本次结果不可用，不能继续沿用旧值的成功状态。

取消停止新的步骤，不撤销已经发出的 DMA/RPC；保留引用至 drain/静止证明。超时而无静止证明时保留 cleanup journal，拒绝复用相关资源。沿用现有 owner/session 故障处理，不新增“网络超时就释放 device buffer”的规则。

默认不透明重试已部分提交的写入。只有后端能以 operation id 去重或提供可证明的覆盖/顺序合同，才可在同一请求内重试。公开 free 的幂等性不能推导为任意 COPY 幂等。

## 4. 身份、视图和内容各自稳定

**COPY 写入既有目的时保留目的 identity；申请独立副本才产生新 backing identity。** 后续读写不会自动传播，host/device 两份内容的一致性靠显式复制与顺序。

MAP 保留源 backing identity，产生目标 attachment/grant；普通 Tensor view 保留 identity 并改变几何。Remote transport token、import_id、本地 VA 和 network sequence 都不替代 backing 身份。

同一 backing 的别名仍用 owner-relative footprint 判断；中间副本有独立身份。复制记录显式关联源/目的，不能为“看起来是同一个逻辑 Tensor”而给副本共用 alias key。底层复制原语不默许重叠源/目的；同 backing 的重叠 copy 当前拒绝，除非某 profile 明确实现并验收了对应语义。

复制范围还必须包含布局合同。当前不支持的 strided H2D/managed copyback 继续拒绝；网络携带 stride 不使下游 H2D 自动正确。所有步骤使用一致的数据布局解释，不能网络传完整 span、device 按 numel 分配然后保留旧 stride。

## 5. 与 Pipeline A、Kernel、Capture 的衔接

共同访问接口复用已有 run/stream 的依赖和退休位置。可以并行开发静态请求、拒绝规则、adapter 接口、wire 与保留记录；消费已合入完成机制时逐后端验收，不修改 Pipeline A 的 admission 或另建 runner。

Host prepare 只在数据依赖满足后读内容。可以提前验证 descriptor、选择机制和预留资源，但不能提前快照前驱未写好的数据。

统一 COPY 合同不扩大 Kernel 的资源职责：Program adapter 负责创建 IO buffer 和复制；kernel 执行核心只消费已合法绑定的数据和 simpler 内部执行资源。Metadata 发布仍属于 runtime 执行协议，不能以“Kernel 禁止用户 IO 搬运”禁止 metadata H2D。

Capture 要求每一条被捕获操作及其等待边都能由 device replay 完成。普通 host RPC、网络接收回调、Python host 调度不能因为被包装为 COPY 就变成可捕获 task。当前这类工作放在 capture 外，并在 graph 使用输入前显式证明目标可用；没有合法 bridge 就拒绝在 capture 中接入。

直接网络/device 集成若未来提供真正可捕获的后端，仍须单独验收 generation、图保活和每次 replay 的数据版本。Eager 支持不等于 capture 支持。

## 6. 实施与验收入口

[统一设计 §6](argument-domain-policy.md) 给出唯一实施顺序；本页不另维护一套阶段状态。必须覆盖：

- 同一套 COPY 合同用同步 fake、异步 device fake、异步 remote fake 验证；不能只测公共结构构造。
- 延迟 receiver commit：send 已返回，consumer 仍不能执行；延迟 H2D：H_B 不得复用。
- 复制到已存在的 D_B 两次：identity 不变，本次完成不能被第一次完成冒充。
- 第二段失败/取消：前段写入不冒充整体成功；未知静止状态下源/中间/目的引用不丢。
- MAP 请求不可降级 COPY；HOST+H2D 不单独授权跨机器传输；kernel 不因可用 adapter 就开始自动搬运用户 tensor。
- 本机复制无需走网络，remote 控制协议不绕过权限；同一接口不增加无必要的 RPC 或全局锁。
- SourceReadDone、DestinationReady、OperationQuiescent 可独立模拟，验证最后使用者再回收。
- 普通网络路径 capture 拒绝；capture 外更新与 graph 消费必须有可验收的顺序。
