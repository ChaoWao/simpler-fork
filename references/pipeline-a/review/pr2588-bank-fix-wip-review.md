> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# PR2588 bank fix working-tree checkpoint

2026-10-15 09:03 UTC; published head remains 1d707f440, original PID186019 active. Twelve modified source files inspected; no product edits or local tests by manager. Recheck final artifact before sending new prompt.

Direction: read_run_terminal_bank uses a real memcpy on mapped platforms instead of success-only shim; PMU/ArgsDump move unproven-stop reservations off reusable epoch records into quarantine, and cancellation before launch explicitly returns them. These are WIP, not accepted fixes.

Pending verification: RunTerminalBanks::quarantine currently releases a held token when run_epoch is zero or mismatched, though inability to attribute is not proof producers stopped. Preserve unknown ownership fail-closed or prove precondition by construction. Full quarantine loop also returns without consuming on full; ensure caller cannot subsequently overwrite/release. Check close/detach/reopen, generation and foreign-token validation, and late-proof versus writer-retirement ordering. R2 real arch failure/join proof and all24 prior CTest failures remain required; bank helper changes alone do not prove coverage. Original author's pending main2579 integration remains.

## 09:07 UTC working-tree follow-up

17 modified files inspected, including five test fixtures. Mapped platform copy selection is consistent with a2a3 onboard/sim null shim; PMU flush now snapshots buffer count before ready-queue publication and increments terminal published counters. ArgsDump held-record test correctly avoids its previous pre-close flush; rollback test uses distinct file target, and lane fixture sizes generation table. These are static observations, not test passes. Swimlane fixture now retries capacity with sleep; it does not validate the production generation-based wakeup, and must not replace that coverage.

Concrete reopen edge to recheck: Reservation::held checks only pool_ != nullptr. RunTerminalBanks::detach resets credits but does not clear quarantined_ tokens; release_backing(true) can free backing, and open can allocate a new backing. Old generation tokens still appear held, occupying quarantine entries. A later unknown-stop reservation can find the array full, remain on a soon-reused epoch, then be released by move assignment. Require either enforced no-reopen lifecycle proof or cleanup of stale parked ownership after stop proof, plus coverage for quarantine → proved shutdown → reopen → new quarantine; matching epoch alone is insufficient across generations. Original writer is active; this is pending final-head recheck, not a new prompt or confirmed published-code finding.

## 09:29 UTC follow-up: writer-finished coverage and detach correction

21 modified files inspected against published 1d707; latest build log LOCAL_PATH reaches 100%, original writer continues formatting. This is build evidence only, no test results.

RunTerminalBanks::detach now resets credits and clears parked Reservations. This addresses the specific stale occupied quarantine-entry mechanism identified above under its documented single teardown owner; do not continue reporting that exact old implementation as current. The retained backing still refuses open until proved release. A quarantine → proved close → reopen → new quarantine regression is still absent from the inspected tests.

New RunTerminalStopAfterTheWriter typed cases exercise both PMU and ArgsDump after actually waiting for writer retirement (bounded 10s wait): retained bank without stop proof, late proof names only its run, and prelaunch withdrawal returns the bank. These are six added case instantiations per applicable target, not executed this round. They materially improve the writer-lifetime proof. They do not replace real architecture runner failure/join coverage: SettlementRunner still overrides prepare/launch/drain and directly injects completion/stopped booleans into the base teardown.

Remaining pending final-head checks: quarantine invalid/mismatching epoch still releases rather than quarantines; full array leaves the incoming token unmoved. The normal production callers may enforce stronger invariants, so require a source proof or fail-closed correction, rather than claiming a reproduced device corruption. No new prompt while original writer is active. Preserve main2579 integration and all eight failure-family checks.
