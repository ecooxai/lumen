

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


## Final scoped WPT and reproduction checkpoint 2026-10-28T21:32:20.724215+00:00

Accepted source remains `436916ceb3b32630ed251bb666b02fa39a3cd0c8`, native `1b9d43ed19cd616b0029e34fde97050b86c9acbdbda16a0dca5892922df704e9`, iteration010 score50/100. The current immutable priority cohort records207/1515 variants;1308 encoding variants remain unrun. Raw158 and separately adjusted160 of207 recorded files fully pass. Headers raw499PASS/2 fixture failures remains immutable; restoring one pinned missing helper and replacing exactly two rows in the separate view yields539PASS/0FAIL and26/52 fully passing files, with13ERROR/13TIMEOUT still present. No denominator, browser assertion or score gate changed.

Exact matrix: `agent/gpt6-astra-pro_builtin-agents_devinmac3d_wpt_matrix_10290244.md` and JSON companion. Provenance `agent/evidence/10290244-final-wpt-provenance.json` binds source receipts, generator and reviewed wrappers. The final functional launcher took304.226s and exited naturally; its duration exceeds five minutes because sequential upstream cases retained fixed15s/65s timeouts and unsupported-context outcomes, not because native compilation slowed. The current prebuilt-V8 build was11.104s. Final115s scrolling and165s navigation workloads, one-second raw telemetry and independent adjudication are recorded in010; current scroll p95 remains23.024% against10%, while idle and soak pass.

The measured priority wrapper is preserved with its external-SIGTERM cleanup limitation documented. Reviewed runtime follow-up supplies exact MessagePort transfer/stream-state RED tests and Apple dependency gaps. Publication and handoff tools are now included in the committed source, with private runtime state excluded. Session elapsed 9885.724s; parallel model compute durations remain unavailable. Final branch push/archive/public proof and durable delivery receipt follow this checkpoint.


## Integration handoff checkpoint 2026-10-28T21:32:30.811977+00:00

Consolidated outdated task-owned instructions and archived both previous documents as ['agent/archive/AGENTS-20261029-053230-10290244-checkpoint.md', 'agent/archive/continue-20261029-053230-10290244-checkpoint.md']. Accepted iteration 10290244-010, source base `952ed465f803118bc6ee8b4c31c82bea7ee1c0b3`, native `1b9d43ed19cd616b0029e34fde97050b86c9acbdbda16a0dca5892922df704e9`, score 50/100. Source hashes and immutable artifact were rechecked; no native code, score or raw WPT result changed. Session elapsed 9895.812s. Separate model compute time remains unavailable. Refresh/push source publication after this documentation commit.

## 102900614-001 — baseline and task setup

- Start `2026-10-28T22:14:41.459Z`; end `2026-10-28T22:21:43.088519+00:00`; elapsed `421.630s`; total session wall time `421.630s`.
- Score **50/100 inherited**, no fresh acceptance earned. Goal98/100.
- Read previous handoff and preserved prior untracked work. Created isolated task branch; archived old instructions and saved full user request.
- Historical WPT1850/1850 assertions in6files;158/207 observed priority variants fully pass,1515variants frozen; full WPT rate unknown.
- Root and all3built-in workers use identical inherited model/reasoning. Child MCP hung; root now brokers remote I/O. Model compute duration unavailable; parallel wall times are not summed.
- No build/test in this setup iteration; test/build timing and resource evidence will be appended by stage.

## 102900614-002 — task-scoped progress and truthful WPT coverage

- Start `2026-10-28T22:21:43.088519+00:00`; end `2026-10-28T22:39:39.725760+00:00`; elapsed `1076.637s`; total session wall time `1498.267s`.
- Score **50/100 inherited**; no changed executable is promoted and no native performance points are added.
- Regression first: new-task identity and stale-receipt isolation **1/3 → 3/3**, with **9/9** total progress checks green. Monitored stage durations: RED `1.059s`, GREEN `1.038s`. Short unit checks have insufficient CPU intervals for a steady-state performance claim.
- Reused existing MVC progress stack with explicit task identity, isolated message storage, raw priority pass denominator, inherited-score label, dynamic status validation, and exact private-key/reachable-history publication checks. Full WPT rate remains unmeasured.
- Initial commit pager/whitespace interruption was infrastructure only; exact user text and archived bytes retained. No separate timing was captured, and no acceptance points were awarded for the repair.
- Evidence: `agent/evidence/102900614-002-progress-task-isolation.json`. Model compute duration unavailable; parallel wall times are not summed.

## 102900614-003 — native ArrayBuffer transfer and unchanged upstream validation

- Start `2026-10-28T22:39:39.725760+00:00`; end `2026-10-28T23:02:31.987958+00:00`; iteration `1372.262s`; total session `2870.529s`. Parallel worker times are not summed; model compute duration unavailable.
- Current acceptance **40/100**. Prior **50/100** remains historical. Current soak and external delivery are pending on the new executable; no functional regression is implied by the evidence reset.
- Native regression **15/68 RED → 68/68 GREEN**; all **826/826** transfer, codec, clone, worker lifecycle/security and port regression checks pass. Build `6.083s`; full regression stage `14.989s`. Existing prebuilt V8 reused.
- Original upstream **1850/1850 assertions across6/6 files** in `9.236s`; targeted encodeInto window/worker **222/222 across2/2** in `1.193s`. Full WPT and Chrome parity remain unmeasured.
- Current-artifact resource run `116.233s`: final idle p95 `0.0%` with stable `150.656MiB` RSS; fractional scrolling p95 `23.054%` still exceeds10%. Subsecond regression runs do not establish steady-state performance.
- The stale official fixture was repaired before rerunning the two pending variants; the accepted original six-file result was preserved. Source commit excludes unrelated GPU candidate work.
- Evidence `agent/evidence/102900614-003-message-transfer.json`; immutable native SHA-256 `733943aa9496d1342dd9d90a76146d0433b24c9ef7848df5860881e57c2ac848`. MessagePort lifecycle, full Streams and same signed IPA execution remain outside this acceptance.

- Checkpoint003 commit whitespace gate caught four trailing spaces on a blank line of the targeted-run helper. Removed that authored-source defect; execution and committed helper hashes are both recorded. No test or production behavior changed, and no verification was rerun or relabelled.

## 102900614-004 — bounded GPU upload reuse and callback lifetime repair

- Start `2026-10-28T23:02:31.987958+00:00`; end `2026-10-28T23:11:31.564421+00:00`; iteration `539.576s`; total session `3410.105s`. Current acceptance **40/100**, prior50 historical; accepted native remains733.
- First b62 candidate aborted after `16.463s` because this pinned wgpu backend exports an unimplemented map-state query. A stricter regression reproduced the abort; reference-counted atomic completion tokens replace that query. The failed candidate and original receipts remain preserved.
- Repaired candidate2dac: normal **68/68** and AddressSanitizer **68/68** pass; build/test stages total `4.439s`. Short1Hz-monitored checks do not establish steady-state CPU usage.
- Unchanged controlled scroll run `116.078s`: meanCPU `18.093→14.151%`; p95 `23.054→18.997%` (**17.598% lower**), still above fixed10%. Final idle p95 `0.993%`, settledRSS `153.188MiB`.
- `656000` CPU-frame page pixels compare identically and all1875 fractional wheel inputs retain exact final position. Dynamic toolbar is explicitly excluded. The native-window capture metadata error remains separate; no invented scanout/PID proof.
- Optional scratch-only preflight22:45:20.172–22:45:21.184Z failed from missing mirror header/ps, with no Mac mutation or performance credit.
- Only GPU source/test and explicit reproduction helpers are committed; stream/raster working edits are excluded. Evidence `agent/evidence/102900614-004-gpu-upload.json`. Parallel wall time is not summed; model compute duration unavailable.

## 102900614-005 — live task progress and private message delivery

- Start `2026-10-28T23:11:31.564421+00:00`; end `2026-10-28T23:14:40.626860+00:00`; checkpoint `189.062s`; total session `3599.168s`. Current acceptance **45/100**; release/progress5points now supported. Prior50 remains historical, current native733 unchanged.
- Verified HTTPS `https://dd1cacbcfa84c17d.q4d3dgcpeeoq.opentunnel.xyz`, WebSocket TLS/handshake, **2.004s** telemetry cadence and round trips `[69.73, 68.219]`ms. System metrics and native test-process measurements remain distinct.
- Full **9504604byte** public source archive verified at commit `d665c4920c0c2c0aaf9e22e9945342c772423f1f`, SHA-256 `313c1fc0874beddcd6e753aec77358a2aa56a1abf9f26b0d47c53ab925c1e09d`. Refresh required after this checkpoint; this receipt is never relabelled as newer source.
- Authenticated task message reached the local inbox and its agent reply returned through the public endpoint. No access key or fragment is printed, committed or published. Private user access HTML0600,1155bytes was transferred with RSA-OAEP, reconstructed with an identical hash, and delivered privately by root; its capability and storage identifier are omitted from public evidence.
- Task tunnel failure traced to launchd definition lacking isolated XDG paths. Only the proven owned profile service was stopped; a detached task relay with explicit DATA/CONFIG/CACHE/STATE restored TLS. Default/other profiles remained untouched.
- Actual probe timestamps are in evidence `agent/evidence/102900614-005-progress-delivery.json`; earlier parallel stages are not added to elapsed wall time. Source archive and Surge refresh remain active work, not goal completion.

## 102900614-006 — independently qualified official Chrome reference

- Start `2026-10-28T23:14:40.626860+00:00`; end `2026-10-28T23:17:54.579818+00:00`; checkpoint `193.953s`; total session `3793.121s`. Score remains**45/100**; no native or expanded-suite promotion.
- Official signed Chrome `156.0.8078.74` SHA-256 `0a8cb6eba2bee365c01fb0612f64f1d15855fd499dd54625ced1fe50215e5fb4` passed strict signature, Google publisher and Gatekeeper after exact FinderInfo-only repair on an isolated copy. Payload/quarantine/all other attributes preserved; original DMG untouched. Verification `25.858s`,1Hz observations.
- Native CDP standards probe remains**3/12 pass,9fail**. Independent direct dump-DOM in two fresh profiles/environments remains**1/6 pass,5fail** per run, with identical DOM hash and natural group cleanup. Durations `[1.293, 1.063]`s. No loader/Chrome environment overrides or managed policy names observed.
- Exact Chromium revision confirms spaced-host rejection is disabled bydefault and file-localhost canonicalization is an open implementation TODO. Host/port compatibility behavior is documented in KURL. Standards expectations and WPT assertions remain unchanged.
- Reference identity and reproducibility are now qualified for fresh paired runs; standards failures stayFAIL. Historical quarantine/reports are preserved byte-for-byte, and no earlier comparison is retrospectively accepted. Sources and full rationale: `agent/evidence/102900614-006-chrome-reference.json`.

## 102900614-007 — shared Encoding Streams candidate

- Start `2026-10-28T23:17:54.579818+00:00`; end `2026-10-28T23:26:56.745213+00:00`; checkpoint `542.165s`; total session `4335.286s`. Current accepted score **45/100** and733 artifact remain unchanged; this is a verified functional candidate.
- Encoding Stream fixture **10/74 RED →74/74 GREEN**. Transform lifecycle fixture originally5 Node-reference checks pass,6 Lumen assertions fail and2 Lumen cases time out; unchanged fixture now **13/13** passes. Unchanged writable checks **26/26** and all custom gates **946/946** pass. Build `5.118s`; full build/regression `19.443s`;1Hz telemetry, existing matched prebuilt V8 reused.
- Original upstream **6/6 files,1850/1850 assertions** remain passing on80a in `9.25s`. The fresh pinned stream diagnostic is **23/26 fully passing variants**,338 passed assertions,8 failed assertions and1 zero-assertion timeout. Both IDL and backpressure files pass.
- The two non-UTF8 failures need legacy incremental decoding; realms.window times out at synchronous srcdoc frame loading. These failures remain unchanged and visible. All1515 variants remain frozen; this26-variant subset is not a full WPT rate or a replacement for the historical207-row cohort.
- Source/fixture/compiled-manifest identities are checked against this isolated commit. Raster, legacy-codec and frame-lifecycle working edits are excluded. No733/2dac resource numbers are attributed to80a. Evidence `agent/evidence/102900614-007-shared-streams.json`.

Checkpoint007 commit infrastructure follow-up `2026-10-28T23:29:47.289388+00:00`: Git's whitespace check found original CRLF CSV rows and Node assertion whitespace in exactly three measured RED copies. Their hashes were independently matched to original raw files and staged bytes, then only these copies were excluded from the whitespace check. All authored source remains strictly checked; original bytes, test results and iteration count are unchanged. See `agent/evidence/102900614-007/raw-byte-preservation.json`.

## 102900614-008 — exact span blending and bounded glyph reuse

- Start `2026-10-28T23:26:56.745213+00:00`; end `2026-10-28T23:37:49.194609+00:00`; checkpoint `652.449s`; total session `4987.736s`. Accepted score **45/100** and native733 remain unchanged.
- Fractional rectangle oracle **93/93**, including12,000 independent coverage/alpha/clipping cases. Glyph reuse frozen fixture **95/97 RED →97/97 GREEN**, including7,168 independent text cases. Existing V8 binaries reused; each build/test stage monitored at1Hz. Short checks provide no steady-state CPU credit.
- Same-stream paired native scroll p95 **20.175% →16.935% →12.060%**; glyph mean **9.912%**, final idle p95 **0.991%**, settledRSS **153.203MiB**, stable across the measured idle phase. Fixed **10% scrolling gate still fails**. Final workload117.419s; exact page pixels, scoped native-window capture and GPU errors all pass.
- Source and compiler dependency comparisons differ only in raster.c; glyph reference bytes exactly equal the prior measured span binary. Persistent font-owned glyph allocations were independently reviewed; the bounded lookup cache does not own/free them.
- Combined candidate4773 remains unpromoted pending fresh combined standards/resource/soak checks. Prior failedGPU receipts remain preserved; no historical performance numbers are transferred to a new binary. Evidence `agent/evidence/102900614-008-raster-glyph.json`. Parallel worker wall times are not summed.

## 102900614-009 — native incremental legacy decoding

- Start `2026-10-28T23:37:49.194609+00:00`; end `2026-10-28T23:47:18.582930+00:00`; checkpoint `569.388s`; total session `5557.124s`. Accepted **45/100**, artifact733 unchanged.
- Frozen legacy fixture **14/178 RED →178/178 GREEN**; codec170,streams74,transfer68,document78 and existing native encoding/encoder/URL gates remain green. AddressSanitizer/UBSan **459,119 cases** pass with44-byte persistent decoder state. Build9.106s; all build/test stages20.3s under1Hz monitoring, reusing prebuilt V8.
- Exact050b original upstream **6/6 files,1850/1850 assertions** pass in9.234s. Fresh pinned diagnostic **90/103 variants fully pass (87.379%)**: streams25/26,core65/77;23,408 passed assertions,235 failed assertions,2timeouts,**1,412 unrun of1,515**. Full official WPT rate remains unknown.
- Known iframe/body/meta/XHR failures and fatalUTF8 streaming cases remain visible. No historical207/80a measurements or later frame improvements are mixed into050b. Source/receipts `agent/evidence/102900614-009-legacy-codecs.json`. Current performance/soak and same-IPA gates remain pending.

## 102900614-010 — queued child-document completion

- Start `2026-10-28T23:47:18.582930+00:00`; end `2026-10-28T23:51:42.000312+00:00`; checkpoint `263.417s`; total session `5820.541s`. Accepted **45/100**, artifact733 unchanged.
- Frozen frame regression **2/7 RED →7/7 GREEN**; existing load order14, microtask realm35, native frame-removal/window-lifecycle gates, streams74, transfer68 and legacy178 pass. Build4.106s; nine build/test stages16.046s under1Hz monitoring, with prebuilt V8 reused. Short tests do not establish performance.
- Exact4d81029 upstream **26/26 Encoding Streams variants and358/358 assertions** pass in13.355s, including the formerly timed-out iframe realm cases. This subset does not establish a full WPT passrate; other groups are recorded separately. No original1850 rerun on4d is claimed here.
- Initial about:blank remains synchronous; child completion now waits for its task and retains parent blocking/removal/replacement guards. Dead or closed realms do not schedule immediate completion loops. This is not comprehensive asynchronous frame parsing/navigation. Evidence `agent/evidence/102900614-010-frame-completion.json`.

## 102900614-011 — Bounded HTML encoding metadata candidate

- Start `2026-10-28T23:51:42.000312+00:00`; end `2026-10-28T23:56:23.528969+00:00`; checkpoint `281.529s`; total session `6102.07s`. Accepted **45/100**, artifact733 unchanged.
- Frozen real-HTTP metadata checks **9/29 RED →29/29 GREEN**; legacy178, streams74, transfer68, codec170 and native document78 remain green. Build1.128s; ten build/test stages12.175s under1Hz sampling with existing V8 reused. Supplemental ASan exposed a pre-existing tokenizer EOF underflow; its failure remains explicit.
- Pre-existing bogus-comment EOF pointer underflow for exact <! causes SIZE_MAX append. Frozen prescan29 passes, but this artifact is not sanitizer-clean; a separate tokenizer correction is required. Source and all relevant RED/GREEN/failed receipts are preserved in `agent/evidence/102900614-011-html-meta.json`. No safety, GUI resource or full-WPT promotion follows from these functional gates.
- Bounded1024-byte HTML prescan handles metadata attribute ordering/duplicates, pragma rules, labels, comments and XML declaration fallback. The parser restart and parent encoding policy remain separate.

## 102900614-012 — Body and frameset Window handler aliases

- Start `2026-10-28T23:56:23.528969+00:00`; end `2026-10-28T23:57:08.141365+00:00`; checkpoint `44.612s`; total session `6146.682s`. Accepted **45/100**, artifact733 unchanged.
- Frozen body/frameset checks **5/45 RED →45/45 GREEN**; separate borrowed-Window regression **3/4 →4/4**. Existing frame7/14/35, native window445, transfer68, streams74 and legacy178 pass. Build3.091s; all ten stages16.126s. Qualified Chrome actually passes43/45; its pageswap/pagereveal alias failures remain visible.
- This exact historical artifact still contains the tokenizer bounds failure recorded in011. Source and all relevant RED/GREEN/failed receipts are preserved in `agent/evidence/102900614-012-body-window-handlers.json`. No safety, GUI resource or full-WPT promotion follows from these functional gates.
- Use private shared Window records for cross-realm handler identity, existing event-listener activation order, private special-event brands and the owner document’s existing inline-handler CSP/sandbox compiler. Blocked attributes preserve valid prior handlers; syntax failure clears them. Parsed body aliases hydrate before author scripts.
