# Static runtime capability contract

Baseline: main `17129ba496c6d7d710b0e1320b3d1c25aeb9d1e1` (#2606). Branch `feat/runtime-capability-contract`, one commit `169e6bb884bd86927d34fb3b521444b932f85a48` on the stated baseline; PR [#2607](https://github.com/hw-native-sys/simpler/pull/2607) is open; CI is pending. Product diff: 9 files, +257/−11. This delivers the capability/profile subset of S3; module ownership and graph/callable/run retention remain separate work.

## Contract

The shared C ABI query receives a version/size/flags structure, creates no context, touches no device, and writes a 16-byte prefix by value. Invalid version, size and null arguments leave output unchanged; larger caller buffers retain their tail. Unknown flag bits are ignored. Onboard declares owned + borrowed + native caller ordering; sim declares owned. Both leave capture/replay disabled.

ChipWorker resolves the optional query, validates its ABI/result, and checks the requested profile before creating a context. An older module still satisfying existing required exports retains ordinary owned initialization; absent query does not grant borrowed or caller-stream use. Advertised caller support additionally requires its validation/context/launch functions. Per-call checks remain unchanged. No whole-API function table, public Python object, executor or device-argument ABI is added.

## Red/green evidence

With only contract/test additions, the old loader accepted 10 requests that should be refused (10 failed / 3 legacy-owned controls passed). The tests then cover refused initialization with zero context creations and successful retry on the same Worker. Four real sim modules initially fail for a missing query symbol. After implementation, 169 related Python tests pass, including real C ABI null/short/wrong-version/extra-tail checks.

The first full C++ run exposed one bypassed-init fixture that supplied caller function pointers but no capability state; prime_caller_streams now states the capability it models. The full C++ suite then passes all 321 targets.

## Validation in progress

Full CPU Python UT passed 3366 cases / 7 skipped / 112 hardware cases deselected (150.65 s). Both complete dispatched sim sweeps exited 0 (A3 61 Resource jobs, A5 57; both L2 runtime groups pass, with capability skips counted separately). Exclusive A3 task `task_20261016_061025_248711820963` completed successfully: its query checks (four sim modules plus two A3 onboard modules) and all eight public caller combinations pass; the full ordinary hardware sweep exited 0 in 581.22 s, with all four device locks released. No new AIV incident was observed; no AIV root-cause fix is claimed. The task checks real query ABI, the 8 public caller combinations, then the ordinary complete hardware corpus with SDMA quarantine markers excluded as CI prescribes. NPU/process/queue snapshots and isolated Ascend logs are saved locally. A5 hardware is unavailable locally and must be qualified by CI.

## Boundary

Flags describe available entry families, not safe admission, arbitrary dynamic shape support, a framework background queue, or graph lifetime. #2605 and #2588 remain independently owned. Existing borrowed HBG first-empty capacity and serial fatal-log test observations are not fixed by this query. Module/callable/run/graph retention and the complete S3 module owner remain required before C2.

Local evidence: [old loader red](../../LOCAL-EVIDENCE.md#artifact-11717e2213ba), [missing query red](../../LOCAL-EVIDENCE.md#artifact-5375d0121f9c), [related green](../../LOCAL-EVIDENCE.md#artifact-07114129f728), [full CPU Python UT](../../LOCAL-EVIDENCE.md#artifact-8c9f63679303), [full C++ UT](../../LOCAL-EVIDENCE.md#artifact-7490d94d2ccd), [A3 sim](../../LOCAL-EVIDENCE.md#artifact-a3327760b35a), [A5 sim](../../LOCAL-EVIDENCE.md#artifact-6a17a7c45f1b), [hooks](../../LOCAL-EVIDENCE.md#artifact-71cd3ca5db81). Raw logs remain local and are excluded from the design backup branch.

[A3 query/public/full-sweep task](../../LOCAL-EVIDENCE.md#artifact-6496ee051ddb). The final loader tests compare counter deltas rather than assuming a fresh cached test DSO; their focused rerun is recorded before commit.

The final cached-DSO counter test rerun passes 14 cases in 4.15 s. Full-suite results above precede this assertion-only refinement and commit; the executable production content is identical. Final head rebuilt successfully and all 169 related CPU Python cases passed again (12.75 s). No final-head hardware rerun or A5 hardware qualification is claimed.

Final [head rebuild](../../LOCAL-EVIDENCE.md#artifact-ee1f3f654576), [169 related tests](../../LOCAL-EVIDENCE.md#artifact-8e213fb2a442), [counter assertions](../../LOCAL-EVIDENCE.md#artifact-3dac033ae80d). Source content was frozen during full-suite runs; the only later source change makes cached test-DSO counter assertions relative to their entry values.
