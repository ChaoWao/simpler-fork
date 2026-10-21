> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# P1 §2.3 / P2 #2267：交付与接手计划

> **历史设计 / 评审快照**：保留文中的原始基线、结论与验证记录，不代表当前待办或支持范围。当前进度统一见 [路线与阶段表](run-pipeline-track-a.md) / [HTML 看板](run-pipeline-track-a.html)。

进度核对：2026-09-18 07:19 UTC；GitHub PR 状态与 main `0a68792ed`。
此处记录 main 的交付状态，不表示各 worktree 已包含全部合入提交。

## 已交付与当前分工

| 工作 | 归属 / PR | 当前状态与下一步 |
| --- | --- | --- |
| P1 §2.3 诊断基础 | temporal，[#2283](https://github.com/hw-native-sys/simpler/pull/2283) | 已合入：每槽结果区、epoch、TMR 诊断保存及保留寿命。 |
| 公共终态格式、发布与读回；两架构 × TMR/HBG 普通入口 | lexical，[#2298](https://github.com/hw-native-sys/simpler/pull/2298) | 已合入：成功也发布，host 无条件读回，与整流同步结果旁路核对。a5 HBG legacy 入口仍未决。 |
| callback owner、fork/reset/迟到通知 | lexical，[#2303](https://github.com/hw-native-sys/simpler/pull/2303) | 已合入（`c6af44fe5`）；范围为通知与日志，不含健康消费者。通知不能直接归属 run。 |
| 输入 staging / 输出回拷 / binding 释放接口 | [#2295](https://github.com/hw-native-sys/simpler/pull/2295) | 已合入；接口可独立排序，生产调用仍相邻，不自行移除同步或放开准入。 |
| 正常 drain、健康 / 未决恢复与资源退休 | lexical / [#2267](https://github.com/hw-native-sys/simpler/issues/2267) | 未交付；验证无同步可见性后接线。main 仍同步整条 stream pair。 |
| §2.4 DFX 身份基础 | temporal，[#2293](https://github.com/hw-native-sys/simpler/pull/2293) | 已合入 scope_stats / swimlane buffer 身份与 host 值副本；其余 collector 和连续采集未完成。 |
| swimlane 准确计数与 buffer 保留复用 | temporal，[#2313](https://github.com/hw-native-sys/simpler/pull/2313) | 已合入（`14918c37c`）；两池发布后归还与复用用例已补，历史设备 timeout 归因仍未定。 |
| 多 launched 准入、跨流 join、DFX 过渡保护 | 后续 P4 / #1853 | 等 #2267 及资源安全条件成立。 |

**结果区已经统一：#2298 扩展 #2283 的同一份协议。** 不需要第二套缓冲，也不需要合并 worktree。
280 B 区域的 payload offset 为 24；host/device 必须使用匹配的格式。

## temporal：继续 §2.4

矩阵与方案已有，身份基础 #2293 与 swimlane 独立 buffer 修复 #2313 均已合入。
本目录下一步补齐 args_dump / dep_gen / pmu 的身份与 buffer 协议，再推进会话复用、配置切换排空与输出适配。
当前逐 run 重置、采集排空及输出完成合同仍保留；不能先删清空再补身份。

先前“residency 6/8 失败”已撤回：六轮是日志重定向失败，pytest 未启动。
它不是既存设备缺陷证据，也不能为本次修改免责。首个 run 的 timeout 原因尚未定位；
共享负载与调宽超时后的通过都不足以证明根因。

普通路径 P4 不等待连续采集全部交付：诊断路径若仍未满足连续运行合同，
由 P4 在 launch 处接入双向独占保护（普通→诊断、诊断→普通及诊断→诊断），
继续允许合法 prepare 重叠。当前单 launched 已强制独占，不提前添加冗余门。
缩小 collector 重建集合的优化已否决，不属于本项。

## lexical：#2267 E1 → E2（#2330 已合入）

E 是接线阶段的总称，拆成两个交付：E1 设备健康消费者是当前下一项；E2 生产 poll/drain 与退休依赖 E1。E1 保留正常 drain 同步，E2 才移除正常成功路径的整流同步。

1. #2303 callback owner 已合入；继续接入设备健康消费者，不从 stream_id、task_id 或到达先后推导 run 身份。
   owner 合入本身不交付健康消费者，也不完成 #2267。
2. #2330 已合入成功路径晚读探针与纯判定规则：N+1 执行时读取 N，D2H 不等待后继。
   E1 完成后进入 E2 的生产 poll/drain 与退休接线；补故障变体，设备发布成本仍未测量。a5 HBG legacy 补覆盖或保留同步。
3. 正常 drain 分开 run verdict、device health 与 retirement。缺失、epoch 不符、格式无效、
   读回失败均为未决，不得当成功；异常路径保留有界恢复。
4. 衔接 #2295 的结果回拷 / binding 释放接口。失败已知不表示设备静止；
   资源释放仍需双流边界及 outstanding wait 引用均满足条件。
5. P4 前处理共享 header 后备与 DFX 独占门，明确 runtime 状态码替代 ACL 错误码的调用方语义。
   当前生产准入继续保持单 launched。

沿用 lexical 的 [四行完成契约](ARCHIVE-INDEX.md#ref-06a0b4419a729b73)
与 [实施指导](ARCHIVE-INDEX.md#ref-5723311a17ef8bdc)。
已完成的探针直接复用；不重跑已有用例或整套 sweep，新场景先设计并按授权范围执行。

## 可并行的 P3：#2316

| 交付 | 状态 | 下一步 / 接入条件 |
| --- | --- | --- |
| PR1 父 run FIFO 覆盖 | [#2314](https://github.com/hw-native-sys/simpler/pull/2314) 已合入（`0a68792ed`） | 当前 terminal 放行边界有回归；P5 改为更早放行时重验。 |
| PR2 跨 run build 所有权 | 待交付，尚未关联实现 PR | 审计实际 SO binding 与 recorder 寿命；复现支持路径的干扰才加 guard。 |
| PR3 host 访问与输入就绪 | 待交付，尚未关联实现 PR | 使用 #2295 接口覆盖调用方等待 / 回拷、producer 拒绝及合法 early prepare。 |

[#2316](https://github.com/hw-native-sys/simpler/issues/2316) 仍开放，#2314 只完成其中 PR1。
建议接下来做 PR2 审计，再交付 PR3；P3 不等待 #2267，可与 lexical 的 P2 并行。
P4 接入前检查 build / host 访问合同；P5 改变激活边界时重新验证父 run 有序提交。
`submission_closed` 不是有序提交完成，单 endpoint 的 FIFO 也不提供跨 endpoint 排序。

## 可并行的 #2254 支线

- #2297 已合入 AICore 参数传值，#2257 已关闭；#2277 已合入 context 常量地址表。
- prepare_execution 直接插桩已由 [#2307](https://github.com/hw-native-sys/simpler/pull/2307) 合入。
  可基于函数自身 STRACE span 建立固定版本基线；P0 外围分解不是它的耗时。
- HBG host-only Runtime 尾部由 [#2309](https://github.com/hw-native-sys/simpler/pull/2309) 合入（`8726d7e13`，关联 #2308）。
- Runtime 按方向拆分与 TMR orch args 按有效长度发布继续独立设计；共享握手区拆分的初始化顺序需先证明。
- #2254 新增内存归一化方向（[分类、代码差距与验收](run-pipeline-track-a.md#02-2254-后续统一-device-内存管理)）：
  M1 资源需求清单 → M2 workspace 追加大块池 / M3 kernel_bin 有界常驻管理 → M4 地址与寿命验收。
  外部 tensor 内存不接管；[§0.3 原则](run-pipeline-track-a.md#03-原则内部-workspace-在-init-定上限容量内复用)要求 init 确定 workspace 总上限，限内按需分配并复用，超限明确失败。
  M2 先保留 bank 与 per-run 版本隔离，增长不释放旧地址；launch / replay 不扩容 backing storage。
  这些工作尚无实现 PR；M2 / M3 可在 M1 后并行。真正共享 scratch 还需初始化与双流顺序证明。
- #2319（OPEN）记录长度 A/B，本次未辨识稳定收益；轮次漂移使逐样本区间不能作为因果收益上界。
  后续细分 H2D / 冷启动阶段并控制独立轮次；#2315（OPEN）跟踪具名 device 描述符。
- #2254 全部完成不是首个双 launched 里程碑的必要条件。

**路线：已合入 fence / 终态 → callback 与正常 drain 接线、验证 → P4 native 排队 → P5 Worker.submit。**

## 2026-09-19 DFX 交付更新

#2343 已合入（`6acd7a867`）：AICore 显式 live / published 记账与无 buffer 丢弃修复。
本目录接下来补轮转 → pending → ACK → 发布的单测，再完成最小设备身份探针；
完成后收口身份与记账基础阶段，再改会话生命周期。逐 run 清空与单 launched 准入仍保留。
此线与 lexical 的节点 E 并行，完整连续采集不作为 P4 普通路径的前置。
