# Developer Scripts

Repo-local scripts that are **not** shipped in the wheel. They assume a full
source checkout and known repo layout.

End-user profiling / debug CLIs live in
[`simpler_setup/tools/`](../simpler_setup/tools/) and ship with the wheel —
invoke them via `python -m simpler_setup.tools.<name>`.

## benchmark_rounds.sh

Batch-run a predefined set of scene tests on hardware and report per-round
latency from host-emitted `[STRACE]` markers. The script supports both runtimes;
`tensormap_and_ringbuffer` remains the default.

```bash
# Use defaults (device 0, 100 rounds, tensormap_and_ringbuffer)
./tools/benchmark_rounds.sh

# On a shared hardware host, hold one task-submit lock for the whole HBG sweep
task-submit --device auto --device-num 1 --timeout 3600 --max-time 3600 \
  --run ".claude/skills/onboard-arch-precheck/check.sh a2a3 && \
    ./tools/benchmark_rounds.sh -p a2a3 -d \$TASK_DEVICE -n 20 -r host_build_graph"
```

`strace_timing --rounds-table` renders one column per captured marker. TMR
reports Host / Device / Effective / Orch / Sched. HBG reports Host / Device;
its orchestration runs on the host, so the TMR-only columns are omitted. The
four architecture/runtime corpus lists at the top of the script independently
control a2a3 + TMR, a2a3 + HBG, a5 + TMR, and a5 + HBG. Every corpus includes
the workloads shared by both runtimes plus its matching Qwen case:
`StressBatch16Seq3500` for TMR and `GraphExecutionBatch16Seq3500` for HBG. SPMD
paged attention is not part of the benchmark sweep.

## hbg_bind_measure.sh

Measure a `host_build_graph` case's bind path **without running it on the
device**, in either of the two shapes
[docs/dfx/hbg-bind-measurement.md](../docs/dfx/hbg-bind-measurement.md)
describes. Both modes set `SIMPLER_SKIP_DEVICE_RUN`, so a case whose device
execution does not complete still yields its whole host picture and no device
time is spent waiting for it to fail.

```bash
# Per-phase minima over the warm passes -- the numbers to compare across branches
task-submit --device auto --device-num 2 --timeout 1800 --max-time 1800 \
  --run "tools/hbg_bind_measure.sh -d \$TASK_DEVICE -m stats -n 5"

# One pass per rank plus a swimlane, for where inside host_orch the time goes
task-submit --device auto --device-num 2 --timeout 1200 --max-time 1200 \
  --run "tools/hbg_bind_measure.sh -d \$TASK_DEVICE -m swimlane"
```

The two modes are mutually exclusive by construction, which is the main reason
to use the script rather than assemble the flags: `--rounds > 1` force-disables
every diagnostic flag, so there is no output directory and hence no per-event
artifact. Many rounds give you statistics; one round gives you a timeline.

`-c` / `-e` retarget it at another case (default: dsv4 flash decode at L3), `-p`
picks the arch, `-l ''` runs a non-L3 case. `-o` names the log; by default it
lands in `outputs/hbg_bind_<mode>_<sha>.log`.

## hbg_phase_stats.py

Parses the `bind phase=` LOG_TIMING lines out of such a log and reports each
phase's min / median / max plus the control-plane total. `hbg_bind_measure.sh -m
stats` calls it; point it at any log that carries the lines.

```bash
python3 tools/hbg_phase_stats.py outputs/hbg_bind_stats_<sha>.log --rounds 5
```

It encodes the three grouping rules that are easy to get wrong by hand:
`arena_h2d` closes a pass (the segments are not contiguous, so timestamp order
does not group them), the control-plane total is summed **within** a pass before
any minimum is taken (summing per-phase minima yields a total no pass achieved
and can invert a comparison's sign), and the first pass of each rank is warm-up.

## verify_packaging.sh

Exercises all 5 install paths × 2 entry points from a fully clean state.
CI calls this directly; see [docs/python-packaging.md](../docs/python-packaging.md).
Must run from the repo root inside an activated venv.

```bash
source .venv/bin/activate
bash tools/verify_packaging.sh
```

## cann-examples/

Standalone runnable references for the CANN host-side ACL APIs. Each
subdirectory is its own minimal CMake project — build and run on a host
with `ASCEND_HOME_PATH` set.

### cann-examples/query

Host-side device-info CLI. Subcommands wrap individual clusters of CANN
APIs (`aclrtGetDeviceCount`, `aclrtGetSocName`, `aclrtGetStreamResLimit`,
`aclrtGetMemInfo`, `aclrtGetVersion`). Treat the source as a runnable
reference for "how do I ask the driver for X?".

```bash
export ASCEND_HOME_PATH=/usr/local/Ascend/ascend-toolkit/latest
cd tools/cann-examples/query
cmake -B build .
cmake --build build

./build/query              # full overview
./build/query devices      # device count and IDs
./build/query device 0     # SoC name, AIC/AIV core counts, HBM total
./build/query mem 0        # HBM free / total / used
./build/query version      # CANN runtime version
```

### cann-examples/aicpu-device-query

Runs `halGetDeviceInfo` queries from **inside an AICPU OS process** —
resolves the "used in device" HAL queries (`AICPU + OS_SCHED`,
`AICPU + PF_*`, etc.) that always fail from host code. Uploads a small
inner SO via the same dispatcher bootstrap path the production runtime
uses; results come back through GM. Documents the resolution of the
a3 AICPU 8 → 6 split and the a5 AICPU 9 → 6 split — see the tool's own
[README](./cann-examples/aicpu-device-query/README.md) for build/run
instructions and what it confirmed.

### cann-examples/aicpu-kernel-launch

The minimum end-to-end demonstration of launching a custom AICPU kernel
from a host process using the production dispatcher bootstrap path —
no sudo, no tar.gz pre-deployment. Strips out everything specific to
this repo's runtime (ringbuffer setup, tensormap encoding, ChipWorker
fork, etc.); the inner kernel writes a magic value, an echoed token, and
one `halGetDeviceInfo` result so the readback proves end-to-end
correctness. Read this first if you want to add new AICPU work to this
repo. See the tool's own
[README](./cann-examples/aicpu-kernel-launch/README.md) for the
pipeline diagram, I/O contract, and Path A vs Path B (#822) notes.

### cann-examples/aicpu-mmio-probes

AICPU-side MMIO microbenchmarks. No AICore involvement. Measures STR
DMB cost (single + burst), STR + LDR round trip, single-thread LDR COND
serialization (same core / rotating cores), and multi-thread parallel
scaling. Reproduces Phase 4 + Phase 12 of
[`docs/hardware/mmio-performance.md`](../docs/hardware/mmio-performance.md);
the multi-thread test is the one that directly refutes "polling COND
from AICPU is sequential". See the tool's own
[README](./cann-examples/aicpu-mmio-probes/README.md) for build and
expected output.

### cann-examples/aicore-notification-perf

End-to-end measurement of the **two AICore→AICPU notification paths**:
`GM + dcci` vs `COND register (MMIO Device-nGnRE)`. Runs an AICore
producer and an AICPU consumer concurrently on two streams, computes
single-event E2E latency and idle-state polling LDR rate for both
paths. Reproduces Phase 13 + Phase 14 of
[`docs/investigations/2026-06-cond-vs-gm-notification.md`](../docs/investigations/2026-06-cond-vs-gm-notification.md)
standalone — no dependency on this repo's runtime. Use as a template
when adding a new notification mechanism that needs head-to-head
comparison with the existing two. See the tool's own
[README](./cann-examples/aicore-notification-perf/README.md) for the
pipeline diagram, build steps, and expected numbers.
