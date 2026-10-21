> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# Simpler 当前交付、Issue/PR 与后续工作

核对日期：2026-10-21 UTC。Simpler main：`7503761a6c01a6d07ccd8d9c82b8060da843d1af`。已读取全部 45 个开放 PR 和相关 Issue；本页是状态与证据整理，本轮未运行产品测试。开放 PR 的存在不等于已支持，合入不等于整体验收完成。

**结论：普通 Async 的资源、入队和诊断基础已经有合入成果；下一重心是四仓真实模型的组合验收与交付。kernel eager/capture 是同时推进的另一能力线。多人协作机制单独讨论，不再依赖聊天记忆充当任务系统。**

## 1. 三个工作入口

| 入口 | 承载内容 | 当前状态 |
| --- | --- | --- |
| [HBG × Async 系统验收 #2658](https://github.com/hw-native-sys/simpler/issues/2658) | 四仓联通、真实模型、持续请求、正确性、性能、回归 | 新建父 Issue；复用原实现与作者，验收尚未完成 |
| [kernel mode 跨仓 prompt](kernel-mode-cross-repo-prompt.md) / [HTML](kernel-mode-cross-repo-prompt.html) | eager/capture 契约、当前 PR 栈、四仓接收任务、交付门槛 | 可复制交接材料；未声称各仓已调通 |
| [多人协作原则](multi-agent-work-principles.md) / [HTML](multi-agent-work-principles.html) | GitHub 合同、WudiTask 协调、本地多 agent、人类决策 | v0 讨论稿，尚未安装工具或修改 Hub |

## 2. 已交付基础及不能外推的结论

| 能力 | 已合入 PR / 已有验收 | 仍缺什么 |
| --- | --- | --- |
| 有界异步与真实 Qwen 起点 | bounded P4/P1–P3 已验收；[#2447](https://github.com/hw-native-sys/simpler/pull/2447)、[#2456](https://github.com/hw-native-sys/simpler/pull/2456) | 不能把有界起点当四仓或所有平台支持 |
| clone 与 DEVICE 连续反馈 | [#2471](https://github.com/hw-native-sys/simpler/pull/2471)、[#2511](https://github.com/hw-native-sys/simpler/pull/2511)、[#2568](https://github.com/hw-native-sys/simpler/pull/2568)、[#2592](https://github.com/hw-native-sys/simpler/pull/2592) | #2511：127 对请求 joined；#2568：127 步、126 相邻边 joined、125 refill；#2592：六请求 636 步。均受原 workload/平台/诊断条件约束，未证明默认自动准入组合 |
| 动态容量与自动资源准入 | [#2571](https://github.com/hw-native-sys/simpler/pull/2571)、[#2596](https://github.com/hw-native-sys/simpler/pull/2596) | 默认按资源证明准入，不再让用户猜多个 depth；内部 slot 仍是可复用资源身份。冷启动从一个逻辑 run 开始，真实 Qwen 自动模式仍待验收 |
| 事务与测试维护 | [#2611](https://github.com/hw-native-sys/simpler/pull/2611)、[#2613](https://github.com/hw-native-sys/simpler/pull/2613) | native 重入防护、专属测试分离已经交付，不替代模型组合测试 |
| DFX 跨 run 接线 | [#2588](https://github.com/hw-native-sys/simpler/pull/2588) | 四后端六 run 的持续补队与诊断归属有证据；A5 HBG ArgsDump 仍不支持，HBG 无设备 ScopeStats/DepGen 生产者，不外推 kernel capture DFX |
| PMU 与上传相关主线 | [#2643](https://github.com/hw-native-sys/simpler/pull/2643)、[#2653](https://github.com/hw-native-sys/simpler/pull/2653) 已合入；[#2620](https://github.com/hw-native-sys/simpler/pull/2620)、[#2628](https://github.com/hw-native-sys/simpler/pull/2628) 上传路径改造已合入 | 当前源码包含相关修复不等于所有历史故障都已有因果闭环，也不等于端到端性能收益已测得 |

ArgsDump 历史超时按用户要求追加三次串行验证均通过，共六次公开 submit；原期限不变，未复现。**已暂时搁置，遇到再定位**。这不是根因已证或自动取消任何未来失败检查。

## 3. Issue 如何归并

| Issue | GitHub 状态 | 建议后续职责 |
| --- | --- | --- |
| [#1706](https://github.com/hw-native-sys/simpler/issues/1706) HBG tracking | OPEN | HBG 能力母线；链接系统验收与 kernel 分线，历史设计不直接当当前实现 |
| [#2429](https://github.com/hw-native-sys/simpler/issues/2429) vLLM execution backend | OPEN | 保留后端主目标；用 #2658 承载跨仓验收，避免重复维护另一套验收清单 |
| [#1853](https://github.com/hw-native-sys/simpler/issues/1853) 连续运行入队 | OPEN | 已有大量交付，按原 acceptance 做收尾核定，不因旧正文还在就重做机制 |
| [#2316](https://github.com/hw-native-sys/simpler/issues/2316) FIFO / host-build 合同 | OPEN | 核对已合入证据能否满足全部 acceptance，再由 owner 关闭；本轮未代关 |
| [#2415](https://github.com/hw-native-sys/simpler/issues/2415)、[#2425](https://github.com/hw-native-sys/simpler/issues/2425)、[#2517](https://github.com/hw-native-sys/simpler/issues/2517)、[#2565](https://github.com/hw-native-sys/simpler/issues/2565)、[#2585](https://github.com/hw-native-sys/simpler/issues/2585) | CLOSED | 保留历史证据入口，不再作为当前待派任务 |
| [#2512](https://github.com/hw-native-sys/simpler/issues/2512) aarch64 SwimlaneLoss | OPEN | 需要单独核查最新 main 覆盖及解决证据；OPEN 本身不是当前仍失败的证明，本轮未复现 |
| [#2459](https://github.com/hw-native-sys/simpler/issues/2459) HBG distinct | OPEN；独立能力线 | 不作为 clone 或 DEVICE feedback 前置；跟随原 Issue 的范围和负责人 |
| [#2654](https://github.com/hw-native-sys/simpler/issues/2654) scope mode 竞争 | OPEN → [#2652](https://github.com/hw-native-sys/simpler/pull/2652) | 并发使用同一 orchestration context 时核对此修复与配套构建；不把所有普通串行 HBG 都判成不可用 |
| [#2594](https://github.com/hw-native-sys/simpler/issues/2594)、[#2642](https://github.com/hw-native-sys/simpler/issues/2642) | OPEN | 通信域和 peer bootstrap 独立线；仅在具体 workload 需要时形成依赖，不归因到 ArgsDump |

## 4. 开放 PR：先接现有作者，不能重复派发

下表只列相关工作；全仓 45 个开放 PR 不都属于本线。这里未逐项重审 CI，不给任何开放 PR 新的 Ready 结论。

| 工作 | 当前 PR 与核对 head | 下一步 |
| --- | --- | --- |
| 真实模型与服务入口 | [#2436](https://github.com/hw-native-sys/simpler/pull/2436) `a4a0b3dc4`；[#2605](https://github.com/hw-native-sys/simpler/pull/2605) `591f003be` | 复用驱动。[#2605](https://github.com/hw-native-sys/simpler/pull/2605) 作者报告短序列通过，128-token 在第 64 token 分歧；分别解决数值等价与性能，不接管另一份实现 |
| TMR kernel | [#2624](https://github.com/hw-native-sys/simpler/pull/2624) `e9c16b047` → [#2625](https://github.com/hw-native-sys/simpler/pull/2625) `7848e06e0` | eager 强化 → capture/replay；最终组合要补消费者证据 |
| HBG kernel | [#2626](https://github.com/hw-native-sys/simpler/pull/2626) `f701832b0` → [#2646](https://github.com/hw-native-sys/simpler/pull/2646) `be63699b4` → [#2647](https://github.com/hw-native-sys/simpler/pull/2647) `8f1a054d9` | 共用资源 → 模块 → dedicated launch 生命周期；箭头是逻辑依赖，必须重核当前 head 的实际祖先 |
| kernel 旧栈扩展 | [#2493](https://github.com/hw-native-sys/simpler/pull/2493) `536cf3eea`；[#2583](https://github.com/hw-native-sys/simpler/pull/2583) `97343062d` | DFX/prefetch 基于旧 integration，先移植审查与接口对齐；不能当 main 支持 |
| HBG 其余进展 | [#2630](https://github.com/hw-native-sys/simpler/pull/2630) prepare queue；[#2652](https://github.com/hw-native-sys/simpler/pull/2652) scope；[#2655](https://github.com/hw-native-sys/simpler/pull/2655) output root | 由原作者完成各自行为闭环；接入场景确需才设为前置 |
| 会话/通信/平台 | [#2598](https://github.com/hw-native-sys/simpler/pull/2598)、[#2639](https://github.com/hw-native-sys/simpler/pull/2639)、[#2651](https://github.com/hw-native-sys/simpler/pull/2651)、[#2656](https://github.com/hw-native-sys/simpler/pull/2656) 等 | 独立原作者线；只按生命周期或多设备实际需求关联 |

跨仓另有 PyPTO #3001/#3084 重叠：#3084 当前已包含 #3001 功能；serving #274 引用的 PyPTO #2975 已合入。细节和不可变证据见 kernel prompt，不能机械沿用旧合并顺序。

## 5. 后续依赖与可并行工作

```text
已合入 Async / 资源 / DFX 基础
  ├─ 原 Qwen 输入恢复可读 → 默认自动准入 × clone/DEVICE 组合验收 ─┐
  ├─ 四仓 HBG 编译/模型/服务入口对齐 → prefill/decode 正确性 ────┤
  └─ TMR/vLLM 可比基线准备 ────────────────────────────────────┤
                                                             ↓
                  #2658 持续请求与生命周期 → 性能归因 → CI/发布矩阵

TMR eager → capture ──────┐
HBG 资源 → 模块 → launch ─┴─ PyPTO adapter → pypto-lib → serving eager/capture
                   ↑ 旧 integration DFX/prefetch 按需迁移；不是所有路径前置

协作原则讨论 → 小范围试用 → 再决定 Hub/自动化改造（独立于产品代码）
```

| 可同时开展的包 | 能现在做的事 | 真正阻塞及交接 |
| --- | --- | --- |
| A 系统入口与合同 | 四仓接口/产物矩阵、现有 driver 复用、回归入口与 owner 确认 | 公开调用链与配套 runtime 需要明确；不另造统一假 API |
| B 模型正确性 | 复用 [#2605](https://github.com/hw-native-sys/simpler/pull/2605) 原作者的数值差异证据，定义 teacher-forced/KV 对照 | 修正数值语义后才能接受完整请求；短序列不代替长序列 |
| C Async 组合 | 保存现有改动与精确产物身份，明确自动准入/寿命证明 | 本地 83 份原参考/身份输入仍不可读；不能绕权限、换参考或缩小模型来宣称完成 |
| D kernel 双路线 | TMR 与 HBG 原作者并行，adapter/模型/serving 提前对齐接口 | 共享 ABI 有明确 owner；最终消费者验收依赖必要 runtime 集成 |
| E 测量方案 | 同工作/质量/精度对照、基线采样、host/device 归因方案 | 功能正确后才形成正式性能结论；诊断测量和正式性能分开 |
| F 协作机制 | 与用户讨论任务身份、认领范围、交接和收尾协议 | 本轮只有设计稿，没有安装 Hub 或接管现有任务 |

更后的工作：约定动态请求形态；capture/replay 的模型与服务闭环；确需的通信/多设备能力；端到端正确性与性能报告。不能把这些全塞进一次“支持异步”的勾选，也不能按 slot 2→3→4 拆成一串不完整用户功能。

## 6. 不变的验收边界

设备 operator 串行、parent FIFO、请求私有资源在最后消费者结束后退休、有限资源与明确背压，不添加隐式跨 run host 同步。请求数 grant 和字节预算是两个概念；用户不应为了普通调用猜多个深度旋钮。能力按架构/runtime/调用模式/任务形态分别验收，未经验证不得外推。

性能方向是 **HBG+Async 优于 TMR+Async，TMR+Async 优于 vLLM-Ascend**，且稳态 host 暴露等待应可忽略；目前是目标。需要相同有效工作、数值质量和资源约束的可比实验；若尚未达到，报告瓶颈而非更改口径。

## 7. 本轮交付与下一次讨论

已建立 #2658，提供 kernel 跨仓 prompt 和独立协作讨论稿。未安装 WudiTask、未更改 Hub、未代替原作者合并 PR、未做新的产品测试。下一次建议先讨论：**一个 GitHub 账号下的本地多 agent，是否统一由一个 manager run 对外认领，再按独立 scope/worktree 在内部并行？** 这与当前 WudiTask 的 same-login 限制吻合，且不会伪造多个参与者身份。
