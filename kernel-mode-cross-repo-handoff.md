# Kernel eager/capture 四仓接管与下一步

接管及只读刷新：2026-10-21T10:40:08.984295+00:00。本轮保存文档、读取四仓 main/open PR 元数据及列出的 PR/Issue 正文；没有运行产品构建、设备测试，也没有重审候选 PR 的完整 diff。

## 1. 新的维护入口

后续由本设计目录维护运行时路线与跨仓交付。原 manager worktree 可移除；活动文档不再依赖它。

| 用途 | 唯一入口 |
| --- | --- |
| 设计判断 | [runtime-design-principles.md](runtime-design-principles.md)；来源中同名文档仅作冻结证据 |
| 实施顺序 | [ROADMAP](ROADMAP.md) / [路线图 HTML](kernel-mode-roadmap.html) |
| 可复制的四仓任务 | [协作提示词](kernel-mode-cross-repo-prompt.md) / [HTML](kernel-mode-cross-repo-prompt.html) |
| 原 Pipeline A、DFX、workspace 与历史评审 | [接管资料索引](references/pipeline-a/README.md) |
| 四仓 HBG × Async 系统验收 | [#2658](https://github.com/hw-native-sys/simpler/issues/2658)，与 kernel 实现职责分开 |

原提示词的 dedicated launch、lease/token 与“公开能力发现”是现有候选 PR 的实现事实和审查入口。它们不替代既定的一套 Worker/Buffer/Tensor/TaskArgs 与共同 executor，也不授权新增 abi_version、struct_size、全 API 查询表或复制 kernel 后缀 API。是否接受候选接口，要对照原则和真实消费者审查。

资源所有权与 eager/capture 仍是不同维度；当前 capture PR 的受限支持不是最终 C1–C4 方案的全部交付。每次 eager fresh prepare/submit，不恢复 PreparedCall、重复 submit 或自动准备缓存。

## 2. 刷新的四仓事实

以下是读取时的事实；开放 PR 数是分页得到的全列表数量，不表示全部属于 kernel 任务。main 不是一个跨仓原子快照。

| 仓库 | main SHA | 开放 PR 数 |
| --- | --- |
| simpler | [`7503761a6c01a6d07ccd8d9c82b8060da843d1af`](https://github.com/hw-native-sys/simpler/commit/7503761a6c01a6d07ccd8d9c82b8060da843d1af) | 45 |
| pypto | [`390a75363f06dd5e538dcb749b92742a20962bde`](https://github.com/hw-native-sys/pypto/commit/390a75363f06dd5e538dcb749b92742a20962bde) | 47 |
| pypto-lib | [`d12b4b878a742c70c55e921efd3769a54470b643`](https://github.com/hw-native-sys/pypto-lib/commit/d12b4b878a742c70c55e921efd3769a54470b643) | 52 |
| pypto-serving | [`2d4b7e371360c4f54cf4b0dce5a34c17e0dbe5f5`](https://github.com/hw-native-sys/pypto-serving/commit/2d4b7e371360c4f54cf4b0dce5a34c17e0dbe5f5) | 45 |

**#2653 已合入 Simpler main `7503761a6`**。其最终 head `dcdb55ef2` 的 CI 为 19 success / 1 deploy skipped，包括两架构 DFX-off profiling smoke。原 AIV 单发仍未分类，已保留在本地 KNOWN_ISSUES 和[本笔记录](kernel-eager-open-work.md#c1-pmu-invocation)；CI 全绿不是根因证明。

原提示词的 pypto-lib 基线为 `d2a3aaa850e13f043bae50682f9d5d20c32cf335`；本次已前移到表中提交。未审计其中新增提交的业务影响，不把该仓更新自动当作组合可用。

### 相关候选的准确归属

| 仓库 / PR | 状态 | 原作者（联系人） | head | target |
| --- | --- | --- | --- | --- |
| [simpler #2624](https://github.com/hw-native-sys/simpler/pull/2624) | OPEN | qweasdzxcht | `e9c16b047179059e5256e7faae22d08e2f025ef2` | `main` |
| [simpler #2625](https://github.com/hw-native-sys/simpler/pull/2625) | OPEN | qweasdzxcht | `7848e06e020fb8fc60fb55167c2336a8ba622a1f` | `main` |
| [simpler #2626](https://github.com/hw-native-sys/simpler/pull/2626) | OPEN / DRAFT | zxx0288 | `f701832b091290d3db9f20eabe678700a4e7a043` | `main` |
| [simpler #2646](https://github.com/hw-native-sys/simpler/pull/2646) | OPEN / DRAFT | zxx0288 | `be63699b446aadba387580f8d188236b0bd7a22b` | `main` |
| [simpler #2647](https://github.com/hw-native-sys/simpler/pull/2647) | OPEN / DRAFT | zxx0288 | `8f1a054d96fc7cbd46458905e07105f77f4758d5` | `main` |
| [simpler #2493](https://github.com/hw-native-sys/simpler/pull/2493) | OPEN | nalinaly | `536cf3eea9e4324351891a8059d0879a1cf7b4f5` | `feat/kernel-mode-integration-test` |
| [simpler #2583](https://github.com/hw-native-sys/simpler/pull/2583) | OPEN | TaoZQY | `97343062d08ad2ce4fac73cc17e42180972da603` | `feat/kernel-mode-integration-test` |
| [simpler #2652](https://github.com/hw-native-sys/simpler/pull/2652) | OPEN | qweasdzxcht | `f2bacd9b1f7d02cac79763cbb35c65ae59fce912` | `main` |
| [simpler #2655](https://github.com/hw-native-sys/simpler/pull/2655) | OPEN | doraemonmj | `f99aabafb9eb8f67146b8dccaf3131dc1e46c357` | `main` |
| [simpler #2605](https://github.com/hw-native-sys/simpler/pull/2605) | OPEN | Crane-Liu | `591f003be553ae074dbcec1fcb55e41445b479a8` | `main` |
| [pypto #2975](https://github.com/hw-native-sys/pypto/pull/2975) | MERGED | lyfne123 | `1127884167fc92715e0b364bc2b73baeffb296f3` | `main` |
| [pypto #3001](https://github.com/hw-native-sys/pypto/pull/3001) | OPEN | YunjiQin | `ed612fbdb7e321013fc4324bd2a6febd86df2234` | `feat/kernel-mode-integration-test` |
| [pypto #3084](https://github.com/hw-native-sys/pypto/pull/3084) | OPEN | TaoZQY | `f6b22488404c5a542806a660f88c137047f3ea5a` | `feat/kernel-mode-integration-test` |
| [pypto-serving #274](https://github.com/hw-native-sys/pypto-serving/pull/274) | OPEN | lyfne123 | `a416406cffe5ce8209d7e9570713694cb27ae699` | `main` |

作者字段只指出已有工作联系人，不代表他们承诺了新的任务。#2658、#2255、#2202 读取时均 OPEN 且没有 assignee；这里不替他人认领、不新建重复 Issue。

## 3. 接下来按什么顺序处理

```text
已合入 Pipeline A / common eager / S3 / descriptor-DFX 基础
  ├─ TMR：#2624 eager 强化 → #2625 native capture
  └─ HBG：#2626 common resources → #2646 graph modules → #2647 production
                   ↓
  同一公开入口与生命周期审查 → PyPTO adapter → pypto-lib → serving
                   ↓
  C1/C2/C3/C4 的多图、换参、DFX、故障、容量与完整产品验收

#2658：HBG × 普通 Async 的真实业务组合验收，独立记账
#2493 / #2583 与 PyPTO #3001 / #3084：旧栈迁移/去重，不自动成为所有路径前置
```

箭头是逻辑依赖，尚未在本次交接中核对所有新 head 的真实祖先关系。先看实际 diff 和 main 增量，再决定组合/改写，不能整条复制作者分支。

| 工作包 | 现在可做的具体工作 | 接收门槛 / 边界 |
| --- | --- | --- |
| A：双 runtime 候选审查（本会话下一步） | 读取 #2624/#2625 与 #2626/#2646/#2647；核对 shared owner、注册、参数快照、stream、完成、close、graph 引用及 stacked diff | 给出能否进入既有共同执行路径的明确结论；TMR/HBG 分别评审，A5 HBG 不从 A3 通过外推 |
| B：四仓调用/产物映射 | 逐项找出真实 compile/load → ChipCallable → public Worker/native bridge → RunHandle/graph 的文件和测试；记录缺口 | 当前只有任务/PR 线索，未完成 adapter 源码审计；不发明 Python 函数或 CLI 参数 |
| C：旧 integration 迁移 | 审 #2493 DFX 与 #2583 prefetch 的 ABI、缓存和组合依赖；核实 PyPTO #3084 对 #3001 的包含/重叠 | 原交接与 PR body 报告了重叠，尚非本次逐 commit 验证；不按旧顺序机械合两份 feature |
| D：真实库与服务验收 | 确定真实链式算子/模型、KV/MTP 状态、changing-content、请求关闭与错误保留合同；消费者准备可与 runtime 审查并行 | runtime 原语、adapter、库用例、请求级结果逐层成立，才可声明端到端支持 |
| E：普通 Async 系统验收 | 按 #2658 复用现有 Qwen/driver、自动资源准入与 clone/DEVICE 反馈证据 | 输入权限缺口、长序列数值差异和性能归因仍独立处理；不以 kernel 进度代替完成 |

四仓之间必须签收：产物 schema/target、整套构建 ABI、注册与 context generation、caller stream 顺序、输入输出/alias、graph 与 buffer 最后使用、partial/unknown failure 保留、平台支持与证据身份。完整交接清单保留在[原提示词 §3](kernel-mode-cross-repo-prompt.md#3-跨仓必须共同签收的契约)。

## 4. 移入的资料怎样使用

共迁入 137 份链接可达的文字文件，约 4 MB；它们覆盖 Pipeline A、workspace、DFX、跨仓任务和必要历史评审。每页标记“接管快照”，不再各自维护当前状态。

- 原 manager 的总体原则中夹有历史实现状态，例如 G3 的“main 仍保留显式 depth”与后续 #2596 自动准入记录不一致。保留原句作为历史证据，不能据此恢复旧的多 depth 方案或认定主线未实现自动准入。
- `manager-workspace.md` 是会话/事件流水，`claude-managed/` 是旧调度/接续控制材料，均仅本地保留、不上传 fork；协作机制稿保持讨论状态，不恢复旧 agent、cron、Hub 或 polling。
- 文档中的原源码/另一 worktree/日志链接已转入[证据索引](references/pipeline-a/ARCHIVE-INDEX.md)，不以不存在的工作目录继续导航。原 SHA、任务号、反例和未验收边界保留。
- 原目录只读归档包含 12,193 个文件、884,041,092 字节；压缩包 201,491,513 字节。逐文件对照源目录校验通过，校验时无变化。归档和原始数据仅本地保留，fork 只备份设计正文及文字证据。
- 原文文件哈希见[来源清单](references/pipeline-a/SOURCE-MANIFEST.json)。新入口的链接、HTML 和发布 manifest 同次更新。

## 5. 收尾与后续维护

这次接管完成后，继续在本目录维护规范、路线与证据；旧目录不是后续依赖，也没有由本次操作删除。重要更新按 [PUBLISHING](PUBLISHING.md) 增量备份到 fork 的 `design/kernel-runtime`，不进入产品代码 PR。

下一次执行从工作包 A 开始：先完成候选与既定共同执行设计的对齐审查，再从确定缺口选择完整功能 PR。跨仓实现和消费者验收按已确认 owner 协作，不把这份交接说明视为代发消息、认领任务或合入 PR 的授权。
