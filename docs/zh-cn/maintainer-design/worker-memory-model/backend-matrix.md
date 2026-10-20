# backend 矩阵 + 范例 + 边界

> **发布快照：2026-10-19。** 设计、实现状态与历史调查须按正文各自基线阅读；不代表当前 main 全量验收。参见[阅读入口与已知更正](../README.md)。
>
> 来源：原 [`worker-memory-model.md`](../worker-memory-model.md) §6 + §7 + §8。

## 6. backend 矩阵（address_space × transport，能力门控）

 | 共享形态 | 同机 fork | 同机跨进程 device | 跨机（remote L3） |
 | -------- | --------- | ----------------- | ----------------- |
 | kind3 host | MAP_SHARED / POSIX-shm ✅ | — | remote-sidecar + host-staging（future） |
 | kind4 device | — | VMM shareable-handle ✅（同机） | RDMA/URMA/fabric（**capability-gated**；A3 Fabric 共享窗口已有实现，具体 backend 与组合分别按 DS-5/DS-3 验收） |

backend descriptor 记的就是这一格；不支持的格子显式报错，不静默降级。

## 7. 端到端范例（L4 → L3 → L2）

```text
L4  H = alloc_shared_tensor(shapes, dtype)       # 当前返回 Buffer，backing 来自继承的 HeapRing
    v = Tensor(H, shapes=shapes, dtype=dtype)    # 显式几何视图，不取得额外 allocation ownership
    submit(L3, args=[v])                        # 示意；wire 传 Tensor descriptor

L3  收到 H 的 Tensor descriptor
    按 backend 在本进程物化；目标接入独立验证来源、范围、授权和保留
    要再喂给 L2 → 在 L3→L2 这条 edge 建立相应 attachment（kind3 不透传）

L2  消费经验证的 Tensor view
    HOST direct / COPY / device view 由请求与 capability 决定
```

以上是契约流程示意，不承诺所有 topology 均已支持；当前 API 与 consumer 边界见
[user-surface §5.2](user-surface.md#52-api-契约与已交付边界)、[RA-13/DS-3](../work-items.md)。
`create_buffer` 同样可为 Tensor 提供 backing，不限于 control region / queue / arena。

关键：**H 每跨一条 edge 换的是"角色/视图 + 物化方式"，不是身份**——canonical
allocation identity（`CanonicalBufferIdentity` = `owner_instance_id + buffer_id +
generation`；**worker path 不在其中**——它已收窄为进程内诊断 `path_id`，见 §3 权威文档）**跨边不变**，只有 export/import record 与 materialized VA 随 edge 变
（对齐 feature_roadmap §2.4：同一 backing 的 view 必须保留 canonical identity；
dependency key 用 canonical identity 而非 materialized VA，否则 alias / retain-release
/ ABA / 依赖键会分裂）。"谁在哪能看到、能不能写"由 **identity + `address_space` + `access`**（handle 侧）
加 **`AttachmentPlan` 的 capability**（域侧）共同保证——**可见性不在 handle 上**。

## 8. 边界（明确不做的事）

- **不删 L2 设备侧 TMR GM heap**（device arena）——那是每张
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
 | `Worker.init()` **之后新建独立 host backing** | `POSIX_SHM` 等可显式 export/import 的 backend | 新映射没有自动 fork 继承保证；名字或 descriptor 可事后传递 |
 | fork 后从**已继承的共享 arena** 分配子区间 | `FORK_SHM` | 继承的是整个映射；子区间分配晚于 fork 不会取消继承关系 |
 | device 侧 | `VMM_WINDOW` / `DEVICE_MALLOC` | 与 host fork 时序无关，但 ref 只到 owner 那张卡 |

`upstream/main@937c57efc` 的 `Worker.alloc_shared_tensor` 从 `_orch._o.alloc` 取得
已继承 HeapRing 内的子区间，再以 `Buffer.wrap_fork_inherited` 返回 `FORK_SHM` Buffer。
因此不能把“新建独立映射晚于 fork”推广成“fork 后任何分配都只能 POSIX_SHM”。
`create_buffer` 的 backing 选择与 `alloc_shared_tensor` 的 HeapRing scope 也不能混为一谈。

⚠️ `FORK_COW` 只能 `READ`：普通堆内存 fork 后是写时复制，**子进程写完父进程看不见且静默**。
这条已在 #1599 的 decode 处执行。用户面的完整说明见
[user-surface §5.9](user-surface.md)。
