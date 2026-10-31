# Lumen Linux compatibility report — candidate 234

**Status: QUALIFIED_SCOPED_LINUX_DEVELOPMENT_RELEASE.** The selected 39-file containment scope passes **286/286 assertions and 28/28 exact rendering comparisons**. The fixed 640-file non-URL sample passes **417/640 whole files (65.15625%) and 9249/10540 assertions**. These are separate measured scopes; this report makes no full-WPT or whole-browser Chromium-parity claim.

## Results and comparison

| Measurement | Accepted 226 baseline | Final 234 | Pinned Chromium 156.0.8078.80 |
| --- | ---: | ---: | ---: |
| Fixed 640 whole files | 411/640 (64.21875%) | 417/640 (65.15625%) | 560/640 (87.50%) |
| Fixed 640 assertions | 9162/10540 | 9249/10540 | 9551/10552 |
| Selected 39 containment assertions | 81/286 | 286/286 | 286/286 |
| Selected 39 exact reftests | 1/28 | 28/28 | 28/28 |
| Separate upstream visibility/observer API3 | 10/23 | 18/23 | 23/23 |
| Authored visibility options | 15/49 in 231 | 49/49 | 49/49 |
| Separate authored slot-accessor controls | Not run | 2/2 | 2/2 |

The focused containment score is **100/100 selected outcomes**, comprising 11 assertion files and 28 reftests. It is not an overall browser score. The original three-file logical subgroup moved from 0/48 assertions and 0/2 reftests to 48/48 and 2/2. The 39-file cohort produced 67 actual native test/reference process lifecycles.

The fixed sample gained **6 whole-file passes and 87 assertion passes** over accepted 226. It preserves all 9,246 assertion passes and 416 whole-file passes measured in intermediate 231, adding three assertions and the complete `the-end.html` file. No registered identity was removed or added, no prior pass was lost, and no new non-OK harness result appeared.

| Fixed 640 outcome | Final 234 | Chromium 156 |
| --- | ---: | ---: |
| Harness OK | 625 | 626 |
| Harness TIMEOUT | 14 | 13 |
| Harness ERROR | 1 | 1 |
| Assertion PASS | 9249 | 9551 |
| Assertion FAIL | 1255 | 980 |
| Assertion TIMEOUT | 13 | 12 |
| Assertion NOTRUN | 23 | 9 |

All 640 files remain in each whole-file denominator. Assertion denominators differ because the browser harnesses registered different numbers of subtests; each actual denominator remains visible. The unchanged schedule uses two 320-file shards and a 15-second per-file ceiling. Final 234 completed in 785.889 seconds with actual outer exit 0 and 650 unchanged recorded inputs. All 640 task-owned native launches exited 0 and completed profile cleanup. WPT duration is not a browser-speed benchmark.

## Changes that produced the measured gains

- Native physical and logical intrinsic-size containment, including parsing, computed values, writing-mode mapping, bounded family cascade priority and actual geometry.
- Correctness-focused hidden-content sizing, descendant paint/hit suppression, observer/visibility behavior and retained element scroll offsets.
- Size-contained grid automatic-repeat sizing, empty multicol intrinsic sizing, and select/button/video sizing controls.
- Constant-color scalar and ARM NEON interpolation arithmetic, with independently proved frame results.
- Rendering-opportunity-driven IntersectionObserver delivery while keeping quiescent pages unscanned.
- Genuine document lifecycle metadata, legacy load target handling, per-context reentrancy/lifetime guards and strict actual-exit test controls.
- `checkVisibility` option conversion and own visibility/inclusive opacity checks through private native ancestry, without reading public DOM accessors.

The changes affect real engine behavior. Selected upstream fixtures, expected results, timeouts, the pixel comparator and frozen 640 schedule were not weakened. The public source retains the actual regression tests and generator dependencies.

## Native, lifecycle and sanitizer gates

- **147/147 selected native tests**, from 150 registered targets. Exactly `url`, `url_host_nul` and `data_url` remain deferred. No selected native test failed or skipped.
- **Document lifecycle:** unchanged upstream `/html/syntax/parsing/the-end.html` passes 4/4. A separate 12-case authored comparison satisfies 114 canonical event/action records and its declared reentrancy/removal contract. Every durable case asserts its finish boundary and absence of late records.
- **Hidden-scroll retention:** seven cases and 30 phases satisfy the pinned offset/range/position contract. The separately logged parent-overflow sequence is 160×140 → 400×400 → 160×140. The related unchanged upstream hidden-scroll test passes 1/1.
- **Observer cadence:** all five authored transitions pass, and a 3.3-second quiet interval produces zero geometry rescans and zero notifications.
- **Actual instrumented C++ runtime:** lifecycle positive, missing-completion negative, FAIL-completion negative and 49-option positive controls exit 0/1/1/0. ASan, UBSan and LSan run without suppressions; no findings remain. Instrumentation is verified on `frames.cc.o`, `js.cc.o`, `jsnative.cc.o` and `jsrun.c.o`. The prebuilt V8 library is not instrumented.
- **Additional fresh sanitizer gates:** stepped math; native containment/grid-used-values/grid-absolute; and the existing raster layer harness pass. Raster interpolation also has an independent per-channel reference proof and an installed ARM64/NEON compile check. ARM runtime behavior was not tested on this x86 host.

Candidate 232 is preserved as a failed sanitizer checkpoint: real process exit 23 with 54,207 leaked bytes in 48 allocations, despite passing behavior. A matched QA-only host correction kept the engine and lifecycle fixture unchanged and proved that releasing event-batch node pins removed the leak. Candidate 233 applied that host correction and changed the lifecycle completion gate to require both its exact completion value and a successful process exit. No sanitizer suppression or relaxed behavioral oracle was used.

## Separate rendering and resource regression gates

The separate retained rendering campaign passes **40/40** reftests:29 stretch and11 paint cases, with80 actual test/reference captures. Exact pixels, readiness, source/binary pins and owned cleanup are independently verified.

All three runs completed the same 200-node, 50-step workload, 10 checks and checksum 21144 with identical geometry. Compared with accepted 226, 234 fixed-work CPU increased 0.019637 CPU-seconds (7.27%) over approximately 4.00 seconds of matched fully covered intervals; late-idle CPU increased 0.009958 seconds. Sampled whole-window RSS/PSS were 1.859/2.130 MiB lower. No predeclared material-regression investigation threshold was reached, and no repeat was warranted. These single-run measurements neither prove an optimization nor measure throughput. Chromium browser-only memory is descriptive and separate from ChromeDriver.

| Browser / phase | CPU on covered intervals (s) | Covered / phase time (s) | Sampled RSS peak (MiB) | Sampled PSS peak (MiB) |
| --- | ---: | ---: | ---: | ---: |
| Lumen226 / idle-before | 0.040 | 4.002 / 5.000 | 146.506 | 131.247 |
| Lumen226 / fixed-work | 0.270 | 4.004 / 5.301 | 166.724 | 151.539 |
| Lumen226 / idle-after | 0.030 | 4.000 / 5.000 | 169.607 | 154.521 |
| Lumen226 / whole-fixture-window | 0.480 | 14.001 / 15.302 | 169.607 | 154.521 |
| Lumen234 / idle-before | 0.040 | 4.003 / 5.001 | 146.366 | 131.063 |
| Lumen234 / fixed-work | 0.290 | 3.998 / 5.327 | 165.519 | 150.138 |
| Lumen234 / idle-after | 0.040 | 4.005 / 5.000 | 167.748 | 152.391 |
| Lumen234 / whole-fixture-window | 0.510 | 14.004 / 15.328 | 167.748 | 152.391 |
| Chromium156 browser only / idle-before | 0.131 | 4.019 / 5.000 | 1377.309 | 474.090 |
| Chromium156 browser only / fixed-work | 0.252 | 4.023 / 5.090 | 1408.492 | 503.469 |
| Chromium156 browser only / idle-after | 0.060 | 4.021 / 5.000 | 1417.294 | 508.930 |
| Chromium156 browser only / whole-fixture-window | 0.501 | 14.020 / 15.090 | 1417.294 | 508.930 |

CPU totals below cover only fully contained sampled intervals, with their covered duration shown; they are not exact full-phase CPU accounting. CPU/RSS are sampled at one-second intervals; PSS is sampled every five seconds when available and otherwise remains null. Chromium browser-only samples are separate from the ChromeDriver-inclusive tree. Peaks are sampled maxima, not exact OS high-water marks. The 15-second paced fixture does not establish throughput or general browser speed.

Before the final run, a timestamped policy defined practical investigation thresholds for 226→234 regressions: work CPU at least 25% and 0.10 CPU-seconds; late-idle CPU at least 2× and 0.05 CPU-seconds; RSS at least 15% and 32 MiB; PSS at least 15% and 16 MiB. These are engineering triage thresholds, not statistical confidence bounds. Existing phase coverage and traces are inspected before any repeat measurement.

## Remaining limitations and exact comparison boundaries

The five retained upstream API failures concern two `content-visibility:auto` cases, two unassigned-slot cases, and listbox option/optgroup boxes. The authored 49 options and two accessor controls are separate tests and do not erase these failures. General CSS cascade layer/origin rollback, complete grid/multicol layout, document.domain and iframe realm/async-loading work remain outside this repair. Dedicated URL work remains deferred.

The hidden-scroll contract has exactly one predeclared size-cache distinction: in `range-shrink-and-regrow / hidden-shrunk`, the native hidden descendant reports 150×130 while the reference retains 400×400. All other pinned dimensions, offsets, ranges and positions remain strict. Hidden content retains layout/style data; no skipped-layout optimization or complete content-visibility conformance is claimed.

Lifecycle raw traces are not fully equivalent. Declared initial child-load microtasks retain existing synchronous frame-insertion timing; the one `pageshow-open` microtask action may observe `activeGeneration=replacement` rather than the reference value `initial`. Author statements after removal may finish, but stale native owner-load/pageshow follow-up is forbidden. The observed pinned-Chromium owner-load/pageshow ordering is retained, with the current-HTML ordering distinction recorded. A separate old nonzero-threshold observer assertion disagrees with Chromium; its exact entry was not logged, so neither a fixture error nor a Chromium bug is established.

The reference adapter does not expose Chromium's OS exit status. Its own successful completion and task-owned profile/process cleanup are recorded. The comparisons make no claim about every browser feature or real-world workload.

## Exact source, executable and reproduction

- Tested working source: `206bb89d161d91278c56899a3f664a99480374f8`.
- Executable SHA-256: `253f563c6529af4d8bab38b12373b51aec9941eb475fa16cae50093ff4e2b282`; size 7,335,352 bytes.
- 650 recorded-input digest: `61bf484b7bcfaf091adf135747423b8ce163c2b48884d5076bd06a469826094f`.
- Sealed recorded-input manifest file SHA-256: `58a936e5da8f063bf666dbe9c988696139a3f8e4a89b2dcae0fcbc8508909c5f`.
- WPT revision: `0e1c0bce3defd59a76cf2f351c7da5c5a42b26fb`.
- Frozen 640 selection SHA-256: `8b5d0d491ce8b14e6ee8107d1213c10b90127a0193207d06057216bd54f3243f`.
- Frozen 640 execution schedule SHA-256: `7e17aeaf30a0d05982a5d69a11fe0bd062561fe9375b2fcad72fb1eba41d1c32`.

The public Git export descends only from reviewed public 226 ancestry. Its final commit and bundle identity are recorded in the publication receipt and offline-recovery metadata; the tested working commit and public export commit intentionally differ. `src.tar.gz` includes the self-contained public-history bundle, which is also a separate download. No private development history is exported.

The executable package preserves the exact tested bytes. It requires the documented Linux runtime libraries and matching V8/ICU/wgpu data. No stripping, relinking, RUNPATH rewriting or unreviewed dependency redistribution was used. See the [build/run guide](gpt-6-astra-pro-builtin-rubywebterm-linux-build.md), [runtime identities](gpt-6-astra-pro-builtin-rubywebterm-linux-runtime.json), and [147-test replay inventory](gpt-6-astra-pro-builtin-rubywebterm-native147-replay-inputs.json). Native replay requires 64 pinned external WPT files before configuration; the guide verifies their hashes, exact parser data set, resolved font bytes, full 147 identity set, actual test exit and absence of skipped cases.

The [machine-readable report](gpt-6-astra-pro-builtin-rubywebterm-linux-report.json) contains selected-path/source/support hashes, scoped outcomes, iteration timing and evidence identities. All 650 exported inputs and reviewed extras match the tested source; no fresh hermetic or bit-reproducible rebuild is claimed. Private guarded WPT capture machinery is not supplied.

## Selected upstream paths

These 39 paths were frozen before the fresh baseline and implementation. The type column separates assertion files from reftests. Reference URLs and source/support hashes are in the JSON report.

| Group | Type | Upstream path |
| --- | --- | --- |
| Selected logical containment | reftest | `/css/css-sizing/contain-intrinsic-size/contain-intrinsic-size-logical-001.html` |
| Selected logical containment | reftest | `/css/css-sizing/contain-intrinsic-size/contain-intrinsic-size-logical-002.html` |
| Selected logical containment | testharness | `/css/css-sizing/contain-intrinsic-size/contain-intrinsic-size-logical-003.html` |
| Numbered containment controls | reftest | `/css/css-sizing/contain-intrinsic-size/contain-intrinsic-size-001.html` |
| Numbered containment controls | reftest | `/css/css-sizing/contain-intrinsic-size/contain-intrinsic-size-002.html` |
| Numbered containment controls | reftest | `/css/css-sizing/contain-intrinsic-size/contain-intrinsic-size-003.html` |
| Numbered containment controls | reftest | `/css/css-sizing/contain-intrinsic-size/contain-intrinsic-size-004.html` |
| Numbered containment controls | reftest | `/css/css-sizing/contain-intrinsic-size/contain-intrinsic-size-005.html` |
| Numbered containment controls | reftest | `/css/css-sizing/contain-intrinsic-size/contain-intrinsic-size-006.html` |
| Numbered containment controls | reftest | `/css/css-sizing/contain-intrinsic-size/contain-intrinsic-size-007.html` |
| Numbered containment controls | reftest | `/css/css-sizing/contain-intrinsic-size/contain-intrinsic-size-008.html` |
| Numbered containment controls | testharness | `/css/css-sizing/contain-intrinsic-size/contain-intrinsic-size-009.html` |
| Numbered containment controls | reftest | `/css/css-sizing/contain-intrinsic-size/contain-intrinsic-size-010.html` |
| Numbered containment controls | reftest | `/css/css-sizing/contain-intrinsic-size/contain-intrinsic-size-011.html` |
| Numbered containment controls | reftest | `/css/css-sizing/contain-intrinsic-size/contain-intrinsic-size-012.html` |
| Numbered containment controls | reftest | `/css/css-sizing/contain-intrinsic-size/contain-intrinsic-size-013.html` |
| Numbered containment controls | reftest | `/css/css-sizing/contain-intrinsic-size/contain-intrinsic-size-014.html` |
| Numbered containment controls | reftest | `/css/css-sizing/contain-intrinsic-size/contain-intrinsic-size-015.html` |
| Numbered containment controls | reftest | `/css/css-sizing/contain-intrinsic-size/contain-intrinsic-size-016.html` |
| Numbered containment controls | reftest | `/css/css-sizing/contain-intrinsic-size/contain-intrinsic-size-017.html` |
| Numbered containment controls | reftest | `/css/css-sizing/contain-intrinsic-size/contain-intrinsic-size-018.html` |
| Numbered containment controls | reftest | `/css/css-sizing/contain-intrinsic-size/contain-intrinsic-size-019.html` |
| Numbered containment controls | reftest | `/css/css-sizing/contain-intrinsic-size/contain-intrinsic-size-020.html` |
| Numbered containment controls | reftest | `/css/css-sizing/contain-intrinsic-size/contain-intrinsic-size-021.html` |
| Numbered containment controls | reftest | `/css/css-sizing/contain-intrinsic-size/contain-intrinsic-size-022.html` |
| Numbered containment controls | reftest | `/css/css-sizing/contain-intrinsic-size/contain-intrinsic-size-023.html` |
| Numbered containment controls | reftest | `/css/css-sizing/contain-intrinsic-size/contain-intrinsic-size-024.html` |
| Numbered containment controls | reftest | `/css/css-sizing/contain-intrinsic-size/contain-intrinsic-size-025.html` |
| Numbered containment controls | reftest | `/css/css-sizing/contain-intrinsic-size/contain-intrinsic-size-026.html` |
| Numbered containment controls | reftest | `/css/css-sizing/contain-intrinsic-size/contain-intrinsic-size-027.html` |
| Numbered containment controls | testharness | `/css/css-sizing/contain-intrinsic-size/contain-intrinsic-size-028.html` |
| Numbered containment controls | testharness | `/css/css-sizing/contain-intrinsic-size/contain-intrinsic-size-029.html` |
| Numbered containment controls | testharness | `/css/css-sizing/contain-intrinsic-size/contain-intrinsic-size-030.html` |
| Numbered containment controls | testharness | `/css/css-sizing/contain-intrinsic-size/contain-intrinsic-size-031.html` |
| Numbered containment controls | testharness | `/css/css-sizing/contain-intrinsic-size/contain-intrinsic-size-032.html` |
| Numbered containment controls | testharness | `/css/css-sizing/contain-intrinsic-size/contain-intrinsic-size-033.html` |
| Containment parsing | testharness | `/css/css-sizing/contain-intrinsic-size/parsing/contain-intrinsic-size-computed.html` |
| Containment parsing | testharness | `/css/css-sizing/contain-intrinsic-size/parsing/contain-intrinsic-size-invalid.html` |
| Containment parsing | testharness | `/css/css-sizing/contain-intrinsic-size/parsing/contain-intrinsic-size-valid.html` |

### Separate API3 selection

- `/css/cssom-view/checkVisibility.html`
- `/css/css-contain/content-visibility/content-visibility-030.html`
- `/css/css-contain/content-visibility/content-visibility-031.html`

## Interactive demonstration and showcase

The [authored interactive demonstration](../examples/gpt-6-astra-pro-builtin-rubywebterm-lumen-live-demo.html) runs in the browser opening it. **Open it in Lumen to exercise the Lumen engine.** It is not a remote Lumen session. Its live values are distinct from the [recorded native234 observations](gpt-6-astra-pro-builtin-rubywebterm-live-demo-evidence.json).

The exact demonstration HTML passed native marker-only validation with actual process exit0 and clean owned-profile cleanup. It shows real logical containment dimensions, retained hidden-scroll offsets, visibility options and genuine DOMContentLoaded/load/pageshow events. It is authored content outside WPT scoring; no screenshot or visual rendering claim is made for this demonstration.

The separately approved showcase is a40.000-second,960x720,24fps H264 motion report from verified result cards. Five extracted frames were visually reviewed for readable scope labels and the large white-on-black attribution: **GPT-6 Astra Pro | built-in agents | RubyWebterm**. It contains no user-display capture or generated imagery.

| Artifact | SHA-256 |
| --- | --- |
| Measured demonstration HTML | 9b330a84b469beb564e615c7aeb18ce3bc93e1b55804f6e49aa5badd1fe11236 |
| Approved40-second showcase MP4 | c5c0905d24f60ef8b676e81e82bb0be60d45de4546cb348df8f8c5929376d5e9 |

The published download page supplies src.tar.gz, the separate public-history Git bundle and an exact-byte Linux development executable package. The source tarball also contains the bundle and offline recovery instructions. The public commit differs from the tested working-source commit because only the reviewed public history is exported. Final public commit/tree, archive and bundle hashes are recorded in the separate publication/recovery receipt, avoiding circular commit contents.

## Iteration ledger

Each duration comes from the recorded start/end. The technical session lasted221.5706009 minutes; post-task packaging/publication time is recorded separately. Scores refer to named scopes; unrelated suites are not averaged. Intermediate 232 and 233 did not run the 640 campaign.

| Iteration | Candidate | Start UTC | End UTC | Minutes | Broad 640 |
| ---: | ---: | --- | --- | ---: | --- |
| 0 | 226 | 2026-10-31T07:00:12Z | 2026-10-31T07:09:52.459971+00:00 | 9.674 | REUSED_ACCEPTED411/640,9162/10540 |
| 1 | 227 | 2026-10-31T07:09:52.459971+00:00 | 2026-10-31T07:26:26.647332+00:00 | 16.570 | NOT_RUN |
| 2 | 228 | 2026-10-31T07:26:26.647332+00:00 | 2026-10-31T07:53:55.093155+00:00 | 27.474 | NOT_RUN |
| 3 | 229 | 2026-10-31T07:53:55.093155+00:00 | 2026-10-31T08:16:56.662800+00:00 | 23.026 | NOT_RUN |
| 4 | 230 | 2026-10-31T08:16:56.662800+00:00 | 2026-10-31T08:33:59.719549+00:00 | 17.051 | NOT_RUN |
| 5 | 231 | 2026-10-31T08:33:59.719549+00:00 | 2026-10-31T09:35:38.382631+00:00 | 61.644 | MEASURED416/640,9246/10540 |
| 6 | 232 | 2026-10-31T09:35:38.382631+00:00 | 2026-10-31T09:53:58.848202+00:00 | 18.341 | NOT_RUN |
| 7 | 233 | 2026-10-31T09:53:58.848202+00:00 | 2026-10-31T10:06:13.077038+00:00 | 12.237 | NOT_RUN |
| 8 | 234 | 2026-10-31T10:06:13.077038+00:00 | 2026-10-31T10:41:46.236056+00:00 | 35.553 | MEASURED417/640,9249/10540 |

Prepared by **GPT-6 Astra Pro**, using **built-in agents** and **RubyWebterm**.
