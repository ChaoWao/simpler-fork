# Copyright (c) PyPTO Contributors.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------
"""Direct L2 submission validates the same Buffer identities and argument grants as L3."""

import threading
from dataclasses import replace
from types import SimpleNamespace

import pytest
from simpler.buffer import AccessMode, mint_owner_instance_id, wrap_device_malloc, wrap_fork_inherited
from simpler.task_interface import CallConfig, DataType, TaskArgs, TensorArgType
from simpler.worker import Worker, _Lifecycle


@pytest.fixture
def l2():
    worker = Worker(level=2)
    calls = []
    frees = []
    impl = SimpleNamespace(_submit_chip_run_direct=lambda cid, args, cfg: calls.append(args) or object())
    worker._chip_worker = SimpleNamespace(_impl=impl, free=frees.append)
    worker._lifecycle = _Lifecycle.READY
    yield worker, calls, frees
    worker._chip_runs.clear()
    worker._chip_run_touched_identities.clear()
    worker._close_chip_import_registry()
    worker._lifecycle = _Lifecycle.CLOSED


def register_buffer(worker, buffer_id=1, *, access=AccessMode.READWRITE, owner_worker_id=0, base=0x4000):
    buffer = wrap_device_malloc(
        base, 64, worker._owner_instance_id, buffer_id, access=access, owner_worker_id=owner_worker_id
    )
    with worker._child_prov_lock:
        worker._record_device_alloc(buffer)
    return buffer


def arguments(buffer, *, tag=TensorArgType.INPUT):
    args = TaskArgs()
    args.add_tensor(buffer.tensor((4,), DataType.FLOAT32), tag)
    return args


@pytest.mark.parametrize(
    "case", ["revoked", "cached_revoked", "generation", "wrong_chip", "foreign_owner", "changed_extent"]
)
def test_invalid_device_identity_is_rejected_before_mapping_or_launch(l2, monkeypatch, case):
    worker, calls, _ = l2
    buffer = register_buffer(worker, owner_worker_id=1 if case == "wrong_chip" else 0)
    if case == "cached_revoked":
        worker._materialize_l2_args(arguments(buffer))
    if case in ("revoked", "cached_revoked"):
        with worker._child_prov_lock:
            worker._drop_device_alloc(buffer.identity)
    elif case == "generation":
        buffer = wrap_device_malloc(0x4000, 64, worker._owner_instance_id, 1, generation=2)
    elif case == "foreign_owner":
        buffer = wrap_device_malloc(0x4000, 64, mint_owner_instance_id(), 1)
    elif case == "changed_extent":
        buffer = replace(buffer, nbytes=128)
    mapped = []
    original = worker._materialize_l2_args
    monkeypatch.setattr(worker, "_materialize_l2_args", lambda args: mapped.append(True) or original(args))
    with pytest.raises(ValueError, match="not a live|does not match"):
        worker._submit_l2_locked(3, arguments(buffer), CallConfig())
    assert not mapped and not calls
    assert not worker._chip_run_touched_identities


def test_mutated_output_tag_is_rechecked_before_any_mapping(l2, monkeypatch):
    worker, calls, _ = l2
    args = arguments(wrap_fork_inherited(1, 64, worker._owner_instance_id, 99, access=AccessMode.READ))
    read_only = register_buffer(worker, access=AccessMode.READ)
    args.add_tensor(read_only.tensor((4,), DataType.FLOAT32))
    args.set_tag(1, TensorArgType.OUTPUT_EXISTING)
    mapped = []
    original = worker._materialize_l2_args
    monkeypatch.setattr(worker, "_materialize_l2_args", lambda args: mapped.append(True) or original(args))
    with pytest.raises(ValueError, match="does not grant"):
        worker._submit_l2_locked(3, args, CallConfig())
    assert not mapped and not calls


def test_mutating_the_callers_args_does_not_change_the_accepted_binding(l2, monkeypatch):
    worker, calls, _ = l2
    first = register_buffer(worker)
    second = register_buffer(worker, 2, base=0x8000)
    args = arguments(first)
    original = worker._materialize_l2_args

    def mutate_then_materialize(accepted):
        args.clear()
        args.add_tensor(second.tensor((4,), DataType.FLOAT32))
        return original(accepted)

    monkeypatch.setattr(worker, "_materialize_l2_args", mutate_then_materialize)
    handle = worker._submit_l2_locked(3, args, CallConfig())
    assert calls[0].tensor(0).data == first.base
    assert worker._chip_run_touched_identities[handle._run_id] == {first.identity}


def test_free_rechecks_after_a_submission_wins_the_reservation(l2, monkeypatch):
    worker, calls, frees = l2
    buffer = register_buffer(worker)
    checked = threading.Event()
    resume = threading.Event()
    errors = []
    original = worker._refuse_free_while_in_flight

    def pause_after_fast_check(handle):
        original(handle)
        checked.set()
        assert resume.wait(5)

    monkeypatch.setattr(worker, "_refuse_free_while_in_flight", pause_after_fast_check)

    def free():
        try:
            worker.free(buffer)
        except BaseException as error:
            errors.append(error)

    thread = threading.Thread(target=free, daemon=True)
    thread.start()
    try:
        assert checked.wait(5)
        worker._submit_l2_locked(3, arguments(buffer), CallConfig())
    finally:
        resume.set()
        thread.join(5)
    assert not thread.is_alive()
    assert len(calls) == 1 and not frees
    assert len(errors) == 1 and "in-flight L2" in str(errors[0])
    assert worker._child_alloc.get(buffer.identity) is not None


def test_binding_failure_releases_the_reservation(l2, monkeypatch):
    worker, calls, frees = l2
    buffer = register_buffer(worker)

    def fail(_args):
        raise ValueError("cannot import")

    monkeypatch.setattr(worker, "_materialize_l2_args", fail)
    with pytest.raises(ValueError, match="cannot import"):
        worker._submit_l2_locked(3, arguments(buffer), CallConfig())
    assert not calls and not worker._chip_run_touched_identities
    worker.free(buffer)
    assert frees == [buffer.base]


def test_accepted_device_buffer_stays_live_through_finalization(l2):
    worker, calls, frees = l2
    buffer = register_buffer(worker)
    handle = worker._submit_l2_locked(3, arguments(buffer), CallConfig())
    assert calls[0].tensor(0).data == buffer.base
    with pytest.raises(RuntimeError, match="in-flight L2"):
        worker.free(buffer)
    worker._finalize_run_handle(handle, handle._run_id, None)
    worker.free(buffer)
    assert frees == [buffer.base]


@pytest.mark.parametrize("second_offset, rejected", [(8, True), (16, False)])
def test_direct_l2_uses_the_shared_writable_overlap_rule(l2, second_offset, rejected):
    worker, calls, _ = l2
    buffer = register_buffer(worker)
    args = arguments(buffer, tag=TensorArgType.OUTPUT_EXISTING)
    args.add_tensor(buffer.tensor((4,), DataType.FLOAT32, byte_offset=second_offset), TensorArgType.OUTPUT_EXISTING)
    if rejected:
        with pytest.raises(ValueError, match="overlapping bytes"):
            worker._submit_l2_locked(3, args, CallConfig())
        assert not calls
    else:
        worker._submit_l2_locked(3, args, CallConfig())
        assert calls[0].tensor(1).data == buffer.base + second_offset


def borrow(worker, base=0x8000, nbytes=64, **kwargs):
    return worker.borrow_device_buffer(base, nbytes, device_id=0, **kwargs)


def test_borrowed_device_buffer_submits_and_is_never_freed(l2):
    worker, calls, frees = l2
    buffer = borrow(worker)
    args = arguments(buffer)
    handle = worker._submit_l2_locked(3, args, CallConfig())
    assert calls[0].tensor(0).data == 0x8000
    with pytest.raises(RuntimeError, match="in-flight"):
        worker.release_buffer(buffer)
    worker._finalize_run_handle(handle, handle._run_id, None)
    with pytest.raises(ValueError, match="borrowed"):
        worker.free(buffer)
    worker.release_buffer(buffer)
    assert not frees
    with pytest.raises(ValueError, match="not a live"):
        worker._submit_l2_locked(3, args, CallConfig())


@pytest.mark.parametrize("base,nbytes", [(0x8000, 64), (0x8010, 16), (0x7FF0, 32)])
def test_borrow_rejects_overlapping_registration(l2, base, nbytes):
    worker, _, _ = l2
    buffer = borrow(worker)
    with pytest.raises(ValueError, match="overlap"):
        borrow(worker, base, nbytes)
    worker.release_buffer(buffer)


def test_borrow_cannot_alias_an_owned_allocation(l2):
    worker, _, _ = l2
    register_buffer(worker, base=0x8000)
    with pytest.raises(ValueError, match="overlap"):
        borrow(worker)


@pytest.mark.parametrize(
    "kwargs",
    [
        {"device_id": 1},
        {"device_ptr": 0},
        {"nbytes": 0},
        {"device_ptr": (1 << 64) - 4},
        {"access": 99},
    ],
)
def test_borrow_rejects_invalid_contract(l2, kwargs):
    worker, _, frees = l2
    values = dict(device_ptr=0x8000, nbytes=64, device_id=0)
    values.update(kwargs)
    with pytest.raises((ValueError, TypeError)):
        worker.borrow_device_buffer(**values)
    assert not frees and not list(worker._child_alloc.values())


def test_borrow_preserves_grant_and_view_bounds(l2):
    worker, calls, _ = l2
    buffer = borrow(worker, access=AccessMode.READ)
    with pytest.raises(ValueError):
        worker._submit_l2_locked(3, arguments(buffer, tag=TensorArgType.OUTPUT_EXISTING), CallConfig())
    with pytest.raises(ValueError):
        buffer.tensor((17,), DataType.FLOAT32)
    assert not calls
    worker.release_buffer(buffer)


def test_release_and_reregister_cannot_revive_an_old_descriptor(l2):
    worker, _, frees = l2
    old = borrow(worker)
    old_args = arguments(old)
    worker._materialize_l2_args(old_args)
    worker.release_buffer(old)
    new = borrow(worker)
    assert old.identity != new.identity
    with pytest.raises(ValueError, match="not a live"):
        worker._submit_l2_locked(3, old_args, CallConfig())
    handle = worker._submit_l2_locked(3, arguments(new), CallConfig())
    worker._finalize_run_handle(handle, handle._run_id, None)
    worker.release_buffer(new)
    assert not frees


def test_borrowed_release_rechecks_a_submission_accepted_after_lookup(l2, monkeypatch):
    worker, calls, frees = l2
    buffer = borrow(worker)
    original = worker._release_borrowed_device_buffer

    def submit_then_release(value):
        worker._submit_l2_locked(3, arguments(value), CallConfig())
        return original(value)

    monkeypatch.setattr(worker, "_release_borrowed_device_buffer", submit_then_release)
    with pytest.raises(RuntimeError, match="in-flight"):
        worker.release_buffer(buffer)
    assert len(calls) == 1 and not frees
    assert worker._child_alloc.get(buffer.identity) is not None


def test_failed_borrow_registration_rolls_back_both_tables(l2, monkeypatch):
    worker, _, frees = l2
    original = worker._record_device_alloc

    def fail_after_registration(handle):
        original(handle)
        raise RuntimeError("registration interrupted")

    monkeypatch.setattr(worker, "_record_device_alloc", fail_after_registration)
    with pytest.raises(RuntimeError, match="registration interrupted"):
        borrow(worker)
    assert not list(worker._child_alloc.values())
    assert not worker._borrowed_device_buffers and not frees


def test_close_revokes_borrowed_registrations_without_free(l2):
    worker, _, frees = l2
    buffer = borrow(worker)
    worker._chip_worker._impl.workspace_report = lambda: ("disabled", {})
    worker._chip_worker._impl._close_chip_run_lane = lambda: None
    worker._chip_worker.finalize = lambda: None
    worker.close()
    assert worker._child_alloc.get(buffer.identity) is None
    assert not worker._borrowed_device_buffers and not frees


class _Index:
    def __init__(self, value):
        self._value = value

    def __index__(self):
        return self._value


def test_borrow_accepts_index_protocol_addresses(l2):
    worker, _, _ = l2
    buffer = worker.borrow_device_buffer(_Index(0x8000), _Index(64), device_id=_Index(0))
    assert buffer.base == 0x8000 and buffer.nbytes == 64
    worker.release_buffer(buffer)


@pytest.mark.parametrize("address", [32768.5, True])
def test_borrow_does_not_coerce_non_integer_addresses(l2, address):
    worker, _, _ = l2
    with pytest.raises(TypeError):
        worker.borrow_device_buffer(address, 64, device_id=0)


def test_borrowed_release_fences_a_contending_submit(l2, monkeypatch):
    worker, calls, frees = l2
    buffer = borrow(worker)
    args = arguments(buffer)
    checked = threading.Event()
    submit_attempted = threading.Event()
    resume_release = threading.Event()
    failures = []
    original_check = worker._refuse_l2_free_while_in_flight
    original_lock = worker._child_prov_worker_lock

    def pause_release(identity):
        original_check(identity)
        checked.set()
        assert resume_release.wait(5)

    def observe_lock(worker_id):
        if threading.current_thread().name == "borrow-submit":
            submit_attempted.set()
        return original_lock(worker_id)

    def release():
        try:
            worker.release_buffer(buffer)
        except BaseException as error:
            failures.append(("release", error))

    def submit():
        try:
            worker._submit_l2_locked(3, args, CallConfig())
        except BaseException as error:
            failures.append(("submit", error))

    monkeypatch.setattr(worker, "_refuse_l2_free_while_in_flight", pause_release)
    monkeypatch.setattr(worker, "_child_prov_worker_lock", observe_lock)
    releaser = threading.Thread(target=release, daemon=True)
    submitter = threading.Thread(target=submit, name="borrow-submit", daemon=True)
    releaser.start()
    try:
        assert checked.wait(5)
        submitter.start()
        assert submit_attempted.wait(5)
        assert not calls
    finally:
        resume_release.set()
        releaser.join(5)
        if submitter.ident is not None:
            submitter.join(5)
    assert not releaser.is_alive() and not submitter.is_alive()
    assert len(failures) == 1 and failures[0][0] == "submit"
    assert isinstance(failures[0][1], ValueError)
    assert "not a live" in str(failures[0][1])
    assert not calls and not frees
