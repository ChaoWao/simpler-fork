> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# DFX 丢失归属：完整设计评估

状态：推荐交接身份方案已通过 #2505 合入 main daa1670c0（2026-10-08）；下文保留原设计基线和评估，不代表诊断提前入队已实现。旧版“一律 global / null”提案撤回。基线 main `a711032ed3a582d241b94af5a045bdb3d5046869`。这是 manager 的独立源码评估，未执行测试、故障注入或板端验证。原 temporal 同会话派发通过去重门禁，但模型服务返回 400，未交付报告；不计作第二份独立评审。

## 结论

**推荐把 run 身份随 ready queue 交接项一起发布；payload 保留身份用于交叉核对。队列 cut 负责证明接收结束，终态负责核对总账。这三种信息各司其职。**

现在不能可靠归属的原因是身份主要在 buffer 内容中，而部分失败恰好发生在取得内容之前。不是设备串行运行不能区分 run，也不是后台输出天然只能报告全局丢失。把“谁的内容”放到交接元数据，就能在内容读回失败时保留归属。

撤回将所有保留模式 `transport_retired` 改为 `null` 的建议。已知归属仍按 run 记数字；只有交接身份本身不可信、相关边界也无法证明时，报告未知完整性，不能伪报为零。

## 1. 目前实际工作过程

1. AICPU/AICore 写 buffer，buffer 头含 `run_epoch`、`local_seq`、记录数。
2. AICPU 代各类生产者提交 ready entry。现有 entry 是 32 B，包含类型、core/thread、设备地址、buffer 序号，**没有 run_epoch**。
3. 先写 entry，再 `wmb()`，最后推进 queue tail。Host 观察 tail 后重新读取这一 entry，防止批量镜像先读 entry、后读 tail 得到旧内容。
4. Host 解析地址并读回 payload，然后 ack 队列，再交 collector；collector 根据 payload 的 run_epoch 找到本 run 的记录。
5. 地址无法解析，或 payload D2H 重试耗尽，可以退休 entry，但 payload 身份没有取得。当前只增加 collector 的 `drain_dropped_buffers_` 总计数；`run_close` 在某个时刻取走这个数，不是按身份分配。

设备执行 A→B 串行，不等于主机此刻收到的一定是 B。多个生产者队列、D2H 重试和后台处理会使 A 的内容延后到 B 执行时才被接收。因此不能拿“当前运行的是谁”来补身份。

## 2. 四种方案比较

| 方案 | 能解决什么 | 主要缺口与成本 | 判断 |
|---|---|---|---|
| A. 提前读取 payload 头 | 头可读、主体不可读时可识别 run | 地址无效或头也不可读仍无解；增加一次读取或拆分读取，头必须在回收前固定 | 可作优化，不能作为唯一依据 |
| B. ready entry 带 run_epoch | 已取得交接项但 payload 失败时仍可归属；不需额外查询通道 | 需所有生产者一致发布、host 保留身份、处理 ack 重试及混合版本；entry 自身损坏仍未知 | **推荐主方案** |
| C. 独立 sidecar 身份表 | 可绕开 payload 读取 | 要有槽号、generation、防 ABA；必须与 entry 同时提交并按消费者退休。按地址查表会被地址复用覆盖；若与队列同寿命，等于重复维护 entry | 现有 entry 有空间，没有理由另建表 |
| D. 每队列 run 起止边界 | 有可信边界时可按入队位置推导身份，甚至应对某些 entry 读取故障 | 需要设备生产者逐队列提交边界、绝对序号、空 run 边界、异常未封口处理、跨 run 保存；现有 host cut 不是这种边界 | 保留作未来备选，首步不引入 |

**现有 cut 不能直接完成 D：**它在 host drain 的安全位置读取 head/tail，目标为“已消费数 + 当前队列积压”。拍下这个边界时 B 可能已发布，因此 cut 可以包含 B。它证明有限的一批内容被处理完，不证明这批全属于 A。若要 D，必须另加设备端 run 边界协议，不能只改一个计算公式。

## 3. 推荐的交接格式和生命周期

### 3.1 可利用现有 32 B 布局

按现有字段类型和对齐规则，建议布局如下；实施时必须加 sizeof/offsetof 断言并一致重建验证：

| 偏移 | 字段 | 作用 |
|---:|---|---|
| 0–3 | core_index | 来源实例 |
| 4–7 | kind | 记录种类 |
| 8–15 | buffer_ptr | 内容位置 |
| 16–19 | buffer_seq | 现有生产者内序号 |
| 20–23 | record_count（替换 pad） | 可选但建议一并携带：这次成功交接多少条记录 |
| 24–31 | run_epoch（利用尾部对齐空间） | 这次交接属于哪个 run |

run_epoch 是归属所必需；record_count 让 payload 整体不可读时也能知道丢了几条。若首步只增加 epoch，仍可准确统计“丢了几个 buffer”，不能凭此声称知道每个丢失 buffer 的记录数。

这不是添加新通道或每个 record 再带一份身份。按拟定布局，设备队列每项仍为 32 B。A3 是 4×992 项，队列数组 126,976 B；A5 是 5×1,456 项，232,960 B，均不因这两个字段增加数组容量。增加的是写字段的指令、host 交接结构和错误记账。不能据此承诺零开销或延迟收益。

### 3.2 发布顺序

生产者在仍独占 buffer 时保存本次 epoch、seq、count；写完整 descriptor，最后发布 tail。成功发布后不可再依赖读取这个 buffer 的头，因为 host 可能已回收它。

统一覆盖 AICPU task、sched phase、orch phase，以及 AICPU 代 AICore 的延期提交和末尾 flush。后者目前通过 task module 的 enqueue wrapper 发布，不能错误地把其 payload 当作 AICPU task 类型解释；应显式传入已经固定的元数据。

payload 的 epoch/seq/count 仍保留。读取成功后与 descriptor 核对；不匹配说明交接或复用有问题，不能静默选一边当作成功。未成功发布的 buffer 仍由生产者负责，下一 run 重用前要清空并重新赋身份；生产者丢失计数与“成功交接后丢失”分开。

### 3.3 Host 先保存身份，再处理内容

每个 queue 仅由其原 drain owner 消费。读取并验证 descriptor 后，把身份放入一个有界的 pending receipt；后续成功送到 collector，或失败记录丢失，都用这份身份，不从当前 active run 推断。

receipt 的重试身份使用 collector 生命周期标识、queue id、host 单调消费位置。**buffer 地址和现有 32 位 buffer_seq 都不能单独做去重键**：地址复用、seq 每 run 重新开始，环形 head 也会回绕。计数溢出须报错停用该通道，不能回绕；重建只在旧 drain/producer 停止后进行，不能清掉尚未解决的错误。

一个 queue 最多保留一个正在处理的 pending receipt，成功交付的身份随现有 host ready ring 传递，失败数累计到最多两份未发布 run 的账本。无历史全量映射表。按目前 64 位结构布局估算，ReadyBufferInfo 从 40 B 扩到约 48 B 即可容纳 epoch/count，增加约 8 B×host ring 容量×shard 数；再加每 queue 常数大小的 receipt 和每 run 计数，需纳入现有 host 预算，最终以实现布局为准。

### 3.4 ACK 是独立问题，身份字段不能自动解决

现有 ack 写 head 失败会恢复 host 影子值并重试。代码没有证明“返回失败就一定没有写到设备”。因此评估不能把 ack 模糊结果当作已证实未消费。

推荐保留 pending receipt、old_head、expected_next_head 和 payload 所有权，先核设备 head：仍是 old 则可重试；是预期 next 则完成这一次消费；读取失败或其他值则保持未知并停止推进该 queue。不得重复算丢失、重复交付、提前回 free queue。只有原单 consumer 且没有 reset 的条件下，这个二选一核对才成立。

归属已知也不等于可以安全释放。无法证明设备/队列所有权转移时应隔离，沿已有错误路径报告；不在此候选中暗中新增 reset 或恢复协议。

## 4. 故障矩阵

| 情况 | run 归属 | 必须采取的处理 |
|---|---|---|
| descriptor 合法，payload 正常 | 已知 | 核对身份及 count，交付该 run |
| descriptor 合法，payload D2H 持续失败 | 已知 | 重试耗尽后按该 run 计 buffer 丢失；count 可信则计记录数；不能记到正在执行的后继 |
| descriptor 合法，地址缺 host mapping | 身份可独立成立，地址所有权另判 | 计该 run 的传输故障；未知地址绝不能直接 free；若出现其他元数据矛盾则转协议错误 |
| descriptor 的 kind/index/epoch/count 自相矛盾 | 不应声称可信 | 协议错误；不能仅因 epoch 恰好等于活动 run 就强行归属 |
| payload 身份与合法 descriptor 不符 | 有预期归属，但交接完整性不可信 | 报出双方身份，禁止当作完整输出；不能用 payload 覆盖原交接身份 |
| queue head/tail 或 descriptor 读不到 | 尚未知，不等于“空队列” | 有限重试；持续失败标记 cut/完整性未知并报错，不擅自 ack |
| descriptor 已读，ack 结果不明 | run 已知，消费状态未知 | 保留 receipt 与资源，核 head 后仅结算一次；无法核实则隔离 |
| 设备发布未成功（如队列满） | 生产者知道自己的 run | 由生产者 dropped/live/terminal 核账；不重复算 host 退休 |
| 正常 run_close 后、cut 完成前才收到失败 | 有 descriptor 则已知 | 更新该 run 的丢失，再推进 cut；close 不能先冻结最终计数 |
| cut 完成且文件已发布后又出现旧 run | 身份已知但协议违例 | 报 late-entry 错误，不能算到后继，也不悄悄改已发布文件；需要查明 cut/生产者封口是否失效 |
| reset、进程断连或 terminal 不可读 | 可保留已经取得的身份，整体完整性未知 | 不猜未见内容的归属/数量，不承诺无损；停止把相关结果标成完整 |
| terminal 或累计计数饱和 | 已见条目仍可归属，总数不精确 | 报未知完整性，不使用饱和值做精确守恒等式 |
| 新 host 配旧 producer（或反向） | 无可信新协议 | 必须一致重建/版本能力拒绝；不能把旧 padding 中的值解释为新 epoch |

descriptor 方案承诺的是有效发布协议下的身份分离，不是对任意内存损坏提供认证。若身份元数据和可用边界都毁损，任何方案都不能凭空恢复；sidecar 也只是把这条边界移到另一片存储。

## 5. 对账、文件和报错

将四件事分开：**属于谁、实际收到了什么、buffer 能否回收、记录是否完整**。

- 每 run 保存 received、transport-retired 和 terminal 证据。丢失事件先计账，再递增 cut 的 consumed；成功路径还须等 collector 处理水位到达，才能封存。
- 可靠 terminal 的 published_buffers 应与已收/已退休的成功交接 buffer 对账。若 descriptor 带 count，published_records 同理。producer 的未交接丢失/live 残留另外处理，不能重复加进 host 丢失。
- 数值 `transport_retired` 保留为该 run 已确认的退休数。明确归属的丢失不能让所有 run 一律变 null。
- 若确有不可归属故障，应额外报告“采集完整性未知”，注明它影响的未封存 run 范围；不能把数字0解释成完整无损。优先保留数值字段并加明确完整性状态，避免全量改变字段类型。具体 JSON 字段名和消费者兼容须在实施合同固定，本文不冒称已实现。
- 建议相关 flush 明确报出已知 run 的丢失；后续若采用 sticky error，错误摘要保留原 run，不能把旧错误重新归属给新 run。是否继续运行与错误归属是两项不同政策。
- flush 的成功仅覆盖指定输出水位及其已处理错误，不是任意未来故障的保证。旧路径中最终 stop/drain 发生在前一次 flush 之后；该阶段错误只能保证现有日志通道，除非另改关闭回传协议。此限制不因加 epoch 自动消失。

## 6. 最小实施范围与验收

建议先完成 **泳道交接身份、按 run 丢失记账和 ack 不确定状态处理** 这一完整传输边界，再让诊断提前入队消费该保证。先不增加 sidecar、设备端跨 run 边界表或对所有 collector 的新抽象，也不开放 capture。

即使提前入队首步只针对 A3 HBG，ready entry 及 producer 是共享代码，需要覆盖 A3/A5、四种 program、适用 kernel/sim 路径，统一布局与构建，不可只改一个 host 分支。

必须验证但本次未运行：

1. 两个 run 混合积压、两队列交错接收；A 的 payload 读失败，只给 A 计一次丢失，B 正常。
2. 空 buffer、零记录、末尾 flush、AICore 延期提交及 phase 路径，epoch/count 与 terminal 一致。
3. 地址快速复用、队列回绕、重复 peek/ack、ack 在写前/写后报错；不重复交付/退休，未知状态不释放。
4. 读 header/descriptor 失败、身份不匹配、terminal 缺失、饱和；不能以“队列空/0丢失”冒充成功。
5. run_close 与 loss/cut 并发；最终计数包含 cut 前退休，后继不能冲掉前驱账目。
6. 默认输出数字兼容；真正未知状态明确报错；flush/close 的实际错误边界与文档一致。
7. 静态 ABI 断言、一致构建、生产者全部写字段；不支持旧新二进制静默混用。

用户已于本轮批准本评估推荐方案，实施/发布一个交接保证 PR；不随本 PR 开放诊断提前入队或 capture。旧“保留模式一律 null/global”条款撤回。

## 7. 证据与覆盖核查

基线代码（链接固定到同一 main 对象）：

- [ReadyQueueEntry / 队列布局](https://github.com/hw-native-sys/simpler/blob/a711032ed3a582d241b94af5a045bdb3d5046869/src/common/platform/include/common/chip_swimlane_profiling.h#L562)
- [生产者字段写入及成功交接计数](https://github.com/hw-native-sys/simpler/blob/a711032ed3a582d241b94af5a045bdb3d5046869/src/common/platform/shared/aicpu/chip_swimlane_collector_aicpu.cpp#L219)
- [entry 先写、tail 后提交](https://github.com/hw-native-sys/simpler/blob/a711032ed3a582d241b94af5a045bdb3d5046869/src/common/platform/include/aicpu/profiler_device_engine.h#L126)
- [host peek / ack / payload 处理](https://github.com/hw-native-sys/simpler/blob/a711032ed3a582d241b94af5a045bdb3d5046869/src/common/platform/include/host/profiler_base.h#L418)
- [按 queue 捕获有限 cut](https://github.com/hw-native-sys/simpler/blob/a711032ed3a582d241b94af5a045bdb3d5046869/src/common/platform/include/host/profiler_base.h#L1785)
- [run_close 当前取 scalar loss](https://github.com/hw-native-sys/simpler/blob/a711032ed3a582d241b94af5a045bdb3d5046869/src/common/platform/shared/host/chip_swimlane_collector.cpp#L3164)

派发前已核全部 32 个开放 PR。相对上次，#2499 已关闭未合入，新增 #2502 `629399e56` 为 scalar 声明语义及图缓存/示例修改，已读相关实际 diff，没有实现本文交接身份。既有外部 #2028、#2501 等仍归原作者，不接管。主线 #2429 最新快照 06:24 UTC，14 条评论，已跟踪的三关联 PR 均已合入。
