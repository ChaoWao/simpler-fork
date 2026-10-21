> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# 三项实施与外部主线续办核查

## 当前实现

三位原作者分别从 main `50fb75b96` 建立独立分支，仍在写入，尚无可审查的完成提交。以下只是当前工作区观察，不是静态 PASS 或 Ready。

- memory：`feat/2254-l3-default-workspace-management`，开始在父进程选择本地 L3 startup-root 路由，并将 manage_workspace 传给 fork 后的 chip。后续审查必须覆盖远端/嵌套排除、真实失败回收、用户分配所有权不变。
- P4：`feat/p4-three-run-capacity`，开始扩展最大资源数、FIFO 租约分配和逐后继准备。尚需核实际 child pre-init 容量配置、READY 协商、三套存储隔离、等待引用及连续补队，不能仅凭上限常量改为3判交付。
- temporal：`feat/scope-stats-background-run-output`，开始分离独立主机记录块、终态快照和文件渲染。尚需核预算/分配失败、默认路径、完整 runner 接线、未知完成零共享读取、flush/close 和发布错误。作者旧 untracked 文件仍保留。

三条线各有一个原 UUID writer，结果文件为空代表尚未交付，不重复派发，不中断当前编辑。无 manager 产品修改或测试执行。

## 外部 PR 更新

| PR | 当前 head | 本次检查 |
|---|---|---|
|2447|fb8d1c280|相对原6836ae910仅整合2463测试接口修复；不据此升级成完整审查通过|
|2456|fb902a012|相对原b10e500f9仅整合2463测试接口修复；此前报告关闭顺序修正保留|
|2461|e4993eb73|相对原dbcfa819d仅整合2463测试接口修复；代码修正保留，正文与确定性回归待办仍未关闭|

2456与2461新 merge-base 均为50fb75b96。三者 delta 都是一份测试文件+8/-6；编译修复与新 main 对应文件相同。没有执行产品或既有测试。

2456 Ubuntu job108814767573已编译后实际运行，失败为 `LateBufferForASealedEpochIsCountedNotAppended`：late_after_seal得到0而预期1，unknown_epoch得到1而预期0；与先前2463/main窗口相同。修复基线编译问题并未修复这个分类窗口，2461正在处理。macOS取消，其余CI仍在途；不能以历史设备等待超时替代新head的失败原因。

2461仍把tombstone写入放在Closing发布前；PR正文却仍说在ACK成功后、隔离关闭不记录。最新diff没有提交可控制该交错的确定性回归。这两项仍由原作者处理，不以CI绿灯替代。

三面反馈复核：2447一个未决，2456五个未决（四个outdated、一个current），2461一个已解决线程。未代发评论、未接管外部作者、未重跑CI。

2462现已 CLOSED，作者留言 Duplicate with #2463；2463已合入50fb75b96。没有第二份独立修复需要合入。

## 证据

主线快照：`../claude-managed/mainline-2429-current.json`。新PR快照：`pr2447-continuation-latest.json`、`pr2456-continuation-latest.json`、`pr2461-continuation-latest.json`。反馈：`external-feedback-current.json`。日志：`pr2456-job108814767573.log`，均位于claude-managed。

## 最新CI收尾 — 2026-09-28T07:11:42.018635+00:00

2456 fb902a012全部结束16success1skip2failure1cancelled。新增A5job108814767443失败为结果保留用例5次completed-across-read；前驱结果全部有效，无法据此区分读回等待后继与后继恰在读回期间完成，根因未定。日志已获取，不能复用前head“设备未分配”结论。2447/2461均19success1skip；manager评审待办未闭合。无重试权限。


## 2026-09-28T07:20:26.305703+00:00 新head与反馈核查

2456 cf03b1646和前fb902a012的tree均bb8cfe03001ba0fecdb8417e54cf76ad03ab58ae，parent均fb8d1c280；diff为空，代码没有新增修正。旧head两failure一cancelled仍为历史证据，新head当前4success1skip15pending。原报告修复保留，5线程未决（4outdated，1当前Python3.9兼容性）。2447/2461当前CI19success1skip，线程未决1/0；原review剩余项不变。2464当前3success1skip16pending、2465当前7success1skip12pending，均无新远端反馈。无测试或POST。
