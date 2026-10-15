# Descriptor：#2353 后的本地迁移与验证

**后续状态（2026-09-19）：** #2358 已合入 `4e14364f`；PR head `7220dd01` 的两架构 onboard、sim、UT CI 通过。下文保留原本地实验的提交和范围。评审明确此改动为准备性 seam：生产 prepare/publish 相邻，快照保护中间 host 修改的性质当前只由单测使用；后续窗口开放须另行落实 pending 与 launch 准入合同。

日期：2026-09-18。基线 main `0dbf12ae`（#2353），包含 #2326 / #2346 的 DFX 发布修复。本地分支 `local/kernel-descriptor-main`；worktree 为 simpler 的 `.claude/worktrees/kernel-mode-s2-main`。本轮不 push、不创建 PR。

## 改动和原分支的关系

原 `fd5bd5a1` 迁移为 `ad767365`，无冲突；补测试和注释后整理为本地提交 **`e6cd39d4`**。原 HBG 改动已通过 #2353 合入，当前 diff 不再包含 HBG publication 实现。`local/kernel-mode-s2-main` 保留原双提交栈供追溯。

- `prepare_runtime_args` 检查／分配 slot 目的空间，保存本次 device descriptor 的独立 host 字节快照；准备时不复制到 device。
- `publish_runtime_args` 同步复制并消费源；错误原样返回，清空本次 `args.runtime_args`，slot allocation 保留给其原 owner。
- 两架构 onboard 在 affinity、orchestration SO metadata 确定后调用 prepare / publish，位置与旧直接 H2D 相同。
- Late DFX arm 仍写 KernelArgs，保留主线 collector 的逐项 copy 错误检查；没有把这些动态字段误当成 Runtime snapshot 的一部分。
- 新增生产 helper 的 CPU 桩测试：编译真实 `device_runner_helpers.cpp` 和 onboard `MemoryAllocator`，仅替换 RTS/ACL 函数。
- 顺手修正 `PersistentKernelArgs` 的旧注释：HBG 和 TMR 都只上传 device descriptor，HBG 不再上传整个 Runtime。

这不是 PreparedCall，也没有重复 submit 或自动缓存。每次 prepare / publish 服务本次调用。独立 snapshot 不保活其内指针指向的 allocation；既有 slot、callable、DFX owner 仍承担寿命责任。

## 审阅

**Standards：**迁移 diff 无新增规范问题。发现的既有 descriptor 大小注释已修正。

**Spec：**无实现错误或越界功能。初审指出原测试只覆盖 `RuntimeLaunchImage`，没有执行生产 `KernelArgsHelper` 的分配／发布失败和 slot 所有权；本轮补齐 6 × 2 个用例并运行通过。

新增用例覆盖：prepare 不 H2D、后续 host 修改不影响本次快照、publish 消费一次、copy 错误清空 run view 而不释放 slot、fresh prepare 复用目的空间、分配失败、slot 大小不匹配、release、move 后原对象释放。

## 验证

验证在上述 worktree 的独立 `.venv`、重新构建的产物上完成。具体提交、源码 diff 和原始日志保存在本页对应目录。

| 验证 | 结果 | 证据 |
| --- | --- | --- |
| C++ 全部无硬件 targets | **163/163** 通过 | [日志](../LOCAL-EVIDENCE.md#artifact-e148d70fcd76) |
| 实际 KernelArgsHelper + allocator | **6 × 2 = 12** 用例通过 | [逐例日志](../LOCAL-EVIDENCE.md#artifact-0d23e87bd7da) |
| Helper ASan + leak detection | 同样 **12/12**，无错误 | [日志](../LOCAL-EVIDENCE.md#artifact-c2bd6f904c0c) |
| A2/A3 sim program 回归 | resource **40/40**；HBG L2 **13 passed / 7 skipped**；TMR L2 **30 passed / 1 skipped** | [日志](../LOCAL-EVIDENCE.md#artifact-d8e85f1ddf30) |
| A5 sim program 回归 | resource **36/36**；HBG L2 **14 passed**；TMR L2 **29 passed** | [日志](../LOCAL-EVIDENCE.md#artifact-1f87ce20f03c) |
| A2/A3 onboard 正常 sweep | resource **73/73**；HBG L2 **42 passed / 1 skipped**；TMR L2 **60 passed**；task-submit **exit=0** | [日志](../LOCAL-EVIDENCE.md#artifact-2f79ba1d888b) |
| pre-commit / diff whitespace | 所有适用检查通过 | [日志](../LOCAL-EVIDENCE.md#artifact-f342112e3e3b) |
| HTML / 文档 | 本地引用、历史源码及归档校验通过；SVG XML 和当前流程图布局检查通过 | [校验](../LOCAL-EVIDENCE.md#artifact-7d9ce5cfcad9)、[流程预览](../LOCAL-EVIDENCE.md#artifact-a1b9fb8fab05) |

Onboard 任务为 `task_20260918_221626_11474429289`，按正常 CI sweep 排除 SDMA 和 level 4，未执行完整手动 DFX 矩阵。日志中的 pytest-timeout marker 警告未计为失败；本次有外层 session / task 超时限制，不能把未加载的逐测试 timeout 当作已生效。

Scene 回归在 `ad767365` 构建的产物上运行；补充到最终 `e6cd39d4` 的内容只有测试、桩和一处注释，运行时代码行为未改变。C++ / ASan 覆盖包含新增测试的工作树。最终提交又重新安装以更新本地 build stamp；没有将旧二提交栈的结果借作本次验证。可核对[新增差异](../LOCAL-EVIDENCE.md#artifact-dad5d00cd6b6)、[最终补丁](../LOCAL-EVIDENCE.md#artifact-6de918f7d89b)和[提交信息](../LOCAL-EVIDENCE.md#artifact-d679a9a33f9f)。

浏览器整页截图未运行；SVG 栅格检查不代替浏览器端完整验收。

## 证据边界与剩余工作

- CPU helper 测试使用 A2/A3 的 KernelArgs 和 allocator，分别编译 TMR/HBG descriptor；证明 host 协议，不代替 RTS 或 A5 硬件。
- Simulator 直接把 host Runtime 传给执行端，不执行 onboard descriptor H2D；sim 结果只作为 program 行为回归。
- 本机仅 A2/A3，未执行本地 descriptor 的 A5 onboard / legacy；不能借用 #2353 的 CI 宣称这个增量已通过 A5。
- 未测受控性能。快照仍增加一次 descriptor 大小的 CPU copy 和 host allocation / run，H2D 字节不减。`graph_upload` 的范围已在 #2353 改为只计 copy；其旧新数值和三段部分合计均不能作为本次加速证据。
- 未实现 async publication、共享 handshake/reset ABI、kernel adapter、capture owner 或 replay；也未替 Pipeline A 宣告验收。

下一步只需围绕这份 descriptor 增量收口验收与交付；A + S2 达到接收门槛后进入 K1，继续复用共同执行层。
