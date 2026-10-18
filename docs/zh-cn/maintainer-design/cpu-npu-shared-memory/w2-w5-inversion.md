# W5 先于 W2 落地：成员词汇的时间窗口与 track 重排

> **发布快照：2026-10-18。** 设计、实现状态与历史调查须按正文各自基线阅读；不代表当前 main 全量验收。参见[阅读入口与已知更正](../README.md)。
>
> **状态**：📐 **排序分析 + 建议**。不是新设计——它只做三件事：
> ① 把 PR #1456 对表 [domain-membership.md](domain-membership.md) 与
> [work-breakdown.md](work-breakdown.md)；② 更正一处我此前的误判；
> ③ 据此给出重排后的执行顺序。
> **来源**：2026-08-07 对 PR #1456 `adc312de` 的复审 + 与用户的设计讨论。
> **姊妹篇**：[../w2-p1b-endpoint-convergence.md](../w2-p1b-endpoint-convergence.md)
> —— **同一天写的，谈的是同一个 W2，但两边互不引用**。本文从 **W5→W2 依赖倒置**的角度谈 W2 的
> **成员词汇**；那份从 **P1-B 供货**的角度谈 W2 的**端点注册表**（PR #1696）。；
> ⚠️ **合看之后有一处必须更正本文 §6**：本文把 **S1（W2-min）** 与 #1696 当成互不相干的两件事，
> 实际上 **S1 要_消费_ #1696 的产物** —— `EndpointDeployment{HOST_CPU, DEVICE_AICORE}` 已经
> 在 #1696 的 `python/simpler/comm_endpoints.py` 里（✅ 核实于 head `24370ad7`）。
> 若 S1 自己再造一个 `deployment` 枚举，就是[总纲](../logical-physical-separation.md) §5
> 「先查规范，再造词」那条教训的第三个副本（前两个：`comm_profile` 三拆、`BackendKind` 双词汇）。
> ⇒ **S1 应排在 #1696 合入之后，从 `comm_endpoints` import 这个枚举**。
>
> ## 合并后的执行顺序以 [`../README.md`](../README.md) §1.5 为准
>
> **基线**：PR head `adc312de`（merge-base `0d202898`，落后 `upstream/main` 4 个 commit）。
> ⚠️ [work-breakdown.md](work-breakdown.md) 的基线仍写 `e711d482`（08-04），**已漂移**
> —— ✅ 2026-08-07 已在该文抬头标注，且本文 §5.1/§5.2/§5.3 三处已就地折回。
> **标注**：✅ 已核实｜🟡 待验证｜⬜ 全新。
> **锚定约定**：沿用 work-breakdown —— 一律用 `file::symbol`、异常消息原文或合并 commit，不用行号。

## 1. 核心发现：依赖被倒过来执行了

[work-breakdown.md](work-breakdown.md) §2 的依赖是 **W5 ← W2**，理由写在 W5 的「依赖」行：
*"需端点模型才知道往谁委派"*。实际发生的是：

 | 项 | 计划 | 现实（2026-08-07） |
 | -- | ---- | ------------------ |
 | **W5** 委派机制 + 事务语义（L） | SM4，依赖 W2 | ✅ **#1456 已交付 ① 与 ② 的主体**，并在双机真硬件 pod CI 跑通 |
 | **W2** 端点与成员模型（M–L） | SM1，无依赖 | ⬜ **未开工** |

W5 的**机制**先于它的**词汇**落地。于是成员表达没有 `EndpointId` 可用，
`Orchestrator::allocate_global_domain` 只能用裸索引二元组顶替：

```python
members: Sequence[tuple[int, int]]   # (l3_worker_id, local_l2_worker_id)
```

这不是作者的疏忽——**W2 的产物根本不存在**。但它正是
[domain-membership.md](domain-membership.md) §10 第二条明令禁止的形式：

> *"端点一律以 `EndpointId`（`session_id + worker_path + deployment`）表达，
> **不以「第 N 个 worker」的裸索引**表达——裸索引正是把成员语义绑死在某一层的根源。"*

缺的正是 `deployment` 那一维。该签名在**类型层面**就让「L4 建的是 L3 的域还是 L2 的域」
只有一个答案，而 §1 说这个提问本身是错的——**内存不坐落在层级上，内存坐落在设备上**。

### 1.1 ⏰ 时间窗口正在关闭

成员**已经在跨节点控制面 wire 上**（✅ 核实）：

- `global_comm_domain::encode_domain_command` 依次打包
  `GLOBAL_DOMAIN_VERSION` → phase/domain_id/generation/window_size → name/profile →
  **`len(members)` + 逐个 `_put_member`** → buffers → descriptors；
- 入口先过 `global_comm_domain::validate_member_table`，该函数直接以
  `(member.node_worker_id, member.local_worker_id)` 判重。

⇒ **[work-breakdown.md](work-breakdown.md) 对 W2 的规模评估已过时**。原文写
*"**不碰 C++/wire**——纯 Python 层的标识 + 校验"*：编码器确实是 Python，但它是
**跨进程、带版本号的序列化格式**，不再是进程内标识。

而 #1623 / #1624 正在同一协议上继续叠加（栈序 #1456 → #1623 → #1624）。

#### 现在改是加字段，等这两个落地后是换协议

## 2. 一处更正：#1456 的 ABORT 缺口没修（我此前判反了）

我在 2026-08-07 的 PR 复审里，把 ABORT 扇出「silent `contextlib.suppress` → 逐节点写
stderr」记成了**已修复**。**这是错的。**
[`../README.md`](../README.md) §1.1 对 #1456 的审计（2026-08-03，`a05b944f`）
点的正是这一条，而它在 `adc312de` 上**依然成立**：

 | — | 契约要求 | #1456 现状 |
 | - | -------- | ---------- |
 | 异步流水 §5.9 第 ⑥ 条 | 完整清理不可证时 **poison ordered admission** | `Worker::_allocate_global_domain` 的 ABORT 扇出 `except` 分支写 **stderr** 后重抛原错 |
 | 同上 | —— | `Worker::_release_all_global_domain_nodes`（L3 节点 close 路径）同样只写 stderr，且**不抛** |

**stderr 不是「比静默好一点」，它就是 §5.9 点名禁止的反模式。** 后果是可推导的：
分配失败路径上 domain 从未注册进 `_live_global_domains` 或 `resources.live_global_domains`，
`requires_ordered_cleanup` 也未置位 ⇒ **run fence 与 `close()` 都够不着** ⇒
后继 run 按满深度放行 = §5.9 所说的 **false clean handoff**。

✅ **硬证据**（复核于 `adc312de`）：`Worker::_record_unreclaimable` 在 main 上有 9 个调用点，
**global-domain 的整段代码里一个都没有**。

⇒ **修法不需要造机制**：`Worker::_record_unreclaimable(message, cause)` 的 docstring
写的正是此场景——*"device state that no cleanup can reach: nothing tracks it, so there is
no handle for a run's fence to fail on"*。

> **教训**：我把「可观测性提升」当成了「契约达标」。契约要的不是**看得见**失败，
> 是让失败**挡住后继准入**。这属于[总纲](../logical-physical-separation.md) §6
> 那类错误的变体——只在单一维度（有没有输出）查一致性，没对表另一维度（准入语义）。

## 3. #1456 对表结果（完整）

### 3.1 符合契约的部分（机制层面是对的）

 | 契约条款 | #1456 | — |
 | -------- | ----- | - |
 | [domain-membership.md](domain-membership.md) §5 声明自顶向下、执行委派 owner | `PREPARE_EXPORT → IMPORT → COMMIT`，L4 只跟 L3 说话 | ✅ 正是 §5 说的「现有模式再往上推一层」 |
 | §5 四项连带责任 | 部分失败回滚（ABORT 逐参与者扇出）· 域 epoch（`generation`，release 校验失配即拒）· 全员 READY barrier（全部 IMPORT 成功才 COMMIT） | ✅ 三项齐备；成员冻结 🟡 未单独核实 |
 | §8 走「共享 Buffer + Attachment」而非 HCCL flatten | `comm_global_domain_prepare` / `_import` 无 communicator，无 rootinfo 握手 | ✅ 正是 §8 说的「对 L4→L3→L2 嵌套反而是优势」 |
 | §9 硬限 2：64 rank 建域时前置校验 | `Worker::_allocate_global_domain` 与 `validate_member_table` 均按 `GLOBAL_DOMAIN_MAX_RANKS` 拒绝 | ✅ 已做 |

**这也就回答了 [work-breakdown.md](work-breakdown.md) §3 的问题 2**（见 §5.2）。

### 3.2 违反契约的部分（词汇层面，按严重度）

 | # | 焊死了什么 | 判据 |
 | - | ---------- | ---- |
 | **V1** | **成员用裸索引二元组** —— 缺 `deployment` 维 | §10 第二条；见本文 §1 |
 | **V2** | **单 backing** —— 一个域 = 一块 window，`CommBufferSpec` 只是它内部按 offset 顺序切的片（`GlobalCommDomainHandle::buffer_range` 累加偏移） | §3 boxed note：`halHostRegister` 不支持 VMM VA ⇒ 同一 backing 不可能既 VMM peer-import 又 host-register ⇒ **域模型必须允许组合多个 backing**。§7 的 `HOST_CPU + 同机 DEVICE_*` 原文就是「host-map control Buffer + device-local/VMM payload Buffer，**逐 Buffer** 选 adapter」 |
 | **V3** | **`comm_profile` 把 transport × medium × backing 压成一个节点级枚举**，且强制全域一致（`Worker::_allocate_global_domain` 有 *"all participating nodes must use the same comm_profile"*） | [总纲](../logical-physical-separation.md) §3 检验标准。契约里这三样分属 `TransportPlan` / Buffer backing / `AttachmentPlan` 的 capability 值，且全挂在 `(member, CanonicalBufferIdentity)` 这条**边**上 |

**V2 是结构缺失，不是能力缺失。** 补 CPU↔NPU 混合域时，`CommBufferSpec` 的语义必须从
「窗口内切片」改成「独立 Buffer，各带自己的 backing 与 capability」——那是破坏性变更，
而它已经在 wire 上了（同 §1.1 的时间窗口）。

**V3 会直接挡死 §7 那行混合域**：host-map control + VMM payload 天然是两种 adapter，
不可能「全域一致」。另注：`global_comm_domain::resolve_global_comm_capability` 把
platform 与 profile 一对一绑死，⇒ `comm_profile` 当前**不携带任何独立信息**，
其唯一实际作用是跨节点一致性断言。

> ⚠️ **我自己踩过的坑，记在这里防止复现**：我曾建议把 `comm_profile` 拆成
> `transport / medium / host_mappable` 三个能力位——那是**又自造了一套名字**，
> 规范里早有 `TransportPlan` 的四个 adapter（HCCL collective / direct-map·device-peer /
> owner-delegated copy / explicit transfer）。这是[总纲](../logical-physical-separation.md) §5
> 「两条教训」② 的又一个副本。**先查规范，再造词。**

## 4. 可以填掉 §4.1 的一格「未核实」

[domain-membership.md](domain-membership.md) §4.1 跨机行的 **`D`（direct-map / device-peer）**
标 🟡，注写着*"填这两格之前必须读 `comm_hccl.cpp` 与 remote 传输层的实际数据路径"*。

### 本轮读了

- `comm_hccl.cpp::comm_global_domain_import` → `import_fabric_window(device_id, fabric_handle, …)`
  即 `aclrtMemFabricHandle` 的 VMM peer import，产出 `ctx.windowsIn[rank]` 是
  **可被 kernel 直接读写的 device VA**；
- `examples/workers/l4/global_tload_mixed_l3` 的 kernel 就是 peer `TLOAD`，
  在 `_st-pod.yml` 双机 pod job 上 `max_diff=0.000e+00`（run 31138473600 / job 92743892751）。

⇒ **`DEVICE_*` ↔ 跨机 `DEVICE_*` 的 `D` 可从 🟡 转 ✅。**
同时 §9 硬限里「A3 Fabric V2 只验证单机」也已过时。

> ⚠️ **边界必须同时写进去，否则就是 §4.1 警告的反向错误**（"因为栈跑通了就默认全通"）：
> 这里的「跨机」是**同一 A3 fabric 内的两个节点**——控制面走 TCP 搬描述符，
> 数据面走 fabric 直访。**跨 fabric / 以太网仍未核实**；
> 且两台 pod 机是否同属一个 super-pod，🟡 **我没有核实过拓扑**，填表前需确认。

## 5. 计划本身需要更新的三处

### 5.1 W2 的规模评估过时 → 见 §1.1

「不碰 C++/wire」不再成立。W2 现在要动跨节点控制面 wire，且有时间窗口。

### 5.2 §3 问题 2 可以判定了 —— **关闭**

work-breakdown §3 列的必答问题 2：
*"委派机制是否认可（W5）——若被否决（改为 L4 直接操作 L2），**W2 + W5 都要重写**"*。

**#1456 事实上已经替这个问题投了票**：它就是委派机制，五步握手，双机真机验证通过。
⇒ **问题 2 关闭，W2/W5 不再背「可能整体重写」的风险** —— 这解除了 W2 开工的最后一个不确定性。

### 5.3 §4 交叠提醒 1 已消解 → **W3 风险与规模下调**

原文：*"W3 ↔ P0.2 follow-up 簇C（retryable journal）：都是「资源只在 native free 成功后才丢账本」。
**应对齐**"*。

**P0.2 follow-up 现已 ✅ DONE**（八条退出条件全绿，末批 #1689 `f8106e24`）。
⇒ W3 不再需要「与一个在飞的东西对齐」，**直接复用已落地的 journal 即可**。

## 6. 建议顺序

依赖强制的只有三条链：`W0→W1→W4→W6→W8`、`W2→W5`、`W3→W7`。
其余是排期判断。下表明确区分**依赖所迫**与**我的判断**。

### 阶段 1 —— 三件小事并行，全是解锁项

 | — | 内容 | 规模 | 类型 | 为什么现在 |
 | - | ---- | ---- | ---- | ---------- |
 | **S0** | #1456 的 ABORT 与 L3-close 路径改调 `Worker::_record_unreclaimable()` | **XS** | 判断 | 合入门，非新工作；修法已在 main（§2） |
 | **S1** | **W2-min**：`members` 换成带 `deployment` 的结构 | **S** | ⏰ 判断 | wire 正在冻结（§1.1）。**现在是加字段，之后是换协议** |
 | **S2** | **W0**：a5 host-map 能力探测 | **XS** | 依赖 | 阻塞整条链 A（W1→W4→W6→W8），而它只是个探测小程序 |

三者互不依赖。**S1 是本阶段重心**：不做完整 W2，只把 `deployment` 这一维在类型与 wire 上
**占住位置**，哪怕当前只接受 `DEVICE_AICORE`。同时把限制**归因到物理**——
文档写「当前 `MemberSet` 只支持 `DEVICE_AICORE`，因为 host-map backing 尚未实现」，
**不写**「窗口在 L2」（后者会把错抽象固化成契约，正是 §2.3 警告的陷阱）。

### 阶段 2 —— 三条链各推一步

 | 项 | 规模 | 依赖 |
 | -- | ---- | ---- |
 | **W2 完整**：`EndpointId` 全套 · 成员选择器 · potential/actual capability 计算 | M–L | 无（S1 已铺路） |
 | **W1**：a5 override 两个虚函数 + `ONBOARD_HOST_MAPPED` profile + device-id gate | S–M | **W0** |
 | **W3**：per-worker 生命周期扩到带 device 流量的资源 | M–L ↓ | 无 |

**W3 建议从计划的 SM3 前移至此。** work-breakdown 自己写了
*"放 SM3 是排期选择，不是依赖所迫；若 #824 提前，可整体前移到 SM1"*，
而现在有两个新理由：§5.3 的风险下调；且 W3 是异步流水 track 与本 track 的**共同前置**。

### 阶段 3

 | 项 | 依赖 | 备注 |
 | -- | ---- | ---- |
 | **W4** 同步原语归一 + cache maintenance 抽象 | **W1** | ⚠️ 文档标注的**最高风险项**（PR #540 真实事故），a2a3 onboard 验证是必要条件 |
 | **W5 完整**事务语义 | **W2** | 把 #1456 的机制补齐到契约：含 S0 的 poison、成员冻结 |

### 阶段 4 / 5

`W6`（W1+W4）→ `W7`（W3）→ `W8`（W2+W6，API 收敛，迁移面 66 个调用点 / 4 个文件）。

**V2 的多 backing 改造**（`CommBufferSpec` 从窗口切片升为独立 Buffer、
`window_size` 从域级参数降为 per-Buffer 属性、`comm_profile` 拆进
`AttachmentPlan`/`TransportPlan`）落在 **W2 完整 + W1** 之后——
它同时需要端点模型和 host-map profile 才有意义。

## 7. 本文与既有文档的关系（防漂移）

本文是**增量记录**，不是第二份排期真源。建议在下一次编辑既有文档时把内容折回去，
然后本文只保留 §2 的更正与 §4 的证据：

 | 本文小节 | 应折回 |
 | -------- | ------ |
 | §1 / §5.1 | [work-breakdown.md](work-breakdown.md) W2 的「规模」「⚠️ 关键」行 |
 | §5.2 | [work-breakdown.md](work-breakdown.md) §3 问题 2 → 标记关闭 |
 | §5.3 | [work-breakdown.md](work-breakdown.md) §4 交叠 1 → 标记消解 |
 | §6 | [work-breakdown.md](work-breakdown.md) §2 的 SM0–SM4 排期表 |
 | §2 | [`../README.md`](../README.md) §1.1 的 #1456 行（该行已记此缺口，本文只补「`adc312de` 上仍成立」与我的误判更正） |
 | §4 | [domain-membership.md](domain-membership.md) §4.1 跨机行 `D` 格 + §9 硬限 1 的 Fabric 单机注 |

> 依据 [`../README.md`](../README.md) §4 规则 1「两份记同一事实必漂移」，
> 以及 [domain-membership.md](domain-membership.md) §7 那条订正的教训——
> **推翻一个前提时必须搜全仓该前提的所有副本**。
