# Simpler 功能演进路线图

> **发布快照：2026-10-18。** 设计、实现状态与历史调查须按正文各自基线阅读；不代表当前 main 全量验收。参见[阅读入口与已知更正](README.md)。
>
> 状态：架构路线图。P0.1 eager-init core 与 P0.2 lifecycle/teardown
> correctness 已交付；P0.2 的 admission、动态 eligibility、cooperative init
> cancellation、retryable cleanup journal、recursive nested-SHM 回收、survivor
> 覆盖和单一 teardown 编排已由 #1663/#1669/#1671/#1672/#1673/#1689 分批闭合。
> P0.3 startup budget 与 full-hierarchy failure-injection/零残留证据已由
> \#1420/#1426/#1427/#1432/#1692 闭合。P1 的 Buffer–Attachment–Tensor 安全 cutover 尚未
> 交付。各 PR/Issue 的实时状态和下一步只以 [.docs 状态看板](README.md) 为准；
> 本文只保留架构契约和不会反转的一次性交付事实。；
> 代码基线：upstream/main@3c64da8f，2026-08-05；本基线含 PR 1681 的
> Worker/Chip/Core 代码命名迁移、PR 1683 的 enqueue/completion drain 拆分、
> PR 1685 的 per-run HostApi/native-run context、PR 1689 的 P0.2 teardown 收尾、
> PR 1692 的 P0.3 full-hierarchy failure-injection/零残留验收、
> **PR 1444 的 host_build_graph Graph Execution** 和
> **PR 1599 的 Buffer/Tensor wire ABI + owner-side `create_buffer`** 和
>
> ## PR 1688 的 mixed local/remote L4 用例 + 双机 CI job（含 remote transport 正名 `host_tcp`）
>
> 、**PR 1703 的 wire gate 加固（G1/G6）**和 **PR 1704 的 descriptor 可信性收口（G3/G2-schema/A1）**，
> 但不含 PR 1453 的后续 wire cutover。；
> ⚠️ **术语更正（2026-08-05，#1688 `e78d8437`）：本文多处写的「simulation remote」是个误名。**
> 那条 remote transport **一直是真 TCP socket**（`socket.AF_INET` / `bind` / `listen`）；
> `"sim"` 只是它的字符串名字，#1688 已正名为 **`host_tcp`**，并给出**双机真硬件**的
> mixed local/remote L4 用例（`examples/workers/l4/vector_add_mixed_l3`，无 mpirun）与 CI job。
> ⇒ **本文凡出现「simulation remote」，读作「`host_tcp` remote transport」**；
> 它**不是模拟器**。启动/控制 transport 不能代表设备数据面；当前主路径为 mpirun，
> A3 HCCS 的 Fabric V2 共享窗口与 peer-TLOAD 已有交付。通信沿用原生 ACL Runtime + PTO；
> session/lease/partition recovery 仍按 DS-1/DS-4 验收。通信定点基线 `68d137fdf`（2026-10-13），详见 §1.3。；
> ⚠️ **#1599 已合入 ≠ P1-A 达成**：它冻结了 byte layout，而本文 §5.6 P1-A 条列的
> 实现级 merge gate **在合入时一条未闭**。收口进度见 §5.6 P1-A 与
> [状态看板](README.md) §1.1；`tasks/task-p1a-close-merge-gates.md`（本地参考：`tasks/task-p1a-close-merge-gates.md`）
> 是那条收口任务，PR **#1703** 已交付其中的 checked-arithmetic 部分。；
> 范围：L2/L3/L4 生命周期、内存、调度、上下级并行和通信域；
> 导航：执行任务与交付记录索引（本地参考：`tasks/README.md`）

## 1. 结论与当前状态

### 1.1 目标拓扑

本文基于以下拓扑：

~~~text
L4：多机协调者
├── Local L3：管理本机选中的昇腾设备
└── Remote L3*：通过控制端点管理远端机器
    └── L2*：每张昇腾卡对应一个 L2 worker
~~~

一台 L3 最多管理 16 张卡，可以只使用其中一部分。不同 L3 下的卡
通过 HCCS、UB、RoCE 或平台支持的其他 Scale-Up 网络互连。

L2/L3/L4 描述逻辑控制树，不是物理可达性的类型系统。Worker 通过
deployment 绑定到 Host CPU、AICPU、AICore 等 execution endpoint；某个
endpoint 能否使用一块物理资源，取决于 allocation backend、互连、runtime
capability 和是否已建立 Attachment，不能由 level 单独推导。

未来 simpler 应收敛到五个核心契约：

1. **Worker.init() 是强 READY 边界。**
   只有整棵下级 worker 树初始化成功后才返回。
2. **物理资源与逻辑组件分离，四类内存成为正式的跨层角色。**
   `Buffer` 表示物理 allocation 的身份和生命周期，`Tensor` 表示逻辑视图，
   `Attachment` 表示某个 endpoint 如何访问它；不再用裸地址、Worker level 或
   隐式 copy 表达 owner、可达性和生命周期。
3. **L3 只执行用户指定的 worker。**
   调度器保留依赖、完成和失败处理，但不再自由选卡。
4. **上下级执行期间控制面始终可响应。**
   消息接收、control、health 与阻塞的设备执行分离。
5. **L4 管理逻辑 CommDomain membership。**
   L4 冻结成员、资源组合、AttachmentPlan、TransportPlan、epoch 和失败回滚；
   只有 device-collective adapter 才分配 global rank、分发 RootInfo。

### 1.2 L3+ eager full-tree READY 已合入，生命周期继续加固

[PR 1396][pr-1396] 先补齐 direct child 的 READY/FAILED handshake；
[PR 1397][pr-1397] 已于 2026-07-19 合入，把本地和 simulation remote
hierarchy 的启动边界正式移入 `Worker.init()`：

~~~text
Worker.init()
├── _init_hierarchical()
│   ├── 创建 mailbox、hierarchical C++ Worker 和 HeapRing
│   └── 只保留 remote spec/descriptor，不连接 daemon、不启动 remote thread
├── _start_hierarchical()
│   ├── 冻结 topology、callable 和 prewarm snapshot
│   ├── fork SUB、Chip 和 next-level child
│   ├── Chip prepare/prewarm 后发布 INIT_READY/INIT_FAILED
│   ├── next-level child 递归 inner.init()，整棵 subtree READY 后再向上 READY
│   ├── parent 使用 deadline + waitpid(WNOHANG) 等待并在失败时 rollback
│   ├── 最后一次 local fork/READY 后 open remote session 并 attach endpoint
│   ├── 启动 Scheduler 和 WorkerThread
│   └── 返回 startup result
└── 提交本轮 READY（PR 1398 已将 final transition 纳入完整 exception/rollback boundary）

Worker.run() / create_host_buffer() / remote control API
└── 只消费已 READY hierarchy，不再隐式启动
~~~

[PR 1420][pr-1420] 已把 `_open_remote_session()` 和
`add_remote_l3_socket()` 都收敛到最后一次 local fork/READY 之后的
`_activate_remote_sessions()`。parent 在每个 remote 前从同一个 root deadline
计算递减的 remaining snapshot，并通过独立 `startup_remaining_s` duration
传给 remote；remote 使用本机 monotonic clock 重建 deadline，不跨机传 absolute
timestamp。

[PR 1426][pr-1426] 随后关闭 numeric timeout 的 resource-before-validation
缺口：只有实际配置了 remote worker 的 parent 才在 `_init_hierarchical()` 创建
mailbox SHM、pre-fork `_Worker`、child 或 daemon socket 前校验
`remote_session_timeout_s`；无 remote 的 L3/local-only L4 不消费这个配置。
daemon 则在 `os.pipe()`、manifest tempfile 和 runner `Popen()` 前同时校验
`session_timeout_s` 与 `startup_remaining_s`。runner 内的第二次不可信输入校验
继续保留。

[PR 1427][pr-1427] 又把每条 daemon client connection 收敛成独立 ownership
transaction：普通 manifest/socket/session 异常不再逃出共享 accept loop；READY/error
reply 的本地 send 或 reaper handoff 失败时，仍由该 connection 的 `finally` 对 live
runner 执行一次回收。`KeyboardInterrupt`/`SystemExit` 继续向外传播以关闭 daemon。
这只证明 local send/handoff 边界；send 返回不代表 parent 已观察 READY，后者仍需
P2 ACK/lease。

[PR 1432][pr-1432] 于 2026-07-22 闭合旧 R2/R4：跨机 parent→daemon 仍只传播
remaining duration；parent 和每台 remote host 各自建立 local absolute monotonic
deadline，daemon→runner 的同机 private manifest 传该 host 的绝对 deadline，所有
connect/send/framed recv、command/HELLO/health attach 都从对应 deadline 重新派生
remaining。最终 READY commit 还会复查 root deadline。native transport 同时拆成
attach timeout 与完整 runtime timeout，避免启动末期余量固化成 READY 后的
TASK/CONTROL/health timeout。

因此 hard-deadline 与 timeout-separation 契约已经交付。[PR 1692][pr-1692]
又补齐完整 hierarchy failure-injection/零残留证据，P0.3 已 **DONE**；production
ACK/lease/onboard transport 继续属于 P2，不计入该条件。

相关实现：

- [Worker 初始化与层级启动][worker-py]
- [Remote L3 session 启动][remote-session]
- [Hierarchical runtime 总览][hier-runtime]

PR 1397 已交付：

- L3 `init()` 返回前 Chip/SUB READY；
- L4+ next-level child 递归等待自己的 descendants，形成 full-tree READY；
- 初始 `ChipCallable` prepare/prewarm 前移到 child `INIT_READY` 之前；
- 移除五个公开 lazy `_start_hierarchical()` trigger；
- local startup 使用统一 epoch deadline，并另给 rollback 有界 cleanup grace；
- local subtree 使用 cooperative SIGTERM 与 `killpg` backstop；
- remote runner 不再在 `inner.init()` 后重复启动 hierarchy；
- SceneTest 增加 eager-fork 所需的 transactional rehost adapter，并保护
  `child_memory` device address 不被 host rewrite；
- 合入 head 的 15 个 CI job 全绿，包括 A2/A3、A5 的 simulation 和 onboard ST。

随后 [PR 1398][pr-1398] 于 2026-07-21 合入 P0.2 的第一批收敛：

- L2/L3+ 共用一个五态 `_Lifecycle`：`NEW / INITIALIZING / READY /
  FAILED / CLOSED`；旧的 `_initialized`、`_hierarchical_started` 只保留为
  派生视图；
- 第二个 concurrent `init()` 在同一个 epoch gate 上 fail-fast，startup 原始
  异常被保存，final READY 位于完整 `BaseException`/rollback boundary；
- `close()` 原子发布 terminal `CLOSED`，READY-only leased API 先停止 admission，
  再 drain in-flight operation；并发 close 固定 join 自己观察到的
  `_CloseAttempt`；
- 实际 native teardown 仍由 init-owner thread 执行；child shutdown 先广播到
  全部 group，再在完整 grace 内交错 reap；
- `_teardown_attempted` 一旦置位就是 single-shot terminal cleanup：残留资源
  会报错并重放同一结果，不会由后续 `close()` 重驱动；在该 commit 之前发生的
  failure 保持可重试，正常路径是 drain timeout，也包括 mid-drain
  `BaseException`；
- initial callable 在创建 startup 资源前按 kind 校验 eligible target；该校验
  在 #1398 合入时只覆盖 startup snapshot，后由 #1672 扩到 post-init register。

[PR 1420][pr-1420] 于 2026-07-21 合入 P0.3 的第一批 simulation remote
correctness：remote open/attach 全部后移到最后一次 local fork/READY 之后；每个
remote 前计算递减 remaining snapshot；manifest 分列 `startup_remaining_s` 与
`session_timeout_s`，remote 以本机 monotonic clock 重建 deadline。该 PR 还补了
duration/fallback/helper validation 和常规 partial-init cleanup 的 device-free
测试；当时未闭合的 hard-deadline 和 native timeout split 后由 #1432 交付。

[PR 1426][pr-1426] 同日把 parent/daemon 的 numeric timeout validation 前移到
真实资源创建入口，并用 device-free UT 证明非法的 0、负数、Inf、NaN 不会创建
parent SHM/`_Worker` 或 daemon pipe/tempfile/runner；检查以实际 remote presence
为 gate，不把无 remote 的 local hierarchy 绑定到未使用的配置。

[PR 1427][pr-1427] 随后关闭 daemon 单连接 shared-fate 缺口：`_serve_connection`
隔离普通异常并保持 live runner 的单一 owner，只有本地 reply send 返回后才把 runner
交给 background reaper；send/reaper handoff 失败则在 `finally` kill/reap。测试覆盖
dead parent、truncated frame、error reply、reaper launch failure、控制异常传播，以及
下一连接仍可服务且 listener/thread 可有界收口。

[PR 1432][pr-1432] 于 2026-07-22 把单 root startup deadline 变成 parent→daemon
→runner→native attach 的 hard wall，并拆分 native attach/runtime timeout。它还把
remote endpoint 限为 numeric address 或 `localhost` 以避开不可界定的 DNS，保持
跨机 wire 为 duration-only，并用 device-free Python/C++ 回归覆盖 HELLO attach、
runtime read/write、最终 READY gate 和 accepted command socket 的 blocking 模式。

[PR 1422][pr-1422] 同日把 `TaskArgsBuilder` argument name 的全局唯一、不得
shadow builder attribute/method 定为入口 invariant，并在修改 builder 状态前
拒绝冲突。这关闭了 duplicate name 覆盖 name-keyed rehost rollback journal 的
独立 SceneTest correctness 缺口，不属于 P0.2 lifecycle 或 P0.3 remote 的
退出条件。

因此，P0.1 与 P0.2 均已 **DONE**。P0.2 的分批交付事实为：

- [PR 1663][pr-1663] 与 [PR 1671][pr-1671] 把 register/unregister 接入同一套
  admission/lease，并以单向 shutdown 标志关闭终态被覆盖的竞争；
- [PR 1669][pr-1669] 建立 device-free fake-chip-L3 harness，
  [PR 1672][pr-1672] 让 post-init dynamic register 与 startup 共用
  per-kind eligible-target 规则；
- [PR 1673][pr-1673] 交付 INITIALIZING 上的 cooperative close-cancel、共享
  close completion、post-success 删除条目的 `CleanupJournal` 和 deterministic
  SHM naming。native startup 段本身仍不可被 Python 强制中断；等待方只作有界等待。
- [PR 1689][pr-1689] 把 startup rollback 与 READY close 归并到一条有序、
  journal-driven teardown sequence；child PID/mailbox 保留到 `waitpid` 成功，
  root 通过精确 tree namespace 回收 recursive nested SHM；region、domain、
  host buffer、remote、native、child 与 SHM cleanup 均遵守 post-success 删除和
  后续 `close()` 可重驱。该 PR 同时交付 `add_worker()` parent/child 原子冻结和
  public `ChipWorker` init-thread ownership gate。

P0.2 的 journal ownership domain 仍只覆盖 Worker-tree startup/close resources；
RunHandle-owned `_RunResources` 继续由单轮 finalizer 管理，P2 的 remote
confirmed-clean/lease/partition recovery 也不属于 P0.2。native startup 段不可被
Python 强制中断同样是明确边界，不是 P0.2 未完成项。

相邻 correctness 状态：

- P0.3 的 post-fork remote activation 与 remaining-duration propagation 已由
  #1420 交付，numeric timeout real-entry preflight 已由 #1426 交付，daemon
  per-connection failure containment 已由 #1427 交付，end-to-end hard deadline
  与 native startup/runtime timeout split 已由 #1432 交付，完整 hierarchy
  failure-injection/零残留证据已由 #1692 交付；P0.3 已 DONE，且不计入 P0.2
  退出条件；
- READY 后 TASK_DONE/CONTROL_DONE 的 child-exit liveness 已由独立的
  P0 runtime-liveness 改动（[PR 1003][pr-1003]）交付；它不属于 P0.2
  lifecycle/cleanup 退出条件。

\#1398 给 native `ChipWorker.init()` 和 hierarchical `_Worker.init()` binding
增加了 GIL release。它不会把 init 迁移到另一条 OS thread，也不改变 owner：
native init 仍在调用线程执行，只是在设备 attach、binary load、prewarm 或
scheduler/worker-thread startup 期间允许其他 Python 线程继续运行。保留这个
行为的主要价值是解释器响应性和不同 Worker 实例的并行，而不是鼓励同一实例
跨线程 teardown。

相关并发 UT 用 `Event.wait()` 人工暂停 init；该等待本身就会释放 GIL，所以这些
UT 不是新增 native call guard 的理由。call guard 面向的是真实 C++ 路径，尤其
`ChipWorker.init()` 的 `dlopen`/binary read/device attach/prewarm；即使产品只
推荐同线程顺序 `init(); close()`，也不应让这段 native 工作独占整个解释器。
换言之，GIL release 对单 Worker 串行功能正确性不是必要条件，但对进程级
响应性是合理且应保留的 binding contract。

同一 Worker 的正常契约仍是：任意线程可以成为 init owner，但实际 native
teardown 必须由该 owner 执行。测试为了制造“同步 init 尚未返回时另一个调用到达”
的竞态，才用另一线程调用 `close()`；这个 foreign call 在 INITIALIZING/READY
都不能接管 native teardown；INITIALIZING 时可以锁存 cooperative cancellation
并 join shared completion。owner claim close 后且
该 `_CloseAttempt` 尚未完成时，其他线程只能 join；terminal attempt 完成后只
返回或重放已存结果。若完成的是 teardown 前的 retryable failure，仍只有 owner
能发起 successor attempt。公开 `ChipWorker` 的同等 owner gate 已由
[PR 1689][pr-1689] 交付：native init 前记录 owner，foreign-thread finalize 在
Python boundary 拒绝。仍不能从 GIL release 推导出 cross-thread-safe。

`CLEANUP_ACK`、finite lease、session epoch/heartbeat、network-partition
reclamation 和 remote onboard backend 不作为 PR 1397 的合入前置，转入
P2 production remote track。本地 Buffer–Attachment–Tensor 基础从 P1 开始，
P3 负责把 local/remote adapter 组合成异构逻辑域并处理 rank capacity。

### 1.3 当前通信：mpirun 控制与 A3 共享窗口

通信定点核验：`upstream/main@68d137fdfd435d8067f0efd9836cb428da3e700a`。
启动主路径是 L4-owned mpirun：rank 0 通过本地 mailbox 接受 task/control，rank 间用 MPI。
不要求预部署 Simpler 常驻 daemon；已有 daemon/host_tcp 作为其他入口保留。

\#1456 `60ab6ab9` 已提供 mixed Local/Remote Global CommDomain 与 A3 Fabric V2 数据面；
\#1623 `cc9bfc45` 与 #1624 `43094d44` 已提供 MPI 启动与 mailbox group dispatch。
Global Domain 通过 PREPARE_EXPORT/IMPORT/COMMIT/ABORT 协调；full-group 由 MPI 交换描述符。
A3 backend 直接用 ACL VMM/Fabric V2 分配、导出和导入窗口，kernel 通过 TLOAD/MTE 读 peer window。
`global_tload_mixed_l3` / `global_tload_mpirun_l3` 是跨机内存访问用例；
`vector_add_mixed_l3` 仅测任务拓扑的边界不能用来否认前者。

通信从原生 CANN ACL Runtime 接口构建，共享窗口由 PTO 访问。
SHMEM 仅为实现参考，不依赖其库；不增加 SHMEM 库适配、heap/team 迁移或必选 HCCL/HCOMM 建域路线。
本地 `comm_init` 的 HCCL rootInfo/barrier 与 Global Fabric 窗口是不同路径；
不要求先建立跨机 HCCL world 才能使用已有 A3 共享窗口。

CommContext 的 64-rank ABI、持久化 ownership、生产会话与失败回收分别归 DS-7、RA-12/DS-4、
DS-1/DS-9；不能从已有代码或历史测试通过推导这些目标全部完成。
具名函数、原生通信层次与后续验收见 [通信方案 D.4](l3l4/cluster-comm.md#d4-remote-l3-通信mpirun--共享窗口)。

## 2. 生命周期、Worker API 与四类内存

### 2.1 递归启动协议

Worker.init() 应执行一次事务化的整树启动：

纯 topology/config/eligible-target preflight 在 lifecycle lock 下先执行，不创建
资源；失败保持 NEW，调用者可以修正配置后重试。preflight 成功才认领
`NEW → INITIALIZING`；从该点起的任何 startup 失败都进入 FAILED，不能隐式
retry 旧 epoch。

1. 冻结 worker 拓扑、callable registry、runtime 和 prewarm 配置。
2. 创建 parent 拥有的 mailbox 和共享元数据。
3. 在任何 scheduler、transport 或 health 线程启动前 fork 所有
   local child。
4. 每个 L2 完成设备和 runtime 初始化后发布：
   - INIT_READY 和 capability metadata；或
   - INIT_FAILED 和有界错误信息。
5. Nested L3 只有在自己的 descendants 全部 READY 后才向上发布 READY。
6. parent 使用 deadline 等待，并通过 waitpid(WNOHANG) 检查进程存活。
7. 所有 endpoint READY 后，才注册进 Scheduler 并允许任务下发。
8. 任意节点失败都回滚整个 startup epoch，并 reap 本轮创建的进程。

截至 [PR 1692][pr-1692] 的 startup correctness 已通过 [PR 1396][pr-1396]、
[PR 1397][pr-1397]、
[PR 1398][pr-1398]、[PR 1420][pr-1420]、[PR 1426][pr-1426]、
[PR 1427][pr-1427]、[PR 1432][pr-1432] 和 [PR 1692][pr-1692] 实现本地直接下级
READY/FAILED、递归 eager
`Worker.init()`、步骤 5 的 descendant propagation、步骤 6 的 local
deadline/liveness、步骤 7、pre-Scheduler rollback plumbing，以及一个覆盖
L2/L3+ 的 authoritative lifecycle 和 final READY rollback boundary；simulation
remote open/attach 已位于最后一次 local fork 之后，并传播 root remaining
duration snapshot；实际配置 remote 时的 numeric timeout 也会在 parent/daemon
创建资源前校验；单个 daemon connection 的普通异常或本地 reply send 失败不再终止
共享 accept loop，并回收尚未 handoff 的 runner；remote startup 已受每 host
absolute deadline 和最终 READY gate 约束，native attach/runtime timeout 已分离；
完整 hierarchy remote failure 的 PID/PGID/SHM/session 零残留证据和 daemon
复用也已交付。capability metadata 和 production-grade remote session
transaction 尚未实现。

目标 Remote session 应与 local edge 使用相同状态契约，HELLO READY 是调度
屏障，不只是健康检查响应。当前 simulation 实现只有 HELLO READY 和
best-effort one-way SHUTDOWN，尚没有 authoritative FAILED、CLEANUP_ACK、
lease 或 confirmed-clean 语义。

[PR 1396][pr-1396]、[PR 1397][pr-1397]、[PR 1398][pr-1398]、
[PR 1420][pr-1420]、[PR 1426][pr-1426]、[PR 1427][pr-1427] 与
[PR 1432][pr-1432]、[PR 1692][pr-1692] 已覆盖：

- 单个 L2 init 失败后快速返回原始错误；
- L2 在 READY 前退出；
- SUB 和 direct next-level 的 READY/FAILED barrier；
- hung child 的 deadline 和非有限 `startup_timeout_s` 配置拒绝；
- READY next-level sibling 的 graceful rollback；
- L3/L4+ local descendant 的递归 full-tree READY；
- initial Chip prewarm-before-READY；
- 删除公开 lazy startup trigger，`init(); close()` 基本路径可用；
- process-group cancellation backstop；
- direct-L3 SceneTest rehost、sub-only host buffer 和
  `child_memory` address-rewrite guard；
- simulation remote post-fork open/attach、递减 remaining snapshot 和
  remote-local monotonic deadline reconstruction；
- parent/daemon 在真实资源入口前的 remote numeric timeout preflight，且
  local-only hierarchy 不消费未使用的 remote timeout；
- daemon per-connection exception isolation、本地 reply send/reaper handoff 失败时
  的 live-runner 回收，以及下一连接继续服务；
- parent/remote host 各自的 absolute startup deadline、每个 blocking stage 的
  remaining 派生、最终 READY deadline gate，以及 attach/runtime timeout 分离；
- numeric-or-`localhost` endpoint preflight、native nonblocking framed I/O 和
  accepted command socket 的 blocking idle contract；
- remote open timeout、later-remote failure、open-success/attach-failure 下真实
  L4/local SUB/remote L3 进程树的 PID/PGID/SHM/session 零残留，以及同一 daemon
  随后可接受健康 session；startup budget 仍以 `TimeoutError` 分类；
- 本地直接下级的 healthy dispatch。

\#1398 已交付 P0.2 的 narrowed slice：五态 authoritative lifecycle、L2 epoch
parity、原始 startup failure/final READY boundary、terminal CLOSED、
exact-attempt close completion、leased-operation drain，以及 startup-only
per-kind eligible-target preflight。

[P0.2 lifecycle hardening][p0-2] 随后分批交付 admission、dynamic eligibility、
cooperative init cancellation 与 retryable journal；[PR 1689][pr-1689] 最终闭合
root-visible recursive nested-SHM reclaim、survivor-before-unlink、完整 cleanup
action 覆盖、public ChipWorker ownership、parent/child topology freeze、
unreaped-child retention 和单一 teardown sequence。P0.2 已 DONE；该 follow-up
文档现应作为交付设计/验收记录读取，而不是当前待办。

两个相邻问题有独立 owner，不计入 P0.2 退出条件：simulation remote 的
post-fork activation/remaining-duration slice 已由 #1420 交付，numeric timeout
real-entry preflight 已由 #1426 交付，daemon per-connection failure containment
已由 #1427 交付，hard-deadline 与 timeout split 已由 #1432 交付，full-hierarchy
failure evidence 已由 #1692 交付，P0.3 已 DONE；READY 后 task/control wait 的
child-exit liveness 已由 [PR 1003][pr-1003] 交付。

Remote `CLEANUP_ACK`、lease/heartbeat、stale epoch、partition recovery 和
onboard transport 属于 P2，不反向阻塞已经合入的 eager core；local
attachment/transfer 基础从 P1 开始，P3 只负责跨 adapter 的异构域组合和
rank capacity。

### 2.2 Local 和 Remote L3 的端点

L4 对 worker 使用稳定逻辑 ID，不应把 IP 和 port 当作 worker identity：

~~~text
WorkerId / SessionId：逻辑身份和生命周期
IP:service_port：L4 找到 L3 daemon 的控制端点
command/health port：单个 session 的协议端点
HCCL/HCOMM port：实际 rank 的通信端口
~~~

同一 IP 上部署多个 L3 没有问题，但必须满足：

- daemon service port 不同；
- session command/health endpoint 不冲突；
- HCCL socket port range 独立或由统一 allocator 管理；
- device lease 不重叠；
- communicator 使用不同 epoch 和唯一标识。

Local L3 可以继续走 LocalMailboxEndpoint 快路径，也可以运行在本机
daemon 中。无论采用哪种 transport，对上都使用相同的 endpoint
状态和 capability 契约。

### 2.3 统一 Worker API

公开生命周期建议统一为：

~~~text
构建拓扑
注册 callable
init
申请或 bind 资源
run / submit
close
~~~

allocation 由部署 endpoint 的 Worker authority 创建和回收；Worker 持有资源
identity、lifecycle 和授权 registry，但不等于物理资源本身。Orchestrator
只负责协调 attachment/transfer、构建任务和依赖关系，不拥有 backing。

资源生命周期只允许两种：**per-orch** 资源由本轮 `RunHandle/_RunResources`
持有并在该轮 fence 后释放；**per-worker** 资源跨 run 存活，由 Worker registry
持有并在显式 release 或 `Worker.close()` 时回收。“persistent”只是 per-worker
的用途描述，不是第三种 lifetime；Worker tree 是资源容器，也不是第三种 owner。

[PR 1397][pr-1397] 已使 `init()` 成为 L3+ 唯一的 hierarchy startup
入口；[PR 1398][pr-1398] 又把 L2/L3+ 归并到一个 authoritative
`_Lifecycle`，并冻结 terminal `CLOSED`。`_initialized` 和
`_hierarchical_started` 现在只是派生视图。

register/unregister admission、INITIALIZING cooperative close-cancel 和 dynamic
eligibility 已由 #1663/#1671/#1672/#1673 收敛；public `ChipWorker` ownership、
`add_worker()` parent/child freeze、journal 失败路径的资源可见性/回收顺序与
统一 teardown sequence 已由 #1689 闭合。P0.2 范围内的 state/API lifecycle
契约已经完成；这不扩张为 RunHandle-owned resource、production remote lease 或
任意 native startup 的强制可中断保证。

建议的 API 形态：

~~~text
Worker.add_local_worker(...)
Worker.add_remote_worker(...)
Worker.register(...)
Worker.init()

Worker.create_buffer(...)
Worker.alloc_shared_tensor(...)
Worker.alloc_child_tensor(...)
Worker.release_buffer(tensor)
Worker.transfer(tensor, target) -> tensor'
Worker.copy_to(..., tensor)
Worker.copy_from(tensor, ...)
Worker.create_domain(..., attachment_plan, transport_plan)

Worker.run(handle, args)
Worker.submit(handle, args) -> RunHandle
Worker.close()
~~~

这里三个语义不能混用，但 Attachment 不作为逐条 public 动词暴露：

- `create_buffer` 创建新的 physical allocation 和新的 Buffer identity；
- attach 为同一 Buffer 建立 endpoint-local Attachment，不复制数据、不改变 identity；
  CommDomain 通过 `AttachmentPlan` 批量声明，普通 worker 树在 tensor 沿 edge
  传播时由 Worker 事务化执行 export/import/map；
- `transfer` 显式复制到目标 physical resource，返回新的 Buffer identity。

`copy_to/copy_from` 是某个 Attachment 暴露的操作 capability，不表示调用它的
host Worker 获得了 Device GM 的可解引用地址。传播即 attach 只在 capability
已经存在并通过 authorization/materialization 校验时成立；否则 submit 必须失败，
不能暗中 transfer。Attachment 的引用由 CommDomain 或 Worker 唯一持有，用户不能
绕过 in-flight retain 手工 detach。

L2 和 L3 内部可以继续使用不同的执行语言，但不应暴露不同的资源
ownership 和 lifecycle。特别是 L3 不应继续接收未注册的 raw Python
orch callable，而 L2 要求 CallableHandle。

### 2.4 物理资源、Attachment 与四类内存契约

内存模型首先分开**物理资源**、**逻辑视图**和**访问关系**。Worker 是逻辑
编排/执行组件；它的 level 只表示控制树位置，实际能否访问某块内存取决于
deployment、物理互连、allocation backend 和已建立的 attachment。

统一词汇如下。`BufferHandle` 只是 owner 侧管理 `Buffer` 生命周期的 1:1 对象，
不成为额外的用户概念，也不跨 edge：

 | 名称 | 契约 |
 | ---- | ---- |
 | `Buffer` | Host DDR、Device GM 等物理存储上的 owner-managed allocation；承载稳定 identity、size 和 lifecycle，不携带任一消费者的 local VA |
 | `CanonicalBufferIdentity` | 一个 Buffer incarnation 的全局逻辑身份；wire 名称为 `CanonicalIdentity`，由 `owner_instance_id + buffer_id + generation` 构成，不能退化成 owner-local `buffer_id` |
 | `Tensor` | `Buffer + byte_offset + shape + strides + dtype` 的地址无关逻辑视图，也是跨 edge 的值；`TensorView`/`BufferRef` 是已废弃词汇 |
 | `Attachment` | `(ExecutionEndpoint, Buffer)` 的访问关系；记录该 endpoint 的 capability、授权、lease 和 endpoint-local materialization |
 | `ChipTensor` | `Tensor` 在 Chip endpoint 上物化出的 GM-address-bearing POD；不跨 Worker edge，也不创造 `L2Tensor`/`LocalTensor` 同义词 |

软件标识符按拥有语义的 runtime domain 命名：L3+ 使用无前缀的 `Worker`、`Buffer`、
`Tensor`、`TaskArgs`；L2 使用 `Chip*`；L0 使用 `Core*`。数字层级只用于拓扑
说明，不能再进入软件名称；例如目标名称是 `WorkerChipQueue`、`ChipSwimlane`、
`ChipTaskArgs`、`CoreTaskArgs`。Python 与 C++ 的 public type 必须使用同一个
canonical name 和语义，不能把 C++ `Tensor` 绑定成 Python `ChipTensor`，也不能
维护两个同名但不同实现的 public `Tensor`。该规则由 [PR 1676][pr-1676] 固化，
存量名称在相关 API/module 被修改时增量迁移，不做全仓机械 sweep。

`Transfer` 是作用于这些概念的显式 data-placement **操作**，不是第六种资源/视图：
它搬运数据并在目标资源上创建新的 `Buffer`。无可用 Attachment 是常见触发，
但即使已有 direct/copy Attachment，也可按 policy 主动选择 Transfer。

~~~text
Worker ──deployment──▶ ExecutionEndpoint
Buffer ──allocated in▶ Host DDR / Device GM
Tensor ─────views────▶ Buffer
Attachment = endpoint × buffer × capability × local materialization
Transfer(A, target) ─▶ new Buffer B
~~~

必须保持以下不变量：

1. 同一物理 allocation 无论在多少 endpoint 映射成多少个 VA，都只有一个
   canonical `Buffer` identity；VA 永远不进入 identity。
2. `Attachment` 是 endpoint 与 Buffer 的相对关系，不是 Buffer 的稳定字段。
   allocation/backend policy 即使允许共享，也不证明某个 endpoint 已经 attach。
3. attachment 可以同时具有 `DIRECT_LOAD_STORE`、`COPY_TO`、`COPY_FROM`、
   `DEVICE_PEER_ACCESS` 等 capability；direct map 与 copy 不是互斥 profile。
4. 注册/import/map 建立 attachment，保持 Buffer identity；`copy_to/copy_from`
   通过已有 Attachment 读写既有 Buffer，也保持 identity。只有显式
   `Transfer` 才在目标 resource 创建新 allocation 和新 Buffer identity；
   Transfer 两端没有隐式 coherence。
5. 普通 submit 不做 silent copy。目标 endpoint 没有满足要求的 attachment 时
   明确失败；用户或 domain plan 必须显式请求 transfer/proxy。合法的 worker-tree
   传播可以建立保持 identity 的 Attachment，但不能借此隐藏 copy。未来若提供
   auto-copy，也必须是显式 policy，并把最终 data path 暴露出来。
   `Transfer` 不是 attachment failure 专用 fallback；用户也可以为 placement、
   isolation 或 lifetime 主动选择它。
6. 自描述 backend descriptor 可以省去部分 eager setup，但不能省掉 edge
   authorization、live registry、retain/release、detach/unmap 和 stale 检查。

统一 capability 判据是：

~~~text
potential = deployment × interconnect × backing/backend × runtime support
actual    = potential ∩ authorization ∩ live Attachment/materialization
            ∩ runtime probe/lease
~~~

因此 Host DDR 是否能在多个 Worker 间零拷贝，不由 `address_space=HOST`
单独决定：POSIX SHM、pre-fork shared mapping 和普通 private heap 具有不同
attachment 能力。同理，某个 host Worker 可以拥有 Device GM 的
`COPY_TO/COPY_FROM` 委派能力，却没有 device VA 或 `DIRECT_LOAD_STORE` 能力。

在此基础上，每层 Worker 都观察到四类**edge-scoped 逻辑角色**：

 | Kind | 含义 | 建议表示 |
 | ---- | ---- | -------- |
 | 1 | 当前 run 借用的参数 | Tensor |
 | 2 | 当前 Worker local | LocalBuffer |
 | 3 | 与选中的下级 shared | SharedBuffer |
 | 4 | 某个下级私有 | ChildBuffer |

这些 kind 不是物理存储类型，也不进入 Buffer identity。递归规则必须是
edge-scoped：

- Parent kind3 成为 child kind1 shared Tensor。
- Parent kind4 成为 child kind1 child-device Tensor。
- Parent kind2 禁止直接下发。
- Root L2 SceneTest 没有上下级边界，可以直接用自身 kind2 作为参数。
- kind3 继续传给 grandchild 时，child 必须在下一条 edge 上重新
  export/import 或 map。

[P1-B corrected design][p1b-abi] 是 P1 fixed field/layout target 的 SSOT；
本路线图是跨模块 identity、Attachment、routing 和 materialization 语义的
SSOT。`BufferHandle` 是 owner-side 生命周期对象，不跨 edge；最终跨 edge 的
`Tensor` 内嵌 descriptor 值拷贝。#1599（已合入 `b535fa21`）把这两个实现类型改名为
`Tensor` 与 `BufferDescriptor`，并把设备侧 Python binding 改名为 `ChipTensor`。
✅ **「Python/C++ canonical type 合一」的两条前置已由 #1681（`b3bf3e7a`）兑现**：
C++ device POD **本身**已名为 `ChipTensor`（`nb::class_<ChipTensor>(m, "ChipTensor")`，
不再是靠绑定层改名），且 `L3L2*`/`L3Host*` 全批迁到 `WorkerChip*`/`WorkerHost*`、无兼容别名、
wire 值不变。
✅ **这条也已兑现**：`Worker` 的 `TaskArgs`/dispatch 现消费地址无关 `Tensor` ——
wire cutover 由 **PR #1729 merged `aa1d7c7d`（2026-08-07）** 落地（#1453 是原 omnibus
DRAFT，已被 #1729 承接并于合入后 CLOSED，未再变更范围重开）。
该文件名中的 “P1-B” 是历史命名：在本路线图当前分期里，fixed identity/wire
layout 属于 **P1-A**，**P1-B** 专指 registry/lifetime。

[PR 1599][pr-1599] **已合入**（`b535fa21`）的 descriptor 是（字段与尺寸由
`buffer.h` 的 `static_assert` 钉死：`CanonicalIdentity` 32 B · `BufferDescriptor` 88 B ·
`Tensor` 144 B）：

~~~text
magic                   # discriminator，不是版本号
address_space           # host / device
access                  # read / write / readwrite
backend_kind
identity:
  owner_instance_id     # 8 B opaque nonce；每次 owner incarnation 都不同
  buffer_id
  generation
nbytes
owner_worker_path_id    # 仅诊断；不参与 identity/routing/authorization
body_len + backend_body # 有界 backend materialization descriptor
~~~

`visibility` 和完整 worker path 不属于 allocation identity。共享范围由
backend、edge authorization 和 live Attachment 表达；`local/remote` 与 owner
route 由当前 endpoint/session registry 推导。canonical allocation identity 只由
`owner_instance_id + buffer_id + generation` 构成；worker path 只是控制拓扑与
诊断上下文。`owner_instance_id` 在本地 P1 也必须真实生成，避免 Worker/session
重建后 buffer_id 和 generation 同时复用造成跨 incarnation ABA。

backend body 描述建立 attachment 所需的 allocation/export 信息，不等于
attachment 本身，也永远不参与 canonical identity。当前 candidate 的
`FORK_*`、`DEVICE_MALLOC` 和 `VMM_WINDOW` body 会携带 raw base/device VA；
该值只能在 validator 可证明地址域相同的 fork descendant 或 exact owner-chip
endpoint 使用，必须拒绝 Python host、其它 chip、任意 re-export 和 remote
consumer。`POSIX_SHM` 等 import backend 则由 consumer 建立 fresh local VA。
因此同一 canonical identity 在不同 endpoint 可以得到不同 local VA、token 和
capability；每个 endpoint 的 `ImportContext/AttachmentRegistry` 负责验证
owner、authorized edge、backend、descriptor fingerprint、address-domain
provenance 和实际 attachment。相同 identity 携带相互矛盾的
backend/body/size/access 必须拒绝。

该 local P1 ABI 不携带 per-handle version：`magic` 只作 discriminator，同构建
边界在解析前用 `SIMPLER_BUILD_COMMIT` 拒绝 binary/source skew；backend body
使用有界、length-delimited 编码并校验 enum、length 和 reserved。未知 backend
必须明确失败。P2 remote 必须在 versioned session envelope 中协商或拒绝
build/capability mismatch，不能把进程侧 `owner_worker_path_id` 当作跨机 route。
所谓 self-contained descriptor 只减少当前 local backend 的 eager export setup；
它不表示 raw VA 可跨任意 endpoint 使用。address-bearing backend 必须执行
exact endpoint/address-domain gate，且不能省掉 authorization、registry、
retain/release、detach/unmap 或 remote handshake。

[P1-B corrected design][p1b-abi] 已把早期 review 指出的两处表面冲突收敛为明确契约：
ref 没有可由任意消费者直接解引用的通用地址字段，但严格 endpoint/address-domain
受限的 backend body 可以携带作为物化材料的 VA；`owner_worker_path_id` 只用于诊断，
不参与 routing、authorization 或 identity。**尚未闭合的是实现而非文档字段语义**：
当前 `DEVICE_MALLOC`/`VMM_WINDOW` materializer 仍把 body 中 owner device VA
直接当 local base，必须先用 exact owner-chip/address-domain gate 把前提变成
可执行检查。该修正不改变 #1599 已列出的 fixed field/layout。

\#1599 的 fixed layout 已对表并**已合入**（`b535fa21`）；精确 head/status 见
[.docs 状态看板](README.md)。⚠️ **下列实现级 merge gate 原为 candidate 的合入门，
但 #1599 合入时一条未闭** —— 它们因此从「合入门」变成 **已发布 ABI 的收口清单**，
逐条状态核于 `main@b535fa21`：

 | # | gate | 状态 | 分期 |
 | - | ---- | ---- | ---- |
 | G1 | view extent 的 offset/shape/stride/dtype 运算必须使用 checked add/multiply，拒绝 `uint64` wrap | ✅ **已闭 —— PR #1703 merged `02f90ef5`**（saturating 运算 ＋ 哨兵 `TENSOR_EXTENT_UNREPRESENTABLE` **按名拒绝**而非与 `nbytes` 比较 ＋ `tensors_overlap` 两端也饱和） | P1-A |
 | G2 | 每种 backend body 必须有 exact schema、reserved-byte 与 semantic validation；raw-address materialize 必须接收显式 `ImportContext` 并验证 fork/exact-owner-chip address domain，不能只凭 `backend_kind` | **schema 半 ✅ 已闭 —— PR #1704 merged `4d0de8ea`**：per-backend body switch（address-bearing 恰好 8 B 且非零基址 · `POSIX_SHM` 为 printable ASCII 无 `/` · `REMOTE_SIDECAR` 空）＋ reserved 尾字节必须为零，且该规则**同时覆盖 `Tensor` 过 `ndims` 的 `shapes`/`strides` 槽位**（`_pad` 显式排除 —— 它是 alignment slack，且 `CanonicalIdentity::_pad` 要故意容忍以保证同一 backing 两次 decode 键相同）。；✅ **`ImportContext` 半 —— PR #1744 merged `0c367015`（2026-08-08）**：新增 `ImportContext(is_host_endpoint, owning_chip_instance_id)`，`ImportRegistry.materialize()` 据此对 DEVICE_MALLOC/VMM_WINDOW 拒绝「host endpoint 一律拒」与「device endpoint 但 owner 对不上」两种情形。范围收窄自 `.docs` 早先的判断：不需要每个消费者进程都查询完整 `EndpointRegistry` —— chip 子进程本身不是 `Worker`（无自己的 `owner_instance_id`），只消费其**父** L3 Worker 铸的 device buffer，故「我是不是 owner chip」收窄成与**一个**已知 nonce 比对，父进程 fork 前就有。已接入三处 `ImportRegistry()` 构造点（`_sub_worker_loop` / `_run_chip_main_loop` / `_run_l2_materialized`）。两条新回归测试（`test_host_endpoint_materialize_refuses_a_device_tensor_directly` ／ `test_chip_materialization_refuses_a_foreign_chips_device_tensor`）直接喂 `materialize()`，绕开 submit，证明这条第二道防线真的挡得住。⚠️ **粒度上限（CodeRabbit review 发现，2026-08-08，已文档化未修复）**：`owner_instance_id` 按 Worker 铸一次不按 chip，故此检查是 Worker-grained——同一多卡 Worker（如 `device_ids=[0, 1]`）下的兄弟 chip 共享一个 nonce，本检查区分不了它们，只挡「别的 Worker」与「host endpoint」两种情形。 | schema 半 = P1-A ✅；`ImportContext` 半 = **P1-B/P1-C ✅ 已闭**（粒度上限见左栏，留作已知边界不阻塞） |
 | G3 | identity cache 必须绑定 semantic descriptor fingerprint，POSIX shm import 还须核对实际 object size；同一 identity 携带冲突 descriptor 必须拒绝 | ✅ **已闭 —— PR #1704 merged `4d0de8ea`**：`ImportedBuffer` 记住来源 descriptor，cache 命中时 field-wise 比对、**不等即拒且保留已发出的映射**（callers 可能已持有其地址）；`POSIX_SHM` 分支 map 后核对 `shm.size >= desc.nbytes`，不足即拒并先 `close()`。⇒ **G1 的「算得对」现在建立在真前提上** —— `nbytes` 不再只凭 owner 自报 | P1-A |
 | G4 | `owner_instance_id` 必须在实际 owner fork/adoption 之后生成，并拒绝同一 NEW Worker 被重复 adopt | ✅ **PR #1744 merged `0c367015`（2026-08-08）**。`init()` 在 `NEW -> INITIALIZING` 转换后重新 mint —— next-level child 的 `init()` 只会在 fork 出的进程内运行，故该 nonce 从不早于自己的 fork。`__init__` 里仍保留一次 mint 作兜底（本仓大量测试 harness 直接改 `_lifecycle` 而不走真 `init()`，需要一个格式合法的 nonce），但生产路径的身份一律以 `init()` 里那次为准。**「拒绝同一 NEW Worker 被重复 adopt」本就已被 `add_worker()` 的 `_topology_parent` 守卫 ＋ `init()` 自身的 NEW-only 重入检查覆盖**，未新增代码。⇒ 本 PR **同时**关了下一行 G2 的 `ImportContext` 半，见 [`README.md` §1.1 #1744 行](README.md) | P1-B（owner authority）✅ 已闭 |
 | G5 | mapping close 失败不能跳过 owner unlink，released handle 也不得继续派生 `Tensor` | ✅ **已闭 —— PR #1749 merged `919abfdf`（2026-08-09）**。⚠️ 范围核对：这条门原判断需要一整套全新 `release_buffer()` 公共 API ＋ 跨 submit 的 in-flight retain ＋ `buffer_id` 复用/generation bump ——重新逐字核对 gate 措辞后发现是误判：`Buffer.close()`（`python/simpler/buffer.py`）**早已存在**，测试套件里到处在用它做 `create_buffer()` 的释放路径，只是恰好带着这两条门点名的 bug（且两条都 0 测试覆盖）：① `shm.close()` 抛异常时 `unlink()` 被跳过、`self.shm` 也不清空——重试只会在同一处再炸一次，永远够不到 `unlink()`；② `close()` 从不标记「已释放」，`buffer.tensor(...)`/`to_descriptor()` 在 close 之后照常把descriptor 建出来，指向可能已被 unlink 的 backing。**#1749 只修这两条**：`close()` 改成先清 `self.shm` 并置 `closed=True`，再 `try/finally` 保证 `unlink()` 总跑；`to_descriptor()` 加 `closed` 守卫（`tensor()` 内部调它，一处守卫两条派生路径都堵）。⚠️ **`ImportRegistry.unregister`（曾经存在，0 调用点 0 测试）已由 PR #1747 merged `24d8e8fc`（2026-08-08）删除**——它是「import 随 handle 生命周期释放」这条不变量的落地位，但 `release_buffer()` 本身还不存在，留一个未测的空方法比没有更误导。**真正的 `release_buffer()`/in-flight retain/`buffer_id` 复用+generation bump 仍完全未建**（`generation` 全仓每处调用都硬编码 1，`_next_buffer_id()` 从不回收 id）——这是独立、更大、未设计的 P1-B「registry + lifetime」后续工作，不由 #1749 关闭，**接手时不要把 #1749 误读成那整块已经做完** | P1-B（lifetime） |
 | G6 | ~~`create_buffer` 首版若不支持 next-level owner delegation，public scope 必须明确限制为 L2/L3~~ | ✅ **已闭 —— #1703 merged `02f90ef5`**（`_next_level_shms` 参与子节点判定 ＋ 131 行新测试；`create_buffer` 此前零测试）。⚠️ **本 gate 的措辞预设了错误方向，应当重写** —— 实际缺陷不是「借 sub child 误称支持 L4」（放得太宽），而是**卡得太严**：next-level child 就是 fork 出的进程、按名 map `POSIX_SHM`，与 chip/sub child 无异，故子节点全为本地 L3 的 L4 被**误拒** | P1-A（是 bug 而非 scope 决策） |

✅ **G1 ＋ G3 的耦合已解开**（#1704）：曾经的风险是「G1 算得对，但算的可能是假前提」——
POSIX shm 不核对实际 object size ⇒ 「视图不越出 backing」建立在 owner 自报、经 wire 传来的
`nbytes` 上。现在两条都在，`nbytes` 本身也被核对。

✅ **gate 清单全闭 ⇒ P1-A 达成。** 清单是**合入门**，[§9](worker-memory-model/p1b-corrected-design.md)
是**不变量表**，两者不是同一个集合 —— 第 5 条从来不在清单里，走完清单它依然不成立（今天 §9 是 7/8）。
⚠️ **但那不是 P1-A 的欠账**：它归 G2 的 `ImportContext` 半，是 **P1-B 的退出条件**，
因为判定所需的信息不在 wire 上（见上方 P1-A 条的说明）。
**原则（2026-08-05 用户裁决）：遗留往后拆，不让前面的里程碑依赖后面的** —— 否则 P1-A 永远关不掉，
而依赖图是反的。

**另有 P1-A 自身的两条要求**（见下方 §5.6 P1-A 条）：

 | # | 要求 | 状态 |
 | - | ---- | ---- |
 | A1 | 冻结每 Tensor descriptor 大小时保留明确的 TaskArgs blob 上界与 256 accepted / 257 rejected 边界 | ✅ **已闭 —— PR #1704 merged `4d0de8ea`**：并列加了一条用 `sizeof(Tensor)`（144 B）的 `static_assert`，**未替换**现有 `ChipTensor`（128 B）那条 —— 今天 blob 确实装 `ChipTensor`，两条各守各的。实测 256×144＋header＋scalars = **37896 B** vs capacity **64016 B**，余 26120 B ⇒ 是 guard 而非正在发生的溢出。；_（下方为收口前的分析，保留以解释为何需要这条）_ 🟡 **两条断言都已有，但 blob 那条用错了类型**。256/257 边界见 `tests/ut/cpp/types/test_chip_max_tensor_args.cpp`；blob 上界见 `worker_manager.h:167` 的 `static_assert(MAILBOX_ARGS_CAPACITY >= …)` —— **它用 `sizeof(ChipTensor)`（128 B，存储型），而 wire 上是 `Tensor`（144 B）**。今天 blob 确实装 `ChipTensor` ⇒ 该断言对今天是对的；**#1453 cutover 后 blob 改装 `Tensor`，它就不再守着实际尺寸**。⇒ 需并列加一条用 `sizeof(pto::Tensor)` 的（**不替换**现有那条）。；**实测：容量不紧张** —— `MAILBOX_ARGS_CAPACITY ≈ 63.5 KiB`，256 × 144 B ＋ header ＋ scalars ≈ **37.0 KiB，余 ~27 KiB**（对照 `ChipTensor` 的 ≈ 33.0 KiB）⇒ **这是一条 guard，不是一处正在发生的溢出** |
 | A2 | local same-build 边界用 `SIMPLER_BUILD_COMMIT` 拒绝 skew | ✅ **已闭 —— 本条此前的判断有误，2026-08-05 更正**。原文写「全仓没有任何一处比较它」，**不成立**：`python/simpler/task_interface.py:95-133`（#1523 交付）拿 `__build_commit__` 与 `git rev-parse HEAD` 对，不等即 `raise ImportError`。**已实测撞到**（本轮 `reset --hard` 换 HEAD 后 pytest 直接被拦：_built from b535fa2108d9, but this source tree is at 29b3527…_）。再对 `buffer.h` 的原话——它声称的是「**会发生**的那种 skew（旧扩展配新 Python）由它挡住」，而这正是该检查做的事 ⇒ **注释准确，没有引用不存在的防线**。唯一边界是它只在 source-tree 装时跑（wheel 无 `.git`），但 wheel 里 Python 与扩展同出一个包，本就不可能 skew。⇒ **原「(a) 真去比对 / (b) 改注释，必须选一个」的要求撤销** |

收口任务：`tasks/task-p1a-close-merge-gates.md`（本地参考：`tasks/task-p1a-close-merge-gates.md`）。
这些不改变 P1-B registry/lifetime、Attachment 或 wire cutover 仍未交付的边界。

TaskArgs 应携带
`Tensor(descriptor, byte_offset, shape, strides, dtype)`，而不是裸指针和
`child_memory` 布尔值。`Tensor` 可以为严格 endpoint-scoped backend 携带
opaque address-bearing body，但只有通过 address-domain/provenance 校验的消费
endpoint 才能将其解释为地址；其它 backend 的 local pointer 或 remote sidecar
只在消费 endpoint 上 materialize。概念上
`Tensor` 是用户和 wire 共用的逻辑视图，在 Chip endpoint materialize 后才得到
`ChipTensor`。

Public resource API 至少包含返回 `Tensor` 的 shared/child allocation 和
`release_buffer(tensor)`。`create_buffer(nbytes)` 是 control region/arena 等
non-tensor backing 的进阶入口；其 owner-side 返回表示尚未冻结，但不得作为
TaskArgs 或跨 edge handle 直接传播。submit 接受任务后 retain 所有 Tensor，completion
后释放 in-flight reference；用户 release 后只有在引用归零时才回收 backing。
`buffer_id` 重用前必须递增 generation；stale handle、duplicate release、
FAILED/CLOSED 中的 release 和 close-time owner cleanup 都要有确定语义。

owner 必须维护 authoritative `BufferRegistry`，区分 persistent
owner-managed Buffer 与 run/scope-managed BufferLease。generation 只解决
同一 `buffer_id` 复用的 ABA，不能代替 live registry、tombstone 或 importer
invalidation。release transaction 的顺序是：阻止新 retain，等待/defer
in-flight，撤销各 edge attachment，detach/unmap，最后 owner free；只有
native free 成功后才能丢失资源账本。

`buffer_id` 在单个 owner incarnation 内唯一，canonical allocation identity 是
`owner_instance_id + buffer_id + generation`；同一 backing storage 的 view
必须保留这个 identity。bounds 和 overlap 按
`byte_offset/shape/strides/dtype` 计算；dependency key 使用 canonical identity
加 byte footprint，不能使用 materialized VA。overlapping writable alias
必须显式序列化或在 dispatch 前拒绝。

依赖追踪必须使用**完整 canonical identity 的相等性**，hash 只用于 bucket，
不能把截断 hash 当作 identity。range-aware tracker 按最终参数 intent
（INPUT/OUTPUT/INOUT 等）判断 RAW/WAR/WAW；target worker 不是共享 host
backing 的 memory identity，不能靠把 worker_id 塞进 key 来规避 overlap。

`copy_to/copy_from` 使用指向既有 Buffer/Attachment 的 typed
BufferSlice operand 和显式 offset/length；`Transfer` 另带 target
resource/owner，并返回新 Buffer。direct mapping、registered-host fast path 或
shared staging 是 adapter 内部选择。对任意 post-init host tensor，runtime 若
无法建立 attachment，必须显式 staging/Transfer，不能跨 fork 发送 parent raw VA。

这样可以最终消除当前的歧义。PR 1397 已提供 host rewrite/visibility 的局部
保护；[PR 1430][pr-1430] 又在旧 raw-pointer ABI 上加入 process-local kind4
exact-provenance guard，但它们仍不是正式 BufferHandle ABI。#1430 使用
`(worker_id, ptr)` 和 typed allocation/domain role 记录 live exact pointer，覆盖
Worker/Orchestrator malloc、free、copy，CommDomain allocate/release，以及
NEXT_LEVEL single/group dispatch；free/release 在 native 回收前 revoke。

> **更新（2026-07-30，[PR #1557][pr-1557] `68ab1b43`，closes #1537）**：guard 的
> exact-only 语义**已按 API 拆分**，"interior pointer 一律拒绝"不再是笼统契约：
>
> | API | 契约 |
> | --- | ---- |
> | `free` / `release_domain` / kind4 **dispatch** | **exact allocation base**（不变） |
> | `copy_to` / `copy_from`（Worker L2 与 Orchestrator L3 两条路径） | **完整落在某个 live allocation 内的 interior range** |
>
> 实现为 `Worker._child_prov_require_live_range(worker_id, ptr, nbytes, *, api=…)`
> （`python/simpler/worker.py`），按 `base <= ptr && ptr + nbytes <= base + extent`
> 溢出安全判定；`_ChildProvEntry` 增 `malloc_size`，CommDomain allocation id 映射到
> 该 base 上 window/buffer 的 extent，`live_extent()` 取**最宽的 live 角色**。
> wrong-worker / stale / 越界仍然拒绝。

当前边界是：

- local child memory handle 没有 owner、size 和 generation；
- #1430/#1557 能拒绝 wrong-worker、歧义 target、stale pointer、非 base 的
  free/dispatch 和越界 copy；完整落在 live allocation 内的
  `copy_to/copy_from` interior range 合法。该 guard 仍不能阻止 allocator 重用
  同一 VA 后的 strict ABA，也不覆盖 raw C++/private binding bypass、跨
  incarnation 或 wire；
- `create_host_buffer()` 和 SceneTest rehost 已覆盖当前 direct local child
  的 post-fork host-storage 可见性，但仍没有跨 edge 的 generation-safe handle；
- TaskArgs rewrite 已通过 `child_memory` guard 避免 device VA 命中 host
  range，但布尔值仍不能表达正式 address space 和 owner；
- inherited mapping 的真实可见范围可能大于公开契约；
- local 与 remote buffer 使用不同的 ownership 模型。

\#1466 的 exact-handle identity check 已关闭 CommDomain same-name replacement
误删问题；[PR 1541][pr-1541] 又把 pending/live release 收敛为 allocation-id
keyed 的 exact-once backend outcome，并让 source-queue claim 只在 native free
成功后退休，关闭了 failed-release bookkeeping 的矛盾 retry 语义。该组
serialization/wire 问题中，KNOWN_ISSUES.md（本地参考：`../KNOWN_ISSUES.md`） 仍有效的 residual
是 `Tensor` inactive/reserved 字段没有确定初始化却可能进入固定宽度序列化；该
tracker 还记录 A2/A3 host registration 绕过 logical-to-driver id 转换的问题，后者是
P1-C adapter 的前置。#1430 的
revoke-before-free 与 #1541 的 run-finalization transaction 都不表示未来 Buffer
wire 已经安全。

### 2.5 HeapRing 的去留

可以移除 hierarchical HeapRing 的通用 buffer 职责，但不能删除
shared allocation 能力。

当前 hierarchical Ring/heap 的耦合点包括：

- Orchestrator.alloc()；
- null OUTPUT 自动分配；
- L3-L2 message queue 通过 Orchestrator.alloc() 暂存的三个描述符张量；
- Ring 自身承载的 task slot 和 scope lifetime accounting。

L3-L2 queue 的通信区不由 HeapRing backing。simulation 使用独立 POSIX
shared memory，onboard 使用独立 device VMM region；HeapRing 只与上述描述符
暂存存在弱依赖。

Ring 已支持 heap bytes 为零的 slot-only allocation。建议迁移顺序：

1. 拆分 SlotArena 和 SharedBufferArena。
2. 增加 Worker-owned shared buffer allocation。
3. null OUTPUT 改为 fail-fast，要求用户提供 Tensor。
4. 迁移 Orchestrator.alloc()，并把 L3-L2 queue 的三个描述符改用显式
   shared buffer；队列通信区无需迁移。
5. 默认用零 hierarchical heap 跑测试。
6. 所有使用者迁移后删除 heap-coupled scope reclaim。

这项工作不能顺带删除 L2 device-side TMR GM heap。L2 device heap 与
hierarchical host HeapRing 的职责和生命周期不同。

### 2.6 Runtime 基础能力、参考 adapter 与用户自定义的边界

CPU↔NPU 需求包含两条互补数据路径，runtime 必须同时容纳：

- **bulk copy path**：由 Device GM owner 提供有 completion 的
  `copy_to/copy_from`（底层可选 `rtMemcpy` 等），适合大块 payload；调用者不获得
  device VA。
- **direct load/store path**：平台和 allocation backend 证明可行时，为 Host
  endpoint 建立 registered/mapped Attachment，使 CPU 对 Device GM 做小粒度
  load/store，适合 control、doorbell 和 queue metadata。它不能由机型名或
  Worker level 硬编码，也不能因为同机就默认存在。

“任意 CPU 进程、任意 NPU 组件可通信”应解释为：每个 Host process、
AICPU/AICore endpoint 都可凭 validated descriptor、授权和 lease 独立建立
Attachment；不是让它们共享一个裸 VA。暂只要求本机 Host endpoint 与本地 L2
Device GM，以及本机进程间 Host DDR；Remote L2 GM 在 P2 可选择 owner-delegated
copy/proxy（保持原 Buffer identity，但 consumer 不获得可解引用 VA），或显式
Transfer（目标 owner 创建新 Buffer identity）；除非未来 backend 单独证明 remote
direct Attachment。

runtime 应提供：

- Buffer allocation/identity/lifetime、export/import authorization、
  AttachmentRegistry 和 typed copy/Transfer；
- `BufferSliceArena` 与 offset/length slice，使多个逻辑对象安全复用一个 persistent
  Buffer；
- publish/observe/wait/fence、memory-order 和 completion 的最小原语；具体
  cache/coherence 要求由 backend capability 明示；
- `ResourceBundle + AttachmentPlan + TransportPlan`，允许 control 与 payload
  使用不同 backing 和不同 adapter；
- generation、retain/release、detach、failure cleanup 和可观测 data path。

[PR 1592][pr-1592] 已提供 `Worker.committed_device_memory(worker_id)`：
L2 直接查询，L3 定向查询 chip child；统计经 `MemoryAllocator` 提交的 user
tensors、pooled arenas、runtime buffers 和 DFX buffers（A5 覆盖由
[PR 1602][pr-1602] 补齐）。它不包含 HCCL/VMM communication windows，也不是
设备总容量、quota 或完整进程 HBM 统计。P1 persistent Buffer/BufferSliceArena 与
CommDomain window 仍需进入统一预算和可观测面。

runtime 不应把一种 queue layout 或 persistent-kernel 协议冻结进 Buffer ABI。
仓库应提供一个有界 SPSC CPU↔AICPU queue 和一个 control-in-Host-DDR /
payload-in-Device-GM 的 persistent-kernel 示例，演示正确的 lifetime、barrier、
backpressure 和 shutdown；用户或上层库负责自定义 queue schema、ring/index
布局、opcode、batching、MPMC 算法和业务循环。只有被多个上层反复证明通用且
能够独立验收的同步原语，才从示例提升为 runtime API。

## 3. 定向调度与上下级并行

### 3.1 L3 定向调度

当前 main 已由 [PR 1436][pr-1436] 收紧 NEXT_LEVEL 契约：

- single submit 的 worker 必填且必须是 non-negative exact target；
- group submit 的 workers 必填、逐 member 精确映射且禁止重复；
- scheduler 使用 per-worker single FIFO，并以共享 group FIFO 保持
  all-or-nothing group；
- group FIFO head 尚不能完整启动时，预留它的全部 target workers，阻止这些
  worker 上后来 single 越过；无关 worker 继续运行。只有当前 head 预留，
  group 启动后释放，后续 group 不提前预留（[PR 1565][pr-1565]）；
- callable eligible set 和 endpoint kind 继续参与校验，local callable 不会被
  投递给 remote endpoint；
- NEXT_LEVEL 不再自由选 worker，SUB 仍保留自由调度。

初期仍保留 dependency、completion、failure propagation、scope 和
drain。

旧 global ready queue 对无关 worker 的队首阻塞已由 per-worker queue 消除；
group target 上保留的是维护 all-or-nothing 的有意局部 reservation。反序 group
member→worker 映射和 mixed Local/Remote endpoint-kind eligibility 已有回归。
\#1430 的 kind4 provenance guard 仍只负责 memory owner，不替代 callable/
endpoint eligibility。

[PR 1614][pr-1614] 用 overlapping-group scene 固化了“无关 worker 可继续、被
reservation 的 target single 不越过 blocked group head”。[PR 1612][pr-1612]
把 blocked head 留下的 permanently-true level predicate 改为 consumed wake
generation，补齐 completion/idle 等可放置性变化的唤醒，避免 scheduler busy loop
和 lost wake。[PR 1613][pr-1613] 对持续 5 秒的 reservation-stall 结构只报告一次
busy target、idle-but-queued target 和 FIFO head；它不释放 reservation，也不把
该状态误判成 deadlock。

移除 hierarchical TensorMap 是后续决策。[PR 1460][pr-1460] 已提供
run-level `RunHandle`；[PR 1467][pr-1467] 先以 launch acceptance 作为下一次
host graph construction 的 gate，[PR 1540][pr-1540] 再提供 generation-safe
depth-two resource capacity。[PR 1541][pr-1541] 已替换前述 gate：`begin_run()`
在调用 graph callback 前取得 negotiated pipeline lease，capacity 用尽时在 callback
之前阻塞。depth=2 时允许 run N `EXECUTING`、N+1 `BUILDING/PREPARED`，N+2
阻塞；depth=1 时第二个 run 即阻塞。只有 whole-run FIFO head 能执行，故不同 run
的 device effect 不交错。随后 [PR 1583][pr-1583] 为 direct A2/A3 onboard、
depth-two chip endpoint 增加 generation-bound 的两个 task frame：active run 与
prepared FIFO successor 可同时发布、验证，但 successor 只有在 FIFO promotion
后才能 activate。[PR 1587][pr-1587] 又允许 predecessor 与 successor 均无
diagnostics 的 HBG successor
在 predecessor device execution 期间使用 leased inactive bank 完成 native
prepare。successor 的 launch 仍必须等待 predecessor poll + finalize；因此当前有
cross-run frame publication 和 `P(N+1) || S(N)` prepare overlap，没有第二个并发
launch/device execution。TMR、带 diagnostics 的 HBG、prepared group，以及
SUB/nested/remote/A5/simulation/depth-one endpoint 仍不走该 early-prepare path。
RunHandle 仍不表达 task-level
dependency，因此删除 TensorMap 前须提供 task `Event`/`Future` 或
`depends_on`。L2 TMR TensorMap 与 L3 hierarchical TensorMap 必须作为两个
独立子系统处理。

[PR 1558][pr-1558] 是另一个独立的 L2 runtime 调度优化：`sync_start` cohort
在 owning scheduler 的 running+pending capacity 足够时局部提前启动，容量不足
才回退 global drain。它不改变 NEXT_LEVEL placement、whole-run admission 或
上述跨 run device-effect 非交错边界。

### 3.2 Local edge 已有窄 progress path，但另起 Python 线程仍不是通用解

当前 local edge 有两种明确不同的契约：

- **single-frame compatibility path**：SUB、nested/remote Worker、simulation、
  A5 和 depth-one endpoint；TASK 与 CONTROL 复用 base frame，child 调 blocking
  composition。仅把 `Worker.run()` 换到任意 Python 线程仍不能解除 mailbox/锁和
  native ownership 的阻塞。
- **two-frame progress path**：direct A2/A3 onboard、`pipeline_depth >= 2` 的
  chip endpoint；base frame 只承载 startup/control，两个 task frame 分别绑定
  pipeline lease slot。一个 parent `WorkerThread` 非阻塞推进两条 frame lane，一个
  child loop 同时服务 control、两个 frame、一个 active native run 和至多一个
  prepared-but-unlaunched successor。

[PR 1574][pr-1574] 已把 L2 host-runtime 边界拆成
`prepare -> launch -> poll/wait -> finalize`：`launch` 在内部 C++ executor
thread 上执行 native run，并在真实 launch marker 或 marker 前的 terminal boundary
返回；`poll/wait` 观察完成，
`finalize` 负责 validation、copy-back、DFX 和 Runtime 销毁。既有
`simpler_run`/`ChipWorker.run()` 只是这些阶段的 blocking composition。

[PR 1583][pr-1583] 已把这条 seam 接进上述窄 local progress path：task frame
发布只短暂与 control mutex 排序，发布后 ordinary control 可在 device task active
期间处理；frame identity 在 staging、activation、acceptance 和 completion 都绑定
run/slot/generation/dispatch。[PR 1587][pr-1587] 再把 capability-gated HBG native
prepare 接入 inactive bank。它们仍不增加第二个 device execution，内部 executor/
progress thread 也不放宽 Worker init-owner/native teardown 契约。

direct A2/A3 local edge 已按以下形态拆开；其余 edge 仍需复用同一契约：

~~~text
receiver / control / health lane
├── decode 和校验 command
├── copy 或 retain payload
├── 发布 TASK_ACCEPTED
└── enqueue execution

serialized native-run owner / completion lane
├── 持有 NativeRun token，并执行 prepare / launch
├── internal C++ executor attach device context 并执行 native run
├── poll / wait / finalize
└── 发布 TASK_COMPLETED 或 TASK_FAILED
~~~

“control lane 可响应”不等于所有 device mutation 都能与 native run 并发。two-frame
path 已能处理 ordinary control，但 callable prepare/register/unregister 会延迟到
active 和 backend-prepared native state 都 finalize；final unregister 还等待所有
引用该 digest 的已发布 frame 退休。finite control timeout 覆盖这段 deferral，若
到期且 completion 不确定则 poison endpoint。其它 device-touching control 仍须逐项
证明 thread ownership、ordering 和 cleanup 后再加入 admission matrix。

当前 two-frame mailbox 已提供 generation/dispatch identity、accepted/completed、
stale-frame rejection 和有限 control timeout。通用 EdgeChannel 若要扩到其它
transport，仍至少需要：

- 有界 queue 和 backpressure；
- command sequence 和 communication epoch；
- accepted 与 completed 两种状态；
- callable slot pinning 和 unregister tombstone；
- 有限 control deadline；
- cancel、drain、shutdown 和 endpoint failure；
- payload 生命周期覆盖到 completion；
- 独立 health path。

统一的 EdgeChannel 应覆盖：

- L3 到 L2 的 local shared-memory transport；
- L4 到 Local L3；
- L4 到 Remote L3 的 framed transport；
- 现有 L3-L2 mapped region/message queue 数据面。

通用 EdgeChannel 仍是新的跨层抽象，但 direct A2/A3 L3→L2 的窄 task/control
progress lane 已由 #1583/#1587 交付，不能再列作待办。后续每个 PR 只扩一种
endpoint family：先把契约抽离出具体 two-frame chip transport，再分别接 Local L4、
SUB/A5/simulation 与 Remote L3；每一步复用相同的 sequence、backpressure 和
completion 契约，不能一次重写四种 transport。

### 3.3 L2 SceneTest

Standalone L2 SceneTest 保留 root-worker 特例：

- 直接创建 Worker(level=2)；
- 没有上级 mailbox；
- run args 来自 root local memory；
- synchronous run 可以实现为 submit().wait()；
- 内部 executor service 与 child L2 共用。

[PR 1397][pr-1397] 增加了 eager-init 过渡期 rehost adapter：direct-L3
SceneTest 在 `generate_args()` 后，将非空 CPU contiguous、byte-range
non-overlapping tensor 复制进每个 tensor 独立的 `create_host_buffer()`；
non-CPU、non-contiguous 和 overlapping alias 明确拒绝，empty tensor 跳过
分配，chip child 和 sub-only L3 都可使用。

该 adapter 保障 dtype、shape、value 和资源回滚，不替代最终
SharedBuffer/ChildBuffer ABI。singleton dimension 的自由 stride 和
non-overlapping shared-storage alias 属于 P1 API 契约，不阻塞已经合入的
eager core；P1 仍应让用户通过 allocation/backend API 显式选择 shared 或
child-private memory，`Tensor` 只表达已选 Buffer 上的 view。

[PR 1422][pr-1422] 已关闭 duplicate-name rollback 歧义：
`TaskArgsBuilder` 在任何 `_specs`、`_data` 或 `_has_scalar` mutation 前拒绝全局
重复名称，以及会 shadow builder attribute/method 的名称。name-keyed rehost
journal 因此只接收唯一 lookup key；该修复是独立 SceneTest correctness，不改变
P0.2 lifecycle 或最终 BufferHandle ABI。

[PR 1431][pr-1431] 只增加 device-free characterization tests，没有修改
source 或 wire：当前 `make_tensor_arg`/rehost 会丢弃 singleton dimension 的自由
stride 并重新 canonicalize；同一 storage 的 non-overlapping views 会被 rehost
为独立 buffer，并产生不同 raw address。C++ `TensorKey::local_host` 的
dependency-key/canonical-identity 尚未刻画，因此该任务仍是 PARTIAL；最终
stride fidelity 和 backing identity 继续属于 P1 Tensor 契约。

### 3.4 同步与异步契约

公开模型应区分：

- **pipeline admission**：`begin_run()` 已取得 `{slot_id, generation}` lease；这一步
  在 graph callback 前发生，并由 negotiated depth 提供 backpressure；
- **graph close / PREPARED**：图已提交，run-owned resources 已 retain；非 FIFO
  head 仍不能执行；
- **launch acceptance**：本 run 的 dispatch 已被 endpoint 接受启动，或通过
  保守 fallback 到达该 fence；它不是 completion，也不再 gate 下一次 callback；
- **completion**：设备工作和必要 copy 已完成；
- **fence/drain**：之前的 non-blocking 操作已经可观察；
- **release**：所有使用者完成后释放逻辑 handle。

[PR 1574][pr-1574] 在 L2 host-runtime boundary 增加一层 progressable native-run
lifecycle；它与上面的 hierarchical `BUILDING/PREPARED/EXECUTING` 状态不可混用：

- native `prepare` 构造并 bind per-run Runtime，但不跨 device launch fence；
- `launch` 在 backend 发布真实 launch marker 后返回，或在 marker 前 execution 已经
  terminal 时提前返回；marker 路径仍不是 completion，early-terminal 路径则携带该
  execution result；
- `poll/wait` 只观察 execution completion，尚不释放 prepared state；
- `finalize` 在需要时先 wait，再完成 validation、copy-back、DFX 和 Runtime
  destruction。成功的 prepare 即把 cleanup ownership 交给 caller，即使放弃 launch
  或 launch 失败也必须 finalize；
- prepare 返回后 args container 可以复用，但其引用的 Tensor backing 及现有
  keepalive 必须 retain 到 finalize 返回，因为该阶段仍可能 copy-back；未来
  Attachment 接线后，对应 attachment lease 也必须覆盖到 finalize；
- token 由 `{slot_id, lease generation, process-unique run_epoch}` 标识并拒绝 stale、
  wrong-phase 和 cross-Worker 使用。支持 concurrent native prepare、且当前配置无
  diagnostics 的 HBG backend 可以同时持有一个 launched predecessor token 和一个
  位于不同 lease-selected bank 的 prepared-but-unlaunched successor token；其它
  backend 仍只允许一个 unfinished native run。每个 token 只有一个 serialized phase
  owner，successor launch 必须等待 predecessor finalize，completion publication 后
  才能复用 slot/storage。

Worker.run() 保留为同步便利 API；[PR 1459][pr-1459]、[PR 1460][pr-1460] 与
[PR 1466][pr-1466] 已交付 per-run hierarchical state、
`Worker.submit() -> RunHandle` 和 run-owned resource cleanup。[PR 1467][pr-1467]
建立了 launch fence；[PR 1574][pr-1574] 后，local chip backend 在 native
post-enqueue 边界发布显式 marker：两种 onboard arch 共用该路径，A2/A3 有实机
lifecycle ST，A2/A3/A5 simulation 也在两组 kernel thread 创建后发布。缺少该 marker
的 remote/L3 Python endpoint 才保守退化到 completion；A5 onboard 的 common-code
路径不能替代单独硬件验收。[PR 1649][pr-1649] 进一步用 A2/A3 simulation/onboard
SceneTest 直接读取 sticky acceptance word，证明 sim runner 的
`set_task_accepted_state_ctx` publication 实际到达，而不是静默退化为 completion；
这是验收证据，不是新的 fence 语义。[PR 1540][pr-1540] 提供 depth-two
generation-safe slot，[PR 1541][pr-1541] 则把它接入 negotiated whole-run FIFO：
下一 callback 由 free pipeline lease admission，而不是前一 run acceptance；只有
FIFO head 进入 `EXECUTING`。run terminal 会恰好一次释放 reservation/lease，graph
failure 会 poison/consume 未启动 slot；run-owned remote buffer、CommDomain、
L3-L2 region/SHM 的 finalization 以单调 cursor 收口，模糊 cleanup boundary 会
sticky-poison Worker 并把 keepalive 保留到整树 teardown。blocking queue 操作必须
有有限 timeout，try 操作在普通 no-progress 情况下不 poison endpoint。

\#1541 同时冻结 direct-control ordering：callback 内的 `malloc/copy/domain/region/
remote-buffer` control 属于当前 run，须先等该 run 成为 FIFO head，并且必须发生在
本 run 第一个 `submit_*()` 之前；callback 外的 control 在整次调用期间独占 submit
serializer，并在仍有 live run 时拒绝。产生 device-touching cleanup 的 run 还会让
successor 等到 cleanup publication，局部退化为 depth=1。后续 Buffer API 与
Domain/Worker 驱动的 attachment establishment
必须复用这条 admission seam，不能另从 mailbox 绕过 whole-run ordering。

[PR 1609][pr-1609] 还把 SDMA 从“编译到某架构就天然可用”收敛成 construction-time
Worker capability：A2/A3 虽编译 backend，`enable_sdma` 默认仍为 false，只有显式
opt-in 才 provision worker-lifetime SDMA workspace/streams；A5 当前 capability
absent。它是异步 completion transport capability，不等同于通用 `rtMemcpy`，也不能
仅由 arch 名称推导 actual capability。

[PR 1521][pr-1521] 将 `CallConfig.aicpu_thread_num=0` 冻结为 per-run auto
sentinel：A2/A3 architecture default 为 4，A5 为 5；onboard 根据 probed usable
topology 收敛且至少需要一个 orchestration 加一个 scheduler thread，simulation
直接使用 architecture default。显式值继续受平台上限约束。effective thread count
是 run launch configuration，不是 endpoint identity 或通信 engine capability。

## 4. L4 与全局通信域

### 4.1 HCCL 不会发现 Remote L3

L3 是 simpler 控制面的对象，HCCL 初始化的是实际 device rank。

主路径采用 mpirun + 原生 ACL/PTO 通信，当前 A3 实现用 MPI descriptor exchange + ACL/Fabric V2。
以下仅描述**选择 HCCL collective adapter 时**的 bootstrap，不是所有 L4 域的必选步骤。

HcclCommInitRootInfo 的业务流程是：

1. 一个真实 device rank 调 HcclGetRootInfo。
2. 业务控制面将相同的 opaque RootInfo 广播给所有 rank。
3. 每个 rank 使用相同 nranks 和 RootInfo，并传入自己的 global rank。
4. HCCL 根据 RootInfo 中的 device/network 信息建立 rank 连接。

因此：

- L4 负责发现 L3 service；
- L4 负责选择真实 root L2 和广播 RootInfo；
- L3 负责向本机 L2 fanout；
- HCCL 不连接 L3 daemon，也不识别 L3 worker。

若扩展跨机 HCCL collective adapter，rootinfo_path API 可抽象为：

~~~text
comm_get_root_info() -> opaque bytes
comm_init_from_root_info(nranks, root_info_bytes, rank)
~~~

这里的参数顺序刻意与官方
HcclCommInitRootInfo(nRanks, rootInfo, rank, comm) 对齐；comm handle 由封装返回。

本地文件 adapter 可以保留用于单机测试，但正式路径不能依赖共享
/tmp。

### 4.2 L4 DomainCoordinator

L4 应维护：

~~~text
EndpointId       = session_id + worker_path + deployment
DomainId         = stable id + generation/epoch
MemberSet        = ordered logical EndpointId set
ResourceBundleSpec = resource_name -> requested Buffer | communicator | adapter resource
AttachmentPlan   = (member EndpointId, CanonicalBufferIdentity)
                   -> requested capabilities + allowed materialization/data-path policy
TransportPlan    = operation -> selected adapter
DeviceRankMap    = collective-capable device EndpointId -> global rank  # optional

AttachmentRegistry = (member EndpointId, CanonicalBufferIdentity)
                     -> committed live Attachment(materialization,
                        granted capabilities, lease/state)
ResourceBundle   = committed Buffer | communicator | adapter state
DomainRegionSet  = ResourceBundle 中按 owner 组织的 Buffer-backed entry 子集
~~~

`CommDomainSpec` 是 `MemberSet + ResourceBundleSpec + AttachmentPlan +
TransportPlan` 以及可选 `DeviceRankMap` 的冻结请求；committed `CommDomain`
还拥有 `ResourceBundle + AttachmentRegistry + adapter state`。Plan 表达 requested/
potential policy，Registry 才表达 actual、已 materialize 且持有 lease 的 live
Attachment，不能让一个 “plan entry” 同时冒充 PREPARE 前请求和 COMMIT 后资源。
`DomainRegionSet` 只是
`ResourceBundle` 中 Buffer-backed entry 的子集，不与 ResourceBundle 同义。
跨文档使用时以这个 containment 为准，不能在 CommDomain 公式中用
`DomainRegionSet` 替换 `ResourceBundle`。
CommDomain 不等于 HCCL communicator，也不要求所有 member 直接访问同一块物理
内存。HCCL communicator 只是 device-collective adapter；host attachment、
copy、peer access 和 explicit transfer 可以在同一 logical domain 中使用不同
adapter 和不同 Buffer。

Domain 创建使用事务：

1. 收集每个 READY endpoint 的 deployment、allocation/import/copy 和 transport
   capability。
2. 校验 device lease、物理拓扑和 requested operations。
3. 冻结有序 logical membership，并分配新的 epoch。
4. 向 owner Worker 发送 PREPARE，创建各自负责的 ResourceBundle resources。
5. 按冻结的 AttachmentPlan 为每个
   `(member EndpointId, CanonicalBufferIdentity)` pair 建立 provisional
   materialization/lease；不能只用 owner-local `buffer_id`，否则跨 owner 会碰撞，
   generation 复用也会产生 ABA。
6. 为每类操作选择显式、可观察的 TransportPlan。
7. 仅在需要 device collective 时分配 DeviceRankMap、分发 RootInfo 并创建
   communicator。
8. 所有资源、attachment 和 adapter READY 后 COMMIT，并一次发布 committed
   ResourceBundle/AttachmentRegistry。
9. 任意失败则 ABORT，撤销 attachment/lease，并由 owner 回收 allocation。

membership 变化创建新的 communicator epoch，不对现有 domain 隐式
hot-add rank。

以下 global-rank/sub-communicator 规则只约束 device-collective adapter，
不约束 CommDomain 中的 host attachment、copy adapter 或 shared Buffer/Attachment。
可以在 L4 初始化时创建覆盖全部选中 L2 的 global base device
communicator。目标 CANN 版本支持时，用户 subset 可以通过
HcclCreateSubCommConfig 派生；否则 simpler 创建独立的 versioned
subdomain。

HcclCreateSubCommConfig 只能从 global communicator 拆分，生成的
sub-communicator 不能再次拆分。因此所有 subset 都必须直接从 global base
派生，不能把 L4 到 L3 到 L2 的控制层级映射成嵌套 HCCL sub-communicator。

### 4.3 Host、AICPU、AICore 不是对等 HCCL rank

HCOMM 把 CPU、AICPU、AIV 等建模为 CommEngine 和通信资源，不是三个
独立 rank。engine/protocol 能力随 CANN 版本和产品变化，以下只作为有版本
边界的证据快照：

- CANN 商用版 8.5 的 CommEngine 文档列出 A2 的 CPU_TS 和 A3 的
  AICPU_TS；
- hcomm@81ad19403b4ab6f4e7c8ce55be5a273c8dccbbdf 的
  HcclChannelAcquire 文档列出 A2 CPU_TS、A3 AICPU_TS，以及 Ascend 950
  的 CPU、AICPU_TS、AIV、CCU 对应协议矩阵；
- 部署时仍须按已安装 CANN tag 做 capability negotiation，不能把上述快照
  当作跨版本、跨代际不变量。

Simpler 应暴露一个逻辑域，由资源、attachment 和 transport 共同 materialize：

~~~text
LogicalCommDomain
├── MemberSet
├── ResourceBundle
│   ├── DomainRegionSet（Buffer-backed regions）
│   ├── communicator
│   └── adapter state
├── AttachmentPlan
└── TransportPlan
    ├── HCCL collective adapter
    ├── direct-map / device-peer adapter
    ├── owner-delegated copy adapter
    └── explicit transfer adapter
~~~

CommDomain 不要求所有成员直接访问同一块 physical allocation。典型
CPU↔NPU ResourceBundle 可以包含可 host-map 的 control Buffer 和仅
device-local/VMM 的 payload Buffer；每个 member 对每个 Buffer 拥有独立
capability。CPU、AICPU、AICore 是 deployment/capability view，不自动成为
独立 HCCL rank。

这些 capability 先作为逻辑扩展点，不提前实现空壳。只有对应硬件 adapter
完成功能和硬件验收后才 materialize；其余能力通过 capability-absent 明确拒绝。

API 可以隐藏 peer 属于 L3 还是直接寻址的 L2，但不能隐藏操作是否受
当前硬件支持。

L4 到 L2 的直接通信应解释为：

- 物理 Device GM allocation 由对应 L2 Worker 持有，L3 管理本机
  device/child lifecycle，L4/DomainCoordinator 只持 lease；
- L3/L4 可以获得本地 direct Attachment，也可以只获得向 owner 委派
  `COPY_TO/COPY_FROM` 的 capability；后者不暴露 device pointer；
- Attachment 成功时复用同一 Buffer identity，硬件支持时数据面可以绕过 L3；
- 没有 Attachment 时普通 submit 明确失败；显式 transfer/proxy 在目标 owner
  创建新的 Buffer identity，不能伪装成同一 backing。

fallback 不能静默发生：domain/profile 创建结果必须暴露最终选择的 data-path
capability，用户也可以要求 direct-only 并在不满足时 fail-fast。

### 4.4 跨机数据面的能力边界

legacy VMM IPC shareable-handle backend 是 host-local，不能仅通过 socket
发送该 handle 就变成跨机 backend。[PR 1424][pr-1424] 已为 A3 当前的 L3
base/domain comm window 增加完整、versioned Fabric V2 handle exchange，并保留
legacy VMM fallback 与显式 mapping release；但该实现范围仍是 single-host
multi-card，没有交付 Remote L3/L4 的跨机 launcher、control plane 或端到端数据面。

平台实现必须 capability-gated：

- A3 cross-server 在 #1424 的 Fabric V2 基础上补 launcher/control plane，并做
  端到端硬件 PoC；
- A3 HCCS/RoCE 使用受支持的 HCCL/HCOMM resource；
- A5/950 UB/URMA 使用 endpoint、channel 和 memory registration；
- 当前不承诺 Host/L4 直接 attach Remote L3 GM。不支持 remote direct
  attachment 时，必须显式选择 owner-delegated copy/proxy（由 owner 读写已有
  Buffer，保持 identity，但 consumer 不获得可解引用 VA）或 Transfer/staging
  （目标 owner 创建新 Buffer identity，transport 搬运内容，completion 后发布）；
  不得把 socket 转发的 local handle、owner-delegated proxy 或内容副本描述成原
  Buffer 的 remote direct/零拷贝 Attachment；
- 实现必须绑定部署的 CANN tag，不能假设 master API 与已安装
  headers/runtime 一致。

HcclCreateSubCommConfig 只负责从 global communicator 派生 device subset，
且不支持嵌套拆分；它不是跨机 window backend。跨机资源仍须直接基于 L4
冻结的 flat global rank map 建立。

当前 CommContext 固定最多 64 rank。超过四台 16 卡 L3 时，需要
dynamic 或 hierarchical device context，并同步评审 PTO-ISA ABI。

### 4.5 通信验收矩阵

至少覆盖：

- 同一 IP 上两个 L3 daemon，使用不同 service/HCCL port range；
- 两机各两卡建立一个 global communicator；
- subset domain 的有序 rank 稳定；
- 一个 rank init 失败后的全局 rollback；
- rank 在 PREPARE 和 COMMIT 后分别退出；
- domain 跨多次 Worker.run 复用；
- 超过 64 个逻辑成员；
- A3 HCCS 和 A5 UB hardware profile；
- 同一 Host Buffer 在多个 local endpoint 的 verified Attachment；
- Host 到 L2 的 direct load/store 与 owner-delegated `copy_to/copy_from`
  两条路径都可选择且可观察；
- 不可 attach 时显式 `transfer`，目标端获得新的 Buffer identity，而不是把
  copy 描述为同一 Buffer 的零拷贝复用；
- 一个 CommDomain 的 `ResourceBundle` 同时包含 Host control Buffer 与
  Device GM payload Buffer；
- Host 到 L2 direct path 与 owner-delegated proxy path。

## 5. Issue/PR 关联与交付路线

### 5.1 全量扫描

截至 2026-07-22 的复核快照，仓库中共有：

- 224 个 Issue：16 open、208 closed；
- 1222 个 PR：19 open、1007 merged、196 closed-unmerged；
- 其中 3 个 open PR 是 draft。

Issue 和 PR 分别按全部状态查询，避免 GitHub 将 PR 混入 Issue。
检索了 worker、memory、scheduler、mailbox、L3/L4、remote、HCCL、
HCOMM、domain 和 concurrency 等关键词，并人工审阅候选的标题和 body。

以上数量及下节 open 分类是 **2026-07-22 的历史快照**，不表示当前 GitHub
状态；当前代码交付以本文顶部基线、§5.3 和 §5.5 为准。

### 5.2 历史快照中的直接相关开放项

 | Item | 关联 | 建议 |
 | ---- | ---- | ---- |
 | [PR 1003][pr-1003] | post-READY child-exit hang | 后续已由 bdf6feda 对应修复进入 main |
 | [PR 1090][pr-1090] | async run/register lane | 作为 EdgeChannel 执行基础 |
 | [PR 1379][pr-1379] | concurrent Worker.run | 与 PR 1090 统一后再合入 |
 | [PR 1436][pr-1436] | required exact NEXT_LEVEL placement + per-worker queue | 后续已由 fee284e8 连同 mixed local/remote 与反序 group-mapping 回归进入 main |
 | [Issue 824][issue-824] | persistent CommDomain | Global L4 domain 前置 |
 | [Issue 1105][issue-1105] | endpoint requeue hang | 与定向 queue 一起修 |
 | [PR 711][pr-711] | gRPC 控制面 + RXE/verbs 数据面 | 合流，避免形成第二套 remote stack |
 | [PR 866][pr-866] | Remote L3 monolith | 保留缺口后关闭 |
 | [PR 755][pr-755] | multichannel MPMC 设计 | 提取 lane 语义 |
 | [PR 803][pr-803] | C ABI + a2a3 onboard host-device comm | 核对 multi-lane 等功能差集，非整体 superseded |
 | [PR 861][pr-861] | host-device mapped-region 原语 | 核对与已合并栈的功能差集，保留独有缺口 |

[PR 1381][pr-1381] 已关闭且未合入；其 eager/full-tree READY 主体已由
[PR 1397][pr-1397] 交付，不再作为实现基线。独有 hardening 缺口统一进入
[P0.2][p0-2]；其中 narrowed lifecycle/terminal-close 子集已由
[PR 1398][pr-1398] 吸收，#1398 当时 deferred 的结构性缺口后来由
\#1663/#1669/#1671/#1672/#1673/#1689 全部交付；不从旧分支继续演进。

全部开放 Issue 分类：

- 直接相关：824、1105。
- 邻近但主要属于 L2 TMR 或性能：
  545、984、1375、1376、1388、1425。
- 与本路线图无直接关系：
  905、995、1209、1299、1315、1386、1428、1429。

全部开放 PR 分类：

- 直接相关：
  711、755、803、861、866、1003、1090、1379、1436。
- backend、性能或 DFX 邻近：
  906、1137、1238、1243、1377、1441、1444、1446。
- 与本路线图无直接关系：
  1309、1447。

### 5.3 已合入的基础

后续应复用以下 merged 工作：

- Hierarchical Worker 和递归组合：
  PR 444、578、579、583。
- 本地直接下级 readiness、有界启动失败和 pre-Scheduler rollback：
  [PR 1396][pr-1396]。
- Eager recursive `Worker.init()`、local full-tree READY、initial
  prepare/prewarm-before-READY、lazy-trigger removal、process-group rollback、
  direct-L3 SceneTest rehost 和 `child_memory` rewrite guard：
  [PR 1397][pr-1397]。交付口径见 [done-P0.1][done-p0-1]。
- 单一五态 Worker lifecycle、L2 init epoch parity、原始 startup failure 和
  final READY boundary、terminal CLOSED、exact-attempt close completion、
  leased-operation drain、init-owner native teardown、native init GIL release
  以及 startup-only per-kind eligibility：[PR 1398][pr-1398]。剩余结构性
  hardening 见 [P0.2][p0-2]。
- Callable mutation admission 与 sticky shutdown：[PR 1663][pr-1663]、
  [PR 1671][pr-1671]；device-free fake-chip harness 与 post-init dynamic
  eligibility：[PR 1669][pr-1669]、[PR 1672][pr-1672]；cooperative init
  cancellation、shared close completion、deterministic SHM naming 和首版
  retryable `CleanupJournal`：[PR 1673][pr-1673]。
- 单一 journal-driven teardown sequence、七类 post-success retry action、
  unreaped child/mailbox retention、root-visible nested-SHM reclaim、topology
  freeze 与 public ChipWorker owner gate：[PR 1689][pr-1689]。
- Simulation Remote L3 的 post-fork session open/endpoint attach、递减
  remaining-duration propagation 和 remote-local monotonic reconstruction：
  [PR 1420][pr-1420]。完整 P0.3 交付链见 §5.5。
- Remote numeric timeout 的真实入口 preflight：parent 只在存在 remote spec 时于
  hierarchical resource/fork/socket 前验证，daemon 于 pipe/tempfile/runner 前验证：
  [PR 1426][pr-1426]。
- Remote daemon per-connection exception isolation、reply send/reaper handoff
  失败时的 runner 单次回收、control-exception propagation 和可停止 accept loop：
  [PR 1427][pr-1427]。
- Remote startup 的 single-root hard deadline、numeric endpoint preflight、
  daemon→runner 同机 absolute deadline、最终 READY gate，以及 native
  attach/runtime timeout separation：[PR 1432][pr-1432]。
- `TaskArgsBuilder` 全局唯一、non-shadowing argument name preflight，以及
  duplicate-name rehost rollback correctness：[PR 1422][pr-1422]。
- kind4 typed `(worker_id, ptr)` provenance、exact-base dispatch/free、
  CommDomain role tracking 和 revoke-before-native-free：[PR 1430][pr-1430]；
  allocation-contained copy range 由 [PR 1557][pr-1557] 补齐。这是 raw-pointer
  ABI 上的过渡保护，不提供 generation、strict ABA 或跨 incarnation/wire 安全。
- SceneTest Python rehost/`make_tensor_arg` 的 singleton-stride normalization
  和 shared-storage split characterization：[PR 1431][pr-1431]。该 PR 是
  test-only，未覆盖 C++ dependency key，也没有改变旧 ABI 行为。
- required exact NEXT_LEVEL placement、per-worker single FIFO、共享
  all-or-nothing group FIFO、local/remote endpoint compatibility 和反序
  group mapping 回归：[PR 1436][pr-1436]；blocked group-head target
  reservation 由 [PR 1565][pr-1565] 补齐。[PR 1614][pr-1614] 增加 overlapping
  group hardware scene，[PR 1612][pr-1612] 用 consumed wake generation 消除
  blocked-head busy loop/lost wake，[PR 1613][pr-1613] 增加不改变 policy 的
  reservation-stall 诊断。
- TASK_DONE/CONTROL_DONE 等待期间的 local child-exit liveness：
  [PR 1003][pr-1003]；该修复不改变 startup READY 协议。
- per-run hierarchical state/TensorMap namespace、public
  `Worker.submit() -> RunHandle` 和 run-owned resource cleanup：
  [PR 1459][pr-1459]、[PR 1460][pr-1460]、[PR 1466][pr-1466]；launch fence 由
  [PR 1467][pr-1467] 交付，generation-safe depth-two resource capacity 由
  [PR 1540][pr-1540] 交付。[PR 1541][pr-1541] 已将 child-published
  `pipeline_depth` 的最小值接入 whole-run FIFO admission，并补齐 run、remote
  buffer、CommDomain 与 L3-L2 SHM 的 interruption-safe/exact-once finalization。
  depth=2 可有一个 `EXECUTING` 与一个 `BUILDING/PREPARED` run，但只有 FIFO head
  获得 launch 权限。
- L2 host-runtime 的 progressable native-run ownership seam：
  `prepare -> launch -> poll/wait -> finalize`、run-owned Runtime/DFX identity、sticky
  launch wakeup、host-build dep-gen state handoff、stale slot/generation/epoch rejection，
  以及 blocking `simpler_run`/`ChipWorker.run()` compatibility composition：
  [PR 1574][pr-1574]。[PR 1583][pr-1583] 随后为 direct A2/A3 onboard depth-two
  chip child 交付 generation-safe two-frame progress/control path；[PR 1587][pr-1587]
  在该 path 上交付 predecessor/successor 均无 diagnostics 时的 HBG successor
  inactive-bank native prepare
  overlap。它们允许一个 launched token 与一个 prepared-but-unlaunched token，
  但 launch/device execution 仍串行；其它 endpoint 保持 single-frame compatibility。
  [PR 1649][pr-1649] 补齐 simulation sticky launch-acceptance publication 回归。
  #1574 同时把未 adopt 的 L3 host-mapping cleanup diagnostic 按 Worker owner token 隔离，并用
  peek/publish/ack 两阶段传递；这是诊断归属与 interruption safety，不是 P0.2 的递归
  retryable teardown journal。
- `sync_start` cohort 的 local-owner fast path 与 capacity 不足时的 global-drain
  fallback：[PR 1558][pr-1558]；这是 L2 runtime 内部优化，不改变 hierarchical
  placement 或 run admission。
- `Worker.committed_device_memory(worker_id)` 的 MemoryAllocator committed-byte
  查询：[PR 1592][pr-1592]；A5 DFX allocations 的计数覆盖由
  [PR 1602][pr-1602] 补齐。HCCL/VMM communication windows 仍不在统计中。
- ACL/rt logical device id 到 direct-HAL driver-visible id 的边界与
  `ASCEND_RT_VISIBLE_DEVICES` 隔离支持：[PR 1600][pr-1600]。转换只属于 raw
  HAL/driver adapter，不改变 public Worker id 或 canonical resource identity。
- A2/A3 SDMA 的显式 Worker capability/default-off provisioning 与 marker 驱动
  CI isolation：[PR 1609][pr-1609]。backend compiled 不表示 workspace 已启用。
- `CallConfig.aicpu_thread_num=0` 的 architecture-aware auto resolution：A2/A3
  default 4、A5 default 5，并由 onboard usable topology 收敛；simulation 直接用
  architecture default：[PR 1521][pr-1521]。这是 per-run launch config，不是
  endpoint identity/capability。
- Ascend950PR_9579 x86 + OCCUPY `0x3e` 的窄 AICPU topology fallback：
  [PR 1552][pr-1552]。这是已验证组合的 fallback，不是通用 950 topology 规则。
- HCCL、simulation、multi-domain、dynamic-domain 和 VMM：
  PR 592、597、752、774、817、1334。
- A3 L3 base/domain comm window 的 versioned Fabric V2 handle exchange、legacy
  VMM fallback 和显式 mapping release：[PR 1424][pr-1424]；该基础不等于
  Remote L3/L4 跨机链路已完成。
- A5 URMA deferred-completion backend 与对应演示：[PR 1392][pr-1392]、
  [PR 1421][pr-1421]；实现存在但 device path 仍受 capability/build gate
  约束，不能据此宣称目标部署可执行，更不等于 production Remote L3 transport
  或 global domain 已交付。
- Remote L3 endpoint、wire、scheduling 和 simulation session：
  PR 1006 至 1011。
- L3-L2 orchestration communication 和 message queue：
  PR 1015、1130、1187、1236、1277、1305。
- 显式 allocation 和 post-fork host buffer：
  PR 485、560、1256、1390。

[Issue 303][issue-303] 虽名为 L1-L4 分布式多卡支持，但关闭它的
[PR 307][pr-307] 实际交付的是本机双卡 L3 allreduce 示例，不能视为
Remote L4 或跨 L3 CommDomain 已完成。

### 5.4 建议新增的 tracking Issue/RFC

现有 Issue/PR 没有完整覆盖以下契约，建议分别跟踪：

1. **Post-eager lifecycle hardening follow-up**
   - #1398 已交付 authoritative state/CLOSED；#1663/#1671 已交付 admission，
     #1669/#1672 已交付 dynamic eligibility，#1673 已交付 cooperative init cancel
     和首版 retryable journal。follow-up 只追踪 recursive nested-SHM
     scan-to-signal、survivor journal 顺序/覆盖、完整 resource actions、public
     ChipWorker ownership、child freeze 和两条 teardown sequence 的归并。
2. **Simulation Remote L3 startup correctness（DONE）**
   - #1420 已交付 post-fork activation 与 `startup_remaining_s` duration
     snapshot，#1426 已交付 parent/daemon numeric timeout real-entry preflight；
     #1427 已交付 daemon per-connection reply-failure containment，#1432 已交付
     end-to-end absolute deadline 与 native startup/runtime timeout split；#1692 已
     交付 full-init rollback/零残留 evidence。ACK/lease/onboard 仍由 production
     Remote L3 transaction 独立跟踪。
3. **Local Buffer–Attachment–Tensor foundation**
   - #1430/#1557 已交付旧 raw-pointer ABI 的 owner/range guard；
     ✅ [PR 1599][pr-1599] 的 target ABI/codec/`create_buffer` **已合入本基线**
     （`b535fa21`），并已采用 `BufferDescriptor`/`Tensor`/`ChipTensor` 词汇；
     ⚠️ 但它**未**把地址无关 `Tensor` 接入 TaskArgs/dispatch，
     且**合入时其实现级 merge gate 一条未闭**（见 §2.4 的 G1–G6 表；#1703 已交付 G1）。
     [PR 1453][pr-1453] 是后续 cutover 设计输入。**「ABI 已合入」不能据此宣称** Attachment、
     retain/release、generation reuse、direct-map、显式 Transfer、range dependency 或
     BufferSliceArena 已交付。该 RFC 只做 local Host DDR + Device GM 的 T1/T2 基础；Remote GM
     direct Attachment 不在本阶段范围。
4. **Responsive EdgeChannel**
   - #1583/#1587 已交付 direct A2/A3 onboard depth-two local-chip 子集；follow-up
     只追踪 transport-neutral contract 与 SUB、nested/Local L4、A5、simulation、
     remote 扩展，以及 multi-flight cancel/health，而不是重做窄 local lane。
5. **Production Remote L3 transaction**
   - ACK/cleanup-unconfirmed、lease/heartbeat、session epoch、daemon journal
     和 partition recovery。
6. **Global Multi-L3 CommDomain**
   - global rank、RootInfo、epoch、persistent domain、remote controls。
     本基线不含 [PR 1456][pr-1456] 的 mixed Local/Remote L4 CommDomain
     candidate；它不能替代 production ACK/lease/session transaction 的退出条件。
7. **Heterogeneous domain views and adapters**
   - Host/AICPU/AIV capability、multi-buffer ResourceBundle、direct/copy/Transfer
     adapter、persistent Buffer/Attachment primitive 和 rank capacity。

### 5.4b 两个维度：P0–P3 是交付里程碑，不是排期单位

> ⭐ **本节 2026-09-08 独立成小节。** 它原本直接跟在 §5.4 的第 7 条后面、没有自己的标题，读起来像是「建议新增的 tracking issue」的一部分 —— 而它governs 的是 §5.5–§5.8。
>
> ⭐ **P0–P3 是交付里程碑，不是排期单位（2026-09-07 重组）。**
>
> 本节四段（§5.5–§5.8）回答的是「**用户已经能做什么、还缺哪项证据**」——
> 每段的**退出条件仍然是权威的**，也仍然有序（P1 给本地基础、P2 给分布式保障、
> P3 验收组合后的用户能力）。
>
> ⛔ **但「谁负责实现、什么挡着什么」不在这里** —— 那是 **`.docs/work-items.md`** 的
> 三条实施线（`RA` 资源与访问 · `DS` 分布式会话与通信域 · `EX` 执行与常驻运行），
> **依赖写在具体工作项之间，不写在阶段之间**。
>
> 这条区分是被观测逼出来的，两个例子：
>
> - 看板曾写「**P3 被 P1 ＋ P2 双重前置**」，而 **PR #2096 已经在实现 P3 的第 ④ 项**；
> - 看板曾写「**P2-D 被 P2-C 挡住**」，而 **PR #2120 已经把 P2-D 的本地那半做了**。
>
> ⇒ **两次都是把「阶段序」当成了「工作项序」。** 阶段序描述的是用户能力的累积顺序，
> 它**不蕴含**每个工作项都要等上一阶段全部完成。
>
> ⚠️ **尤其是 P3：它的用户模型必须从一开始参与设计。** 若等 P1、P2 全部完成再设计 P3，
> 前面会各自做出本地接口与远端接口，最后由 P3 承担一次统一改造 —— 而总设计的目标
> 恰恰是 §5.8 描述的那个最终使用方式，三段应当**共用同一套契约**。

### 5.5 P0：eager core 已交付，分开推进 correctness

阶段状态：

- **P0.1 DONE**：eager-init core 已由 [PR 1397][pr-1397] 合入。
- **P0.2 DONE**：narrowed lifecycle/terminal-close slice 已由
  [PR 1398][pr-1398] 合入；admission 由 [PR 1663][pr-1663]/[PR 1671][pr-1671]
  收口，device-free harness 与 dynamic eligibility 由 [PR 1669][pr-1669]/
  [PR 1672][pr-1672] 交付，cooperative init cancellation 与首版 retryable
  `CleanupJournal` 由 [PR 1673][pr-1673] 交付；recursive nested-SHM
  scan/reclaim、survivor journal 顺序与覆盖、七类资源清理、单一 teardown
  sequence、topology freeze 与 public ChipWorker owner gate 由
  [PR 1689][pr-1689] 闭合。详细验收记录见 [P0.2 follow-up][p0-2] 与
  [.docs 状态看板](README.md)。
  #1398 的交付契约见 [done-P0.2][done-p0-2]；
  [P0.2 follow-up][p0-2] 保留后续批次的设计与验收历史。
  review design §0（本地参考：`review/p0.2-design.md`） 是交付切片的 companion，后续章节保留
  deferred/历史设计。修改已交付契约时同步 done-P0.2、follow-up 交付记录与
  design §0；修改 state/API matrix、journal、deadline、failure precedence 或验收
  测试时，同步 P0.2 与 design 中对应章节。
  代码引用以函数/符号名为主，固定行号只能标作某一 commit 的定位提示。
- **P0.3 DONE**：[PR 1420][pr-1420] 已交付 post-fork remote activation 和
  remaining-duration propagation，[PR 1426][pr-1426] 已交付 numeric timeout 的
  parent/daemon real-entry preflight，[PR 1427][pr-1427] 已交付 daemon
  per-connection failure containment，[PR 1432][pr-1432] 已交付 hard-deadline 与
  native timeout separation，[PR 1692][pr-1692] 已交付 full-init rollback/零残留
  evidence，并保留 startup-budget `TimeoutError` 分类。

已进入 `upstream/main`（**基线以本文顶部为准**，不在此重复 SHA —— 两处记同一事实必漂移；
下列各项自 `c866c827` 起即已在 main 上，之后未回退）：

- [PR 1396][pr-1396] 的 direct-edge READY/FAILED、deadline/liveness 和
  pre-Scheduler rollback；
- [PR 1397][pr-1397] 的 eager recursive init、full-tree READY、
  pre-READY Chip prewarm、五个 lazy-trigger removal、process-group
  cancellation backstop、SceneTest rehost 与 `child_memory` guard；
- [PR 1398][pr-1398] 的单一五态 lifecycle、L2 epoch parity、原始 startup
  failure/final READY boundary、terminal CLOSED、exact-attempt close
  completion、leased-operation drain、init-owner teardown、native init GIL
  release 和 startup-only per-kind eligibility；
- [PR 1663][pr-1663]/[PR 1671][pr-1671] 的 register/unregister admission
  linearization 与 sticky shutdown；[PR 1669][pr-1669]/[PR 1672][pr-1672]
  的 fake-chip harness 与 post-init eligibility；[PR 1673][pr-1673] 的
  cooperative init cancellation、shared close completion、deterministic SHM
  naming 与首版 retryable cleanup journal；
- [PR 1689][pr-1689] 的单一 journal-driven Worker-tree teardown、七类
  post-success retry action、unreaped child/mailbox retention、root-visible exact
  nested-SHM namespace reclaim、parent/child topology freeze 和 public
  `ChipWorker` init-thread ownership；
- [PR 1648][pr-1648] 的 256 ChipTensor 参数容量边界，以及
  [PR 1676][pr-1676] 的 Worker/Chip/Core 分层软件命名规范与
  [PR 1681][pr-1681] 的全仓代码符号迁移；[PR 1683][pr-1683] 的 device enqueue
  与 completion drain 拆分；
- [PR 1420][pr-1420] 的 simulation remote post-fork open/attach、递减
  remaining-duration manifest、remote-local monotonic reconstruction 和
  compatibility fallback；
- [PR 1426][pr-1426] 的 actual-remote-gated parent numeric timeout preflight、
  daemon pre-resource validation 与 runner defense-in-depth validation；
- [PR 1427][pr-1427] 的 daemon per-connection exception isolation、local-send
  ownership transaction、exactly-once runner disposition 和 stoppable accept loop；
- [PR 1432][pr-1432] 的 single-root hard deadline、numeric endpoint contract、
  same-host absolute deadline handoff、最终 READY gate、native attach/runtime
  timeout split 和 deadline-bounded nonblocking framed I/O；
- [PR 1692][pr-1692] 的真实 L4/local SUB/remote L3 failure injection，覆盖 remote
  open timeout、later-remote failure 与 endpoint attach failure，并验证精确
  PID/PGID/SHM/session 零残留、daemon 可复用和 startup-budget `TimeoutError`
  分类；
- [PR 1003][pr-1003] 的 TASK_DONE/CONTROL_DONE post-READY child-exit
  liveness；
- [PR 1436][pr-1436] 的 exact NEXT_LEVEL placement、per-worker FIFO、
  shared group FIFO、mixed local/remote eligibility 与反序 group mapping 回归，
  [PR 1565][pr-1565] 的 blocked group-head target reservation、
  [PR 1612][pr-1612] 的 edge-triggered wake、[PR 1614][pr-1614] 的 overlapping
  group scene，以及 [PR 1613][pr-1613] 的 reservation-stall diagnostic；
- [PR 1459][pr-1459]、[PR 1460][pr-1460] 与 [PR 1466][pr-1466] 的
  per-run hierarchical state、`RunHandle` 和 run-owned resource isolation；
  [PR 1467][pr-1467] 建立 launch fence，[PR 1540][pr-1540] 提供 depth-two
  generation-safe capacity，[PR 1541][pr-1541] 将其接成 negotiated whole-run
  FIFO admission，并补齐 failure-safe run finalization；
- [PR 1574][pr-1574] 的 progressable native-run C/C++ lifecycle、内部 executor
  handoff、sticky launch wakeup、poll/wait/finalize 和 stale token protection；
  [PR 1583][pr-1583] 的 direct A2/A3 onboard depth-two two-frame progress/control
  endpoint；[PR 1587][pr-1587] 在 predecessor/successor 均无 diagnostics 时的
  HBG successor inactive-bank native prepare overlap。prepared successor 未
  launch/accepted，且必须等待 predecessor
  finalize，所以没有第二个并发 device execution；其它 endpoint 仍使用 single-frame
  blocking compatibility。[PR 1649][pr-1649] 为 sim launch acceptance 补验收；
- [PR 1422][pr-1422] 的 `TaskArgsBuilder` unique/non-shadowing name preflight
  与 duplicate-name rehost rollback correctness；
- [PR 1430][pr-1430] 的 kind4 typed `(worker_id, ptr)` provenance、
  exact-base dispatch/free、CommDomain role tracking 和 revoke-before-native-free，
  以及 [PR 1557][pr-1557] 的 allocation-contained copy range；这是旧 ABI 的
  安全 guard，不是 generation-safe handle；
- [PR 1431][pr-1431] 的 SceneTest Python rehost/`make_tensor_arg`
  singleton-stride/shared-storage characterization；该 PR 只增加测试，C++
  dependency-key evidence 仍未交付；
- [PR 1424][pr-1424] 的 A3 L3 comm-window Fabric V2 handle、legacy fallback 和
  mapping ownership/release；其 scope 仍是 single-host multi-card；
- [PR 1558][pr-1558] 的 L2 `sync_start` local-owner fast path 与
  global-drain fallback；它不改变 hierarchical run admission；
- [PR 1592][pr-1592] 的 committed-HBM query 与 [PR 1602][pr-1602] 的 A5 DFX
  accounting coverage；HCCL/VMM communication windows 仍不计入；
- [PR 1521][pr-1521] 的 `aicpu_thread_num=0` architecture auto default 与
  onboard usable-topology resolution；
- [PR 1392][pr-1392] 与 [PR 1421][pr-1421] 的 A5 URMA
  deferred-completion backend/demo；device path 仍受 capability/build gate
  约束，且不交付 Remote L3 transaction 或 global membership；
- [PR 1397][pr-1397] 合入 snapshot 的 15 个 CI job 全绿，包括 A2/A3、A5
  simulation 与 onboard ST；这不是当前 main 的独立全量复测声明。

当前 main 已实现的 lifecycle 状态边：

  ~~~text
  NEW ──init claim──→ INITIALIZING ──success──→ READY
                             └──────failure──→ FAILED
  NEW | READY | FAILED ──close claim──→ CLOSED
  INITIALIZING ──close claim/cancel──→ CLOSED
  ~~~

`CLOSED` 是唯一 public terminal state；没有 public `CLOSING`。私有
`_CloseAttempt` 只记录一轮 close 的 completion，不是第二套 lifecycle。
READY-only leased API 在 close claim 后不再 admission，已进入的 operation
先 drain；同一轮并发 close 固定 join 自己观察到的 completion。native teardown
只能由 init owner 执行。INITIALIZING 上的 close 锁存 cooperative cancel；已经
进入的 native startup 段不被强杀，close 等待该段返回有独立上界。

[PR 1541][pr-1541] 没有改变这五态 public lifecycle，但把 close drain 扩成同一个
absolute cleanup budget 下的 operation leases + accepted RunHandles；预算耗尽时
teardown 保持未尝试，CLOSED worker 保留 tree/handles 供后续 owner-thread
`close()` 重驱。它还用一次 immutable `_CloseOutcome` reference publication 取代
`error/incomplete/done` 三字段逐次发布，关闭了 joiner 可能观察到 torn completion
payload 的窗口。

当前 cleanup 由 [PR 1673][pr-1673] 的 `CleanupJournal` 与
[PR 1689][pr-1689] 的统一 teardown driver 共同构成：action 只有在 native cleanup
成功后才删除，失败条目可由后续 owner-thread close 重驱；startup rollback 与
READY close 共用一条有序 sequence。child PID/mailbox 只在 `waitpid` 成功后释放，
hard-killed descendant 的 SHM 由 root-visible exact tree namespace 枚举回收；
region、domain、host buffer、remote、native、child 与 SHM 都进入同一
post-success retry discipline。它不改变 `CLOSED` 的 public terminal 语义，也不
吸收 RunHandle-owned cleanup。

P0.2 已交付边界：

- 明确 journal ownership domain：P0.2 journal 只覆盖 Worker tree 的
  startup/close resources，不吸收 [PR 1466][pr-1466] 已建立、并由
  [PR 1541][pr-1541] 事务化加固的 RunHandle-owned `_RunResources`；#1541 的
  `_RunFinalizationCursor` 是单轮 run 的 monotonic exact-once finalizer，不是
  Worker-tree teardown 的 retry journal。跨 run persistent resource 属于
  **per-worker** lifetime，必须进入显式、可枚举的 Worker resource registry，定义
  admission/release 和与 close 的锁序，不能退回无类型的 Worker-global 隐式 sweep；
- root-visible recursive PID/PGID/mailbox/shm ownership、scan-to-signal closure，
  以及 hard-killed descendant 的 root-owned unlink 已由 #1689 交付；
- survivor-before-unlink、真实 survivor failure-injection 和七类 resource-action
  覆盖已由 #1689 交付；
- public ChipWorker owner/lifecycle gate 已交付；raw binding 释放 GIL 仍不表示
  init/finalize 可以并发；
- `add_worker()` 的 parent/child 原子 freeze，以及 startup rollback 不丢失
  unreaped direct child 的保证已交付；
- `_abort_hierarchical` 与 `_teardown_ready_tree` 已收敛到同一个 journal/action
  schema 和一条 teardown sequence。

P0.3 simulation remote correctness 的 #1420 + #1426 + #1427 + #1432 已交付
startup/deadline 切片：

- remote session open/daemon activation 与 endpoint/health-thread attach 均在
  本进程最后一次 local fork 和 local READY barrier 之后；
- parent 在每个 remote 前从 root deadline 计算递减 remaining snapshot，wire
  通过独立 `startup_remaining_s` 传播 duration；remote host 使用自己的
  monotonic clock 重建 deadline，缺少新字段时兼容回退到 session timeout；
- parent/session/daemon 都有 positive-finite helper，已有测试覆盖 manifest
  字段分离、fallback、递减 snapshot、expired-before-open 和常规 partial-init
  cleanup；
- 存在 remote spec 时，parent 在 hierarchical SHM/`_Worker`、local fork 和
  daemon socket 前校验 `remote_session_timeout_s`；没有 remote spec 时该配置
  inert。daemon 在 pipe、manifest tempfile 和 runner `Popen()` 前校验
  `session_timeout_s` 与 `startup_remaining_s`，runner 继续执行第二次校验；
- daemon 为每条 client connection 独立收口普通异常；live runner 在本地 reply
  send 返回前仍由 connection 持有，send 或 reaper handoff 失败时由 `finally`
  回收，成功后才交给 background reaper。控制异常仍向外传播，listener close
  可以有界停止 accept loop；
- endpoint 只允许 numeric address 或 `localhost`，避免不可界定的 DNS；跨机
  parent→daemon 继续只传 remaining duration，parent 与每台 remote host 分别建立
  local absolute monotonic deadline；daemon→runner 的同机 private manifest 传
  remote host 的绝对 deadline；
- parent connect/send/framed recv、native command/HELLO/health attach 和最终 READY
  commit 都从对应 deadline 派生 remaining，不允许 blocking stage 重启完整预算；
- native transport 分列 attach/runtime timeout；command/HELLO/health connect 使用
  attach deadline，READY 后 TASK/CONTROL/health 使用完整 runtime timeout。connected
  fd 保持 nonblocking 并以 deadline poll framed I/O；runner 的 accepted command
  socket 恢复 blocking，健康 idle session 不消费 startup timeout。

[PR 1432][pr-1432] 已在一个耦合 PR 中闭合旧 P0.3a（attach/runtime timeout
separation）与 P0.3b（end-to-end hard wall）；二者不再属于 residual。
\#1432 只保证 READY 后使用完整配置的 runtime timeout，不负责把单条 command
completion deadline 与 session health/lease 解耦；健康长任务的 production
failure-isolation 仍属于 P2。

P0.3 full-hierarchy evidence（**已由 [PR 1692][pr-1692] 交付**）：

1. **Full-hierarchy failure-injection evidence。** 在 test-level hard timeout 下，
   真实 L4 + local SUB + remote L3 树覆盖 local child 已 READY 后的 remote open
   timeout、later-remote failure 和 open-success/attach-failure；断言本地 PID、
   remote runner PGID、精确观测 SHM、session/native state 零残留，并证明同一
   daemon 能接受下一健康 session。remote open/attach 错误带失败 `worker_id`，
   startup budget 耗尽仍保留 `TimeoutError`，不被统一压平成 `RuntimeError`。

另一个相邻 correctness 项不计入 P0.2 或 P0.3 退出条件：
[PR 1003][pr-1003] 已独立交付 TASK_DONE、CONTROL_DONE 运行期 child-exit
liveness。SceneTest duplicate-name ambiguity 已由 [PR 1422][pr-1422] 关闭。

P0.1 eager core 的完成口径：

> 健康 local/simulation hierarchy 的 init() 返回时整棵 worker 树 READY；
> first dispatch 和其它公开 API 不再承担 startup。

\#1398 narrowed slice 的退出条件（已满足）：

> L2/L3+ 共用一个 authoritative lifecycle；第二个 concurrent init 对
> INITIALIZING fail-fast，原始 startup cause 在 rollback 前保存，final READY
> transition 位于完整 exception boundary。NEW/READY/FAILED 的 close claim
> 原子发布 CLOSED；CLOSED 不重启旧 epoch，也不再 admission leased API。
> 并发 close join 固定的 `_CloseAttempt`，active operation 在 native teardown
> 前 drain；teardown residual 必须报错且 terminal result 可重放。initial
> callable 在 startup 资源创建前按 kind 验证 eligible target。

完整 P0.2 follow-up 的退出条件（**已由 #1689 满足，P0.2 DONE**）：

> admission、dynamic eligibility、cooperative close-cancel 和 journal 的
> post-success commit 已交付。root 能枚举 nested PID/PGID/mailbox/shm 并封闭
> scan-to-signal；survivor 在 unlink 前登记，hard-killed descendant 不承担最终
> unlink，failure-injection 会真正产生并重驱 survivor。public ChipWorker 具备
> owner gate，add_worker 原子 freeze child，startup rollback 不丢失 unreaped child；
> startup rollback 与 READY close 共用一条 teardown sequence。上述保证限定于
> Worker-tree 的 local/simulation startup/close ownership domain，不包含 P2 remote
> confirmed-clean 或 RunHandle-owned resource finalization。

\#1420 交付切片的退出条件（已满足）：

> 本进程最后一次 local fork/READY 前不打开或激活 remote session，也不启动
> endpoint/health thread；每个 remote 前从同一 root deadline 取得递减 snapshot；
> wire 只传播有界 `startup_remaining_s` duration，remote host 使用自己的
> monotonic clock 重建 deadline。

\#1426 交付切片的退出条件（已满足）：

> 只有实际存在 remote spec 的 parent 才校验 remote session timeout，并且校验
> 发生在 hierarchical SHM/`_Worker`、fork 和 daemon socket 之前；local-only
> hierarchy 不消费该配置。daemon 在 pipe、tempfile 和 runner 之前校验 session
> timeout 与 startup remaining，runner 保留 defense-in-depth 校验。0、负数、NaN
> 和 Inf 在上述入口 fail-fast 且不留下对应资源。

\#1427 交付切片的退出条件（已满足）：

> 一个 daemon client connection 的 manifest/socket/session `Exception` 不会终止
> shared accept loop；live ready runner 在本地 reply send/reaper handoff 成功前
> 保持唯一 owner，失败时恰好回收一次，成功后交给 background reaper。控制异常
> 继续关闭 daemon；dead/truncated client 后下一连接仍可服务。send 返回只表示
> 本地发送成功，不承诺 parent 已确认 READY。

\#1432 交付切片的退出条件（已满足）：

> parent 与每台 remote host 各自只建立一个 local absolute monotonic deadline；
> 跨机 wire 只传 remaining duration，同机 daemon→runner private manifest 传该 host
> 的 absolute deadline。connect/send/framed recv、command/HELLO/health attach 和
> 最终 READY commit 都从对应 deadline 派生 remaining。native transport 分开
> attach timeout 与 READY-era runtime timeout，startup 末期余量不会再缩短健康
> session 的 TASK、CONTROL 或 health timeout。

完整 P0.3 simulation remote correctness 的退出条件（**已由 #1692 满足**）：

> \#1426 已满足 numeric timeout 的 parent/daemon pre-resource 子条件；#1427 已满足
> daemon per-connection exception isolation 和 local-send-failure runner 回收子条件；
> \#1432 已满足 single-root hard wall、最终 READY gate 和 attach/runtime timeout
> separation。#1692 已补齐 local child READY 后的 remote open timeout、
> later-remote failure 和 open-success/attach-failure 的 PID/PGID/SHM/session
> 零残留回归，并证明 daemon 仍能接受下一请求。发送成功但 parent 未确认的歧义
> 留给 P2 ACK/lease；P0.3 不承诺 CLEANUP_ACK、lease 或 partition recovery。

Production remote 的 confirmed cleanup、lease/epoch 和 partition recovery
属于 P2，不反向阻塞 P0.1 eager core；本地 attachment/transfer 基础属于 P1，
跨 local/remote 的异构域组合与 rank capacity 属于 P3。

### 5.6 P1：建立本地 Buffer–Attachment–Tensor 基础并简化执行

P1 的纯值类型、codec 和 validator 早期可以与 P0.2 并行设计；#1689 合入后，
P0.2 对 public 五态 lifecycle、terminal CLOSED、admission、retry journal 和
recursive ownership 的 merge gate 已满足。任何 public create/release、由
Domain/Worker 驱动的 attach/detach、dispatch cutover 或 Worker-close ownership
仍必须复用该契约，不得另建 registry-local lifecycle；一旦调用会触达
device/mailbox，还必须遵守 #1541 的 run/control admission：
callback 内先于首个 `submit_*()` 并等待本 run FIFO head，callback 外在无 live run
的独占 reservation 中执行。

\#1430/#1557 已交付旧 raw-pointer ABI 上的过渡性 owner/range guard；#1431
已刻画 Python rehost 的 singleton-stride normalization 和 backing split。
[PR 1599][pr-1599] 是 §2.4 target ABI/codec 与 unwired `create_buffer` 的载体，
**已合入本基线**（`b535fa21`），并已采用 `BufferDescriptor`/`Tensor`/`ChipTensor` 词汇；
Python/C++ public type 的镜像/异名已由 #1681（`b3bf3e7a`）消除（C++ device POD 本身即
`ChipTensor`）。✅ **更正 2026-08-11：本段原写「仍未做的是把地址无关 `Tensor` 接入 TaskArgs/dispatch（＝ #1453 的 cutover），以及 §2.4 G1–G6 的收口（#1703 已交付 G1）」——两条都已过期，且与**本文件 §2.4 上方
那句「✅ 这条也已兑现」自相矛盾**（同一文件内对同一件事给出相反状态）。现状：
**① wire cutover 已由 PR #1729 `aa1d7c7d`（2026-08-07）落地**，#1453 是原 omnibus DRAFT，已被 #1729
承接并于合入后 **CLOSED**，未再变更范围重开 ⇒ 它不再是「后续设计输入」，也不再与 main 冲突。
**② §2.4 的 G1–G6 ＋ A1/A2 现已全部收口** —— G1＋G6 ＝ #1703 `02f90ef5`，G3＋G2(schema)＋A1 ＝
\#1704 `4d0de8ea`，A2 本就已闭，G4 ＝ #1744 `0c367015`，G5 ＝ #1749 `919abfdf`。
⚠️ **本段此前是这份文档里唯一一处仍把 P1-A 说成未收口的地方** —— 状态以 `.docs/README.md` §1/§2 为准，
本文件只在分期（P0–P3、P1-A–P1-F）与契约上有权威性。#1599 的 `create_buffer` 与
close cleanup 不是纯 codec，而是 P1-A 加一个 minimal allocation-lifetime
slice：review 时必须证明 public admission、handle-local idempotent cleanup 和
close ownership 不绕过上述 lifecycle gate，也不绕过 whole-run/direct-control
ordering。当前 candidate 的
`_create_buffer_locked()` 的 eligibility gate 只识别 chip/sub children：只有
`_next_level_shms` 的 L4 会直接拒绝；mixed L4 即使因另有 SUB 而过 gate，也不因此
获得正确的 next-level owner delegation/materialization。合入前须补完整的
next-level 支持矩阵，或把首版 public scope 明确限制为 L2/L3，不能宣称 local L4
foundation 已交付。
这些条件满足也不表示 Attachment、完整 lifetime 或 dependency 契约已交付。

P1 按以下小步顺序推进：

- **P1-A identity/wire**：一次性冻结 [P1-B corrected design][p1b-abi]
  定义的 fixed-layout descriptor，以及 §2.4 定义的跨模块语义、带 strides 的
  `Tensor`、完整 canonical
  identity、single validator、unknown-backend 拒绝与 address-domain capability
  matrix。address-bearing body 只准发往 fork-compatible/exact owner-chip
  endpoint；local same-build 边界用 `SIMPLER_BUILD_COMMIT` 拒绝 skew。P2 在
  外层 versioned session envelope 增加 build/capability negotiation 和
  instance routing，不把 `owner_worker_path_id` 或 raw VA 升格为 identity/route。
  [PR 1648][pr-1648] 已交付 256 ChipTensor/frame 容量；P1-A 冻结每 Tensor
  descriptor 大小时，必须保留明确的 TaskArgs blob 上界与 256 accepted/257
  rejected 边界，不能让新 wire layout 静默缩小已交付容量。
  **交付状态（`b535fa21`）**：identity/wire layout 已由 #1599 冻结
  （`CanonicalIdentity` 32 B · `BufferDescriptor` 88 B · `Tensor` 144 B，
  `static_assert` 逐字段钉死；`magic` 是 discriminator 而非版本号，无多版本共存）。
  ⚠️ **但 P1-A 尚未达成**：§2.4 的 G1–G6 六条实现级 gate 在合入时一条未闭。
  ✅ **gate 清单已全闭**：**G1 ＋ G6** 由 [PR 1703][pr-1703]（`02f90ef5`）、
  **G3 ＋ G2(schema) ＋ A1** 由 [PR 1704][pr-1704]（`4d0de8ea`）、**A2** 本就已闭。
  ✅ **⇒ P1-A 达成**（退出条件 = 冻结 layout ＋ 走完自己的合入门，二者皆已满足）。
  ⚠️ **本条文里那句「address-bearing body 只准发往 fork-compatible/exact owner-chip endpoint」
  是 P1-A 声明、P1-B 兑现** —— §9 第 5 条（endpoint × `address_space` 判定）所需的信息**不在
  wire 上**（`owner_instance_id` 是不透明 nonce；`owner_worker_path_id` 按设计只作诊断；
  `address_space` 只有 HOST/DEVICE、不区分哪张卡），只能由 P1-B 要建的 registry/owner authority
  提供。**它是 P1-B 的退出条件，不是 P1-A 的遗留** —— 记成后者会让前一个里程碑永久依赖后一个。
  以下为各 gate 的内容说明 —— G1 是 saturating 运算 ＋
  哨兵**按名**拒绝（不与 `nbytes` 比较，否则一个声称巨大 `nbytes` 的 descriptor 能把视图买回来）＋
  `tensors_overlap` 两端也饱和（否则两个完全重叠的视图判为不相交 = 漏依赖边）；
  G6 是 `create_buffer` 认 `_next_level_shms`。**A2 本就已闭**（见 §2.4 表，此前判断有误）。
  G3 是 cache 命中比对 descriptor ＋ shm object size 核对；G2 的 schema 半是 per-backend body
  规则 ＋ reserved 区归零（**同时覆盖 `Tensor` 过 `ndims` 的槽位**，`_pad` 显式排除）；
  A1 是 wire 元素的 blob 上界断言。收口任务
  `tasks/task-p1a-close-merge-gates.md`（本地参考：`tasks/task-p1a-close-merge-gates.md`） **已交付完毕**。
  ⚠️ **两条断言都已有，但 blob 那条用的是 `sizeof(ChipTensor)`（128 B）而非 wire 的
  `Tensor`（144 B）** —— 对今天正确，**cutover 后就不再守着实际尺寸**。实测容量不紧张
  （余 ~27 KiB），所以那是一条 **guard** 而非正在发生的溢出。详见 §2.4 表的 A1 行。
- **P1-B registry/lifetime**：物理 allocation 由部署在该资源上的 Worker
  authority 创建和回收；owner 建立 `BufferRegistry`，consumer 建立
  `AttachmentRegistry`。同批交付 retain/release、deferred free、generation/ABA、
  stale/duplicate release、detach 与 close-time cleanup。Orchestrator 只协调
  attachment、transfer 和 task，不成为 backing owner。
  ⬅️ **并入 P1-A 移交的一条退出条件（2026-08-05）**：**§9 不变量 5 —— device ref 只到其
  owner chip**，即 G2 的 `ImportContext` 半（`materialize` 收显式 endpoint 上下文并验
  fork/exact-owner-chip address domain，不只凭 `backend_kind`）。它落在这里而非 P1-A，
  是因为判定所需的信息不在 wire 上，只能由本里程碑要建的 registry/owner authority 提供。
  ⚠️ **这条是 wire flip 的硬前置**：今天不可能被违反只因 `ImportRegistry` 还没有生产调用者
  （`worker.py` 只 import `Buffer` / `create_host_shared_buffer` / `mint_owner_instance_id`），
  而 [PR 1453][pr-1453] 把地址无关 `Tensor` 接进 TaskArgs/dispatch 的那一刻这条路径就活了。
- **P1-C local attachment adapters**：先覆盖 Host DDR 与本地 L2 Device GM。
  Host shared mapping、host register 后的 direct load/store、Device GM
  owner-delegated `copy_to/copy_from` 可以并存；potential/actual capability
  必须遵守 §2.4 的完整公式，不能由 level、`address_space` 或 topology 单独
  猜测。扩展 `halHostRegister` 前须先关闭当前 A2/A3 registration path 绕过
  `acl_to_hal_device_id()` 的 known issue；不能把 logical id 直接复制进新 backend。
  当前不做 Host/L4 对 Remote L2 GM 的 direct attachment。[PR 1651][pr-1651]
  已交付 HBG host-view：有 host map 时使用 direct view，
  无 host map 时使用 orchestration-window mirror + owner copy-back。后一路径必须标成
  copy-capable Attachment，而不是 host-direct，也不能让 staging VA 进入 identity。
- **P1-D explicit transfer/copy**：typed `Tensor` 操作只接受由 Domain/Worker
  registry 持有的已授权 live Attachment；`copy_to/copy_from` 读写既有 Buffer 并保持 identity，
  `Transfer` 按显式 placement policy 向目标 owner 创建并返回新 Buffer identity。
  无满足操作的 Attachment 是必须选择 Transfer/proxy 的情形，但不是 Transfer
  的唯一入口。普通 submit 不 silent copy，也不把 copy capability 伪装成 host
  可解引用的 device VA。
- **P1-E dependency/arena**：按 canonical identity + byte footprint 跟踪
  RAW/WAR/WAW，定义 strided bounds/overlap；拆分 SlotArena 与
  SharedBufferArena，并提供基于一个 Buffer 的 `BufferSliceArena`/offset slice，
  作为用户构建 queue、metadata table 等 persistent structure 的基础资源；
  在 #1592/#1602 committed-byte 基础上补 persistent Buffer/domain resource 与
  communication-window accounting/budget。
- **P1-F execution service**：[PR 1574][pr-1574] 已交付 B3a native phased
  lifecycle；[PR 1583][pr-1583] 已交付窄 B3b 的 direct A2/A3 onboard depth-two
  local-chip subset；[PR 1587][pr-1587] 已把 phased API 接入 child progress loop，
  并交付 predecessor/successor 均无 diagnostics 时的 HBG successor
  inactive-bank native prepare overlap。ordinary
  control 可在 active run 期间服务，registry mutation deferred；staged lane 只接
  prepared FIFO successor 的首个 eligible single，prepared group 仍在 promotion 后
  同步 dispatch。剩余工作分两条：本基线不含的 [PR 1588][pr-1588] 是 TMR prepared-state
  pipeline candidate；通用 B3c 则把 sequence/backpressure/completion/control 契约
  推广到 SUB、nested/Local L4、A5、simulation 和 remote transport，并补
  multi-flight control/cancel/health。第二个 launched/device execution、registry
  mutation overlap 和通用 persistent-kernel session 都是独立后续，不得混入
  transport 扩展。[PR 1650][pr-1650] 是 post-B4 native ownership/compatibility
  correction candidate，尚未进入本基线，不能写成 main 已交付。

P1-A → P1-B → P1-C/P1-D → P1-E 是资源模型主链。已交付的窄 B3b/B4 可继续
沿用旧 TaskArgs wire，但任何 referenced backing 都必须保留到 native finalize；
通用 B3c transport 也可先保持旧 wire，只有 `Tensor`/Attachment payload cutover
依赖 P1 wire/retain 稳定。常驻 native run 要复用的 backend 必须先具备对应的
responsive health/status/shutdown 与 single phase owner；direct A2/A3 path 已提供
窄证据，其它 endpoint 仍须补齐。任意 register overlap 和第二 device execution
不是 persistent-kernel adapter 的前置。#1599（**已合入**）= P1-A 加 minimal
allocation-lifetime slice；其 local registry/`create_buffer` 部分仍须保持
unwired、受限并按上述 lifecycle/L4 scope 条件单独验证，不能借“skeleton”
提前发布完整 public lifecycle。⚠️ **它是在 merge gate 未闭的情况下合入的** ——
收口清单与逐条状态见 §2.4 的 G1–G6 表，任务见
`tasks/task-p1a-close-merge-gates.md`（本地参考：`tasks/task-p1a-close-merge-gates.md`）。该步不同时引入 remote routing、queue protocol
或 persistent kernel。

退出条件：

> 同一 local Buffer 能在多个 Worker 上建立可验证 Attachment 而不改变 identity；
> copy-only endpoint 永远拿不到可解引用 VA；不可 attach 的数据只能通过显式
> Transfer 得到新 identity。所有非法 memory/endpoint/capability 组合在
> materialize 或 dispatch 前失败；release/close 后无 backing 泄漏、
> stale-generation 误用或 double-release。Local L3 能以零 hierarchical heap
> 运行，长时间 L2 task 执行期间仍能处理 control。

### 5.7 P2：建立生产级 Multi-L3 通信

当前主路径为 **mpirun 启动 + 原生 ACL Runtime/PTO 通信**。#1456 的 A3 Fabric V2 共享窗口、
\#1623 的 MPI 启动与 #1624 的 mailbox group dispatch 均已在 main（通信核验 `68d137fdf`）。
A3 数据路径有当前代码与持续双机 PR CI 证据，近期具名测试 SHA 逐项通过（见通信方案 D.4）；PREPARE_EXPORT/IMPORT/COMMIT/ABORT 与 MPI readiness 已有基座。
P2 要补齐的是生产级 session/epoch、lease、成员冻结与失败后确定性回收，
不能写成跨机通信或建域事务从零实现。`host_tcp` 的限制只适用于该控制 transport。
各原生 backend 验收归 DS-5；SHMEM 仅为实现参考，不增加库依赖或 HCCL/HCOMM 必选建域路线。

[Issue 824][issue-824] 的 local-only domain persistence 与 remote transaction
可以分 PR，但不是简单跳过 run fence：#1466 已把 domain/region/remote refs
收紧为 RunHandle-owned `_RunResources`，#1541 又让这一 owner 的 release/finalize
exact-once，并在模糊 cleanup boundary 后停止后继 admission；这些都没有创建跨
run resource entry。persistent domain 属于既定的 **per-worker** lifetime，仍必须
新增显式、可枚举的 Worker domain registry，并定义 admission、retain/release、
budget 和 close/journal lock ordering；这不是第三种 owner/lifetime。
下文 P2-D 的 remote 扩展还必须受 P2-A/P2-B 的 session 和 membership
transaction 约束。

工作项：

- **P2-A session transaction**：由 L4-owned MPI group 与 rank-side runtime 组织
  session/epoch registry 和 remote resource journal；独立常驻 daemon 不是前置。增加 version/capability negotiation、
  `CLEANUP_ACK`、`cleanup-unconfirmed`、finite lease 与独立 heartbeat；定义
  versioned、幂等、可重传的 `CANCEL`/`SHUTDOWN` 状态转换及 terminal ACK，
  重复 op 不得重复释放、stale epoch 必须拒绝；覆盖 network partition、client
  crash、health loss 和 stale epoch。session registry 同时管理 remote
  Buffer identity、attachment lease 和 capability；远端 local VA/VMM handle
  本身不能作为 Attachment 或跨机 identity。
- **P2-B membership/bootstrap transaction**：引入 device lease 和 endpoint
  capability inventory，冻结 global rank；在已有 MPI descriptor exchange 与
  PREPARE_EXPORT/IMPORT/COMMIT/ABORT 上闭合 communication epoch、rollback 与 session lease。
  HCCL rootInfo 的 opaque distribution 只属于相应 collective adapter。
- **P2-C remote transfer/attachment plan**：显式区分两条 copy 语义。remote
  owner-delegated copy/proxy 可以经 socket/staging 读写 owner 已有的同一 Buffer，
  保持 identity，但 consumer 不因此获得可解引用 VA；显式 Transfer 则由 target
  owner 创建新 Buffer，transport 搬运并在 completion 后发布新 identity。只有经过
  硬件和 runtime 验证的 RDMA/URMA/Fabric memory-registration 路径才能发布
  remote direct Attachment capability；owner-delegated proxy 不能伪装成 direct map，
  Transfer 也不能伪装成同一 identity。
- **P2-D per-worker domain lifetime**：先建立独立于 `_RunResources` 的 explicit
  Worker domain registry，再让 CommDomain 跨 run 持久化并实现 Remote CommDomain
  controls；禁止退回无类型的 Worker-global implicit sweep。
- **P2-E onboard profiles**：以已有 A3 HCCS 共享窗口实现为基座，沿用原生 ACL/PTO 路径
  组织 RoCE、A5 UB/URMA 的逐 backend 验收；完整交付联验事务和 cleanup，
  backend 可独立推进，不重做 A3 首次通信。
- command completion deadline、health 和 session lease 必须相互独立；健康的
  长任务不能仅因固定 command timeout 被误判为 endpoint failure。

⛔ **2026-09-07 改写。原文是「P2-A → P2-B → P2-C → P2-D → P2-E 是 merge-gate 顺序；
后序阶段不能绕过前序退出条件」，而本节前言已声明依赖只在具体工作项之间** ——
两句直接矛盾，且旧句会重新挡住已经明确分出来的**本地**那半（`P2-D` 的本地 ownership
＝ `RA-12`，PR #2120 在没有 `P2-C` 的情况下已经做成）。

**保留的是真实的远端安全门，按工作项表达**（真源 `.docs/work-items.md`）：

- 远端资源的 lease、断连与回收（`DS-4`）**必须**在 MPI session owner 拥有 session/epoch registry
  （`DS-1`）与成员事务执行（`DS-9`）之后 —— 没有事务就没有可回滚的远端状态。
- 后续 onboard backend（`DS-5`）**不能**与 session transaction 合成一次大切换。
  这一条是原文里唯一真正的「不许合并」，与阶段编号无关。
- **本地**的 per-worker domain ownership（`RA-12`）**不受**上述门约束。

⚠️ **不要再用含本地半的旧阶段编号表达远端门** —— `P2-D` 一个编号跨了本地与远端，
用它当门就会连带挡住本地。

退出条件：

> 一个 L4 能创建、复用和销毁跨 Local/Remote L3 的通信域，并在任意
> rank 部分失败时完成确定性清理；收到 ACK 的 remote 可标记
> confirmed-clean，未 ACK 的 session 明确返回 cleanup-unconfirmed，并由
> finite lease 最终回收；CANCEL/SHUTDOWN 重传具有幂等 terminal outcome；
> 健康长任务不会被 command deadline 冒充为 session health failure。remote
> direct Attachment 与 explicit Transfer 在 identity、completion 和可观测
> data-path 上严格区分。

**逐条承载**（2026-09-08 补，真源 `.docs/work-items.md`；P1 已在 `.docs/README.md` §2.-1 有同款表）：

 | P2 退出条件（逐条） | 承载 |
 | ------------------- | ---- |
 | 一个 L4 能创建、复用和销毁跨 Local/Remote L3 的通信域 | `DS-10` |
 | 任意 rank 部分失败时完成**确定性清理** | `DS-9`（PREPARE/COMMIT/ABORT ＋ 回滚） |
 | ACK'd remote 标 confirmed-clean；未 ACK 明确返回 cleanup-unconfirmed；finite lease 最终回收 | `DS-4` |
 | CANCEL/SHUTDOWN 重传具有**幂等 terminal outcome** | `DS-9` |
 | 健康长任务不会被 command deadline 冒充为 session health failure（三者必须相互独立） | `DS-4` |
 | remote direct Attachment 与 explicit Transfer 在 identity / completion / 可观测 data-path 上严格区分 | `DS-3`（本地那半＝`RA-5`） |

⇒ **六条全部有承载，且都在 `DS` 线内**（`RA-5` 是它的本地对应物）。

### 5.8 P3：暴露异构逻辑域

当前只有逻辑扩展点；P1 提供 local Buffer–Attachment–Tensor 基础，P2 提供
remote session/transfer/attachment plan，P3 才把它们组合成用户可选择的
异构逻辑域。CommDomain 不是 HCCL communicator 的别名，也不要求所有成员共享
同一块物理 memory。

工作项（**编号 ①–⑥ 于 2026-09-07 补**，此前无编号 —— 无编号的清单无法被引用，
也就无法检查是否有遗漏；实施归属见 [`work-items.md`](work-items.md)）：

- **①** 增加由 `(session_id, worker_path, deployment)` 标识的 Host/L3/L2
  EndpointId 和
  capability view；level 表达控制拓扑，deployment/interconnect/backend 决定
  物理访问潜力，actual capability 仍须经过 authorization、live
  Attachment/materialization、runtime probe 和 lease。
- **②** 让一个 CommDomain 的 `ResourceBundle` 组合多种 backing 和 adapter resource；
  例如 Host DDR control Buffer、Device GM payload Buffer 和 collective
  communicator；`DomainRegionSet` 只包含其中前两类按 owner 组织的
  Buffer-backed entries，不包含 communicator。由 `AttachmentPlan/TransportPlan`
  选择 direct load/store、
  owner-delegated copy、device-peer、collective 或 explicit Transfer adapter。
- **③** 对用户暴露最终 direct/proxy/transfer plan、capability 和 completion；
  runtime 可以按显式 policy 选 adapter，但不能 silent copy 或隐藏 identity
  变化。
- **④** 在 P1 `BufferSliceArena` 上提供 publish/observe/wait 等最小同步原语和第一个有界
  SPSC CPU↔AICPU queue adapter 示例。queue schema、opcode、batching、
  backpressure policy 和 persistent-kernel 业务循环由用户/上层库定义，不进入
  Buffer/Attachment 基础 ABI。
- **⑤** 移除或重构 64-rank device ABI 上限。
- **⑥** capability view 只在对应 backend 完成功能、故障和硬件验收后实现，
  不为未落地能力发布空壳资源或伪成功接口。

⭐ **六项的实施归属与验收拆分**（真源 [`work-items.md`](work-items.md)，此处只列映射）：

 | — | 归属 | 验收怎么拆 |
 | - | ---- | ---------- |
 | **①** endpoint 与 capability view | `RA-8` | 与 P1/W2 共同设计，**逐 backend 交付** |
 | **②** 多 backing / adapter 的 ResourceBundle | 本地半 `RA-9` · 跨节点半 `DS-10` | 先定义组合与失败语义，**先验收本地域，再验收跨节点域** |
 | **③** 暴露 plan / capability / completion | `RA-10` | **随每条访问路径一起交付**，成为 P1/P2 的接口要求，不单独排一段 |
 | **④** 最小同步原语与 SPSC queue | `RA-11`（原语）· 上层库（schema/policy）· `EX-2`（常驻循环） | 与 W4①/W6 **合成同一工作包**，不重复排期 |
 | **⑤** 64-rank 上限改造 | `DS-7` | **独立的规模扩展工作包**，不阻塞小规模域先行交付 |
 | **⑥** 验收后才发 capability view | **不是工作项，是约束** | 加在 `RA-8` 上并传导到 `RA-9`/`RA-10` |

⇒ ⭐ **本地异构域与 SPSC 示例无需等待全部远端可靠性工作完成**；跨节点异构域仍必须过
session、事务及故障清理验收；64-rank 扩展也不阻塞小规模域。
⛔ **因此「P3 被 P1 ＋ P2 双重前置」这个说法作废** —— 依赖在具体工作项之间。

退出条件：

> 用户选择逻辑 endpoint、操作和允许的 data-path policy；runtime 内部执行
> AttachmentPlan，并向用户暴露最终 capability/Transport choice 与 completion，
> 不暴露可手工 detach 的 live Attachment handle。不支持的 engine/protocol/capability
> 组合明确失败；`copy_to/copy_from` 保持既有 Buffer identity 且 completion
> 可观察，任何 Transfer 创建的新 Buffer identity 也必须显式可见；不以
> “统一内存”名义提供隐式 coherence。

**逐条承载**（2026-09-08 补）：

 | P3 退出条件（逐条） | 承载 |
 | ------------------- | ---- |
 | 用户选逻辑 endpoint、操作与允许的 data-path policy | `RA-8`（endpoint 与 capability view） |
 | runtime 内部执行 AttachmentPlan，向用户暴露最终 capability / Transport choice 与 completion | `RA-10` |
 | **不暴露可手工 detach 的 live Attachment handle** | **约束**（§6 表，加在 `RA-10` 上）——`Attachment` 不是用户概念，2026-08-03 裁决 |
 | 不支持的 engine/protocol/capability 组合**明确失败** | `RA-8` 的本地拒绝路径（同时也是 P1 的一条退出条件） |
 | `copy_to`/`copy_from` 保持既有 identity 且 completion 可观察；Transfer 的新 identity 必须显式可见 | `RA-5` |
 | **不以「统一内存」名义提供隐式 coherence** | **约束**（§6 表，加在 `RA-5`/`RA-10` 上） |

⇒ **四条是工作项、两条是约束**；约束不建格（建了就是永远做不完的格），
写在 `.docs/work-items.md` §6。

### 5.9 架构决策

在没有新的硬件证据推翻前，采用以下决策：

- 主干现有 WorkerEndpoint 和 remote session 是唯一 remote worker
  演进栈。
- L4 拥有控制面 membership；L3 管理 child/device lifecycle；实际部署在 L2
  Device GM 或 Host DDR 上的 Worker authority 拥有 physical allocation，
  coordinator/orchestrator 只持 lease 并协调 Attachment、Transfer 和 task。
- control topology 的 level 与 physical deployment 分离。L4 bootstrap 后只有在
  `AttachmentPlan/TransportPlan` 明确允许时，L2 rank 才直接走数据面；不存在
  “所有 L2 天然互通”。
- public `device_id(s)` 属于 ACL/rt logical namespace；direct `hal*`/DSMI backend
  边界必须按 [PR 1600][pr-1600] 经 `acl_to_hal_device_id()` 转为 driver-visible
  namespace，ACL/rt 调用不得转换。driver-visible numeric id 是 backend-local
  routing datum，不进入 EndpointId 或 CanonicalBufferIdentity；容器隔离后的编号也
  不能被误称为全局 physical identity。
- topology probe/fallback 只提供 capability evidence，不提供 endpoint identity。
  [PR 1552][pr-1552] 只验证 Ascend950PR_9579 x86 + OCCUPY `0x3e`；未知组合应
  capability-absent 或显式失败，不能从这一 fallback 外推。
- `Buffer` 是带 allocation identity/lifecycle 的物理资源；
  `Tensor/CommDomain/queue` 是逻辑组件；`Attachment` 才表达某个部署
  endpoint 如何访问某个 Buffer。
- `AttachmentPlan` 只表达 requested capability/materialization policy，COMMIT 后的
  `AttachmentRegistry` 才表达 actual live Attachment；两者都必须用完整
  `CanonicalBufferIdentity`
  （`owner_instance_id + buffer_id + generation`）键控；owner-local
  `buffer_id` 不能单独充当跨 endpoint/domain identity。
- attachment/import/map 和 `copy_to/copy_from` 保持 Buffer identity；只有
  Transfer 创建新 Buffer identity，Transfer 两端没有隐式 coherence。
  owner-delegated copy capability 不等于 consumer 获得映射或可解引用 VA。
- CommDomainSpec 是 MemberSet、ResourceBundleSpec、AttachmentPlan 和 TransportPlan
  以及可选 DeviceRankMap 的冻结请求；committed CommDomain 还拥有 ResourceBundle、
  AttachmentRegistry 和 adapter state。DomainRegionSet 只是 ResourceBundle 的
  Buffer-backed 子集。HCCL communicator 只是 collective adapter。CPU、AICPU、
  AIV 是 capability/view，不自动成为对等 HCCL rank。
- local P1 edge 只使用 opaque、validated same-build descriptor；
  cross-machine P2 使用 versioned session/wire envelope 并协商 build/capability。
  address-bearing backend body 只在已验证的 fork/exact owner-chip 地址域中使用，
  禁止向其它 chip/host/remote re-export。local VA、process-side path id、
  runtime token 和 socket 转发的 VMM handle 永远不作为 canonical identity。
- capability 是可组合集合，不是 `direct`/`copy` 二选一 profile；潜在能力由
  deployment × interconnect × backing/backend × runtime support 决定，实际
  能力必须再经 authorization、live Attachment/materialization、runtime
  probe 和 lease 建立。
- placement、resource lifetime、`copy_to/copy_from`、Transfer 和 data-path
  policy 必须显式；
  普通 submit 不 silent copy。
- hierarchical run admission 以 child capability 协商出的最小
  `pipeline_depth` 为界；`begin_run()` 在 callback 前取得 generation-safe lease，
  只有 whole-run FIFO head 能进入 `EXECUTING`。launch acceptance 只属于该 run
  自己的 dispatch fence，不再 admission 下一 callback；不得把一个 prepared run
  描述成第二个并发 device execution。
- progressable native run 是 L2 host-runtime boundary 的 ownership lifecycle，
  不是新的 public Worker lifecycle，也不等于通用 async EdgeChannel。
  [PR 1574][pr-1574] 后，successful
  native prepare 必须恰好配对 finalize，slot/lease-generation/run-epoch token 必须
  全部匹配。[PR 1583][pr-1583]/[PR 1587][pr-1587] 后，capable direct A2/A3
  depth-two HBG path 可同时持有一个 launched token 与一个不同 bank 的
  prepared-but-unlaunched successor；launch/device execution 仍只有一个。SUB、
  nested/remote、A5、simulation 和 depth-one endpoint 仍用 single-frame blocking
  composition。内部 progress/executor thread 不放宽 Worker init-owner teardown 或
  public `ChipWorker` ownership gate。
- [PR 1541][pr-1541] 的 `_RunFinalizationCursor` 只拥有单个 RunHandle 的资源收尾；
  P0.2 Worker-tree infrastructure journal、per-worker resource registry 和 P2 remote
  session journal 是不同的 transaction/ownership domain，但不创造第三种 Buffer
  lifetime；不得因“已有 transaction”而合并或省略。
- [PR 1430][pr-1430]/[PR 1557][pr-1557] 的 `(worker_id, ptr, extent)`
  provenance 是旧 raw-pointer ABI 的过渡 guard；在 BufferHandle 带
  generation/canonical identity 落地前，不得把它描述成 strict-ABA-safe
  ownership。
- SharedBufferArena 迁移完成前不删除 HeapRing。
- [PR 1397][pr-1397] 的 eager `Worker.init()` 与 [PR 1398][pr-1398] 的
  narrowed 五态 lifecycle/terminal CLOSED 共同构成后续基线；任何公开 API
  都不得重新承担 lazy startup，后续也不得把 #1398 的 deferred gap 写成已交付。
- A3 HCCS 数据面与 MPI 启动/派发已有交付；生产 session/lease/partition recovery
  仍按 DS-1/DS-4/DS-9 验收。控制 transport 字符串与单个拓扑用例的边界，不能代表全部数据路径。

- 每个仍需创建 local child 的进程都保持 fork-before-thread invariant；fork 前
  只能准备不连接 daemon、不启动线程的 remote spec/descriptor。
  [PR 1420][pr-1420] 已把实际 session open/activation 和 endpoint/health
  thread 放到该进程最后一次 local fork/READY 之后；后续改动不得把连接或
  thread creation 重新前移。
- remote numeric timeout 必须在真实资源入口前 fail-fast；[PR 1426][pr-1426]
  已建立 parent actual-remote gate、daemon pre-resource validation 和 runner
  defense-in-depth，后续不得退回“先分配/拉起、再校验”。
- daemon client failure 必须保持 per-connection containment；[PR 1427][pr-1427]
  已建立 local-send/handoff 前的 runner ownership transaction 和 control-exception
  propagation。后续不得让普通连接异常逃出 shared accept loop，也不得把 local
  send success 写成 parent receipt confirmation。
- remote startup deadline 不跨机传 absolute timestamp；[PR 1432][pr-1432] 已固定
  跨机 duration-only、每 host 一个 local absolute deadline、最终 READY gate 和
  attach/runtime timeout separation。后续不得让任一 blocking stage 重启完整预算，
  也不得把 startup remaining 复用成 READY-era command/health timeout。
- Remote endpoint host 只接受 numeric IP 或 `localhost`；在具备可取消且有界的
  resolver 前，不得把 hostname resolution 放回 startup transaction。

### 5.10 参考资料

- HCCL 总览：[GitCode][cann-hccl] / [昇腾社区版本页][cann-hccl-hiascend]
- HcclGetRootInfo：[GitCode][cann-rootinfo] /
  [昇腾社区 8.5][cann-rootinfo-hiascend]
- HcclCommInitRootInfo：[GitCode][cann-init-rootinfo] /
  [昇腾社区 8.5][cann-init-rootinfo-hiascend]
- HCOMM engine/protocol 定义：[GitCode pinned commit][cann-hcomm-defs] /
  [昇腾社区 8.5 CommEngine][cann-hcomm-defs-hiascend]
- HcclChannelAcquire 能力矩阵：[GitCode][cann-channel] /
  [昇腾社区 9.0 beta2][cann-channel-hiascend]
- HcommEndpointCreate 支持矩阵：[GitCode][cann-endpoint] /
  [昇腾社区 9.0 beta2][cann-endpoint-hiascend]
- CANN Runtime VMM API：[GitCode][cann-vmm] /
  [昇腾社区 V2 API][cann-vmm-hiascend]
- HcclCreateSubCommConfig：[GitCode][cann-subcomm] /
  [昇腾社区 8.5][cann-subcomm-hiascend]

[worker-py]: ../python/simpler/worker.py
[remote-session]: ../python/simpler/remote_l3_session.py
[hier-runtime]: ../docs/hierarchical-level-runtime.md
[done-p0-1]: done-P0.1.md
[done-p0-2]: P0.2/done.md
[p0-2]: P0.2/done-followup.md
[p1b-abi]: worker-memory-model/p1b-corrected-design.md

[pr-1381]: https://github.com/hw-native-sys/simpler/pull/1381
[pr-1396]: https://github.com/hw-native-sys/simpler/pull/1396
[pr-1397]: https://github.com/hw-native-sys/simpler/pull/1397
[pr-1398]: https://github.com/hw-native-sys/simpler/pull/1398
[pr-1420]: https://github.com/hw-native-sys/simpler/pull/1420
[pr-1421]: https://github.com/hw-native-sys/simpler/pull/1421
[pr-1422]: https://github.com/hw-native-sys/simpler/pull/1422
[pr-1424]: https://github.com/hw-native-sys/simpler/pull/1424
[pr-1426]: https://github.com/hw-native-sys/simpler/pull/1426
[pr-1427]: https://github.com/hw-native-sys/simpler/pull/1427
[pr-1430]: https://github.com/hw-native-sys/simpler/pull/1430
[pr-1431]: https://github.com/hw-native-sys/simpler/pull/1431
[pr-1432]: https://github.com/hw-native-sys/simpler/pull/1432
[pr-1436]: https://github.com/hw-native-sys/simpler/pull/1436
[pr-1453]: https://github.com/hw-native-sys/simpler/pull/1453
[pr-1456]: https://github.com/hw-native-sys/simpler/pull/1456
[pr-1459]: https://github.com/hw-native-sys/simpler/pull/1459
[pr-1460]: https://github.com/hw-native-sys/simpler/pull/1460
[pr-1466]: https://github.com/hw-native-sys/simpler/pull/1466
[pr-1467]: https://github.com/hw-native-sys/simpler/pull/1467
[pr-1521]: https://github.com/hw-native-sys/simpler/pull/1521
[pr-1540]: https://github.com/hw-native-sys/simpler/pull/1540
[pr-1541]: https://github.com/hw-native-sys/simpler/pull/1541
[pr-1552]: https://github.com/hw-native-sys/simpler/pull/1552
[pr-1557]: https://github.com/hw-native-sys/simpler/pull/1557
[pr-1558]: https://github.com/hw-native-sys/simpler/pull/1558
[pr-1565]: https://github.com/hw-native-sys/simpler/pull/1565
[pr-1574]: https://github.com/hw-native-sys/simpler/pull/1574
[pr-1583]: https://github.com/hw-native-sys/simpler/pull/1583
[pr-1587]: https://github.com/hw-native-sys/simpler/pull/1587
[pr-1588]: https://github.com/hw-native-sys/simpler/pull/1588
[pr-1592]: https://github.com/hw-native-sys/simpler/pull/1592
[pr-1599]: https://github.com/hw-native-sys/simpler/pull/1599
[pr-1600]: https://github.com/hw-native-sys/simpler/pull/1600
[pr-1602]: https://github.com/hw-native-sys/simpler/pull/1602
[pr-1609]: https://github.com/hw-native-sys/simpler/pull/1609
[pr-1612]: https://github.com/hw-native-sys/simpler/pull/1612
[pr-1613]: https://github.com/hw-native-sys/simpler/pull/1613
[pr-1614]: https://github.com/hw-native-sys/simpler/pull/1614
[pr-1648]: https://github.com/hw-native-sys/simpler/pull/1648
[pr-1649]: https://github.com/hw-native-sys/simpler/pull/1649
[pr-1650]: https://github.com/hw-native-sys/simpler/pull/1650
[pr-1651]: https://github.com/hw-native-sys/simpler/pull/1651
[pr-1663]: https://github.com/hw-native-sys/simpler/pull/1663
[pr-1669]: https://github.com/hw-native-sys/simpler/pull/1669
[pr-1671]: https://github.com/hw-native-sys/simpler/pull/1671
[pr-1672]: https://github.com/hw-native-sys/simpler/pull/1672
[pr-1673]: https://github.com/hw-native-sys/simpler/pull/1673
[pr-1676]: https://github.com/hw-native-sys/simpler/pull/1676
[pr-1681]: https://github.com/hw-native-sys/simpler/pull/1681
[pr-1683]: https://github.com/hw-native-sys/simpler/pull/1683
[pr-1689]: https://github.com/hw-native-sys/simpler/pull/1689
[pr-1692]: https://github.com/hw-native-sys/simpler/pull/1692
[pr-1703]: https://github.com/hw-native-sys/simpler/pull/1703
[pr-1704]: https://github.com/hw-native-sys/simpler/pull/1704
[pr-1003]: https://github.com/hw-native-sys/simpler/pull/1003
[pr-1090]: https://github.com/hw-native-sys/simpler/pull/1090
[pr-1379]: https://github.com/hw-native-sys/simpler/pull/1379
[issue-824]: https://github.com/hw-native-sys/simpler/issues/824
[issue-1105]: https://github.com/hw-native-sys/simpler/issues/1105
[pr-711]: https://github.com/hw-native-sys/simpler/pull/711
[pr-866]: https://github.com/hw-native-sys/simpler/pull/866
[pr-755]: https://github.com/hw-native-sys/simpler/pull/755
[pr-803]: https://github.com/hw-native-sys/simpler/pull/803
[pr-861]: https://github.com/hw-native-sys/simpler/pull/861
[pr-1392]: https://github.com/hw-native-sys/simpler/pull/1392
[issue-303]: https://github.com/hw-native-sys/simpler/issues/303
[pr-307]: https://github.com/hw-native-sys/simpler/pull/307

[cann-hccl]: https://gitcode.com/cann/hccl/blob/master/README_en.md
[cann-rootinfo]: https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_mgr_c/HcclGetRootInfo.md
[cann-init-rootinfo]: https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_mgr_c/HcclCommInitRootInfo.md
[cann-hcomm-defs]: https://gitcode.com/cann/hcomm/blob/81ad19403b4ab6f4e7c8ce55be5a273c8dccbbdf/include/hcomm_res_defs.h
[cann-channel]: https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclChannelAcquire.md
[cann-endpoint]: https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/basic_resource_mgmt/HcommEndpointCreate.md
[cann-vmm]: https://gitcode.com/cann/runtime/blob/master/docs/zh/api_ref/11-04_virtual_memory_management.md
[cann-subcomm]: https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_mgr_c/HcclCreateSubCommConfig.md

[cann-hccl-hiascend]: https://www.hiascend.com/document/detail/zh/CANNCommunityEdition/850/commlib/hcclug/hcclug_000001.html
[cann-rootinfo-hiascend]: https://www.hiascend.com/document/detail/zh/canncommercial/850/API/hcclapiref/hcclcpp_07_0005.html
[cann-init-rootinfo-hiascend]: https://www.hiascend.com/document/detail/zh/canncommercial/850/API/hcclapiref/hcclcpp_07_0006.html
[cann-hcomm-defs-hiascend]: https://www.hiascend.com/document/detail/zh/canncommercial/850/API/hcclapiref/hcclopdevapi_07_0009.html
[cann-channel-hiascend]: https://www.hiascend.com/document/detail/zh/CANNCommunityEdition/900beta2/API/hcclapiref/hcclopdevapi_07_0054.html
[cann-endpoint-hiascend]: https://www.hiascend.com/document/detail/zh/CANNCommunityEdition/900beta2/API/hcclapiref/hcclopdevapi_07_0065.html
[cann-vmm-hiascend]: https://www.hiascend.com/document/detail/zh/CANNCommunityEdition/900beta1/API/appdevgapi/aclcppdevg_03_2006.html
[cann-subcomm-hiascend]: https://www.hiascend.com/document/detail/zh/canncommercial/850/API/hcclapiref/hcclcpp_07_0019.html
