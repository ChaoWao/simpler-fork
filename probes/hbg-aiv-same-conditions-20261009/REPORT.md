# HBG AIV：原条件重放与 callable 上传取证

状态：本轮实验结束。未复现目标 AIV 越界，根因未定位；没有生产代码修复、提交或 PR 更新。原故障仍未关闭。

## 重放基线

原故障任务：`task_20261008_233035_11224665011`，四个 HBG 进程报告 `VEC instruction error: the ub address out of bounds`，随后超时。现场在 `../../reviews/pr2510-context-refactor/failure-archive/`。

两组完整重放都使用设备 1/2/3/4、原完整 pytest 命令、scheduler=2000 ms / op=3000000 us / stream-sync=4000 ms，全部经 task-submit。临时恢复归档中的六个不同 runtime 文件后，23/23 runtime .so hash 与故障现场一致。源码保持 3c8cad19；它与原测试工作树的差异只有已记录的格式/注释变化。没有恢复两次预约之间的设备寄存器、指令缓存或线程时序，不称完全同一现场。

## 实测

| 组 | task | pytest 耗时 | 结果 |
| --- | --- | --- | --- |
| 同产物、原 callable 缓存、并行 CPU 工作 | task_20261008_235818_2624824423 | 445.47 s（外层含预约/采集 450.28 s） | exit 0，目标签名 0 |
| 空 callable 缓存、并行模拟器和 Python UT | task_20261009_000621_34821847966 | 567.38 s（外层 571.22 s） | exit 0，目标签名 0 |
| 上传记录验证，五个 pytest 项（四个实际运行、一项 skip） | task_20261009_010950_115413711904 | 27.71 s（外层 32.85 s） | exit 0，目标签名 0 |

第一组重放 C++ -j6 编译/-j4 ctest、两套 max-parallel=4 的完整 simulator sweep、Python UT。Python UT 2990 passed/59 skipped；C++ 297 targets 与两套 simulator 都通过。为产生编译工作，移除了上次日志所列的 312 个本工作树 UT object，未动生产源代码。

**第二组不是严格的单变量实验**：相同的 object 删除操作触发了 ccache 命中，C++ 构建迅速完成，上板启动时它已经结束。两个模拟器因冷 callable 缓存分别运行 252.06/240.81 s。不能据此声称排除缓存或负载原因。后续若比较编译负载，需要记录/控制 ccache，而不仅删除 .o。

原故障归档中的 611 个 callable/incore `.bin` 在第一组使用的 warm cache 中全部存在且 hash 一致，见 `warm-cache-vs-failure.json`。这不等于证明故障时实际上传内容/地址一致。

第一组、第二组分别有 87、150 个目标设备 PID 采样可沿父进程链回溯到自己的 npu-lock；另有 16、22 个样本在 npu-smi 与 ps 两次查询之间缺少 PID 信息。记录中未确认有外来进程挤占目标设备，但不能证明无采样间隙。见 `summary.json` 和各组 `occupancy.log`。

## 上传取证：本轮补齐的能力

`callable_trace.cpp` 是本地 LD_PRELOAD 探针，仅对 `upload_chip_callable_buffer` 调用的成功同步 H2D 保存完整源字节，同时记录实际 destination、size、pid、时间和 rtFree。先通过假 runtime 的 CPU smoke，再通过真实短测试验证。未进入产品/CI。

旧探针有两个覆盖缺口：256 KiB 限制排除了实际约 0.87–0.95 MB 的 ChipCallable；测试辅助器 `tools/onboard_diagnostics.py::capture_case` 修改 ASCEND_PROCESS_LOG_PATH，三组 early-enqueue 记录进入独立 `/tmp/simpler-onboard-evidence-*`，不能只扫描外层日志根。新采集已将这些目录复制到 `trace-coverage-01/per-case/`。

本次四个实际执行进程全部有上传记录：1159338、1159347、1162514 各一个 callable；1160696 两个 callable。五份上传字节完整，解析出了 13 个 child code range，见 `trace-coverage-01/code-ranges.json`。这些是本次成功运行的记录，不能代替旧失败运行缺失的上传地址。

第三组采用当前最终构建产物，没有临时恢复故障版 runtime，且设备改为 0/1/3/4：设备 2 当时已被另一条注册任务占用。它只验证取证覆盖，不是同条件对照。插桩也会改变时序。

## 新线索，尚非归因

旧四次故障 PC 是 `0x12c0c00ee75c`（三次）和 `0x12c0c0101f5c`（一次）。新正常运行的上传记录中：

| callable | func_id | code start | bytes | end（不包含） |
| --- | --- | --- | --- | --- |
| 三个 early-enqueue 进程 | 1 | 0x12c0c00ee6e0 | 124 | 0x12c0c00ee75c |
| dep-gen 进程第一份 callable | 1 | 0x12c0c0101ee0 | 124 | 0x12c0c0101f5c |

两种旧 PC 恰好等于新正常运行的这两个代码区间末尾。early-enqueue 的 func_id=1 源是 `worker_async_fifo/kernels/aiv/kernel_add_scalar.cpp`。保留了各 child 的原始代码字节、hash、上传前 patched descriptor。

**不能把跨运行的地址相等当作旧运行的地址证明，也不能由此声称 CPU/设备越界取指或返回地址损坏。** 还需要失败运行自己的上传记录，以及该架构异常 PC 的语义，才能把故障归到具体指令。下一步应围绕这两个短代码段和其调用现场取证，避免继续无差别全扫。

## 清理与复跑

两个完整实验都已恢复原六个 runtime 文件（逐文件 hash 验证）与原 callable 缓存；冷缓存生成物保留在 `loaded-cold-cache-01/generated-kernel-cache/`。Git 工作区干净，所有设备任务已结束。

脚本：`replay.py` / `replay_cold.py`（输出目录固定、拒绝覆盖；再次使用需新输出名），`launch_trace.py`（经 task-submit 的显式 env LD_PRELOAD，避免任务环境快照剥离该变量）。探针输出必须连同辅助器的 per-case 目录一起收集。`launch.py` 外层进程 exit 0 不能作测试通过依据，必须读取 result.json.rc；本报告已按内部 rc 核对。
