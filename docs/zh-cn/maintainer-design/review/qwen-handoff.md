# Qwen 跨仓目标交接到 #2658

> **发布快照：2026-11-05。** 本轮为资源与跨仓目标的跟踪交接；不代表当前 main 全量验收。源码事实按各自 SHA 阅读，资源进度查 GitHub。参见[阅读入口](../README.md)。

本地 Qwen 独立进度轨已退役。整体目标、交付和进度以 [#2658](https://github.com/hw-native-sys/simpler/issues/2658) 为入口；本文只保存旧目标与验收要求的对应关系，不维护进度，也不表示验收已完成。

## 来源与范围

2026-11-05 UTC 读取 #2658 与 #2429 正文，并对照 #2658 的 [2026-11-04 进展评论](https://github.com/hw-native-sys/simpler/issues/2658#issuecomment-6625458806)。正文与评论哈希见 [来源清单](../goal-sources.json)。本次只核归属与验收覆盖，不推进代码或硬件核验基线。评论报告的本地冻结组合不等于四仓 main 已交付；旧正文进度不能覆盖更新评论。

旧设计来源是 Qwen 集成计划（本地参考：`../others-ref/worker-async-pipeline/qwen-integration.md`） 的 2026-08-04 方案。链路为 Serving 请求调度 → Lib 模型 → PyPTO 编译与调用 → Simpler 执行。目标是让实际模型消费异步能力，证明准备与执行的有效重叠，以及正确性、有限资源、失败关闭和端到端性能。

## 旧编号与验收交接

| 旧编号 | 目标 | 唯一整体跟踪入口 | 验收要求 |
| ------ | ---- | ---------------- | -------- |
| Q1 | PyPTO 异步提交与 handle | [#2658](https://github.com/hw-native-sys/simpler/issues/2658) M0/M2 | 输出与错误只发布一次；程序、参数、配置快照及可变 frame 保活；真实模型调用实际消费接口。 |
| Q2 | token、KV 与元数据依赖链 | [#2658](https://github.com/hw-native-sys/simpler/issues/2658) M1/M2 | 连续 DEVICE feedback、请求隔离、metadata 不被提前复用；以真实参考核对输出与 KV。 |
| Q3 | Serving 与离线持续推进 | [#2658](https://github.com/hw-native-sys/simpler/issues/2658) M2 | 有限准入与背压、持续补队、FIFO、EOS/取消/失败与 close；动态 batch 和行映射切换须验证适用路径。 |
| Q4 | 四仓端到端验收 | [#2658](https://github.com/hw-native-sys/simpler/issues/2658) M1/M3/M4 | 真实 prefill/decode/持续请求；同条件正确性与性能，四仓版本与支持矩阵、最小回归和可执行复现。 |
| C2b | 跨仓 trace 关联与归因 | [#2658](https://github.com/hw-native-sys/simpler/issues/2658) M3 | request、token/step、dispatch、run/slot/generation 与 code image 身份可关联；证明实际准备重叠、执行顺序与设备空隙，不拿单仓时间替代整体验收。 |

## 保留的具体场景与方案边界

\#2658 正文明确承接 M0–M4 总体合同；旧计划以下细节是交接时保留的验收场景，尚不表示负责人已经逐项确认了新一轮支持范围：

- 在线与离线生成的语义一致；若范围不同，在支持矩阵中明确。
- 稳定 batch、行映射改变、chunked prefill、prefix cache、EOS、abort、preemption 分别明确测试或支持边界；不因短 decode 成功外推。
- 前驱失败或未静止时，不允许有依赖的后继错误执行；资源按最后消费者回收，背压先于复用可变参数。
- trace 关联须能将用户请求与实际执行、代码身份对上；C2b 的目标是可验证归因，不强制新增某种公共 trace API。

固定两帧、最多一个预备后继、Token-A/B、具体 DecodeHandle/session API 和唯一 progress owner 等，属于旧方案的实现选择。当前要求是有限资源、正确依赖、可证明重叠与安全退出；容量、API 和调度机制按现有合同与场景核定，不以旧方案形状重新制造待办。旧目标中的完全隐藏准备是待验证性能目标，不能当作现有事实。

## 相关入口与责任边界

- #2658 负责真实模型 Async 组合、四仓验收、系统性能与回归；原有各仓实现负责人不因此改变。
- [#2429](https://github.com/hw-native-sys/simpler/issues/2429) 在 [#2666](https://github.com/hw-native-sys/simpler/issues/2666) 下负责 vLLM 消费者接入，向 #2658 提供产物与证据；不继续作为 Qwen 整体验收的重复入口。
- EX-7 保留程序 admission/early-enqueue 的基础合同；#2666 保留执行资源及可选 kernel/capture 合同。组合验收不接管这些基础能力的实施进度，也不等待整个 capture track 关闭。
- 原 V1、VLLM 系列的分项范围按 [work-items §7](../work-items.md#7-既定范围之外的工作) 和原来源保留，不把 adapter/capture 实现混入 Q1–Q4。

历史证据和旧编号用于追溯；本文没有完成率、排期或独立状态栏。
