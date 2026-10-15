# backend 矩阵 + 范例 + 边界

> **发布快照：2026-10-15。** 设计、实现状态与历史调查须按正文各自基线阅读；不代表当前 main 全量验收。参见[阅读入口与已知更正](../README.md)。
>
> 来源：原 [`worker-memory-model.md`](../worker-memory-model.md) §6 + §7 + §8。

## 6. backend 矩阵（address_space × transport，能力门控）

| 共享形态 | 同机 fork | 同机跨进程 device | 跨机（remote L3） |
| -------- | --------- | ----------------- | ----------------- |
| kind3 host | MAP_SHARED / POSIX-shm ✅ | — | remote-sidecar + host-staging（future） |
| kind4 device | — | VMM shareable-handle ✅（同机） | RDMA/URMA/fabric（**arch-gated**：A3 先做 V2 Fabric PoC；A2/950 需 staging） |

backend descriptor 记的就是这一格；不支持的格子显式报错，不静默降级。

## 7. 端到端范例（L4 → L3 → L2）

```text
L4  v = alloc_shared_tensor(shapes, dtype)     # 返回 Tensor；背后的 Buffer H 不出现
    submit(L3, args=[v.slice(...)])               # wire 上走的就是这个 Tensor；H 对 L4 是 kind3

L3  收到带 H descriptor 的 Tensor              # 对 L3 是 kind1（shared view）
    按 backend=fork-shm 在自己地址空间物化 H
    要再喂给 L2 → 在 L3→L2 这条 edge 上重新 export（kind3 不透传）
    c = alloc_child_tensor(L2[k], shapes, dtype)  # 返回 Tensor；对 L3 是 kind4

L2  收到 H' / C 的 Tensor            # 都是 L2 的 kind1
    H' → shared view（读写 L3 映射区）
    C  → child-device view（引用 L2 自己的 device buffer）

# 注：底层的 create_buffer(1MB) 仍然存在，但只用于非 tensor backing
#     （control region / queue / arena）与"一块 backing 切多个不相干视图"，见 §5.2。
```

关键：**H 每跨一条 edge 换的是"角色/视图 + 物化方式"，不是身份**——canonical
allocation identity（`CanonicalBufferIdentity` = `owner_instance_id + buffer_id +
generation`；**worker path 不在其中**——它已收窄为进程内诊断 `path_id`，见 §3 权威文档）**跨边不变**，只有 export/import record 与 materialized VA 随 edge 变
（对齐 feature_roadmap §2.4：同一 backing 的 view 必须保留 canonical identity；
dependency key 用 canonical identity 而非 materialized VA，否则 alias / retain-release
/ ABA / 依赖键会分裂）。"谁在哪能看到、能不能写"由 **identity + `address_space` + `access`**（handle 侧）
加 **`AttachmentPlan` 的 capability**（域侧）共同保证——**可见性不在 handle 上**。

## 8. 边界（明确不做的事）

- **不删 L2 设备侧 TMR GM heap**（`PTO2_RING_HEAP` / `DeviceArena`）——那是每张
  卡 AICPU 上的另一子系统，与 host 侧 HeapRing 职责/生命周期不同。
- **HeapRing 保留** task-slot/task-id/drain 职责，只把"通用 buffer 分配"这一
  职责迁到 `SharedBufferArena`（`Ring::init(heap_bytes=0)` 关容量不删结构）。
- **跨机 device 共享 arch-gated**，不是到处都能做的承诺；rank 绑单卡的硬约束
  不变，host/AICPU/AIV 不是对等 rank（那是 comm-domain 的事，但跨机 kind4
  backend 必须尊重它）。

## 9. 还有一维：**分配时机**（2026-08-05 补）

§6 的矩阵按"共享形态 × transport"分格，但选 backend 时还有一条正交约束，
它决定的是**哪些格子当下够得着**：

| 分配发生在 | 可用 backend | 为什么 |
| ---------- | ------------ | ------ |
| `Worker.init()`（fork）**之前** | `FORK_SHM`（MAP_SHARED）/ `FORK_COW`（普通堆） | fork 继承 ⇒ 子进程同 VA，物化退化为**恒等** |
| `Worker.init()` **之后** | **只有 `POSIX_SHM`** | 名字可事后传，映射可事后建；`FORK_*` 的同 VA 前提已不成立 |
| device 侧 | `VMM_WINDOW` / `DEVICE_MALLOC` | 与 host fork 时序无关，但 ref 只到 owner 那张卡 |

⇒ **`create_buffer` / `alloc_shared_tensor` 落在 `POSIX_SHM` 上不是随手选的**：用户的数据几乎总是
在 `init()` 之后才产生（scene test 的 `generate_args` 就是典型），而那时 `FORK_*` 已经用不了了。
反过来，`FORK_SHM` 的真实场景是预加载权重那类 **fork 之前就 `share_memory_()` 好**的大块内存——
顺序颠倒就退化成一次全量拷贝。

⚠️ `FORK_COW` 只能 `READ`：普通堆内存 fork 后是写时复制，**子进程写完父进程看不见且静默**。
这条已在 #1599 的 decode 处执行。用户面的完整说明见
[user-surface §5.9](user-surface.md)。
