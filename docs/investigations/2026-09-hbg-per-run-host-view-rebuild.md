# Rebuild the orchestrator's host views every run instead of caching them per allocation

**Date**: 2026-09-13
**Verdict**: dropped — the cache #2205 prescribes is needed after all

## Question

Issue #2205 gives a child-memory tensor a host view so a `host_build_graph`
orchestration can read and write it. It prescribes caching that view for the
device allocation's lifetime, held by whoever owns `device_malloc` /
`device_free`, because #1848 measured `halHostRegister` / `halHostUnregister`
at ~256 ms per run over 38.05 GiB.

Rebuilding the view every run instead is tempting, and three of the arguments
for it hold regardless of cost:

1. A cached host VA that outlives its device allocation hands out a mapping
   into a returned page. Per-run rebuild makes that bug structurally
   impossible.
2. A data-dependent orchestration can read different tensors on different runs.
   Rebuild tracks that set; a cache accumulates their union over the Worker's
   lifetime with no eviction.
3. A resident mapping pins host page-table entries for the Worker's whole life.
   Rebuild bounds the peak to one orchestration window.

It is also much less code: no `HostApi` hook, no `DeviceRunnerBase` member, no
sim counterpart.

The question is whether per-run establish + release is cheap enough at the
sizes this feature actually touches. Every orchestrator-accessed tensor in the
corpus is ≤256 KiB (`block_table` in `paged_attention` Case1), five orders of
magnitude below #1848's 38 GiB.

## What was tried

Issue #1848's cost model — O(mapped bytes), ~6.7 ms/GiB — is an **inference**,
not a measurement. Its own text says so: it had one aggregate point (20
mappings, 38.05 GiB, 255.98 ms) and reasoned from the size asymmetry of those
tensors that a fixed per-call term was implausible. A per-call floor of a few µs
is invisible in that aggregate but decides this question.

So: sweep size at a fixed call count and read the intercept and the slope
apart. `halHostRegister(DEV_SVM_MAP_HOST)` and `halHostUnregister` are timed
separately, rotating over 4 distinct `aclrtMalloc` allocations per size so no
measurement is served by HAL state a real bind would not have. 300 iterations
per size, reporting min and median.

```bash
# .docs/bench_host_register.cpp in the PR branch; not retained in-tree
g++ -std=c++17 -O2 bench_host_register.cpp -o bench_host_register \
    -I$ASCEND/include -L$ASCEND/lib64 -lascendcl -ldl
task-submit --device auto --device-num 1 --run .docs/run_bench.sh   # a2a3, CANN 9.0.0
```

## Result

```text
device=1 iters=300 buffers=4
         bytes    reg_min    reg_med  unreg_min  unreg_med   pair_med
            64      1.270      1.580      2.790      3.741      5.321
          4096      1.280      1.610      2.600      3.600      5.210
         65536      3.470      4.140      3.590      4.860      9.000
        262144     10.480     11.340      5.980      7.420     18.760
       4194304      2.350      3.430     27.700     29.371     32.801
      16777216      6.640      8.760    102.851    105.301    114.061
     268435456     86.460    115.901   1598.205   1649.126   1765.027
```

**There is a per-call floor of ~5.2 µs per register/unregister pair**, stable
across two independent runs. #1848's reading that the cost is essentially all
per-byte is wrong at small sizes — it was right about the regime it measured
and does not extrapolate down.

The per-byte slope from the two largest points is
`(1765.03 − 114.06) / 240 MiB = 6.9 µs/MiB`, i.e. **7.0 ms/GiB** — which does
confirm #1848's 6.7 ms/GiB for large buffers. Both readings are right in their
own regime.

Register is **per mapped page, not per byte**, and the page size is the
allocation's: 64 B = 1 page = 1.58 µs; 64 KiB = 16 ordinary pages = 4.14 µs;
256 KiB = 64 ordinary pages = 11.34 µs (≈ 0.16 µs/page above the floor); 4 MiB
takes huge pages under `ACL_MEM_MALLOC_HUGE_FIRST` and drops back to 3.43 µs
for 2 pages. That non-monotonicity is the mechanism, not noise, and it is the
same page-configuration sensitivity as #1531.

Cost for a realistic `paged_attention` bind, which reads two control tensors:

| tensor | bytes | pair |
| ------ | ----: | ---: |
| `context_lens` | ~1 KiB | 5.2 µs |
| `block_table` | 256 KiB | 18.8 µs |
| | | **24 µs/run** |

Against a 0.6 ms warm `chip.run` that is **~4%**, paid every run, on the bind
critical path before the device starts.

## Why not (now)

The decision rule was fixed before the number was seen: under 10 µs per run for
a realistic bind, ship rebuild-only. The measurement is 24 µs. Rebuild loses on
its own pre-committed terms.

Two details sharpen it beyond the headline ratio:

- **The floor makes small tensors cost more than they look.** An orchestration
  that reads a 32-byte `num_tokens_per_owner` still pays 5.2 µs to establish
  the view. Cost tracks the number of accessed tensors, not their size, in
  exactly the regime this feature lives in.
- **The slope makes the stated trigger a cliff.** #2205 says the case to worry
  about is a tensor that is both large and orchestrator-accessed. Rebuild would
  charge 1.77 ms/run for a 256 MiB one — three times the whole run. The cache
  turns that into a one-time cost, which is the difference between a feature
  with a sharp edge and one without.

Caching also removes the need for any access-count heuristic. Without it there
is a real crossover — a tensor read only two or three times is cheaper served
by per-access `rtMemcpy` (~1.5 µs each) than by a 5.2 µs register/unregister
pair — and picking between them per tensor is a policy nobody wants to own.
With the cache, SVM-always is right.

The three cost-independent arguments above do not go away; they become design
constraints on the cache rather than reasons to skip it. Invalidation is why it
is held next to `mem_alloc_` and dropped in `free_tensor` and `finalize`, and
the monotonic-union and pinned-page concerns are why the total cached mapped
bytes are logged.

## When to reconsider

If `halHostRegister`'s per-call floor drops to ~1 µs, a 2–3 tensor bind costs
~3 µs and the cache stops paying for its invalidation surface. Re-run the sweep
above on a newer CANN before assuming the floor still holds.
