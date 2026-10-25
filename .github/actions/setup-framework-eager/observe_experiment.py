# Copyright (c) PyPTO Contributors.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------
"""Run an unchanged consumer command and observe only its descendant mappings."""

import hashlib
import json
import subprocess
import sys
import time
from pathlib import Path


def descendants(root):
    parents = {}
    for entry in Path("/proc").iterdir():
        if entry.name.isdigit():
            try:
                fields = (entry / "stat").read_text().rsplit(")", 1)[1].split()
                parents[int(entry.name)] = int(fields[1])
            except (OSError, ValueError, IndexError):
                pass
    selected = {root}
    while True:
        expanded = selected | {pid for pid, parent in parents.items() if parent in selected}
        if expanded == selected:
            return selected
        selected = expanded


def main():
    destination = Path(sys.argv[1])
    command = sys.argv[2:]
    destination.mkdir(parents=True, exist_ok=False)
    records = {}
    with (destination / "stdout.log").open("w") as out, (destination / "stderr.log").open("w") as err:
        child = subprocess.Popen(command, stdout=out, stderr=err)
        while True:
            for pid in descendants(child.pid):
                try:
                    raw = Path(f"/proc/{pid}/maps").read_text()
                    paths = {line.split(maxsplit=5)[5] for line in raw.splitlines() if len(line.split(maxsplit=5)) == 6}
                    selected = {
                        path
                        for path in paths
                        if path.startswith("/")
                        and any(
                            token in path
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
                        records.setdefault(pid, set()).update(selected)
                        (destination / f"{pid}.maps").write_text(raw)
                        (destination / f"{pid}.cmdline").write_bytes(Path(f"/proc/{pid}/cmdline").read_bytes())
                except (OSError, ProcessLookupError):
                    pass
            if child.poll() is not None:
                break
            time.sleep(0.1)
    identities = {}
    for paths in records.values():
        for path in paths:
            source = Path(path)
            try:
                identities[path] = {
                    "realpath": str(source.resolve()),
                    "sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
                }
            except OSError as error:
                identities[path] = {"unavailable": str(error)}
    result = {
        "command": command,
        "root_child_pid": child.pid,
        "exit_code": child.returncode,
        "mapped_paths_by_actual_pid": {str(pid): sorted(paths) for pid, paths in records.items()},
        "mapped_file_identity": identities,
        "limit": "Sampled actual consumer descendants. A missing mapping is not proof a library was never loaded.",
    }
    (destination / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"child": child.pid, "exit_code": child.returncode, "evidence": str(destination)}), flush=True)
    return child.returncode


if __name__ == "__main__":
    raise SystemExit(main())
