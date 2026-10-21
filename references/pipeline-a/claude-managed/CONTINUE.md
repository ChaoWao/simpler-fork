> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# Pipeline A 续办入口

这是用户授权的持续执行；完成本轮可推进的工作，不只汇报状态。
所有相对路径从 `pipeline-a-manager` worktree 根目录解析。

## 读取顺序与状态来源

1. 先仅删除 `.docs/claude-managed/manager-tick.pending`，不删除 enabled 或其他文件。 用户普通追问、直接指令或原生 Completed 触发的 manager 轮次也执行此步，不能仅在收到固定续办文案时消费 pending；否则后续完成通知会被旧标记持续合并。
2. 遵守仓库 `CLAUDE.md` 和相关规则；读取 `.docs/manager-workspace.md` 的工作约定。
3. 读取 `.docs/claude-managed/registry.md` 顶部 **CURRENT CONTRACT** 最新记录。
   它是任务、原会话、授权、暂停和待反馈状态的权威来源；较新用户指令优先。
   按需向下查对应授权和证据，不每轮读取全部历史。
4. 运行 `python3 .docs/claude-managed/manager_wakeup.py inbox` 查看子任务完成事件，再按当前任务读取关联的 decision、review、prompt、result 和 CI ledger。
   原会话映射由 `dispatch_claude.py` 保存。旧触发消息、旧 exec、旧绿灯不是当前状态。
5. 恢复或长时间未续办时，逐项核对原UUID进程与result文件。进程已退出且结果为空时，记为执行中断/结果缺失，不能沿用active；先核远端是否已合入，再决定原会话收尾或下一项。cron存在、queued日志和pending只说明触发状态，不证明作者或manager正在工作。

## 当前派发后端（用户指定 Codex 子代理）

- 用户“后续任务用codex子agent来做吧”覆盖此前 Claude 恢复约定；后续任务使用 Codex 原生子代理，不再启动或续派 Claude。
- 2026-10-18用户另行明确要求Claude Code和DS加入DFX定位：允许本机现有claude/allrouter与claude_ds入口各一个独立只读排查者。只读源码/已有证据，不成为第二writer、不运行设备/测试；原Codex DFX作者继续唯一补丁/实验执行。具体会话与状态见dfx-independent-readers-current.json；其他后续任务仍用Codex。
- 两条线已由原 Codex 作者接任；历史交接清单见 `codex-backend-handoff.md`。同一条线始终只有一个 writer，保留原 worktree；当前作者及任务以 registry 为准。
- Codex 派发仍执行最新 main/all-open PR 去重和任务级 overlap review；记录 agent id、工作区、范围与交付路径，原生完成通知到达后审查结果。Claude UUID 和上下文压缩门禁不套用到 Codex。
- 30分钟兜底与现有任务完成通知保留。后端切换不扩大实施或发布授权，也不改变单提交、验收和本地测试限制。

## 执行约束

- Pipeline A 当前交付是改造普通异步模型；capture 是前瞻设计约束，不把 capture/replay 接线列为当前任务或验收前置。按用户单独批准的范围推进。
- 用户已明确重启原P4的DEVICE连续异步调用任务，具体批准范围见 registry 最新合同及 review/p4-device-chain-decision.md；其余外部主线/workload工作不接管。③DFX、④prepare/上传、⑤内存归一化按已登记授权和当前后端推进；单个PR合入不代表整条工作流结束。
- 每轮只读跟踪接手方主线：运行 `python3 .docs/claude-managed/refresh_mainline_snapshot.py`，审阅 #2429 新评论及实际关联 PR，并核对 #2425/#2415/#2316/#1853。记录证据 baseline、当前阻塞与下一步；外部报告不写成 manager 实测。只推进用户已明确重启的P4范围，不接管接手方任务、不重复跑测试、不代发评论。读取失败保留原核对时间。
- 保留原 worktree，一条线一个 writer；原 Claude 收尾完成后登记 Codex 接任，不重复启动，不接管外部作者代码。
- **每次派发前核对最新main及全部open PR（用户明确要求）：**先运行 `dispatch_preflight.py --out <snapshot.json> --prompt <task.prompt.md>`，完整分页获取open PR并获取main精确对象。不能只查已知任务号或旧registry；阅读main相关源码/已合入改动，以及可能重叠PR的正文、实际diff和作者。已合入则撤销重复任务；已有open PR覆盖则跟进原作者或明确互不重叠的范围，不再启动另一实现。CI修复还须核对实际测试merge/base是否包含已合入修复。
  - 按 [派发前核查](dispatch-preflight.md) 写任务级review JSON，Codex 派发由 manager 将任务文本绑定到核查记录并在 spawn 前再次查询 main/open PR；旧 Claude 派发脚本只保留审计用途，不再执行。缺记录、查询失败、main或PR状态已变化均停止派发，先重新核对。此核查由manager完成，不是向用户重复请批。
  - 原作者开始修改及发布前也再核对相关PR，覆盖核查后发生的合入/新PR；发现覆盖就停止重复实施并报告。已有运行中的writer保持唯一，不因新增此规则重复启动。
  - 发布前核查与 push/PATCH 分开调用，显式检查核查 exit code 为 0 后才能变更；断言失败必须停止，不能放进失败后仍继续的 shell 命令串。bot 或元数据变化也先阅读、分类并记录，再执行已授权发布。
- 每次新任务用 Codex 子代理：先确认该线 idle、核查通过，再派发。运行中反馈交当前唯一作者；后端交接前保存待反馈，收尾后核实接任者已读取。
- 检查交付和实际代码，静态评审精确 head；将具体问题交同一作者。
- **单提交交付（用户明确要求）：** 后续创建/更新 PR 必须执行 github-pr / fix-pr 标准流程；修复折入原提交（amend/squash），不得持续追加修复提交。发布前核对最新基线和完整 diff，确认相对目标 base 只有一个 commit，并在交付记录中保存计数；manager 验收也检查该项。共享/外部作者改动必须完整保留，不因 squash 丢失。#2571 本次由用户自行 squash，manager 和原作者不改其提交历史；此前窄修的保留历史限制不沿用为后续 PR 的默认流程。
- **按完整功能组织PR（用户最新要求）：**一个PR必须交付选定公开使用场景的完整行为，含持续运行、资源/背压、失败/关闭及真实API验收；容量、接口和寿命修补是内部步骤，不各自拆作交付。必要依赖同PR补齐，独立可选能力才另立项；不能以CI绿代替功能闭环审查。详见 `complete-feature-delivery-contract.md`。
- 查询CI前，从目标CheckRun的detailsUrl解析workflow/job，并核对workflow名称、head和job清单；同一提交的docs与CI是不同workflow，不能取第一个链接后冒充全CI步骤证据。
- CI不能只读job汇总：对运行中的自有job检查steps；内部step已failure时立即记录失败，即使job仍在Complete job。记录清理状态与测试结果两个事实；日志巨大时用明确起止字节Range读取并核验206，不把全文下载超时当无日志。
  仅在该任务已获实现/发布授权时使用 git-commit、github-pr、fix-pr；design-only/HOLD 不得实现或发布。
- 设备队列监控使用 `task-submit --status/--log/--list` 只读查询；不要对 pending 任务用带超时的 `--wait` 或 `--run` 监控。当前安装工具在等待超时/客户端信号退出时可能取消 pending；`--timeout` 不是无副作用的查询时限。任务执行上限用提交时的 `--max-time`，不靠反复等待客户端限制。已受理任务不得重复提交；若工具明确取消且证明尚未分配设备、实际0执行，先保留失败证据并由manager核准一次替代，不能当成测试复跑或实验结果。
- 提交客户端同样不得用任意墙钟超时杀掉：不得用 `subprocess.run(timeout=...)` 或 `timeout task-submit` 包装首次提交。任务运行上限用 `--max-time`；提交采用可持续查询的进程，执行前保存唯一attempt，立即记录client/supervisor PID、startticks、精确argv和独占回执文件。等公共提交协调锁时仅查询，不kill、不重提。若受理未知，先核精确命令的任务/临时元数据、原进程与输出，证据不足保留未知；不能用当前队列空冒充历史未受理或已取消。
- 不合并。不重跑既有或已经执行过的新本地测试。CI 按用户最新授权由 manager 自主判断是否重试，不再逐项请批：同一失败问题最多重试两次，同样错误出现三次就停止重试并定位修复。既有重试计入次数，不因换 head/rebase 清零；禁止整 workflow 盲重跑。
  每次使用精确 job，先核当前 head/workflow/失败证据及持久计数，等待上一 workflow attempt 结束后串行提交。accepted POST 立即消费；受理不明先核实，不能重复提交。同一问题两次重试额度耗尽后转修复；旧逐项授权耗尽不妨碍本次新政策下尚余的额度。外部 PR 仍由原作者负责。
- 新架构合同或跨工作流需具体审批；用户合入后自动选同一授权工作流的下一项有界任务。
  Ready 是用户检查点，不是整条工作流结束。不得把设计完成写成主线完成。
- 不新增 PTO_ 或 PTO2 标识符；既有错误码引用与新定义分开，保留外部兼容性。

## 每轮收尾

- 更新 registry 最新记录、workspace 和 `.docs/run-pipeline-track-a.html`。阶段合入、验收或依赖变化时，同步 `.docs/run-pipeline-track-a.md` 的步骤表与 P0–P6，核对 #2429 里程碑；不得只更新 HTML 时间戳。
  对外展示统一区分 main 已合入、PR 当前 head Ready、历史 Ready 与后续审查撤回。CI CheckRun 与外部 status 分开计数；summary/handoff 的嵌套 ready、awaiting_user_merge、当前作者列表也必须一致，历史快照不改写。
  新状态只维护在登记表与看板，不复制回定时消息，也不把每次历史重写成一大段触发文本。
- 顶部合并“待用户确认 + 当前任务与负责人”，每项可展开目的、改动、边界、证据和下一步。
  已合入 PR 只留在下方交付 DAG；先整体进度，再各线下一步。E2/P3 已完成前置归组，目录只在详情。
- 任务详情首先解释用户要判断的交付：原问题与具体例子、改动后的行为、适用范围/边界、下一步；验证结论只占简短一段。job ID、POST/消费、session/context百分比、轮询历史放证据/registry链接，不得替代任务内容或堆在用户详情。
- 全页一致刷新：顶部任务/数量、整体 DAG、分支进度、任务详情、§08 实施状态使用同一批事实；旧架构图明确标历史或设计目标。不要只更新时间或顶部卡片。逐行核对 `#managed-task-dag` 总览主图的已交付、当前工作和剩余差距；不能用更新分支图代替总览。核查内部锚点、详情链接以及旧待批/旧 OPEN 状态；合入不等于父 Issue 自动关闭。
- “待你确认”只统计已有具体问题且确实需要用户决定的事项。CI失败、内部审查和作者待交付由manager负责，分别显示为阻塞/审查/进行中，不把没有具体问题的“待处置”算成用户待办。同一确认项只在顶部提出，下方图表引用该项，不重复催批。
- Ready 必须有当前 head 的 manager review、已处理阻塞意见及相关 CI 完成；head 改变则重审。
- 子任务退出（成功或失败）会落盘到 `completion-events/`，通过同一 Codex thread 主动触发续办；与 cron 共用锁及 pending 标记，多个事件合并入队。事件只表示作者结束，不等于交付通过。读结果并处理后用 `python3 .docs/claude-managed/manager_wakeup.py ack <prefix>...` 标记已审；收尾再次检查 inbox，避免漏掉本轮新完成的任务。
- Codex 子任务保留原生完成通知，并在原子写好报告/result 后、final 前运行 `python3 .docs/claude-managed/manager_wakeup.py complete --name <task> --prefix <unique-phase> --result <absolute-result-path>`；该入口将结果写入持久 inbox 并唤醒既有 manager thread。result 必须位于本 manager 的 `.docs/` 内；每阶段 prefix 唯一，重复调用不重复发送。成功或失败均通知，保存 queued/coalesced/失败回执；受理不明不盲重发。后续任务 prompt 必须包含此交付步骤。旧 Claude 退出入口仅保留审计与兼容，不启动新任务。原生 Completed、持久完成队列与30分钟兜底三者互补；agent异常终止而未落盘时仍须由manager核实。
- **完成通知必须推进工作：** 原生 Completed 只代表子任务结束。manager收到后同轮读取报告和实际差异、核对产物身份，登记通过/退回/阻塞，并在既有授权内继续派修或首次验证；不得只转述通知、等待下一次cron或等用户再次催办。尚有本轮可处理的子任务结果时，不以final结束本轮。预计很快交付的子任务使用原生等待接续处理；只有确有外部等待或长时执行时才交代准确状态。
- 有可推进任务或 CI 在途时保持 30 分钟兜底定时器，子任务完成事件不等下一次 cron。全部只等用户决定/确认，或授权目标全部完成时，
  删除 manager-tick.enabled，并仅移除含 `# pipeline-a-manager-tick` 标签的 cron 行，保留其他 cron。
  记录合入/新授权为重启条件，恢复30分钟兜底及完成通知；暂停后不声称后台仍轮询。通知队列明确失败会保留事件并撤pending，供cron恢复；若发送超时且受理不明，保留pending并记录manager-wakeup-error.json，下次人工/已排队续办先核对，不能盲目重复发送。

## 设计资料持续备份（用户已授权）

- 用户要求后续更新后备份到个人fork；每轮设计/路线/看板有实质更新，收尾同步现有 `ChaoWao/simpler-fork` 的 `codex/pipeline-a-design-docs` 分支，不再逐次请批。
- 隔离工作区 `../pipeline-a-design-docs`，目录 `docs/zh-cn/pipeline-a-design/`；只同步已选设计、合同、报告与进度，保持当前原则/历史合同/未审方案标签，新增重要设计先明确状态再加入索引。不上传运行日志、会话、机器配置或敏感信息。
- 在manager运行 `python3 .docs/claude-managed/export-design-docs.py`；按manifest源文件指纹增量导出，未变文件保留已校验格式。检查脱敏、Markdown格式、链接/锚点及状态一致性；格式修正后重算manifest的export_sha256。
- 依git-commit流程提交文档并正常push到origin的同名备份分支，验证远端精确commit并更新design-docs-publication.json；origin必须是simpler-fork（旧ChaoWao/simpler会重定向上游）。保留备份历史，不强推、不创建上游PR。
- 无实质变化不制造空提交；工作树有非本次修改或远端前进时先核对保留，不覆盖他人内容。推送失败记录未备份状态，下次续办补传，不能声称已备份。

## 文件分工

- `manager-tick.prompt.txt`：固定短触发消息，只指向本文件。
- `CONTINUE.md`：稳定流程与限制，只有流程变更才修改。
- `registry.md`：最新任务、授权与执行状态；保留历史审计。
- `../review/`、各 `*.prompt.md` / `*.result.json`：按任务读取的具体合同与证据。

新用户授权和限制先记录到登记表；本入口不自行新增、撤销或扩大授权。
