# Copyright (c) PyPTO Contributors.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------
"""Successors resolve completion records from every returned MIX participant."""

import json

import pytest
import torch

from tests.st.a5.host_build_graph.async_completion.helpers import (
    POISON,
    Case,
    CounterRunner,
    Scenario,
    assert_observations,
    l3_case,
    read_host_tensor,
)

pytestmark = [
    pytest.mark.platforms(["a5", "a5sim"]),
    pytest.mark.runtime("percounter"),
    pytest.mark.device_count(1),
]


def _run_and_check(runner: CounterRunner, *, reset_window: bool = True):
    runner.submit(reset_window=reset_window).wait(timeout=30)
    assert_observations(runner.case, runner.capacity_waiters, runner.records, runner.output, runner.control)
    captures = sorted(runner.capture.rglob("chip_swimlane_records.json"))
    assert len(captures) == 1, captures
    capture = json.loads(captures[0].read_text())
    assert capture["metadata"]["runtime"] == "percounter"
    assert capture["aicore_tasks"], "no kernel execution trace"
    domain = runner.worker.live_domains["async_completion_contract"]
    runner.worker.copy_from(runner.signal_staging, domain[0].buffers["signals"])
    signals = read_host_tensor(runner.signal_staging, runner.initial_signals)
    assert torch.all(signals[:, 1:] == POISON), "completion handling overwrote adjacent signal bytes"


@pytest.mark.parametrize("sync_start", [False, True], ids=["mix", "sync_mix"])
@l3_case
def test_all_mix_participants_register_completion(st_platform, st_device_ids, tmp_path, sync_start):
    case = Case(
        scenario=Scenario.NEVER,
        conditions=4,
        blocks=2,
        mask=7,
        sync_start=sync_start,
        initial_signal=1,
    )
    with CounterRunner(st_platform, st_device_ids, tmp_path, case, runtime="percounter", profiling_level=1) as runner:
        _run_and_check(runner)


@l3_case
def test_graph_invocations_resolve_child_completion(st_platform, st_device_ids, tmp_path):
    # The shared fixture's Graph body has one AIV waiter and one dependent consumer.
    case = Case(scenario=Scenario.GRAPH, conditions=4, instances=2, initial_signal=1)
    with CounterRunner(st_platform, st_device_ids, tmp_path, case, runtime="percounter", profiling_level=1) as runner:
        _run_and_check(runner)


@l3_case
def test_retained_window_reuses_mix_completion_records(st_platform, st_device_ids, tmp_path):
    case = Case(scenario=Scenario.NEVER, conditions=4, blocks=2, mask=7, initial_signal=1)
    with CounterRunner(st_platform, st_device_ids, tmp_path, case, runtime="percounter", profiling_level=1) as runner:
        for iteration in range(3):
            _run_and_check(runner, reset_window=iteration == 0)
