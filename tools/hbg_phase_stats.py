#!/usr/bin/env python3
# Copyright (c) PyPTO Contributors.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------
"""Per-phase statistics from a `bind phase=` LOG_TIMING breakdown.

Reads the log a `--rounds N` run leaves behind and reports each bind phase's
minimum, median and maximum across the warm passes, plus the control-plane total.
Driven by `tools/hbg_bind_measure.sh -m stats`; usable on any log that carries the
lines.

Three rules from docs/dfx/hbg-bind-measurement.md are encoded here rather than
left to the reader, because each of them silently produces a wrong number:

* `arena_h2d` is the last segment of a pass, so it is what closes one. The
  segments are not contiguous in time and cannot be grouped by timestamp order.
* The control-plane total is summed **within** each pass, and the minimum taken
  over those sums. Summing per-phase minima gives a total no pass achieved, and
  it can invert the sign of a comparison.
* The first pass of each rank is warm-up and belongs in neither statistic.

Compare branches by the **minimum**. On a shared box the mean and the median
measure the other tenants as much as the change; the minimum is the closest thing
to the cost with the machine to itself. The spread is worth reading too: a change
that lowers the floor while widening the range has made the cost less
predictable, which is a real cost of its own.
"""

import argparse
import re
import sys

PHASE_LINE = re.compile(r"bind phase=(\w+) start_ns=(\d+) dur_ns=(\d+)")
# The environment stamp hbg_bind_measure.sh writes as the log's first line. Echoed
# with the table so a pasted result carries the conditions it was measured under —
# a number without them cannot be compared to another number.
STAMP_LINE = re.compile(r"^\[hbg_bind_measure\] (.*)$")

# The segments between "the caller's data is in place" and "the device can run":
# what a dispatch-path change is allowed to move. `args` and `host_view_close`
# are per-byte costs over the weights the case stages, so they belong to getting
# the weights resident, not to dispatching a graph.
CONTROL_PLANE = ("host_orch", "graph_upload", "relocate", "sm_h2d", "arena_h2d")

# Display order: the bind path's own sequence, so a reader can follow it down.
PHASE_ORDER = (
    "args",
    "arena_build",
    "static_arena",
    "gm_heap",
    "shared_mem",
    "runtime_init",
    "host_orch",
    "graph_upload",
    "relocate",
    "sm_h2d",
    "arena_h2d",
    "host_view_close",
)

# `arena_h2d` closes a pass.
PASS_CLOSING_PHASE = "arena_h2d"


def parse_passes(path: str) -> list[dict[str, float]]:
    """Group `bind phase=` lines into passes, in milliseconds."""
    passes: list[dict[str, float]] = []
    current: dict[str, float] = {}
    with open(path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            match = PHASE_LINE.search(line)
            if match is None:
                continue
            phase, _start_ns, dur_ns = match.group(1), int(match.group(2)), int(match.group(3))
            current[phase] = dur_ns / 1e6
            if phase == PASS_CLOSING_PHASE:
                passes.append(current)
                current = {}
    if current:
        passes.append(current)
    # A group without `host_orch` is not a bind pass.
    return [p for p in passes if "host_orch" in p]


def parse_stamp(path: str) -> str:
    """The run's environment stamp, or "" for a log this script did not produce."""
    with open(path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            match = STAMP_LINE.match(line.rstrip("\n"))
            if match is not None:
                return match.group(1)
    return ""


def spread(values: list[float]) -> tuple[float, float, float]:
    ordered = sorted(values)
    return ordered[0], ordered[len(ordered) // 2], ordered[-1]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("log", help="run log carrying the `bind phase=` lines")
    parser.add_argument(
        "--ranks",
        type=int,
        default=None,
        help="ranks in the run; the first pass of each is warm-up and is dropped. "
        "Default: inferred as the number of passes divided by the round count when "
        "--rounds is given, else 1.",
    )
    parser.add_argument("--rounds", type=int, default=None, help="rounds the run was given, to infer --ranks")
    parser.add_argument("--keep-first", action="store_true", help="keep the cold pass in the statistics")
    args = parser.parse_args()

    passes = parse_passes(args.log)
    if not passes:
        print(
            f"{args.log}: no `bind phase=` lines. SIMPLER_HBG_BIND_BREAKDOWN_ENABLE=1 and "
            "SIMPLER_LOG_LEVEL=TIMING must both be set, and an L3 case must be invoked as "
            "its own child command -- see docs/dfx/hbg-bind-measurement.md Recipe A.",
            file=sys.stderr,
        )
        return 1

    ranks = args.ranks
    if ranks is None:
        ranks = max(1, len(passes) // args.rounds) if args.rounds else 1
    dropped = 0 if args.keep_first else min(ranks, len(passes) - 1)
    warm = passes[dropped:]

    print(f"{args.log}")
    stamp = parse_stamp(args.log)
    if stamp:
        print(f"  {stamp}")
    else:
        print("  (no environment stamp: not produced by hbg_bind_measure.sh, so not")
        print("   comparable to a stamped run without checking the conditions by hand)")
    print(f"  {len(passes)} passes, {len(warm)} warm ({dropped} cold dropped, ranks={ranks})\n")
    print(f"  {'phase':<18}{'min':>10}{'median':>10}{'max':>10}   n")
    for phase in PHASE_ORDER:
        values = [p[phase] for p in warm if phase in p]
        if not values:
            continue
        low, mid, high = spread(values)
        mark = "*" if phase in CONTROL_PLANE else " "
        print(f" {mark}{phase:<18}{low:>10.3f}{mid:>10.3f}{high:>10.3f}  {len(values):3d}")

    unknown = sorted({k for p in warm for k in p} - set(PHASE_ORDER))
    if unknown:
        print(f"\n  phases this tool does not know about: {', '.join(unknown)}")
        print("  (add them to PHASE_ORDER, and to CONTROL_PLANE if a dispatch change can move them)")

    # A control-plane phase can be absent because the change under measurement
    # retired it -- #1932 folds the shared-memory image into the arena copy and
    # removes `relocate` and `sm_h2d` entirely. That is a real saving, so the total
    # is summed over the phases this run has and the retired ones are named. Absent
    # from *some* passes only is a different thing: a truncated log or a phase that
    # did not run, which would understate a pass, so it is called out separately.
    present = [k for k in CONTROL_PLANE if any(k in p for k in [k] for p in warm)]
    partial = [k for k in present if not all(k in p for p in warm)]
    retired = [k for k in CONTROL_PLANE if k not in present]
    complete = [p for p in warm if all(k in p for k in present)]
    if complete:
        totals = [sum(p[k] for k in present) for p in complete]
        low, mid, high = spread(totals)
        print("\n  * = control plane, summed within each pass and then:")
        print(f"    total{'':<13}{low:>10.3f}{mid:>10.3f}{high:>10.3f}  {len(totals):3d}   (ms)")
        if retired:
            print(f"    over {len(present)} of {len(CONTROL_PLANE)} phases; absent from every")
            print(f"    pass: {', '.join(retired)}")
            print("    (a phase this run does not have contributes nothing — compare totals")
            print("     across runs only after checking which set each one covers)")
        if partial:
            print(f"    WARNING: {', '.join(partial)} is missing from some passes but not all;")
            print("    those passes are excluded, and the total may not describe the run")
        print("\n  Compare branches by min. A lower min with a wider range means the cost")
        print("  got less predictable, which is a cost of its own.")
    else:
        print(f"\n  no pass carries any of {CONTROL_PLANE}; the control-plane total is not computable")
    return 0


if __name__ == "__main__":
    sys.exit(main())
