## Linux engine handoff — session 4, 2026-10-29

The old session-3 handoff is archived verbatim at agents/archive/20261028-session3-linux-engine-handoff.md. Standing instructions above this section remain unchanged.

### Result, source and launch

Candidate168 is accepted ONLY as a **qualified experimental checkpoint**, not a regression-free or production-parity release. Six substantive candidates163–168 were evaluated; every whole-goal score remains **19/100**, requested98 unmet.

Tested engine commit: **fbbf127b9e082d2805b750e5238a77d7f8c30aa2**. Private branch: **gpt-6-astra-engine-lead-linux-conformance-20261029-0647**. Immutable executable: **build/codex-native/lumen-astra-i168**, SHA256 **127ec518e9522352aa58d91e3807671c65f593a06f68c95bc355f74012a49052**. The standard build/codex-native/lumen matched it at the final candidate build. No desktop/app restart or user launcher configuration change occurred.

All605 recorded inputs match the tested commit. Canonical input digest: caddfbad3c0df624b82cd3a846ce42475d49a6544ec5f53cb136e58753a4bdc7. The source-manifest-before.json file hash is separately f0702523e59d2ce1201d39ad03464acea5b7ee0342725f066bfcbc12c2d6aa16. agent/validation/iteration-168/accepted-git-map.json has SHA25671e6a591718075225e702e54374c6257224b19f4cf70c57d734e4dc04e31945b.

Starting HEAD879c633714d99943c0ec6d817eca12086d776660 was checkpointed with the exact latest request at df053adb69af76da0b8ae5ba092fc3cf2af44b41 before the new branch. agent/task.md remains8029bytes/SHA2561fd961d4427df1b11499ac3d560d0541dae84900f189102278d66c13724d47f4. Seven original untracked fixtures remain unchanged and untracked. Later local docs/public-source/showcase commits must not be confused with the tested engine commit.

Supported launch:

    cd /mnt/p3/project/browser/lumen-android/lumen-android
    LUMEN_ICU_DATA="$PWD/build/codex-deps/v8/bottle/v8/15.6.75.8/libexec/icudtl.dat" ./build/codex-native/lumen-astra-i168 https://example.com/

### Changes and candidate history

- src/css/style.c, src/css/css.h, src/js/prelude/03-element.js: final writing-mode/direction/text-orientation prerequisites; logical sizes/edges/borders and corresponding inheritance across differently oriented parents; computed getters/incremental invalidation; physical border CSS-wide resets; supported actual-font length paths.
- src/app/main.c: stylesheet request context and real completion in the owning live page/frame; frame restyling; pending-link lifetime through navigation/removal/adoption/self-navigation. Retained nodes are released before Document destruction. Detached links retain genuine callbacks; connectedness gates stylesheet attachment.
- src/net/fetch_metadata.c, src/net/http.c, src/net/net.h: stylesheet Fetch Metadata origin/site/trust/redirect-taint handling. A fresh no-PSL configuration passed195 conservative controls; no Android claim.
- src/text/font.c, src/text/font.h: actual zero-glyph advance for ch, valid zero metrics, final font prerequisites/load invalidation, and FreeType memory-face buffer ownership.
- src/layout/inline.c: both break-spaces fit decisions retain preserved-space width without changing normal/pre-wrap behavior or reverting correct ch metrics.
- Ordinary regression controls: tests/test_fetch_metadata.c, test_logical_size.c, test_font_units.c, test_break_spaces.c and CMakeLists.txt.

163 was a focused accepted checkpoint (138 native entries).164 was rejected for294px→300px logical inheritance.165 was rejected at138/139 for border-width:initial returning7 instead of3.166 passed139 normal entries but was rejected for a real1056-byte production font-buffer leak.167 repaired ownership and passed native instrumented74-font controls; its later rendering regression reproduced0/207/0/207 differing pixels in162/167/162/167.168 repaired that actual line-fitting defect.

Fresh normal168 passed **140/140 native entries**, including22 preserved-space,74 actual-font and186 retained border controls. The exact upstream rendering regression returned PASS/0 differing pixels. Focused source/gate records retain their original candidate labels: unchanged166 iframe metadata58/60 (two parser load notifications unsupported), lifecycle41/41 and detached-link12/12; fixed font4 is6/20 versus Chromium14/20. Overlapping authored/focused groups are not additive WPT totals.

Native instrumented font proof is **167 with unchanged font.c only**, exit0/no diagnostics. Instrumented browser41/12 markers were reached but actual children exited-9 with post-marker loader/LLVM diagnostics; a separate cleanup-timeout60 replay retained the same incomplete shutdown. Wrapper0 is not a clean child exit. No further concrete engine ASan/UBSan fault signature was found after the repaired leak, but absence is not proved. New168 inline code lacks that sanitizer coverage. No clean whole-browser sanitizer or bounded font-cache memory claim.

### Final comparison and retained uncertainties

The unchanged fixed selected corpus of4,200 variants started01:08:56.667 and drained01:52:59.457061 UTC. A full pass requires harnessOK, at least one registered subtest, and every registered subtest PASS.168: **1629/4200 fully passing;199362/215657 subtest PASS**. Fresh162: **1628 fully passing;199263/215657 PASS**. Exact identities: **102 PASS gains,3 prior-PASS losses,29 other status changes,zero added/missing registrations**; two full-variant gains/one loss. Registered PASS can occur within incomplete harnesses; observed changes are not all independent causal improvements.

All54 lifecycles cleaned:53 actual native exits0 plus the same IndexedDB40-second deadline-9 present in baseline. Source/binary/protocol remained invariant. Dated Chromium is separate:3534/4200 full and209574/217350 PASS, not the native denominator.

Original losses remain:
1. /referrer-policy/gen/srcdoc-inherit.meta/strict-origin-when-cross-origin/a-tag.http.html: same-HTTPS4/4→3/4, undefined.referer TypeError (missing result/header object, not an observed wrong Referer value).
2. /html/webappapis/scripting/processing-model-2/unhandled-promise-rejections/promise-rejection-events.html: delayed queueTask/many-microtasks handling PASS→unexpected-unhandledrejection FAIL; both native broad harnesses incomplete.
3. /scroll-to-text-fragment/find-range-from-text-directive.html?17-20: fourth PASS→NOTRUN behind an earlier TIMEOUT.
A hidden nested-media case also worsened from completed rejection-wait FAIL to outer TIMEOUT.

Exactly16 isolated controls used162→168→162→168 per exact variant, unchanged settings/caps: referrer4/4 all; promise targetPASS all but every harness39PASS+4TIMEOUT; media same completed FAIL all; scroll allTIMEOUT1PASS+1TIMEOUT+2NOTRUN. The fourth scroll case was not shown passing. These controls do not recreate batch history or identify cause; original broad rows remain. The proposed original expanded-prefix961 history pair was **not executed**, because a bounded cleanup route was not established through the permitted public interface. The two help usage errors do not show that cancellation is absent. No protected source/guard was bypassed.

Final rendering100: **38 PASS/60 FAIL/1 UNSUPPORTED/1 TIMEOUT**, all statuses match dated162; the167 defect is repaired.198 actual native exits0;197 successful800×600/DPR1 captures. Dated Chromium94/5/0/1.

Final v2 performance:21/21 balanced serial trials,7 per162/168/Chromium, actual **9395 checks each**. Median ms:

|Phase|162|168|Chromium|
|---|---:|---:|---:|
|DOM|40.622|38.418|10.0|
|Style/layout|339.506|332.838|55.9|
|Grid|134.530|137.891|47.2|

All trials remain, including227.386ms168 Grid. Ranges overlap; no general speedup/stable-regression claim. Earlier167 interim21-trial **4275-check DOM/style-only** measurement is a separate protocol; its inherited9395/Grid description was withdrawn. Final v2 used CPU-only benchmark modes at748×596 CSS pixels/DPR1. Protocol SHA256:6b064fce68a034e4bbe56338a4722c3c43945d701dc52aabe675de50bb1338f2; workload SHA256:730267e57bea73cd13dc95bf7e3dcc636a5bdffa9c5092a037e1bd2ea259282b.

### Source closure and public delivery

Archive SHA256 **bddd9f93afca4d8fbbf036229c1bcbe2f8e20aaec53caf48c73d6f0457458277**,4451575bytes,699 safe files:698 exact repository blobs/modes plus metadata, all605 inputs exact.

Fresh extraction configure/build **02:12:22.849193→02:13:19.936005 UTC**,57.087seconds/jobs2/600-second cap: lumen plus test_break_spaces/test_font_units built; **22/74 controls actually passed**, no skip. All699 extraction bytes unchanged;146 compile-command source entries internal to extraction/generated build. Coordinator/root/monitor absent, no residual jobs.57 one-second samples: CPUmedian135.0005%/max196.002%, sampledRSSpeak697.598MB/PSS514.745MB; interval sampling can miss short processes and RSS can double-count shared pages.

Archive-built lumen SHA256 **b498a8dae4cd0bc4896304b5aace8c4176bc6476f899cf9667b63152797910df** is distinct from measured168. No WPT/render/performance results were relabeled. This is source closure with declared external clang/V8/wgpu dependencies, not hermetic/bit-identical/Android/full140 or browser-runtime coverage. An optionalpsutil preparation failed before directory/child creation; actual worker used standard-library/proc without dependency installation.

- Public source-only branch gpt-6-astra-pro-builtin-agents-rubywebterm-linux-engine-experimental-source-20261029 at https://github.com/ecooxai/lumen: independent root commit **2e04f123b584b8b6acce08a1e6416f1bf2793845**, verified02:16:30.732877 UTC,699 files/no private ancestry.
- Showcase https://ecooxai.github.io/lumen/ payload **b8a0137382fe9cac3b08a267b7bf6435052eb261**:12 plain URLs HTTP200/exact sizes/hashes by02:24:09.010003.
- Receipt-only showcase commit **8a77e748b38ce6020486496471c2005dbba98960**. publication-receipt.json served HTTP200 at02:28:09.906806,18022bytes/SHA256 **ab011ccf140219cf8358c28ce4d91fa4e42efa8f656a9b2ce9b37bf70473de47**.

Five actual168 PNG captures drained02:04:47.359604; two34-second510-frame/15fps/960×720(4:3) videos drained02:15:17.552463. Credited stills preserve native800×640 pixels. Capture wrapper1/missing-marker and unavailable actual child exit remain; artifacts are not test PASS claims.

Public payloads exclude private Git ancestry, agent/task, AGENTS/devsummary, private harnesses, profiles, credentials and inbox. Full private history/denied comparator exports remain blocked by auto-review; no alternate route was used. FileHarbor403/Surge authentication were not successful deliveries.

### Reproduction and next-agent priorities

Session root below is agent/validation/linux-wpt-20261029-session4. Exact executed argv/source/binary/guard/cleanup metadata are in performance-v2-final168/, render100-final168/, final-native4200/ owner/launch/completion receipts. Key seals:

- final-native4200/independent-summary.json:8db953cb4461183e90e67f5d9b44fca914e936f38d27b61a5311a06cb60528eb.
- final-native4200/identity-comparison.json:248aab290f6bad70d5e2a43319a1f5918335de6b35424d290f4bdc08344b4877.
- final168-validation-recommendation.json:269cc512e7d9753e307799d4dd596aaa7759535591522ff86bd2566a93a9800b.
- archive168-closure-independent-review.json:78127ac050b8d87548ea31812db56776dc227c648b93b24fd76eae15277f8bcd.
- publication168-delivery-independent-review.json:499e74447d1c1a61732b7a191fc49179a13d8638bb62333089446252ea367caf.
- history-prefix961-unexecuted/plan.json:42084cc6c71c8384c18f896e863efd859f5f92f66f73a393684fc066cd3c31b7.

WPT checkout /mnt/p3/project/browser/wpt is pinned at0e1c0bce3defd59a76cf2f351c7da5c5a42b26fb. Chromium150.0.7871.114 is /home/a/.local/archpkg/pacman/usr/bin/chromium; use recorded launch hashes. Never overwrite immutable candidate binaries/output directories.

Build via python3 tools/codex_compile.py python3 agent/validation/build_candidate.py --iteration NEW_ID --jobs 2 --startup. Tests retain1s owned CPU/RAM evidence. Original15-second168 compile had1s shared CPU/2s liveRAM but no separate owned-compile RSS/PSS CSV; do not invent a peak. Startup/subsequent tests have their own1s artifacts.

Existing performance CLI: agent/validation/linux-wpt-20261028-session2/performance-final/run_performance.py or run_performance_v2.py --baseline BIN --final BIN --chromium BIN --mode measure --output DIR, through quota wrapper. Protected helper/workload source is unnecessary. Fixture CLI: agent/validation/run_guarded_fixture.py --fixture ABS --marker MARKER --output NEW_ABS --binary ABS. Preserve guarded interfaces, selections/caps, fresh profiles/audits, cleared LUMEN_WPT_CTRL/PRE and source/cleanup seals. Do not read/rebuild/rehash protected guard libraries/private capture helper/auth records.

External actual fonts (not bundled): Ahem.ttf21768bytes/SHA256b719ecb31c5b21fc573c03f6421c74ac63c271a5a3ff841e34f9705fb94b8448; ChTestZeroWidthZero.woff1056bytes/SHA25645280eaf99497623d086bb218e8faf059e3034b675985d04139077393a99f22f. Configure LUMEN_TEST_AHEM_FONT/LUMEN_TEST_ZERO_FONT. Missing configuration is skip77, not coverage; actual74 ran in normal168/instrumented167/source closure.

Future minimum-size work must retain CSS transitions AND Web Animations (auto/underlying/neutral, directions, allow-discrete, cascade, axes/writing modes/aspect ratios), sizing38/six-URL animation holdout and tests/browser/sizing-auto-interpolation-regression.html; prior native/sizing-only success missed120 interpolation losses. Preserve real delayed-response/removal/adoption/error/detached-link and font/whitespace oracles.

First unfinished priority is a documented bounded batch-history reproduction for the three original broad losses. Other limits include parser stylesheet event ordering, logical/font pseudo/math semantics, Grid/flex/text/sizing, all/revert/sideways and border shorthand/radius completeness, invalid padding handling, Unicode-range/family selection, context-free ch callers and incomplete browser-sanitizer shutdown. Do not add superficial callbacks or revert correct metrics to hide layout errors.

### Accounting and closing boundary

Original start **2026-10-28 22:46:53 UTC**; normal deadline01:46:53; necessary repair/validation extension hard deadline02:46:53. Documentation cutoff **2026-10-29 02:33:29 UTC**, **226.6 minutes (3h46m36s)**, is an as-of point, not a future closing claim. Later final Library/chat records actual closing separately. Original six candidate intervals remain in iteration records/devsummary; overlapping agents are not summed. Root/engine_lead_20261029/conformance_20261029/progress_20261029 used same-capability GPT-6 Astra built-in agents.

Feedback polling ended02:33:19.619605; lastpoll02:33:20.969460 had zero messages. Progress froze02:33:20.969486, session_active=false/agent_session=closed; dashboard/tunnel telemetry remain. No new tests or engine edits occurred during final docs. The later private docs commit and preservation checks are recorded separately at agent/validation/linux-wpt-20261029-session4/final-docs-commit-receipt.json.
