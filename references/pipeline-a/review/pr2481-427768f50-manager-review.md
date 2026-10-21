> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# PR2481 427768f50 manager review

Exact head: `427768f504ef5952d55bba1e6aeb61e09fa0b3e3`.
Increment from reviewed e99e34b78: two production files, +111/-22.
Current remote base35e9331b5; merge-basec4ffb02f2. Original result18728 read,
UUID matches, is_error=false, clean author tree, no duplicate writer.

## Disposition

Static review accepts the bounded invariant repair. Current-head CI is now complete
(19success/1skip); the previously failing output and early-enqueue acceptance cases
pass. Original author corrected inaccurate causal claims in the PR metadata
without changing the head. **READY, awaiting user merge.** Caller-side origin of bank0 remains unresolved.
See the completed evidence below; do not infer root-cause localization from green CI.

## What was checked

- Platform resolves the arena bank before placement construction, reservation,
  HostApi binding and setup_static_arena. The resolved descriptor is then shared
  consistently by these consumers. The only existing producer is ChipWorker's
  arena_bank_for_slot; no second caller-side bank consumer needs updating.
- Current four onboard runtime declarations have GM_HEAP and agree on all declared
  arena copy counts (worker validates this). For valid slots, runtime max depth
  versus worker granted depth does not change mapping: per-run is slot_id when
  depth>1, scratch is bank0. A depth-one grant admits only slot0. This acceptance
  does not generalize to hypothetical contracts with only GM_SM or arbitrary bank
  remapping; the new helper omits GM_SM unlike the worker helper.
- Before bind can rewrite a region, overlap of a declared per-run bank is refused
  via the existing cleanup_failed_prepare and PREPARED_INCOMPATIBLE path. This
  safe fallback cannot satisfy the early-enqueue acceptance by itself; the existing
  two-LAUNCHED, full device serial-order, numerical and refill checks must still pass.
- Weak shared-bank compatibility changes 1 to0. Both TMR implementations provide
  their own probe; A5 HBG lacks one, so an unexplained shared bank no longer gains
  permission from a weak unconditional yes. Per-run distinct banks bypass the probe.
- Successful image-copy LOG_WARN is removed, destination is carried on the existing
  bind-phase record; failure logs correctly treat destination contents as unknown
  after nonzero copy status. Existing timing emission can still occur: this is not
  a promise of complete ordinary-run log silence.
- No change to input ownership, event retirement, device serialization, numerical
  oracle, warmup/drop-first behavior or public resource depth. No new PTO_/PTO2 name.

## Evidence requirements recorded before CI completion

Current A5 onboard job109436908507 / workflow36577189939 must establish distinct
live storage/publication and correct first output while preserving actual early
submission and sustained refill. A3/TMR/default-depth regression checks remain
relevant because c_api_shared.cpp is shared code. There is no new isolated test
of this platform boundary; existing real Worker.submit integration is the decisive
coverage, not helper-only reasoning or a simulator that disables joined launch.

If the descriptor-mismatch error fires, inspect the worker's latched contract and
bank-return value through the original author. If it does not fire and outputs pass,
that alone does NOT locate the previous loss in the reservation path: the revision
changes resolution plus two rejection conditions, and execution/log sampling can
also differ. Preserve this uncertainty in final PR evidence. No speculative extra
instrument-only head or wording-only dispatch is requested while CI runs.

Prior job109418430382 proves equal non-null bank bases and identical successful
copy destination/length within the same child during a live predecessor. It does
not identify the exact predecessor reads that yielded zero. Same failure family
remains four job occurrences/eight cases; do not reset its count or retry it.

## Validation and feedback

Source review only; manager ran no test, build, install, retry POST or merge.
Remote feedback fully paginated:0 review threads/0 submitted reviews/10 comments.
The latest author comment's strong root-cause inference is narrowed above. DFX
PR2483 remains independently Ready at1d79e7263 (19success/1skip, all6threads resolved).

## Completed current-head CI evidence — 2026-09-29T14:33:35.012595+00:00

Source: manager downloaded full GitHub job109436908507 log to
`../claude-managed/pr2481-427-a5.log`. Workflow36577189939 checks merge
fb4c0860da0a461389fceca6d1323b22129ebb69 of427768f50 into35e9331b5
(checkout lines301–304; the runner's earlier checkout is not the executed target).

- A5 two-run case PASS6.5s/device7, line692.
- A5 sixteen-run refill PASS7.9s/device5, line705.
- A5 default-depth-one no-overlap control PASS16.5s/device6, line717.
- All five DFX smoke groups exit0, lines1704–1771.
- Total current head19success/1skip; A3 onboard, hardware UT, CPU and simulator
  checks completed. Feedback pagination complete,0 unresolved threads, no new
  external action item. DFX2483 independently remains19success/1skip.

Re-read exact test assertions: every output including the first is compared;
real concurrent LAUNCHED frames and an unfired predecessor boundary are required;
whole-operator timestamps reject measurable overlap, accept ordered or same-tick
readings, and sustained refill checks16handles/repeated pairs/monotonic identities.
A fallback that serializes every launch cannot pass these cases. Same-tick readings
are not sub-tick measurement proof; no latency benefit is claimed.

Passing pytest cases suppress captured child output. Public success logs therefore
DO NOT establish whether the mismatch LOG_ERROR fired or expose new bank addresses.
No conclusion about where the old bank value was lost follows from missing messages.
The numerical/integration blocker is cleared for this head and supported scope;
the origin of the caller-side inconsistency and exact old device-read sequence are
residual diagnostic unknowns, not newly proved facts. No tests/retries were run by
manager. Original author has a metadata-only closeout to remove the overclaim;
no new commit, push or speculative diagnostic revision is requested.

Manager also fetched the exact CI merge fb4c0860 and reviewed the shared-platform
head-to-merge delta: it contains the already-merged2473 context-init/teardown proof
and retention-probe DrainAttempted/DrainProvedComplete fixes. The new bank selection,
shared-bank gate and A5 publication repair are unchanged in the tested integration.

Final closeout 2026-09-29T14:39:49.284370+00:00: original UUID result is_error=false, unchanged clean427768f50.
Body and comment5892426206 independently read; unsupported reservation-path
inference explicitly withdrawn, captured-log and existing timing-emission limitations
retained. No remaining blocking feedback. Completion ACKed; author idle awaiting merge.
19success/1skip is the PR check rollup, not a count of steps inside the A5 job.
