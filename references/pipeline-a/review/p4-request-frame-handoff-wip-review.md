> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# Request-frame handoff WIP review

Original P4 writer active; local commit 0d64e2d34 plus ongoing WIP. Recheck the delivered exact head before same-session feedback. No duplicate prompt or manager product edit.

## F1: consumption ACK precedes the child's final frame store

`release_request_frame` writes entry.frame_consumed_generation, then stores IDLE into the frame. Parent `reclaim_consumed_request_frames_locked` tests only the generation echo and immediately permits frame reuse. Concrete interleaving: child echoes old generation; parent sees echo, writes new payload/header/TASK_READY; child writes old IDLE last. The new task is erased and its run-state entry has no terminal. `_mailbox_serve_loop::serve_one_request` uses the same order. The ownership-transfer ACK must be the child's final access to the request frame (normally clear IDLE before publishing the ACK, with release/acquire ordering). Cover actual parent reuse between the two stores; testing only a finished helper misses the race.

## F2: exceptional staging publishes terminal before its final entry/frame accesses

`stage_request` except writes TASK_FAILED, then rereads the frame header and calls release_request_frame. Parent observes terminal, `release_entry_locked` clears generation and returns the associated frame; it can reuse both before the old child writes the ACK and IDLE. Settle/release the request frame before the entry's terminal publication, and make terminal the last child access to that entry on every path. Simply reversing two stores inside release_request_frame does not fix the terminal-before-release path.

## F3: stale-frame rejection writes into the unrelated current entry

On entry-generation mismatch, staged path raises then except unconditionally writes error and TASK_FAILED into entry_buf; it subsequently echoes a stale frame's generation there. The single-run path likewise echoes into the mismatched entry. A stale frame must not modify the newer run's entry. Validate identity before acquiring the right to publish error/ACK, and use a defined bounded endpoint-failure or stale-frame recovery path that cannot mutate another owner's entry. Also validate entry index before raw pointer loads in the single-run path.

Existing non-retained bank withdrawal finding remains pending until final head review. Current WIP is not a final delivery or Ready verdict. No tests/builds/POSTs.
