# 通信域的成员模型：level 是逻辑概念，能力挂在 endpoint–resource 边上（契约）

> **发布快照：2026-10-18。** 设计、实现状态与历史调查须按正文各自基线阅读；不代表当前 main 全量验收。参见[阅读入口与已知更正](../README.md)。
>
> **状态**：⬜ **设计契约提案**。这是 [cpu-npu-shared-memory.md](../cpu-npu-shared-memory.md)
> 的 §3.0 展开。
> **为什么单列**：它回答「**L4 建的是 L3 的域还是 L2 的域？**」这个跨层级抽象问题——
> 比拓扑/布局更基础，且**独立于**本设计的其余部分成立（任何跨层级共享内存机制都需要它）。
> **约束依据**：[hardware-constraints.md](hardware-constraints.md)。

## 1. 问题：为什么"按层级定义成员"是错的抽象

现有 API（`create_worker_chip_region` / `create_worker_chip_queue`，#1681 前叫 `create_l3_l2_*`）把**三件正交的事焊死在一个名字里**：

 | 被焊死的三件事 | 现状 |
 | -------------- | ---- |
 | **谁创建**（协调者 / 生命周期所有者） | L3 |
 | **谁参与**（成员集） | L3 + 单个 L2 |
 | **什么传输**（后端） | host↔device |

由此产生一个无法回答的问题：**"L4 建的是 L3 的域还是 L2 的域？"**

该问题预设了"域成员坐落在某个层级上"，但——

> ### 内存不坐落在层级上，内存坐落在设备上

说"一个 L3 的域"是**歧义的**：是这些 L3 **宿主进程的 CPU 内存**在共享，还是它们底下
那些 **L2 的设备内存**在共享？**歧义本身即证明该抽象是错的。**

## 2. 根因：**level 是逻辑概念**

> **worker 的 level 是编排树中的位置，是纯逻辑概念，不携带物理含义。
> 部署（CPU / NPU）与物理互连只决定访问潜力的第一道门槛；完整 potential capability
> 还取决于 backing/backend 与 runtime support，actual capability 还必须有授权、live
> Attachment/materialization、runtime probe 和 lease。**

### 2.1 三层表述（勿混淆）

 | 层面 | 状态 |
 | ---- | ---- |
 | **概念上** | level = 编排树中的位置，**纯逻辑**，不含物理含义 |
 | **当前实现** | level **事实上耦合了部署**：`level == 2` ⇒ `ChipWorker` ⇒ NPU。见 ✅ `python/simpler/worker.py:3126, 3151, 3173, 3184, 3460` 等大量 `level==2` / `level>=3` 语义分派。**这是历史包袱，非本质** |
 | **本设计取向** | 通信能力**不看 level**；potential/actual 判据按 §3 的完整公式计算 |

### 2.2 ✅ 实证：同一编排角色，仓库里已存在两种部署

 | runtime | 编排跑在哪 | 代码 |
 | ------- | ---------- | ---- |
 | `tensormap_and_ringbuffer` | **设备侧 AICPU** | `src/{a2a3,a5}/runtime/tensormap_and_ringbuffer/aicpu/aicpu_executor.cpp` |
 | `host_build_graph` | **主机侧 CPU** | `src/a2a3/runtime/host_build_graph/host/runtime_maker.cpp`、`runtime/orchestrator_core/pto_orchestrator.cpp`（并有 `host_orch_compat_stubs.cpp` = host-orch 迁移） |

**同一逻辑角色、不同部署位置** —— 这不是理论推演，是仓库现状。

### 2.3 ⚠️ 陷阱警告（给实现者）

**当前实现里 level 与部署的耦合是个陷阱**：实现者读 `worker.py` 会自然而然认为
"level 就是部署"，从而**又把 API 按层级设计一遍**——正是 §1 那个坑。

**判定规则**：任何以 level 为条件决定"能不能共享内存"的代码或 API，都是错的；
它应先以**部署 + 互连**判断物理门槛，再纳入 backing/backend、runtime support、授权和
live Attachment 等条件。

### 2.4 但 level 仍然承重——只在另外两件事上

 | level 决定 | 说明 |
 | ---------- | ---- |
 | ✅ **控制 / 分发树**（可见性） | L4 只 `submit` 给 L3 —— 编排契约，该守 |
 | ✅ **命名 / 寻址** | 路径 `L4/L3[2]/L2[5]` 的层次结构 |
 | ❌ **通信能力** | 由 deployment/interconnect/backing/backend/runtime support 与 live Attachment 条件共同决定 |

> **前两件是逻辑的（level 说了算）；第三件由物理潜力与实际 Attachment 共同决定，
> level 不参与判定。**

## 3. 契约：域 = **MemberSet + ResourceBundle + AttachmentPlan + TransportPlan**

`DeviceRankMap` 只在需要 device collective 时作为可选组成加入。

> ### 成员由 `EndpointId = session_id + worker_path + deployment` 定义，不由层级定义
>
> ### 不再是「一块所有成员都能访问的内存」
>
> `halHostRegister` 不支持 VMM VA
> （[hardware-constraints.md](hardware-constraints.md) §5），所以**同一块 backing
> 不可能既被 VMM peer-import、又被 host-register**。因此本文目标中的 CPU↔NPU
> 混合访问不能只靠一块 backing，域模型必须允许组合多个 backing。
>
> [feature_roadmap.md](../feature_roadmap.md) 给出的规范形式是：
>
> ```text
> EndpointId       = session_id + worker_path + deployment
> DomainId         = stable id + generation/epoch
> MemberSet        = ordered logical EndpointId set
> ResourceBundle   = resource_name -> Buffer | communicator | adapter state
> DomainRegionSet  = ResourceBundle 中按 owner 组织的 Buffer-backed entry 子集
> AttachmentPlan   = (member EndpointId, CanonicalBufferIdentity)
>                    -> capability + attachment
> TransportPlan    = operation -> selected adapter
> DeviceRankMap    = collective-capable device EndpointId -> global rank   # optional
> ```
>
> ```text
> CommDomain = MemberSet + ResourceBundle + AttachmentPlan + TransportPlan
>              [+ DeviceRankMap]
> ```
>
> | 组件 | 承担什么 | 关键边界 |
> | ---- | -------- | -------- |
> | **EndpointId** | 端点身份 | 必须包含 `session_id`，才能区分同一路径的不同 incarnation |
> | **DomainId** | 域身份 | `generation/epoch` 是身份的一部分，重建不能复用旧 handle |
> | **MemberSet** | 有序端点集合 | level 只参与 `worker_path`，不决定物理能力 |
> | **ResourceBundle** | 域内按名称组织的全部异构资源 | 可同时包含 Buffer、communicator 和 adapter state |
> | **DomainRegionSet** | `ResourceBundle` 中按 owner 组织的 Buffer-backed entry 子集 | 不含 communicator/adapter state，也不能替代 CommDomain 公式里的 `ResourceBundle` |
> | **AttachmentPlan** | `(member, CanonicalBufferIdentity) → capability + attachment` | 必须使用 `owner_instance_id + buffer_id + generation`；裸 `buffer_id` 跨 owner 会碰撞 |
> | **TransportPlan** | operation → adapter | adapter 的选择必须显式且可观察 |
> | **DeviceRankMap** | device collective 的 endpoint → global rank | 可选；只约束 collective adapter |
>
> ### capability 的完整判据 = `AttachmentPlan` 的键与值
>
> 本文 §4 的互连矩阵写成 `可达 = f(部署, 部署, 互连)`。按规范，capability 挂在
> **`(member EndpointId, CanonicalBufferIdentity)`** 上，所以那个矩阵**只是必要门槛，
> 不是判据全集**。
> 通过矩阵之后仍须逐项过：
>
> | 维度 | 少了它会怎样 |
> | ---- | ------------ |
> | **Buffer/backing/backend** | 部署与互连都对，但 backing 是 VMM ⇒ host-map 仍失败（`HOST × VMM_WINDOW = ❌`） |
> | **授权** | 物理可达 ≠ 允许访问；`access` 是 Buffer handle 的属性，须独立判 |
> | **实际 attachment / materialization** | 有潜在能力 attach ≠ **已经** attach；必须存在 live attachment |
> | **runtime probe / lease** | 驱动能力位、页大小和资源存活是运行期事实，不是静态矩阵（见 [hardware-constraints.md](hardware-constraints.md) §5 的 64 KiB 页 / `support_bar_mem` 门控） |
>
> ```text
> potential = deployment × interconnect × backing/backend × runtime support
> actual    = potential ∩ authorization ∩ live Attachment/materialization
>             ∩ runtime probe/lease
> ```
>
> ### 传输方式：用规范的 adapter 名，不要自造
>
> ```text
> TransportPlan
> ├── HCCL collective adapter          设备侧集合通信
> ├── direct-map / device-peer adapter  直接映射 / peer 访问——保持原 Buffer identity
> ├── owner-delegated copy adapter      委派 owner 代读写——保持原 Buffer identity
> └── explicit transfer adapter         分配目标 Buffer 并复制——**创建新 identity**
> ```
>
> 只有 **explicit transfer** 会为 data placement 创建目标 allocation 和新的
> `CanonicalBufferIdentity`；其它 adapter 本身不把既有 Buffer 换成新 identity。
> **等价关系只覆盖同一个 canonical Buffer 上、具有相同读写效果与同步契约的
> direct-map 和 owner-delegated copy**。它们可以在允许这两条路径的显式 policy 下互换，
> 但最终选择必须可观察。把 direct-map 换成 explicit transfer 会改变依赖边、别名判定、
> 生命周期归属，属于换语义而不是性能降级。
>
> ### 术语：本设计的 "region" ≡ 规范的 **Buffer**
>
> 早前本文与[主文档](../cpu-npu-shared-memory.md)写作 `control_region` / `payload_region`，
> **已统一改名为 `control_buffer` / `payload_buffer`**——规范里它们本就是 Buffer：
> *"典型 CPU↔NPU `ResourceBundle` 可以包含可 host-map 的 control **Buffer** 和仅
> device-local/VMM 的 payload **Buffer**；每个 member 对每个 Buffer 拥有独立 capability"*。
> **同一个东西，两个名字**——正是[总纲](../logical-physical-separation.md) §3 检验标准
> 要抓的那类错误，只是发生在词汇层。
> ⇒ **以 Buffer 为准**；小写 `region` 不再是一种独立 allocation/resource 类型，
> 仅用于引用已有代码符号（今为 `WorkerChipRegion` 等，见紧接下方的迁移说明）。`DomainRegionSet` 作为
> `ResourceBundle` 内 Buffer-backed 子集的既定分组名保留；基于单个 Buffer 做 offset
> 子分配的 helper 统一叫 `BufferSliceArena`，它也不是另一种物理资源。
>
> ⚠️ **数字层级标识符的定位 —— ✅ 已迁移（2026-08-04，#1681 `b3bf3e7a`）**
> （规则见 [`codestyle.md`](https://github.com/hw-native-sys/simpler/blob/937c57efc71ad01a01c3a72072df075d2a5dd24f/.claude/rules/codestyle.md) 规则 13）：
> **我前一版写的「本文档族里出现它们一律是在引用现存代码符号、迁移只在其 API 本就要改时
> 顺带做」已经过时** —— #1681 `Rename: apply the Worker/Chip/Core layered naming contract`
> **已把整批标识符一次迁完，且不留兼容别名**：
> `L3L2Queue*` → **`WorkerChipQueue*`** · `L3L2Orch*` → `WorkerChipOrch*` ·
> `L3L2Endpoint*` → `WorkerChipEndpoint*` · `L3L2Region*` → **`WorkerChipRegion*`** ·
> `L3HostMappedRegion*` → `WorkerHostMappedRegion*` ·
> `L3L2_*` / `CTRL_L3_L2_*` 常量 → `WORKER_CHIP_*` / `CTRL_WORKER_CHIP_*`
> （**值不变 ⇒ wire layout 未动**）· `l3_l2_*` 函数/Python 模块/头文件/示例目录 →
> `worker_chip_*` · `_L2HostL3L2Region*` → `_HostWorkerChipRegion*`（收掉重复 context）。
> ⇒ **本文档族里再出现 `L3L2Region` / `L3L2Queue` 一律是历史名**，指代现存符号时
> 请用新名。**设备日志文本与架构叙述保留 `L3-L2` 拼写** —— 规则 13 允许拓扑描述用数字层级。
> 这条同时把 **域模型与内存模型接上了**：
> `DomainRegionSet` 里的每一块，
> 就是 [p1b-corrected-design.md](../worker-memory-model/p1b-corrected-design.md) 里
> 那个带 `CanonicalBufferIdentity`（wire 类型名为 `CanonicalIdentity`）的 Buffer，不是另一套东西。

 | 端点示例 | 路径 | 部署 |
 | -------- | ---- | ---- |
 | 某 L3 宿主进程作为 CPU 参与者 | `L4/L3[0]` | `HOST_CPU` |
 | 某张卡的设备内存（AICore 可达） | `L4/L3[0]/L2[5]` | `DEVICE_AICORE` |
 | 片内 AICPU（与 AICore 是**不同 CommEngine**） | `L4/L3[0]/L2[5]` | `DEVICE_AICPU` |

歧义因此消解——不再说"L4 建 L3 的域"，而是精确表述：

```text
MemberSet = {
  EndpointId(session=s, worker_path=L4/L3[0], deployment=HOST_CPU),
  EndpointId(session=s, worker_path=L4/L3[0]/L2[0..7], deployment=DEVICE_AICORE)
}
```

> **路径**来自层级（用于命名/寻址，§2.4）；**部署**是物理事实。
> 二者正交——**同一路径可以有多个部署视图**（如某卡的 AICore 与 AICPU）。

## 4. 必要门槛：**可达性 = 部署 × 互连**（导出量，非原语）

```text
可达性(A, B) = f( 部署(A), 部署(B), 互连(A, B) )
```

### 4.1 互连能力矩阵（**注意：整张表没有一个 level 字**）

> **本表在 2026-08-03 重写。** 旧版把「可达」写成**布尔量**，并给跨机一行
> `❌ 无`——那既不准确，也制造了一个**特例化的恒假分支**。
> 正确的形态是：可达性不是"是/否"，而是**哪些 `TransportPlan` adapter 可用**。
> 跨机不是"不可达"，是**可用 adapter 集合更小**——用同一把尺子量，不是另设一条规则。

按 adapter 列（`D`=direct-map / device-peer，`C`=owner-delegated copy，
`T`=explicit transfer，`H`=HCCL collective）：

 | 部署 A ↔ 部署 B | `D` | `C` | `T` | `H` | 机制 / 备注 |
 | --------------- | :-: | :-: | :-: | :-: | ----------- |
 | `HOST_CPU` ↔ **同机** `HOST_CPU` | ✅ | ✅ | ✅ | ❌ | fork-shm / POSIX shm。CPU 非 HCCL rank |
 | `HOST_CPU` ↔ **同机** `DEVICE_*` | 🟡 | ✅ | ✅ | ❌ | `D` 需 `halHostRegister`，且受 §5 三条约束（非 VMM backing / 页大小与能力位 / logical→driver id gate）门控 |
 | `DEVICE_*` ↔ **同机** `DEVICE_*` | ✅ | ✅ | ✅ | ✅ | VMM / Fabric-V2 peer import |
 | `HOST_CPU` ↔ **跨机**任意 | ❌ | ✅ | ✅ | ❌ | `D` **物理上不可能**——host CPU 无法映射另一台机器的内存。但**域仍可包含跨机成员**，走 `C`/`T` |
 | `DEVICE_*` ↔ **跨机** `DEVICE_*` | 🟡 | ✅ | ✅ | 🟡 | 由 #1456→#1623→#1624 栈提供（export/import 描述符 + MPI allgather + commit）。**`D` 与 `H` 的跨机形态待核实**，见下 |

**这张表只依赖部署与物理拓扑，与 L2/L3/L4 无关**——这是它的价值所在。

> ### ⭐ 与代码的对表（2026-08-14，核验于 `upstream/main@3f4686c1`）
>
> **本表是规范，不是现状描述** —— 它引的是 [`feature_roadmap.md`](../feature_roadmap.md) 的
> 契约形式，**从未声称已实现**。记下差距是为了让「还差多少」可量化，不是为了给本表打折。
>
> **规模**：本表 **5 行 × 4 个 adapter 列 ＝ 20 格**。
> **代码今天真正做判定的是 1 格** —— `DefaultRegionAccessService._evaluate_host_vmm_copy`
> （`comm_endpoints.py:636`），覆盖 `HOST_CPU ↔ 同机 DEVICE_*` 的 `C` 列、
> 且只在 `BackendKind.VMM_WINDOW` 下。
>
> | 代码状态 | 对应本表 |
> | -------- | -------- |
> | ✅ 真判定 | `HOST_CPU ↔ 同机 DEVICE_*` 的 `C`（`HOST_VMM_COPY`） |
> | ⚠️ 能产出候选但**生来就带 `unsupported_reason`** | `DEVICE_FABRIC_V2_PEER_IMPORT`（`:875`）· `REMOTE_COPY`（`:907`）· 两处 `EXPLICIT_TRANSFER`（`:895`/`:912`）⇒ **`T` 列整列、跨机 `D`/`H` 全部未实现** |
> | ⛔ **连候选都产不出** | `FORK_SHM` / `FORK_COW` / `DEVICE_MALLOC` / `REMOTE_SIDECAR` 四个 `BackendKind` 在 `_adapter_candidates:899` 落到 `return ()` ⇒ **`HOST_CPU ↔ 同机 HOST_CPU` 那一行（fork-shm / POSIX shm）的判定，service 从未被问过** |
> | ⛔ 从未被构造 | `AdapterProfile.HOST_SVM_MAP`（唯一出现处是 `:615` 一条永远到不了的拒绝分支）· `AdapterKind.COLLECTIVE` ⇒ **`H` 列在代码里没有载体** |
>
> ⭐ **本表比代码更完整，而且它标了证据边界** —— 跨机 `D`/`H` 标 🟡 并注明
> *「不是保守，是没查过」*。**代码里没有任何地方记录「这一格没查过」**：
> `_adapter_candidates` 的 `return ()` 与「查过且不支持」在下游**完全同形**
> （都走到 `UNSUPPORTED_DEPLOYMENT_COMBINATION`）。
> ⇒ **把本表落进代码时，「未探测」必须是一个与「不支持」不同的返回值** ——
> `RegionAccessReasonCode` 已有 `NO_IMPLEMENTED_DIRECT_MAP_PROBE` 这个先例，是对的方向。
>
> ⚠️ **这是「资源模型 ↔ capability 判定」真正的接缝，也是唯一值得「整合」的地方** ——
> 但它是**把契约往代码里落**，不是把文档并到一起。文档合并的讨论见
> [`../track-boundaries.md`](../track-boundaries.md) §2。

#### ⚠️ 跨机行的证据边界（严格区分"已验证"与"未核实"）

**已验证**：#1623 跑通 **two-host A3 2x2**，四 rank compute/communication
`max_diff == 0`，序列为 `PREPARE_EXPORT → MPI allgather → IMPORT → COMMIT → copy → release`。
⇒ **跨机域可以建立、数据确实过去了**，`C`/`T` 成立。

**未核实**：该序列里的 `copy` 走的**物理通道**是什么，我没有从代码核实过。
因此上表跨机行的 `D`（跨机 direct-map / device-peer）与 `H`（跨机 collective）
标 🟡 而非 ✅ —— **不是保守，是没查过**。

> 这条纪律来自[总纲](../logical-physical-separation.md) §6 的事故：
> **跨文档的物理能力断言必须对表**。上次是许诺硬件给不了的；这一行的风险是反向——
> 因为"栈跑通了"就默认全部 adapter 都通。**填这两格之前必须读 `comm_hccl.cpp`
> 与 remote 传输层的实际数据路径。**

⚠️ **但它不是成员合法性的_唯一_判据**（早前的措辞过强，已更正）：按规范 capability 挂在
**`(member EndpointId, CanonicalBufferIdentity)`** 上，本表只覆盖其中的
「部署 × 互连」一维，是**必要门槛**。通过本表后仍须过 backing/backend、授权、
live attachment/materialization、runtime probe/lease——见 §3。

### 4.2 "能不能" vs "实际" —— 两个不同层次

 | 层次 | 判据 | 规范对应物 |
 | ---- | ---- | ---------- |
 | **潜在能力**（potential） | 部署 × 互连（§4.1，**必要门槛**）× backing/backend × runtime support | capability discovery |
 | **实际能力**（actual） | potential ∩ authorization ∩ live attachment/materialization ∩ runtime probe/lease | `AttachmentPlan` 的 capability + attachment |

> 规范把 capability 与 attachment 并列写进
> `(member, CanonicalBufferIdentity) -> capability + attachment`，正是因为潜在能力与
> 已建立、可使用的访问关系必须分开判。

两个同机 host 进程，部署与互连都满足，但**没建立 shm 映射**仍然访问不到。
"怎么建立映射"由 §5 的**委派机制**负责。

> **分工**：**部署 × 互连先过物理必要门槛；capability discovery 完成 potential 判定；
> 委派机制建立并登记 actual Attachment。**

## 5. 跨层级机制：**声明自顶向下，执行委派给 owner**

L4 **不直接操作** L2；它声明域，由各层 owner 逐级展开执行：

```text
L4  声明域（成员选择器）
 └→ 委派给每个 L3
      └→ L3 对自己的 chip 执行 alloc / export / map，逐级上报
```

✅ **这是现有模式的推广，不是新机制**：今天 `allocate_domain` 内部已经是
"L3 把 `CTRL_ALLOC_DOMAIN` 分发给每个 chip child、各自 alloc+map 后回报"
（`python/simpler/worker.py:1103` / `:1123-1132`）——本设计只是**再往上推一层**。

封装因此不破：**L4 只跟 L3 说话（守住 §2.4 的可见性契约），L2 的实际映射由它的
owner 完成。**

⬜ **本机制的连带责任（必须一并定义，否则不可用）**：

- **部分失败回滚**：某个 L3 展开失败时，已成功的 L3 必须回退（否则留下半个域）
- **成员冻结 membership freeze**：建域期间成员集不得变动
- **域的 epoch / 代际**：重建后的域必须与旧 handle 可区分（ABA）
- **全员 READY barrier**：所有成员就绪才发布可用

> ✅ 这四项在 [../l3l4/cluster-comm.md](../l3l4/cluster-comm.md)
> §D.5 已被列为 L4 建域的必需件（当时针对 HCCL global communicator，
> **同样适用于共享内存域**）。

## 6. 成员的两种表达（建议默认前者）

 | 形式 | 说明 | 何时用 |
 | ---- | ---- | ------ |
 | **子树选择器**（idiomatic 默认） | "这些 L3 子树下的所有 chip"，各 L3 **本地展开** | 常规——保持封装，L4 不必知道 chip 拓扑 |
 | **显式端点列表** | 精确指定每个 `EndpointId` | 需要非规则子集时；要求调用者知道拓扑 |

## 7. 后端由成员、资源与 capability plan 共同选择，不支持则**显式失败**

下表是代表性组合，不表示只凭 MemberSet 就能确定后端；同一成员对不同 Buffer backing
可能得到不同 capability，最终 adapter 由 `AttachmentPlan/TransportPlan` 明确选择。

 | 成员与资源组合 | 候选后端 | 状态 |
 | -------------- | -------- | ---- |
 | 同一 L3 下的多个 `DEVICE_*` | VMM / Fabric-V2 设备共享 | ✅ 今天就有 |
 | **`HOST_CPU` + 同机 `DEVICE_*`** | host-map control Buffer + device-local/VMM payload Buffer；逐 Buffer 选 direct/copy adapter | ⬜ **本设计要补的那块砖** |
 | 同机 `HOST_CPU` + `HOST_CPU` | host 共享内存（fork-shm / POSIX） | ✅ 机制已有 |
 | 跨机 device 窗口；其他异构组合逐项验收 | 主路径 mpirun/MPI + A3 ACL/Fabric V2 共享窗口；SHMEM 库适配另验 | A3 基本栈 #1456/#1623/#1624 已合入（`68d137fdf` 复核）；任意 host/device ResourceBundle 组合见 DS-10 |

> ### ⚠️ 订正（2026-08-05，用户指出）：本行此前写作 ❌「今天不可达 → 显式失败」，**错在把
>
> 「我们还没做」说成了「硬件做不到」**
>
> **A3 HCCS/Fabric V2 的跨机实现已在 main**（`68d137fdf`）；#1456 → #1623 → #1624
> 均已合入，#1623 正文自报**双机 A3 2x2** 四 rank `max_diff == 0`；独立读取的原始 CI 证据
> 包括 #1624 head `87489e1a` 的历史 PASS，以及 10 月 12–13 日三个面向 main 的具名 PR 测试 SHA 的五项 NETWORK1 PASS，确认持续双机 CI 看护。证据及 NaN 验金缺口见 [D.4](../l3l4/cluster-comm.md#调用链与执行证据2026-10-13)；任意异构组合由 DS-10 验收。
>
> #### 这条错误为什么值得单独记**：它和 §4.1 旧版那个「跨机一行 ❌ 无」是**同一个错误的两处副本
>
> —— §4.1 已于 2026-08-03 重写成「按可用 adapter 集合量」，**本表却漏改**，于是被作废的前提
> 又在同一份文档里活了两天。§9 硬限 1 也是同一批改的。
> ⇒ **推翻一个前提时必须搜全仓该前提的所有副本**，否则「已更正」只是局部的
> （与 [`README.md`](../README.md) §4 规则 1「两份记同一事实必漂移」同源）。
>
> **仍然成立的那半**：`D`（跨机 direct-map）对 `HOST_CPU` 是**物理不可能**、对 `DEVICE_*`
>
> #### 未核实**（§4.1 标 🟡）；A3 Fabric V2 本身只验证过单机。**请求 `D` 的跨机域应显式失败
>
> —— 但那是「这个 adapter 不可用」，不是「跨机不可达」。

沿用已冻结的能力门控原则
（[../worker-memory-model/p1b-corrected-design.md](../worker-memory-model/p1b-corrected-design.md) §5.1）：

#### 不支持的组合在 dispatch 前显式拒绝，不静默回退 host staging

## 8. ⚠️ 两条机制的答案**不同**（勿混用）

 | 机制 | 跨层级如何表达 |
 | ---- | -------------- |
 | **HCCL collective 域** | **必须 flatten** 全部选中 L2 成全局 device rank、建**单一** communicator；子集用 `HcclCreateSubCommConfig` **仅从 global 拆分**（不支持嵌套拆分）。见 l3l4 §D.5 / `:741` |
 | **共享 Buffer + Attachment（本设计）** | **不受此限**——可为任意端点集合，**允许嵌套与重叠**。✅ 依据现有 `comm_alloc_domain_windows` / `comm_derive_context`：这条 simpler 自研 VMM 共享句柄路径**不受 HCCL“子域不可嵌套”限制——这对 L4→L3→L2 的嵌套结构反而是优势** |

⇒ **本设计走第二条**，因此可以做"L4 声明、跨 L3 的 chip 级域"，**无需先解决 HCCL 的
flatten 问题**。两者是**独立的轴，不要互相阻塞**。

## 9. 该模型下的硬限

1. **跨机成员：合法，但 adapter 集合更小**（本条已于 2026-08-03 重写）。
   ~~跨机不可达 → 跨机成员集显式失败~~ **不再成立**——#1456 → #1623 → #1624 栈已在
   双机真硬件上建立跨机域。现在的规则与其它 capability **完全同构**：

   > 跨机成员的 capability 由 `AttachmentPlan` 按 §4.1 的 adapter 表判定；
   > **请求的 adapter 不可用时才失败**，而不是"因为跨机所以失败"。

   两条随之而来的**实操规则**：

   - **跨机域不得默认 `direct-map`**。`HOST_CPU` 跨机 `D` 是**物理不可能**；
     `DEVICE_*` 跨机 `D` **未核实**。请求 `D` 的跨机域应**显式失败并说明**，
     而不是静默降级到 `C`（那会违反[总纲](../logical-physical-separation.md) §5
     的"跨 identity 边界不得静默选路"）。
   - **`explicit transfer` 在跨机上是常规手段，不是降级**。它创建新
     `CanonicalBufferIdentity`，调用者必须知情——这一点跨机与同机**没有区别**。

   ⚠️ **A3 Fabric V2 只验证单机**仍然成立：不要把它当成跨机数据面的解释。
2. **64 rank ABI 硬限**：`CommContext.windowsIn[]/windowsOut[]` 固定
   `COMM_MAX_RANK_NUM=64`——每台 16 卡时**最多 4 台满配 L3**。走 windows 路径的域受此限。
   ⚠️ **跨机成为现实之后，这条从理论上限变成了实际会撞到的墙**：单机 16 卡时它绰绰有余，
   4 台以上的集群走 windows 路径**必然撞限**。⇒ 跨机域的规模上限应在**建域时前置校验**
   并给出可读错误，而不是等 ABI 层越界。
3. **部署能力不对等**：`HOST_CPU`（过 PCIe、需显式 fence）/ `DEVICE_AICORE` /
   `DEVICE_AICPU` 是三种能力，域必须携带**部署视图**，不能假装对等。

## 10. 命名原则（防止再次焊死层级）

- **API 名不得包含层级对**（`l3_l2` / `l4_l3` 之类）。层级信息只出现在**参数**
  （端点路径 / 选择器）里，**不出现在函数名里**。
- 端点一律以 **`EndpointId`（`session_id + worker_path + deployment`）** 表达，**不以"第 N 个 worker"的裸索引**表达——
  裸索引正是把成员语义绑死在某一层的根源。
- **不得用层级词汇描述成员语义**（"这是个 L3 的域"）。正确说法是列出
  `EndpointId` 集合，或说"某某子树的 `DEVICE_AICORE` 视图"。
- **不得以 level 为条件判定共享能力**（§2.3）——§4.1 互连矩阵只是必要门槛，
  完整判据是 §3 的 potential/actual capability 公式。
