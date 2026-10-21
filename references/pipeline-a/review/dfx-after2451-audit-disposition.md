> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# DFX after #2451: finite audit disposition

Baseline: `bef53236679a0ff56d38abbf38d5a738f40e530c`.
Reviewed original revision2: `../../../temporal-sauteeing-shell/.docs/dfx-after2451-next-boundary.md`.

## Current direction — 2026-09-25T03:17:45.617267+00:00

User asked to continue DFX. Original temporal resumed for a concrete ArgsDump background payload-and-manifest contract. The audit below remains historical evidence, but comparative performance measurements are no longer a prerequisite to specifying functional async ownership. No new implementation/test authorization is inferred. ScopeStats/DepGen remain subsequent gaps.

## Historical outcome

The authorized finite source audit is complete. No new implementation PR is selected or approved. This does **not** mean the DFX workstream or its full asynchronous goal is complete. ArgsDump, ScopeStats and device-side DepGen still have synchronous boundaries. Original temporal author is idle.

## Established boundaries

- ArgsDump copies payload out of the arena into an owned vector before handing it to the writer. Disk progress is the current acknowledgement/backpressure policy; new device arenas are not proved necessary. Moving writer completion past next-run counter resets still requires a concrete generation/ownership contract and bounded host storage.
- Moving only the manifest can leave payload write/join on the boundary. That is a bounded option, but it must not be advertised as complete asynchronous ArgsDump.
- DepGen already has per-run record identity and receives a per-run output prefix. A source comment mentioning session output is not a prohibition. TMR export quiesces and reconciles, while HBG graph emission happens at bind; their boundary costs and scopes differ.
- ScopeStats holds a shared vector and live metadata, with a serialization allocation coexisting with the records. Its device dropped/total fields alone do not express host budget loss or an unknown cut. Retention needs an explicit resource/error contract.

## Claims the manager does not adopt

The revision's suggestion that DepGen's multiple-epoch refusal is a reachable corner-case bug remains unproved: no production sequence through quiesce/reconcile has been demonstrated. It is an explicit unsupported condition, not evidence of silent loss in supported use. Similarly, only ArgsDump being “plausibly large”, repeated graphs being identical, and diagnostics being one-shot are not accepted workload facts. No comparative performance claim follows from this static audit.

ScopeStats is not impossible and changing a format is not inherently out of bounds for a future approved contract. We decline to select it merely for uniformity; this is prioritization under missing workload evidence, not a proof that no useful design exists.

## Concrete next evidence, before choosing a PR

On an identified repeated-run workload with diagnostics actually enabled, distinguish:
1. ArgsDump copy/drain and writer-join time from metadata merge/sort/manifest time;
2. ScopeStats drain/close snapshot from JSONL serialization/output;
3. TMR DepGen drain/reconcile from graph emit (HBG bind work reported separately).

Record run counts, enabled collectors, payload/record volume and distributions, not a single aggregate. Prefer an already captured suitable trace/log. A new measurement run, instrumentation or architecture change requires its own concrete scope; none was executed or authorized by this design audit. No test rerun, CI POST, new API, source change or PR was performed.

## Other current work

Memory L3 remains the existing user architecture decision. PR2443 and PR2446 remain CI-blocked with exact retries consumed; no new evidence or safe concrete patch has appeared. External2447 is read-only tracking. There is currently no in-flight writer or CI work; timer pauses until user decision, merge notification, new authorization or actionable diagnostic evidence.
