# AIV prefix reduction, 2026-10-09

## Question and fixed inputs

Can the default-DFX failure be reproduced without the full sweep? Baseline evidence is `reviews/callable-fault-dfx/frozen-failure` outside the repository. All 23 runtime libraries are checked against its start manifest before each harness runs. No production C++ source or runtime binary is changed for this investigation.

The seven-case prefix has four real tests and three inherited test_run placeholders that skip. The targets are dep-gen, depth-one control and depth-two sustained refill. Counts refer to fresh test processes, not logical submissions or kernel invocations.

The fixed-lane harness preserves dep-gen on device 3, device-result-chain on 4, two skip placeholders followed by depth-two on 5, and depth-one on 6. Each lane starts a fresh pytest process per item using the production resource child's argv. First jobs are submitted to threads in device order; OS/Popen launch order and exact timing are not guaranteed. It removes later successors and the outer pytest dispatcher, so it is a candidate reduced scenario, not an exact replay.

## Runs

| Arm | Variation | Rounds | Target test executions | AIV failures |
| --- | --- | --- | --- | --- |
| quiet-3 | Fixed lanes, no added simulator load | 3 | 9 | 0 |
| loaded-3 | Same lanes with original A2/A3sim and A5sim commands concurrently | 3 | 9 | 0 |
| native-abba | Original pytest scheduler; narrow/full/full/narrow collection, keep same seven items | 4 | 12 | 0 |
| native-combined | Narrow+KMP, narrow, full+simulator load twice | 4 | 12 | 0 |

Each fixed-lane arm has 12 passed real tests and 9 skipped placeholders. Simulator processes in the load arm are deliberately stopped after the hardware comparisons; those partial simulator runs are **load generators, not passing validation**. Command equality does not establish identical CPU scheduling, cache warmth or external machine load.

The native scheduler preserves the first four device assignments, but assigns successors by whichever device completes first. Its depth-two device can therefore differ from the original failure. The first two target cases remain mapped as in the failure. The post-collection plugin filters items only after ordinary repository collection; it records a non-secret environment allowlist.

## A measured collection side effect

Full collection sets KMP_DUPLICATE_LIB_OK=TRUE through worker example modules, while narrow collection leaves it unset. Both set TORCH_DEVICE_BACKEND_AUTOLOAD=1. This is a reproducibility difference, **not a demonstrated bug cause**. `native-combined` tests KMP alone and combines full collection with simulator load.

## Interpretation limits

No reduced arm has gone red yet. These results do not prove that removed work is necessary, nor that CPU load/collection effects cannot contribute. The original full conditions need a contemporaneous positive control before interpreting deletion results. Keep the original red archive; do not replace it with these warm passing runs.

All hardware work is submitted through task-submit; each arm has an external occupancy/ancestor sampler. Scripts, diagnostics and planning material remain outside the product repository. Existing DFX changes are still local and uncommitted.

The single-KMP arm passes. Both combined full-collection+simulator-load arms pass. Total reduced matrix: 14 rounds, 42 target test executions, zero target VEC signatures. This is not a statistically independent 42-trial reproduction-rate estimate. An unchanged full-sweep reference is running under `positive-control/`; the directory name expresses its intended role, not a red result.

## Contemporaneous full reference

The original full command with concurrent complete A2/A3sim and A5sim sweeps returns exit 0 in 465.84 s (wrapper), with no target VEC signature. Both simulator sweeps also return exit 0. All 2052 files present in the failed scene's compiled-kernel cache remain byte-identical; none are missing. Current runtime hashes also remain the original 23. Thus the small-arm greens cannot establish which removed factor caused the earlier red: the unreduced reference is green too.

## Predecessor history experiment

`history/` compares fresh processes in the sequence current/current/old/current/old/current/current/current on each of devices 4,5,6 under one task-submit lock. Each process runs depth-one control, using its own worktree and venv. Old is the existing clean detached main 97f8b6c92 control; current is the unchanged f365ae97e+DFX build. The hypothesis being tested is whether a completed preceding build changes the target's reproducibility. This is a build-history probe, not an isolated test of instruction-cache mechanics: whole prior runtime implementations differ. Hashes and source commits are recorded in `history/builds.json`. Stop a lane on any failure rather than treating a failed predecessor as a valid transition.

### Important correction to history interpretation

All 24 history processes passed (six old→current transitions). Although the two aicore_kernel.o whole-file hashes differ, their extracted .text is identical: 3668 bytes, SHA-256 48312a4f9716121e3d87124bd93591a511fac6f64b7076550a99cfe5f68736c7. Thus this does NOT test switching instruction content, only whole runtime builds. Whole-ELF hash inequality must not be substituted for executable-byte inequality. The separate text-hash inventory records this check.

## Different instruction content, completed

`instruction-history/` repeats the eight-process sequence on devices 4,5,6 under task `task_20261009_194542_61219125857`. The old build is clean `tensor-transfer-policy` at `1c095ade228344e041c993953fd6ed50d616199d`. Unlike `history/`, its 3668-byte AICore .text has SHA-256 `aae3faa9308e175d0e414eeec51c5eef1f4bd01314f1c5446856e2b886bfba27`, differing from current `48312a4f9716121e3d87124bd93591a511fac6f64b7076550a99cfe5f68736c7`. The harness checks this before execution.

All 24 processes passed, including six completed old-to-current transitions; no target VEC signature or EACCES was found. This tests one concrete predecessor image and process-level transition, not every prior device state, and does not rule out instruction-cache involvement. Occupancy was sampled externally while the task queued and ran. Full build hashes, commands, PIDs and per-process device logs are retained.

The fixed-binary reduction stage ends here. Subsequent diagnostic development changes the runtime build and must use a new evidence directory; it must not be reported as another unchanged-binary experiment.
