> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# HBG background rev3 manager review

2026-09-30；当前 main 9b283ab1e。设计接受，可向用户提交一个 PR 的具体合同；不是实现授权或代码 Ready。

已实读原作者 770 行方案和原调用路径，rev2 两项阻塞均闭合：
- 操作 lease 在 take/错误状态/写文件之前取得；finish 关闭准入并等待已有 lease，析构覆盖未调用 finish 的 init 失败路径。同步回退先登记，先记录 verdict 再解除登记，flush 不会漏掉已提交回退的错误。
- 输出路径固定容量且超长拒绝；后台五 vector 按 capacity 计费，固定路径/暂存计入；撤回“预算限制所有构图内存”的错误说法。

保留副作用已进入中文实施合同：预算/队列满同步写盘、默认末尾写盘检查、opt-in 不覆盖旧文件、主机构图完成即可输出并不证明设备执行成功。implementation review 须核所有异常出口的 lease 释放和错误保存，不能以 noexcept 注释替代异常处理。

2447 合入只改变 Qwen 验证，无本方案生产代码覆盖；2487 的参数载体与本方案图导出归属不同，共享 c_api/runner 文件仅属集成风险。没有运行测试。
