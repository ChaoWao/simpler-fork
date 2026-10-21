> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# P3 交付核查与 P4/P5 交接

核查时间 2026-09-21T03:20:44Z；代码基线 main `857350519c84b1f32d93b5e81d2a0611ac77e4c1`。GitHub 当前 #2316 仍 OPEN，正文待办未随合入更新，评论已关联 PR2 #2399、PR3 #2406。本次只读检查代码和既有验证记录，没有运行测试、发布评论或关闭 issue。

## 结论

P3 的三类合同/审计/回归交付均已合入，实际涉及四个 PR。未发现还需要为当前 P3 补开的生产修复。剩余是 issue 验收登记，以及 P4/P5 改变准入后承担的集成重验；不能把后者说成 P3 已验证了多 launched。

| 交付 | 已合入证据 | 完成内容 |
| --- | --- | --- |
| 父 run FIFO | #2314 `0a68792ed` | ready 分区消失/重建仍按 RunId 选择；后继可 staging，但前驱 terminal 前不能激活；构图重入拒绝。controlled endpoint 验证顺序，不冒充算术验证。 |
| build 所有权与 job 寿命 | #2356 `d17842db5` | 正常/异常入口先等 recorder 完整返回及 captures 析构，再释放借用 build 状态。支持入口/唯一 SO 注册/同一 worker prepare 边界已有源码审计；不新增并发 unregister 支持。 |
| 独立 build 等待隔离 | #2399 `13e9e206f` | 按 RuntimeContext 统计 queued+running jobs，bind 只等自己；捕获对象销毁后才减计数，保留嵌套 recording 与全池 shutdown。修正 #2356 原先安全但过宽的整池等待。 |
| host 输入就绪 | #2356 基础 + #2406 `857350519` | host/child-memory 就绪输入、显式 wait/copyback 后读前驱结果、get/set 与当前图 producer/重叠 writer 拒绝、独立/共享只读输入提前准备。#2406 补真实 compiler/Worker.submit/RunHandle 公开路径。 |

## 逐项验收

1. 父 run 术语与 scoped queue：#2314 文档和回归已合入。当前 main 仍保留 terminal-gated 执行；#2404 增加逻辑 pending，不等于多设备执行。
2. build ownership：main docs/task-flow.md 已列 Worker.submit、ChipWorker prepare、runner reservation、unique temporary SO + RTLD_LOCAL、entry guard。main 两架构 guard 仍调用 graph_record_wait(rt)，共享实现转发 pool.wait(rt)；#2399 进一步消除跨 owner 等待。
3. 输入合同：main test_hbg_bind_ledger.cpp 的 HbgHostAccessContractTest 覆盖真实 bind/accessor/copyback；#2406 public readiness 场景明确 producer.wait 后 consumer.submit/result，FIFO 场景分别验证独立/只读输入后继提前 prepared 且不能提前执行。
4. Group/SUB/requeue/direct control：保留既有覆盖与当前 gate；#2314 scoped scheduler 回归、#2406 FIFO/DMA-control 场景提供现有语义证据。改变 gate 时必须重新检查，不能宣称现有测试证明将来多 launched。
5. 验证证据：#2356 本地8项×两架构及归档报告；#2399/#2406 原作者 PR body 中有基线、主机/sim/A3硬件结果，manager 固定 head 评审与 CI 完成记录另存。未复跑。A5 本地硬件未跑，不能把 a5sim 或主机编译当硬件证明；旧 AIV 偶发原因未查明。远端原始日志/task ID 未在本次全部重新获取，因此不能声称 issue 的“每项原始日志与任务号齐全”已独立验讫。
6. P4/P5 交接：以下明确记录到 manager 路线和 P4 任务依据；GitHub #1853/#2316 的外部登记尚未由本次修改。

## P4/P5 改准入时必须携带的检查

- N 的全部 dispatch 按序提交后，才允许 N+1 的 dispatch；submission_closed 只是声明结束，ACCEPTED 只是入队，均不能单独证明该边界。覆盖 pending 尾任务、ready 分区重建、group/SUB、requeue、异常与终态清理。
- 同一 loaded SO 的 RuntimeContext binding 保持到本 build 全部 recorder 和 captures 结束；并发 prepare 门变更不能破坏它。独立注册的 module 与单 build 内 Graph 并行不能被粗粒度锁压掉。
- Host accessor 不由设备 stream 排序保护。使用前驱生成值必须先等值就绪与必要回拷；独立/共享只读输入仍可提前准备，别名与 staging 原始存储仍遵守调用方合同。
- direct-control 的 malloc/free/copy 与 request/frame/lease 最后读者仍需保留；不以设备 kernel 结束代替 host 输出完成或资源退休。

## 管理状态修正

此前 Track A Markdown 仍把 PR2/PR3 写作待交付，HTML 把全部工作归成 #2314/#2356，遗漏 #2399/#2406；这是看板/登记未对齐。已更新本地路线、DAG 与本报告。P3 原实施 worktree 已归档，保持退役，不新起重复 writer。外部 issue 仍 OPEN，需对齐验收清单与 #1853 交接登记后再正式关闭。
