# The host_build_graph prepared call

`host_build_graph` builds its whole task graph on the host and ships it. This
document is the contract for the object that result lives in — what it owns, what
publishing it to a device does, and what a caller may and may not change between
two executions of the same one.

## Why the split exists

A bind used to fuse construction with publication. One function ran the
orchestration entry, packed Definitions, committed the arena and heap, rebased
every heap address onto the region it had just committed, and H2D'd the image —
all against borrowed per-slot scratch: the runner's shared-memory mirror, its
Definition staging, and its retained temporary buffer. Nothing survived the bind
that produced it, so every `Worker.run` of the same graph paid a full host
orchestration and a full Definition rebuild.

Preparation now ends at a **canonical** result, and publication is a separate pass
that runs once per execution:

```text
orchestrate ──► PreparedCall  (owned, immutable, bank-independent)
                    │
                    ├──► publish #1 ──► bank A image + Definition block (+ a5 scheduler state) ──► launch
                    └──► publish #2 ──► bank B image + Definition block (+ a5 scheduler state) ──► launch
```

Ordinary `Worker.run` does both and drops the result. A caller that names a
prepared-call register keeps it, and a later matching submission publishes it
again — the same graph, fresh working state, no second orchestration.

## Canonical means four things

Each is what makes the result independent of the bank, slot and buffers that
happened to be in play when it was sealed.

| In the canonical image | Bound per execution by |
| ---------------------- | ---------------------- |
| Heap addresses still in the `HEAP_VIRTUAL_BASE` window | `sm_layout::rebase_image`, onto the heap the execution's bank committed |
| Every outer Graph task's `graph_context` null | `definition_bindings`, once the Definition block has a device address |
| Caller-argument tensor addresses listed in `arg_slices` by offset from the temporary buffer's base | the base this run's `RetainedTempBump` sliced from |
| `args` and `scalars`, the contract the sealing submission presented | nothing — they are compared against, not rewritten |

The one class publication must **not** touch is a caller-owned device address. It
is neither in the heap window nor in the temporary buffer, so no relocation
reaches it, and a submission that changed one is refused.

Publication only ever reads a `PreparedCall`. A refused or part-failed publication
therefore cannot damage it, and a retry re-reads the unchanged canonical result.
`test_hbg_prepared_call.cpp` pins that byte-for-byte.

## What the result owns

`simpler::hbg::PreparedCall` (`src/common/host_build_graph/prepared_call.h`):

- **Identity** — the orchestration entry address the graph was built from, a
  digest of the struct shapes and constants it was built against, the task
  capacity and the submitted task count.
- **The canonical image** and its `sm_layout::BindUsage`.
- **The copied zone** — the runtime-arena prefix carrying the `RuntimeContext`,
  its stashed layout and the derived `ReadyQueueCapacities`. Already
  position-independent, so it is retained verbatim.
- **Device requirements** — heap bytes, arena bytes, shared-memory size, layout.
- **The Definition block**, packed once, plus one `{task_index, object_offset}`
  binding per outer Graph task. Sharing is preserved: one object serves every
  task with its key.
- **The argument contract and the slice relocations**, plus the scalar values
  orchestration may have built the graph from.
- **An arch payload** — empty on a2a3; on a5 the AICore scheduler plan.
- **A reuse bar**, when one applies (below).

`retained_host_bytes()` is what it costs to keep; `restored_device_bytes()` is
what one publication writes. They answer different questions and are reported
separately — the mirror's frozen capacity is neither.

## a5: the AICore scheduler

The mode verdict (legacy-graph, legacy-unsupported-shape, or resident) and the
`SchedulerTaskMetadata` are a function of the graph, so they are classified once,
at preparation, from the mirror. Everything an execution owns is stood up per
publication: the state allocation, this run's callable addresses read from its own
`func_id` table, the device shared-memory address the worker contexts carry, and
the layout re-planned at the swimlane level this run's configuration asks for. The
previous execution's state is retired first, so no task control, inbox or error
field is inherited.

## The reuse contract

Reuse is requested, never inferred. There is no shape or callable cache: an
ordinary call always orchestrates, so it always observes current control data.

**A register is a hint, not a promise.** The retained result is published only
when it is reusable and provably describes the submission; otherwise the bind
orchestrates fresh and reseals. Each fallback logs why, and
`host_orchestration_entries` advances — so a fallback is visible rather than
showing up later as a wrong number.

What must hold for a republication:

- the same callable registration (identity is the resolved orchestration entry);
- the same argument count, kinds, directions, byte sizes, **geometry** (shape,
  strides, dtype, element offset, address space) and temporary-buffer slice
  offsets;
- the same **scalar values** — orchestration can embed one in the graph or branch
  on it, so a scalar is a preparation input, not per-submission data;
- the same caller-owned device addresses;
- the same resolved task capacity;
- host dep_gen capture off for this run. A captured dependency graph describes
  what orchestration recorded, and a republication records nothing, so a
  dep_gen-enabled run orchestrates rather than emitting the graph of whatever
  bind last ran on the thread.

Geometry is checked separately from byte size because two arguments of equal byte
count can still be different views — a 32×32 and a 16×64 float32 tensor cover the
same bytes and produce different graphs.

What may change freely:

- ordinary tensor **contents**, in either direction. Inputs are copied in per
  submission and `OUT`/`INOUT` tensors are copied back per submission, exactly as
  on a fresh call;
- the arena bank, the pipeline slot, and the base of the retained temporary
  buffer. A result survives reuse of the slot that built it: its identity is a
  register index, not a slot or an image address.

What is reused by definition: every decision host orchestration made from the
values it read. Control data that shapes the graph or is embedded in it is a
**preparation input** — changing it requires a fresh preparation, which is what an
ordinary call always does.

### Preparation-produced tensor writes

Host orchestration may write tensor bytes through `HostTensorAccessor`
(`set_tensor_data`). Two cases, and they differ in whether the effect can be
replayed:

- **A host-memory argument.** The write lands in the caller's own host buffer,
  which every submission's copy-in reads. The effect therefore survives a
  republication, in the right order relative to that submission's input transfer,
  without the host callback running again.
- **A device-memory (child) argument.** The write lands directly in memory the
  caller owns, and a bind stages nothing for such a tensor — so no per-submission
  transfer reproduces it. A result that made one of these writes is sealed
  **non-reusable** (`PreparedCallReuseBar::HostOrchestrationWroteDeviceTensor`),
  and a register holding it falls back to a fresh preparation with that reason
  named. This is a documented restriction, not a silent loss: the ordinary fresh
  path is unchanged.

## Lifetime

A retained image names its kernels by `func_id`, and the address behind a
`func_id` is replayed per run from the callable that run binds. So a result must
not outlive the code it dispatches:

- `simpler_unregister_callable` and `finalize_device` drop **every** register.
  Registers are a narrow internal seam, so dropping all of them is cheaper to
  reason about than tracking which register belongs to which callable.
- `simpler_release_prepared_call(reg)` drops one on demand.
- A borrow taken by a publication pins the result, so a concurrent release only
  drops the register's own reference — it cannot free an image a publication is
  still reading.

## The seam

`NativeRunDescriptor::prepared_call_register` (0 = none, 1..8) is the whole ABI.
It reaches the runtime as `HostApi::prepared_call_register()`, and
`ChipWorker::set_prepared_call_register` is what a test sets before submitting.
Three C entries round it out: `simpler_release_prepared_call`,
`simpler_prepared_call_metrics` and `simpler_prepared_call_metrics_reset`, each
answering `PTO_RUNTIME_ERR_UNSUPPORTED` from a runtime that retains no such
result rather than reporting silent zeros.

No public Python API is added. `tests/st/{a2a3,a5}/host_build_graph/prepared_call_reuse/`
drives the seam through the production consumer and asserts on
`host_orchestration_entries`, which is the only thing that separates reuse from a
silent re-preparation.

## Bind phases

The bind's `[STRACE]`-adjacent phase breakdown keeps its shape, with one addition.
`graph_pack` is new and belongs to preparation; `graph_upload` now measures the
per-execution block acquire and H2D alone.

| Phase | Half |
| ----- | ---- |
| `args`, `arena_build`, `runtime_init`, `host_orch`, `graph_pack`, `host_view_close` | preparation |
| `static_arena`, `shared_mem`, `gm_heap`, `graph_upload`, `arena_h2d` | publication |

## What this is not

A `PreparedCall` is not an ACLGraph, and HBG's internal Graph Definitions and
GraphExecution objects are not CANN capture graphs. Host-side restoration for
serial reuse is what this contract covers; in-graph restoration for capture and
replay is a separate question, and nothing here establishes capture
compatibility. Publication is synchronous and admission is unchanged — including
the existing prepare-while-active overlap, which a prepared successor still gets.

Coordinated with the parallel `tensormap_and_ringbuffer` work (issue #2270) on the
vocabulary only — an immutable resolved result, a per-execution submission, and
the mutable working set it binds into. The payloads stay runtime-specific: HBG
keeps its images and relocations rather than adopting a universal transport
packet.
