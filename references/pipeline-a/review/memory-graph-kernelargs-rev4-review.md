> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# HBG graph KernelArgs — revision 4 design review

Reviewed the complete 536-line proposal and delivered result against main 35e9331b5. DESIGN ACCEPTED as input to a concrete implementation approval, not implementation authorization or code approval.

The previous R1–R3 are addressed in the design:

- All three borrowed image pointers are identified. The Definition remains an offset; fanin CSR arrays become execution-owned values in the existing outer-task storage tail, instead of retaining the localizer's RTS address. Boundary argument pools and materialized task storage already have independent ownership and remain there.
- Reading uses byte copies into aligned locals, so there is no unproven RTS alignment precondition or whole-image landing allocation.
- A5 Graph's actual legacy executor and A3 consumers use their own views; sim explicitly uses a run-owned HostSnapshot. Localize failure reaches the existing graph-prepare INVALID path and fail_scheduler/emergency_shutdown. It is a run-wide error, not a claim that peers continue unaffected.

Source checks confirmed GraphExecution's borrowed fields, bind_graph_topology assignments, scheduler fanin consumers, the execution-storage sizing helper/callers, and the null-context -> INVALID -> fail_scheduler path. No source edits/tests/builds by manager.

## Controlling implementation clarifications

1. Do not call the current pointer-returning graph_definition_object_framed on a standalone copied header: it dereferences `&header + 1`. Replace the check with validation of separately decoded framing header and Definition values. No typed address into an unaligned package.
2. Check offset multiplication/addition before narrowing, and every section read against both the containing framed Definition and total package extent. A field pointing into a different valid object in the same package is still invalid.
3. CSR storage costs include alignment and layout changes, not just the two array payloads. Extend the sizing inputs with edge count and update the producer in common/host_build_graph/host/orchestrator.cpp, both bind validators and device storage acquisition together. Validate capacity before copying; do not silently extend beyond the existing outer-task allocation.
4. Byte transport moves from bind to launch; it can reduce upload overlap with the predecessor. Do not promise unchanged performance, device memory reduction, physical RTS copy count, or successful submission of every representable package. RTS allocation/API failures still follow the actual submission grade.
5. Sim's source discriminant and snapshot are explicit; it cannot accidentally fall through to a stale onboard view. The host snapshot remains pinned until its run's last consumer ends.

These are concrete implementation checks incorporated into the manager decision; another open-ended design investigation is unnecessary. Original memory author stays idle pending the user’s concrete architecture approval. No manual graph block fallback, artificial size limit or capture wiring is authorized.
