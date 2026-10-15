# Tensor 映射视图合同

更新：2026-09-27。本文是 [Tensor 统一设计](argument-domain-policy.md) 的映射子合同。以下描述目标行为，**当前跨 HOST/DEVICE mapping 均不开放**。本地/Remote 释放的现状差异以主文 §5.1 为准，不把这里的保留语义冒充现有实现。

## 1. 映射视图独立建模，不归入搬运标签

之前只在表里写“跨侧映射拒绝”不够：那只说明支持状态，没有给出映射的对象模型。

```text
Buffer：canonical identity / generation / backing location / granted access
  └─ Tensor：dtype / shape / stride / offset；可选 mapping 引用
       └─ endpoint attachment：访问端点 / adapter profile / local address / 有效范围 / 权限
```

映射视图是同一 backing 在目标 endpoint 的访问入口。存储位置仍归 Buffer；视图可访问的侧归 attachment。Host 与 device 即使指向同一 backing，也可以具有不同虚拟地址。
不定义 HOST_MAPPED_DEVICE 或 DEVICE_MAPPED_HOST 位置枚举，也不把 MAP 放进 H2D/D2H 的 transfer 枚举。
概念上可沿用已有 ImportRegistry / adapter / capability 体系；这不是声明当前所有跨侧 attachment 已存在。

| 来源 backing | 目标访问域 | 方式 | 数据与身份 | 当前支持 |
| --- | --- | --- | --- | --- |
| HOST | HOST | 原地址或跨 host 进程共享映射 | 同一 backing，保留 identity | 沿用已有 host backend 和权限检查 |
| DEVICE | DEVICE | 本设备地址；已有 peer attachment | 保留 backing identity，地址限于有效 endpoint | 沿用已支持的 endpoint/peer 合同，不自动开放所有设备 |
| HOST | DEVICE | H2D 复制绑定 | 独立 device allocation，独立内容；保留显式源/目的关系 | Program 已有 IO 路径 |
| DEVICE | HOST | 显式 copy_from | 写入另一个 HOST backing | 保留既有 copy API；自动 D2H 参数标签仍拒绝 |
| HOST | DEVICE | 映射视图 | 同一 backing 的 device 访问入口；无独立 tensor 副本 | 暂不支持，LOG_ERROR 并返回失败 |
| DEVICE | HOST | 映射视图 | 同一 backing 的 host 访问入口；无独立 tensor 副本 | 暂不支持，LOG_ERROR 并返回失败 |

跨 host 进程的 POSIX_SHM 映射不是 host/device 跨侧映射，不能连同 L3 参数传递一起禁掉。AICPU 在设备上执行编排时属于 device 访问域，不因其是 CPU 指令就归为 host。

复制形成副本，后续写入不会自动传播。映射视图访问同一 backing，但这仍不保证无数据竞争或缓存自动可见：未来开放时必须同时满足以下合同。

- **别名**：保留原 canonical identity 和视图范围；不能为映射伪造无关 buffer 身份，绕开 L3 依赖检查。COPY 的目标则是独立 backing。
- **权限**：map_view 的 access 请求只能收窄，结果保存在 mapping grant；调用方向与实际操作不得超出 Buffer/grant 和后端能力。Tensor 只引用 grant，不新增 view_access。
- **地址**：只有映射成功后得到的目标 endpoint 地址可用于该端点；不能直接转型源指针，也不能假定数值相同。
- **就绪/可见性**：producer 完成、必要的 cache/coherence 操作完成后才可读；映射成功本身不提供这些保证，也不允许并发冲突写入。
- **寿命**：有效 view、在飞使用者及 captured graph 的引用共同保活 backing 和 mapping；最后使用者退出前不能 unmap/free。Generation/context 改变使旧 attachment 失效。
- **机制选择**：一次参数绑定明确采用 copy 或 mapped attachment，不能按平台能力静默切换；普通几何 slice/view 不创建跨侧映射。

完整映射合同在下面确定；当前实现只走支持策略的拒绝分支，不执行 map/unmap、缓存同步或 replay 映射保活。开放映射时不修改 Buffer 位置含义或用户参数类别。

## 2. 确定的公开接口与对象语义

以下是目标接口合同，函数名和参数名据此统一；尚未实现的入口报不支持，而不是假装产生 view。

```python
# Buffer 决定原始存储位置；Tensor 仍是唯一的 tensor 参数类型。
host_tensor = Tensor(host_buffer, shape=shape, dtype=dtype)
device_tensor = Tensor(device_buffer, shape=shape, dtype=dtype)

# 几何 view：同一 backing、同一 attachment；不会调用平台 map。
slice_tensor = tensor.view(...)  # 沿用各层现有几何 view 操作，不新增另一套布局算法

# 显式跨侧映射：返回的仍是 Tensor，附带已注册的 mapping 引用。
host_view = worker.map_view(device_tensor, target=host_endpoint, access=READ)
device_view = worker.map_view(host_tensor, target=device_endpoint, access=READ)

# 释放这次 map_view 获得的映射句柄；不是释放源 Buffer。
worker.unmap_view(host_view)
```

**map_view 返回新的 Tensor 描述，不原地修改输入 Tensor，不分配新的 tensor 数据 backing。** 原 device tensor 继续使用 device 原生地址；新 host view 使用映射引用在目标 host endpoint 解析地址。两者共享源 Buffer 的 canonical identity。映射关系直接引用 backing，不把原 Tensor wrapper 当作存储所有者，也不形成逐层 parent Tensor 保活链。

```text
源 Buffer 的 allocation 控制记录（DEVICE backing）
  ├─ 原 device tensor：native 参数描述
  └─ 独立 mapping 句柄：持有 backing lease
       ├─ 新 host tensor view：参数描述 + mapping 引用
       └─ 已接收的任务/graph：独立使用引用
```

源必须有可验证的 backing 生命周期合同。simpler 自有 Buffer 使用其 allocation 控制记录；外部 allocation 必须提供能实际保活存储的 owner lease。仅有裸 device pointer、尺寸或一个 Tensor wrapper 不足以建立映射寿命；不能获得这个保证的 map_view 请求明确拒绝。此限制不改变无映射 kernel 参数原有的 caller-owned 寿命合同。

`host_endpoint` / `device_endpoint` 是已有 endpoint 身份及 context generation，不是裸 HOST/DEVICE 字符串：host 进程 A 的 VA 对进程 B 无效，device 0 的 VA 也不自动对 device 1 有效。

`map_view` 的确定语义：

1. 输入必须是有效的、未附带跨侧 mapping 的 Tensor；它本身没有 transfer 属性。映射视图用于调用时，TaskArgs 参数项必须请求 NONE，不把 map 和自动搬运合并。目标须在另一侧；同侧切片、host 进程间导入和已支持的 device peer 使用原有路径，不经这个跨侧接口。
2. 校验来源 identity/generation、请求范围、权限、endpoint 关系及支持策略。当前在此返回 UNSUPPORTED，不调用平台 mapping API，不返回占位 view。
3. 支持后，由目标 endpoint 的管理者实际建立映射；成功返回前映射已建立。返回的是可路由的 mapping 引用，不把另一个进程的指针当作本进程可用地址。
4. 输出 Tensor 保留原 BufferDescriptor、dtype/shape/stride/offset，附带 mapping 引用；权限由该引用指向的 grant 保存，不在 Tensor 新增权限字段。只改变访问入口，不改变原 Buffer、其他 Tensor 或用户数据。
5. 不复制 tensor 内容，不等待尚未完成的 producer，不允许“map 不支持就自动 copy”。

需要两侧消费时，原 Tensor 与映射后的 Tensor 是两个显式参数；不在一个 Tensor 中暗藏可供 get/set 自动切换的 host/device 双指针。当前 map_view 不支持，所以仍需两个分别具有 HOST/DEVICE backing 的参数，或通过 Program H2D 建立独立副本。

## 3. 描述格式：声明与已解析地址分开

| 对象 | 必须携带的语义 | 不允许混入 |
| --- | --- | --- |
| BufferDescriptor | canonical identity、原位置、backing 大小、所有者权限、已有 backend 描述 | 逐调用 transfer、某个消费者的映射地址 |
| Tensor 视图 | Buffer 描述引用、几何、可选 mapping 引用 | 本次 transfer、未经 endpoint 验证的直接寻址、第二份 host/device tensor 类型 |
| TaskArgs 参数项 | Tensor、调用方向/tag、本次 transfer | 改写源 Buffer 位置或身份 |
| Mapping 记录 | owner-issued id/generation、源 Buffer identity/generation、目标 endpoint/context generation、profile、映射范围、授予权限、生命周期状态 | 与 backing 无关的新 allocation identity |
| endpoint 本地 attachment | 平台映射句柄、本地 base、覆盖范围、实际能力、可见性规则、引用计数 | 跨 endpoint 复用的裸指针 |
| 已解析 L2 / runtime tensor | 本次操作实际可用的地址、访问域与几何 | 用源 backing location 判断这个已解析地址能被哪侧访问 |

**所有公开层级传同一种 Tensor，实际消费 endpoint 解析地址。** 同一个 mapping 引用可被合法的同目标参数重复使用；派生几何 view 共享它，不重新 map。传到错误 endpoint/context、使用旧 generation 或扩展范围/权限都拒绝。

L2 必须把“已解析地址的访问域”与“原 backing 的位置”在术语和实现上分开：例如 DEVICE backing 的合法 host mapping，L2 host 参数使用的是 HOST 可访问地址。源 backing 位置与身份保存在原 descriptor 和 binding/lease 中，不靠篡改 Buffer.address_space 通过检查。拟将 L2 这个意义明确命名为 access_space，仍使用 HOST/DEVICE 值域；它不是第二个物理位置。

一个逻辑 kernel 可能有 AICPU/AICore 多个实际消费者。目标 device attachment 必须覆盖这次调用所需的消费角色，满足共同 descriptor 的寻址合同；只证明某一个处理器能读不够。无法提供所需共享地址或可见性时拒绝该 profile，不能把一侧 VA 当作另一侧地址。

Wire 编码必须同时更新生产者、解析者和版本/能力校验。不借 reserved padding 静默塞字段；不序列化 STL 容器或 host ownership 指针；接收方不认识 mapping/transfer 时拒绝。映射引用验证和 backing 身份验证是两件事，二者都不能省。

## 4. 组合裁决：标签不能偷偷改变访问方式

| 参数形式 | 有效目标 | 结果 |
| --- | --- | --- |
| Native HOST + NONE | HOST | 同侧访问 |
| Native DEVICE + NONE | 已授权 DEVICE endpoint | 同侧/既有 peer 访问 |
| Native HOST + H2D | DEVICE | Program 创建独立 device 绑定 |
| Native DEVICE + D2H | HOST | 自动复制标签暂不支持，拒绝 |
| HOST backing + DEVICE mapping + NONE | mapping 指定的 DEVICE endpoint | 设计合法；当前能力未实现，拒绝 |
| DEVICE backing + HOST mapping + NONE | mapping 指定的 HOST endpoint | 设计合法；当前能力未实现，拒绝 |
| 任意 mapped 参数 + H2D/D2H | 任意 | 参数绑定同时指定映射与复制，拒绝；要复制须用独立显式 copy 操作和目标参数 |
| mapped view 再跨侧 map | 任意 | 拒绝映射链；从原 native tensor 分别建立所需 view |
| mapped view 传给原侧或其他 endpoint | 与 mapping 目标不符 | 拒绝，不自动回退到源地址；原侧传原 Tensor |
| 任意 view 提权、扩大授权范围、引用过期 | 任意 | 无效参数，拒绝 |

当前支持策略是一次明确的裁决。两个映射方向都是已知合法模型但暂不支持，不将它们归为未知 location。LOG_ERROR 应区分 UNSUPPORTED 与 INVALID_ARGUMENT；沿用已有错误返回/异常通道，不扩展一套独立错误协议。

## 5. 顺序与可见性合同

Mapping 是 live alias，不是 snapshot。它不让 host 读提前完成，也不让 device 在 HBG 构图中途先执行。

| 数据流 | 完整顺序 |
| --- | --- |
| HOST 写 → DEVICE mapped read | host 写完成 → backend 要求的写入发布/可见性动作 → 有序 device launch → device read |
| DEVICE 写 → HOST mapped read | device 写完成及必要发布动作 → 覆盖 producer 的完成边界 → 调用方等待 → host 侧必要可见性动作 → host read |
| 两侧可能写同一范围 | 调用方或已有显式 DAG 依赖建立互斥顺序；仅持有两个 view 不允许同时冲突访问 |

不额外创建全局跨 run tensor 冲突推断系统。L3 已声明的 DAG 依赖和 canonical identity 别名检查继续生效；外部调用方及跨 run 的未声明关系仍由调用方负责。

Backend profile 必须明确规定其 release/acquire 可见性动作：硬件保证一致时动作可为空，非一致时必须落到具体可执行的位置。Runtime 在已声明的准备/执行/完成边界做这些动作；不能在 host get 中偷加 stream wait 或 D2H。若无法保证某个写入方向的可见性，就不授予对应能力并返回不支持。不能以“地址已经映射成功”替代这份合同。

调用方承诺 host 读取前已经完成必要等待。Runtime 不可能仅凭裸指针自动证明外部 stream 的就绪性。HOST buffer 若可能被 device 写入，即使原 host 地址一直有效，仍须遵守这条顺序。

映射来源 HOST 存储须是可注册、寿命明确的 Buffer；不能把临时 host 值快照参数悄悄升级为可被 device 长期访问的 caller 容器。simpler 不取得 caller host 容器的所有权。

## 6. 所有权、关闭、失败

映射是独立的资源句柄，不是源 allocation 的所有权转移。跨进程建立映射时，负责该 endpoint 的 runtime 管理本地 mapping，调用方持有 owner-issued 句柄。

```text
创建前：全量静态预检 → 支持/后端检查
创建失败：清理已取得的部分资源，返回错误，不公开 view
创建成功：ACTIVE（允许派生 view 和提交）
unmap_view：CLOSING（拒绝新的派生和提交；已经接收的使用者继续保活）
最后一个已接收使用者退出：实际 unmap → CLOSED
```

- 原 Tensor descriptor 的销毁不结束 backing 或 mapping；host view 的寿命不依赖这个 wrapper。不得原地修改原 Tensor 的地址、位置或权限。
- free/close 源 Buffer 结束的是源 owner 句柄，拒绝通过它新建 native 绑定或 map。已存在且仍 ACTIVE 的 mapping 句柄拥有独立 backing lease，可继续按其授权使用；物理 free 延迟到全部保留义务退出。
- unmap_view 只结束对应 mapping 的公开授权，不释放原 Buffer owner。派生的 Tensor descriptor 即使还在，也不能据此重新取得新使用引用；过期引用必须报错。
- 一个 map_view 调用获得一个显式句柄；对其 unmap 使该句柄派生的所有 Tensor view 不能用于新的提交。不能靠 Python 对象引用数量定义跨进程协议。
- 内部可以复用同一物理 mapping，但每个公开句柄的关闭不能撤销另一个有效句柄的授权；复用键包含 backing generation、endpoint/context generation、profile、覆盖范围及权限兼容性。
- 有效句柄、已接收的 host 准备、device submission、状态读取和 captured graph 都是保留义务。A3 要覆盖 AICPU/AICore 两侧实际使用期限。
- 原 backing 在这些引用存续时不能物理释放；API 可以结束调用方的 owner 句柄并阻止新派生，但真正释放推迟到所有引用结束。外部借用存储仍要求外部所有者履行同样寿命合同。
- 未 map 成功不产生可用句柄。若部分映射回滚失败，runtime 保留受隔离的清理记录，不返回成功，不遗失仍需处理的资源。
- 正常 unmap 失败时保留原平台句柄与清理状态，拒绝新使用，允许已有清理路径重试；设备/context 状态不确认时不能再把地址投入复用。
- generation/context 的有效性检查和取得使用引用必须作为一次受保护的操作；不能先检查 ACTIVE，解锁后再使用一个已被 unmap 的地址。

这些是映射支持必须遵守的语义，不要求当前拒绝映射的实现创建整套 mapping manager。

## 7. Capture/replay 合同

1. map/unmap 是 capture 外的资源操作。Capture 开始前 view 必须已解析，地址和映射代际稳定；capture 内请求新映射直接报不支持。
2. 图记录 device 可执行地址及完整设备顺序，并持有 backing + mapping 使用引用；原 Tensor wrapper 的 host 生命周期不被转移给图。
3. 调用方 unmap 结束公开句柄，但图先前取得的引用继续有效，允许已有图 replay；真正 unmap 晚于图销毁和最后一次 replay 完成。新图不能从 CLOSING 句柄取得引用。
4. Reset/context replacement 使旧代际图失效；不能用同值的新地址冒充旧 mapping。图 wrapper 在允许 replay 前校验执行域 generation。
5. HOST backing 的 device mapped view 是 live 数据来源。连续 replay 读新内容仍要求 host 写与各次 device 读取有序；图拥有 mapping 不等于拥有数据快照。
6. DEVICE backing 的 host mapped view 仅让 host prepare 能合法读当前值。Capture 仍不会重放 host get/set；依赖每次 device 结果重新构图的调用不能因此变成可 replay，必须放 device 控制或显式拆调用。
7. Backend 所需可见性动作若不能纳入 capture/replay 的合法有序路径，该 profile 的 capture 能力为不支持；不能由 eager 能跑推断 capture 能跑。

## 8. 与 Remote 和现有 capability 的衔接

同机 host SHM import 与跨侧 map_view 共用 backing/grant/attachment 的内部模型；公开入口仍按用途区分。同机 SHM 不证明跨机器映射；peer device profile 在当前 DefaultRegionAccessService 中仍拒绝，不能将“沿用 peer 合同”写成已交付 peer 能力。

沿用 EndpointIdentity 的 session_instance_id / registry_epoch / endpoint_id，并在 attachment 登记目标 device context generation。Buffer 的 owner incarnation 和 endpoint session 身份各有作用，不能互相替代。

attachment 存 owner-relative 的覆盖范围，local_base 对应范围起点；Tensor offset 始终保留 owner 坐标。不同导入范围不能改写 canonical backing 大小。读取地址 = local_base + (tensor.byte_offset - attachment.begin)。

主线调查 `docs/investigations/2026-09-hbg-per-run-host-view-rebuild.md` 已否定逐 run 重建映射的性能假设。本设计的显式映射按 backing/endpoint/grant 生命周期保留，可复用物理 attachment；不把禁用隐式映射误称为平台无法映射，也不重新引入每调用 map/unmap。此性能结论不使当前产品支持策略自动放开。

## 9. 统一访问层中的 MAP 合同

map_view 与 remote_import 都转为 [共同访问请求](endpoint-access-contract.md) 的 MAP，返回同 backing 的目标 attachment/grant；平台 H2D/D2H 与网络复制则转为 COPY。MAP 不能用 COPY 返回一个“看起来相同”的 Tensor：两者后续写入是否共享、完成与寿命不同。

同 backing 的权限、identity、owner-relative 范围在多次导入中保持；目标地址和 transport token 属于 adapter。跨 host/device 映射当前仍拒绝，共同层的存在不扩大 profile 支持范围。

map_view(..., access=READ) 保留为建立映射授权的请求参数，remote import 的 access 参数同理；它们不构成每次提交需要重复填写的 Tensor 权限字段；内部 ChipTensor 也不新增该字段。
