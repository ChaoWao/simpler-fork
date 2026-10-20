# W2 端点模型 ↔ P1-B `ImportContext`：收敛计划

> **发布快照：2026-10-19。** 设计、实现状态与历史调查须按正文各自基线阅读；不代表当前 main 全量验收。参见[阅读入口与已知更正](README.md)。
>
> **状态**：⬜ **计划**。本文只做**依赖识别与合入排序**，不含实现者 prompt。
> **触发**：2026-08-07 review PR #1696（`24370ad7`）时发现一条**未被任何文档记录的跨里程碑依赖**。
> **上游**：[cpu-npu-shared-memory/work-breakdown.md](cpu-npu-shared-memory/work-breakdown.md) §W2
> ／ [worker-memory-model/p1b-corrected-design.md](worker-memory-model/p1b-corrected-design.md) §5.2 / §9
> ／ [feature_roadmap.md](feature_roadmap.md) §2.4（G2 / G4 / G5）
> **姊妹篇**：[cpu-npu-shared-memory/w2-w5-inversion.md](cpu-npu-shared-memory/w2-w5-inversion.md)
> —— **同一天写的，谈的是同一个 W2，但两边互不引用**。那份从 **W5→W2 依赖倒置**（#1456 已把成员
> 送上跨节点 wire）的角度谈 W2 的**成员词汇**；本文从 **P1-B 供货**的角度谈 W2 的**端点注册表**。
> 两份的「下一步」不冲突但**必须合看**（合并后的顺序见 [`../README.md`](README.md) §1.5）。
> **锚定约定**：符号 / 合并 commit / 异常消息原文，不用行号（`comm_endpoints.py` 在飞）。
>
> ---
>
> ⛔ **2026-08-07 自我更正：本文 §1 / §2 / §5 的链尾建立在一个已被推翻的前提上。**
> 写作时（01:49）我记的是「**#1453** wire flip，**硬前置 ＝ 不变量 5**」。两小时后核对
> 看板才发现两处都变了：；
> ① **wire flip 已由 #1729 承接**（`c9f5db5e`，125 files，2026-08-07T03:54 仍在动）；
> \#1453 是原 omnibus DRAFT，07-30 后未动。；
> ② **用户裁决 A（2026-08-07）已解除该硬前置** —— 提交时那道防线本就落了
> （host endpoint 行由 `_reject_device_args` ＋ `mapped_args_from_blob` 双重拒绝，chip 行由
> `_child_prov_check_dispatch` 按精确 `(target_worker_id, ptr)` 键 fail-closed），
> 剩余缺口收窄为「chip 行缺 `materialize` 兜底」，**降为 P1-B 的普通退出条件**。；
> ⇒ **§5 的「关键路径 ＝ 决定① → #1696 → G4 → ImportContext → #1453」不再成立。**
> `ImportContext` 与 wire flip **解耦**：前者是质量链（补第二道防线），后者可独立推进。
> 本文其余部分（依赖识别、#1696 的三条 blocker、决定 ①②、为什么信息只能来自 registry）
> **全部仍然成立** —— 变的是链尾的**紧迫性**，不是链本身。；
> **教训**：写排序文档时我引用的是**记忆里的看板**，不是**当天的看板**。
> 排序结论的保质期比事实结论短得多 —— 落笔前重新读一次 SSOT。

## 0. 一句话

### PR #1696 造的端点注册表，就是 P1-B 卡了一周的那张 endpoint 矩阵 —— 但两边互不知情

放着不管，仓库会长出**两张 endpoint 矩阵**：一张决定「能不能计划」，一张决定「能不能物化」，
随时可以对不上。这是 [`ci-change-detection.md`](https://github.com/hw-native-sys/simpler/blob/937c57efc71ad01a01c3a72072df075d2a5dd24f/.claude/rules/ci-change-detection.md) §1
「一套词汇」原则在更高一层的失效，也是本轮 review 已抓过一次的同类错误
（`BackendKind` 双词汇，作者已改对）。

## 1. 现状（2026-08-07）

 | 条目 | 状态 | 卡在哪 |
 | ---- | ---- | ------ |
 | **P1-A** wire ABI 冻结 | ✅ DONE（#1599 `b535fa21` ＋ #1703 ＋ #1704） | — |
 | **G2 schema 半** | ✅ 已闭（#1704 `4d0de8ea`） | — |
 | **G2 `ImportContext` 半** | 🔴 未闭 | 需 registry/owner authority 供 endpoint 上下文 |
 | **G4** `owner_instance_id` mint 时机 | 🔴 未闭 | 在 `Worker.__init__` 里 mint，早于任何 fork |
 | **G5** close 失败不跳 unlink | 🔴 未闭 | 与本文正交 |
 | **§9 不变量 5** device ref 只到 owner chip | 🔴 未强制 | ＝ `ImportContext` 半 |
 | **#1453** P1-B wire flip | 🔵 DRAFT，**07-30 后未动** | ⛔ **已被 #1729 承接** —— 见抬头更正 |
 | **#1729** P1-B wire flip（现役） | 🔵 OPEN `c9f5db5e`，125f | 🟡 **不再被不变量 5 硬阻**（裁决 A） |
 | **#1696** W2 端点模型 | 🔵 OPEN `24370ad7`，**CI 全绿** | 3 条 blocker（§3） |
 | **W0** a5 host-map 探测 | 🚧 未核实 | 无前置，**无人认领** |
 | **W1 / W4 / W6** 平台链 | ⬜ 未开工 | 全卡在 W0 |

⇒ 共享内存设计的**链 A（W0→W1→W4→W6）整条停摆**；#1696 是当前唯一在动的一环，
且意外压在 P1-B 关键路径上。

## 2. 核心发现：一条没写下来的依赖链

[README.md](README.md) 的 P1-B 行、[feature_roadmap.md](feature_roadmap.md) 的 G2 行、
[p1b-corrected-design.md](worker-memory-model/p1b-corrected-design.md) §9 第 5 条，同一件事说了三遍：

> `materialize` 至今无 endpoint × `address_space` 检查 ⇒ **先补 endpoint 矩阵
> （G2 的 `ImportContext` 半），再 flip wire**

而 §5.2 已经把那张矩阵画好了：

 | endpoint | HOST | DEVICE |
 | -------- | ---- | ------ |
 | L2 chip | ✅ | ✅ **仅其 owner chip** |
 | Python SUB（host 进程） | ✅ | ❌ **提交时拒绝** |
 | L4→L3 转发 | ✅ 不物化，仅 re-export | ✅ 同左 |

**#1696 造的就是这个形状**：按 `(path, deployment)` 索引的端点注册表，
＋ 裁决 `(provider endpoint, consumer endpoint, backend_kind)` 的 `RegionAccessService`。
`RegionAccessQuery` 里两个端点记录和 backend 全带着 —— 这**就是** §5.2 需要、
而 `ImportRegistry.materialize` 现在缺的那份 endpoint × address-domain 上下文。

真实依赖链：

```text
\#1696（W2 端点注册表）
   └─→ G2 ImportContext 半（endpoint × address_space 门）
          └─→ #1453 wire flip（P1-B 出口）
```

**这条链在任何文档里都不存在。** roadmap 把 `ImportContext` 归给 P1-B 自建；
PR #1696 也不知道自己在给 P1-B 供货。

### 为什么信息只能来自 registry

不是排期偏好，是结构性的 —— 判定「device ref 只到其 owner chip」所需的信息**不在 wire 上**：

 | 字段 | 为什么不够 |
 | ---- | ---------- |
 | `owner_instance_id` | 不透明 nonce，不含位置信息 |
 | `owner_worker_path_id` | 按设计 *"DIAGNOSTIC id only … takes part in **no** routing, visibility or identity decision"* |
 | `address_space` | 只有 HOST / DEVICE 两值，**不区分哪张卡** |

⇒ 只能由 registry / owner authority 供上下文。**既然只能有一个 registry 权威，就不该有两个矩阵。**

## 3. PR #1696 的三条 blocker ＋ 两条待澄清

`24370ad7` 已修掉上一轮 11 条里的 9 条（cross-node 硬拒、node identity、`EndpointIdentity`、
重复标签、path 格式、admission fence、adapter 词汇、`from_worker` 私有属性、`EndpointSelector` 绕过），
并把 capability *查表* 重构成 capability *service* —— 比 review 建议的更好。剩下：

### 3.1 🔴 `_owner_instance_id` 重复赋值，且耦合到未闭的 G4

`Worker.__init__` 里赋值两次：先 `uuid.uuid4().bytes`（**16 B**，本 PR 新增），
后被 #1599 的 `mint_owner_instance_id()`（**8 B**，`os.urandom(OWNER_INSTANCE_ID_BYTES)`）覆盖。
第一行是死代码，但**宽度不对**，且 #1599 的 docstring 明写该宽度承重
（*"Must stay a full-width random draw"*）。

真正的问题在耦合：本 PR 把 `EndpointIdentity.session_instance_id` 接到了这个 nonce，
而 **G4 是一条未闭的门** —— 它规定 mint 点必须移到 fork/adoption 之后。
G4 一收口，**在那之前铸出的所有 `EndpointIdentity` 语义都会悄悄改变**，
而这个 PR 的目的恰恰是让身份跨 incarnation 稳定。

### 3.2 🔴 两个 region part 共用一个 `BackendKind`，与 main 上的 C++ validator 冲突

`_backend_kind_for_provider` 给 device provider 的**两个 part 都**发 `VMM_WINDOW`，
`_adapter_candidates` 随后在该 `VMM_WINDOW` 之下给 `COUNTER` 提供 `DIRECT_MAP / HOST_SVM_MAP`。

三份独立依据都判它非法：

1. §5.1 能力矩阵 `HOST × VMM_WINDOW = ❌`，且 `main` 上的 C++
   `validate_buffer_descriptor` **已在强制**（*"unsupported address_space x backend_kind
   (capability matrix)"*）
2. [hardware-constraints.md](cpu-npu-shared-memory/hardware-constraints.md) §5：
   `halHostRegister` **Not support vmm va**（CANN 9.0 `ascend_hal_base.h`）
3. §9 不变量 5

⇒ **Python planner 能产出同一个 `main` 上 C++ validator 会拒收的 plan**，
且 `test_static_service_can_select_counter_direct_map_without_payload_direct_attempt`
把这份 plan **钉成了预期行为**。生产未炸，只因 `NO_IMPLEMENTED_DIRECT_MAP_PROBE` 挡着。

hardware-constraints 已给出该有的形状：*"必须拆成 `control_buffer`（可 host-map 的非 VMM
backing）＋ `payload_buffer`（device VMM）"*。**counter 就是 control buffer，
它的 backing 不能是 `VMM_WINDOW`。**

### 3.3 🔴 `comm_endpoints` 改为硬依赖 extension，而守护该不变量的测试被删

`from _task_interface import BackendKind` 是模块级导入。
上一版带着 `test_comm_endpoints_import_survives_without_the_extension`，
docstring 原文 *"The W2 endpoint model is pure Python and must not import `_task_interface`"* ——
该测试在 `24370ad7` 中**被删除且无替代**。

用 canonical `BackendKind` 是对的（上一轮 review 要求的）。问题是**契约变更没留任何记录**：
`simpler/__init__.py` 仍把 `comm_endpoints` 列在 `_LAZY_SUBMODULES`，
lazy 只是把失败推迟，不是消除。

### 3.4 ⚠️ 待澄清（本目录看板已预先登记）

 | # | 内容 | 状态 |
 | - | ---- | ---- |
 | ① `EndpointId` 缺 `session_id` | [README.md](README.md) 第 63 行登记 | ✅ **本次已修** —— `EndpointIdentity(session_instance_id, registry_epoch, endpoint_id)` |
 | ② `level < 3` 门控 | 同上 | ⚠️ **仍未答复**。`_require_ready_for_region_planning` 保留该门，且**现在还覆盖了 planning**（比单纯建 registry 更贴近 §2.3 那条线）。加一行注释写明「这是 §2.4 控制树可见性，不是 §2.3 能力判定」即可永久关闭 |

> ⚠️ **看板本身过期**：README 第 63 行仍写「5 files / +960 −2」「capability cache 桩」，
> 是 force-push 前那版；现为 1720 行，capability cache 已被 `RegionAccessService` 取代，
> 偏差 ① 已修但未勾。**随本文一并更新。**

## 4. 阶段 0：先拍板，不写代码

这两条一旦写下代码就很贵，**必须先于阶段 1 结束**。

### 决定 ①：endpoint 矩阵谁拥有

 | 方案 | 代价 | 风险 |
 | ---- | ---- | ---- |
 | **A. W2 注册表升级为唯一 authority**，`ImportContext` 作消费者 | W2 补 `owner_instance_id → owner endpoint` 绑定表 | #1696 范围小幅扩大 |
 | B. 两边各建各的 | 现在最省 | **两张矩阵长期漂移** |

**建议 A** —— 理由见 §2「为什么信息只能来自 registry」。

### 决定 ②：`session_instance_id` 与 `owner_instance_id` 共用还是分离

与决定 ① 是同一枚硬币。若选 A，**共用就不是巧合而是桥**：
拿到 `BufferDescriptor.identity.owner_instance_id` 才能反查是哪个 endpoint 铸的。

**建议：本 PR 保持共用，但写明它是桥**，并在 roadmap 的 G4 行补一句
「G4 收口时同时移动 W2 的 session id」。理由：`EndpointIdentity` 目前
**从不跨进程、从不序列化** ⇒ G4 耦合是**潜在的而非活的**，不必在本 PR 付解耦成本；
但必须留记录，否则 G4 动的时候没人知道会波及 W2。

## 5. 合入顺序

 | 阶段 | 内容 | 规模 | 退出条件 |
 | ---- | ---- | ---- | -------- |
 | **0** | 决定 ① ②（用户拍板） | 半天 | 两条决定写入本文 |
 | **1** | #1696 收口：§3.1 删死代码＋注明桥 · §3.2 `_backend_kind_for_provider` 收 `part`（或从 `VMM_WINDOW` 的 counter 候选里删 `HOST_SVM_MAP`）· §3.3 恢复/替换测试 · §3.4② 加注释 | 1–2 天 | CI 绿 ＋ 4 条 ＋ G4 行补 W2 波及说明 |
 | **2** | **G4 收口**：mint 点移到 fork/adoption 之后，拒绝重复 adopt | 2–3 天 | G4 转 ✅；两处身份 mint 时机一致且有测试 |
 | **3** | **`ImportContext` 半**：`materialize` 收显式 endpoint 上下文 · 用 W2 注册表实现 §5.2 判定 · 补 `owner_instance_id → owner endpoint` 绑定 · 两道防线（提交时拒绝＋materialize 兜底） | ~1 周 | 不变量 5 转 ✅；§9 由 7/8 → 8/8；G2 全闭 |
 | **4** | ~~**#1453 评估**~~ ⛔ **已作废** —— wire flip 由 **#1729** 承接且**不再被本链阻塞**（裁决 A，见抬头更正）。本阶段改为：**`ImportContext` 落地后回头补 P1-B 的第二道防线**（chip 行的 `materialize` 兜底 ＋ 一条「绕过 submit 直接喂 blob 仍被拒」的用例），把 §9 不变量 5 从「声明」变成「强制」 | 待评估 | 不变量 5 转 ✅ |

**阶段 2 为何排在 1 之后**：#1696 合入后，G4 的影响面才完整可见（buffer 身份 ＋ endpoint 身份两处）。
反过来先做 G4，#1696 rebase 时要再改一次同一处。

⚠️ **本表只覆盖 P1-B 这一侧。W2 的另一半（成员词汇 / `deployment` 维 / #1456 的 wire 时间窗口）
在 [cpu-npu-shared-memory/w2-w5-inversion.md](cpu-npu-shared-memory/w2-w5-inversion.md) §6** ——
那份的 **S1（W2-min：`members` 换成带 `deployment` 的结构）实际上要_消费_本文阶段 1 的产物**
（`EndpointDeployment{HOST_CPU, DEVICE_AICORE}` 就在 #1696 的 `comm_endpoints.py` 里），
两份都把对方当成了「并行项」。**合并后的顺序以 [`../README.md`](README.md) §1.5 为准。**

### 并行轨道（不占关键路径）

 | 轨道 | 何时可开 | 说明 |
 | ---- | -------- | ---- |
 | **W0** a5 host-map 探测 | **立刻** | 无前置、**XS 规模**，却是链 A 唯一的塞子 ⇒ **当前最划算的一件事** |
 | **a2a3 logical→driver id gate** | **立刻** | work-breakdown 明写「不受 W0 阻塞，应独立先修」，是既有 correctness 缺口 |
 | **G5** close 失败不跳 unlink | 随时 | P1-B lifetime，与端点矩阵正交 |

```text
现在 ─┬─ 阶段0 决定①②（半天）
      ├─ 阶段1 #1696 收口 ──→ 阶段2 G4 ──→ 阶段3 ImportContext ──→ 阶段4 #1453
      ├─ W0 探测（并行，XS）
      ├─ a2a3 id gate（并行）
      └─ G5（并行）
```

**关键路径 ＝ 决定① → #1696 → G4 → `ImportContext` → #1453**，约两周半。

## 6. 风险

 | 风险 | 应对 |
 | ---- | ---- |
 | **决定 ① 拖着不定，两边各写一套矩阵** | **最贵的失败模式** —— 一旦两套都有测试就很难合并。阶段 0 必须先于阶段 1 结束 |
 | `.docs` 不在仓库里，作者看不到 G4 / `ImportContext` 这两条 | review 评论必须**摘录结论**，不能只给链接 |
 | 看板过期（§3.4 注） | 随本文更新 README 第 63 行 |
 | #1453 静置 8 天，作者可能已转向 | 阶段 3 开始前确认还有人接，否则 P1-B 出口无人走 |
 | W0 长期无人认领 | XS 却卡整条链 A；每拖一天，W1 是「翻门控」还是「移植实现」就多不知道一天 |

## 7. 与其它文档的关系

- [work-breakdown.md](cpu-npu-shared-memory/work-breakdown.md) §4 列了两条交叠提醒
  （W3↔P0.2 簇C、W2 path ↔ P1-A path side table）。**本文是第三条**，
  且比那两条更紧：那两条是「别各做一套」，这条是「同一件活被算进了两个里程碑」。
- [p1b-corrected-design.md](worker-memory-model/p1b-corrected-design.md) §5.2 是矩阵的**规范**，
  本文只回答**谁来实现、什么顺序**，不改任何字段语义。
- [feature_roadmap.md](feature_roadmap.md) §2.4 的 G2 / G4 行需按 §5 阶段 1／2 的退出条件更新。
