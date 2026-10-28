# Native Grid repeat iteration 3 — GPT-6 Astra / built-in agents / Ruby MCP / Chromium

Candidate 138 is not accepted and receives revised scoped engineering quality 88/100 after final independent review exposed calculated-count regressions. Its preliminary 96/100 assessment was conditional and is superseded. Whole-project readiness remains provisional 19/100. This bounded change validates fixed-size automatic repetition through the existing native track parser, rejects unsupported values before prior declarations are mutated, and shares the predicate with native CSS.supports and @supports. Complete CSS grammar or rendering parity is not claimed.

Recorded source-ready checkpoint: 2026-10-28T10:14:51.129212Z. Final unchanged upstream replay: 2026-10-28T10:20:47Z. Recorded interval: 5.9312 minutes; earlier design and baseline investigation are excluded. Structured timing, resources and hashes: agent/progress/runtime/session2-iteration3-native-repeat.json. Agents: progress_and_deployment (implementation/evidence), repository_lead (build/review/Git), standards_comparison (independent review), root (acceptance).

## Behavior and bounded validation

Native GridTrack metadata now identifies fixed-size and automatic/repeated track lists. Unsupported intrinsic auto-repeat, multiple automatic repetitions and nested repeat are rejected before generic declaration metadata or track arrays change. Invalid assignments preserve prior arrays, values and allocation ownership. Validation-only parsing avoids expanding large repeat counts; materialized storage remains bounded. Bracketed line-name groups are consumed as one syntax component. The existing public parse wrapper retains its negative-status-to-empty-output contract.

The unchanged authored 84-case browser fixture measures raw styles, CSSOM, CSS.supports and @supports: frozen 135 passes 41/84, frozen 138 passes 84/84, Chromium 150.0.7871.114 passes 84/84. Native allocation/atomicity assertions additionally cover both axes, prior generic metadata identity, shorthand preflight and bounded repetition. The ordinary intrinsic-repeat observation now preserves the earlier 25px value through both CSSOM and raw styles. It is a fallback observation, not a Grid3 rendering pass.

Full native CTest: 122/122 PASS, 38.39 seconds at -j3. Exact one-second process-tree monitor: 39 samples, median 129.495% CPU, peak 253.012% CPU, peak 494.371 MiB RSS and sampled peak 203.908 MiB PSS. Tests share the 480% aggregate CPU budget. Concurrent frozen WPT controllers mean this is not an isolated performance benchmark; interval snapshots can miss short-lived children and RSS can double-count shared pages.

## Unchanged upstream result and remaining losses

Pinned WPT revision 0e1c0bce3defd59a76cf2f351c7da5c5a42b26fb; all 37 ordinary URLs and all 1,172 assertions retained.

| Frozen browser | Passing assertions | Fully passing URLs | Harness OK |
|---|---:|---:|---:|
| Lumen 128 | 347/1,172 | 3/37 | 37/37 |
| Lumen 136 | 1,059/1,172 | 31/37 | 37/37 |
| Lumen 138 | 1,059/1,172 | 31/37 | 37/37 |
| Chromium 150 | 1,130/1,172 | 35/37 | 37/37 |

Exact 136 to 138 identity changes: zero gains and zero losses. Exact 128 to 138: 754 gains and 42 losses. Those 42 values are valid tentative Grid3 intrinsic-repeat grammar beyond the implemented Grid1/2 boundary; they are not generally invalid CSS. Their raw failures remain visible. The native follow-up closes the raw-style fallback inconsistency without changing this upstream denominator.

There remain 71 Chromium-passing misses in this cohort: 67 native resolved-track assertions, three declaration-block recomposition assertions and one native area-template computed-value assertion. Broader native grammar, stable automatic-repeat geometry, pending-substitution provenance and complete CSS support remain incomplete. Three real placement losses found later in the supplemental 200-URL cohort were attributed to 135 exposing the old lossy native start/span representation; candidate 139 repairs that separate domain. Do not claim global zero regressions while its validation is pending.

## Exact source and evidence

Binary SHA256: 6b758aacf3049a2c37942dbd7aa41885ea20336ff3e6f98e9fd3547330705fca.

Source manifest SHA256: b69447d1788cbdb7a545f8209a2a332f95e32db9a791d9b73b10b93f8f71c236.

Exact native 138 source copies, before the separate 139 placement edits, are privately retained under agent/progress/runtime/native-repeat-staged-tree/src/css. Ready metadata lists the source hashes. The owned production changes are the repeat parser/preflight and prepared-array branches of src/css/style.c, the css_grid_repeat_valid declaration in src/css/css.h, its supports hook in src/css/parse.c, tests/test_grid_repeat_validation.c, tests/browser/grid-auto-repeat-regression.html and their two CMake test blocks. Do not stage unrelated 139 placement regions with this change.

Private evidence root: agent/validation/linux-wpt-20261028/session2/css-grid-parser.

- native-repeat-baseline135.json, candidate138-native-repeat.json and chromium-grid-repeat-regression.json retain unchanged 84-case outcomes.
- candidate138-comparison.json retains every identity, raw-result hash, URL and binary metadata.
- candidate138-start.json and agent/progress/runtime/native-repeat138-ready.json retain source and fixture provenance.
- candidate138-grid-intrinsic-observation/ retains raw-style and CSSOM fallback observations.
- native138/ retains full CTest output, completion and one-second resource samples.

All external native WPT/browser launches use the sealed capture guard. Source and raw results remain private; the live preview publishes reviewed aggregates only.

## Primary standards

- https://drafts.csswg.org/css-grid-1/#repeat-syntax
- https://drafts.csswg.org/css-grid-2/#repeat-syntax
- https://drafts.csswg.org/css-grid-3/

## Final review blocker: calculated repetition counts

The unchanged 16-case ordinary observation discovered ten valid math counts omitted from the 84-case fixture. Frozen 128 accepts them but often calculates incorrect geometry; 135 newly rejects them through CSSOM and CSS.supports, and 138 additionally rejects raw styles and @supports. These are real regressions, separate from the 42 unsupported tentative Grid3 cases. Examples include calc(1 + 1), min(2, 3), calc(2.5) and calc(20px / 10px). Chromium accepts valid typed number expressions, rounds integer counts, and clamps calculated nonpositive counts to the valid minimum. Literal fractional counts and dimension-valued expressions remain invalid. Candidate 141 must correct this before the combined release is accepted.

All 16 observations are retained in repeat-count128.json, repeat-count135.json, repeat-count138.json and chromium-repeat-count-observation.json under the same private evidence root. Fixture SHA256: 3f70ff4bdd9947f5b8cf9508c26abf73728fa194176d05c537ac5e40c8031ade. Native probes use sealed guards and ephemeral localhost fixture servers with LUMEN_WPT_CTRL and LUMEN_PRE_FILE removed; no active WPT controller was contacted.

Additional primary standard: https://drafts.csswg.org/css-values-4/#calc-range (including integer math substitution, type checking and range checking).
