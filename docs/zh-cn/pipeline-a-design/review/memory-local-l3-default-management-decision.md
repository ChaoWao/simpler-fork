# 本地 L3：默认管理四类工作区 — 用户已批准

设计归档快照：状态与阅读顺序见[索引](../README.md)。设计约束、历史合同和当前实现须分别阅读。

基线410502f7b。用户已批准实现并交付一个PR；以下范围优先于原作者rev3的成本概括。

## 现在与改后

L2已经默认用WorkspaceManager管理内部空间。本地L3的chip子进程还走原始allocator路径。首步让这些子进程默认接入同一管理器：临时tensor副本、编排中间数据、共享状态、runtime arena四类；用户device_malloc分配仍归用户。

例如前两次run需要8MiB，容量足够就复用。下一次需要12MiB时先取得新块，成功后发布新地址，失败则本次run报错且旧计划保留。被替换的旧块等真正消费者、回拷和绑定引用结束后，在后续prepare或finalize回收。这里8/12MiB仅为说明，不是预分配或预算常量。

## 一个PR的确切范围

- 仅用户直接创建、自己init/close的本地L3，a2a3/a5 onboard program，HBG/TMR。父级fork点以device_ids和startup-root条件传递现有manage_workspace开关；nested/remote/MPI/L4/SUB、sim与kernel行为保持。
- 不新增公开配置。L3仍不要求或开放workspace_budget_bytes，不设设备均分额度。
- HBG按构图实际布局取得arena；TMR沿用已知配置的启动准备；后续均保有足够容量并复用。不是每run都malloc，也不是预留最大可能内存。
- 使用已有run退休事实、release失败记录、隔离和terminal sweep。保留设备完成与诊断失败的区别。未知完成不准重新出借相关块；退出进程不等于证明设备已停止。
- 父子IPC及现有close步骤保持；不新增工作区查询命令/关闭门禁。子进程finalize失败沿现有非零退出和teardown报告向父进程传播。无进程内恢复新承诺。
- 不接capture/replay，不推导tensor依赖，不改变用户地址释放权。

## 代价与限度

每次复用增加管理器引用登记及锁开销。保留旧代会增加占用，容量变小时当前块保持温热；分配仍可能OOM，失败会报错。

不能笼统声称旧arena扩容峰值为max(old,new)：既有arena事务本来就先申请替换块后释放旧块，新旧同时占用已存在。retained_temp原未管理路径先释放再申请，其失败保全才新增这一峰值；arena的额外代价主要是安全退休前继续保留旧代。

不承诺“全局每region最多一个旧代”：引用槽数限制每块引用数量，不是块总数硬上限；失败释放、隔离和准备回滚保留需要按实际状态解释。正常路径按消费者退休回收，异常未证安全者不复用。无强制内存上限或性能收益承诺。

## 验收与下一步

批准已执行，原memory作者正在实施一个PR，验证真实Worker本地L3路由、两架构管理开关和sim/nested排除；新增病例覆盖稳定复用、扩容失败旧计划不变、最后消费者回收、正常/失败关闭传播。不得重跑已执行测试或CI；新增测试按授权策略仅首跑。manager评审精确head、反馈和当前CI后交用户合入。

原作者依据：[L3机制与源码证据](../README.md#archive-boundary)。
