# PR2677 independent review disposition

Final head154c77af6; implementation/code-test head a547efa3d; base fa6528f4e. Actual Claude CLI independently reviewed19ed, d980 and a547, then independently verified the final doc-only delta. Final verdict: approve, no remaining Must/Should. Optional Consider items are not treated as defects silently fixed.

| Finding | Accepted result / evidence |
| --- | --- |
| Unknown close still chased live device publications | stop(false), entry-boundary stop check and conditional final drain; deterministic gated finite-publication regression red on19ed, default-stop control passes, final-source green four arch/runtime combinations |
| Unknown counters masquerade as proven zero | ordinary dropped_records_complete field; false on skipped/failed/incomplete accounting; numeric lower bounds retained; no retained manifest shape change |
| Host drain loss taints clean successor | consume host-only loss after stop joins all writers; real boundary/clean-successor regression red on d980 and green after fix |
| Multi-lane retry duplicates records/payload | per-run/lane successful buffer identity bookkeeping; failed lanes remain retryable; d980 yields5records/20bytes vs expected2/8; corrected paths cover both failure orders and counters |
| Single-lane-only coverage | six two-lane parameterized cases over four arch/runtime binaries, with sums, incomplete known bounds, saturation and address reuse |
| Empty collection emits no completeness artifact | preserve existing no-record/no-manifest convention; explicitly state absence does not prove zero loss, rather than inventing an empty-artifact API |
| Documentation drift | manifest example/keys, ordinary lifecycle diagrams, A5 recovery text, base teardown contract, residency/quiesce comments, no-artifact convention and final summary matrix corrected in the product commit |
| Ordinary ScopeStats sibling recovery | static source/API report accepted and gap recorded in #2666/KNOWN_ISSUES; separate complete follow-up must include host-only partial export and consumer unknown semantics; no hardware race claimed |

## Final-head equivalence

154c77 differs from a547 only in docs/dfx/args-dump.md (+2/-1). The parent and actual Claude each independently checked the whole tracked tree. Local C++390/390, Python3525/192, axis/strict docs and A3public4+TMR/HBGfull remain attached to a547. Final154c77 docs hooks/strictbuild pass; final-head CI is still pending and will be recorded separately. The local extension is rebuilt to the final source stamp for future imports; no executed-runtime result is relabelled.

## Optional considerations retained

- Unknown close consumes the host loss counter without setting the sticky completeness flag; correctness depends on unknown close never calling reconcile and default completeness being false, both verified.
- Proven close reads then exchanges the drain counter after a stop proof and queue drain. Atomic return-value simplification is optional; no live retirement remains to race it under the documented precondition.
- Generic report comment names an ArgsDump-specific unproven close as one consumer. This is accurate but not a generic consumer taxonomy.

No ABI-version/capability protocol, new public execution API, PreparedCall, tensor movement, device-free authority, bounded interruption of RTS reads or hardware-fault reproduction is introduced. No automatic merge.
