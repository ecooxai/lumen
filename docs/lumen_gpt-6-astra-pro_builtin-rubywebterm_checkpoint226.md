# Lumen Linux source checkpoint 226

GPT-6 Astra Pro · built-in agents · RubyWebterm · 30 October 2026

## Measured result

Selected stepped math594/594, stretch1012/1012 and MIME87/87 assertions pass. Actual focused and rendering executions are candidate226; fresh native and fixed640 executions are candidate226.
Native regression passes 143/143. Fresh exact640 passes 411/640 whole files and 9162/10540 reported assertions. All historical prior PASS identities and whole files required by the regression union are retained.

| Scope | Engine | Passing assertions | Whole files |
|---|---|---:|---:|
| CSS stepped math | 214 | 462/594 | 0/4 |
| CSS stepped math | 226 | 594/594 | 4/4 |
| CSS stepped math | Chromium156 | 592/594 | 3/4 |
| CSS stretch | 219 | 218/1012 | 11/48 |
| CSS stretch | 226 | 1012/1012 | 48/48 |
| CSS stretch | Chromium156 | 1012/1012 | 48/48 |
| MIME/nosniff safe6 | 221 | 37/86 | safe baseline |
| MIME/nosniff full7 | 226 | 87/87 | 7/7 |
| MIME/nosniff full7 | Chromium156 | 87/87 | 7/7 |
| CSS math neighbors | 226 | 173/432 | 3/16 |
| CSS math neighbors | Chromium156 | 430/432 | 15/16 |
| CSS progress | 226 | 138/139 | 2/3 |
| CSS progress | Chromium156 | 99/139 | 1/3 |
| Grid percentage family | 226 | 29/44 | 1/2 |
| Grid percentage family | Chromium156 | 44/44 | 2/2 |
| DOM insertAdjacentHTML family | 226 | 71/72 | 3/4 |
| DOM insertAdjacentHTML family | Chromium156 | 72/72 | 4/4 |
| Fixed640 non-URL sample | 214 | 8764/10540 | 401/640 |
| Fixed640 non-URL sample | 226 | 9162/10540 | 411/640 |
| Fixed640 non-URL sample | Chromium156 | 9551/10552 | 560/640 |

The stepped-math4 files have zero overlap with640. Other focused scopes can overlap640; gains are never added across scopes. Chromium and native broad assertion denominators differ. The sealed reference records99/139 in the draft progress family; all40 nonpasses remain counted. MIME has86 safe before assertions and one importScripts assertion newly executed after its lifetime repair; the extra assertion is new coverage, not a recovered before failure.

## Rendering and independent controls

All29 unchanged stretch comparisons and11 separate paint guards pass at800×600, DPR1, with the official comparison/readiness rules unchanged. All80 actual capture exits are0 and profiles/monitors are joined. This is software rendering evidence, not GPU certification. The11 paint guards have no pre221 native baseline.

| Independent authored fixture | Passing checks |
|---|---:|
| qa-css-math-zero-percent-constants | 4/4 |
| qa-css-progress-context-v2 | 33/33 |
| qa-insert-adjacent-authored | 29/29 |
| qa-css-progress-authored | 36/36 |
| qa-css-progress-percentage-types | 8/8 |
| qa-css-progress-percentage-positive | 8/8 |
| qa-css-math-authored | 62/62 |
| qa-css-time-signed-zero | 12/12 |
| qa-css-math-boundaries | 20/20 |
| qa-css-font-destination | 4/4 |
| qa-css-transform-token-edges | 8/8 |
| qa-css-font-basis-observation | 12/12 |
| qa-css-math-crash-smoke | 3/3 |
| qa-css-stretch-authored-v2 | 32/32 |
| qa-css-stretch-abspos-direction | 12/12 |
| qa-css-stretch-edge-contract | 12/12 |
| qa-css-stretch-float-observation | 9/9 |
| qa-css-stretch-paint-geometry | 32/32 |
| qa-css-stretch-clearance-controls | 18/18 |
| qa-css-stretch-static-float-controls | 20/20 |
| qa-script-mime-authored | 37/37 |
| qa-script-mime-worker-authored | 13/13 |

## Versioned controls and retained diagnostics

The required corrected progress-context fixture passes33/33; the frozen Chromium156 reference passes21/33. Two original literal-minus-zero inputs were explicitly replaced with arithmetic negative zero, with unchanged expected values. All original measurements and the correction receipt are retained. DOM29/29 and constant-zero-percent4/4 are separate mandatory authored controls.

| Namespace diagnostic | Passing checks | Reference |
|---|---:|---:|
| qa-insert-adjacent-context-boundaries | 1/2 | 2/2 |

The prior SVG context pass is preserved. XML case preservation remains visible as a deferred parser diagnostic if it still fails. These two diagnostics are not counted among the all-passing authored target fixtures. Ten frozen grid geometry observations match the reference and contribute zero WPT assertions.

These authored controls and three ordinary crash smokes are separate from WPT assertion counts; ordinary smokes are not formal WPT crashtest-protocol results. The raster arithmetic seam passed ASan/UBSan; worker/V8 was not sanitizer-instrumented. Worker lifetime evidence combines code ownership review with real rejection, nonexecution and diagnostic checks.

## Changes

CSS stepped math now handles the measured round(), mod() and rem() cases with typed values, IEEE edge behavior and deferred percentages. The compact 28-byte Length remains allocation-free, with no retained expression tree or layout-time reparsing.
Stretch sizing resolves actual available space, margins and border-box floors across blocks, flex, grid, replaced elements, absolute positioning and float exclusions. The complete 48-file family includes 42 standard files (738 assertions) and 6 tentative alias files (274 assertions). All 29 unchanged rendering comparisons pass.
Classic script and stylesheet response boundaries share correct MIME/nosniff decisions while ordinary fetch/image behavior and strict worker/module rules remain distinct. importScripts owns diagnostic MIME bytes before response destruction, repairing a confirmed lifetime bug.
Painting now respects negative stacking contexts through non-context wrappers, and monochrome glyph blending uses correctly rounded source-over arithmetic. Original intermediate candidate 220 and final candidate 226 captures demonstrate the repaired red overlay; official pixel expectations and fuzziness are unchanged.
CSS progress serialization, scalar typing, current and root font contexts, and selected integer consumers are improved. Marked affine resolution uses double intermediates to avoid spurious overflow; constant zero-percent provenance is preserved and the existing atan2 dispatcher is reachable.
insertAdjacentHTML now validates receivers and outer parents with the required conversion and Trusted Types order, inserts through intrinsic DOM operations, and chooses the correct HTML fragment context. XML parsing and 3D transform composition remain outside this repair.
The explicit-track intrinsic grid helper preserves fixed-track sizes and each placed item's own contribution, including cyclic 0% typing. Complex unsupported track and span cases retain the existing path; full grid conformance is not claimed.

## Provenance

Tested source commit: 3f8f41434bfca738a7a23ddd9448e6acf1fe6c4d
Separate private documentation handoff commit: b23bf7e41828309b858533c74039d1608883ee41. Its reviewed changes preserve all 646 tested inputs and both default/immutable engine hashes.
Immutable engine SHA256: 0305887bb666840e82857f13d28e6c5af5e33c7e2071e015c2e47c669cbf35b2
Exact recorded build inputs: 646. Their source manifest file SHA256 is c94d38938ed7c4e0981dd7edf70308c6fa15bfbc529695aba1a7fed71102b691.
The export retains reviewed public ancestry and contains exact recorded source inputs. Private task/history/auth/profile data, raw operational receipts and dependency caches are excluded. The source archive includes its public Git bundle and offline restoration receipts. The export was not independently rebuilt; external V8/wgpu/system dependencies are not bundled. This is source, not a portable browser binary.

## Remaining limits

- The full WPT suite and whole-browser 98/100 goal remain unestablished. The fresh 640 result is a frozen non-URL assertion sample, with 624 harness OK, 15 TIMEOUT and 1 ERROR retained.
- Complete focused families remain partial where reported: progress 138/139, DOM 71/72, grid percentage 29/44 and neighboring CSS math 173/432. One 3D rotate case, malformed XML fragments and broader layout/math gaps remain explicit.
- The separate namespace diagnostic remains 1/2: its prior SVG context pass is preserved, while XML case preservation is a preexisting deferred parser failure.
- Chromium 156 records 592/594 stepped assertions, 99/139 progress assertions and 21/33 corrected context controls. These reference nonpasses remain counted; draft feature differences do not justify changing upstream expectations.
- All 22 mandatory authored fixtures pass 424 fields, including 3 ordinary crash smokes. These are not additional WPT assertions or formal WPT crashtest-protocol results.
- URL conformance remains deferred by the user. Full WebDriver, manual, media, hardware GPU, platform and Test262 coverage is not established by these runs.
- The raster arithmetic seam passed ASan/UBSan; worker/V8 was not sanitizer-instrumented. Worker lifetime evidence is bounded ownership review plus actual post-fix rejection, nonexecution and continuation.
- One-second resource samples describe active workloads and can miss short-lived processes. Tree RSS can double-count shared pages, PSS is sampled separately, and no matched Chromium speed or idle-memory advantage is claimed.
- The curated source export preserves 646 exact recorded inputs and reviewed public ancestry but was not independently rebuilt. External V8/wgpu/system dependencies and private validation setup are not bundled; this is not a portable browser binary.
- The results walkthrough is rendered by Chromium and includes unedited original WPT images. It is not native Lumen desktop footage or GPU certification; no existing desktop process was restarted.

## Delivery

Source archive: src.tar.gz. Companion measurements: lumen_gpt-6-astra-pro_builtin-rubywebterm_checkpoint226.json.
Reviewed source/build limits and exact provenance accompany the archive and measured report. Live telemetry is task-limited and feedback requires the existing session key. Surge remains unauthenticated, with no new Surge deployment.
