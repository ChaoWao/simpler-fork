# PR #2536: previous-head CI triage

Old head: c7457fbe; Actions run 38020825252 (2026-10-10). These failures precede the update to main e0a5e9a16.

## Network: confirmed TMR VEC UB addressing fault

Job 114121890422. Test global_tload_mpirun_l3 declares runtime tensormap_and_ringbuffer. PID 3641281, device 13, physical core 31. Original CANN plog confirms VEC read/write UB out of bounds. Current PC 0x12c0c00aa794 equals the same process's uploaded child end-exclusive address: start 0x12c0c00aa6a0, 244 bytes, func_id=0, chip_hash=0xb2ee225a6c7baeeb, code_fnv1a64=0xa43e420eb83e2e4d. Run DFX ties this image to run_epoch=1 / generation=3 / dispatch_id=1 / slot=0 / cid=0.

Later scheduler timeout reports S1:running-stalled (task 0, logical core 26); finalize status -100 and lane poison are consequences. The same end-of-child PC pattern appeared in the earlier HBG failures, so evidence is no longer confined to HBG. It does not prove a shared root cause, fall-through, or instruction-cache corruption. No exact binary artifacts or fault-time occupancy were included in this CI artifact; do not claim exclusive device use or disassembly of the actual faulting upload.

Archived the downloadable network1-ci-38020825252-1 artifact under network-ci. The original host console and decisive extracts are beside this file. Earlier checkout network errors recovered and did not cause the final failure.

## General onboard + DeepSeek: AICPU bootstrap errors

Jobs 114121890527 / 114121890465 repeatedly fail on device 8 during simpler_init. Console identifies LoadAicpuOp::BootstrapDispatcher's aclrtSynchronizeStream returning 507018 before the test's run. This differs from the Network execution-time AIV failure. No device-side cause is available in these consoles; the only downloadable artifact for the run is Network's. The machine in previous investigation was unreachable from the development host. Root cause is unclassified; neither infrastructure nor the PR is exonerated by the host code alone.

## Disposition

Rebase #2536, validate the updated build, and use its new CI run to recheck all three jobs. Retain the old failures even if reruns pass. This PR adds diagnostics; it does not fix either failure class. Local KNOWN_ISSUES.md tracks both. No active stress-reproduction campaign is started by this update.
