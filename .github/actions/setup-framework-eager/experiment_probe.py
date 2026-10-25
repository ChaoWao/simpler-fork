# Copyright (c) PyPTO Contributors.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------
"""Temporary CANN experiment: original smoke plus actual consumer mappings."""

import hashlib
import json
import os
import subprocess
import sys
import threading
from pathlib import Path

import observe_experiment
import pytest

_STOP = threading.Event()
_THREADS = []
_RECORDS = {}
_OUT = Path("outputs/framework-eager-ci/consumer-maps")


def _observe():
    while not _STOP.wait(0.1):
        for pid in observe_experiment.descendants(os.getpid()) - {os.getpid()}:
            try:
                raw = Path(f"/proc/{pid}/maps").read_text()
                selected = {
                    line.split(maxsplit=5)[5]
                    for line in raw.splitlines()
                    if len(line.split(maxsplit=5)) == 6
                    and any(
                        token in line
                        for token in (
                            "/Ascend/",
                            "/torch_npu/",
                            "_task_interface",
                            "libhost_runtime",
                            "_simpler_framework_queue",
                        )
                    )
                }
                if selected:
                    _RECORDS.setdefault(str(pid), set()).update(selected)
                    (_OUT / f"{pid}.maps").write_text(raw)
                    (_OUT / f"{pid}.cmdline").write_bytes(Path(f"/proc/{pid}/cmdline").read_bytes())
            except OSError:
                pass


def pytest_sessionstart(session):
    if session.config.getoption("--test-environment", default="default") != "torch_npu":
        return
    if not os.environ.get("TASK_DEVICE"):
        raise pytest.UsageError("The CANN experiment smoke requires the existing task-submit reservation")
    command = [
        sys.executable,
        str(Path(observe_experiment.__file__)),
        "outputs/framework-eager-ci/smoke",
        sys.executable,
        "examples/workers/l2/framework_eager/main.py",
        "--runtime",
        "tensormap_and_ringbuffer",
        "--device",
        os.environ["TASK_DEVICE"],
        "--calls",
        "1",
        "--build-dir",
        "build/experiment-framework-bridge",
    ]
    result = subprocess.run(command, env={**os.environ, "TASK_QUEUE_ENABLE": "1"}, check=False)
    if result.returncode:
        raise pytest.UsageError(
            f"Original CANN experiment smoke failed with exit {result.returncode}; retain its actual logs"
        )
    _OUT.mkdir(parents=True, exist_ok=False)
    _THREADS.append(threading.Thread(target=_observe, daemon=True))
    _THREADS[-1].start()


def pytest_sessionfinish(session, exitstatus):
    if not _THREADS:
        return
    _STOP.set()
    _THREADS[-1].join()
    identities = {}
    for paths in _RECORDS.values():
        for value in paths:
            path = Path(value)
            try:
                identities[value] = {
                    "realpath": str(path.resolve()),
                    "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                }
            except OSError as error:
                identities[value] = {"unavailable": str(error)}
    (_OUT / "identity.json").write_text(
        json.dumps(
            {
                "actual_descendant_pids": {pid: sorted(paths) for pid, paths in _RECORDS.items()},
                "mapped_files": identities,
                "pytest_exitstatus": int(exitstatus),
                "limit": "Consumer descendants only; missing samples do not prove absence.",
            },
            indent=2,
        )
        + "\n"
    )
