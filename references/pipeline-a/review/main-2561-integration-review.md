> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# 新main清理故障修复集成审查

核对时间：2026-10-14T01:48:41.270652+00:00。main从a89c1170c到5f4f9525d，仅新增#2561。

A3 `DeviceRunner::cleanup_execution` 把决定是否放弃正常退休的poison读取移至`discharge_boundary_waits_noexcept`之后。后者可能因无法证明stream wait已消费而poison runner；此后必须保留wait/fence引用并隔离slot，不能继续正常设备资源回收。新增RTS stub probe覆盖两种ownership、健康/两stream失败/预先poison及重复清理；本轮仅静态审查，无测试。

#2564只改离线工具/单测/README，完全不相交；当前CI testmerge786f2dd已含此main。#2563同一device_runner文件只在launch构造run metadata时传入artifact identity，不改cleanup，与本修复无冲突；CI仍是旧base，不能冒称已重测新main。两PR当前MERGEABLE，不为无关base移动发起重复rebase/CI。

P4动态资源实施须在fresh main上保留该修复：排干动作可以改变安全性，不能使用动作前缓存的状态决定资源退休。该要求落实原失败隔离合同，不增加实施授权、不替用户批准。
