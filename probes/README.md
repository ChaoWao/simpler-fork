# 原语探针与原始证据

当前解释、撤回与适用范围集中在 [证据索引](../kernel-mode-design.md)。本目录保存历史测量，不定义运行时方案。

- 最终 Probe L：[TQ0](../LOCAL-EVIDENCE.md#artifact-a66bd42b06a4)、[TQ1](../LOCAL-EVIDENCE.md#artifact-410caef5f522)。顶层早期报告已归档。
- Probe K：[TQ0](../LOCAL-EVIDENCE.md#artifact-0a579ac039ff)、[TQ1](../LOCAL-EVIDENCE.md#artifact-66f20318fa41)。同步 op 被拒仍可结束捕获，必须逐次检查返回值。
- Probe M：[原始 JSON](../LOCAL-EVIDENCE.md#artifact-82eb9bc4b048)。`2 s <- CI's value` 标签已撤回，原始数值不变；actual 回读不证明真实 watchdog 触发时刻。
- [RESULTS](RESULTS.md) 是累积实验说明。其中“200000 节点没有上限”只表示测到该规模；“1792 可恢复”来自独立 under_limit 臂，不是被终止的饱和臂恢复。

脚本、日志、产物与硬件基线按实验记录保留。本轮没有重跑，也没有改写原始结果。
