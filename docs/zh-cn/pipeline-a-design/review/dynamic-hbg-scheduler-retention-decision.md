# 上传支线下一步：复用 A5 HBG 调度状态存储

设计归档快照：状态与阅读顺序见[索引](../README.md)。设计约束、历史合同和当前实现须分别阅读。

状态：用户已于 2026-09-23T11:40:13.872843+00:00 明确批准本合同并交付一个 PR。实现与 git-commit/github-pr/fix-pr 发布维护已授权；不合并。经理稿优先于作者细节稿。代码核对基线906060582，具体实施仍排在2440合入之后；2440已于2026-09-23T12:28:07Z合入9ca91c522，前置条件满足；从新main启动原dynamic会话，无需再次申请本合同批准。

## 解决什么

A5 HBG resident调度路径每次bind为调度状态分配device块和host暂存区，初始化后完整上传，run结束再释放。相同或较小规模的后续run仍重复这些分配。

本PR让同一pipeline槽保留自己的两侧存储，容量足够且此前消费者结束时复用。每次仍初始化本次有效范围并完整上传；只去掉重复分配/释放和host多余的一次全区清零。节省上传字节为0，不改变拷贝次数，不承诺延迟收益。

## 所有权与失败

- DeviceRunnerBase按槽持有存储；Runtime仅临时引用。设备执行、DFX读取、输出回拷和bindings消费者结束后才可复用；源码现有reservation阻止同槽重入。
- 保留现有同步、准入、错误停止与诊断规则。停止未证明时不能因idle标志允许覆盖。
- 增长先准备host，再申请device新块；分配失败保留旧device地址/容量。新块记录后释放旧块。
- 每槽仅一个释放失败记录。该记录占用时拒绝任何后续增长，容量内复用仍须满足健康/寿命条件；不覆盖失败记录，不无限积累旧块。
- 正常关闭将free结果保存后再清holder；失败块交已有allocator终局处理。回收未知明确报告，不据reset成功推定释放；abandon分支不再调用device。

## 成本与范围

只改A5 onboard HBG resident调度状态持有方式，host/sim接口需一致可编译；A3、TMR、GRAPH fallback算法、P4、capture和新DFX模式不在范围。

每槽保留历史最大容量，闲置或转legacy也继续占用至关闭。无诊断额外buffer时100task基础约0.31MiB/device和0.31MiB/host每槽；两个槽稳态各侧约0.61MiB。增长时新旧共存，两个槽都增长时可达各侧约1.2MiB，同规模估算非通用上限；更大任务数/诊断配置还会增加。每槽device最多当前块加一个失败旧块；host扩容也有旧新瞬时共存。

本PR暂不纳入workspace_budget_bytes四区域额度，报告仍是partial；不能据此称永久排除统一管理或设备总预算受控。增长free与关闭free仍存在，仅稳定复用省掉逐run分配/free。

## 交付与验收

2440合入后刷新main，交付一个完整PR；不另造工具/报告前置PR。真实bind/release路径验证：同容量复用、增长失败旧计划保全、连续free失败不会丢owner、不同slot不串用、legacy转换不发布陈旧bootstrap、失败/关闭不访问reset后资源。仅真正新增未执行case首跑，现有或已执行case不重跑；CI重试需逐项授权，不合并。

本决策不把结构性减少分配包装成已测性能收益。

审查依据：[rev3处置](../README.md#archive-boundary)；原作者细节在相邻dynamic worktree的.docs/upload-after-2438-next-decision.md，永久预算排除及free次数两处以本稿为准。
