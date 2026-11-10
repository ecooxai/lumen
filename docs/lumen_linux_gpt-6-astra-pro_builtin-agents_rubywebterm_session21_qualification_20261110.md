# Lumen Linux — accepted candidate 2105

**The selected three unprefixed border-radius parsing/computed-value WPT files pass 48/48, up from 21/48.** The broader frozen sample now passes **23,365/26,245 assertions (89.02648%)**, with **820/1,248 complete entries**. This release also repairs the ordinary MutationObserver callback-exception reporting realm and preserves every previously passing registered assertion.

The frozen sample contains 1,248 test entries/URLs backed by 1,247 unique source files; entry variants retain their original identities. This is a measured scoped checkpoint. Whole-WPT coverage, Chromium-wide parity and the requested overall 98/100 goal have not been achieved. Dedicated URL conformance work remains deferred. Standalone ECMAScript conformance belongs primarily to Test262 and is outside these WPT counts.

Prepared by **GPT-6 Astra Pro / built-in agents / RubyWebTerm** on 10 November 2026. Session start: 11:52:26 UTC; normal deadline: 16:52:26 UTC. This report was prepared 276.57 minutes after session start. Parallel stage durations are not summed as wall time.

## What changed

The radius implementation now retains separate horizontal and vertical components for all four corners. Parsing validates a complete value before changing style state, expands shorthand into the physical corners, preserves ordinary priority/removal/order behavior and returns real native computed values through the existing getter. Native style ownership, copying and destruction account for optional vertical storage.

Rendering uses the two axes for elliptical border geometry and clipping, resolves horizontal and vertical percentages against their respective dimensions, preserves overlap normalization and inset geometry, and includes vertical-only changes in the linked application's damage/signature path. Fixed-position descendant hit testing and existing affine/rectangular clipping controls were repaired together. This is actual style/render/hit behavior, not a JavaScript string façade.

The adjacent typed-length validation and bounded mask repair remain included. Within the existing background-colour mask approximation, intrinsically sized image masks use one tiled display item for absent or initial-resolved repeat, repeat, or repeat repeat, so a 1×1 mask covers the intended box. Explicit no-repeat/other authored modes and fitted contain/cover retain their prior paths. There is no per-tile allocation or claim of complete CSS Masking conformance.

Candidate 2105 changes only **src/js/prelude/01-events.js** relative to accepted 2104. When a MutationObserver callback throws, the engine uses the existing same-origin native creation-realm lookup to invoke that realm's private exception reporter, falling back to the current realm. Successful callback delivery and the native realm/security-token contract remain unchanged. No HTML/XML parser, outerHTML reaction or broad observer rewrite is included.

## Measured results and scope

| Scope | Earlier result | Accepted 2105 | Chromium 150.0.7871.114 |
|---|---:|---:|---:|
| Three selected unprefixed radius parsing/computed files | 21/48 | **48/48** | 48/48 |
| Separate prefixed radius companion | 20/25 | **25/25** | 24/25 |
| Frozen expanded cohort, complete entries | Accepted 2007 replay: 813/1,248; 2104: 819/1,248 | **820/1,248** | Original full: 1,060/1,248 entries |
| Frozen expanded cohort, passing assertions | Accepted 2007 replay: 23,329/26,245; 2104: 23,364/26,245 | **23,365/26,245** | Original full: 24,517/25,969 |
| Sixteen selected MutationObserver assertion files | 2104: 157/163 | **158/163** | Reference: 163/163 |
| Authored callback/realm controls | 2104: 8/13 | **12/13** | 13/13 |
| Native executable/JavaScript test groups | 218 retained names | **222/222**, including all 218 retained | Different test scope |

The expanded gain is **36 assertion passes and seven complete entries** over the accepted 2007 replay, including **one assertion and one complete entry** over accepted 2104. There are **zero lost prior passes, lost registrations or lost complete entries**. The sole new passing upstream assertion is the ordinary callback's exception being reported in its callback global.

All 1,248 native browser process exits were zero. Harness outcomes are separately retained: **1,238 OK, one ERROR and nine TIMEOUT**. Assertion outcomes are **23,365 PASS, 2,852 FAIL, seven TIMEOUT and 21 NOTRUN**. A process exit code does not turn a harness timeout or failed assertion into a pass.

The original 2007 full run remains HOLD; its later accepted reliability replay is labeled separately. Held development candidates 2101, 2102 and 2103 retain their original failures and incomplete runs. The report's historical 2104 snapshot is preserved verbatim, including its exact source, runtime and measurement qualifications.

### Selected MutationObserver limits

All **16 selected assertion files and their 163 identities** were rerun; this is not unqualified complete-family conformance. Five upstream failures remain: three HTML-parser mutation cases, one XML-parser mutation case and one outerHTML record-combination case. The separate nested MutationObserver crash guard was **not executed** and is not counted as an assertion.

The 13 authored controls comprise **9/10 normative passes** and **3/3 exotic runtime-preservation checks**. The retained normative failure is a child-realm observer constructor with a host callback observing a host target: that callback is not invoked, while a later host observer still delivers. Bound/proxy callback associated-realm observations are not promoted to normative conformance passes. The routing-only patch does not claim to solve this constructor-realm gap.

### Regression and rendering gates

The same exact 2105 source/binary passed all required scopes, preserving their original identities, expectations and fuzzy rendering metadata:

- **17/17 upstream rendering comparisons**, separate from authored geometry and actual-window checks.
- **94/94 authored CSSOM/review/geometry controls**, retaining the original normative removal-return assertion even though Chromium disagrees on that particular return value.
- **770/770 adjacent background interpolation assertions** across five files; math **8/8**; fixed-hit controls **5/5**; late geometry **9/9**.
- Explicit paired-axis animation **1/1**. The separately retained mixed-arity animation control remains **0/1**; all border-radius animation conformance is not claimed.
- Background controls preserve **37 required passes** and three disclosed observations.
- Native nested checks: ownership **133/133**, rendering/hit **157/157**, typed backgrounds **30/30**, actual-main signature checks **9/9**. These nested counts are not extra WPT assertions.
- Scoped ASan/UBSan runs pass ownership 133/133 and rendering/hit 157/157. Instrumentation covers the declared style/ownership and block/hit test scopes; the entire browser and V8 were not instrumented.
- Fourteen inherited stages preserve object controls **75**, upstream object **3 files/123 assertions**, CSS **26**, transfer **107/109** with 106 required, authored animation **53+7**, upstream animation **50 files/618 assertions with 552 passes and 41 complete files**, authentication **80+19**, and messaging **18 files/39 assertions with 34 passes and 15 complete files**.

These scopes overlap and use different units. Their counts must not be added into one conformance denominator. An inherited transfer audit initially refused a stale expected-head argument; that refusal is preserved, the supported argument was corrected, and no browser result or expectation was changed. Post-collection closure-schema corrections likewise did not rerun browser tests.

## Comparison with Chromium

Chromium's original full run is **1,060/1,248 complete entries and 24,517/25,969 passing assertions**, with **three startup errors**. A separately preregistered startup diagnostic registered 95 assertions and produced **24 PASS /71 FAIL**. That diagnostic is not silently merged into the original full run.

Browser-specific registrations differ. On the **25,909 exactly shared original-run identities**—full URL, exact assertion name and occurrence index—Lumen passes **23,119** and Chromium passes **24,457**:

| Exact shared identity result | Count |
|---|---:|
| Both PASS | 22,497 |
| Lumen PASS, Chromium non-PASS | 622 |
| Chromium PASS, Lumen non-PASS | 1,960 |
| Both non-PASS | 830 |

The separate diagnostic-qualified comparison has 26,004 common identities: 22,513 both PASS, 649 Lumen-only PASS, 1,968 Chromium-only PASS and 874 both non-PASS. It remains explicitly a qualified composite, not a clean full Chromium run. Unequal raw assertion denominators are not treated as a direct engine ranking.

The downloadable comparison retains all 1,248 file rows and the complete earlier 2007, Chromium and 2104 projections, adding a separate accepted 2105 projection. It contains standard WPT paths and outcome counts, without profiles, private messages, capabilities, environment data or raw error logs.

Sampled priority groups remain useful for future work, rather than complete-family scores or work estimates: CSS Values and CSSOM account for the largest recorded Chromium-only pass gaps, followed by backgrounds, variables and DOM nodes. Dedicated URL work was not selected for this iteration.

## Actual native-window redraw and showcase

The final gate ran the immutable 2105 binary in a fresh isolated Xvfb display/profile with software rendering. After the quiet input cutoff, the only page mutation changed one top-left vertical-radius component. No toolbar input, other DOM update, layout/computed getter or paint-request API was used between captures. Raw existing X11 pixels were captured from the same window.

The before capture ran at page READY+10.000088 to +10.215409 seconds, finishing before the frozen +11.5-second boundary. The setter marker occurred at +12.009803 seconds. The after capture ran at +16.000071 to +16.168466 seconds. All **ten 3×3 patch checks (90 pixels)** pass, with **1,127 changed pixels inside the shape and zero outside**. Root independently viewed the before/after images and all three representative video frames.

The real showcase is **25 seconds, 1,280×960, 4:3**, with readable white-on-black model/tool/candidate credits. Recording command launch was READY+5.004364 seconds; the setter was 7.005439 seconds after that launch. The first encoded-frame clock was not separately measured, so the video-relative eight-second frame correctly already shows the changed shape. The frozen badge in the fixture explicitly shows a historical baseline; current results are stated in this report and release page.

The first 2104 capture attempt remains a transport-timing FAIL with **no accepted pixel verdict**. Its single-shot 1fps capture crossed the mutation boundary; only that transport rate was changed to 30fps before the independently accepted 2104 and unchanged-controller 2105 runs. The fixture, timer, quiet interval and pixel oracles were never weakened.

All five sampled owned processes, all three owned process groups and the fresh task profile were gone at closure. Source, default/immutable binary and ICU pins remained exact. The existing sibling desktop browser, profile and account flow were untouched. These live redraw results are distinct from the 17 upstream rendering comparisons and nine native signature checks.

## Resource and performance evidence

The full 2105 sample used 1,384 one-second samples over 1,384.08 seconds. Peak sampled process-tree RSS was **659.894 reported MB**; median tree CPU 49.0%, peak 199.799%, and shared weighted CPU 56.772% under the 480% quota. The final window used 36 samples over 36.08 seconds, peak RSS **640.381 reported MB**, peak tree CPU 266.847%, shared weighted CPU 48.194%, and zero added throttled periods.

One-second samples may miss subsecond peaks. Shared cgroup activity includes other tasks and is not attributed solely to Lumen. The monitor's documented output labels are retained as reported MB.

The following timings preserve the single required diagnostic runs on held 2102, accepted 2104 and accepted 2105. Each uses the same actual hit-testing probe: 5,000 ordinary nodes, 20 warmups and 300 queries. The native hit-testing code is byte-identical between 2104 and 2105; only the callback-reporting JavaScript changed.

| Query | Held 2102 p50 /p95, microseconds | Accepted 2104 p50 /p95, microseconds | Accepted 2105 p50 /p95, microseconds | Correctness |
|---|---:|---:|---:|---|
| Outside clipped tree, no fixed descendant | 0.157976 /0.166008 | 293.956022 /689.315959 | 396.421994 /1,449.901029 | All correct |
| Inside the same clipped tree | 535.161002 /814.838917 | 571.591896 /1,113.764010 | 689.492910 /1,963.158022 | All correct |
| Escaped inline fixed descendant | 0.199070 /0.230968 | 134.222908 /242.153998 | 211.897073 /793.991028 | Old 0/300 incorrect; 2104 and2105 both300/300 correct |

The old no-fixed fast path was correct and much faster. The corrected escape-aware traversal adds a real O(n) outside-query cost. The old escaped-fixed path was fast because it returned the wrong result. The slower absolute 2105 values are retained without rerunning or assigning a cause; these single runs do not establish a causal regression from the JavaScript change or a general browser-speed result. A future optimization should preserve the fixed/affine correctness while restoring and independently measuring a correct fast path.

Fresh 2105 diagnostic SHA256: db7061f1516c01bcdf8e33fa950e49293dadc4de33aee17d2d52126f905af5cb. The raw latency CSV remains pinned as44efd0a3deadd36c531ad6c9cfc002c0c0bf32131ad1a7ae6988ed7641101cb4. The older2102/2104 exact measurements and source pins remain in the machine-readable report.

## Source, build and run

Tested engine commit: **20a7dd4b54f1a4e587168f10875e43f307a76fb8**  
Tested tree: **2b07ae05ef2adf0b91398d98adf3a082915ddfcd**  
Immutable Linux binary SHA256: **2d38085828206f384aeafc3afc38c63009fda483c55f0fcd057e0df9255ffcb3**

The source export is a normal descendant of published 2104 commit **54999c2cfe6b159167b89c1b5b98427e9eaf5370**. It contains **815 pinned source paths, 28 preserved historical public files, five metadata files, three reports and one separately curated current build/run guide: 852 tracked paths**. All **748 recorded build inputs** match the tested commit. The build guide is outside the 815 tested source paths. Canonical private Git history, profiles, inbox data and session capabilities are excluded.

The source archive includes complete approved public source history and offline restoration instructions. The Pages checkout itself is shallow/filtered and is not claimed to contain complete Pages history. External native/V8 dependencies are not bundled. The current guide documents the actual host-specific compiler, ABI, dependency and RPATH recipe; no independent archive rebuild or portable/byte-identical binary guarantee is made. A newly rebuilt executable remains unqualified until tested.

On the original project host, use the immutable executable with a fresh isolated project profile. No unsupported profile flag or HOME override is used:

    cd /mnt/p3/project/browser/lumen-android/lumen-android
    lumen_2105_profile="$(mktemp -d "$PWD/build/lumen-2105-profile.XXXXXXXX")"
    mkdir -m 700 "$lumen_2105_profile/config" "$lumen_2105_profile/data" \
      "$lumen_2105_profile/cache" "$lumen_2105_profile/downloads" "$lumen_2105_profile/tmp"
    XDG_CONFIG_HOME="$lumen_2105_profile/config" \
    XDG_DATA_HOME="$lumen_2105_profile/data" \
    XDG_CACHE_HOME="$lumen_2105_profile/cache" \
    XDG_DOWNLOAD_DIR="$lumen_2105_profile/downloads" \
    TMPDIR="$lumen_2105_profile/tmp" \
    LUMEN_NO_SESSION=1 \
    LUMEN_ICU_DATA="$PWD/build/codex-deps/v8/bottle/v8/15.6.75.8/libexec/icudtl.dat" \
    ./build/codex-native/lumen-astra-i2105 about:blank

The ordinary build output currently has the accepted candidate identity, but future builds may replace it; the immutable candidate path is the release pin. Existing DISPLAY0 belongs to a sibling release and was left intact under the project's ownership rule.

## Durable deliverables and remaining work

- [Current release page](https://ecooxai.github.io/lumen/) and [immutable candidate 2105 page](https://ecooxai.github.io/lumen/candidate2105/).
- [Public source branch](https://github.com/ecooxai/lumen/tree/gpt-6-astra-pro_builtin-agents_rubywebterm_linux-source2105-20261110).
- [Latest source archive](https://ecooxai.github.io/lumen/src.tar.gz).
- [Machine-readable qualification](lumen_linux_gpt-6-astra-pro_builtin-agents_rubywebterm_session21_qualification_20261110.json), [four-run comparison](lumen_linux_gpt-6-astra-pro_builtin-agents_rubywebterm_session21_wpt1248_comparison_20261110.json) and [current build/run guide](lumen_linux_gpt-6-astra-pro_builtin-agents_rubywebterm_session21_build_and_run_20261110.md).
- [Preserved accepted 2104 release](https://ecooxai.github.io/lumen/candidate2104/) and its immutable source/archive/media remain unchanged.
- [Live progress](https://a0bb38e846444140.5x7aqstuceku.opentunnel.xyz) retains two-second telemetry and the existing guarded feedback interface. A plain public link is read-only until the current session key is entered. Posting expiry is **16:52 UTC on 10 November 2026 — 00:52 on 11 November 2026 (Shanghai time)**. No authenticated posting is claimed. Its old live archive mapping remains candidate 2007; the current archive is on Pages.

Surge was unauthenticated and Fileharbor returned HTTP403. Those access limitations were preserved without account/auth workarounds; the existing user-authorized GitHub Pages workflow provides durable delivery.

Known remaining work includes mixed-arity radius animation, partial pending-variable mutation behavior, the five selected upstream MutationObserver failures and the foreign-constructor callback gap, broader unsampled APIs, and the measured hit-test traversal cost. The proposed native animation-endpoint resolver and outerHTML reaction change were deferred after concrete purity/reaction-order review. No unsupported feature was labeled complete to raise a score.

## Exact evidence identities

| Evidence | SHA256 |
|---|---|
| Independent full core seal | a6cccfbf608ca25d3f9cb3cd776519ff3ca8fe7a78fc0daa2e2f7c9da7f29186 |
| Independent final window seal | 442e781d175d99450344cfeff73c33985d8fb15b165c6a750760b7c3d47fc553 |
| Root five-image visual review | ff7391aeed729f5b77f8601e3323c1161464f1c3e5d1a65a0a1b91033f0b3dc1 |
| Current machine-readable report | 8b1c468c88bb5fed142fe135263f745065f979a8ab11a0f7914f60439a32d189 |
| Sanitized additive comparison | bb2e5cd75086db5db0d8d20c4d4112d0c64710308bef2abc1b352c3364fe310b |
| Current build/run guide | fab7355e455628e6f31d4bb69756c2c25cc99a7d9fd4ade061aa8dcef87146e5 |
| Real credited 4:3 video | fdf01c56395f86eee5f2d5b79d195274e14df1296f1ad24f2dd4ba4383985ae2 |

Final public export commit/tree, source archive hash and Pages commit are recorded by separate publication manifests after the report-containing commit, avoiding self-referential hashes. Original raw failures, comparator diagnostic qualifications, held candidates and earlier publication identities are retained.
