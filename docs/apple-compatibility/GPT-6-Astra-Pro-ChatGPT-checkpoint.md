# Apple browser compatibility checkpoint

Model: GPT-6 Astra Pro. Coordinator: ChatGPT built-in agents; native execution: devinmac3d.
Branch: `gpt-6-astra-chatgpt-apple-compat-20261018`.
Date: 2026-10-18. **Fourth implementation wave: 82/100 against the requested 98/100 goal.**
The score is a subjective engineering completeness estimate for the whole requested goal, not a test-pass percentage. This is not a completed release or a claim of Safari equivalence. The full original request is preserved in `task.md`.

## Architecture and delivery

The C/C++/V8/SDL3/wgpu/FFmpeg browser engine remains intact. An isolated, nonpersistent WebKit reference utility was used for standards/geometry comparisons only. Device and simulator builds use the pinned jitless V8 configuration. Native desktop measurements below use the desktop V8 configuration and do not establish iOS jitless performance.

The current simulator app and universal iPhone/iPad device IPA both build successfully. The device IPA is `build-ios/Lumen-jitless.ipa`: 29,655,600 bytes at this checkpoint, iPhoneOS platform, arm64 device executable, device families 1 and 2, minimum iOS16.0, bundle identifier `com.ecooxai.lumen`. It is explicitly a **local, unprovisioned package**. The Mac reports **zero valid code-signing identities**. Physical installation, Apple-provisioned export and running the same device IPA on Apple Silicon Mac remain unverified; neither a simulator nor a Catalyst binary substitutes for that evidence.

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
| Worker lifecycle and shared backing-store ownership | 55/55, plus process-exit cleanup |
| Auxiliary window lifecycle and messages, real V8 | 445/445 |
| Actual native App window host | 146/146 |
| Bounded native window queue | 58/58, also Apple ASan/UBSan |
| History URL/origin restrictions | 79/79 |
| Composer CSS/SVG raster behavior | 93/93 |
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
| ChatGPT public composer, fourth-wave run | 46s; settle15s | 1.113% | 200.953 MiB | 182.391 MiB | Initial and settled native images show the centered composer and correct rounded SVG send button; no message sent |
| Google public sign-in, latest build | 31s; settle12s | 0.437% | 235.062 MiB | 235.062 MiB | Email/phone field at26,218 with378×52 geometry; settled RSS growth +0.047 MiB; no login attempt |
| YouTube playback/pause/seek, historical f75 checkpoint | 34s; settle10s | 10.067% | 457.703 MiB | 390.719 MiB | Native captures show different decoded forest frames before pause and after seek; media error remains null |
| YouTube latest-build attempt | 41s | Not a playback sample | Not a playback sample | Not a playback sample | Google unusual-traffic CAPTCHA appeared before any video element was created; current playback validation is blocked |

The fresh ChatGPT run contained46 samples,31 after the15s settling period, and its reported settled RSS growth was −18.484 MiB. The latest Google run contained31 samples,19 settled. Both processes exited normally. These runs showed no sustained memory ramp within the observed intervals. The ChatGPT visual/resource check preceded only the final worker shared-allocator correction; Google and the blocked YouTube check used the relinked final desktop binary.

The latest YouTube attempt exited normally, but its native captures show Google's unusual-traffic CAPTCHA at the redirect destination. There was no player or video element throughout the probe. Its low CPU/RSS values therefore describe a blocked page and are not video-performance evidence. The following playback details are explicitly historical evidence from the earlier f75 checkpoint and do not prove playback on the latest source revision.

YouTube's measured clock advanced3.813→7.814→11.819 seconds, paused at13.813, sought to23.813 and resumed through31.638 with readyState4 after buffering. These controls were programmatic page/media operations, not native touch interactions. Both actual native window screenshots and GPU/page-alpha diagnostics were inspected; the CPU-only screenshot hook alone cannot prove a GPU video's visible output.

The earlier occluded-surface regression queued about464 MiB of texture uploads during692 failed acquisitions, reaching approximately929 MiB RSS. After the acquisition fix, a controlled occlusion run recorded1239 occluded acquisitions with zero texture uploads; a separate activation/recovery run recorded885 occluded acquisitions followed by523 successful submissions, with442.312 MiB peak and385.203 MiB final RSS. The41-check production-code fixture verifies the causal upload/resize/recovery behavior independently of page variability.

An earlier live YouTube native capture was black despite valid source frames and an opaque CPU page center. Subsequent runs with the old probe, the ancestor probe and a fresh profile show visible video with a transparent page hole. The original failing profile/flags were not fully preserved, so the old black incident is not falsely attributed to the separate rounded/opacity fix. That fix and the empty-clip fix have their own reproducible RED/GREEN evidence.

The final native suite completed in9.026s with330.5 MiB sampled build/test RSS; the simulator build completed in7.033s with576.031 MiB peak, and the device IPA build completed in9.047s with626.781 MiB peak. These incremental build measurements are not clean-build timing or browser-performance claims. The full commands and logs appear in the fourth-wave section below.

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

The auxiliary-tab feature is now implemented and covered by the real-V8 and actual-App fixtures described below. Each top-level page retains an independent V8 isolate with stable context identities, document epochs, immutable principals and bounded deferred host commands/messages. This supplies real opener/message/close/navigation behavior needed by popup-based flows, while arbitrary synchronous foreign object/DOM access and several cross-isolate transfer types remain unsupported. It is not full WindowProxy parity, and authenticated provider compatibility still requires a completed live flow.

Further outstanding gates: current YouTube playback through a permitted session that can reach a video; completed authenticated Google/ChatGPT flows; actual native typing and submission; iPhone/iPad keyboard/rotation/dismissal tests; valid Apple export and same-IPA Mac installation; long-session and jitless-device CPU/memory. IME composition, full grapheme/bidi editing, undo, clipboard events, transformed control hit testing, Service Workers and broader browser API compatibility remain outside the proven scope.

Fourth-wave native evidence is also under `out/apple-compat/`: `apple-fourth-native-gate.log`, `apple-fourth-sim-build.log`, `apple-fourth-device-build.log`, `chatgpt-popup-final-*`, `google-popup-final-*`, `youtube-popup-final-*`, `worker-sab-red.log`, `worker-sab-green.log`, `worker-sab-exit.log`, `frame-click-red.log`, `frame-click-green.log`, `frame-close-red.log` and `frame-close-green.log`. The latest device package has9 ZIP entries, iPhoneOS platform, families1/2 and no embedded provisioning profile.

The canonical repository is on the native Mac. Agents have a local scratch mirror whose base commits are older; apply only reviewed incremental exports, never its entire diff over the canonical repository. Root owns native runs and final integration. No push, deployment, final AGENTS.md rewrite or release completion is claimed here; the user's post-task instructions are conditional on reaching the goal.

## Fourth-wave checkpoint: auxiliary windows, lifecycle and composer rendering

**Overall score remains 82/100, below the requested goal.** This checkpoint records verified native functionality and successful Apple compilation. It does not establish iOS simulator execution, a signed device/Mac installation, authenticated provider login or complete Safari compatibility. The coordinator reviewed this evidence before integrating the checkpoint.

### What changed

Real auxiliary browser tabs replace the old popup stubs. Registry-owned monotonic browsing-context IDs survive document replacement, while separate document epochs prevent a queued message from migrating to a later page. Native App callbacks reserve and coalesce bounded commands; actual tab creation, navigation and destruction occur after the active native event/JavaScript batch. Top-level tabs retain separate V8 isolates. Stable handles support opener, postMessage, close/closed, focus, named target reuse and bounded location navigation.

Document principals are immutable, with distinct opaque-origin identities. History push/replace validates the permitted URL rewrite before changing either history state or the native URL. Initial real about:blank documents inherit the actual creator's origin, base, CSP and referrer state; public document.baseURI uses the inherited fallback and current base element. Same-origin iframe creators inherit the top active COOP policy. Strict top COOP forces independent creation for cross-origin frame creators only in the new-window branch; existing target reuse follows its separate rules. COOP transitions, sandbox propagation, noopener/noreferrer and trusted activation are enforced. Actual iframe opener identity survives frame navigation.

Directed messages capture the source ID, send-time origin and destination document epoch. Accepted messages survive sender close/navigation, including postMessage followed immediately by close; destination close/replacement frees queued payloads. Limits are 8 MiB per message, 32 MiB globally across pending/staging allocations, 1,024 queued messages globally and 256 per destination. Accounting includes serializer allocation capacity and transferred backing-store maximum size during serialization, including reentrant getters. Remaining queued messages stay counted during delivery; each dispatched native payload is released before its receiver handler runs. Page and worker V8 isolates now use shared ArrayBuffer allocator ownership so a transferred ArrayBuffer or shared worker buffer can safely outlive its source isolate.

Iframe click dispatch pins its native targets and revalidates the active frame context, document and connected target after every callback. It also rejects a synchronously closing browsing-context identity, preventing stale mouse events/default navigation while physical teardown remains deferred. Worker shutdown retains prompt per-tab cancellation plus the bounded process-exit drain/backstop before static teardown.

Composer fixes are generic CSS/SVG behavior: border-radius inherit/initial/unset, currentColor resolved through each SVG use instance, inherited gradient fill/stroke, reference-aware cache invalidation after color or referenced-shape changes, and percentage stroke widths retained through inheritance then resolved against the current SVG viewport's normalized diagonal. Cycle/depth/visit guards keep reference traversal bounded; insufficient hash traversal does not reuse an incomplete cached image. The regression covers nested/deep references, independent instances, square/non-square viewports and scaled viewBox rendering.

### Native proof and regression gates

| Gate | Latest verified result |
| --- | --- |
| Real-V8 auxiliary lifecycle/messages | 445 / 445 |
| Actual native App window host | 146 / 146 |
| Bounded native queue on Apple, ASan/UBSan | 58 / 58, including 10,000 coalesced requests |
| History URL/origin restrictions | 79 / 79 |
| Composer CSS/SVG raster checks | 93 / 93 |
| Worker lifecycle/default shared-buffer ownership | 55 / 55, plus process-exit observer/backstop |

The proof sequence includes real failing behavior. History was 27/79 before its guard and 79/79 afterward. The first auxiliary fixture reported the inherited-base failure and then crashed after transfer-delivery check 48 during source-before-receiver teardown. Shared page allocator ownership removed that crash, yielding 298/299 with only baseURI still failing; the base/policy follow-ups reached 409/409, and the final expanded fixture reached 445/445. Frame-event fixtures reproduced a native crash and stale tail dispatch before reentry/closing guards. Worker native run 724 crashed when collecting a default shared backing store after its source worker exited; the unchanged fixture passed 55/55 in run 726 after the shared-allocator correction. The worker cases separately cover source exit before delivery, termination after delivery, and owner close with an undelivered 2 MiB buffer.

The V8 fixture results are normal native runs. The installed V8/PartitionAlloc build aborts at allocator-zone initialization under ASan; the separate portable native queue is the Apple ASan/UBSan gate.

### Completed native suite and Apple cross-builds

Root's native run 727 verified all three commands:

| Command | Outcome | Elapsed | Sampled build/test peak RSS |
| --- | --- | ---: | ---: |
| `make -j3 test` | PASS | 9.026s | 330.5 MiB |
| `make -j3 PLAT=sim IOSDEPS=build-apple-toolchain/sim V8DIR=build-apple-toolchain/v8 app` | PASS | 7.033s | 576.031 MiB |
| `make -j3 PLAT=ios IOSDEPS=build-apple-toolchain/ios V8DIR=build-apple-toolchain/v8 ipa` | PASS; unprovisioned IPA | 9.047s | 626.781 MiB |

Logs: out/apple-compat/apple-fourth-native-gate.log, out/apple-compat/apple-fourth-sim-build.log and out/apple-compat/apple-fourth-device-build.log. The device output is build-ios/Lumen-jitless.ipa (29,655,600 bytes,9 ZIP entries) and is explicitly unprovisioned, with no embedded.mobileprovision. Remaining warnings are the known capture/WebGPU unused-context and duplicate-lc++ warnings; the dangling-else/braces warning is gone. These RSS values measure build/test processes, not the running browser.

### Limits and next-agent starting point

The implementation deliberately exposes a bounded auxiliary-window API. Arbitrary foreign DOM/object access across separate top-level isolates raises NotSupportedError; prohibited cross-origin access raises SecurityError. Cross-isolate MessagePort/SAB/Wasm transfer and cross-tab iframe navigation remain explicitly unsupported, while ordinary ArrayBuffer transfer works. Auxiliary navigation supports HTTP, HTTPS and about:blank. Popup requests create real browser tabs, not separate OS windows or complete popup geometry semantics.

The overall 82/100 score remains constrained by simulator boot failure after20 recovery attempts, absent signing identities/profiles, no authenticated Google/YouTube/ChatGPT login, the latest YouTube CAPTCHA blocker and unavailable remote OS event-post permission. No access-control settings were changed. Successful simulator/device compilation must not be reported as successful UIKit runtime validation or signed installation of the same IPA on iPhone, iPad and Mac.

An ignored manual-only loopback target is available at out/apple-compat/GPT6AstraPro_builtin_apple_browser_review_exec_popup_ui_smoke.py. It uses two local origins, no provider/account endpoints, and checks the real popup's origin/source identity/closed state. Python and embedded JavaScript syntax passed; no remote OS-input execution is claimed. Source ownership: windows.cc/windows.h and JS/frame bridges hold registry/security/message behavior; main.c/window_queue.h hold the deferred host; independent fixtures are test_window_lifecycle.cc, test_app_windows.c, test_history_origin.cc, test_worker_lifecycle.cc and test_composer_paint.c. Continue from the current integrated source; do not reapply earlier full or incremental patches.
