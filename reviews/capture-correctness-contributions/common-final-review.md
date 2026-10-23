Delta inspected. Scope is exactly as you describe, and I verified that rather than taking it on trust.

**Identity checks**

- `git diff e0d6e22 dbfbaf8` (two-dot, siblings) touches one file: the header, +4/−2, comments only.
- Blob hashes are equal at both commits for the runtime and the test source — `kernel_device_resources.cpp` `93e86f4…` and `test_kernel_execution_state.cpp` `9f419cd…` — so the production TU and the test TU are byte-identical to what I reviewed at e0d6.
- `dbfbaf8^` is `f701832b`, the published #2626 head. Whole correction `f701832b...dbfbaf8` is still the same three files, now 9/5 + 2/1 + 125/0.

## Should #1 — capacity vs. binding: **closed**

```cpp
// Enables binding of prepared views; the first successful prepare() already
// fixed the original capacity.
int freeze();
```

Both halves check out against the code:

- *Enables binding of prepared views* — `bind()` returns `PTO_RUNTIME_ERR_INVALID_STATE` unless `frozen_` (`kernel_device_resources.cpp:145`), and `freeze()` itself requires `prepared_` (`:137`). That is the complete and only role `frozen_` plays; it appears nowhere in `prepare()`.
- *The first successful prepare() already fixed the original capacity* — `layout_` is assigned once, at `:105`, in the first-call branch, and `:75`/`:83` compare every later request against it. The qualifier "successful" is tighter than what I asked for and is the correct one: `prepared_` is set only at `:132`, after allocation succeeds, and a failed first call runs `close()`, which resets `layout_` and `prepared_` (`:182-186`) so the next call is a first call again. A reader can now derive from this comment alone that a second `prepare` cannot grow the layout even when `freeze()` was never called — which is what the old wording denied.

The false attribution ("later prepare() calls cannot grow it", as a property of `freeze`) is gone, not merely softened.

## Should #2 — refusal scope: **closed**

```cpp
// Refusal of a repeated request leaves all views intact.
```

Now scoped to repeated requests, so the first-call allocation-failure path at `:115`/`:121` — which calls `close()`, clears `views_`, latches `closing_`, and drives `phase_` to `Closing` via `kernel_execution_state.cpp:229-232` — is no longer inside the claim. The scoped claim is still true for every refusal in the repeated branch: `:61`, `:62-63`, `:64`, `:66-69`, `:73-75`, `:80-83`, `:88`, plus a throwing `next_views` construction at `:77` caught by the wrapper, all return before the sole mutation `views_ = std::move(next_views)` at `:93`.

Plural grammar also fixed: "nonempty requests retain **the** original offset."

## Remaining

No Must. No new Should — the delta introduces no claim I can contradict from the source, and the two-line split creates no formatting or wrapping concern.

Still open by your explicit decision, unchanged in status and both non-blocking:

- The struct-level comment at header lines 43-45 keeps "the original offset or capacity **used when** the region becomes nonempty again". Accurate, just awkward phrasing; this was a Consider, not one of the returned Shoulds, so I am not reopening it.
- No assertion pins that a region first prepared empty can never become nonempty (`:83` against a zero original `bytes` → `PTO_RUNTIME_ERR_PREPARED_INCOMPATIBLE`). Unauthorized here; worth carrying forward as a note for whoever next edits the repeated branch.

## Verdict

**Approve the amended head `dbfbaf8`** for inclusion in #2626. Both returned Shoulds are closed with correct, verifiable wording; the reviewed mechanism is bit-for-bit the one I analyzed at e0d6, so my §2 correctness argument transfers without re-derivation.

Scope of that statement, stated plainly: it covers the comment delta and the unchanged mechanism as read in source. The fresh normal + Release 68-target requalification starting at this head is not claimed green by me; the eight production-TU mutant results remain your evidence from the source-equivalent old head and I am not asserting they were executed at `dbfbaf8`; and nothing here signs off full C2, capture, current main, or hardware.
