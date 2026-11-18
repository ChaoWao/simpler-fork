# Percounter cluster scheduling

Percounter schedules AICore work with one temporary scheduling owner per
1AIC+2AIV cluster. C, V0 and V1 all run worker loops and may become owner;
there is no dedicated scheduling core. The Host supplies static resource order
and dependency metadata. AICPU materializes Graph bodies and supervises the
run; AICore owners allocate blocks, check dependencies and dispatch kernels.

## Ownership and memory

| State | Location and visibility | Writer |
| ----- | ----------------------- | ------ |
| `owner = NONE/C/V0/V1` | Cluster-shared GM, separate 128-byte sector | Successful CAS winner; current owner releases with exchange |
| Resource heads, `running[3]`, `next[3]`, execution records | Cluster-shared SSBUF | Current owner |
| `command[3]` | Cluster-shared SSBUF | Current owner |
| `FIN[3]` | Cluster-shared SSBUF | Corresponding worker |
| Order projections, task geometry, argument metadata | Global GM | Host; Graph metadata published by AICPU |
| Claim tickets, defer indexes and groups | Global GM | Owners through the corresponding atomic/publication protocol |
| Completion components | Global GM | Single-resource workers for ordinary returns, owners for MIX, defer settlement and dependency-only tasks |
| Payloads, Context, registration scratch | Per-worker GM | Worker during execution; owner adopts returned registrations |
| EXIT and lifecycle handshake | SPR | Existing AICPU/AICore lifecycle protocol |

SSBUF has no atomics. A successful GM owner CAS followed by acquisition grants
exclusive scheduling authority; reading `owner == self` does not. Initialization
first authorizes V0 while the cluster is quiescent. The owner sets a shared
SSBUF hint so idle workers skip GM probes while it holds authority. It clears
the hint before the release fence and GM exchange so a successor can safely
set the hint again after taking ownership. Scheduling writes have one
owner at a time; each worker remains the sole writer of its FIN. Commands and
FINs carry nonzero execution sequences to reject duplicate consumption. These
sequences are execution identities, not an owner epoch; exhaustion fails the run.

The cluster region occupies exactly the last 1 KiB of the 3 KiB SSBUF. The first
2 KiB belong to incore. The layout in `cluster_scheduler_types.h` is:

| SSBUF allocation | Bytes |
| ---------------- | ----- |
| Header and cursors | 64 |
| Three resource states | 48 |
| Three commands | 48 |
| Three FINs | 48 |
| Six execution records, 72 bytes each | 432 |
| Six cached fanin views | 96 |
| Reserved bytes before defer cursors | 224 |
| Six observed defer heads | 48 |
| Reserved tail | 16 |
| **Total** | **1,024** |

Each physical resource has one running and one next reference. Every live
execution record is pinned by a resource reference until its accounting commits.
Large payloads and Context remain in GM. Reserved SSBUF bytes carry no active
protocol state and do not change the public kernel ABI. Pending defer groups live in GM and do not
retain the producer's execution record or payload.

When an owner publishes a command that includes itself, it ends the service
round after all participating commands are visible. Its next step handles its
command before claiming other work. At kernel entry it releases `owner = NONE`
in GM after committing scheduling state. A nonparticipating owner can continue scheduling
while peers execute. With all three cores executing, no owner polls in the
background. The first returning core publishes FIN, then may win the owner CAS;
another idle core may also win. The new owner acquires current shared state.
Waiting for dependencies or resources keeps ownership, with bounded service of
other work. There is no sleep, yield or repeated idle handoff.

A5 AICores have no hardware cache coherency. GM atomics arbitrate shared words;
cache observation/writeback and barriers separately order associated metadata
and outputs. SSBUF command/FIN publication and owner handoff also use explicit
ordering. An atomic alone does not publish arbitrary cached GM data.

## Static order, allocation and execution

The Host derives a `global_order_id` from a virtual resource schedule using
resource masks, dependency edges and block counts. Unit-cost blocks compete by
b-level; a task's first virtual start sets its rank. This is an allocation order,
not a prediction of kernel time or a physical placement. Graph bodies occupy
contiguous ranks at their shell's position. All blocks of a task share a rank;
block index identifies an invocation.

Three immutable resource projections describe C, V0 and V1 order. The two
AIC/vector catalogs supply backing identities, not independent scheduling
priorities. Each cluster stores three heads in SSBUF. An ordinary V can run on
either physical V, preferring V1 when available; its callable slot and
`sub_block_id` retain ordinary-V semantics. MIX reserves its complete actual
mask: C+V0, C+V1, 2V or 1C2V. A task must satisfy every target projection before
reservation. Independent resources may advance separately.

| Owner action | State and location |
| ------------ | ------------------ |
| Consume returned commands | Match FIN sequences in SSBUF; adopt actual defer registrations in GM before releasing the returned resource |
| Find an allocatable task | Read immutable projections/pieces and global allocation closure in GM; advance bounded SSBUF cursors |
| Reserve a block's complete target set | Reserve one execution record and all target `next` references in SSBUF |
| Allocate | Fetch a GM ticket, then commit the actual task/block identity in SSBUF; invalid tickets roll back only the local reservation |
| Wait for execution resources | Keep the reservation until every target's `running` is empty and `next` still names this record, all in SSBUF |
| Check metadata and dependencies | After that resource gate, observe Graph `metadata_ready` and predecessor completion in GM; retain successful progress in SSBUF |
| Dispatch | Move the complete target set from `next` to `running`, then publish executable commands in SSBUF |
| Execute and return | Worker prepares its GM payload and acquires inputs, releases ownership if held, runs the kernel and orders GM outputs/scratch. A single-resource worker with no defer contributes completion in GM before publishing FIN in SSBUF |
| Retire | Owner accounts for MIX or defer in GM as needed and frees the last SSBUF resource/record reference |

Allocation does not require ready dependencies or idle targets. It does require
complete `next` capacity before taking a ticket. Reservation, ticket allocation
and commit/rollback cannot be interrupted by an owner handoff. A physical
resource cannot hold two blocks of the same actual task across `running` and
`next`. Once a returned block is consumed, that resource can receive another
block of the task. Task identity includes the Graph invocation, not just a
kernel function ID.

Ordinary single-block entries share fetch-add tickets within cut-bounded pieces.
Pieces end at relevant MIX/SPMD resource boundaries and all synchronous
boundaries; an overshooting ticket cannot allocate from another piece. MIX and
SPMD tasks use four independent 128-byte ticket sectors plus one 128-byte
closure sector: 640 bytes per task claim structure. Sector `p` assigns block
`p + ticket * 4`. Its last valid ticket closes that sector; empty sectors start
closed. Four closed sectors prove all valid blocks were allocated, independently
of overshoot or execution completion.

For `sync_start` SPMD, every earlier node in both catalogs must first be allocated.
While the cohort still has unallocated blocks, no later task may be newly
allocated. Earlier work can continue returning and satisfying dependencies.
Every valid cohort ticket already has a complete local reservation, and no
physical core can hold two of its blocks. Closure therefore proves sufficient,
disjoint reserved execution opportunities, although older kernels may still be
using those cores. Each owner starts its local block once its targets are idle,
metadata/dependencies are ready and global allocation is closed, by publishing
ordinary executable commands. There is no separate start-arrival counter or GO
state. In-kernel cross-core synchronization, including correct end sync, remains
the kernel's responsibility. Host and discovered-topology checks reject cohorts
wider than the eligible physical resources.

A static head blocked on dependencies can leave resources idle. Reordering or
PGO may improve such cases; it does not remove runtime dependency checks or
make external defer duration predictable. Three cores simultaneously in kernels
also leave no independent scheduler or defer poller.

## Completion, defer and terminal work

Completion remains a logical contribution counter: `c = 1 - T` initially,
ordinary contributions add their shares, and `c > 0` means complete. Multi-block
tasks use four 128-byte components, selected by block index modulo four; all
components must complete. Single-block tasks and Graph shells use one counter.
Independent atomic groups occupy 128-byte GM sectors; scalar cache maintenance
still operates on 64-byte lines. Host initializes completion and claim state in
each uploaded run image; workers do not recreate those seeds at lane entry.

The physical 64-bit completion word is
`HAS_DEFER | (BIAS + c)`, with `HAS_DEFER = 1 << 63` and `BIAS = 1 << 62`.
The supported total is `T <= (1 << 61) - 1`. At most T physical invocations can
each create one deferred group, so even if every defer debt arrives first,
the low field stays in `[3, BIAS + 1]`. Native positive/negative fetch-add
contributions cannot carry into or borrow from the tag. Readers mask and decode;
a signed comparison of the raw word is invalid. Only actual defer publication
sets the tag with CAS, preserving concurrent progress. The tag remains set until
new-run initialization. Ordinary dependencies need no defer-index read.

| Physical return | Return publication in GM | Signal settlement in GM |
| --------------- | ------------------------ | ----------------------- |
| Ordinary single C/V | Worker adds 1 before FIN | None |
| Deferred single C/V | Withhold its physical contribution | Add 1 after all group conditions hold |
| MIX with p participants | Adopt each actual deferred participant with debt -1; after every FIN, add p once | Each deferred participant's group restores 1 |

MIX can release an early returned resource after its FIN is consumed. The record
stays pinned until the last required FIN and completion accounting. Defer does
not keep a core occupied after physical return.

Each actual deferring invocation produces one persistent 192-byte GM group,
containing up to four AND conditions, completion targets, dual index links,
owned-list link, pending mask and claim state. Scratch and payload are reusable
once the owner has adopted their valid registrations. The producer owner records
MIX debt before exposing settlement, publishes both the component index and
optional Graph-shell index while the group is `Initializing`, then releases
`Unclaimed` and sets completion tags. Readers retry initialization rather than
following partially published links.

A successor checks defer only after its own target resources are idle and
reserved. If its predecessor is incomplete without a defer tag, it only waits
on completion. If tagged, it enumerates that completion's GM index. The first
eligible successor cluster to CAS a group `Unclaimed -> Claimed` retains
responsibility until `Settled`; other successors wait for completion. Ownership
roots/cursors are in the waiting successor's SSBUF record, and full links/token
progress remain in GM. Owner core handoff preserves this responsibility; pending
polls do not release or re-claim it. Index-head observations and cursors avoid
rescanning an unchanged list from its beginning on every pass.

The responsible owner polls bounded group work, saves consumed conditions and
settles once all conditions hold. Completion and optional shell contributions
precede the settled count and terminal state. Destructive event consumption,
progress persistence and settlement cannot hand off midway. Existing token
backends retain their semantics; a CCU event is not a general SPR/SSBUF signal.
Same-card producer ordering still requires explicit task dependencies.

A Graph shell starts at `1 - total sink contributions`. A body sink's ordinary
or deferred contributions update both its own component and the shell. Its two
indexes refer to the same defer group, not duplicated consumable tokens.
Non-sink defer delays downstream sinks through dependency edges, naturally
holding the shell incomplete.

After local allocation and resources drain, one cluster globally claims a
zero-kernel terminal record and observes all top-level sinks, adopting otherwise
unclaimed defer through the same dependency mechanism. Other drained clusters
may close. The AICPU supervisor declares run completion only after all top-level
sinks are logically complete and `async_registered == async_settled`; it retains
watchdog and EXIT/nonce-handshake responsibility. No per-core whole-graph block
progress counter is needed. Terminal work does not emit a fake kernel record.

## Metadata, payload ABI and failures

Host resolves ordinary-task callables and tensor/scalar spans at bind. AICPU
materialization resolves Graph body metadata before publishing `metadata_ready`;
owners do not parse unpublished body descriptors. Graph roots observe shell
fanin. Static shape, rank and completion geometry are available before
materialization. The Definition carrier remains available to AICPU materializers.

Each worker prepares only its commanded payload in GM: function, arguments and
block Context. Payload publication writes back the control line, populated
argument prefix and global-context line, then orders publication. Input cache
acquisition follows dependency readiness. Predicated-off and dependency-only
tasks contribute completion but emit no business-kernel record. The public
Context and dispatch ABI remain shared with the supported runtime interfaces.

Workers observe shared error and SPR EXIT periodically during scheduling.
Owners publish the first run error, and AICPU observes errors and broadcasts
EXIT. Neither publication preempts a running kernel or fences admission before
the next observation. Registration errors, exhausted capacity and inconsistent
protocol state fail the whole run; claimed groups are not reassigned as healthy
recovery work. A kernel that never returns relies on platform timeout/teardown;
a failed poisoned Worker cannot accept another run. Final retirement rechecks
errors after supervision stops, covering failures during the remaining drain.

## Profiling and validation

`enable_chip_swimlane` uses the standard levels:

| Level | Recorded data |
| ----- | ------------- |
| 0 | Disabled; uninstrumented worker implementation |
| 1 | Kernel execution through the shared collector |
| 2 | L1 plus local dispatch/return timestamps joined by `(worker_id, reg_task_id, run_epoch)` |
| 3 | L2 plus nested percounter intervals of at least 1 microsecond and GM/SSBUF/cache/barrier operations |
| 4 | L3 with all instrumented intervals retained, plus Host orchestration phases |

Each worker has an **AICore Scheduler** lane grouped by cluster and C/V0/V1.
Owner service, allocation, dependency/defer processing, FIN aggregation and
worker preparation are scalar intervals; L1 alone does not show these stages.
The standard converter accepts `runtime=percounter`, preserves HBG task identity
including Graph bodies, and nests kernel/service intervals without double
counting parents. Host runtime selection reports mode 5.

Receive/dispatch timing starts at execution of the commanded payload; start is
immediately before the kernel call, and end follows the output-store barrier.
Finish means local return, not final sibling return or consumer observation.
Task identity is captured at return before payload reuse. No async token is
stored in a payload. L2+ task records are published after the business completion
signal; terminal cache clean/order publishes scalar records before retirement.

L2 reserves at most 65,536 task records per worker, bounded by graph block count;
overflow fails timing export. L3/L4 additionally reserve 8 MiB per worker:
1,048,576 compact 8-byte rows or 209,715 extended five-word rows. Compact rows
pack start offset, duration, phase and nesting depth; extended rows retain
out-of-range timestamps and task reference. Scalar overflow exports
`capture.truncated=true` and a dropped count. L0/L1 allocate neither tail.
Only headers are uploaded; committed rows are downloaded after retirement.
The detailed stream covers post-startup scheduling through record publication,
excluding final retirement wait. Use level 0 for latency measurements.

L1 uses `PLATFORM_AICORE_BUFFER_SIZE` records per rotating shared-collector
buffer, publishing full buffers and the final partial tail. AICPU/Host drain and
refill them; backpressure beyond the platform budget reports timeout. Memory
tracing preserves existing protocol accesses: returning loads/atomics finish
when consumable; unused-result operations record issue time. DCCI issue and DSB
completion are distinct events.

Common device timing reports **AICore wall** independently of L1: earliest actual
kernel start to latest actual kernel end, including scheduling/dependency gaps
between those endpoints and excluding setup/handshake/final drain. See
[Host trace timing](../../../../docs/dfx/host-trace.md).

CPU protocol tests exercise production control, allocation and completion seams;
they do not prove A5 cache/SSBUF ordering or performance. Deferred peer scenes
require two A5 devices and remain manual. Build and test with the repository's
allocation and architecture-precheck workflow:

```bash
python -m simpler_setup.build_runtimes --platforms a5 --runtime percounter
pytest tests/st/a5/percounter/defer_wait --platform a5 --manual include
```

Peer scenes validate received payload before consumers complete. CPU defer
tests inject external completion explicitly rather than scheduling a hidden
same-card notification kernel behind its waiter.
