# Apple browser compatibility checkpoint

Model: GPT-6 Astra Pro. Coordinator: ChatGPT built-in agents; native execution: devinmac3d.
Branch: `gpt-6-astra-chatgpt-apple-compat-20261018`.
Date: 2026-10-18. **Intermediate verified checkpoint: 78/100 against the requested 98/100 goal.**
This is not a completed release or a claim of Safari equivalence. The full original request is preserved in `task.md`.

## Architecture and delivery

The C/C++/V8/SDL3/wgpu/FFmpeg browser engine remains intact. An isolated, nonpersistent WebKit reference utility was used for standards/geometry comparisons only. Device and simulator builds use the pinned jitless V8 configuration. Native desktop measurements below use the desktop V8 configuration and do not establish iOS jitless performance.

The current simulator app and universal iPhone/iPad device IPA both build successfully. The device IPA is `build-ios/Lumen-jitless.ipa`: 29,620,349 bytes at this checkpoint, iPhoneOS platform, arm64 device executable, device families 1 and 2, minimum iOS16.0, bundle identifier `com.ecooxai.lumen`. It is explicitly a **local, unprovisioned package**. The Mac reports **zero valid code-signing identities**. Physical installation, Apple-provisioned export and running the same device IPA on Apple Silicon Mac remain unverified; neither a simulator nor a Catalyst binary substitutes for that evidence.

The new `ios/package-ipa.py` and `ios/signing/` project provide a supported Xcode archive/export route when a matching valid identity and provisioning profile are supplied. Validation checks device Mach-O platform, app metadata, identifier/team/certificate/expiry/device scope, and JIT entitlement compatibility. It rejects disguised simulator/Catalyst packages and external symlinks, publishes output atomically and leaves a previous IPA intact on export failure. It does not import credentials or bypass Apple signing. The promoted signing project also completed a real unsigned Xcode archive; signed export and installation are separate outstanding gates.

## Verified production changes

### Layout, device behavior and native editing

- Flexible grid row sizing now handles bare `fr` content minima, definite and indefinite sizes, implicit rows and nested `repeat()`/`minmax()` syntax. CSS `env()` substitutes safe-area values and nested fallbacks. The generic regression fixes the ChatGPT mobile composer's vertical position.
- Screen, outer-window, orientation, touch and pointer information come from the native host rather than fixed desktop defaults. The media-query cache key includes DPR and the relevant orientation/touch/pointer preferences, avoiding stale capability results after changes. Native UIKit/device compilation passes; actual device delivery remains a runtime gate.
- Canonical text controls retain UTF-16 values and selection state only on relevant controls, using the existing optional node extension. Selection ranges, direction, replacement modes, value/type transitions, normalization and cloning have native and public-JS coverage. Email/number remain internally editable while public selection APIs retain their type restrictions.
- SDL default actions use the canonical caret and range for insertion, deletion, movement, select-all and selected-text clipboard operations. The target remains retained across `beforeinput`; focus, document, connection, editability and the current selection/value are checked again afterward. `input` follows successful edits. Line breaks expose null event data.
- Caret and selection painting use the real prepared text fragments, including UTF-16/UTF-8 and password projections. Default textarea/email/URL display uses the same normalization as DOM values. Clearing a textarea restores its placeholder. Temporary textarea/select display strings are released after text preparation.
- Previously verified independent CSS translate/rotate/scale, percentage translation, mixed overflow normalization and sticky paint/hit/DOM/selection geometry remain covered. Scroll offsets clamp synchronously, while scroll events are deferred/coalesced without fractional-deadline busy loops.

### CPU, memory, video and network lifecycle

- Metal surface acquisition now happens before page/video texture allocation and upload. An occluded or otherwise unavailable drawable retains bounded CPU damage metadata and retries with backoff; it does not queue new GPU staging uploads on every failed attempt. Resize and zero-damage recovery upload the current caller-owned frame and preserve missed damage.
- Rounded clips and group opacity use the ordinary video raster path instead of erasing video and then declining the GPU underlay. An empty clip cannot falsely claim a transparent hole, and fully clipped NV12 frames avoid unnecessary BGRA materialization. Existing holes survive unrelated partial damage.
- Opt-in GPU and video paint diagnostics are bounded to approximately once per second. They distinguish source pixels, CPU page alpha, drawable results and paint order; disabled diagnostics do not scan a frame or retain pixel buffers.
- Progressive-media shutdown cancels a held network request before joining its loader. Held headers and bodies close promptly in the native regression. Native DNS resolution/codec calls remain cancellation limits.
- Previously verified Fetch credential propagation, CORS preflight/session isolation, redirect behavior and synchronous request cleanup remain covered. Socket cancellation detaches ownership before pooling/closing and cannot interrupt a connection reused by another request.
- Worker process shutdown covers startup and cleanup, cancels held requests and drains before other subsystems are destroyed. Promise rejection reporting waits for a completed task checkpoint and releases native promise/reason handles after reporting or handling. Storage proxy enumeration no longer violates the backing-property invariant seen on YouTube.
- The reproducible V8 link helper preserves original archives and resolves the V8/wgpu Rust personality collision with a precise symbol operation. Development builds use three parallel jobs, with process-tree CPU/RSS monitoring.

## Native validation

The current full `make -j3 test` gate passes after the final raster correction. The native app plus parser, style, layout, render and JavaScript runners compile. The simulator and device builds pass with all changes in this checkpoint. The Apple builds retain existing unused-context/duplicate-library warnings; no new native control build diagnostic was reported.

| Gate | Native result |
| --- | --- |
| Canonical control core | 52/52 |
| Public selection/value APIs | 143/143 |
| Trusted input bridge and callback reentry | 12/12 |
| Actual app default-edit helper | 116/116 |
| Actual layout/font/display-list caret and selection | 64/64 |
| Control fragment/UTF-16 geometry | 84/84 |
| Video clip/fallback/hidden-frame work | 45/45 |
| Real GPU presentation code with controlled drawable failures | 41/41 |
| Device media-query/capability cache | 58/58 |
| JavaScript device APIs | 22/22 |
| Flexible grid rows and CSS env | 219/219 |
| Independent CSS transforms | 282 checks |
| Sticky geometry and repaint | 125 checks |
| App viewport / touch contacts / presentation retry | 34 / 29 / 31 checks |
| Socket cancellation | 32/32, including held headers/body/TLS |
| Worker lifecycle | 29/29 |
| Promise native retention | 16/16, including collection of a 32 MiB reason |
| Existing URL, network, CSS viewport, overflow and media lifecycle | Pass |
| IPA packaging / V8-link helper / monitor / UI-fixture Python tests | 11 / 9 / 3 / 5 tests |

Meaningful failing baselines were recorded before fixes: public selection19/121; input value modes130/143; caret painting31/48; line-break data115/116; default-text painting55/61; rounded/opacity video27/33; empty-clip video39/45; media-query cache53/58. Check counts can increase on successful dependent assertions, as in the final64-check paint result.

An Apple WebKit reference run passed120/121 of the earlier public selection fixture. Its only difference was an invalid `setRangeText` enum value: the fixture retains the HTML/WebIDL TypeError requirement rather than adopting that reference implementation's behavior. This is recorded as a standards/reference divergence, not hidden by weakening the test.

Earlier verified coverage retained in the branch includes Fetch credentials60/60, Storage38/38, DOM scroll32/32, public Promise reporting6/6, and30 repeated worker lifecycle/process-exit runs. The input module tests execute production default-action code; they are not end-to-end SDL/UIKit key-delivery evidence. Portable ASan/UBSan checks also passed for the control core/default actions/geometry; local LeakSanitizer itself was blocked by the execution environment and is not claimed as passed.

## Current website observations and resources

These are fresh, isolated native macOS engine runs at430×932, with no user credentials imported or entered. CPU is `ps` process CPU (100% is one core); memory is RSS, not physical footprint. Short samples establish the observed runs, not a guarantee against all long-session or device-specific regressions.

| Public page / activity | Duration | Settled CPU mean | Peak RSS | Final RSS | Observation |
| --- | --- | --- | --- | --- | --- |
| ChatGPT public composer | 26s; settle12s | 0.679% | 201.156 MiB | 201.156 MiB | Correct centered mobile composer; textarea at64.5,403 with305×24 geometry; editable APIs exposed; no message sent |
| Google public sign-in | 27s; settle12s | 0.500% | 235.375 MiB | 234.156 MiB | Email/phone field renders at26,218 with378×52 geometry; no login attempt |
| YouTube fresh-profile playback/pause/seek | 34s; settle10s | 10.067% | 457.703 MiB | 390.719 MiB | Native window captures show different decoded forest frames before pause and after seek; media error remains null |

YouTube's measured clock advanced3.813→7.814→11.819 seconds, paused at13.813, sought to23.813 and resumed through31.638 with readyState4 after buffering. These controls were programmatic page/media operations, not native touch interactions. Both actual native window screenshots and GPU/page-alpha diagnostics were inspected; the CPU-only screenshot hook alone cannot prove a GPU video's visible output.

The earlier occluded-surface regression queued about464 MiB of texture uploads during692 failed acquisitions, reaching approximately929 MiB RSS. After the acquisition fix, a controlled occlusion run recorded1239 occluded acquisitions with zero texture uploads; a separate activation/recovery run recorded885 occluded acquisitions followed by523 successful submissions, with442.312 MiB peak and385.203 MiB final RSS. The41-check production-code fixture verifies the causal upload/resize/recovery behavior independently of page variability.

An earlier live YouTube native capture was black despite valid source frames and an opaque CPU page center. Subsequent runs with the old probe, the ancestor probe and a fresh profile show visible video with a transparent page hole. The original failing profile/flags were not fully preserved, so the old black incident is not falsely attributed to the separate rounded/opacity fix. That fix and the empty-clip fix have their own reproducible RED/GREEN evidence.

Simulator build: about6s,606 MiB peak compiler-tree RSS. Device IPA build: about9s,672 MiB peak. These incremental build measurements are not clean-build timing claims.

## Apple UI and access limits

Earlier actual iPhone simulator evidence includes portrait safe-area geometry, native taps, drag scrolling, fixed-element taps, landscape viewport/pixel placement and taps, portrait restoration, and opening the software keyboard. XCTest `.typeText` could not focus SDL's hidden field; the harness now taps visible keyboard keys and checks the exact DOM value. Later simulator failures prevented completion of that revised gate.

The independent UIKit host and the browser runner subsequently stalled before installation/test execution. A fresh task-owned iPad simulator completed20 recovery attempts with30-second waits and bounded boot-status probes; all20 failed. That fresh device was shut down. Task-owned iPhone/iPad simulators were shut down; the unrelated preexisting iPhone simulator was left alone. The local fallback is Linux and has no Apple SDK/runtime. This is an environment limitation, not an iOS UI pass.

The macOS native input helper's permission preflight reported `post_event_access=false`; no keyboard/mouse events were posted through it and no system security setting was changed. Native window activation and screen capture were separately available. Current simulator/device compilation does not resolve the OS input/runtime or signing gates.

## Reproduction and continuation

```sh
make -j3 test
python3 tests/test_fetch_credentials.py --jsrun build/jsrun
python3 tests/test_package_ipa.py
python3 tests/test_prepare_v8_link.py
python3 tests/test_monitor_process.py
python3 ios/ui-tests/test_fixture_server.py
make -j3 PLAT=sim IOSDEPS=build-apple-toolchain/sim V8DIR=build-apple-toolchain/v8 app
make -j3 PLAT=ios IOSDEPS=build-apple-toolchain/ios V8DIR=build-apple-toolchain/v8 ipa
```

Ignored native evidence is under `out/apple-compat/`: `third-wave-final-native-gate.log`, `native-control-final-results.json`, `empty-video-clip-red.log`, `empty-video-clip-green.log`, `third-wave-tooling-results.json`, `third-wave-simulator-build.*`, `third-wave-device-build.*`, `youtube-cold-final-*`, `chatgpt-controls-final*`, `google-controls-final*`, and `fresh-ipad-recovery.jsonl`. The public website run manifest records its flags/probe/profile and distinguishes native desktop from iOS runtime.

The next concrete login feature is auxiliary tabs for `window.open`, `opener`, directed `postMessage`, navigation and close. At this checkpoint those top-level APIs are still stubs. Each top-level page has an independent V8 isolate; sharing isolates without an explicit agent/retired-document lifetime would risk dangling native DOM objects. The planned bounded implementation preserves separate isolates and stable handles, with explicit limits on arbitrary synchronous same-origin object access. It must not be described as full WindowProxy parity.

Further outstanding gates: completed authenticated Google/ChatGPT flows; actual native typing and submission; iPhone/iPad keyboard/rotation/dismissal tests; valid Apple export and same-IPA Mac installation; long-session and jitless-device CPU/memory. IME composition, full grapheme/bidi editing, undo, clipboard events, transformed control hit testing, Service Workers and broader browser API compatibility remain outside the proven scope.

The canonical repository is on the native Mac. Agents have a local scratch mirror whose base commits are older; apply only reviewed incremental exports, never its entire diff over the canonical repository. Root owns native runs and final integration. No push, deployment, final AGENTS.md rewrite or release completion is claimed here; the user's post-task instructions are conditional on reaching the goal.
