# PR 2561 首轮 CI 核查

head `98e4da4f`，run `38358457015`。

- A3 UT `115167871443`：`test_onboard_a_context_that_asked_for_nothing_is_managed[host_build_graph]` 在 device 9 初始化时 `BootstrapDispatcher` sync 返回 507018。
- A3 ordinary ST `115167871524`：第一个失败及后续大量失败均指向 device 9 bootstrap/init，而非 run cleanup。
- A3 DeepSeek `115167871448`：device 9 相同 bootstrap/init 507018。
- A5 ST `115167871486`：task-submit 1800 秒未取得设备；日志明确取消仍排队的任务，没有执行测试。
- macOS UT `115167871480`：3026 Python pass，313 C++ 目标 pass；check annotation 明确 job exceeded maximum execution time 20m0s，最终 CANCELLED。
- A5 UT 当时还在 GitHub queue。其余检查通过。

A3 原因未定：507018 不等于 AIV，也不能从这些 host 日志认定硬件损坏或抢占。需要 device 9 原始设备日志和当时占用；该 run 的 artifact 列表仅有 network1 现场，不能用它代替三个失败作业的现场。修复行在 cleanup，初始化失败未到达该行。尚不把“未进入改动路径”升级成具体根因结论。

对已分类的 macOS 总时限/A5 资源队列失败申请定向 rerun；A3 不盲目同机重跑，不改业务代码掩盖初始化错误。

定向重跑结果：macOS UT 和 A5 ST 的 jobs rerun API 均返回 HTTP 403：`The workflow run containing this job is already running`。A5 UT 仍在队列，重跑尚未启动；未取消该 workflow。

## Full-log follow-up

Read all three A3 job logs (UT 520 lines; ordinary ST 11997; DeepSeek 1048). Counted 39 explicit bootstrap/fault callback records: device 9 throughout, task 0, error 507018; stream 46 in 37 records and stream 45 in 2. Stream IDs are not a global identity across processes. Three task-timing failure groups lack the bootstrap print but explicitly fail at simpler_init with 507018. The visible-device communication failure prints only nested runner rc=1 (devices 9/10), so its mechanism remains individually unclassified. Do not describe every ST failure as directly proven bootstrap.

No error detail, scheduler subclass, AIV exception, HandleTaskTimeout, or report_deadlock signature was present in these logs. The exact reviewed workflows (_ut-npu-a2a3.yml, _st-npu-a2a3.yml, _st-deepseek-a2a3.yml) do not upload device logs. Rechecked artifacts API: only network1-ci-38358457015-1. Direct read attempt with `ssh -o BatchMode=yes -o ConnectTimeout=8 infra-gpu-npu-021 ...` failed DNS resolution. This is the current runner machine, not the earlier mutil-A3-125-1 incident.

Each CI hardware job submits via task-submit, with a logged task ID. Historical out-of-queue processes cannot be excluded: no occupancy snapshot was uploaded. Need this runner's reachable address or its device-9 logs to proceed to device root cause; changing runtime behavior based on host code 507018 alone is unsupported.
