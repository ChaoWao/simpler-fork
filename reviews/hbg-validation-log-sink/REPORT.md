# Host log sink test correction

PR: https://github.com/hw-native-sys/simpler/pull/2537
Branch: fix/hbg-validation-log-sink, independent of the DFX branch.
Base: e6fd8c608. Head: 61b3b8c0.

Before modification, native_run_lifecycle followed by validation[zero_block_num] fails: 1 passed, 1 failed. The old test reads only capfd after the predecessor's drain_host_log fixture binds the process to a file. The prior sink-trace experiment retained the expected INVALID_ARGS records in that file.

The fix reuses drain_host_log, preserves the exception and error-marker checks and dropped-record invariant, and deletes the stderr-only waiter. Changes: one test file, +9/-31.

After correction: lifecycle plus all eight invalid-input tests pass 9/9; the full HBG a2a3sim sweep passes 30 with 7 skipped; a5sim passes 32. Pre-commit passes. No runtime change or hardware test is claimed by this PR. Temporary records stay outside the repository.
