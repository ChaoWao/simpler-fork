# Tensor 统一设计：源码证据与验证边界

更新：2026-09-24。设计入口：[argument-domain-policy.md](argument-domain-policy.md)。本页区分观察和方案，不另定义一套原则。

## 1. 本次核查基线

- 读取最新 `upstream/main`：`c605b03cad7be2a80700efbdf3bd00dbc79812a9`。
- 工作分支 #2444：`1761d1304eec8b1ea7836d8d2f6812acbc5b2498`，基线 `bef53236679a0ff56d38abbf38d5a738f40e530c`。
- fetch 后没有 rebase、修改产品代码、commit 或 push。main 新增变更集中于 resident scheduler storage；本文引用的 Buffer、Remote、能力判断路径相对分支基线未改变。
- 当前分支已经改过 HOST/H2D 行为。主线旧 HBG 路径用 `git show` 单独核查，不能拿分支的禁跨侧访问证明 main 已禁用。
- 引用主键是固定提交 + 文件 + 符号/独特文本。行号不作为移动分支的事实依据。

## 2. 逐层事实表

以下链接全部固定 main commit；定位键在第二列。

| 文件 | 定位键 | 观察与设计影响 |
| --- | --- | --- |
| [buffer.h](https://github.com/hw-native-sys/simpler/blob/c605b03cad7be2a80700efbdf3bd00dbc79812a9/src/common/task_interface/buffer.h) | CanonicalIdentity / BufferDescriptor / Tensor | 身份为 owner nonce + buffer id + generation；descriptor 不包含消费者本地地址；Tensor 承载几何 |
| [buffer.h](https://github.com/hw-native-sys/simpler/blob/c605b03cad7be2a80700efbdf3bd00dbc79812a9/src/common/task_interface/buffer.h) | AccessMode / validate_buffer_descriptor / tensor_footprint | READ=0、WRITE=1、READWRITE=2，非位掩码；backend/位置配对和范围已有校验，不能另造不一致权限规则 |
| [tensor.h](https://github.com/hw-native-sys/simpler/blob/c605b03cad7be2a80700efbdf3bd00dbc79812a9/src/common/task_interface/tensor.h) | ChipTensor | L2 已解析 POD 与 L3 Tensor 分层存在，不需把所有层合成一个 C++ struct |
| [task_interface.cpp](https://github.com/hw-native-sys/simpler/blob/c605b03cad7be2a80700efbdf3bd00dbc79812a9/python/bindings/task_interface.cpp) | materialize_one | 从 identity 找 local_base，折入 byte_offset，保留 shape/stride；新增 transfer/grant 须经过此 seam |
| [buffer.py](https://github.com/hw-native-sys/simpler/blob/c605b03cad7be2a80700efbdf3bd00dbc79812a9/python/simpler/buffer.py) | ImportRegistry.materialize | map-once 缓存；同 identity 不同 descriptor 拒绝；copy-only adapter 不产生本地地址 |
| [buffer.py](https://github.com/hw-native-sys/simpler/blob/c605b03cad7be2a80700efbdf3bd00dbc79812a9/python/simpler/buffer.py) | select_adapter / ImportContext | HOST SHM 为 host prepare 映射，即使调用目标为 chip；DEVICE 需 owner nonce，精确 chip 校验在父端 |
| [buffer.py](https://github.com/hw-native-sys/simpler/blob/c605b03cad7be2a80700efbdf3bd00dbc79812a9/python/simpler/buffer.py) | capabilities_for_adapter / _CAPABILITIES_BY_ADAPTER | direct 与 delegated copy 区分；能力再按 AccessMode 收窄；未知值 fail closed |
| [comm_endpoints.py](https://github.com/hw-native-sys/simpler/blob/c605b03cad7be2a80700efbdf3bd00dbc79812a9/python/simpler/comm_endpoints.py) | EndpointIdentity / EndpointRecord | 已有 session_instance_id、registry_epoch、endpoint_id、node_scope_id；不必再用字符串猜 endpoint 身份 |
| [comm_endpoints.py](https://github.com/hw-native-sys/simpler/blob/c605b03cad7be2a80700efbdf3bd00dbc79812a9/python/simpler/comm_endpoints.py) | BufferAccessQuery / DefaultRegionAccessService | 候选不等于已实现。HOST_SVM_MAP、DEVICE_VMM_PEER_IMPORT、REMOTE_COPY、DEVICE_FABRIC_V2_PEER_IMPORT 在默认 service 被拒绝 |
| [worker.py](https://github.com/hw-native-sys/simpler/blob/c605b03cad7be2a80700efbdf3bd00dbc79812a9/python/simpler/worker.py) | _child_prov_check_dispatch_locked | 私有注册 descriptor 与 target chip 精确验证；check-through-submit 保持事务性 |
| [worker.py](https://github.com/hw-native-sys/simpler/blob/c605b03cad7be2a80700efbdf3bd00dbc79812a9/python/simpler/worker.py) | release_buffer / free / _require_freeable | host release 拒绝在飞引用；device free 先撤销 provenance；CommDomain buffer 不允许通过普通 allocator free |
| [worker.py](https://github.com/hw-native-sys/simpler/blob/c605b03cad7be2a80700efbdf3bd00dbc79812a9/python/simpler/worker.py) | _device_control_admission | owner 外部 device control 对在飞 run 有 admission 限制；不是已有通用延迟回收体系 |
| [host_tensor_access.cpp](https://github.com/hw-native-sys/simpler/blob/c605b03cad7be2a80700efbdf3bd00dbc79812a9/src/common/host_build_graph/host/host_tensor_access.cpp) | AccessMeans / resolve_means / read / write | 主线 HBG 仍可为 child memory 查 host mapping，失败走 copy；这与用户新约束冲突，#2444 的有效迁移不能丢 |
| [HBG runtime_maker.cpp](https://github.com/hw-native-sys/simpler/blob/c605b03cad7be2a80700efbdf3bd00dbc79812a9/src/a2a3/runtime/host_build_graph/host/runtime_maker.cpp) | bind_callable_to_runtime_impl 的 tensor loop；needs_copy_in / needs_copy_back | DEVICE passthrough；HOST 分配 device slice、按方向复制/回拷，并登记 host accessor；nbytes 分配与任意 stride 不相容 |
| [remote_l3_protocol.py](https://github.com/hw-native-sys/simpler/blob/c605b03cad7be2a80700efbdf3bd00dbc79812a9/python/simpler/remote_l3_protocol.py) | RemoteAddressSpace / RemoteTaskArgsWire / FrameHeader | Remote 标签混合载荷形式和引用方式；TaskArgs 有 indexed sidecar；frame 有 session_id，不能据此认定 backing identity 已含 owner incarnation |
| [remote_l3_session.py](https://github.com/hw-native-sys/simpler/blob/c605b03cad7be2a80700efbdf3bd00dbc79812a9/python/simpler/remote_l3_session.py) | ALLOC_REMOTE_BUFFER / _RemoteBufferEntry.wire_buffer | host_tcp 的 REMOTE_DEVICE entry 持有 create_buffer / create_host_shared_buffer 生成的 POSIX_SHM；协议名不是物理位置 |
| [remote_l3_session.py](https://github.com/hw-native-sys/simpler/blob/c605b03cad7be2a80700efbdf3bd00dbc79812a9/python/simpler/remote_l3_session.py) | EXPORT_BUFFER / IMPORT_BUFFER | transport_descriptor 是 shm_name，import 调 SharedMemory(name=...)；它不是跨机器 mapping 的实现 |
| [remote_l3_session.py](https://github.com/hw-native-sys/simpler/blob/c605b03cad7be2a80700efbdf3bd00dbc79812a9/python/simpler/remote_l3_session.py) | _materialize_task_args / _materialize_inline_payload / _resolve_session_backing | inline 变为接收端新 Buffer；standing handle 走 session registry；重建参数时必须保留新的 transfer/tag/grant 合同 |
| [buffer.py](https://github.com/hw-native-sys/simpler/blob/c605b03cad7be2a80700efbdf3bd00dbc79812a9/python/simpler/buffer.py) | remote_backing_identity | owner nonce 目前由 worker_id 填充，远端真实 incarnation 未随此 identity 传播；扩展跨 session 复用必须补齐 |
| [remote_l3_session.py](https://github.com/hw-native-sys/simpler/blob/c605b03cad7be2a80700efbdf3bd00dbc79812a9/python/simpler/remote_l3_session.py) | _import_wire_buffer | imported wire Buffer 重新填 descriptor/access；统一设计需把原 backing 信息和导入范围/权限分开，不能丢 grant |
| [task_interface.py](https://github.com/hw-native-sys/simpler/blob/c605b03cad7be2a80700efbdf3bd00dbc79812a9/python/simpler/task_interface.py) | RemoteBufferHandle / RemoteTensorRef.host_inline / _sidecar_from_ref | owner/import/inline handle 生命周期不同；用户一个 add_tensor 已能内部生成 indexed transport 描述，无需新增用户双列表 |
| [worker.py](https://github.com/hw-native-sys/simpler/blob/c605b03cad7be2a80700efbdf3bd00dbc79812a9/python/simpler/worker.py) | remote_free / remote_release_import / _flush_pending_remote_frees | Remote 已有逻辑释放、slot/import refs 和 pending journal；RPC 结果不确定时保留所有权，而不是假设释放成功 |
| [orchestrator.cpp](https://github.com/hw-native-sys/simpler/blob/c605b03cad7be2a80700efbdf3bd00dbc79812a9/src/common/hierarchical/orchestrator.cpp) | infer_deps / TensorKey::remote_buffer | local key 以 backing 为单位并带 footprint；remote key 仍带 desc.offset，remote 分支用默认 whole-backing footprint |
| [tensormap.h](https://github.com/hw-native-sys/simpler/blob/c605b03cad7be2a80700efbdf3bd00dbc79812a9/src/common/hierarchical/tensormap.h) | RunTensorKey / lookup_overlapping | 已有推导以 RunId 限域，不等于全局跨 run hazard tracker；统一身份不能顺手扩大并发承诺 |

另外核查了 `Worker.alloc_shared_tensor → Orchestrator::alloc`：它使用 HeapRing/FORK_SHM，按 scope/consumer 自动回收。`reserve_outputs_and_slot` 当前只分配零 heap 字节；旧 Remote 文档中“local null OUTPUT 自动分配”的描述不能当作现状。`infer_deps` 的 OUTPUT/OUTPUT_EXISTING 只 insert，完整的 WAW/WAR 自动推导不在当前合同中。

`validate_tensor` 当前拒绝零 shape/stride，而 L2 构造器可以产生空 Tensor。主文把统一空视图规则列为迁移内容，并未把这两层现状混同。

## 3. 需要纠正的理解

1. **平台有 mapping hook ≠ 共享 capability service 已开放该 profile。** 两条路径当前并不完全一致；新策略必须覆盖 HBG 旧 accessor 的旁路。
2. **Remote API 存在 ≠ 当前所有 backend 支持跨机器共享存储。** host_tcp 的 inline/copy 与其 SHM import 是不同机制。README/设计里的 HCOMM contract 不能当作对应 producer/materializer 已实现的证据。
3. **同 backing 的多个 import ≠ 多个 backing。** canonical 信息、授权范围与 attachment 本地地址必须分开；具体非零 export offset 行为需用端到端用例钉住，本文不把静态审计当成已完成缺陷复现。
4. **当前不同释放行为不能靠重命名统一。** Remote 的 deferred free 是可复用先例；本地必须新增/接通使用保留后才能改变 release_buffer 的拒绝行为。
5. **现有 Program HOST 输出依靠 copyback。** 参数模型迁移不能把 H2D 的名字解释成“去掉输出回拷”；也不能利用回拷保留旧 host get DEVICE 的隐式行为。
6. **几何校验正确不保证搬运正确。** 同一合法 strided Tensor 可以给原生消费者使用，但现有连续 H2D 算法不能因此宣称支持它。

这些是设计迁移必须覆盖的断点，不是对未经完整调用链验证的安全漏洞作定性。

## 4. 已有调查如何约束方案

- [映射逐 run 重建调查](https://github.com/hw-native-sys/simpler/blob/c605b03cad7be2a80700efbdf3bd00dbc79812a9/docs/investigations/2026-09-hbg-per-run-host-view-rebuild.md)：逐 run 重建的优化方案被测量否定。因此未来显式 attachment 生命周期按 backing/grant 管理，不能每次 bind 重新 map/unmap。当前禁隐式跨侧访问是用户合同变化，不是推翻原性能测量。
- [HBG host watermark 等待调查](https://github.com/hw-native-sys/simpler/blob/c605b03cad7be2a80700efbdf3bd00dbc79812a9/docs/investigations/2026-08-hbg-consumer-wait-cannot-observe-device.md)：host mirror 无法以那个等待观察 device 进度。设计不恢复 HBG 构图中途等待 device 的路径。
- 仓库 `docs/remote-l3-worker-design/buffers-and-transports.md` 同时包含现状与目标段落，部分正文仍称旧 ChipTensor / ptr key；本次以代码为准，不能整份照搬。

## 5. 本次验证

先运行三组现有 Python UT，构建身份保护在收集阶段拒绝：本地扩展标记为 `0a024158aff8`，源码为 `1761d1304eec`。没有绕过身份保护。

按仓库 testing skill，用 worktree 自己的 `.venv` 执行 `python -m pip install --no-build-isolation -e .` 重建成功，然后执行：

```sh
.venv/bin/python -m pytest \
  tests/ut/py/test_buffer.py \
  tests/ut/py/test_worker/test_comm_endpoints.py \
  tests/ut/py/test_remote_l3_protocol.py \
  -q --disable-warnings
```

结果：**139 passed in 0.33s**，基线为分支 `1761d130`。日志：`reviews/tensor-model-tests-1761d130.log`；构建日志：`reviews/tensor-model-rebuild-1761d130.log`。

范围：已有 descriptor、adapter/endpoint、remote protocol 单测。不是新设计的测试，也不是当前 main 的全套测试；没有宣称真实跨机器 peer、跨侧 map、capture 或所有生命周期竞争已通过。
