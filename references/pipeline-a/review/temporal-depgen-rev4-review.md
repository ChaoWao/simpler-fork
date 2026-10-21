> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# DepGen rev4 manager review

2026-09-29，基线35e9331b5，全文492行及结果已读。接受有界TMR后台输出设计，尚需用户批准实施。此前独立源码核查的多producer与多overflow-chain问题由按实际增长计费解决；writer取得时才收费消除两个sealed输出与单工作区矛盾；默认错误语义已具体列清。

审批合同 temporal-depgen-background-decision.md 为控制版本，并澄清：
- 数字固定为沿用256 MiB、两份未发布（含当前采集）、1 MiB序列化暂存；分段指针目录/rehash新旧表也必须计费。
- rev4称处理第二份时第一份block仍在不是必要前提：前一份已完全释放；预算不足可来自当前blocks、另一新run或自身工作集。接受不等待、失败settle的规则。
- 两份table只对base/有效record sizing，overflow记录依协议解析，不能把payload误作普通task字段。溢出和范围检查两模式都适用；无预算不等于允许无效布局。
- 不将应用层预算宣称进程RSS上限，列明系统/allocator开销；饱和在UINT32_MAX即未知。
- 默认无新sticky或run rc改变，但缺失/未知拒绝输出；保留旧文件不等于本run产生了新图。

实现时必须检验完整生产接线、资源结清、真实多边图语义及重复run-local ID，不能仅用helper测试宣称完成。无需再派设计润色；原作者idle等待具体批准。
