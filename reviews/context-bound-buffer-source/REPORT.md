# Native environment identity and Buffer sources — PR #2587

2026-10-15. Base `653a24c4a61fde1ef8c28838b67a4a6dd37a916b` (#2584 merged).
[PR #2587](https://github.com/hw-native-sys/simpler/pull/2587), head `430d5f4d6a8b9f525d74a75a2cfe88906363a252`.
Nine product/test/doc files, +217/−12. Planning and this evidence remain outside the code repository.

## Contract and scope

ChipWorker owns one host-only environment identity from native context creation through retirement: process, non-repeating incarnation, nonzero logical context generation, device and owned/borrowed policy. Borrowed generation comes from init; owned generation uses its environment incarnation. Failed init rollback or teardown preserves the binding for explicit retry; successful cleanup clears it.

The existing runner still owns actual RTS identity/current-context validation. No raw RTS pointer or new C ABI is exposed. The internal binding snapshot associates source liveness with that runner; it cannot detect an external RTS reset or validate an arbitrary pointer's allocation. Worker source liveness compares the current native snapshot with the captured one, alongside existing READY/failure gates. A successful native teardown invalidates an old source even with a stale READY facade. Source release still discharges metadata references without freeing external storage.

The native shared validator rejects foreign-process bindings before entering host-runtime validation. The fork test proves validate/malloc/finalize refusal with host-backed stub DSOs; it does not qualify general post-fork Worker/lane operation. Public Worker borrowed init, adapter/materialization consolidation and capture remain subsequent work. K2b is not complete.

## Evidence

| Check | Result | Log |
| --- | --- | --- |
| Before implementation | 12 selected failures (initial selection; later added PID-field mutation case) | [red](../../LOCAL-EVIDENCE.md#artifact-d4c7d8a64349) |
| Related suites | 146 passed | [focused](../../LOCAL-EVIDENCE.md#artifact-5730e5973e12) |
| Python UT | 3261 passed, 91 skipped | [Python](../../LOCAL-EVIDENCE.md#artifact-2fc6ec2a261d) |
| C++ UT | 319 targets passed | [C++](../../LOCAL-EVIDENCE.md#artifact-d79dbc1b538d) |
| Sim | 13 A2/A3 and 10 A5 scheduler items passed, including seven HBG readiness arms each | [A3](../../LOCAL-EVIDENCE.md#artifact-6bd91c10800f), [A5](../../LOCAL-EVIDENCE.md#artifact-976cc1a40fa4) |
| Hardware | 44 caller/lifecycle tests plus seven HBG readiness scheduler items; task-submit device 0, task_20261014_215208_7377079281 | [hardware](../../LOCAL-EVIDENCE.md#artifact-773af8eb80cc) |
| Final committed head | Rebuilt, 234 related tests passed | [build](../../LOCAL-EVIDENCE.md#artifact-4ba5fb967d29), [tests](../../LOCAL-EVIDENCE.md#artifact-f2fa05f7a056) |
| Hooks | Changed-file hooks passed; docs rechecked after line wrapping | [hooks](../../LOCAL-EVIDENCE.md#artifact-484e973415a1), [docs](../../LOCAL-EVIDENCE.md#artifact-91b61fecb97f) |

Hardware uses separate [Ascend logs](../../LOCAL-EVIDENCE.md#artifact-5a77760e07cc) and [occupancy sampling](../../LOCAL-EVIDENCE.md#artifact-b123120dbc2a). No AIV occurred in this run. Real owned Program Buffer.wrap → Worker.submit is exercised by HBG readiness; borrowed coverage remains the existing native caller bridge, not full public borrowed qualification. A5 coverage is simulation on this A2/A3 host.

Native identity and cleanup lifetime tests cross the actual C++/nanobind interface against stub DSOs. Per-field source changes are deliberately synthetic tests; they assert refusal before identity allocation, host materialization or native submit and recovery when the original binding is restored. Two early test defects (a misplaced assertion variable and a recovery path that left its materializer interception installed) were corrected without product workarounds; all suites above are final reruns.

Historical #2584 evidence remains pinned to its original head. No claim of AIV remediation or complete kernel eager delivery is made.


## Review correction: successful early rollback — revision `51961a02`

Current head `51961a02a1ea2fdfe0f89445c15ff066f288425b`, rebased on main
`b95785db1` (#2586, pto-isa pin update). The initial measurements above stay
attached to `430d5f4d`; they are not new-head hardware validation.

CodeRabbit and the subsequent user review correctly found an untested early
catch in init. Grant refusal and invalid native storage size/alignment destroyed
the context and nulled its handle, but left the identity engaged. The initial
report's successful-retirement guarantee was therefore incomplete for that head.
The existing failed-native-init test exercised the later catch only.

The fix adds only `execution_environment_.reset()` after the early context
destruction. It does not clear unrelated runtime function bindings. Audit of
all three destruction exits confirms: early rollback now resets explicitly;
late init rollback clears the runtime binding after successful cleanup and
retains it if borrowed cleanup fails; successful finalize clears the binding
while failed teardown retains cleanup debt.

Six new cases cross owned/borrowed with grant refusal, invalid size and invalid
alignment. They assert the exact init-stage error, one context destruction, no
unnecessary finalize/native borrowed init, an empty identity, and successful
healthy initialization on the same object. The alignment case uses a value
above pointer alignment that is not a power of two.

- [Before fix](../../LOCAL-EVIDENCE.md#artifact-c40d52a293aa): all six failed
  on the stale non-None identity assertion. The initial alignment probe used 3;
  its final fixture uses three times native alignment to isolate the power-of-two guard.
- [After fix](../../LOCAL-EVIDENCE.md#artifact-4bd00d817556): six passed.
- [Final-head build](../../LOCAL-EVIDENCE.md#artifact-f7aca95ed3d6) and
  [related Python tests](../../LOCAL-EVIDENCE.md#artifact-4d9e2239c034): 240 passed.
- [C++](../../LOCAL-EVIDENCE.md#artifact-3c6284d18c8c): 319 targets passed.
- [Changed-file hooks](../../LOCAL-EVIDENCE.md#artifact-40aa144c7692): passed.

No new hardware work was needed for this correction: the injected failures
occur before the device initializer. The original hardware logs remain in their
original directory and are attributed only to the initial head. No new wire/API
or public borrowed qualification is introduced.
