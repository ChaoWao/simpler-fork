以下保留独立 validator 交接时的记录；后续 manager 接受和 fork 发布状态以[当前贡献报告](REPORT.md)为准。原始日志仍只在本地。

---

# Current HBG validation receipt —7d1dae515

## Identity and disposition

- Exact candidate: `7d1dae515b4e7f3471313e15eb303d46f51e87d7`.
- Exact parent integration: `84b4653bbf935658b8aa71307f53f1c4e8719641` (parents original #2647 `80a08550b...` and main `62565feb3...`).
- Six approved files,+131/-16. Relative to reviewed81, runtime implementation is unchanged; added accurate private retirement contract and three discriminating direct retirement controls.
- Actual Claude recheck: **approve, Must0/Should0**, session `b8013bee-884a-4788-bf4b-3943f78f040f`. Both required findings closed.
- Root read the review/qualification and accepted this correction. Publication is separately assigned to a release contributor; validation performed no push/PR/GitHub action.

## Fresh independent final-head evidence

| Check | Result / source |
| --- | --- |
| Release rebuilt focused targets |4/4 targets;139passed/9existingA3skips, `qualification-7d1dae515/report.json` and per-target XML/logs |
| Release neighbors |8/8 CTest targets pass, `independent-neighbor-ctest-7d1dae515.log` |
| Final source identity |All six hashes match `LOCAL_PATH` |
| Actual Claude closure |`claude-recheck-7d1dae515.md` and raw `hbg-local-recheck-7d1dae515-result.json` |
| Source scope / style |Exact six-file diff inspected; `git diff --check` and clean detached worktree verified |

The new retirement test positively acquires a generation1 Ready execution before calling the real retirement function. FatalFailure-only, runtime-error-only and teardown-error-only arms each require Poisoned and refusal of subsequent acquire. This does not short-circuit on an already-poisoned registry. Contributor's six real production-TU predicate mutants were read in `LOCAL_PATH`; each isolated predicate removal reddens its own arm on both architectures.

## Fresh contributor final-head broad evidence, independently read

- Frozen7d C++406/406 pass,67.78s: `LOCAL_PATH`.
- Frozen7d editable product build and Python41passed/66skipped,31.93s: `LOCAL_PATH`, `LOCAL_PATH`.
- Extension stamp equals full7d SHA. Rehashed six source+29 runtime/extension files against `LOCAL_PATH`; all match. Manifest capture was post-install/while focused Python ran; no pre-test hash snapshot is invented.
- These are contributor broad results, separate from validation's own Release target run. Earlier81's133/9,406/406 and Python41/66 remain historical; their old reports are not relabeled as7d.

## Limits

Accepted correction covers submitted HBG device-restore failure propagation through cleanup to successor refusal, preserving normal replay/native/host-clean retry controls. CPU support compiles real production code; it does not establish real RTS scheduling, silicon recovery, hang recovery or full HBG capture acceptance. No new local A5 silicon or performance claim. Existing feature ownership, dependency stack, #2647 latest-head CI gap and shared-resource feedback remain separately tracked. No canonical planning document or backup was edited during product validation.
