# DFX PR validation

PR: https://github.com/hw-native-sys/simpler/pull/2536
Head c7457fbe, base e6fd8c608. One commit; no experiment artifacts included.

Rebuilt after rebase. Three unit targets pass (11 cases); cold-path DFX-off tests were also verified before rebase. Pre-commit passes.

HBG onboard task task_20261009_202927_23491585609 passes 70, skips 7 (266.41 s pytest); task-submit reservation and external occupancy log retained. A5sim passes 32. The HBG a2a3sim sweep reproduces the known stderr-only assertion problem: 22 passed, 7 skipped, 8 failed. #2537 fixes that independently.

Integration validation temporarily applied only #2537's test-file change to the DFX worktree: full HBG a2a3sim then passed 30, skipped 7 (29.33 s). The overlay was restored in a finally block; the DFX worktree is clean, and its commit does not contain that test change. Merge #2537 first, then rebase/validate #2536 on the merged base.

This is diagnostic coverage, not an AIV fix or stable reproducer. Archive the next failure before rebuilding, keeping host/device logs, exact binaries, source revision, commands/order and occupancy samples together.
