# L2 RunHandle context 拒绝边界

基线 main `d793c89c`（#2578 已合入），单个提交 `bfd4f923`。

## 本次范围

共同 ChipWorker validator 暴露为内部 binding。每个 direct L2 RunHandle 保存该 validator，wait/result 在 native 执行异常被转为终态之前校验。拒绝走现有 waiter election unwind，不删除 run id、参数快照或 source uses；恢复 context 后可重试。真正设备失败仍是终态并保守保留未证明结束的 Buffer 引用。终态清理 callback。

这是公开借用 Worker 的前置修正，**没有开放公开 borrowed init 或 submit(caller_stream=...)**，没有消费 context_generation，没有交付 capture。没有新 Worker、Tensor、PreparedCall 或 executor。

## 验证

- Python 完整 UT：3206 passed / 91 skipped，135.49s。针对性 381 项另过。
- C++：317/317 targets passed；native C++ 执行器未改，重建的是 binding。
- L2 sim：a2a3sim 6 项、a5sim 3 项均通过（--manual include；开始 --manual exclude 只有 malloc 臂，不将它冒称执行覆盖）。
- task-submit `task_20261014_025206_27970816737`，device 4：44 passed / 27 deselected，173.17s。包含 TMR/HBG × owned/borrowed × depth1/2 的 caller 前后顺序、生命周期和拒绝。新增检查把真实 native handle 接到共享 Python RunHandle：切错 current context 后 wait/result 拒绝且不消耗 handle；恢复后完成。该桥接不是完整公开 Worker.submit 路径验收。
- source Buffer 保活由生产 `_submit_l2_locked` + fake native 的 UT 独立验证：连续两次拒绝不终结、不释放引用、不 poison；正确 context 完成后引用释放。真实设备执行异常控制臂仍终态，source 未证明结束引用不释放。
- 负对照在独立进程用 AST 删除 wait 的 preflight，不改正在板测的文件/二进制，wait/result 两项均失败（提前终态）。negative.py / negative.log。
- pre-commit 全通过；最终提交重建与针对性复核另记。

caller/ascend 和 caller/occupancy.log 保留独立设备日志及持续占用记录。采用主线 DFX，未添加 overlay；无本轮故障，不宣布 AIV 已修复。A5 实板本机不可用，由 CI 验证。

## 后续确定范围

下一份代码接 direct L2 Worker 同一 init/submit 的借用环境与 caller stream、统一 Buffer/TaskArgs。需在 Python 状态变更前拒绝错误 context/capture/H2D，close 保留可重试 teardown debt；context generation 必须实际关联 source/调用环境，不与 Buffer/slot generation 混同。完整 eager 验收之后再做 capture/replay。

## 最终提交与 PR

[#2580](https://github.com/hw-native-sys/simpler/pull/2580)，单个提交 `bfd4f923`，基线 `d793c89c`，0 behind。最终提交重建后 357 项相关 Python UT 通过（19.90s）；全量、板测归属于提交前相同代码，不移挂为另跑一次的最终 head sweep。规划材料未进入 commit。
