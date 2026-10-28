# Grid used values: candidates 145–150

## Retained improvement

The final frozen150 holdouts pass all **47 unchanged authored controls** and retain **1,087/1,172 assertions** across the unchanged37-URL parser cohort:32 URLs fully pass and every harness is OK. This is **28 existing-identity gains and zero losses versus 141**, with exact zero gains/losses versus 145 and 146. Chromium passes1,130/1,172 and 35 URLs. Against 128, the complete record retains782 gains and all 42 unsupported tentative Grid 3 intrinsic automatic-repeat losses. Those42 values are valid in the tentative extended grammar; they are not described as generally invalid CSS.

The scoped repair is assessed at 96/100 for the combined validation gate. Whole-browser readiness remains19/100, a subjective judgment distinct from pass rates. Final native, supplemental and broad release gates are independently owned and remain pending in this record until their completion is supplied.

## What changed and why

Computed-style reads now reuse actual final column and row arrays produced by Grid layout. An optional Box-owned payload stores those arrays and the explicit/implicit grid offsets; the JavaScript layer does not duplicate sizing. The arrays are replaced on subsequent layout and freed on Box destruction before arena reset. Independent reviews found no shallow Box copying along the relevant paths.

The public getter emits used pixel tracks only for the correct generated grid Box. It preserves computed track structure for hidden, detached and non-grid cases, and the private animation getter retains its existing computed-token contract. Pseudo queries use the pseudo's own Box and style. Cross-document reads flush through the target document's owning JavaScript context and host, so parent-window queries after iframe mutations see the correct layout.

Line-name serialization consumes actual materialized tracks and preserves explicit and leading implicit offsets. Parsing and serialization share an escape-aware closing-bracket finder. No synthetic track size is inferred from a test's expected geometry.

The authored controls include percentages, fractions, gaps, auto tracks, empty grids, leading/trailing implicit tracks, names and escapes, display transitions, detach/reattach, font and size changes, foreign-document reads, generated and ungenerated pseudos,120 mutation/rebuild cycles and 40 remove/recreate cycles. The native fixture retains17 checks and 120 rebuild cycles. These target the concrete ownership and invalidation risks.

## Rejected attempts and corrections

Candidate 145's first build failed on a C type error while accessing the stylesheet engine through a void pointer. The failed build was preserved; the second attempt added the established cast without changing semantics. Runtime145 remained unaccepted at 88/100:46/47 authored controls and 127/129 native checks. Its real blocker was an explicit content:none pseudo returning none instead of its computed repetition structure.

The narrow146 correction retains a pseudo style that was already allocated and computed for an explicit content declaration. It keeps both the early content-declaration allocation guard and the generated-box content condition. This repairs computed queries without creating boxes for content:none or allocating two new pseudo styles for every universal reset. Omitted-content and hidden-host pseudo-query gaps remain inherited limitations.

A separate146 placement experiment produced21 supplemental geometry regressions and was rolled back byte-for-byte in 150. All gains attributable to that experiment are excluded from the final result. The 47 cache controls and 37-URL parser outcomes survive the rollback unchanged.

## Native fixture migration was not an engine gain

The original grid_item_width fixture mixed five geometry checks with minified track-structure strings read through the public getter. Once that public getter correctly returned used values, the parser-structure coverage needed an independent home. The first migration incorrectly copied the old simplified strings into private-animation expectations. Immutable144 source proof shows that the private getter has always preserved the authored tokens `10px minmax(0,1fr)10px` and `minmax(20px,.8fr)auto minmax(20px,1fr)` along this path.

The reviewed147 fixture preserves all five geometry checks, explicitly verifies all three column and row track structures, zero/min20 floors and 0.8:1 flex factors, and checks the unchanged private tokens. It separately derives public columns as10px,1060px,10px from the 1080px container. Public rows use the actual middle item's height and divide the remaining 540px space in a0.8:1 ratio with 0.01px tolerance. The full147 native suite passes130/130. No upstream expected result changed, and the fixture correction is not counted as an engine or WPT gain.

## Evidence and practical limits

The structured companion record contains every relevant binary/source/fixture hash, rejected-attempt disposition, scoped comparison and cleanup result. Final150 binary SHA-256: `79874c9c16af2086cb1de93a6194276d4b17512897aeca118244beddef7bb967`. Its exact584-file snapshot is preserved privately. The final focused comparison is `agent/validation/linux-wpt-20261028/session2/final-css-holdouts150/comparison.json`; earlier evidence remains under `session2/used-grid-tracks/` and the original candidate directories.

All final browser probes use sealed capture guards, unique empty profiles and private outputs, one-second process-tree monitoring, and real cleanup-worker joins. No primary controller is reused. The full148 per-test CTest log is retained; an attempted late147 log copy that actually contained148 startup output is accurately renamed with an explicit provenance correction, not presented as old full-suite evidence.

The remaining 39 of 67 original used-size misses reflect unresolved sizing/serialization behavior. Intrinsic sizing, automatic repetition, subgrid and placement remain incomplete. Correctly exposing measured layout values does not establish complete layout conformance. The standards basis is the CSS Grid resolved-track distinction and CSSOM pseudo/computed-style model: <https://drafts.csswg.org/css-grid-2/#resolved-track-list> and <https://drafts.csswg.org/cssom/>.

## Final native gate update

The immutable build 150 full native suite completed 132/132 checks with exit 0 from 12:02:46 to 12:04:01 UTC (74.87 seconds in CTest; 75 one-second resource samples). The 33 unchanged Grid rollback controls also restore all 21 geometry losses and exactly match build 145. Supplemental 200, final broad, rendering and dedicated performance gates remain pending; this completion is not a claim of full WPT parity.

## Accepted combined source and supplemental gate

Source 150 is accepted as commit `0f62172b08683c7c0d89722c588af6ff94f82ca3`, with all 584 compiled files matched to Git. The sealed supplemental cohort passes 1,767/3,370 assertions and 77/200 URLs, with all 200 harnesses OK: 554 existing gains and zero losses versus 128, and 128 gains with zero losses versus 139. The scoped correction is accepted; final primary, expanded, rendering and dedicated performance measurements continue separately.
