# Copyright (c) PyPTO Contributors.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------
"""Isolated L2 worker for the prepared-call reuse tests.

The default ``st_worker`` (root conftest) is shared across L2 ST classes in a
session-scoped pool. These tests assert on process-wide prepared-call counters
and on which registers hold a result, so a shared worker would let another
class's runs move the numbers under them.

Override ``st_worker`` here as class-scope, building a fresh L2 worker that does
**not** enter ``_l2_worker_pool``. Cost: one extra init/close per test class.

Mirrors the prepared_callable conftest, for the same reason, with one deliberate
difference: ``init()`` runs inside the block that guarantees ``close()``. A
partially-failed initialization journals teardown debt that only ``close()``
retries, so releasing the device id without it would strand that debt on the
device.
"""

from __future__ import annotations

import pytest


@pytest.fixture(scope="class")
def st_worker(request, st_platform, device_pool):
    cls = request.node.cls
    if cls is None or not hasattr(cls, "_st_runtime"):
        pytest.skip("isolated st_worker requires a SceneTestCase subclass")

    runtime = cls._st_runtime

    ids = device_pool.allocate(1)
    if not ids:
        pytest.fail("no devices available for isolated L2 worker")
    dev_id = ids[0]
    try:
        from simpler.worker import Worker  # noqa: PLC0415

        w = Worker(
            level=2,
            device_id=dev_id,
            platform=st_platform,
            runtime=runtime,
        )
        try:
            w.init()
            yield w
        finally:
            w.close()
    finally:
        device_pool.release(ids)
