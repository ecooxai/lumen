

# Session 10290244 — Lume Apple WPT

Start: 2026-10-28T18:47:35Z (2026-10-29 02:47:35 Asia/Shanghai).
Deadline: 2026-10-28T21:47:35Z. Branch: `gpt6-astra-pro_release-integrator_lume-apple-wpt-10290244`.
Model: GPT-6 Astra Pro, built-in subagents with inherited same reasoning.
Workers: wpt_engine, apple_runtime, release_integrator; root manages and brokers remote I/O.

Intake preserved inherited source in `a8bd64552f9379cca33cd736a187924ec18eb356` and archived prior handoff.
No new compatibility percentage or overall quality score is inferred from intake.
Iteration scores and times below must come from completed evidence-backed work.

| Iteration | Start UTC | End UTC | Wall seconds | Worker effort seconds | Tests/build timing | Score and basis | Result |
|---|---|---|---:|---:|---|---|---|
| 10290244-001 | 2026-10-28T18:47:35Z | 2026-10-28T19:00:32.602027+00:00 | 777.602 | Not separately instrumented; three same-model workers active in parallel | Native frozen WPT 9.224 s; Chrome reference 4.144 s; compare 0.037 s; monitor regression stage 1.053 s (20 tests) | 9.97/100 task gates; only current frozen subset earns points so far | Preserved source/task, established current 1844/1850 baseline, hardened interval CPU sampling acceptance. |

Iteration 10290244-001 evidence: `output/session-20261029/native-qa-kit-v1/source/output/wpt-comparison-session10290244-baseline-1.json`; `output/session-10290244/monitor-gate-green.json`; monitor raw CSV and test log under the same prefix. The monitor suite is 20/20, not WPT. A one-sample short unit run does not establish browser resource usage. The six-file upstream subset is 1844/1850 (99.675676%) versus Chrome 1850/1850; all six harnesses completed, five files fully pass. Full official WPT remains unmeasured. Session elapsed to this checkpoint: 777.602 seconds.


## Iteration 10290244-002 — enforced idle acceptance

Start: 2026-10-28T19:00:32.602027+00:00. End: 2026-10-28T19:10:56.571308+00:00. Iteration wall time: 623.969 s. Total session elapsed: 1401.571 s.
Workers: apple_runtime implemented and verified; release_integrator independently reviewed/committed; root brokered remote commands. Separate model compute/worker-active seconds are unavailable and are not equated with elapsed time.

The post-WASM idle runner now rejects over-limit p95 CPU, missing/nonfinite measurements, stale passing labels, short windows, and sampling errors/gaps. Mac regression tests: 6/6; monitored test stage 1.044 s. This is a tool acceptance correction, not an invented change in browser CPU.

Existing current-baseline raw samples were adjudicated using the preset full interval starting after 15 seconds of settling: 65 samples over 64.009 s, interval CPU mean 0.061%, p95 0.996%, peak 1.0%; RSS 156.484→144.266 MiB. No collection errors/gaps. Evidence: `output/session-10290244/apple-wasm-idle-baseline/fixed-settle-review.json`; original raw and receipt hashes are retained in that review.

Score: 14.97/100 task acceptance for baseline binary `7a4bfec10412f3e2c0eee92a01bf8c9bec496eb122114ab4844e8e4a4c783b85`. WPT baseline remains 1844/1850 (99.675676%) across six files; no full WPT or same-IPA claim. A changed candidate requires new artifact-matched performance evidence before retaining these points.


## Iteration 10290244-003 — native DOM clone and ownership repair

Start: 2026-10-28T19:10:56.571308+00:00. End: 2026-10-28T19:18:56.812906+00:00. Iteration wall: 480.242 s. Session elapsed: 1881.813 s. Workers: wpt_engine implementation; apple_runtime independent source review; release_integrator artifact/acceptance review; root remote I/O coordination. Separate model compute durations remain unavailable.

Fixed native Document allocation/clone ownership; qualified-name, URL/content-type/encoding/quirks metadata; cross-realm Attr identity, values, clone/import/attachment; shallow template cloning; and XHR XML document URL/baseURI. Regression-first clone metadata changed from 1/36 to36/36; XML/cross-realm fixture from25/33 to33/33. All262 targeted regression checks passed. These are separate from upstream WPT.

The unchanged six-file pinned WPT subset now passes1850/1850 (100%) in Lume and Chrome, all6files fully passing, no comparison blockers. This is not a full WPT result. Native binary SHA-256 `5ec2abd8d1503c17524e65ec6ed89ed5ba4a75253e326a0d8715f7a49b60fd75`; jsrun `ac22b2347e0d786f220575b140e75a775ea3d38fccdea4f2be460cd4a77e2966`. Incremental native build 3.075 s using prebuilt V8; longest test stage 9.083 s. Every stage retained one-second resource samples; very short stages do not establish idle behavior.

Source, fixtures, complete compiled-source hashes, targeted-stage timing, WPT comparison and red-log hashes are in `agent/evidence/10290244-003-clone-metadata.json`; raw reports remain under the recorded output paths. Score: 35.00/100 task gates. Prior 7a4b idle observations are explicitly historical; the new5ec2 artifact gets no idle/interaction/soak points before its own tests.


## Iteration 10290244-004 — renderer overdraw reduction

Start: 2026-10-28T19:18:56.812906+00:00. End: 2026-10-28T19:52:01.222206+00:00. Iteration wall: 1984.409 s. Session elapsed: 3866.222 s. Workers: wpt_engine renderer implementation; apple_runtime independent pixel/performance review; release_integrator evidence/commit review; root remote coordination. Separate model compute durations are unavailable.

Avoided browser chrome pixels that page compositing immediately overwrites, and skipped repaint work hidden by a full opaque rectangle while preserving conservative clip/layer/rounded-edge rules. Regression-first exact-pixel tests improved 54/92 to 92/92. Build plus six raster/layout executables passed; build 2.102 s with prebuilt V8. Each stage has one-second samples and recorded start/end/duration.

The unchanged six-file pinned WPT subset still passes 1850/1850 in both Lume and Chrome, 6/6 files, on native SHA `f941cfcdedd9a59b60f41b1cb2be560dce136fc4e6d44cacfce0e5ab25cbbe46`. This remains a selected-suite result. Controlled scroll p95 decreased from 29.312% to 23.864% (18.59% observed reduction), but the 10% target still FAILS. The independent fixed final idle window passes: 49 valid interval samples over 48.976s, p95 1.000%, RSS 151.859→151.859MiB; no gaps/collector errors and natural cleanup. No soak or same-IPA credit.

Score: 40.00/100 task gates. All source hashes, fresh WPT, timing, raw CPU hash and explicit failed interaction verdict are retained in `agent/evidence/10290244-004-renderer.json`. Expanded priority WPT remains 1515 frozen variants across 235 sources/seven categories; first bounded paired categories are next.


## Iteration 10290244-005 — retain lazy wrappers during DOM cloning

Start: 2026-10-28T19:52:01.222206+00:00. End: 2026-10-28T19:56:47.055158+00:00. Iteration wall: 285.833 s. Session elapsed: 4152.055 s. Workers: apple_runtime implementation and allocation experiment; wpt_engine independent review; release_integrator artifact/commit review; root remote coordination. Separate model compute durations are unavailable.

Clone metadata now walks native trees and copies private platform state only where a source wrapper already has relevant metadata. Ordinary parsed descendants remain lazy, unopened template contents are not allocated merely for inspection, and page-defined Array.prototype setters cannot intercept the internal metadata list. The targeted allocation test improved 20/30 to 30/30. Two deep clones of a 1024-descendant tree added 2 wrappers instead of 3074; template clones 2 instead of 2055; sparse metadata 2 instead of 2049. These exact counters are not a measured general RAM reduction.

All 292 retained native compatibility/allocation checks passed across nine timed stages; prebuilt-V8 incremental build 3.093 s. The unchanged six-file pinned WPT subset freshly passes 1850/1850 in Lume and Chrome, 6/6 files, on native SHA `de338a9f08913dc814d0051918ade567548f4f1c9231dcc7e44247ffbdf018e5`. Evidence and full compiled-source hashes: `agent/evidence/10290244-005-clone-allocation.json`.

Task score 35.00/100. The previous 40.00 score included 5 idle points measured on f941; they are now explicitly historical because the executable changed. No current-artifact idle/interaction/soak or same-IPA success is inferred from these short checks. Expanded 1515-variant inventory is unchanged; the completed f941 DOM comparison remains a separate artifact cohort.


## Progress publication checkpoint — 2026-10-28T20:09:13.618684+00:00

Native iteration and task score remain unchanged (35.00/100). The isolated progress view passed six service regressions, actual public TLS/WebSocket tests with two live samples and nonce RTT, full downloaded source SHA verification, authenticated message storage and an agent reply. Fresh-profile native Chrome verified desktop1280×1100 and phone390×844 layout, exact current native/subset counts, all seven frozen categories, historical cohort labeling, real CPU/RAM/RTT updates and a fixed bottom-right iteration badge. Visual QA wall time 12.806s includes one-second Chromium process sampling; it is not a Lume performance benchmark.

Public origin: https://4d3ee194969f5f51.czd5qzludboe.opentunnel.xyz. Keys remain solely in ignored private runtime state; no access-fragment link belongs in source or public manifests. Pending release steps: push this source checkpoint, refresh the committed-source/archive alias, verify its public SHA again and publish the same view/archive to Surge if existing authentication permits. The release gate stays unchanged until current-source publication evidence is complete.


## Iteration10290244-006 — avoid unnecessary wheel hit searches

Start: 2026-10-28T19:56:47.055158+00:00. End: 2026-10-28T20:09:22.517549+00:00. Iteration wall: 755.462s. Session elapsed: 4907.518s. Workers: wpt_engine implementation/TDD; apple_runtime independent raw CPU/RSS/soak review; release_integrator provenance and commit review; root remote coordination. Parallel model compute durations are unavailable.

The completed layout records whether an element can consume a wheel event. Ordinary pages avoid full-tree hit testing while fractional motion, real overflow scrollers, sticky layout and invalidation remain covered. Wheel regressions improved29/35→35/35; all303 focused wheel/raster/allocation/window checks and overflow/sticky executables pass. Seven measured stages; prebuilt-V8 incremental build4.098s. Fresh unchanged6-file WPT still1850/1850 in both native Lume and Chrome.

Native SHA `7f0cafc374cf2cf1c51f83f44eec8da7220b996ac836bef4fd6caf3687623026`: scroll p9521.973% remainsFAILED against10%, versus23.864% on the previous cumulative renderer artifact. This comparison includes the allocation change and is not an isolated wheel-only benchmark. Fixed final idle passes49CPUintervals over49.012s: p950.000%, peak1.000%, RSS151.906MiB flat. The zero percentile reflects sampler granularity.

A separate166.162s controlled20-navigation run completed21pages×80clone/Attr/import checks. Settled RSSmedian155.750→147.656MiB(-8.094MiB), live nodes1218/styles11/workers0 unchanged, no gaps/errors or forced process cleanup. Identical workload per page, with different step URLs; no tab-close, forced-GC, deviceIPA or universalRAM claim. Evidence `agent/evidence/10290244-006-wheel-routing.json`.

Task score 45.00/100. Expanded1515-variant WPT stays incomplete and f941 cohort historical; same-device IPA gates remain unverified. Continue broader failure fixes and high scrolling CPU work until the deadline.


## Iteration10290244-007 — DOM ownership, events and microtask ordering

Start: 2026-10-28T20:09:22.517549+00:00. End: 2026-10-28T20:40:00.823780+00:00. Iteration wall: 1838.306s. Session elapsed: 6745.824s. Implementation: wpt_engine and apple_runtime; independent peer review by both owners; integration by release_integrator and root. Parallel model compute time is unavailable. Native source checkpoint `4517145578ef3374cba6e699b2fe183fc25e48a0` was committed before the next candidate was applied.

This checkpoint repairs adopted/template document ownership, foreign attribute namespace metadata, live qualified-name collections, event initialization and dispatch state, and event/microtask FIFO/error reporting. Fifteen measured build/test stages passed; the prebuilt-V8 incremental build took11.092s. The exact total is714/714 targeted checks (the earlier814 summary was a counting typo). The unchanged six-file WPT subset freshly passes1850/1850 in native Lume and the installed control. Current native SHA `eac62c75a9958f9cdc26cc85d4aa54e17c4031f9c9e43915263673375626fb55`.

Additional absolute upstream evidence on this artifact includes DOM995/996 with13/14 fully passing files, events78/78 with16/16 fully passing files, and microtasks5/10 fully passing files. The complete frozen1515-variant inventory remains unchanged and incomplete. The installed Google-signed Chrome156.0.8078.48 separately failed9of12 native URL/anchor sanity checks on fresh about:blank; preserve all recorded failures and quarantine comparative retention. Downloaded official CfT archive integrity matches its Google storage ETag, but its macOS signing verification failed; no CfT execution or trust bypass has been accepted.

Task score 40.00/100. Previous wheel idle/soak points are historical for this changed executable; scroll remains a known prior failure, not a current pass. Verified public delivery adds5points: live2s telemetry, full source-archive hash, and message/response. Its last verified archive identifies source `0e5c8e5c93e92f79d8461afc54af6f16debe5327`; refresh/push the new checkpoint immediately. Same-device IPA execution remains unverified. Evidence `agent/evidence/10290244-007-dom-events.json`.


## Iteration10290244-008 — frame microtask context and Storage.key conversion

Start: 2026-10-28T20:40:00.823780+00:00. End: 2026-10-28T21:00:46.673756+00:00. Iteration wall: 1245.85s. Session elapsed: 7991.674s. Runtime implementation/TDD: apple_runtime; independent source review: wpt_engine; integration/evidence: release_integrator; remote operations: root. Parallel model compute duration unavailable.

Frame window initialization and queued microtask execution now preserve the appropriate frame realm. Storage.key follows unsigned-long WebIDL conversion, including wraparound, nonfinite values and throwing coercions. Meaningful regressions improved frame26/35→35/35 and storage20/66→66/66; the same101 diagnostic assertions passed in the installed Chrome control with clean process cleanup. This is narrow fixture validation and does not remove its separate URL-sanity quarantine.

All815 targeted native checks passed in17 measured stages, prebuilt-V8 incremental build5.106s. Immutable native `build-apple-load-20261029/lumen-frame-storage-2c27f3bb2c305b2f` SHA `2c27f3bb2c305b2fc3b79d1e257c9913380432014a5a85832306afa13441dc32` freshly passes the unchanged1850-assertion, six-file upstream WPT subset. Frozen1515-variant coverage remains incomplete and does not earn broader parity credit. Evidence `agent/evidence/10290244-008-frame-storage.json`.

Task score40/100. Current native idle/interaction/navigation-soak and same-device IPA remain unverified. Prior wheel scrolling21.973%p95 remains a historical failed10% gate, not a pass for this executable. Refresh source publication/status after this checkpoint; do not change upstream tests or hide unsupported workers.


## Iteration10290244-009 — Fetch Headers and shared text codec interfaces

Start: 2026-10-28T21:00:46.673756+00:00. End: 2026-10-28T21:07:22.953952+00:00. Iteration wall: 396.28s. Session elapsed: 8387.954s. Implementation/TDD: wpt_engine and apple_runtime; independent JS/native integration review: release_integrator and both implementation owners; root brokers execution. Separate model compute duration is unavailable.

The shared window/worker Headers implementation now validates WebIDL iterable/record conversion order, live sorted iteration, Set-Cookie handling, request/no-cors/response/immutable guards and cross-realm brands. Fetch and XHR transport preserve actual ByteString octets; replaced public header helpers cannot inject transport bytes. Review regressions cover quoted override-method tokens and overwritten Set.has.

The137-check Headers fixture improved 33/137→137/137; the170-check codec fixture improved 46/170→170/170. All1122 retained/Headers/codec checks pass across20 timed stages, with the native test_encoding assertion target passing separately. Prebuilt-V8 build 11.104s. Native `build-apple-load-20261029/lumen-headers-codec-1b9d43ed19cd616b` SHA `1b9d43ed19cd616b0029e34fde97050b86c9acbdbda16a0dca5892922df704e9` freshly preserves1850/1850 original six-file WPT assertions. TextEncoder/Decoder now share window/worker behavior, native canonical labels and one-shot legacy tables, checked fatal errors, UTF8/UTF16 streaming, BOM handling, scalar-safe encodeInto, fixed-length BufferSource validation and cross-realm brands. Legacy multibyte/fatal streaming is explicitly unsupported. The52 frozen upstream Headers variants and all1515 broader variants remain separate, unchanged inventories. Evidence `agent/evidence/10290244-009-fetch-headers-text-codec.json`.

Score40/100. Current executable CPU/RSS/soak and same-device IPA remain unverified; previous scrolling failure stays historical. Preserve reference quarantine and rerun the frozen Headers category on this immutable executable before claiming upstream improvement.


## Iteration10290244-010 — current native resource acceptance

Start: 2026-10-28T21:07:22.953952+00:00. End: 2026-10-28T21:28:11.356457+00:00. Iteration wall: 1248.403s. Session elapsed: 9636.356s. Runtime owner: apple_runtime; independent integration/adjudicator review: release_integrator; root brokers measured execution. Separate model compute duration is unavailable.

Native `1b9d43ed19cd616b0029e34fde97050b86c9acbdbda16a0dca5892922df704e9`: final idle p95 0.992% over 49.023s with 49 valid CPU intervals; scroll p95 23.024% against the unchanged10% ceiling, accepted=False. Navigation soak accepted=True; settled RSS medians 154.016→147.062MiB (growth -6.953MiB). Raw phase timing, one-second samples, CPU counters, fixture hashes, process identities and natural cleanup reproduce independently.

Scroll workload duration 116.064s; navigation workload duration 166.543s. Both raw reviews and all compiled-source hashes were rechecked immediately before this documentation checkpoint. Evidence `agent/evidence/10290244-010-current-native-resources.json`. Score **50/100**. The score remains below the98/100 whole-task goal; full frozen priority coverage, reference validity and same-IPA execution remain incomplete. This tenth iteration requires an immediate committed-source push/archive and live status refresh. No native code or threshold changed.

The official Google stable DMG diagnostic is preserved at `output/wpt-10290244/reference/official-signed-chrome-dmg-supportdoc-10290244/receipt.json` (receipt SHA `245ea39c0a1513860188dbd45329548bb2f5c036d3b1473e3015048b7244e4f8`). The278,915,685-byte download SHA `fb63667b7e800fc63f906de5fb40c15337f5c97c92fcd94fbb0a7b75f44b8600` passed disk-image integrity, but mounted-app deep strict signature validation rejected Finder metadata. Its owned read-only mount was detached. The app was not copied or executed; no trust or metadata changes were made, and installed-reference quarantine remains in force. This diagnostic earns no parity credit.
