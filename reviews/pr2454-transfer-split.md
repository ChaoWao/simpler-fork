# #2454 参数合同拆分记录

> 2026-09-27 后续决定：本文保留 `df9dd00d` 的实现与测试事实。该 head 的非 skipped GitHub 检查现已全部成功（包括 onboard）；但公开模型修订为一个 Buffer/Tensor、transfer 归 TaskArgs 参数项，因此建议 #2454 修改后合入。下方“下一笔”属于当时拆分，当前顺序以 [统一设计 §6.1](../argument-domain-policy.md#61-实现步骤与-pr-边界2026-09-27) 为准。


- PR: https://github.com/hw-native-sys/simpler/pull/2454
- 提交：`df9dd00d3ca0e2f2e4a24c48481208e1eb7b8be7`
- 基线：`c605b03cad7be2a80700efbdf3bd00dbc79812a9`，main；19 文件，+334/-33。
- 分支：`feat/tensor-transfer-policy`，独立 worktree `simpler/.claude/worktrees/tensor-transfer-policy`。
- 旧 #2444 已关闭并评论拆分，不删除旧分支及其迁移实现。

## 交付边界

独立 AddressSpace/TensorTransfer；沿用 Buffer/import grant 与 TaskArgs/tag/signature，不增加 Tensor/ChipTensor 权限字段。转发、mailbox、materialize 保留 transfer；已有构造默认行为不变。四个 chip binder 在分配/内容复制前做全调用 transfer 组合检查。HOST/NONE 在 chip binder 暂拒绝，HBG 旧跨侧 get/set 暂不改变。Remote v4 对无法表达的请求拒绝，不静默丢弃。

下一笔是显式 HOST 绑定、跨侧拒绝与用例迁移；再下一笔收口共同绑定和 IO 记录。第一笔不计为完整 U1 或 kernel eager 完成。

## 验证

- C++：`ctest --test-dir tests/ut/cpp/build -LE requires_hardware --output-on-failure -j 8`，252/252 目标通过。
- Python：`python -m pytest tests/ut -q`，2691 passed / 54 skipped。
- A2/A3 sim：44 resource cases 通过；HBG 14 passed / 7 skipped；TMR 31 passed / 1 skipped。
- A5 sim：40 resource cases 通过；HBG 14 passed；TMR 30 passed。
- 两架构 sim 均运行 `pytest examples tests/st --platform <arch>sim --device 0-7 --max-parallel 4 --pto-session-timeout 600`。
- 全部 pre-commit 检查通过；本地新 worktree 独立 venv，按仓库约束使用 nanobind<3。
- 所有可见设备 HBM 非零，未运行本地 onboard；不以 sim 代替硬件证据。

首次 Python 全量出现 inspect.getsource 定位错误；当时同时格式化源码改变了函数行号。隔离测试通过，固定源码后的全量重跑通过，不将该现象登记为主线缺陷。首次 C++ 两个 HBG 目标失败是测试手工把 HOST tensor 改成 DEVICE 时未改 transfer；测试修正为 DEVICE/NONE，随后全量通过。

规划文档与 HTML 均留仓库外；PR 仅含代码、测试与公开 Buffer API 参考文档。
