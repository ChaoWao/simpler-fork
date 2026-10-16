# Public caller-stream DEVICE data chain

Baseline: main `4aedb1f4318f024f9d50392bb312673523841632` (#2604). Branch `test/public-caller-stream-state-chain`, head `f2a559418f54e599571040345e987e8f2ed3d878`, one commit; PR [#2606](https://github.com/hw-native-sys/simpler/pull/2606) is open; CI is pending. Product diff is one existing test file, +29/−5.

## Scope

The existing public caller-stream hardware test alternates streams but uses independent inputs. The new sequence uses three real public Worker calls on streams A → B → A. Each edge records a caller-owned event after the prior output D2H, waits on it in the next stream, and D2D-copies the prior DEVICE output into the next DEVICE input. The next input is first filled with -31, making a missing copy observable. Every output word is checked against the dependent golden sequence; three calls keep those integer values exactly representable in float32.

This is the existing Worker/Buffer/Tensor/TaskArgs/RunHandle path on both runtimes, owned/borrowed contexts and RTS TASK_QUEUE_ENABLE=0/1. Wrong-context wait/result/submit/close refusals and external source close refusal remain exercised. There is no successful RunHandle wait/poll before caller-stream synchronization and result checking.

Caller D2H/D2D and event waits have their own last-use obligation: the final cleanup proves caller-stream completion before releasing caller events or raw allocations. A kernel completion fence alone is insufficient for those external operations.

## Evidence in progress

- Supported-main runtime rebuilt locally. Changed-file hooks pass.
- Exclusive A3 task `task_20261016_052106_416490226662` ran a negative control and then all eight owned/borrowed × runtime × task-queue combinations. Removing the D2D from a temporary copy makes the second HBG output 702 instead of 600, as expected; all eight positive combinations passed (78.67 s), followed by task exit 0 and lock release.
- Full CPU Python UT completed with 3348 passed / 7 skipped / 108 hardware cases deselected in 141.28 s, with product files held stable. The subsequent source edit only clarifies that wrong-context RunHandle queries are refused; changed-file hooks pass after that comment change.
- A5 hardware is unavailable locally; its existing parametrization is retained for CI.

## Acceptance boundary

The public stream/data chain checks ordered results. Conservative native admission may still drain a predecessor, so this does not measure host overlap or prove nonblocking admission. Fixed-shape framework emission is separately owned by PR #2605 and is not implemented or qualified here. Partial enqueue, fault/DFX and the borrowed HBG first-empty capacity limitation remain separately tracked. No production runtime or public interface is changed.

Local evidence: [A3 eight-case task](../../LOCAL-EVIDENCE.md#artifact-029ffb5b86c5), [missing-D2D control](../../LOCAL-EVIDENCE.md#artifact-edf6090a9a70), [changed-file hooks](../../LOCAL-EVIDENCE.md#artifact-fd89d919c0ba), [baseline rebuild](../../LOCAL-EVIDENCE.md#artifact-e7b5803334d6).

[Full CPU Python UT](../../LOCAL-EVIDENCE.md#artifact-25732957e1bd). Runtime rebuilt after the final commit; all 20 focused public borrowed checks passed in 0.77 s. The full hardware matrix above belongs to the precommit candidate with the same executable test content, not to an additional final-head board run.

Final [head rebuild](../../LOCAL-EVIDENCE.md#artifact-6baa0010c90e), [20 public borrowed checks](../../LOCAL-EVIDENCE.md#artifact-59553c0916bb), and [initial CI status](../../LOCAL-EVIDENCE.md#artifact-7cb20f12b2ad). No A5 hardware result or merge is claimed at this checkpoint.
