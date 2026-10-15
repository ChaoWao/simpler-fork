# 单枚举参数合同：本地验证

2026-09-24，PR #2444 head `1761d1304eec8b1ea7836d8d2f6812acbc5b2498`，base `bef53236679a0ff56d38abbf38d5a738f40e530c`。本地规划/验证记录，不进代码 PR。

## 实现

HOST / HOST_TO_DEVICE / DEVICE 使用既有 address_space 字段；memory_kind 是构造入口。HBG host-only access 与 device operands 分开；删除 accessor 的跨侧映射与自动复制；TMR 拒绝 HOST entry。两侧都消费时传独立 tensor。Program 的默认 IO、按方向输出回拷保留。原 KernelCallBindings / KernelCallArgs 模块撤回，未进入最终提交。

既有 attention、batch attention、GEMM、readiness、FIFO、layout 输出、Qwen 与 DeepSeek 参数声明已迁移。大型模型和硬件 FIFO 仅做代码迁移，不声称设备验收。

## 证据

| 检查 | 结果 | 本地日志 |
| --- | --- | --- |
| Native/Python rebuild | 成功 | /tmp/strict-memory-final-install.log |
| 全量 C++ 非硬件 CTest | 251 targets 全通过 | /tmp/strict-memory-final-ctest.log |
| 全量 Python UT | 2690 passed, 54 skipped | /tmp/strict-memory-final-python.log |
| 最终 HBG/TMR bind tests | 两架构 4 targets 全通过 | /tmp/strict-memory-final-gates.log |
| A2/A3 默认 sim sweep | HBG 14 passed/7 skipped，TMR 31 passed/1 skipped；resource 43 pass + 1 fail | /tmp/strict-memory-full-a2a3sim.log |
| 上项旧枚举断言修正 | TestL3CreateBuffer 重跑通过；未声称原全量 sweep 全绿 | /tmp/strict-memory-fixed-l3.log |
| A2/A3 readiness + attention | 4 readiness jobs + 2 HBG scene classes 通过 | /tmp/strict-memory-a2a3sim.log |
| A5 readiness + attention/TMR | 4 readiness jobs + HBG/TMR 各一 scene class 通过 | /tmp/strict-memory-a5sim.log |
| 真实 L3→L2 HOST/device 独立性 | 两架构通过；HOST 5→8，独立 device 7 不变，device 输出15 | /tmp/strict-memory-l3-host.log；/tmp/strict-memory-l3-host-a5.log |
| GEMM 双参数小场景 | Golden 通过 | /tmp/strict-memory-bgemm.log |
| A5 batch attention 双参数小场景 | Golden 通过 | /tmp/strict-memory-a5-batch.log |
| HOST device-operand gate 负对照 | 删除 gate 两架构 exit1；恢复均 exit0 | /tmp/strict-memory-negative-control.log |
| pre-commit | 全部适用检查通过 | /tmp/strict-memory-hooks-ready.log |
| HTML | jsdom 6 种访问组合通过，无外部脚本；未做浏览器截图验收 | /tmp/check-strict-argument-html.cjs |

本机 npu-smi 查询成功，但没有 HBM usage 为零的设备，本次未执行 onboard。A5 真机、设备 FIFO 和大模型验收仍需设备/CI。

## 未由本次解决

- Program strided transfer 的 nbytes 与 reachable span 差异，仍记录在本地 KNOWN_ISSUES.md。
- 原 #2444 head 上 A5 retained-result overlap 观察未分类，此次没有用本地 sim 结果关闭它。
- Kernel eager 接线、HOST 值快照与 capture/replay 不是本次交付。

PR 只含代码、测试与产品文档；本文件、HTML、ROADMAP 均保留仓库外。
