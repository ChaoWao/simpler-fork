> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# 人类与多 agent 的统一工作原则：与 WudiTask 配合的 v0 讨论稿

状态：**DISCUSSION；建议规则，不是已生效制度或实施授权。**

本次仅研究 WudiTask，不安装工具或修改 Hub；本文为供讨论的设计资料。代码核对版本为 [`cf72026393c2d63412782c67f6cea33ce21c43b0`](https://github.com/ChaoWao/wuditask/commit/cf72026393c2d63412782c67f6cea33ce21c43b0)（查询 main 得到的当前 HEAD；2026-10-21 查询）。

**建议总原则：Issue/PR 负责说明做什么、为什么、怎样验收；Hub 负责协调依赖与参与运行；本地 manager 负责把多个人和 agent 的工作接起来。多人可以参与同一目标，但每个写入范围、集成分支和最终验收必须有明确负责人。**

## 1. 已实现的能力，以及不能从它推导出的保证

以下“已实现”均来自上述固定 commit；静态阅读了源码与现有测试，未运行测试。

| 项目 | 核实结果及适用边界 |
| --- | --- |
| 业务事实源 | canonical GitHub Issue/PR 保存目标、约束、acceptance、讨论和交付。Hub schema v3 / API5 仅保存 source、执行仓、priority、dependencies、active_agents 与 archive。不要把正文复制到 Hub。[数据格式](https://github.com/ChaoWao/wuditask/blob/cf72026393c2d63412782c67f6cea33ce21c43b0/docs/data-format.md) |
| owner 与执行者 | PR owner = author + assignees；Issue owner = assignees + closing-linked PR authors。owner 不是锁。active entry 是 `{login, run_id}`。[架构](https://github.com/ChaoWao/wuditask/blob/cf72026393c2d63412782c67f6cea33ce21c43b0/docs/architecture.md) |
| 多人并行 | 同一 task 的不同 login 可以并行；同一 task、同一 login 忽略大小写最多一个 run。同 login 可以参与不同 task；不是全局单 run 限制。测试覆盖不同用户并行、同用户竞争失败。[实现](https://github.com/ChaoWao/wuditask/blob/cf72026393c2d63412782c67f6cea33ce21c43b0/wuditask/workflow.py#L266-L301)、[校验](https://github.com/ChaoWao/wuditask/blob/cf72026393c2d63412782c67f6cea33ce21c43b0/wuditask/model.py#L60-L92)、[测试](https://github.com/ChaoWao/wuditask/blob/cf72026393c2d63412782c67f6cea33ce21c43b0/tests/test_git_concurrency.py#L198-L225) |
| 启动事务 | 必要时先 GitHub self-assign，再普通 push Hub；成功后重查 GitHub ownership / delivery，不合法则尝试释放自己的精确 run。两系统没有原子事务，失败不回滚已确认 assignment。[execute](https://github.com/ChaoWao/wuditask/blob/cf72026393c2d63412782c67f6cea33ce21c43b0/wuditask/cli.py#L573-L674) |
| Hub 并发 | 普通 push 序列化分支更新；non-fast-forward 后读新快照、重放操作，保留其他 login。成功 push 直接确认；失败后若远端历史包含本次 commit，也视为已接受；否则竞争错误重试或返回 unknown。[write](https://github.com/ChaoWao/wuditask/blob/cf72026393c2d63412782c67f6cea33ce21c43b0/wuditask/gitops.py#L198-L320)、[祖先确认](https://github.com/ChaoWao/wuditask/blob/cf72026393c2d63412782c67f6cea33ce21c43b0/wuditask/gitops.py#L638-L653) |
| 租约与存活 | task schema 无 heartbeat、过期时间或自动接管字段；现有 operation lease 是本机 CLI 临时 worktree 的进程锁，用于垃圾回收，不是长期 agent 执行租约。不能因为长时间无消息就认定 run 已失效。[schema](https://github.com/ChaoWao/wuditask/blob/cf72026393c2d63412782c67f6cea33ce21c43b0/schemas/task.schema.json)、[回收](https://github.com/ChaoWao/wuditask/blob/cf72026393c2d63412782c67f6cea33ce21c43b0/wuditask/gitops.py#L478-L505) |
| release | 仅删除当前认证 login 的精确 run；不改 GitHub owner、不停止进程、也不回收硬件锁。重复 release 可能报 agent_not_active；旧 run 不能释放新 run。[实现](https://github.com/ChaoWao/wuditask/blob/cf72026393c2d63412782c67f6cea33ce21c43b0/wuditask/workflow.py#L322-L387) |
| archive | active 时调用者必须匹配自己的 run；done 还要 live owner、依赖 ready、成功 GitHub 终态和非空 evidence。**一次 archive 清空全部 active runs**，保存当时全集为 participants；无 active 时仅 creator 可归档且不能传 run_id。同 actor、run、outcome、result、evidence 的已归档结果可幂等确认。[实现](https://github.com/ChaoWao/wuditask/blob/cf72026393c2d63412782c67f6cea33ce21c43b0/wuditask/workflow.py#L408-L572) |
| check 与依赖 | check 纯读，报告依赖、ownership drift、终态未归档等。GitHub merge/close 本身不解除 Hub 依赖，只有 archive done 解锁。check 没有完成事件的自动处理器。[check](https://github.com/ChaoWao/wuditask/blob/cf72026393c2d63412782c67f6cea33ce21c43b0/wuditask/cli.py#L677-L791)、[工作流](https://github.com/ChaoWao/wuditask/blob/cf72026393c2d63412782c67f6cea33ce21c43b0/docs/workflow.md) |
| 身份去重 | WDT ID 是 Hub 记录身份；创建代码检查 ID 冲突，未按 source 做唯一性去重。不同 ID 指向同一 Issue 的重复登记，需要协调层避免。[create_task](https://github.com/ChaoWao/wuditask/blob/cf72026393c2d63412782c67f6cea33ce21c43b0/wuditask/workflow.py#L34-L123) |

**文档与代码差异，操作时以代码能力为限：**

- architecture 写了完整远端 postcondition 校验，但 `GitCoordinator.write` 的普通成功路径没有再次读 task；失败恢复使用 commit ancestry。祖先包含证明“提交被接受过”，不证明“此刻 entry 仍 active”。execute 后检查看的是 GitHub，不重查 Hub run。开工前仍应 check 精确 run；这次 check 也不是持续租约。
- architecture 提到 unknown 时使用同 run 重试，但 CLI `execute` 没有 `--run-id`，每次新调用生成新 ID；同一次调用内的 push 重试才沿用原 ID。不要发明重试参数。[CLI 参数](https://github.com/ChaoWao/wuditask/blob/cf72026393c2d63412782c67f6cea33ce21c43b0/wuditask/cli.py#L109-L127)、[ID 生成](https://github.com/ChaoWao/wuditask/blob/cf72026393c2d63412782c67f6cea33ce21c43b0/wuditask/cli.py#L660-L674)
- workflow 的 unknown 恢复指南是先 check，若旧 run 存在则精确 release，再 execute。业务层还须先确认原执行进程停止；工具不会替我们停进程或协调多个本地会话。[故障指南](https://github.com/ChaoWao/wuditask/blob/cf72026393c2d63412782c67f6cea33ce21c43b0/docs/workflow.md#10-常见故障)

## 2. 建议的角色、身份和工作边界

### 谁负责什么

| 角色 | 建议职责 |
| --- | --- |
| 人类目标负责人 | 决定目标、优先级、验收标准；裁决需求冲突、跨仓接口冲突、无法协商的范围冲突。无需批准每次可逆日常动作。 |
| manager / integrator | 每项交付明确一个；检查 main 与全部 open 工作，拆分范围、安排依赖、接收完成事件、集成单一 PR、收齐证据并协调归档。 |
| contributor | 人或 agent 均可；领取明确范围，在独立 worktree 工作，按约定交接；不能因自己部分完成而 archive 整体。 |
| independent reviewer | 不承担该 diff 的实现；从目标和风险独立检查精确 head，指出缺口。修改 head 后旧结论不能自动沿用。 |
| Hub maintainer | 维护 Hub 数据与权限，不代替业务验收，也不因为拥有仓库权限就获得接管所有运行的业务授权。 |

角色可由同一人兼任部分职责，但同一 diff 的作者不能充当唯一独立 reviewer。外部真实 GitHub owner 与内部 manager 角色分别记录；不要伪造 login 来绕过同账号 run 限制。

### 任务身份与分工

- canonical key 建议采用 `github.com / owner/repo / issue-or-pr / number`；运行身份为 `WDT ID + login + run_id`；交付身份为 `PR URL + head SHA`。本地 worker 再带 manager 分配的 worker ID、scope revision、worktree 标识。
- 同一 Issue 允许多个 contributors，先记录每人的可写路径、接口、产物和依赖。一个 worktree 只允许一个写者；集成分支只由 integrator 写。同文件即使不同函数也默认串行交接，除非明确隔离且 integrator 接受冲突处理成本。
- 同账号本地多 agent 默认只占一个 Hub run，由 manager 管理本地 workers。不同账号可各有 Hub run，但需共同认可同一分工表。active_agents 没有文件路径字段，不能替代范围协调。
- 分工与 acceptance 放在 canonical Issue/PR；本地 registry 只记录本机执行状态、scope 引用和回执，不成为另一份可独立修改的需求。
- 任务 claim、文件写入归属、硬件资源锁是三件事。Hub execute 不授权独占 NPU；占卡实验仍走项目的架构预检与 task-submit 锁。

### 一 PR 一完整功能，跨仓使用父子目标

默认一个 PR 交付一个可独立验收的完整行为；实现、必要测试和必要文档一起进入该 PR。内部 worker 切分是实现细节，不因此拆成多个不完整 PR。

跨仓需求不能由一个 GitHub PR 覆盖全部仓库：建议一个父 Issue 写端到端验收，各仓子 Issue / PR 各自交付可独立解释的完整增量。父 Issue 不复制子 Issue 细节，只记录接口契约、验收与链接。可独立合并的基础能力可以有自己的完整 PR，父目标要等所有必要交付和端到端验证完成。

**父子关系不是阻塞依赖。** 父目标完成依赖子交付，子交付依赖上游接口或实现；不要让所有子 task 依赖父 task done，造成环。Hub 目前只有 dependency edges，没有独立 parent 字段。新任务依赖必须先存在，当前 CLI 没有编辑既有 dependencies 的命令；初期先在 Issue 定义关系，按依赖顺序登记可执行任务，父协调 Issue 可暂不登记为阻塞型 Hub task。

## 3. 从派工到验收的建议流程

这是团队协议，除明确注明者外不代表 WudiTask 自动执行。

1. **派工前复核。** 获取当前 main SHA、全部 open Issue/PR、既有任务和本地 writers；判断已落地、有人在做、被替代还是确实缺失。保存查询时间和 coverage。不能把旧轮的 main / allopen 结论当成永久授权。
2. **确定合同和分工。** canonical source 写目标、约束、可观察 acceptance；manager 记录 contributors、integrator、reviewer、写入范围、依赖和验证计划。范围重叠先协商，不先并行再等 Git 冲突。
3. **确认启动。** 通过已安装版本支持的命令 execute；仅当响应确认且 work_authorized、repo、run_id 正确才开始。再 check 当下精确 run；记录 Hub commit 与 source 版本。GitHub 不可用或 push unknown 时不开始新写入。
4. **本地执行与交接。** 每个 writer 独立 worktree；共享接口由指定 owner 协调。范围扩张先更新分工，不能私自越界。完成通知必须附 commit/head、范围、验收证据、未完成项。
5. **实际处理完成事件。** manager 收到通知后读取产物、复核 scope、安排 review / 集成 / 下游任务或记录 blocker，并留下处理回执。“已收到”不算完成。通知可重复，处理必须按任务、run、产物 SHA 去重。
6. **精确 head 评审与验收。** 独立 reviewer 记录审阅 SHA；CI 是必要技术检查之一，不等于 feature acceptance。每项 acceptance 对应实测行为和证据。性能结论附基线、代码 SHA、工具链、设备锁、配置、样本与限制；范围上限、局部测量或未执行验证必须如实标注。
7. **集成与归档。** integrator 收齐所有 contributors 的交接，确认没有未结束的写入、没有被遗漏的参与者，再依授权提交或合并完整 PR。GitHub 终态后执行 archive；archive 权限在工具层较宽，团队默认仅 integrator 发起。保留精确回执，重新 check 下游 ready。
8. **失败与取消。** 暂时等待不应伪装成完成；release 自己的精确 run，保留 owner 与 blocker。failed / cancelled archive 还要匹配 GitHub 终态；不得为了清空队列把没做完的任务标 done。

### 完成回执、失联和未知结果

建议 manager 本地事件台账分成 `observed → processing → processed`，每条记录包含：

```text
event_key: task / login / run / event-kind / artifact-head
observation: source URL, observed state, observed_at
effect: review / integrate / unblock / archive / blocked
receipt: exact commit or PR head, evidence links, remote confirmation
next_action: owner, condition, retry state
```

这是拟议的 manager 台账，不是现有 Hub schema 字段。Hub archive 的 completion 保存最终证据；具体事件处理回执在本地台账，并在 canonical source 提供必要结果链接。重启后先读取 GitHub / Hub 实际状态，再决定是否补做；不要因为通知重发而再次合并、再次开任务或重复跑昂贵实验。

| 情况 | 建议恢复动作 |
| --- | --- |
| execute unknown | 停止新写入；保存错误中的 task/run。check 失败则保持 unknown；查到旧 run 时先确认原进程已停、按精确 run release，确认后再 execute。查不到时仍检查原会话，确定没有另一执行者才重启。 |
| release 回应丢失 | check 精确 pair；已消失说明当前无该 run，但单凭消失不能证明是本次 release 的效果；保留原响应或 Hub commit 作为审计。出现新 run 时绝不换 ID 重放旧 release。 |
| archive 回应丢失 | 用原 actor/run/outcome/result/evidence 重试并核对 archive；若内容不同，交给 integrator 协调，不覆盖。 |
| 会话失联 / 机器重启 | active_agents 不会自动过期。先确认进程和未交接产物，再由同登录身份恢复或精确 release。其他 login 无现成强制 release 路径；需要 maintainer 处理时升级给人类，禁止冒用身份或静默改 JSON。 |
| 多人 scope 冲突 / 双 manager | 受影响写入暂停，保留分支和证据；由原 integrator 协调，无法一致则人类目标负责人选择范围与接续者；无冲突范围继续。 |
| GitHub 已完成、Hub 未 archive | check 仅报告 archive_required；由合法归档者检查 acceptance 后归档，不能把 GitHub 终态直接当作队列完成。 |

## 4. 需要补齐的能力与逐步采用

下表是设计差距清单，**不是批准修改 WudiTask**；先用明确的人类/manager 协议补足，遇到真实痛点再另立需求。

| 差距 | 当前后果 | v0 默认处理 / 未来候选 |
| --- | --- | --- |
| 多 agent scope / integrator 不在 schema | 多 login 并行仍会重复做事、重叠写文件 | source 分工表 + 单 integrator；将来是否结构化再讨论 |
| 没有长期 run 心跳与到期接管 | 失联 entry 持续存在；无防止旧执行者恢复写入的 fencing | 人工确认后交接；若将来加租约，必须同时设计旧 run 写入拒绝，不能只定 TTL |
| CLI execute 无外部幂等键 / 重用 run 参数 | 跨调用恢复比内部 push 重试复杂 | 保存错误 run、check / release / execute；候选为持久操作 ID 和明确恢复命令 |
| 文档 postcondition 描述强于代码 | 接受过的 commit 可能被误读为当前仍获授权 | 修订能力描述为独立工具维护议题；manager 开工前 check，归档前收齐参与者 |
| source 无唯一性 guard；依赖无编辑 CLI | 重复 task、动态 DAG 调整需额外治理 | 登记前按 canonical key 查重；稳定依赖后登记；未来定义安全重连与去重事务 |
| 无自动完成消费与业务验收证明 | check 能看到终态，但不会接续调度；自由文本 evidence 不能验证功能正确 | manager 幂等回执、独立 reviewer、逐项验收；未来自动化仍保留这些边界 |
| archive 清空全部 active | 一位参与者可结束其他仍工作的 run | 约定只有 integrator 收齐交接后 archive；候选为明确完成协调协议 |

### 从本地 registry 接到 GitHub / Hub，避免双重事实源

- **现在：讨论并试用原则。** 现有本地 registry 继续跟踪本地执行；所有记录指向已有 Issue/PR，无远端变更。本草案本身不触发 install、任务登记或产品开发。
- **准备采用时：按字段迁移。** 目标/acceptance/决策归 canonical source；dependencies/active runs/archive 归 Hub；本机进程/worktree/事件消费回执留本地。需要迁移的内容先作映射与查重清单。
- **切换时：每个字段只有一个 writer。** 已在跑的任务先对账，不把同一工作再次 execute。切换后的本地任务状态只作为缓存，带 source、Hub commit 和 fetched_at；旧 registry 协调字段只读。Hub 不可用时标 unknown，不退回本地另派同一任务。
- **小范围验证后扩展。** 用一项单仓完整功能验证多 worker、独立 review、一次丢失回应恢复、最终 archive 与下游处理；再试跨仓父子目标。采用工具与创建远端记录属于后续明确工作范围。

### 简短任务与交接模板

```text
任务（canonical Issue/PR）
目标 / 不在本次范围：
验收（可观察结果）：
执行仓 / parent 与依赖：
integrator / contributors / reviewer：
每位 contributor 可写范围 / 接口 / 产物：
当前 main 与 allopen 检查时间、证据：
验证计划 / 资源需求 / 风险：
```

```text
交接（worker → integrator）
canonical source / WDT ID / login / run_id / worker ID：
scope revision / 已停止写入：
产物：branch + commit/head SHA；变更范围：
验收证据：命令、环境、结果、链接、未验证项：
阻塞 / 风险 / 下一动作：
manager receipt：处理结果、review head、集成 head：
```

## 5. 建议先讨论的三个决定

1. **多 agent 的 run 粒度。** 默认：同账号一个 manager Hub run，内部多个 worker；不同真人账号可各自 run。是否符合实际协作习惯？
2. **完整功能与跨仓边界。** 默认：单仓一个功能一个 PR；跨仓父 Issue + 各仓完整子 PR；仅真实前置条件进入 Hub DAG。是否接受父目标与子交付分开验收？
3. **结束与接管权限。** 默认：只有指定 integrator 收齐交接后归档；失联不自动过期、不自动接管，先恢复确认，冲突交给人类目标负责人。是否需要将来建设带防旧执行者机制的租约？

优先把这三点定清楚，再决定是否需要修改工具。当前实现已经能承载“共同目标、多人参与、GitHub 合同、最小 Hub 协调”的基本模式；重复劳动、文件冲突和真正完成的判断，仍需要上述明确工作协议。
