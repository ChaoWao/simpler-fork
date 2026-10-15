# Callable fault DFX — 2026-10-09

## Scope

Local branch `fix/callable-fault-dfx`, base `f365ae97e`. No commit, push or PR.
The production change adds host-only diagnostics, not an AIV fix:

- Successful callable H2D: actual patched child entry range, sparse func_id, code length and FNV-1a64; default TIMING threshold.
- Registration: cid ↔ chip image hash ↔ AICore image hash.
- Successful free and abandonment delimit host allocation ownership.
- Completion failure before recovery: device/runner, epoch/generation/dispatch/slot, cid/image and runtime_args; ERROR threshold, no device readback.

Unit tests cover patched rather than unresolved addresses, sparse IDs, changed bytes at identical addresses, caller-source preservation and explicit WARN suppression. Product troubleshooting documentation explains the half-open ranges and attribution limits.

## Same-run fault evidence

Full onboard task `task_20261009_181647_15293812165` owns devices 3/4/5/6.
Timeout requests: scheduler 2000 ms, op execute 3000000 us, stream sync 4000 ms.
This is the normal platform-regression sweep, with both simulator sweeps running concurrently initially. It is not a minimized reproduction.

| Case | PID | Device | func_id | Published code range (end exclusive) | Fault current PC | Code FNV-1a64 |
| --- | --- | --- | --- | --- | --- | --- |
| Early enqueue depth-one control | 164023 | 6 | 1 | 0x12c0c00ee6e0–0x12c0c00ee75c | 0x12c0c00ee75c | 0x43924124fff53dc6 |
| HBG dep-gen across runs | 166816 | 3 | 1 | 0x12c0c0101ee0–0x12c0c0101f5c | 0x12c0c0101f5c | 0x4764a3af384f3d92 |
| Early enqueue sustained refill | 178882 | 5 | 1 | 0x12c0c00ee6e0–0x12c0c00ee75c | 0x12c0c00ee75c | 0x43924124fff53dc6 |

All three report VEC UB out-of-bounds before scheduler timeout. All three failure records identify epoch=1, generation=1, dispatch_id=1, pipeline_slot=0, cid=0. These identify failed completion boundaries; they are not a device-side trace of the arguments actually read.

The PC equals the end of the 124-byte child in each failure's own publication. This closes the prior cross-run address-mapping gap. It does **not** establish fall-through, stale instruction cache, corrupted device bytes, or the hardware PC reporting convention.

A separately compiled worker_async_fifo/kernel_add_scalar.cpp produces 124 bytes and matching FNV-1a64 0x43924124fff53dc6. ELF and extracted text are under `disassembly/`. The installed llvm-objdump returns `<not available>` for instruction decoding; ccec rejects `-S`. No instruction-level conclusion is claimed.

`same-run-evidence.json` contains the exact host records and adjacent plog PC/error records. Two early-enqueue per-case bundles are copied into `early-fault-evidence/`; the dep-gen host records are retained in the full console, not claimed as a separately recovered original host-log file.

## Occupancy

Fault-near samples at 01:17:05.737805, 01:17:10.076812 and 01:17:23.450249 UTC show each fault PID descending from npu_lock.sh PID 153132, task-daemon PID 153081. The queue assigned our task devices 3/4/5/6; contemporaneous other tasks held 0/1 and 2. No foreign process appears on the affected die in those samples. Sampling does not exclude activity between samples.

## Validation

- New callable diagnostics UT: three cases pass.
- Production editable build: passes.
- A2/A3 simulator full sweep: exit 0, 57 scheduler groups, no FAIL groups, 196.23 s.
- A5 simulator full sweep: exit 0, 53 scheduler groups, no FAIL groups, 191.71 s.
- Onboard full sweep: exit 1, 499.43 s pytest / 502.69 s wrapper. RED: three AIV failures plus eleven halMemCtl EACCES preparation failures (one resource-case, four L2 HBG, six L2 TMR). EACCES exhausts the 2000 ms retry budget before launch, on device 3. Root cause not classified.

Do not call this a green validation or an AIV fix. Preserve the complete scene and compiled artifacts before any rebuild/retry. Follow-up should minimize the red-capable full-sweep condition, not replace this result with a warm passing repeat.

## Frozen artifacts

`frozen-failure/archive.json`: complete=true, 2624 files, no copy errors/missing inputs. All 23 runtime library hashes match the run-start manifest. The two new untracked C++ files are archived separately under `untracked-sources/`, with their own hash manifest. The final CMake-only edit moves the new test target out from between an existing comment and its target; production sources and runtime binaries remain unchanged.

## Selected-case follow-up

After freezing the red scene, the same binaries ran the three AIV cases plus the get-set-host readiness case through task-submit on devices 3/5/6: all four scheduler groups PASS, pytest 25.78 s, wrapper 29.00 s, zero VEC UB signatures. Logs and occupancy are under `isolated/`. This is a warm, selected-case run, not a full-sweep baseline and not evidence of an AIV fix or of EACCES causality. No production source or binary was changed between the runs.

## Next diagnostic boundary

The default diagnostic now answers which published child image belongs to the fault. It does not yet answer which dispatch arguments or vector state the core observed. Preserve this red workload before any minimization. The next reproduction comparison must vary one condition at a time; PC-at-end alone cannot select a device-code fix. Instruction decoding and hardware PC semantics remain unverified.
