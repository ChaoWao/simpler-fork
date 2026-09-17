#!/usr/bin/env python3
# Copyright (c) PyPTO Contributors.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------
"""Repeated consumption of one host_build_graph preparation result.

Every ordinary ``Worker.run`` on this runtime already builds a ``PreparedCall``
and publishes it once. These arms hold one through a *second* execution and check
the two properties that make it worth having:

* the orchestration entry is not called again — asserted on the runtime's own
  ``host_orchestration_entries`` counter, not inferred from timing;
* the second execution still produces the right answer, which means the working
  state the first one mutated (scheduler slots, task progress, Graph execution
  storage, completion counters) was restored rather than inherited.

The payload is the ``graph_execution`` callable on purpose: it submits a DAG with
intermediate storage and two outer Graph tasks that share Definitions, so a
republication that forgot to restore ``graph_context`` or the task table produces
a wrong number rather than passing vacuously.

Reuse is requested through ``_prepared_call_register``, a narrow internal seam.
It is deliberately not something an ordinary call can reach: reusing a
preparation result reuses the decisions host orchestration made from the control
data it read, so it has to be asked for.
"""

from __future__ import annotations

import pytest
import torch
from simpler.task_interface import ArgDirection as D

from simpler_setup import SceneTestCase, TaskArgsBuilder, TensorArg, scene_test
from simpler_setup.scene_test import _build_chip_task_args, _compare_outputs

# Away from 0, which the framework's own inherited test_run holds for the rest of
# the class however the arms are ordered.
_ARM_CALLABLE_ID = 1
_REGISTER = 1
_SHAPE = (128 * 128,)
# Four times the bytes, so a run at this shape grows the runner's retained
# temporary buffer and moves the base every argument slice was cut from, and
# gives its intermediates a different Definition key. The kernels write one
# 128x128 tile whatever the tensor says, so a run at this shape has no golden of
# its own — it is here to churn the buffers A's canonical image names by offset,
# not to compute anything.
_WIDE_SHAPE = (512 * 128,)


class PreparedCallReuseChecks:
    """Arm bodies shared by the two architectures' concrete classes.

    Not a ``SceneTestCase`` and carries no ``@scene_test``: each arch's module
    subclasses it alongside ``SceneTestCase``, so the arms are collected once per
    architecture against that arch's own platform list.
    """

    RTOL = 1e-5
    ATOL = 1e-5

    CALLABLE = {
        "orchestration": {
            "source": "../graph_execution/kernels/orchestration/graph_execution_orch.cpp",
            "function_name": "aicpu_orchestration_entry",
            "signature": [D.IN, D.IN, D.OUT, D.OUT, D.OUT],
        },
        "incores": [
            {
                "func_id": 0,
                "source": "../vector_example/kernels/aiv/kernel_add.cpp",
                "core_type": "aiv",
                "signature": [D.IN, D.IN, D.OUT],
            },
            {
                "func_id": 1,
                "source": "../vector_example/kernels/aiv/kernel_add_scalar.cpp",
                "core_type": "aiv",
                "signature": [D.IN, D.OUT],
            },
            {
                "func_id": 2,
                "source": "../vector_example/kernels/aiv/kernel_mul.cpp",
                "core_type": "aiv",
                "signature": [D.IN, D.IN, D.OUT],
            },
        ],
    }

    def generate_args(self, params):
        shape = params["shape"]
        a, b = params.get("a", 2.0), params.get("b", 3.0)
        return TaskArgsBuilder(
            TensorArg("a", torch.full(shape, a, dtype=torch.float32)),
            TensorArg("b", torch.full(shape, b, dtype=torch.float32)),
            TensorArg("output_1", torch.zeros(shape, dtype=torch.float32)),
            TensorArg("output_3", torch.zeros(shape, dtype=torch.float32)),
            TensorArg("output_5", torch.zeros(shape, dtype=torch.float32)),
        )

    def compute_golden(self, args, params):
        base = args.a + args.b
        args.output_1[:] = (base + 1.0) * (base + 2.0)
        args.output_3[:] = (base + 100.0) * (base + 4.0)
        args.output_5[:] = (base + 5.0) * (base + 6.0)

    # ------------------------------------------------------------------
    # harness
    # ------------------------------------------------------------------

    @pytest.fixture
    def arm(self, st_platform, st_worker):
        """A registered callable, a plain config, and zeroed counters.

        The counters are process-wide, so every arm resets them here and asserts
        on a delta rather than on an absolute value.
        """
        chip_worker = st_worker._chip_worker
        assert chip_worker is not None
        callable_obj = self.build_callable(st_platform)
        chip_worker._register_callable_at_slot(_ARM_CALLABLE_ID, callable_obj)
        assert chip_worker._impl._reset_prepared_call_metrics() == 0
        arm = _Arm(self, chip_worker, st_platform, self._build_config({}))
        try:
            yield arm
        finally:
            chip_worker._impl._prepared_call_register = 0
            chip_worker._impl._release_prepared_call(_REGISTER)
            chip_worker._unregister_slot(_ARM_CALLABLE_ID)

    # ------------------------------------------------------------------
    # arms
    # ------------------------------------------------------------------

    def test_second_execution_does_not_reorchestrate(self, arm):
        """A, then a structurally different B, then A again from the same result.

        B runs without a register, so it rebuilds everything the runner keeps per
        slot — the shared-memory mirror, the Definition staging, the retained
        temporary buffer and the caller descriptor containers — in between A's two
        executions. A's second execution must therefore be served entirely from
        data A owns.

        The counter is the load-bearing assertion. Goldens alone cannot tell reuse
        from a silent re-preparation, which is the failure mode a timing-based
        check would also miss.
        """
        arm.run({"shape": _SHAPE}, register=_REGISTER)
        arm.run({"shape": _WIDE_SHAPE, "a": 4.0}, register=0, golden=False)
        arm.run({"shape": _SHAPE}, register=_REGISTER)

        metrics = arm.metrics()
        assert metrics["host_orchestration_entries"] == 2, (
            f"A, B and a reused A must enter host orchestration twice, not "
            f"{metrics['host_orchestration_entries']} times"
        )
        assert metrics["definition_packs"] == 2, (
            f"the reused execution must not rebuild its Graph Definitions, saw {metrics['definition_packs']} packs"
        )
        assert metrics["publications"] == 3, "every execution publishes, reused or not"
        assert metrics["reused_publications"] == 1
        assert metrics["last_restored_device_bytes"] > 0

    def test_reuse_after_the_buffers_it_was_built_from_moved(self, arm):
        """A survives two wider runs that grow the buffers its slices were cut from.

        The wider runs replace the retained temporary buffer with a larger
        allocation at a different base, and grow the Definition staging and device
        block. A's canonical image names both by offset, so its second execution
        has to rebind — a retained pointer would read a freed allocation, and a
        second rebase of an already-rebound image would land outside the heap.
        """
        arm.run({"shape": _SHAPE}, register=_REGISTER)
        arm.run({"shape": _WIDE_SHAPE}, register=0, golden=False)
        arm.run({"shape": _WIDE_SHAPE, "b": 5.0}, register=0, golden=False)
        arm.run({"shape": _SHAPE}, register=_REGISTER)

        metrics = arm.metrics()
        assert metrics["host_orchestration_entries"] == 3, (
            "only the two wider runs may orchestrate on top of A's own preparation"
        )
        assert metrics["reused_publications"] == 1

    def test_input_contents_are_per_submission(self, arm):
        """One result, three executions, three different sets of input data.

        Separates the two kinds of input: the values host orchestration read to
        shape the graph are preparation inputs and are reused, while ordinary
        tensor contents belong to the submission. Each execution golden-checks
        against its own inputs, so a republication that reused the first
        submission's copied-in bytes — or skipped a copy-back — fails here.
        """
        arm.run({"shape": _SHAPE, "a": 2.0, "b": 3.0}, register=_REGISTER)
        arm.run({"shape": _SHAPE, "a": 7.0, "b": 11.0}, register=_REGISTER)
        arm.run({"shape": _SHAPE, "a": -1.5, "b": 0.25}, register=_REGISTER)

        metrics = arm.metrics()
        assert metrics["host_orchestration_entries"] == 1, "three submissions of one graph orchestrate once"
        assert metrics["reused_publications"] == 2

    def test_a_result_that_does_not_fit_is_replaced_not_reused(self, arm):
        """A submission the retained result cannot describe re-prepares instead.

        Asking for a register is a hint, never a promise: a changed argument shape
        makes the retained relocations wrong, so the bind orchestrates fresh and
        reseals. The counter advancing is what makes that visible — what this
        rules out is a cache that quietly publishes an obsolete graph, which would
        surface only as a wrong number.
        """
        arm.run({"shape": _SHAPE}, register=_REGISTER)
        arm.run({"shape": _WIDE_SHAPE}, register=_REGISTER, golden=False)
        arm.run({"shape": _WIDE_SHAPE, "a": 6.0}, register=_REGISTER, golden=False)

        metrics = arm.metrics()
        assert metrics["host_orchestration_entries"] == 2, (
            "the changed shape must re-prepare once, and the run after it must reuse that result"
        )
        assert metrics["reused_publications"] == 1

    def test_a_refused_publication_leaves_the_retained_one_usable(self, arm):
        """A result may not be published for a callable it was not built from.

        A retained image names its kernels by func_id, and the address behind a
        func_id is replayed per run from whichever callable that run binds — so
        publishing A's image under another callable's registration would execute
        whatever that callable put in the table. The bind refuses it.

        The refusal happens after the run has already copied its inputs in and
        overwritten the runner's shared scratch, which is what makes the third
        execution worth checking: A must still publish from its own retained data.
        """
        arm.run({"shape": _SHAPE}, register=_REGISTER)

        other_id = _ARM_CALLABLE_ID + 1
        # The same source, registered again: a separate host dlopen, so a separate
        # orchestration entry address and therefore a different callable identity.
        arm.chip_worker._register_callable_at_slot(other_id, self.build_callable(arm.platform))
        try:
            with pytest.raises(RuntimeError):
                arm.run({"shape": _SHAPE}, register=_REGISTER, callable_id=other_id, golden=False)
            arm.run({"shape": _SHAPE}, register=_REGISTER)
            metrics = arm.metrics()
            assert metrics["reused_publications"] == 1, "A must still publish from its own retained data"
            assert metrics["host_orchestration_entries"] == 1, (
                "the refused bind must not have orchestrated, and A must not have re-prepared"
            )
        finally:
            arm.chip_worker._unregister_slot(other_id)

    def test_release_and_unregister_drop_what_a_register_holds(self, arm):
        """Retained host bytes are owned, reported, and released on demand.

        Both release paths matter: an explicit one for a caller that is done with
        a result, and callable unregistration, which frees the code a retained
        image dispatches and must therefore not leave a result behind naming it.
        """
        arm.run({"shape": _SHAPE}, register=_REGISTER)
        assert arm.metrics()["retained_host_bytes"] > 0, "a sealed result must report the host bytes it holds"

        assert arm.chip_worker._impl._release_prepared_call(_REGISTER) == 0
        assert arm.metrics()["retained_host_bytes"] == 0

        arm.run({"shape": _SHAPE}, register=_REGISTER)
        assert arm.metrics()["retained_host_bytes"] > 0
        arm.chip_worker._unregister_slot(_ARM_CALLABLE_ID)
        assert arm.metrics()["retained_host_bytes"] == 0, (
            "unregistering a callable frees the code a retained image dispatches, so the register must not outlive it"
        )
        # Put the callable back, so the fixture's own teardown stays symmetric.
        arm.chip_worker._register_callable_at_slot(_ARM_CALLABLE_ID, self.build_callable(arm.platform))

    def test_an_impossible_register_is_refused(self, arm):
        """A register outside the declared range is refused, not silently ignored."""
        with pytest.raises(RuntimeError):
            arm.chip_worker._impl._prepared_call_register = 9999
        assert arm.chip_worker._impl._release_prepared_call(0) != 0
        assert arm.chip_worker._impl._release_prepared_call(9999) != 0


class _Arm:
    """One test arm's worker, platform and default config, with the run helper."""

    def __init__(self, case, chip_worker, platform, config):
        self.case = case
        self.chip_worker = chip_worker
        self.platform = platform
        self.config = config

    def run(self, params, register, config=None, golden=True, callable_id=_ARM_CALLABLE_ID):
        """Execute once through the production consumer.

        ``register`` is this submission's prepared-call register: a non-zero one
        asks the runtime to publish the result it holds, or to seal a fresh one
        there when it holds none, or holds one this submission does not fit.

        ``golden=False`` is for the wide-shape runs, which exist to churn the
        buffers rather than to compute — see ``_WIDE_SHAPE``.
        """
        orch_sig = self.case.CALLABLE["orchestration"]["signature"]
        test_args = self.case.generate_args(params)
        chip_args, output_names = _build_chip_task_args(test_args, orch_sig)
        golden_args = test_args.clone()
        self.case.compute_golden(golden_args, params)
        self.chip_worker._impl._prepared_call_register = register
        try:
            self.chip_worker._run_slot(callable_id, chip_args, config=config or self.config)
        finally:
            self.chip_worker._impl._prepared_call_register = 0
        if golden:
            _compare_outputs(test_args, golden_args, output_names, self.case.RTOL, self.case.ATOL)

    def metrics(self):
        metrics = self.chip_worker._impl._prepared_call_metrics()
        assert metrics["publications"] > 0, (
            "host_build_graph must report prepared-call counters; all zero after a run means the runtime "
            "answered 'unsupported', i.e. the production path is not building a preparation result at all"
        )
        return metrics


@scene_test(level=2, runtime="host_build_graph")
class TestPreparedCallReuseHbgA2a3(PreparedCallReuseChecks, SceneTestCase):
    """a2a3 host_build_graph: the ordinary path plus repeated consumption."""

    CASES = [
        {
            "name": "graph_dag",
            "platforms": ["a2a3sim", "a2a3"],
            "params": {"shape": _SHAPE},
        },
    ]
