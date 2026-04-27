# Copyright (c) PyPTO Contributors.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------
"""End-user profiling and debug CLIs shipped with the wheel.

Invoke via ``python -m simpler_setup.tools.<name>``:

- ``swimlane_converter``   : perf JSON -> Perfetto/Chrome trace
- ``sched_overhead_analysis``: scheduler overhead deep-dive
- ``perf_to_mermaid``       : perf JSON -> Mermaid flowchart
- ``dump_viewer``           : inspect tensor dumps
- ``device_log_resolver``   : shared library used by the converters
"""

import os
from pathlib import Path


def get_outputs_dir() -> Path:
    """Directory where the runtime writes ``l2_perf_records_*.json`` etc.

    Honors ``SIMPLER_OUTPUT_DIR`` — the same env var the C++ runtime and the
    parallel test dispatcher use to scope each subprocess to its own directory.
    Empty/unset falls back to ``./outputs`` under CWD.
    """
    env = os.environ.get("SIMPLER_OUTPUT_DIR")
    if env:
        return Path(env)
    return Path.cwd() / "outputs"
