> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# Capture memory: framework evidence

Checked 2026-09-22. Scope: primary-source evidence for address stability, growth, reuse, and lifetime. This note uses “unified memory” to mean simpler's common allocation/budget interface, not CUDA managed-memory page migration.

## PyTorch has the same address and reuse problem

CUDA graph replay uses captured virtual addresses. Freeing them risks invalid access; giving them to unrelated tensors risks corruption. PyTorch routes allocations during capture into a private pool, normally one per capture. The pool survives until both its graph and tensors allocated during capture disappear. Sharing pools is a caller promise: replay in capture order and never concurrently. This is a lifetime/scheduling contract, not merely an allocator cache setting. [PyTorch graph memory management](https://docs.pytorch.org/docs/2.14/notes/cuda.html#graph-memory-management)

PyTorch recommends eager warmup on a side stream before capture, with long-lived input/output tensors. New input data is copied into those fixed buffers before replay. Warmup is preparation; it does not prove that an arbitrary future larger workload fits the captured memory layout. [PyTorch graph usage](https://docs.pytorch.org/docs/2.14/notes/cuda.html#cuda-graphs)

## CUDA driver facilities are a separate mechanism

CUDA graph allocation nodes retain virtual addresses across graph launches while their physical backing may change. Allocation/free dependencies define when contents are valid. CUDA can reuse backing for nonoverlapping allocations and graphs; a unique pointer does not imply exclusive backing forever. Captured `cudaMallocAsync`/`cudaFreeAsync` produce memory nodes, so “allocation during capture is universally forbidden” is false. Destroying a graph does not automatically free still-live graph allocations. [NVIDIA graph memory nodes](https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/cuda-graphs.html#graph-memory-nodes)

CUDA user objects provide refcounted resource lifetime attached to graphs and executable instances. If an executable is destroyed while work remains, those references survive until execution completes. This is evidence for a resource lease spanning asynchronous use; it does not automatically retain every arbitrary external pointer passed to a graph. [NVIDIA user objects](https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/cuda-graphs.html#cuda-user-objects)

The stream-ordered allocator reuses memory after its ordered free: within a stream, across event dependencies, or after completion. Otherwise it can obtain more memory. Pool retention controls reduce allocation overhead; they do not independently establish captured address ownership. CUDA's graph-memory pool is distinct from its stream-ordered pool. [NVIDIA stream-ordered allocation](https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/stream-ordered-memory-allocation.html#memory-reuse-policies), [graph physical footprint](https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/cuda-graphs.html#physical-memory-footprint)

## torch_npu implements corresponding private-pool ownership

Source snapshot: Ascend/pytorch `master` at `dcff953b9394c9685b4af429f11a911a9d0aa147`. This is upstream evidence, **not verification of the deployed torch_npu/CANN version**.

`NPUCachingAllocator` creates/refcounts `PrivatePool`s and routes allocations through `beginAllocateToPool`. `releasePool` decrements graph ownership and marks a zero-owner pool reclaimable; it explicitly accounts for other graphs sharing the pool and output tensors still referenced by users. Pool deletion additionally waits for its backing-allocation count to reach zero. Its checkpoint discussion explains another hazard: replay does not update ordinary allocator state, so a later capture can overwrite live replay tensors unless the allocator state/liveness is restored. These are implemented ownership and reuse concerns, not an analogy inferred solely from CUDA. [NPUCachingAllocator source](https://github.com/Ascend/pytorch/blob/dcff953b9394c9685b4af429f11a911a9d0aa147/torch_npu/csrc/core/npu/NPUCachingAllocator.cpp#L1852)

The Python API exposes `graph_pool_handle`, `NPUGraph.pool()`, and a capture pool argument. `make_graphed_callables` warms up before capture, shares a pool, and captures forward/backward passes in their eventual execution order. Its wrapper copies new inputs into static storage before replay. Some docstrings still contain CUDA terminology; the NPU calls and implementation are the stronger evidence. [graphs.py](https://github.com/Ascend/pytorch/blob/dcff953b9394c9685b4af429f11a911a9d0aa147/torch_npu/npu/graphs.py#L966)

`NPUGraph::replay` calls `AclmdlRIExecuteAsync`. `reset` releases device/host pool ownership and calls `AclmdlRIDestroy`; this source alone does **not** establish all CANN guarantees for outstanding replays, external allocations, or graph destruction. Do not infer CUDA user-object semantics or CUDA graph-memory remapping support from this wrapper. [NPUGraph.cpp](https://github.com/Ascend/pytorch/blob/dcff953b9394c9685b4af429f11a911a9d0aa147/torch_npu/csrc/core/npu/NPUGraph.cpp#L356)

## Consequences for the simpler design

The following are design deductions, not vendor guarantees:

- One allocation interface and one aggregate budget can serve eager work and captures while maintaining separate ownership. A single relocatable backing buffer is not required by a unified interface.
- Appending stable blocks can grow capacity without moving existing pointers. It does not make overwriting a captured slot safe. Each allocation needs a region/offset, extent, owner, and last-use condition.
- A graph lease must cover every captured address, including internal workspaces and host copy sources, until no graph can replay it and no submitted use remains. Output views can extend lifetime further. An execution completion event proves the current replay ended; it does not prove the graph will never replay again.
- Reuse requires both address/lifetime eligibility and safe execution ordering. Unrelated captures need distinct regions by default; shared regions require an explicit serialization/liveness contract.
- Grow and prepare before capture; freeze the selected graph layout for capture/replay. Treat “no new backing allocation during capture/replay” as simpler's chosen policy, not a universal CUDA prohibition. If preparation exceeds capacity, enlarge before capture or build a new graph generation, retaining the old generation until its leases expire.
- Warmup estimates demand but cannot replace an explicit fit check for the actual capture. Output retention, concurrent graphs, and outstanding replays all count against the same budget.

Before implementation, verify the deployed CANN contract for capture-safe allocation/copies, replay completion, graph destruction, and any host-buffer retention. Upstream torch_npu demonstrates the ownership model; it does not settle those backend contracts for simpler.
