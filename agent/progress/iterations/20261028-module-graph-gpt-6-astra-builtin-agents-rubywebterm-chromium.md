# Module error identity and graph selection — GPT-6 Astra / built-in agents / Ruby WebTerm / Chromium

The scoped module gate passes on frozen candidate 150. Broader release validation remains active. The qualitative score for this bounded repair is 96/100; the inherited whole-browser readiness estimate remains 19/100. These judgments have no numerical weighting formula and are not web-platform pass percentages.

## What changed

Each realm and typed module URL retains its own script creation or parse error object. An importing module no longer stores a descendant's selected error as its own intrinsic failure. Once reachable scripts have been fetched, an ordered depth-first traversal selects the error for the requested root, with a fresh visited set for cycles. Previously compiled dependencies are still traversed. Network and MIME failures retain their distinct failure class and take priority over retained parse failures.

Window and module-worker paths use the same selection rules. Direct dynamic-import resolution, fetch/MIME and link failures keep their separately tested behavior. Existing evaluation-error identity and successful namespace reuse remain intact. This does not implement every module-map, origin-policy or CSS-module behavior.

## Unchanged upstream comparison

| Measure | Accepted 144 | Rejected 147 | Candidate 150 | Pinned Chromium |
| --- | ---: | ---: | ---: | ---: |
| URLs | 44 | 44 | 44 | 44 |
| Registered assertions | 320 | 320 | 320 | 320 |
| Passing assertions | 303 | 306 | 308 | 320 |
| Fully passing URLs | 34 | 36 | 38 | 44 |
| Harnesses reporting OK | 44 | 44 | 44 | 44 |

Candidate 150 gains five existing assertion identities and loses none against accepted 144, with no missing identities. Against rejected 147, it restores cyclic-root selection and fixes fetch priority: two gains and no losses.

Candidate 147 remains an unaccepted experiment: four gains accompanied one real cyclic-graph regression despite 130 passing native tests and 25 passing authored controls. Its source snapshot, frozen binary and results are retained.

## Independent controls and cleanup

The original 25 identity controls remain byte-unchanged: accepted 144 passed 13; both 147 and 150 pass 25, matching Chromium. New 42 controls pass 26 on frozen 147 and 42 on both 150 and Chromium. They cover cold and warm cyclic roots in opposite import orders, direct/reversed/deep dependency order, repeated identity, cached syntax alongside reachable HTTP failure, and valid import after failure. Window and module Worker execute the same ordinary cases.

All 19 fixture/resource files match the sealed baseline manifest. No upstream expectation changed. The complete native gate passes 132/132 tests, including the graph fixture and the independently corrected Grid parser/used-value fixture.

The 44 upstream launches and two authored fixture launches have 46 real, linked guard exit records. Runner and guard hashes stayed fixed, actual cleanup workers drained, and no recorded processes remained. Tests use the sealed pre-main capture guard and isolated profiles.

## Exact source and timing

Frozen binary SHA-256: 79874c9c16af2086cb1de93a6194276d4b17512897aeca118244beddef7bb967.

The exact 584 compiled source/resource files are retained at agent/progress/runtime/private-prepared/source-snapshots/iteration-150. Build-reported source-content digest: c31fd602a7c4a99ea976c7a9bccab8f8e79f26a90ab6382b4fa4d3fd57a8cfdd. Build and startup passed with unchanged sources.

Native testing ran 12:02:46–12:04:01 UTC on 2026-10-28: 75.1378 seconds monitored controller time and 74.87 seconds CTest time. The one-second monitor recorded 75 samples, peak CPU 173% of one core, peak RSS 314.581 MiB and peak PSS 178.139 MiB. Concurrent comparisons were active; these are resource observations, not a controlled performance benchmark.

The first durable graph patch through final module cleanup spans 8.3049 minutes. Earlier offline diagnosis is not separately timed; overlapping agent intervals must not be added to task elapsed time. Exact timing/provenance is in the private iteration149-150-module.json.

## Remaining limits and combined-candidate boundary

The remaining 12 upstream assertions are retained: four origin-policy cases, one error source-location case, four CSS serialization cases, one CSS import-filtering case, and two CSS NUL/UTF-8 selector cases. They are not waived or relabeled.

Candidate 150 also includes reviewed Typed OM and used-track changes. It precisely removes unaccepted 146 auto-placement after broader tests exposed 21 baseline-layout losses. That experiment's 64 focused gains are not shipped or claimed as final gains. Its source, tests and failed results remain private evidence. Primary, expanded, rendering and controlled performance results are separate gates.

Evidence root: agent/validation/linux-wpt-20261028/session2-module-errors. Key records: comparison144-150.json, comparison147-150.json, candidate150-cleanup.json, native150/completion.json, iteration147-unaccepted.json and iteration149-150-module.json.
