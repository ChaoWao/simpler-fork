# Copyright (c) PyPTO Contributors.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------
"""A completed producer supplies the next run's native host scalar access."""

import struct
from contextlib import ExitStack
from pathlib import Path

import pytest
import torch
from simpler.buffer import AddressSpace
from simpler.task_interface import ArgDirection as D
from simpler.task_interface import CallConfig, DataType, TaskArgs, TensorArgType
from simpler.worker import Worker

from simpler_setup.scene_test import compile_chip_callable_spec, l3_compile_cache_key

_HERE = Path(__file__).resolve().parent
_SIZE = 128 * 128
_RUNTIME = "host_build_graph"


def _build_callable(platform):
    arch = "a5" if platform.startswith("a5") else "a2a3"
    kernel = _HERE.parent / arch / _RUNTIME / "vector_example/kernels/aiv/kernel_add_scalar.cpp"
    spec = {
        "orchestration": {
            "source": str(_HERE / "kernels/orchestration/host_readiness.cpp"),
            "function_name": "aicpu_orchestration_entry",
            "signature": [D.IN, D.INOUT, D.OUT, D.INOUT],
        },
        "incores": [{"func_id": 0, "source": str(kernel), "core_type": "aiv", "signature": [D.IN, D.OUT]}],
    }
    return compile_chip_callable_spec(
        spec, platform, _RUNTIME, l3_compile_cache_key(__name__, "host_readiness", "scalar", platform, _RUNTIME)
    )


def _host_tensor(worker, tensor):
    return worker.make_tensor_arg(tensor, shapes=(_SIZE,), dtype=DataType.FLOAT32)


def _args(worker, source, control, output, host_control, mode, offset=0.0):
    args = TaskArgs()
    args.add_tensor(_host_tensor(worker, source), TensorArgType.INPUT)
    args.add_tensor(control, TensorArgType.INOUT)
    args.add_tensor(_host_tensor(worker, output), TensorArgType.OUTPUT_EXISTING)
    args.add_tensor(
        worker.make_tensor_arg(host_control, shapes=(_SIZE,), dtype=DataType.FLOAT32, memory_kind=AddressSpace.HOST),
        TensorArgType.INOUT,
    )
    args.add_scalar(mode)
    args.add_scalar(struct.unpack("<I", struct.pack("<f", offset))[0])
    return args


@pytest.mark.platforms(["a2a3", "a5", "a2a3sim", "a5sim"])
@pytest.mark.device_count(1)
@pytest.mark.runtime(_RUNTIME)
@pytest.mark.parametrize("storage", ["host", "child"])
@pytest.mark.parametrize("write_control", [False, True], ids=["get", "get-set"])
def test_completed_producer_supplies_native_host_access(st_platform, st_device_ids, storage, write_control):
    with ExitStack() as cleanup:
        worker = Worker(level=2, platform=st_platform, runtime=_RUNTIME, device_id=int(st_device_ids[0]))
        cleanup.callback(worker.close)
        handle = worker.register(_build_callable(st_platform))
        worker.init()
        source = torch.full((_SIZE,), 2.0)
        control = torch.full((_SIZE,), -91.0)
        output = torch.zeros(_SIZE)
        host_control = torch.empty(_SIZE)
        device = None
        if storage == "child":
            device = worker.malloc(control.nbytes)
            cleanup.callback(worker.free, device)
            worker.copy_to(device, control)
            control_arg = device.tensor((_SIZE,), DataType.FLOAT32)
        else:
            control_arg = _host_tensor(worker, control)

        for offset in (5.0, 11.0, -4.0):
            output.zero_()
            produced = 2.0 + offset
            producer = worker.submit(
                handle, _args(worker, source, control_arg, output, host_control, 0, offset), CallConfig()
            )
            producer.wait(30.0)
            if storage == "host":
                torch.testing.assert_close(control, torch.full_like(control, produced))

            # The completed producer is copied explicitly into a separate HOST argument.
            if device is not None:
                worker.copy_from(host_control, device)
            else:
                host_control.copy_(control)
            mode = 2 if write_control else 1
            consumer = worker.submit(
                handle, _args(worker, source, control_arg, output, host_control, mode), CallConfig()
            )
            consumer.result(30.0)
            expected = torch.full_like(output, 2 * produced + 3 if write_control else 2 + produced)
            torch.testing.assert_close(output, expected)
            if device is not None:
                worker.copy_from(control, device)
            assert control[0].item() == produced
            assert host_control[0].item() == produced + (3 if write_control else 0)


@pytest.mark.platforms(["a2a3", "a5", "a2a3sim", "a5sim"])
@pytest.mark.device_count(1)
@pytest.mark.runtime(_RUNTIME)
def test_l3_host_and_device_arguments_remain_independent(st_platform, st_device_ids):
    with ExitStack() as cleanup:
        worker = Worker(level=3, platform=st_platform, runtime=_RUNTIME, device_ids=[int(st_device_ids[0])])
        cleanup.callback(worker.close)
        handle = worker.register(_build_callable(st_platform))
        worker.init()
        kinds = [AddressSpace.HOST_TO_DEVICE] * 3 + [AddressSpace.HOST]
        buffers = [worker.create_buffer(_SIZE * 4, memory_kind=kind) for kind in kinds]
        views = [torch.frombuffer(buffer.shm.buf, dtype=torch.float32) for buffer in buffers]
        try:
            for index, value in enumerate((2.0, 7.0, -1.0, 5.0)):
                views[index].fill_(value)
            args = TaskArgs()
            for buffer, tag in zip(
                buffers,
                (TensorArgType.INPUT, TensorArgType.INOUT, TensorArgType.OUTPUT_EXISTING, TensorArgType.INOUT),
                strict=True,
            ):
                args.add_tensor(buffer.tensor((_SIZE,), DataType.FLOAT32), tag)
            args.add_scalar(2)
            args.add_scalar(0)

            def graph(orch, task_args, config):
                orch.submit_next_level(handle, task_args, config, worker=0)

            worker.submit(graph, args, CallConfig()).result(30.0)
            torch.testing.assert_close(views[2], torch.full_like(views[2], 15.0))
            torch.testing.assert_close(views[1], torch.full_like(views[1], 7.0))
            assert views[3][0].item() == 8.0
        finally:
            views.clear()
