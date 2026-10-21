> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# #2463 与 #2462：同一测试接口修复

## 0. 结论

#2463 head `1f6b4446d1579d978f951df29e90feb067c6cc67` 与 #2462 head `9ebf5c02a79ed912aaff54af5d0dcf310c4dc13b` 修复同一基线编译问题。两者merge-base均为新鲜main `410502f7bf18aef0b033c987c8fb995f0bc8c5ca`。代码静态上等效，不能作为两项独立交付重复合入。

#2452更新WorkspaceManager接口时，#2455新增测试的fake backend尚用旧签名；两PR都把release迁到三参数ReleaseOutcome，并把configure与set_limit分开，保留同一个1MiB预算和所有原断言。没有产品代码变更。

## 4. 差异、验证与残余

#2462额外说明fake不真正释放设备，并显式将platform_rc置零；#2463省略该说明及赋值。真实调用方 `workspace_manager.h:970` 本来就在调用前将platform_rc初始化为零，Freed分支直接返回成功，所以这不是功能差异。

作者在#2461新评论中声称#2463本地263/263通过，属于作者验证，manager未执行或重跑测试。#2463 Ubuntu CI失败、macOS取消，不能以本地声明替代当前CI。#2462 Ubuntu、macOS、A3 UT均成功，A5 UT仍排队，也尚未Ready。

#2461作者“nothing else outstanding”不覆盖manager独立记录的正文与代码不符、确定性回归缺口。该PR只解决tombstone发布时序，不因提出另一个基线修复而自动关闭评审待办。

## 5. 合入建议

保留先交付且CPU CI已通过的#2462作为基线修复候选；等待其相关CI全部结束后交用户确认。#2463作重复候选跟踪，建议不再重复合入。manager本轮不merge、不关闭外部PR、不代发评论、不接管作者。

基线修复合入后，各受影响PR需核实际基线并由原作者整合；不能用另一个PR的绿灯替代其当前提交CI。根因未明的板端失败仍单独保留，不因编译修复消失。

## Appendix A：改动规模

| 分类 | 文件 | 新增 | 删除 |
| --- | ---: | ---: | ---: |
| Test | 1 | 8 | 6 |
| Core / Build / Docs / Other | 0 | 0 | 0 |

## Appendix B：pto-isa

pin为`c0d7148e95ef73bd12a73165fdce4b723a3b7e72`。无PTO头引用或pin变化，无需因此重建。

## CI日志补证（本轮只读获取）

Ubuntu job108791781396在编译完成后执行263个CTest target，262成功、一个失败：`test_a5_tmr_chip_swimlane_retained_runs` 内 `LateBufferForASealedEpochIsCountedNotAppended` 在762/763行得到 `late_after_seal=0, unknown_epoch=1`，预期分别为1、0。macOS被取消。

该测试和collector在main、#2462与#2463的blob完全相同：测试`22dfb227252192470488a3de8291abac62f9b737`、collector`494d86acccb3cf08b5ae111e60fb63c0477ad321`。测试在观察到文件后注入late buffer；基线在文件发布后的slot release才登记tombstone，存在文件可见而身份尚未登记的窗口。这一日志与已静态确认的基线时序问题一致，#2461将登记前移至Closing发布前正处理该问题。不是#2463 fake allocator改动造成的新逻辑；也不能据此称偶发、忽略红灯或假定#2461已完成所有验收。

日志：`../claude-managed/pr2463-job108791781396-download.log`。没有执行复现、测试或重试；#2462当前CPU绿灯也不证明基线不存在这个窗口。
