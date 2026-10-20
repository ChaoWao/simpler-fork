# Issue / PR 关联总表

> **发布快照：2026-10-19。** 设计、实现状态与历史调查须按正文各自基线阅读；不代表当前 main 全量验收。参见[阅读入口与已知更正](../README.md)。
>
> 来源：原 `l3l4-cluster-requirements-analysis.md` 跨节说明 + §E + 附录 + §F。

## 跨节说明:持久化域(#824)与 investigations 立项

### #824 —— per-worker 域 ownership

当前实施状态只见 [RA-12 / DS-4](../work-items.md)。`77132c7bf` 的 `retain_after_run` 已提供跨 run 存活；独立 Worker ownership 的 PR #2120 仍 OPEN。
资源寿命只有 per-orch 与 per-worker 两种。本文 2026-07-30 的「必须新增第三种 lifetime」建议已被 2026-08-03 裁决撤回；不得作为新 registry 的依据。

### investigations 立项(discipline.md §4)

在动手 D.3/D.5 前,建议在 `docs/investigations/` 记录 **"cross-host / host-in-domain 的 windows backend 选型"** 决策(VMM vs URMA/RDMA vs fabric handle;**当前 investigations 目录无相关条目**),避免后续重复论证 —— 并把该条目加进 `docs/investigations/README.md` 索引。

---

## E. 上下级 worker 并行能力（消息面 vs 执行面分离）

> 用户要求的"上下级 worker 并行"是 **L3↔L2、L4↔L3 的消息线程与执行线程分离**，
> 不等同于 C.3 调度语义或 D 通信域。#1090 已 CLOSED-未合并，仅保留为历史原型；
> 当前 merged 基线是 #1541 whole-run FIFO 与 #1574 B3a phased native lifecycle。

### E.1 当前代码状态 —— native 可推进，edge 仍阻塞

\#1541 与 #1574 解决了两个不同 ownership 层次，不能混成“edge 已异步”：

- **whole-run FIFO（#1541）**：`Orchestrator::begin_run()` 在 graph callback 前取得 negotiated pipeline lease；只有 FIFO head 可产生 device effect，run-owned teardown/cleanup publication 也在同一事务内。#1467 的 launch acceptance 不再 gate 下一 callback。
- **B3a native seam（#1574）**：C ABI 已拆为 `simpler_prepare_run` → `simpler_launch_run` → `simpler_poll_run`/`simpler_wait_run` → `simpler_finalize_run`；`simpler_run` / `ChipWorker.run()` 保持 blocking composition。`finalize` 仍负责 validation、copy-back、DFX 与 Runtime destruction。
- **child endpoint 仍单帧 blocking**：`_run_chip_main_loop` 的 task handler 仍调用 blocking `run_from_blob`；`_child_worker_loop` 仍在 TASK frame 内调用 inner `Worker.run()`。两者都要等完整 run 返回才重新接收下一帧。
- **local parent 仍整段持锁**：`LocalMailboxEndpoint::run()` 从 payload/state 发布一直持有 `mailbox_mu_` 到 `TASK_DONE`/failure；所有 `control_*` 同锁，长 run 期间发不进 control。
- **remote 只分离了 health**：remote session 已有独立 command socket 与 health socket/thread，但 TASK 与 CONTROL 仍共享 ordered command lane；长 `inner_worker.run()` 仍阻塞该 lane。

**结论**：#1574 让 host-runtime boundary 可以被 service 驱动，但现有 child 没有利用该 seam；
它没有增加第二条 mailbox frame、第二个 device execution 或 responsive EdgeChannel。

### E.2 直接相关 Issue/PR

- **#1541 merged `9a2a5f71`** —— negotiated whole-run FIFO、failure-safe run resource/finalization transaction 与 direct-control ordering；是 B3 的 hierarchical admission 前置。
- **#1574 merged `87dce627`** —— progressable native `prepare/launch/poll/wait/finalize`、sticky launch wakeup、run-owned Runtime/DFX identity 与 stale token protection；是 **B3a ownership seam**。
- **#1090 CLOSED-未合并** —— `TASK_RUNNING` + register lane + L2 direct Worker 的历史原型。它证明过一种局部 overlap 形状，但未进入 main，且其并发 contract 已被 #1541/#1574 之后的 ownership 约束取代，不能作为实现基础。
- **#1379 open** —— 跨 token 的 pipelined orchestration/scheduling；与 responsive edge 相关但不是 B3b 本身。
- **#583/#605 merged** —— 递归 Worker 组合与错误传播，是 edge 进程模型底座。

### E.3 差距

1. **缺 B3b local receiver/service**：local child 没有在 unfinished native run 期间回到 receive loop，也没有独立 health/status path。
2. **phase ownership 必须串行**：每个 DeviceRunner 仍只允许一个 unfinished native run；同一 token 的 poll/wait/finalize 只能由一个 serialized phase owner 驱动，不能让 control threads 竞态调用。
3. **payload 生命周期尚未接到 finalize**：args container 可在 prepare 后复用，但其 Tensor backing、Python keepalive 与未来 attachment lease 必须保留到 `finalize` 返回，因为 copy-back/validation 仍可能在该阶段访问它们。
4. **mutating control 不能直接开放**：runner claim 从 prepare 保持到 finalize，register/unregister 当前明确拒绝；malloc/copy/domain/region 等 device-touching control 还受 #1541 whole-run/direct-control ordering 约束。
5. **无统一 EdgeChannel 抽象**：L3↔L2 local shm / L4↔local L3 / L4↔remote framed / L3-L2 mapped-region 数据面仍是四套栈，各自的 sequence、backpressure、health、cancel/drain 与 completion 语义不一致。

### E.4 建议 / 落地路径

1. **先做窄 B3b：只改 L3→L2 local edge**。receiver lane 负责 decode/validate/retain、发布 `TASK_ACCEPTED` 并 enqueue；单一 serialized native phase owner 负责 `prepare/launch/poll|wait/finalize`，随后发布 `TASK_COMPLETED/FAILED`。
2. **B3b 首版只开放 responsive health/status/receive**。registry mutation 与其它 device-touching control 先 queue 或明确拒绝；shutdown/drain 必须由同一 owner 协调，不能绕过 native finalize 或 #1541 cleanup publication。
3. **把 Tensor backing/keepalive retain 到 finalize**，并保证 phase 调用只有一个 owner。这个要求是接线前置，不因 launch marker 或 execution completion 提前释放。
4. **通用 EdgeChannel 留到 B3c**：待 Buffer/Attachment wire+retain 契约就绪后，再统一 sequence、bounded queue/backpressure、accepted/completed、epoch、tombstone、deadline、cancel/drain 和独立 health path，并逐步扩到 local L4 与 remote framed transport。
5. **第二个 native execution 独立后置**：先把 execution-only DeviceRunner state 做成 per-run，再谈多个 unfinished native execution；depth-two whole-run capacity 本身不授权 device overlap。
6. **保留 L2 SceneTest root-worker 特例**：standalone `Worker(level=2)` 无上级 mailbox，synchronous run 可继续是 phased lifecycle 的 blocking composition；L3 SceneTest 的 root-local tensor 仍须显式提升为 SharedBuffer/ChildBuffer 后再下发。

---

## 附录：Issue / PR 关联总表

### A. 已合并能力栈（构成各诉求的既有基线）

 | 栈 | PR（全 MERGED，除注明） | 推进了哪条诉求到什么状态 |
 | -- | ----------------------- | ------------------------ |
 | **Remote-L3 worker**（从 #866 拆栈） | #1006 契约 · #1007 import descriptor · #1008 wire codec · #1009 endpoint eligibility · #1010 C++ facade · #1011 Python session | A.1 / D.4：从零到"契约 + 四层骨架（identity→wire→eligibility→facade→session）已合并"。⚠️ **此处此前写「仅 sim 打通」—— 已作废（2026-08-05，#1688 `e78d8437`）**：那条 transport 从来是真 TCP socket，`"sim"` 只是个撒谎的名字（现已正名 `host_tcp`）；#1688 更给出**双机真硬件**的 L4 mixed-L3 用例与 CI job。**当前边界**：host_tcp 属控制 transport；主路径为 mpirun，A3 Fabric 共享窗口已有数据面。原生通信 backend 验收见 DS-5；SHMEM 仅为实现参考。承接情况已变：⛔ #866/#711 仍 CLOSED-未合并，但 **#1688 已 merged**；#1456 亦已 merged `60ab6ab9`（2026-08-11），其后继 #1623 `cc9bfc45` / #1624 `43094d44` 亦在 main（`68d137fdf` 复核）。 |
 | **Comm-domain**（HCCL 动态域 + L3-Host↔L2 在途通信） | #817 Option A · #809/#813 类型/清理 · #1334 VMM 跨进程窗口(#1037) · #1015 设计 · #1305 a2a3 onboard · #1277 a5 onboard；**#1291 CLOSED-未合并** | D.1 完整落地；D.3 的一半（L3 Host CPU 与本卡 L2 AICPU 共享控制域）。#1291（把 onboard 数据路径统一搬到 L3 Host mapped-region）虽 CLOSED-未合并，但其目标已在 main 实现（`l3-l2-orch-comm.md:48`：sim+onboard 均在 L3 Host 走同一 mapped 路径）；域生命周期仍 run-scoped（=#824 未解）。 |
 | **L3-L2 message-queue** | #1130 底座 · #1187 示例 · #1236 L2 input window | D.2：一个持久 L2 orch run 与 L3 交换一串 input/output 消息，免每 task 停/起 L2；为 C.1 的"L2 常驻"提供通信载体。当前仅 **SPSC**；#755 MPMC 设计已 CLOSED-未合并。 |
 | **Memory / host-buffer + runtime-arena** | #1256 post-fork zero-copy · #1390 暴露给 SubWorker · #1234/#1193 arena 复用 · #1322 prewarm · #1245 去 local-ready-buffer（#1198 已回滚，不计） | B.kind3（host 侧）权威落地；arena 预热服务 C.1（eager）与 #984/#545 perf。 |
 | **Distributed 基座 + 集合通信例库** | #444 fork+shm 进程模型/group task · #807 allreduce 2..16 · #888 broadcast/all2all · #842 AllGather/ReduceScatter · #1267 all2all push-lowering | D.1/D.5 的可运行通信语义与 group-dispatch 基线；#444 的 fork+shm 同时是 A（拉起下级）与 C.1（进程隔离）的底座。局限：集合对端仍 device-only（NPU↔NPU）。 |
 | **L4 递归组合 + 错误传播** | #583 递归 Worker(`_child_worker_loop`) · #605 L4 错误传播 | A 的本地多层组合（L4 fork 并递归驱动本地 L3→L2，child 异常端到端上抛）。局限：仅本地、同机、同进程树 fork。 |
 | **启动 readiness 协议** | #1396（merged `35b76c53`） | C.1 前置：init 从"无界自旋 + 失败静默退出"改为**有界 READY 边界**——INIT_READY/INIT_FAILED + `_await_children_ready`(deadline + `waitpid`) + `_abort_hierarchical` 回滚，覆盖三 edge。 |
 | **eager 递归 init** | **#1397（merged `483d97e3`，2026-07-19）** | **C.1 完整落地**：eager 递归 `init()`（返回即 full-tree READY）+ 删 5 lazy trigger + 单一 startup epoch deadline + `_abort_hierarchical` 扩为 SIGTERM/killpg 子树回收 + prewarm-before-READY + `child_memory` guard + SceneTest rehost。取代 #1381。 |
 | **生命周期加固（第一批）** | **#1398（merged `0ffb88f8`，2026-07-21）** | **P0.2 narrowed slice**：单一权威 5-state `_Lifecycle`（无 CLOSING）+ terminal single-shot `close()` + **INITIALIZING close = fail-fast**（active-cancel 未做）+ leased-op admission + eligibility 校验 + L2 两并发 init（第二个立即报错，非串行）+ 两段式 child shutdown。**deferred**：register/unregister admission、cooperative cancel、可重试账本、ChipWorker gate（P0.2.md（本地参考：`../P0.2/done-followup.md`） follow-up）。见 done-P0.2.md（本地参考：`../P0.2/done.md`）。 |
 | **P0.3 remote startup budget** | **✅ #1420 形状 + #1432 收尾（`effffe52` + `8fd7c27e`）** | C.1 remote 分支：activation-after-fork（`_activate_remote_sessions`）+ 去 per-remote full-timeout 复制 + `startup_remaining_s` duration wire + remote monotonic clock。**#1420 交付形状，4 实现缺陷全闭**：#1426 校验前移 + #1427 daemon-send + **#1432** R2+R4（单 root deadline 硬墙 + attach/runtime timeout 拆分，一个耦合 PR）；validation-ordering 证据 #1426 mock 补上 → **P0.3 实现缺陷 4/4 全闭**。**里程碑 acceptance 未满**：roadmap 记 **P0.3 PARTIAL**，唯一剩余门 = full-hierarchy failure-injection 零残留回归（test-coverage follow-up），send-未确认歧义 = P2。见 §D.4 / `KNOWN_ISSUES.md`（已移除 R2/R4）。 |
 | **SceneTest arg-name 校验** | **#1422（merged `4dbcc9c8`，2026-07-21）** | rehost correctness：`TaskArgsBuilder` 拒绝 duplicate / builder-member-shadowing arg name（原子、mutation 前），闭合 `_originals` name-keyed rollback 的 use-after-free。 |
 | **whole-run FIFO admission** | **#1541（merged `9a2a5f71`）** | C.3/E：negotiated pipeline lease 在 callback 前提供 backpressure；只有 whole-run FIFO head 执行，run-owned resource teardown 与 cleanup publication failure-safe。取代 #1467 launch acceptance 作为下一 callback gate。 |
 | **progressable native-run lifecycle（B3a）** | **#1574（merged `87dce627`）** | E：交付 `prepare/launch/poll/wait/finalize`、sticky launch wakeup、run-owned Runtime/DFX identity 与 token guard。single-frame child 仍 blocking、每个 DeviceRunner 仍只有一个 unfinished native run；B3b 尚未实现。 |

### B. 2026-07-17 当时的 OPEN PR 摘要（历史快照 + 当前状态）

> 本表保留当时的在途脉络；第一列直接标出本轮已核实的现况，不能据表名把条目
> 推断为当前 OPEN。未逐项标注的条目仍须以 `gh` 现况为准。

 | PR | 对齐 | 目标 |
 | -- | ---- | ---- |
 | ~~#1381~~ | **CLOSED — superseded by #1397** | 早期最小 eager patch（贡献者，`eager-l2-worker-init`）。目标已由 #1397（merged `483d97e3`，完整事务化递归版）完全覆盖且范围更广。**已于 2026-07-19 关闭**（附 superseded 说明 comment）。 |
 | ~~#1003~~ **MERGED** | C.1（TASK 阶段，补 #1396） | 修 L3 chip child 发 TASK_DONE 前退出导致父进程无限轮询挂死。#1396 覆盖 init，#1003 覆盖 dispatch/TASK。 |
 | ~~#866~~ **CLOSED-未合并** | A.1 / D.4 / D.5 | remote Worker(level=3) C++ 历史原型；设计骨架以 merged #1006–#1011 为准。 |
 | ~~#711~~ **CLOSED-未合并** | A.1 / D.4 | L4→L3 + RXE 历史路线；不要派生第二套 remote stack。 |
 | ~~**#803**~~ **CLOSED-未合并** | **D.1 / D.2 / D.3** | L3↔L2 host-device communication 历史原型；当前代码基线是 merged #1015/#1305/#1277。 |
 | ~~**#861**~~ **CLOSED-未合并** | **D.3 底层原语 / B.kind3** | `HostDeviceMappedRegion` 历史设计；需求仍由当前 shared-memory 设计承接。 |
 | ~~#755~~ **CLOSED-未合并** | D.1 / D.2 | MPMC 语义历史草案（docs-only，无 main 落地）。 |
 | ~~#1345~~ **merged `ee40a7f0`** | C.3 相邻 | 最终 scope = barrier-free per-thread scheduler init（早期 "skip handshake barrier" draft 描述已过时）；不作 C.2/C.3 callable 统一基线。 |
 | #1379 | D.2 / C | Pipelined orchestration：token N+1 编排与 token N 调度/执行重叠（per-token 背压式 L3↔L2 交互，含 Perfetto 证据）。 |
 | ~~#1392~~ **merged `ca000263`** | D（A5 传输后端） | A5 URMA backend 完整 submit/CQ/host-overlay 链路；当前仍 capability/workspace gated，且不是 Remote CommDomain/window bootstrap。 |
 | ~~**#1090**~~ **CLOSED-未合并** | **E（历史并行原型）** | `TASK_RUNNING` + register lane + L2 direct worker；只作设计审计，当前 B3 基线是 #1541/#1574。 |
 | #1137 | C.3 对照 | 把 a2a3 L2 tmr 的"逐 consumer RMW 通知"wiring 改成 polling readiness（中位 −16.9%）——理解 L2 自由调度的最新权威实现，是 C.3 要在 L3 简化掉那套的**对照基线**（非同层）。 |

> 注：**#1190（L3 post-fork host-buffer registration）实际 CLOSED-未合并**（早期方案，已被 merged #1256/#1390 取代），此前误列为 open，已从本表移除。

### C. Issue → 诉求映射（含已关闭的历史证据）

 | Issue | 对齐 | 诉求 |
 | ----- | ---- | ---- |
 | **#824** | D.1/D.5 + C.3 | 通信域跨 `Worker.run` 持久化；当前由 RunHandle-owned `_RunResources` 在 run finalization 中清理，仍不能跨 run 存活。 |
 | #984 | C.2/C.3 + D.2 | Streaming orchestrator：量化 per-task submit 热路径（去 TensorMap 把 lookup+dep+insert 从 23.4%→4.7%）——评估"tmr 能否精简"的量化基线。 |
 | ~~#1341~~ **CLOSED** | C.3 实证 | 单 AICPU 调度线程囤 running+pending 饿死 sibling——**直接佐证"L3 改成 submit 时用户指定 worker、不搞 L2 式自由调度"**（Issue 已 CLOSED，仅作论据）。 |
 | #1388 | C.3 张力 | TensorMap 扩为 READER/WRITER 追踪器自动推导 WAR——**方向与 C.3 相反**（L2 侧继续增强 auto-dep），需注意张力。 |
 | #545 | C.3/D.2 | tmr runtime 整体性能优化伞状追踪（orchestrator 专项已拆到 #984）。 |
 | ~~**#1105**~~ **CLOSED** | **C.3 / E（历史 HOL 证据）** | 旧 eligible-endpoint requeue 机制已由 #1436 删除；保留为 directed-scheduling 的历史动机。 |
 | #995 | 横切 | DFX 能力总览（Args Dump / L2 Swimlane / DepGen 等）——C/D 改造与 perf 分析的诊断底座。 |

### D. 已完成的去重 / 关闭结论

- **remote-L3 轴**：#866/#711 已 CLOSED-未合并；当前只读 merged #1006–#1011，不再建议 rebase closed heads。
- **host↔device / MPMC 轴**：#803/#861/#755 已 CLOSED-未合并；需求没有消失，但实现应从 main 与当前设计重启。
- **并行原型**：#1090 已 CLOSED-未合并；#1541/#1574 是当前 ownership 基线。
- **其它状态订正**：#1003 已 MERGED；#1105 Issue 已 CLOSED；#1190/#1303/#1381 已 CLOSED-未合并；#1392 已 MERGED但 capability/workspace gated。

### E. 建议在 `docs/investigations/` 立项的决策（discipline.md §4；当前目录无相关条目）

1. **cross-host / host-in-domain 的 windows backend 选型**：VMM shareable-handle（现，host-local）vs URMA/RDMA vs V2 Fabric handle（**跨服务器 sample 当前仅 A3**）——服务 D.3/D.5。
2. **HCCL 建域跨机化**：rootInfo 文件式 → socket 带外分发（opaque bytes）vs ranktable（`HcclCommInitClusterInfo`）；并记录"管理层级 L4→L3→L2 ≠ HCCL rank 嵌套 → **flatten 全部 L2 成 global device rank 建单一 communicator**（非每层建 world），子集用 `HcclCreateSubCommConfig` 从 global 拆分"这一约束。

---

## 附录 F：2026-07-17 当时的全量 OPEN 清单（历史 inventory）

> **基线**：`main@8fba23214e9359cf6a32ad13e89243c9758644c7`；**查询时间**：2026-07-17。
> ⚠️ **这是 2026-07-17 的时点快照，不是当前 OPEN 清单**。当前 `main@b69f4537`
> 对应统计见文首的带日期查询快照（不要在此重复一份会漂移的数字）。例如
> 下表 #1381（当时 open）已 superseded-**关闭**、其目标由 **#1397 merged
> `483d97e3`**（当时尚未存在）交付；核对具体 PR 时以 `gh` 现况为准。
> **快照统计**：Issue 218（16 open / 202 closed）；PR 1176（24 open / 970 merged / 182 closed-unmerged；5 open draft）。
> 复现：`gh api --paginate 'repos/hw-native-sys/simpler/pulls?state=all&per_page=100'` 与 `.../issues?state=all`（**issue 计数须过滤 `select(.pull_request==null)`，否则 `/issues?state=all` 会混入 PR；或用 GitHub Search `is:issue`**）。
> relevance：**direct** = 本路线图 A–E 直接抓手；**adjacent** = perf/dep/DFX/arch 邻近；**unrelated** = 与本图无直接关系。

### F.1 当时的 24 个 OPEN PR

 | PR | draft | relevance | title / 归属 |
 | -- | ----- | --------- | ------------ |
 | #711（现 **CLOSED-未合并**） | | **direct** A.1/D.4 | Python distributed L4→L3 dispatch（含 RXE 数据面） |
 | #755（现 **CLOSED-未合并**） | | **direct** D.1/D.2/E | L3/L2 multichannel MPMC 设计（docs-only，无落地文件） |
 | #803（现 **CLOSED-未合并**） | | **direct** D.1/D.2/D.3 | L3/L2 host-device communication（C-ABI + a2a3 onboard） |
 | #861（现 **CLOSED-未合并**） | | **direct** D.3/B | L3/L2 host-device mapped region 设计 |
 | #866（现 **CLOSED-未合并**） | | **direct** A.1/D.4/D.5 | remote L3 worker（历史 head 为 C++ 实现） |
 | #906 | | adjacent perf | [WIP] A5 performance opt |
 | #986 | draft | unrelated | Paged Attention benchmark vs pypto |
 | #1003 | ✅ **MERGED** | **direct** C.1/E | Fix L3 worker child-exit hangs |
 | #1090（现 **CLOSED-未合并**） | | **direct** E | async chip callable register/run overlap（TASK_RUNNING + register lane） |
 | #1137 | | adjacent C.3 对照 | Replace wiring with polling readiness（L2 tmr，−16.9%） |
 | #1238 | draft | adjacent perf | 2O+2S Pipline |
 | #1243 | | adjacent perf | prefetch dispatch buffers + inline timestamp |
 | #1266 | draft | unrelated | pto-isa spmd_paged_attention_highperf |
 | #1309 | | unrelated | test: block_dim=0 auto |
 | #1345 | draft | adjacent C.3/E | Decouple Orchestrator from handshaking |
 | #1362 | | unrelated DFX | dfx: report drained slot count |
 | #1363 | | adjacent D.2 观测 | dfx: split async-wait scheduler phase |
 | #1377 | | unrelated DFX | deps_viewer creator edges |
 | #1379 | | **direct** D.2/E/C | pipelined orchestration across tokens/requests |
 | #1381 | | **direct** C.1 | start L2 worker hierarchy eagerly in Worker.init() |
 | #1382 | | unrelated DFX | l2-swimlane phase-record granularity |
 | #1384 | | unrelated DFX | deps_viewer scroll/zoom |
 | #1392 | ✅ **MERGED** | **direct** D.2/D backend | A5 URMA deferred completion backend（当前 capability/workspace gated） |
 | #1395 | | unrelated DFX | deps_viewer scroll/zoom（与 #1384 重复） |

### F.2 当时的 16 个 OPEN Issue

 | Issue | relevance | title / 归属 |
 | ----- | --------- | ------------ |
 | #545 | adjacent C.3/D.2 | Runtime performance optimization tracking |
 | #824 | **direct** D.1/D.5/C.3 | Support persistent communication domains across Worker.run |
 | #905 | unrelated DFX | A5 onboard self-managed perfmon L0 swimlane |
 | #984 | **direct** C.2/C.3/D.2 | Streaming orchestrator design space |
 | #995 | 横切 | [Tracking] DFX capability overview & roadmap |
 | #1105（现 **CLOSED**） | **direct** C.3/E | Group eligible endpoint reuse can leave scheduler requeueing forever |
 | #1209 | unrelated bug | a5 ST CANN driver exit-time SIGSEGV |
 | #1299 | unrelated DFX bug | L2 swimlane dispatch anchor on Sched lanes |
 | #1315 | adjacent D.2(a5) | Require upgraded a5 environment supporting sdma |
 | #1341 | adjacent C.3（**设备侧类比**） | AICPU scheduler one thread hoards slots |
 | #1351 | adjacent D.2(a5) | Extend PTO-ISA version guard to a5 sdma overlay |
 | #1375 | adjacent C.3/dep | Split wait/retain dependency semantics |
 | #1376 | adjacent C.3/dep | Reduce redundant WAIT edges with reachability bitmap |
 | #1386 | unrelated DFX bug | Qwen serving L2 swimlane no merged JSON |
 | #1388 | adjacent C.3 张力 | Track reader accesses in TensorMap → WAR |
 | #1389 | unrelated bug | A5 inlined-kernel pl.read/write all-zero sparse_attn |
