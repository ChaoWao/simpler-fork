# L3 eager init 与 L2 启动

> **发布快照：2026-10-15。** 设计、实现状态与历史调查须按正文各自基线阅读；不代表当前 main 全量验收。参见[阅读入口与已知更正](../README.md)。
>
> 来源：原 `l3l4-cluster-requirements-analysis.md` §C + §C.1。
>
> 状态图例：**已实现** = 已在 main 落地并可用；**部分实现** = 代码在但仅 sim / 未 onboard 验证，或只覆盖部分层；**设计已存在未落地** = 有设计文档 / OPEN PR 但未合并；**未实现** = 代码与设计均缺。

L3 改造的三个子诉求分别落在不同的成熟度：**C.1 已由 #1397（merged `483d97e3`）落地**（下方 C.1 章为交付前调查记录，见其章首 banner），C.2 是一次接口消歧与收敛（无单一 PR，需围绕已统一基座做），C.3 大部分已到位（钉 worker 是 L3 主流写法）、真正缺的是把"自由调度 + 自动堆分配"降级为可选/移除。下面逐条对齐。

---

## C.1 L3 创建即拉起 L2（eager init）

> **✅ 已落地（2026-07-19）：eager init 由 #1397（merged `483d97e3`）交付，取代
> #1381（superseded）。当前状态见 (c) 差距 0。以下 (a)–(d) 保留为**交付前的调查与
> 落地路径记录**（基线 main@8fba2321+#1396）——其中"main 仍 lazy""采纳 #1381"
> 等表述描述的是 #1397 合并前的状态，不再是当前事实。生命周期加固转
> P0.2.md（本地参考：`../P0.2/done-followup.md`）。**

### C.1(a) ⚠️ **pre-#1397 快照** —— "lazy 在 l3 run 时初始化"的精确位置

> #### 本小节描述的两级懒加载已被 #1397（merged `483d97e3`）整体移除
>
> 当前 main 是 **eager 递归 `init()`**：`init()` 内直接 `_init_hierarchical()` →
> `_start_hierarchical()`，递归 `inner.init(_startup_deadline=…)`，**INIT_READY 在整棵
> 子树 ready 之后才上传**，5 个 lazy trigger 已删除。交付记录见 done-P0.1.md（本地参考：`../done-P0.1.md`）。
> 下方原文**仅作改造前的现状记录**，勿据此判断当前行为、更勿据此实现。

改造前（pre-#1397）main 是**两级懒加载**，L3 的 L2/子 worker 直到首次 `run()` 才真正上电：

- **`Worker.__init__`**（`python/simpler/worker.py:1733`）：纯 Python 对象，无 fork、无 C++ 引擎，`_hierarchical_start_state = "not_started"`。
- **`Worker.init()`**（`worker.py:3243`）→ level>=3 走 `_init_hierarchical()`（`worker.py:3302`）：只做四件事，**全部无 fork** —— 为 sub/chip/next-level 分配 `MAILBOX_SIZE` 的 `SharedMemory`；解析 L3 运行时二进制存入 `self._l3_bins`；在 fork 前构造 C++ `_Worker(level)`（其 ctor 在 `src/common/hierarchical/worker.cpp:55` 里 `mmap` HeapRing 为 `MAP_SHARED`，使 fork 子进程按同一 VA 继承，此处不起任何 C++ 线程）；打开 remote L3 会话。C++ 侧"构造与 init 分离"的不变量注释见 `worker.h:33-38`。
- **首次 `Worker.run()`**（`worker.py:4506`）→ **`worker.py:4535` 的 `self._start_hierarchical()`** —— **这就是"lazy 在 l3 run 时初始化"的确切触发点**。`_start_hierarchical()`（`worker.py:3385`）才依次：`os.fork()` 出 sub / chip / next-level 子进程（chip 子进程内 `ChipWorker().init(...)` + 发 `_INIT_READY`/`_INIT_FAILED`，入口在 `worker.py:1538`）→ 父侧 `_await_children_ready` **有界屏障**（deadline + `waitpid`，#1396；仍兼顾 issue **#897** stream-sync 偏斜级联）→ `dw.init()`（`worker.cpp:94`：起 C++ Scheduler + WorkerThreads，**必须在所有 fork 之后**）→ `control_prepare` 广播完成 ChipCallable H2D 上传。

除 `run` 外还有 **4 个懒触发点**（任一在首次 run 前被调用都会提前拉起整棵树）：`create_host_buffer`（`worker.py:4251`）、remote require/register/unregister（`worker.py:1990 / 2572 / 3101`）。

**嵌套 L3-under-L4 更晚**：L4 的 `_start_hierarchical` fork 出 L3 子进程后，子进程内 `inner_worker.init()` 只跑 `_init_hierarchical`（分配 shm + 构造 C++ `_Worker`，不 fork 芯片），随后进入 `_child_worker_loop`（`worker.py:1616`）。L3 自己的芯片要等 **L4→L3 的第一个任务派发**：`_child_worker_loop` 收到 `_TASK_READY` 才调 `inner_worker.run(...)`（`worker.py:1648`）→ `_start_hierarchical()` 才 fork L3 芯片。即今天 L3-under-L4 的 L2 在"首个 L4→L3 任务"时上电，而非 L4.init() 或 L3-child 创建时。

**结论（pre-#1397 快照，2026-07 上旬）**：C.1 要求的"L3 创建时就把 L2 拉起"，**在当时的 main 上未实现**。⚠️ **这句不是当前事实** —— #1397（`483d97e3`）已交付 eager init。

### C.1(b) 相关 Issue/PR

- **#1381（已 CLOSED 2026-07-19，superseded by #1397；下为提案时描述，仅存作调查记录）**（branch `eager-l2-worker-init`，`worker.py` 净 +48/-28）—— **C.1 的 eager 触发点迁移 patch（非完整 eager lifecycle）**。做法：把 `_start_hierarchical()` 调用点收敛到 `init()` 的 `_init_hierarchical()` 之后（失败由 `_cleanup_partial_init` 回收，先 `close()` C++ `_Worker` 停调度器再杀子进程），使 L3+ 在 `init()` 返回时整棵 L2 已就绪（fork、`ChipWorker.init`、ChipCallable H2D、prewarm 全完成）；删除各懒触发点（原 `worker.py:1990/2572/3101/4251` + `run()` 内的调用）——**PR head 当前 `run()` 内已不再调用，只剩 `init()` 中的一处**（故文中"run() 保留幂等 no-op"的旧说法不适用）。行为变化：init()~run() 间的 register 改走 post-start 广播；无子进程的 L3 在 init() 后 register 一个 Python fn 会**立即抛错而非静默快照**（移除"init 完成但未 started"窗口）；附带一处 `child_memory` tensor blob-rewrite 修正、`scene_test.py`（+114/-48）与三处 doc。**仅 Python，无 C++/ABI 改动**（C++ 侧构造/init 已是分离两阶段）。**自述 onboard/sim st 未本地验证**（fork+OpenMP 在 main 上同样卡死，依赖 CI）——即**设计已就绪、未合并、未 onboard 验证**，且当前 **open / `mergeable_state=dirty`（与 main 冲突，需 rebase）**。**⚠ #1381 只把 eager 触发点迁移，并不构成完整 eager lifecycle（缺 INIT_FAILED / 超时 / liveness / 递归 READY / rollback，见 (c) 差距 0）。**
- **#1003 ✅ MERGED（2026-07-25）** —— 修 #980：L3 chip child 在发 `TASK_DONE` 前退出时父进程无限轮询挂死。把 child PID 传入 C++ `WorkerThread`，等待时检测并 reap child 退出、抛清晰 `RuntimeError`。它已经是 C.1 当前健壮性基线；#1381 则已 CLOSED/superseded，不再存在“一并落地”的动作。
- 已合并的 eager 铺垫 **#1322 / #864 / #613 / #1234 / #1193**（全 merged）—— 均在**设备/arena 层**做预热（prewarm_config 机制、持久化 stream、chip bootstrap 编排、常驻 prebuilt runtime arena 复用），**没有一个把 `os.fork()` 提前**；优化的是芯片上电"之后"的冷启动成本，与 C.1 的 fork 提前**正交**。
- **#583 / #605 merged** —— L4+ 递归 Worker 组合（`_child_worker_loop` 基础）与 L4 错误传播，是 eager 改造会触及的进程模型底座。
- **#824 open** —— 跨 `Worker.run` 的持久化通信域，与 run 生命周期/eager 相关的持久化诉求。

### C.1(c) 差距

1. **[P0 — ✅ 已解决] eager init 已由 #1397 落地（merged `483d97e3`，2026-07-19），前置启动失败协议由 #1396（`35b76c53`）提供。** #1396：`_INIT_READY=6`/`_INIT_FAILED=7` + `_await_children_ready`（deadline + `waitpid(WNOHANG)` + 失败传播）+ `_abort_hierarchical` 回滚，覆盖 sub/chip/next-level 三 edge，统一 `_forked_child_main`。#1397 在其上完成 **eager**：`init()` 内 `_init_hierarchical()`→`_start_hierarchical()`、递归 `inner.init(_startup_deadline=…)` 让 INIT_READY 在整棵子树 ready 后才上传、删除 5 个 lazy trigger、单一 startup epoch deadline、`_abort_hierarchical` 扩为 SIGTERM 协作取消 + `killpg` 子树回收 + READY rescan、chip prewarm 前移到 INIT_READY 之前、`child_memory` host-rewrite guard、direct-L3 SceneTest rehost。**剩余（非本 feature）**：① 生命周期加固——**#1398（merged `0ffb88f8`）已交付**：三标志归并为单一权威 5-state 状态机、`close()`×INITIALIZING **fail-fast**、eligible-target 校验、L2 两并发 init（第二个观察到 INITIALIZING **立即报错**，非串行等待）（见 done-P0.2.md（本地参考：`../P0.2/done.md`））；~~仍 deferred~~ **✅ 全部已交付（2026-08-04）** —— 曾 deferred 的五项：**cooperative init cancellation / shared close transaction**（#1673 `b7141748`）、**register/unregister-vs-close admission**（#1663 `115d7c4d` ＋ #1671 `d95514b4`）、**递归资源账本**（#1673 ＋ #1689 `f8106e24`）、**`add_worker` freeze** 与 **ChipWorker gate**（均 #1689）（交付记录见 `P0.2/done-followup.md`（本地参考：`../P0.2/done-followup.md`），八条退出条件全绿）；② post-READY liveness ✅ **已由 #1003 交付（merged 2026-07-25）**；③ sim-remote 的 CLEANUP_ACK/lease/epoch 按 roadmap 细分 **P0.3 sim startup correctness / P2 production transaction / P3 heterogeneous**。

> **注：差距 1–4 = #1397 前的历史差距，已随 #1397（`483d97e3`）合并消解——当前
> 状态以差距 0 为准，以下仅存作调查记录。**

1. ⚠️ **pre-#1397 快照**：当时 C.1 尚未在 main 落地，实现它的 #1381 尚 OPEN 且未本地验证（fork+OpenMP 卡死问题依赖 CI 排除）。**今天两者都已终结** —— #1381 CLOSED（superseded），C.1 由 #1397 `483d97e3` 落地。
2. #1381 引入**行为回归面**：init()~run() 间对无子进程 L3 的 Python fn register 由"静默快照"改为"立即抛错"，且改了 `scene_test.py` +114 —— 采纳前需与 Codegen owner 核对现有 scene test / 动态 register 用户是否依赖旧的"init 完成未 started"窗口。
3. **eager fork 与 eager 预热是两件正交的事**：只提前 fork（#1381）而不预热 arena（#1322/#864），首个 run 仍付冷构建成本；两者叠加才真正"init 返回即整树可派发"。
4. **A.1 交互未闭环**：即便采纳 #1381，L4 fork L3 子进程仍在 L4 首次 run（经同一 init()→_start_hierarchical 路径提前）；remote L3 会话在 `_init_hierarchical`（`worker.py:3355-3378`）打开——L4 侧 eager 拉起本地/remote L3 的端到端路径需与 remote/comm 梳理合并结论，不在 C.1 单独闭合。

### C.1(d) 历史建议及当前处置

1. **阶段一（当时的方案：采纳 #1381 —— 启动失败协议前置已由 #1396 满足）** ⚠️ **未按此执行**：最终由 #1397 取代 #1381：在 `Worker.init()` 的 `_init_hierarchical()` 之后调用 `_start_hierarchical()`，删除 run 前懒触发点。改动集中在 `python/simpler/worker.py`，**不需动 C++**。采纳后：独立 L3 芯片在 `L3.init()` 上电；嵌套 L3-under-L4 因 `inner_worker.init()` 会自身调 `_start_hierarchical`，L3 芯片在"L4 fork 出 L3 子进程并 init"那一刻上电——与 C.1 一致。**#1396 的 `_await_children_ready` 已是递归 plumbing：eager 后 INIT_READY 会在整棵子树 ready 后才上传，无需改协议。#1381 需 rebase 到含 #1396 的 main。**
2. **阶段二（TASK 阶段 child-exit，#1003 ✅ merged 2026-07-25）**：#1396 只覆盖 **init 窗口**；child 发 INIT_READY 后再死属 TASK 阶段，由 #1003 覆盖（把 child PID 传入 C++ `WorkerThread` 检测 dispatch 期 exit）。
3. **阶段三（补验证）**：按 running-onboard 规则用 `task-submit` 锁卡跑 `tests/st` 的 L3/L4 用例 + `tests/ut/py/test_worker/{test_host_worker,test_l4_recursive,test_error_propagation}`，确认 eager 后 fork 顺序不变量（Worker/HeapRing 先于 fork、`dw.init()` 后于 fork）不破。
4. **阶段四（行为回归评估）**：核对现有 scene test 是否依赖旧的"init 完成未 started"窗口；把 eager fork 与已合并的 eager 预热视为一套采纳。
5. **A.1 挂钩**：L4 侧 eager 拉起本地/remote L3 的结论留到 remote/comm 章节合并，本节只保证"L3 一旦创建即拉起其 L2"。

---
