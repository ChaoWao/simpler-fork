#!/usr/bin/env python3
# Copyright (c) PyPTO Contributors.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------
"""a5 host_build_graph: repeated consumption of one preparation result.

The arms are the a2a3 module's, imported rather than copied — the property under
test is the runtime's, and only the platform list and the compiled kernels differ.

What a5 adds is its AICore scheduler state. Its mode verdict and task metadata are
classified once, at preparation, from the graph in the mirror; everything an
execution owns — the state allocation, this run's callable addresses, the device
shared-memory address the worker contexts carry, and the swimlane level the run's
configuration asks for — is stood up per publication. So the arms below cover
that state's initialization and retirement as well as Graph execution, and the
``graph_execution`` payload deliberately keeps the graph-execution path rather
than forcing a shape the resident scheduler happens to accept.
"""

from __future__ import annotations

from simpler_setup import SceneTestCase, scene_test
from tests.st.a2a3.host_build_graph.prepared_call_reuse.test_prepared_call_reuse import (
    _SHAPE,
    PreparedCallReuseChecks,
)


@scene_test(level=2, runtime="host_build_graph")
class TestPreparedCallReuseHbgA5(PreparedCallReuseChecks, SceneTestCase):
    """a5 host_build_graph: the ordinary path plus repeated consumption."""

    CASES = [
        {
            "name": "graph_dag",
            "platforms": ["a5sim", "a5"],
            "params": {"shape": _SHAPE},
        },
    ]
