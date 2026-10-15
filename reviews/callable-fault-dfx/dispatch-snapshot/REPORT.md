# Dispatch shutdown diagnostics, 2026-10-09

## Outcome

No stable AIV reproducer or root-cause fix. The preceding unchanged-runtime series is recorded separately in `probes/hbg-aiv-prefix-20261009`. In particular, all 24 genuinely different-instruction predecessor-history processes passed (six old-to-current transitions). These results are not proof that the earlier AIV faults are fixed.

The original three device snapshots each name logical core 24, kernel 1, COND ACK; depth-one and depth-two stop at task 1 with 1/513 complete, dep-gen at task 2 with 2/5 complete. See `original-shutdown-gaps.json`. Logical core equality does not establish physical core equality or causality. Missing physical mapping and dispatch addresses prevent a direct join with the CANN fault and host publication records.

## Diagnostic change

Both HBG architectures add a shutdown-only WARN record for each non-idle running/pending token: logical and physical core, slot, token-selected bank, payload and args addresses, function entry, source payload and register base. It reads existing AICPU state. It does not alter device ABI, add per-dispatch writes or log on the normal dispatch path. It does not dereference argument/source addresses or establish what the AICore actually fetched. Cross-thread records are not atomic snapshots.

The regression test exercises the real cold-path logger with distinct running/pending payload banks and an unreadable source address. Before the production edit, the expected dispatch records fail on both architectures. After it, both architectures pass with SIMPLER_DFX=1 and SIMPLER_DFX=0, four cases each. The no-DFX build directory was moved outside the repository after testing; its stored CMake paths are historical, not a relocatable build recipe.

## Validation

- Editable runtime rebuild: passed.
- HBG a2a3 onboard: task `task_20261009_195547_100932811694`, devices 4/5/6, 70 passed, 7 skipped, 150.13 s pytest; external occupancy sampler retained. No target VEC signature. Ordinary passing execution does not exercise the timeout-only diagnostics.
- HBG a5sim: 32 passed.
- HBG a2a3sim: 22 passed, 7 skipped, 8 failed on absent stderr log markers. This is not a green sweep.
- Validation tests alone: all eight pass. dep-gen then validation: pass.
- Minimal independent logging defect: native_run_lifecycle then validation[zero_block_num] fails both here and on clean control 97f8b6c92 without these DFX edits. `drain_host_log` permanently binds a file sink; the latter test only reads capfd. The sink trace identifies the transition and `sink-capture/host.*.log` contains all eight expected INVALID_ARGS records. Root cause is the test's sink assumption, not dropped logs or AIV. Recorded in local KNOWN_ISSUES; no unrelated fix included.

`runtime-artifacts/` retains all runtime files listed by `runtime-sha256.json`; hashes were checked unchanged after the runs. `source.patch` plus `untracked-sources/` record the local code. No commit, push or PR was made for this stage.
