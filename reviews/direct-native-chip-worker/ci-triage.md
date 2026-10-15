# PR #2589 initial CI triage

Run 38702273946, head 1a113d50. Logs read directly through gh; raw logs stay local.

| Check | Verdict and evidence |
| --- | --- |
| st-sim-a2a3 Ubuntu | Native object lacks wrapper _register_callable_at_slot / lease entry. Eight failed cases. |
| st-sim-a5 Ubuntu | Same object mismatch; seven failed cases, including the lease wrapper. |
| st-onboard-a2a3 | Same mismatch; eight failed TMR cases. |
| st-onboard-a5 | Same mismatch; seven failed TMR cases. |
| ut macOS | One independent failure: one of three L3 host children remained visible beyond the 20-second post-SIGKILL deadline. 3247 passed / 21 skipped; cause unclassified. |
| ut Ubuntu | Python 3257 passed; canceled during C++ compilation. No separate test failure. |
| st-sim-a2a3 macOS | Canceled while building/running descendants; no independent test verdict. |
| st-sim-a5 macOS | Canceled while building/running descendants; no independent test verdict. |

The new code changes only direct L2 construction and initialization delegation; the orphan scenario starts Worker(level=3) host children. The local Linux orphan case passes, which cannot qualify macOS behavior. Revised-head CI is the next check, not proof of a fix in this PR.

The migration covers all nine white-box files, including HBG paths that the failed CI sequence had not yet reached. Original complete CI log and canceled job logs are retained outside the published snapshot.

Updated-head checkpoint (27098ab9, 2026-10-15 08:50 UTC): all affected scene jobs pass; macOS Python UT step succeeds, Ubuntu UT job succeeds. Only macOS C++ UT remains running. The original macOS symptom is not reproduced, not declared fixed.

Final result: run 38706012077 succeeds on 27098ab9, all 17 CI jobs pass, including macOS C++ UT. The previous checkpoint is retained as chronology, not current pending work.
