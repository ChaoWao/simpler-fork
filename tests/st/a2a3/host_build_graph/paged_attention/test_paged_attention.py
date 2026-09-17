#!/usr/bin/env python3
# Copyright (c) PyPTO Contributors.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------
"""Paged attention — host_build_graph runtime.

Tests host_build_graph runtime with AIC+AIV mixed execution and INOUT tensors.
Templated kernels support variable tile sizes via runtime dispatch.
"""

import torch
from simpler.task_interface import ArgDirection as D

from simpler_setup import Scalar, SceneTestCase, TaskArgsBuilder, TensorArg, scene_test
from simpler_setup.goldens.paged_attention import compute_golden as _pa_compute_golden  # noqa: PLC0415
from simpler_setup.goldens.paged_attention import generate_inputs as _pa_generate_inputs  # noqa: PLC0415
from simpler_setup.scene_test import _build_chip_task_args, _compare_outputs


@scene_test(level=2, runtime="host_build_graph")
class TestPagedAttentionHostBuildGraph(SceneTestCase):
    """Paged attention with host_build_graph runtime."""

    RTOL = 1e-3
    ATOL = 1e-3

    CALLABLE = {
        "orchestration": {
            "source": "kernels/orchestration/paged_attention_orch.cpp",
            "function_name": "aicpu_orchestration_entry",
            "signature": [D.IN, D.IN, D.IN, D.IN, D.IN, D.OUT],
        },
        "incores": [
            {
                "func_id": 0,
                "name": "QK",
                "source": "kernels/aic/aic_qk_matmul.cpp",
                "core_type": "aic",
                "signature": [D.IN, D.IN, D.OUT],
            },
            {
                "func_id": 2,
                "name": "PV",
                "source": "kernels/aic/aic_pv_matmul.cpp",
                "core_type": "aic",
                "signature": [D.IN, D.IN, D.OUT],
            },
            {
                "func_id": 1,
                "name": "SF",
                "source": "kernels/aiv/aiv_softmax_prepare.cpp",
                "core_type": "aiv",
                "signature": [D.IN, D.OUT, D.OUT, D.OUT],
            },
            {
                "func_id": 3,
                "name": "UP",
                "source": "kernels/aiv/aiv_online_update.cpp",
                "core_type": "aiv",
                "signature": [D.IN, D.IN, D.IN, D.INOUT, D.INOUT, D.INOUT, D.INOUT],
            },
        ],
    }

    CASES = [
        {
            # Marked manual for host_build_graph: this batch=256 case submits
            # ~64K tasks, and host-orchestration populates the whole task graph
            # before the device schedules — nothing is reclaimed mid-orchestration,
            # so the table must hold the entire graph at once. That exceeds the
            # default. Run it explicitly with a larger
            # runtime_env.ring_task_window if needed; the GM heap needs no sizing,
            # since it is committed to the size orchestration measured.
            "name": "Case1",
            "platforms": ["a2a3"],
            "manual": True,
            "params": {
                "batch": 256,
                "num_heads": 16,
                "kv_head_num": 1,
                "head_dim": 128,
                "block_size": 128,
                "context_len": 8100,
                "max_model_len": 32768,
                "dtype": "bfloat16",
            },
        },
        {
            "name": "Case2",
            "platforms": ["a2a3"],
            "manual": True,
            "params": {
                "batch": 64,
                "num_heads": 64,
                "kv_head_num": 1,
                "head_dim": 128,
                "block_size": 64,
                "context_len": 8150,
                "max_model_len": 32768,
                "dtype": "bfloat16",
            },
        },
        {
            "name": "small1",
            "platforms": ["a2a3sim", "a2a3"],
            "params": {
                "batch": 1,
                "num_heads": 16,
                "kv_head_num": 1,
                "head_dim": 16,
                "block_size": 16,
                "context_len": 16,
                "max_model_len": 256,
                "dtype": "bfloat16",
            },
        },
        {
            # Same workload as small1 with every tensor in child memory,
            # including the context_lens and block_table this orchestration
            # reads on the host to shape the graph.
            "name": "small1_child_memory",
            "platforms": ["a2a3sim", "a2a3"],
            "params": {
                "batch": 1,
                "num_heads": 16,
                "kv_head_num": 1,
                "head_dim": 16,
                "block_size": 16,
                "context_len": 16,
                "max_model_len": 256,
                "dtype": "bfloat16",
                "child_memory": True,
            },
        },
        {
            "name": "small2",
            "platforms": ["a2a3sim", "a2a3"],
            "manual": True,
            "params": {
                "batch": 1,
                "num_heads": 16,
                "kv_head_num": 1,
                "head_dim": 16,
                "block_size": 16,
                "context_len": 64,
                "max_model_len": 256,
                "dtype": "bfloat16",
            },
        },
    ]

    def generate_args(self, params):
        inputs = _pa_generate_inputs(params)
        child_memory = params.get("child_memory", False)
        specs = []
        for name, val in inputs:
            if isinstance(val, torch.Tensor):
                specs.append(TensorArg(name, val, child_memory=child_memory))
            else:
                specs.append(Scalar(name, val))
        return TaskArgsBuilder(*specs)

    def compute_golden(self, args, params):
        tensors = {s.name: s.value for s in args.specs if isinstance(s, TensorArg)}
        _pa_compute_golden(tensors, params)
        for s in args.specs:
            if isinstance(s, TensorArg) and s.name in tensors:
                getattr(args, s.name)[:] = tensors[s.name]

    def test_a_fallback_from_a_prepared_call_still_reads_its_control_tensors(self, st_platform, st_worker):
        """A submission a retained preparation result cannot serve must still orchestrate.

        This orchestration reads ``context_lens`` and ``block_table`` through the
        bind's host-view window to decide how many blocks each batch row needs.
        Whether a retained result can serve a submission is only settled after the
        argument contract has been compared, which needs the temporary-buffer slice
        offsets the tensor loop hands out — so the window has to be built before
        that decision, not after it. Built the other way round, a fallback
        orchestrates against an empty window, every control read fails closed, and
        the graph comes out shaped for zero blocks.

        Two submissions with different geometry through one register: the first
        seals, the second cannot be served and must re-prepare. Both golden-check,
        so a fallback that lost its control reads fails here rather than passing
        vacuously.
        """
        chip_worker = st_worker._chip_worker
        assert chip_worker is not None
        callable_id = 1
        register = 1
        orch_sig = self.CALLABLE["orchestration"]["signature"]
        config = self._build_config({})
        chip_worker._register_callable_at_slot(callable_id, self.build_callable(st_platform))
        assert chip_worker._impl._reset_prepared_call_metrics() == 0

        def run(params):
            test_args = self.generate_args(params)
            chip_args, output_names = _build_chip_task_args(test_args, orch_sig)
            golden_args = test_args.clone()
            self.compute_golden(golden_args, params)
            chip_worker._impl._prepared_call_register = register
            try:
                chip_worker._run_slot(callable_id, chip_args, config=config)
            finally:
                chip_worker._impl._prepared_call_register = 0
            _compare_outputs(test_args, golden_args, output_names, self.RTOL, self.ATOL)

        base = {
            "batch": 1,
            "num_heads": 16,
            "kv_head_num": 1,
            "head_dim": 16,
            "block_size": 16,
            "max_model_len": 256,
            "dtype": "bfloat16",
        }
        try:
            run({**base, "context_len": 16})
            # A longer context gives the caches more blocks, so the argument
            # geometry differs and the retained result cannot describe it.
            run({**base, "context_len": 48})
            metrics = chip_worker._impl._prepared_call_metrics()
            assert metrics["host_orchestration_entries"] == 2, (
                f"the second submission must re-prepare, saw {metrics['host_orchestration_entries']} orchestrations"
            )
            assert metrics["reused_publications"] == 0, "neither submission can reuse the other's result"
        finally:
            chip_worker._impl._release_prepared_call(register)
            chip_worker._unregister_slot(callable_id)


if __name__ == "__main__":
    SceneTestCase.run_module(__name__)
