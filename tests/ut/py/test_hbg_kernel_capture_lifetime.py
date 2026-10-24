# Copyright (c) PyPTO Contributors.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------
"""HBG graph owners outlive construction and snapshot changed caller inputs.

ChipCallable/holder construction scope ends before replay. The caller image and
Args stay alive with a legal alternate entry/scalar; replay keeps the accepted
entry/scalar. Unchanged code bytes are not a complete deep-copy falsification.

CPU cases exercise the same numerical/transcript validators as the two hardware
cases. They do not qualify RTS execution, device capture or native DSO lifetime.
"""

from __future__ import annotations

import ctypes
import gc
import hashlib
import json
import os
import subprocess
import sys
import weakref
from pathlib import Path

import pytest
from test_hbg_kernel_launch import _launch, hot_path_probe  # noqa: F401
from test_kernel_mode_c_api import CallConfig, _binaries, _load

_ROOT = Path(__file__).resolve().parents[3]
_PREFIX = "HBG_CAPTURE_LIFETIME "
_COUNT = 128 * 128
_SCALAR = 1.25
_REPLACEMENT_SCALAR = 7.75
_POISON = -999.0
# Unknown capture/drain outcomes retain every owner until the child exits.
_UNPROVEN = []
_PHASES = (
    "COMPILED",
    "REGISTER_BEGIN",
    "REGISTERED",
    "CALLER_IMAGE_REPLACED",
    "CAPTURE_BEGIN",
    "HOST_ORCH",
    "CAPTURED",
    "CALLER_ARGS_REWRITTEN",
    "CONSTRUCTION_GONE",
    "LIVE_GUARDS",
    "REPLAYS_COMPLETED",
    "MODEL_DESTROYED",
    "RELEASE_REFUSED",
    "RELEASED",
    "UNREGISTER_BEGIN",
    "DSO_UNLOADED",
    "UNREGISTERED",
    "CLOSED",
)


def _mark(phase):
    print(_PREFIX + phase, flush=True)


def _sha(data):
    return hashlib.sha256(data).hexdigest()


def _source():
    return subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=_ROOT, text=True).strip()


def _validate_outputs(outputs: list[list[float]], count: int):
    assert len(outputs) == 3, "three distinct replay results required"
    for round_index, output in enumerate(outputs):
        offset = (0, 257, 514)[round_index]
        expected = [float(index % 127 + offset) + _SCALAR for index in range(count)]
        assert list(output) == expected, f"replay {round_index} contains stale/poisoned/incorrect values"


def _validate_transcript(stdout, profile, source, test_sha):
    phases = [line[len(_PREFIX) :] for line in stdout.splitlines() if line.startswith(_PREFIX)]
    summaries = [
        json.loads(line[len("LIFETIME_RESULT ") :])
        for line in stdout.splitlines()
        if line.startswith("LIFETIME_RESULT ")
    ]
    assert phases == list(_PHASES), f"unexpected native lifetime/order transcript: {phases}"
    assert len(summaries) == 1, "one complete result required"
    summary = summaries[0]
    assert summary["profile"] == profile
    assert summary["source"] == source
    assert summary["native_source"] == source
    assert summary["test_sha"] == test_sha
    assert summary["rounds"] == 3 and summary["count"] == _COUNT
    assert summary["caller_image_replaced"] and summary["caller_args_rewritten"]
    assert summary["input_address"] and summary["output_address"]
    assert summary["input_address"] != summary["output_address"]
    for field in ("runtime_sha", "aicpu_sha", "aicore_sha", "dispatcher_sha", "host_so_sha", "aiv_sha"):
        assert len(summary[field]) == 64 and all(c in "0123456789abcdef" for c in summary[field])
    return summary


def _compile_callable(directory):
    from simpler_setup.elf_parser import extract_text_section  # noqa: PLC0415
    from simpler_setup.kernel_compiler import KernelCompiler  # noqa: PLC0415
    from simpler_setup.pto_isa import ensure_pto_isa_root  # noqa: PLC0415

    # KernelCompiler reads build_config.py; it never dlopens the resulting SO.
    # Unlike scene_test.compile_chip_callable_spec, this path caches no ChipCallable.
    source = directory / "lifetime_orchestration.cpp"
    original = Path(__file__).with_name("kernel_eager_orchestration.cpp")
    marker = directory / "native-unloaded"
    assert not marker.exists()
    source.write_text(
        '#include <cstdio>\n#include "' + original.as_posix() + '"\n'
        'extern "C" void lifetime_orchestration(const ChipTaskArgs &args) {\n'
        ' std::puts("HBG_CAPTURE_LIFETIME HOST_ORCH"); std::fflush(stdout);\n'
        " kernel_eager_orchestration(args);\n}\n"
        "__attribute__((destructor)) static void lifetime_unload() {\n"
        ' std::puts("HBG_CAPTURE_LIFETIME DSO_UNLOADED"); std::fflush(stdout);\n'
        + " if (auto *file = std::fopen("
        + json.dumps(str(marker))
        + ', "a")) {\n'
        '  std::fputs("unloaded\\n", file); std::fclose(file);\n }\n}\n'
    )
    compiler = KernelCompiler("a2a3")
    orchestration = compiler.compile_orchestration("host_build_graph", str(source), build_dir=str(directory))
    kernel = _ROOT / "examples/a2a3/tensormap_and_ringbuffer/vector_example/kernels/aiv/kernel_add_scalar.cpp"
    incore = compiler.compile_incore(
        str(kernel),
        core_type="aiv",
        pto_isa_root=ensure_pto_isa_root(),
        extra_include_dirs=compiler.get_orchestration_include_dirs("host_build_graph"),
        build_dir=str(directory),
    )
    text = extract_text_section(incore)
    original, alternate = _callable_images(orchestration, text)
    assert not marker.exists(), "canonical compilation unexpectedly loaded/unloaded the Host SO"
    return original, alternate, {"host_so_sha": _sha(orchestration), "aiv_sha": _sha(text)}


def _image_bytes(chip):
    return ctypes.string_at(chip.buffer_ptr(), chip.buffer_size())


def _callable_images(orchestration, text):
    from simpler.task_interface import ArgDirection, ChipCallable, CoreCallable  # noqa: PLC0415

    signature = [ArgDirection.IN, ArgDirection.OUT, ArgDirection.SCALAR]
    children = [(0, CoreCallable.build(signature=signature, binary=text))]
    images = [
        ChipCallable.build(signature=signature, func_name=name, binary=orchestration, children=children)
        for name in ("lifetime_orchestration", "kernel_eager_orchestration")
    ]
    original, alternate = images
    assert original.buffer_size() == alternate.buffer_size()
    assert original.binary_size == alternate.binary_size == len(orchestration)
    assert original.child_offset(0) == alternate.child_offset(0)
    assert _image_bytes(original.child(0)) == _image_bytes(alternate.child(0))
    assert [original.sig(i) for i in range(3)] == [alternate.sig(i) for i in range(3)] == signature
    return original, alternate


def _set_args(args, addresses, scalar):
    from simpler.task_interface import ChipTensor, DataType  # noqa: PLC0415

    pointer = args.__ptr__()
    args.clear()
    for address in addresses:
        args.add_tensor(ChipTensor.make(address.value, (_COUNT,), DataType.FLOAT32, child_memory=True))
    args.add_scalar(ctypes.c_float(scalar))
    assert args.scalar(0) == int.from_bytes(bytes(ctypes.c_float(scalar)), "little")
    assert args.__ptr__() == pointer
    assert args.tensor_count() == 2 and args.scalar_count() == 1
    assert [args.tensor(i).data for i in range(2)] == [address.value for address in addresses]
    assert all(args.tensor(i).shapes == (_COUNT,) and args.tensor(i).dtype == DataType.FLOAT32 for i in range(2))


def _configure(lib):
    signatures = {
        "aclInit": [ctypes.c_char_p],
        "aclFinalize": [],
        "aclrtSetDevice": [ctypes.c_int],
        "aclrtResetDevice": [ctypes.c_int],
        "aclrtCreateStream": [ctypes.POINTER(ctypes.c_void_p)],
        "aclrtDestroyStream": [ctypes.c_void_p],
        "aclrtSynchronizeStreamWithTimeout": [ctypes.c_void_p, ctypes.c_int32],
        "aclrtMalloc": [ctypes.POINTER(ctypes.c_void_p), ctypes.c_size_t, ctypes.c_int],
        "aclrtFree": [ctypes.c_void_p],
        "aclrtMallocHost": [ctypes.POINTER(ctypes.c_void_p), ctypes.c_size_t],
        "aclrtFreeHost": [ctypes.c_void_p],
        "aclrtMemcpyAsync": [
            ctypes.c_void_p,
            ctypes.c_size_t,
            ctypes.c_void_p,
            ctypes.c_size_t,
            ctypes.c_int,
            ctypes.c_void_p,
        ],
        "aclmdlRICaptureBegin": [ctypes.c_void_p, ctypes.c_int],
        "aclmdlRICaptureEnd": [ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)],
        "aclmdlRIExecuteAsync": [ctypes.c_void_p, ctypes.c_void_p],
        "aclmdlRIDestroy": [ctypes.c_void_p],
        "simpler_unregister_callable": [ctypes.c_void_p, ctypes.c_int32],
        "simpler_kernel_mode_retain_graph": [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint64)],
        "simpler_kernel_mode_release_graph": [ctypes.c_void_p, ctypes.c_uint64],
    }
    for name, arguments in signatures.items():
        function = getattr(lib, name)
        function.argtypes, function.restype = arguments, ctypes.c_int


class _Construction:
    """Owns temporary ChipCallable wrappers; mutable registration/Args owners are separate."""

    def __init__(self, chip, args):
        self.chip, self.args = chip, args


def _capture(lib, probe, owners, directory):
    from simpler.task_interface import ChipStorageTaskArgs  # noqa: PLC0415

    chip, alternate, hashes = _compile_callable(directory)
    owners["module_marker"] = directory / "native-unloaded"
    _mark("COMPILED")
    args = ChipStorageTaskArgs()
    _set_args(args, owners["device"], _SCALAR)
    image = owners["caller_image"] = bytearray(_image_bytes(chip))
    view = owners["caller_image_view"] = (ctypes.c_ubyte * len(image)).from_buffer(image)
    # ChipCallable's existing wire storage alignment is 16 bytes.
    assert ctypes.addressof(view) % 16 == 0
    construction = _Construction(chip, args)
    owners["construction"] = construction  # Retained on any unproven return.
    reference = weakref.ref(construction)
    cid = owners["cid"] = ctypes.c_int32(-1)
    _mark("REGISTER_BEGIN")
    assert lib.simpler_register_callable_v2(owners["ctx"], view, len(image), ctypes.byref(cid)) == 0
    assert not owners["module_marker"].exists()
    _mark("REGISTERED")
    replacement = _image_bytes(alternate)
    assert len(image) == len(replacement) and bytes(image) != replacement
    image[:] = replacement
    assert bytes(view) == replacement
    _mark("CALLER_IMAGE_REPLACED")
    token = owners["token"] = ctypes.c_uint64()
    model = owners["model"] = ctypes.c_void_p()
    assert lib.simpler_kernel_mode_retain_graph(owners["ctx"], ctypes.byref(token)) == 0
    assert token.value
    _mark("CAPTURE_BEGIN")
    assert lib.aclmdlRICaptureBegin(owners["stream"], 0) == 0
    assert _launch(lib, probe, owners["ctx"], cid.value, args, owners["stream"]) == 0
    assert lib.aclmdlRICaptureEnd(owners["stream"], ctypes.byref(model)) == 0
    assert model.value
    _mark("CAPTURED")
    _set_args(args, owners["device"], _REPLACEMENT_SCALAR)
    owners["rewritten_args"] = args
    _mark("CALLER_ARGS_REWRITTEN")
    del owners["construction"]
    # ChipCallable wrappers leave scope. Changed image/Args stay live so stale
    # aliases observe valid alternate values instead of freed-memory residues.
    return reference, hashes


def _live_guards(lib, owners, committed):
    assert not owners["module_marker"].exists(), "registered Host SO was unloaded while graph token is live"
    assert lib.simpler_unregister_callable(owners["ctx"], owners["cid"].value) == -1003
    assert lib.finalize_device(owners["ctx"]) == -1003
    assert lib.committed_device_memory_ctx(owners["ctx"]) == committed
    assert not owners["module_marker"].exists()


def _run(device, profile, directory):
    runtime = _ROOT / "build/lib/a2a3/onboard/host_build_graph/libhost_runtime.so"
    # Hash the image before loading and reject replacement during this child.
    runtime_sha = _sha(runtime.read_bytes())
    lib = _load("a2a3", "onboard", "host_build_graph")
    _configure(lib)
    probe = ctypes.CDLL(None)
    probe.hbg_guard_hot_path.argtypes = [ctypes.c_int]
    owners = {"lib": lib, "probe": probe, "device": [], "pinned": [], "stream": ctypes.c_void_p()}
    _UNPROVEN.append(owners)
    # The test owns the ACL environment. create_device_context is only Simpler's
    # opaque runner object; kernel_mode_init borrows the current caller context.
    assert lib.aclInit(None) == 0
    assert lib.aclrtSetDevice(device) == 0
    assert lib.aclrtCreateStream(ctypes.byref(owners["stream"])) == 0
    owners["ctx"] = lib.create_device_context()
    assert owners["ctx"]
    binaries = owners["binaries"] = _binaries("a2a3", "host_build_graph")
    config = owners["config"] = CallConfig()
    for index in range(4):
        config.runtime_env[index] = 8 if profile == "compact" else 64
        config.runtime_env[4 + index] = 1 << 20
        config.runtime_env[8 + index] = 1024
    cpu, core, dispatcher = binaries
    assert (
        lib.simpler_kernel_mode_init(
            owners["ctx"],
            device,
            cpu,
            len(cpu),
            core,
            len(core),
            dispatcher,
            len(dispatcher),
            ctypes.byref(config),
            71,
        )
        == 0
    )
    array = ctypes.c_float * _COUNT
    nbytes = ctypes.sizeof(array)
    for _ in range(2):
        address = ctypes.c_void_p()
        owners["device"].append(address)
        assert lib.aclrtMalloc(ctypes.byref(address), nbytes, 0) == 0
    for _ in range(9):
        address = ctypes.c_void_p()
        owners["pinned"].append(address)
        assert lib.aclrtMallocHost(ctypes.byref(address), nbytes) == 0
    for index in range(3):
        array.from_address(owners["pinned"][index].value)[:] = [float(i % 127 + index * 257) for i in range(_COUNT)]
        array.from_address(owners["pinned"][3 + index].value)[:] = [_POISON] * _COUNT
        array.from_address(owners["pinned"][6 + index].value)[:] = [_POISON] * _COUNT
    reference, hashes = _capture(lib, probe, owners, directory)
    gc.collect()
    assert reference() is None, "caller construction holder survived its lexical scope"
    _mark("CONSTRUCTION_GONE")
    committed = lib.committed_device_memory_ctx(owners["ctx"])
    _live_guards(lib, owners, committed)
    _mark("LIVE_GUARDS")
    source, destination = owners["device"]
    for index in range(3):
        # Each DMA reads a distinct pinned source, retained until the one final boundary.
        for target, host in ((source, owners["pinned"][index]), (destination, owners["pinned"][3 + index])):
            assert lib.aclrtMemcpyAsync(target, nbytes, host, nbytes, 1, owners["stream"]) == 0
        assert lib.aclmdlRIExecuteAsync(owners["model"], owners["stream"]) == 0
        assert lib.aclrtMemcpyAsync(owners["pinned"][6 + index], nbytes, destination, nbytes, 2, owners["stream"]) == 0
    assert lib.aclrtSynchronizeStreamWithTimeout(owners["stream"], 60000) == 0
    _validate_outputs([list(array.from_address(p.value)) for p in owners["pinned"][6:]], _COUNT)
    _mark("REPLAYS_COMPLETED")
    assert lib.aclmdlRIDestroy(owners["model"]) == 0
    _mark("MODEL_DESTROYED")
    probe.hbg_fail_owned_sync()
    assert lib.simpler_kernel_mode_release_graph(owners["ctx"], owners["token"].value) == -4328
    _live_guards(lib, owners, committed)
    _mark("RELEASE_REFUSED")
    assert lib.simpler_kernel_mode_release_graph(owners["ctx"], owners["token"].value) == 0
    assert lib.simpler_kernel_mode_release_graph(owners["ctx"], owners["token"].value) == -1004
    _mark("RELEASED")
    _mark("UNREGISTER_BEGIN")
    assert not owners["module_marker"].exists()
    assert lib.simpler_unregister_callable(owners["ctx"], owners["cid"].value) == 0
    assert owners["module_marker"].read_text() == "unloaded\n"
    _mark("UNREGISTERED")
    assert lib.finalize_device(owners["ctx"]) == 0
    assert lib.committed_device_memory_ctx(owners["ctx"]) == 0
    lib.destroy_device_context(owners["ctx"])
    for address in reversed(owners["device"]):
        assert lib.aclrtFree(address) == 0
    for address in reversed(owners["pinned"]):
        assert lib.aclrtFreeHost(address) == 0
    assert lib.aclrtDestroyStream(owners["stream"]) == 0
    assert lib.aclrtResetDevice(device) == 0
    assert lib.aclFinalize() == 0
    _UNPROVEN.remove(owners)
    _mark("CLOSED")
    import _task_interface  # noqa: PLC0415

    assert _sha(runtime.read_bytes()) == runtime_sha
    summary = dict(
        hashes,
        profile=profile,
        source=_source(),
        native_source=_task_interface.__build_commit__,
        test_sha=_sha(Path(__file__).read_bytes()),
        rounds=3,
        count=_COUNT,
        caller_image_replaced=True,
        caller_args_rewritten=True,
        input_address=source.value,
        output_address=destination.value,
        runtime_sha=runtime_sha,
        aicpu_sha=_sha(cpu),
        aicore_sha=_sha(core),
        dispatcher_sha=_sha(dispatcher),
    )
    print("LIFETIME_RESULT " + json.dumps(summary, sort_keys=True), flush=True)


@pytest.mark.requires_hardware
@pytest.mark.platforms(["a2a3"])
@pytest.mark.parametrize("profile", ["default", "compact"])
def test_hbg_capture_lifetime(request, hot_path_probe, tmp_path, profile):  # noqa: F811
    device = str(request.config.getoption("--device")).split("-")[0].split(",")[0]
    try:
        result = subprocess.run(
            [sys.executable, __file__, device, profile, str(tmp_path)],
            capture_output=True,
            text=True,
            timeout=240,
            check=False,
            env=dict(os.environ, LD_PRELOAD=str(hot_path_probe)),
        )
    except subprocess.TimeoutExpired as error:
        (tmp_path / "child.stdout").write_bytes(error.stdout or b"")
        (tmp_path / "child.stderr").write_bytes(error.stderr or b"")
        raise
    (tmp_path / "child.stdout").write_text(result.stdout)
    (tmp_path / "child.stderr").write_text(result.stderr)
    assert result.returncode == 0, result.stdout + result.stderr
    _validate_transcript(result.stdout, profile, _source(), _sha(Path(__file__).read_bytes()))


def test_replay_output_validator_rejects_stale_poison_and_single_element_errors():
    outputs = [[float(i % 127 + offset) + _SCALAR for i in range(131)] for offset in (0, 257, 514)]
    _validate_outputs(outputs, 131)
    for bad_middle in (
        outputs[0],
        [_POISON] * 131,
        [*outputs[1][:-1], outputs[1][-1] + 1],
        [value + _REPLACEMENT_SCALAR - _SCALAR for value in outputs[1]],
    ):
        with pytest.raises(AssertionError, match="replay 1"):
            _validate_outputs([outputs[0], bad_middle, outputs[2]], 131)


def test_legal_caller_image_replacement_changes_entry_without_changing_child():
    from simpler.task_interface import ChipCallable  # noqa: PLC0415

    # Structural CPU fixtures are never loaded, registered or executed.
    original, alternate = _callable_images(b"host image fixture", b"aiv image fixture")
    image = bytearray(_image_bytes(original))
    view = (ctypes.c_ubyte * len(image)).from_buffer(image)
    pointer = ctypes.addressof(view)
    assert ChipCallable.from_bytes(bytes(view)).func_name == "lifetime_orchestration"
    image[:] = _image_bytes(alternate)
    assert ctypes.addressof(view) == pointer
    actual = ChipCallable.from_bytes(bytes(view))
    assert actual.func_name == "kernel_eager_orchestration"
    assert _image_bytes(actual.child(0)) == _image_bytes(original.child(0))
    assert actual.buffer_size() == original.buffer_size()


def test_args_rewrite_keeps_descriptor_storage_and_changes_only_scalar():
    from simpler.task_interface import ChipStorageTaskArgs  # noqa: PLC0415

    # These descriptor-only addresses are never submitted or dereferenced.
    addresses = [ctypes.c_void_p(0x100000), ctypes.c_void_p(0x200000)]
    args = ChipStorageTaskArgs()
    _set_args(args, addresses, _SCALAR)
    pointer = args.__ptr__()
    before = ctypes.string_at(pointer, args.sizeof())
    _set_args(args, addresses, _REPLACEMENT_SCALAR)
    after = ctypes.string_at(pointer, args.sizeof())
    assert args.__ptr__() == pointer and before != after
    assert args.scalar(0) == int.from_bytes(bytes(ctypes.c_float(_REPLACEMENT_SCALAR)), "little")
    _set_args(args, addresses, _SCALAR)
    assert ctypes.string_at(pointer, args.sizeof()) == before


def _valid_transcript():
    summary: dict[str, object] = {
        field: "a" * 64
        for field in (
            "test_sha",
            "runtime_sha",
            "aicpu_sha",
            "aicore_sha",
            "dispatcher_sha",
            "host_so_sha",
            "aiv_sha",
        )
    }
    summary.update(
        profile="compact",
        source="b" * 40,
        native_source="b" * 40,
        rounds=3,
        count=_COUNT,
        caller_image_replaced=True,
        caller_args_rewritten=True,
        input_address=100,
        output_address=200,
    )
    lines = [_PREFIX + phase for phase in _PHASES]
    return lines, "LIFETIME_RESULT " + json.dumps(summary)


def test_lifetime_transcript_requires_native_host_call_and_module_release_order():
    lines, summary = _valid_transcript()
    _validate_transcript("\n".join([*lines, summary]), "compact", "b" * 40, "a" * 64)
    variants = [
        [*lines[:5], _PREFIX + "HOST_ORCH", *lines[5:]],
        [line for line in lines if line != _PREFIX + "HOST_ORCH"],
        [line for line in lines if line != _PREFIX + "CONSTRUCTION_GONE"],
        [line for line in lines if line != _PREFIX + "CALLER_IMAGE_REPLACED"],
        [line for line in lines if line != _PREFIX + "CALLER_ARGS_REWRITTEN"],
        [*lines[:7], _PREFIX + "DSO_UNLOADED", *lines[7:]],
        [line for line in lines if line != _PREFIX + "DSO_UNLOADED"],
    ]
    for bad in variants:
        with pytest.raises(AssertionError, match="lifetime/order"):
            _validate_transcript("\n".join([*bad, summary]), "compact", "b" * 40, "a" * 64)
    for profile, source, digest in (
        ("default", "b" * 40, "a" * 64),
        ("compact", "c" * 40, "a" * 64),
        ("compact", "b" * 40, "c" * 64),
    ):
        with pytest.raises(AssertionError):
            _validate_transcript("\n".join([*lines, summary]), profile, source, digest)
    with pytest.raises(AssertionError, match="complete result"):
        _validate_transcript("\n".join(lines), "compact", "b" * 40, "a" * 64)


if __name__ == "__main__":
    _run(int(sys.argv[1]), sys.argv[2], Path(sys.argv[3]))
