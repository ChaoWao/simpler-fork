#!/usr/bin/env bash
# Copyright (c) PyPTO Contributors.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------
# Measure a host_build_graph case's bind path, without running it on the device.
#
# Implements the two recipes in docs/dfx/hbg-bind-measurement.md, whose flag
# combinations are each silently wrong in a different way if you assemble them by
# hand:
#
#   stats     --rounds N. Per-phase minima over the warm passes, from the
#             LOG_TIMING breakdown. This is the number to compare across
#             branches. Diagnostics are force-disabled at rounds > 1, so this
#             mode produces no per-event artifact and no swimlane.
#   swimlane  --rounds 1 plus one diagnostic flag, which is what makes
#             CallConfig.output_prefix non-empty and so gives the per-event
#             artifact a directory. One pass per rank, and a swimlane built from
#             it.
#
# Both skip the device: SIMPLER_SKIP_DEVICE_RUN returns at simpler_launch_run,
# and since #1935 the host phase records are written on that path too. A case
# whose device execution does not complete still yields its whole host picture,
# and no device time is spent waiting for it to fail.
#
# Usage:
#   tools/hbg_bind_measure.sh [-c <case>] [-e <example>] [-p <platform>]
#                             [-d <devices>] [-m stats|swimlane] [-n <rounds>]
#                             [-l <level>] [-o <logfile>]
#
# On a shared box, wrap the whole thing in one task-submit lock:
#   task-submit --device auto --device-num 2 --timeout 1800 --max-time 1800 \
#     --run "tools/hbg_bind_measure.sh -d \$TASK_DEVICE -m stats"

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

EXAMPLE="examples/a2a3/host_build_graph/deepseek_v4_flash_decode/test_deepseek_v4_flash_decode.py"
CASE="TestDeepseekV4FlashDecodeHostBuildGraph::"
PLATFORM="a2a3"
DEVICES=""
MODE="stats"
ROUNDS=5
LEVEL="3"
LOGFILE=""

usage() {
    sed -n '11,40p' "$0" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

while [ $# -gt 0 ]; do
    case "$1" in
    -c) CASE="$2"; shift 2 ;;
    -e) EXAMPLE="$2"; shift 2 ;;
    -p) PLATFORM="$2"; shift 2 ;;
    -d) DEVICES="$2"; shift 2 ;;
    -m) MODE="$2"; shift 2 ;;
    -n) ROUNDS="$2"; shift 2 ;;
    -l) LEVEL="$2"; shift 2 ;;
    -o) LOGFILE="$2"; shift 2 ;;
    -h | --help) usage 0 ;;
    *) echo "unknown option: $1" >&2; usage 1 ;;
    esac
done

case "$MODE" in
stats | swimlane) ;;
*) echo "-m must be 'stats' or 'swimlane', got '$MODE'" >&2; exit 1 ;;
esac

if [ -z "$DEVICES" ]; then
    echo "-d is required (a device list; \$TASK_DEVICE under task-submit)" >&2
    exit 1
fi

cd "$PROJECT_ROOT" || exit 1

if [ -z "${VIRTUAL_ENV:-}" ]; then
    if [ -f .venv/bin/activate ]; then
        # shellcheck disable=SC1091
        . .venv/bin/activate
    else
        echo "no .venv in $PROJECT_ROOT and no venv active; see .claude/rules/venv-isolation.md" >&2
        exit 1
    fi
fi

# The bind path is host-only, so the arch precheck is about the case's kernels
# compiling for the right silicon, not about reaching the device.
PRECHECK=".claude/skills/onboard-arch-precheck/check.sh"
if [ -x "$PRECHECK" ]; then
    "$PRECHECK" "$PLATFORM" || exit 1
fi

if [ -z "$LOGFILE" ]; then
    LOGFILE="$PROJECT_ROOT/outputs/hbg_bind_${MODE}_$(git rev-parse --short HEAD).log"
fi
mkdir -p "$(dirname "$LOGFILE")"

# Each view has its own switch (#1937), and the two modes want different ones:
# the breakdown emits the `bind phase=` lines every mode's log is parsed for,
# while the per-event pool is what the swimlane is built from. Setting only the
# breakdown -- which used to arm both -- leaves swimlane mode with no artifact and
# no warning.
export SIMPLER_HBG_BIND_BREAKDOWN_ENABLE=1
if [ "$MODE" = swimlane ]; then
    export SIMPLER_HBG_HOST_PHASE_RECORDS_ENABLE=1
fi
export SIMPLER_LOG_LEVEL="${SIMPLER_LOG_LEVEL:-TIMING}"
# Presence-based: SIMPLER_SKIP_DEVICE_RUN=0 still skips.
export SIMPLER_SKIP_DEVICE_RUN=1
# torch_npu grabs a device on import, which a host-only measurement does not want.
export TORCH_DEVICE_BACKEND_AUTOLOAD="${TORCH_DEVICE_BACKEND_AUTOLOAD:-0}"

# --skip-golden because the device never runs: the outputs a golden check would
# compare are never produced, so a case that checks them fails at validation with
# the whole bind measurement already complete in the log. dsv4 is declared
# `skip_golden` and does not notice; qwen is not, and without this it fails every
# time for a reason that has nothing to do with what is being measured.
ARGS=(-p "$PLATFORM" -d "$DEVICES" --manual only --case "$CASE" --skip-golden)
if [ -n "$LEVEL" ]; then
    # An L3 case run through the module runner's own L3 phase captures the child's
    # output, so the `bind phase=` lines never reach this log. Invoking the child
    # command directly is what keeps them.
    ARGS+=(--runtime host_build_graph --level "$LEVEL")
fi

if [ "$MODE" = stats ]; then
    ARGS+=(--rounds "$ROUNDS")
else
    # One diagnostic flag, for output_prefix. --enable-chip-swimlane raises
    # NotImplementedError at level 3 (per-chip-process filename collision), so
    # this uses the PMU collector instead.
    ARGS+=(--rounds 1 --enable-pmu 2)
    ASCEND_LOGDIR="$PROJECT_ROOT/outputs/hbg_bind_ascend"
    mkdir -p "$ASCEND_LOGDIR"
    export ASCEND_PROCESS_LOG_PATH="$ASCEND_LOGDIR"
fi

# Stamp the whole measurement environment into the log, first line.
#
# Two runs are only comparable if this line matches, and it is not enough to have
# used this script for one of them: a hand-assembled baseline compared against a
# scripted measurement is what produced a wrong number once, because the baseline
# was missing TORCH_DEVICE_BACKEND_AUTOLOAD=0 and so paid for torch_npu grabbing a
# device that the other arm did not. `diff <(head -1 a.log) <(head -1 b.log)` now
# answers "same ruler?" instead of leaving it to memory.
STAMP="mode=$MODE platform=$PLATFORM devices=$DEVICES rounds=$ROUNDS level=${LEVEL:-none}"
STAMP="$STAMP case=$CASE head=$(git rev-parse --short HEAD)"
for var in SIMPLER_HBG_BIND_BREAKDOWN_ENABLE SIMPLER_HBG_HOST_PHASE_RECORDS_ENABLE \
    SIMPLER_LOG_LEVEL SIMPLER_SKIP_DEVICE_RUN TORCH_DEVICE_BACKEND_AUTOLOAD; do
    eval "value=\${$var-<unset>}"
    STAMP="$STAMP $var=$value"
done

echo "=== $STAMP"
echo "=== log=$LOGFILE"
echo "[hbg_bind_measure] $STAMP" >"$LOGFILE"
python "$EXAMPLE" "${ARGS[@]}" >>"$LOGFILE" 2>&1
RC=$?
echo "=== run exit=$RC ($(grep -c 'bind phase=' "$LOGFILE") bind-phase lines)"
if [ "$RC" -ne 0 ]; then
    echo "--- tail of $LOGFILE"
    tail -25 "$LOGFILE"
    exit "$RC"
fi

if [ "$MODE" = stats ]; then
    # --rounds lets the parser infer the rank count, and so drop one cold pass per
    # rank rather than one in total.
    python "$SCRIPT_DIR/hbg_phase_stats.py" "$LOGFILE" --rounds "$ROUNDS"
    exit 0
fi

# The artifact lands in the case's own outputs/<case>_<ts>/; take the newest.
RECORDS=$(find outputs -name host_phase_records.jsonl -newer "$LOGFILE" -print 2>/dev/null | head -1)
if [ -z "$RECORDS" ]; then
    RECORDS=$(ls -t outputs/*/host_phase_records.jsonl 2>/dev/null | head -1)
fi
if [ -z "$RECORDS" ]; then
    echo "no host_phase_records.jsonl found; see docs/dfx/hbg-bind-measurement.md Recipe B" >&2
    exit 1
fi
SWIMLANE="$(dirname "$RECORDS")/host_swimlane.json"
python -m simpler_setup.tools.strace_timing "$LOGFILE" \
    --host-phase-records "$RECORDS" --swimlane "$SWIMLANE" >/dev/null || exit 1
echo "=== records:  $(realpath "$RECORDS")"
echo "=== swimlane: $(realpath "$SWIMLANE")"
