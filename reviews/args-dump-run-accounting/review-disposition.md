# #2671 独立审阅处置

审阅固定 head `7448ed4e5`，base `2522f02bf`；真实 Claude CLI、独立只读 worktree，无其他 CLI/硬件审阅。首个 CLI 请求只返回无法继续，没有审阅结论；随后标准只读 review 请求完成。不能把首个请求当通过。

- **F1**：成立。Host collector 可包含不启用的尾部 lane，epoch 不匹配不能无条件报 ERROR。新 UT 在 host2/device1 布局复现错误，已修复；全量386/386重新通过；已有 stale 非空 lane 的拒绝仍须保留。
- **F2**：底层普通恢复不消费 stop-proof 是既有缺口；本笔注释应明确这是计算式依赖且当前未检查的前置条件。已修改注释/产品文档，不宣称修复底层边界。
- **F3**：已记录独立后续。成功保有的记录可 best-effort 输出，但 unread producer loss 不应被解释为已证明0；后续 manifest 需要不完整标记。
- **F4**：CI 日志已读取。HBG sustained refill 输出正确，但仅观察到两条不相连的重叠边；本地不同失败为 TMR 首次 prepare 的驱动映射13/errno16。分别定位，main/candidate 正常资源交替16项通过；独立未改main在2核affinity复现字节一致的链式断言，4核通过。此为既有测试host时间窗口假设的证据，不宣称测试已修复。
- **F5**：字段/断言说明随同修正；ordinary 的 uint64 headroom 与 retained 的差异保留原限定，不宣称全路径预检。

完整初审：[Claude 原文](claude-review-7448ed4e5.md)。最终 head `2894ac39c` 的[Claude 复核](claude-recheck-2894ac39c.md) **approve code，等待 CI**，无剩余 introduced Must/Should；不是 CI/合入签收。新小建议是 free TEST 的 ASSERT 失败会绕过全局状态清理；当前行为断言使用 EXPECT，结构前置断言风险窄，作为测试清理后续记录，没有追加产品范围。最终上板与CI仍在进行。


<a id="device-mapping"></a>

## NPU 表映射核对

Claude 复核把 NPU1/chip1 的pid2711509说成了冲突进程；它映射 physical3，实际不构成本例设备1的冲突证据。board 表的 NPU0/chip1 才映射 physical1，故障附近该行有pid2693254/43GiB，且不是本次子进程。这个推断采用真实 Phy-ID 映射，不按 process 表的 NPU 第一列直接当逻辑device。raw snapshot 和两份解释均保留。不能仅因存在其他进程，就证明它导致了Map errno16；本轮未复现AIV。
