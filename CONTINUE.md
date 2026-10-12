# Lumen: handoff notes

## Android/Linux checkpoint (2026-10-12)

**Current user priority: Android only.** Linux development and additional desktop
benchmarks are deferred. An authorized MI 8 (Android API 35, arm64-v8a) is
connected. The own-engine debug APK now builds and passes all 23 packaging
gates after fixing SDL's missing libc++_shared.so packaging. The original launch
crash from a hidden font JNI entry point is fixed: cycle2 visibly renders the
home page on the phone. Its idle gate fails at 101.20% CPU (one core = 100%)
and 234.19 MiB RSS over five complete idle intervals. SDLThread consumes
98–100%. Non-root LLDB snapshots locate that thread in SDL_WaitEventTimeoutNS,
both its event-read and event-pump paths. See
`build/codex-android-idle-profile/cycle1/symbolized-stacks.txt`.
The pinned SDL Android wait loop self-wakes by inserting a poll sentinel every
iteration. A generated-source fix retains normal poll sentinels but does not
insert them inside the blocking loop. Its eight behavioral model cases and
four source-generation checks pass after four behavioral RED failures. The
39 existing APK/prebuilt/build-limit unit checks also pass, and the corrected
APK builds incrementally in 23 seconds. All 23 packaging checks pass. The
cycle3 phone run drops idle CPU to 9.40% (5.05–14.14%), RSS232.17 MiB, but
still fails the 0–3% gate after five complete intervals. SDLActivity uses
4–5% and SDLThread intermittently 0–10%; residual profiling is required.
Do not call this an idle pass. The installed APK is
`build/codex-android-sdl-wait/green/codex-android-idle-wait-debug.apk`, SHA256
`1fa9a030fe1a7e85febf46b19ec0c58d9c8970f4c170c5d81205902eabaf667c`.
See `build/codex-android-sdl-wait/green/REPORT.md` and
`build/codex-android-browser-phone/cycle3/REPORT.md`.
The controls are desktop-sized and overlap the Android status bar. Fixture,
interaction and site checks have not run. Evidence:
`build/codex-android-browser-phone/cycle2/REPORT.md`.
No browser runtime pass is claimed. The isolated
font test APK was previously installed/tested. Android artifacts are under
`build/codex-android-*`.

**Latest host compiler budget: all new builds share 60% of all CPUs (480% on
this eight-core host).** `tools/codex_compile.py` now places both ordinary and
`--v8` commands in `codex-lumen-compile.slice`, with SCHED_IDLE/nice19 and a
100 ms quota period. At most six compiler jobs; prefer fewer for large links.
The host watcher has one additional narrowly scoped exemption for that slice
at the exact 480% quota. Its prior backup is
`/usr/local/lib/cpulimit/cpulimit_watcher.py.codex-before-shared-sixty`.
Unit/mock checks pass 8+15. A 25-second six-worker loaded interval averaged
479.06% shared-slice CPU, peaked at 116.15 MiB summed RSS, and recorded all
182 thread observations as SCHED_IDLE/nice19. Membership and 480000/100000
held across all 26 saved samples. The raw observer exited 1 on an extra
post-completion controller check; its unsaved final state prevents claiming
a clean observer teardown, not the bounded live measurements. See
`build/codex-android-compile-sixty/green/LIVE_REPORT.md` for that limitation.
No unrelated quota is raised. The old V8 source scope stays frozen with its historical 320% leaf
and 200% parent; this allowance does not authorize a duplicate or unsafe resume.

**Earlier host compiler budget: V8 may use 40% of all CPUs (320% on eight
cores), with SCHED_IDLE/nice 19; other builds still share 140%.** The user
explicitly requested 40%/320% after the earlier reduction and lowest priority.
V8 uses
`python3 tools/codex_compile.py --v8 COMMAND ARGS...` and the separate
`codex-lumen-v8.slice`, with at most six compiler jobs. Other compilation uses
the wrapper without `--v8` and retains `codex-lumen-compile.slice` (140000/100000
microseconds), at most two compiler jobs per build. Do not stop either slice or
the old anchor service while any build processes remain in it. The authorised
V8 concurrency change preserves existing GN outputs; it is not a clean rebuild.
The iteration-7 scope `run-p3601367-i28741414.scope` received
`cpu.max=320000 100000`, retaining parent `560000 100000` for its monitor.
However, at 13:11:38 PDT another host configuration change set both Lumen
parent slices to `200000 100000`; the CPU watcher moved the build into
`/cpulimit.slice/cpulimit-70.slice` and its safety monitor stopped Ninja and
six workers. The old scope disappeared. The global cpulimit parent is also
200%. Ninja PID 3601367 was stopped at 826/2592 steps; that attempt did not
pass its 320% live acceptance gate. Do not launch a duplicate build.
The original monitor's guard is latched, so merely sending SIGCONT is unsafe.
Evidence: `build/codex-android-v8-forty-percent/REPORT.md` (13 mocked checks
pass; sustained live verification failed). Do not overwrite unrelated host settings.

Recovery now preserves the stricter shared host limit rather than raising it.
The existing seven stopped processes were moved to the system scope
`/cpulimit.slice/codex-lumen-v8-recovery.scope` with its own `320000 100000`
ceiling, beneath the unchanged shared `200000 100000` parent. An initial scope
below `cpulimit-70.slice` failed safely with EUCLEAN (threaded parent); no build
process resumed during that attempt. The host watcher now preserves only the
exact new scope with the exact 320% quota; other mappings remain unchanged.
Original backup: `/usr/local/lib/cpulimit/cpulimit_watcher.py.codex-before-nested-recovery`.
New guard: `tools/android/build_guard.py`, documented in
`docs/codex-android-build-guard.md`, run by user service
`codex-lumen-v8-recovery-guard.service` with Restart=no. It verifies process
identities, per-thread idle/nice19, quotas, memory, concurrency and input hashes,
and stops/freezes its own build on failure. The original monitor remains alive
to reap Ninja and report the actual exit code; its latched alert is historical.
After guard readiness and stopped placement checks, the same Ninja and six
workers were resumed via identity-pinned pidfds, without restarting compilation.
Configuration, placement, resume and per-second CPU/RSS evidence are in
`build/codex-android-v8-recovery-host/`. Guard/watcher unit and simulated
fault tests pass 30/30 in `build/codex-android-v8-recovery-test/green-cycle4-q36oL3T3/`.
Fresh independent live verification passes 10/10 recovery gates over 91.074
seconds (92 samples): progress 831 to 837/2592, weighted mean CPU 138.45%,
interval peak 159.65%, peak summed RSS 4.086 GiB, minimum available 20.073 GiB.
Five priority scans preserve all identities, hashes, quotas and idle/nice19.
Evidence: `build/codex-android-v8-recovery-live/REPORT.md`. Main bounded recovery
score: 95/100; this is not a browser-readiness score. Do not claim the full
archive, native link or browser APK has passed. The current ceiling is 200% shared,
not guaranteed 320% throughput. Do not stop either monitor or launch another V8.
**Subsequent safety stop:** at 13:54:34 PDT the new guard observed seven compiler
processes and exited 2 after freezing its scope and sending identity-checked
SIGSTOP. At 14:03 PDT the scope reports `frozen 1`; Ninja remains the same
process at 891/2592 steps. There are six direct compiler jobs, but the Clang
driver PID 3942053 spawned `clang -cc1` PID 3942055, so the guard double-counted
one job. Both wait in `do_freezer_trap`. Fix compiler ancestry counting before
resuming. `guard-latest.json` is now stale, not live progress evidence. Preserve
this frozen fallback while evaluating the user-requested prebuilt alternative;
do not automatically thaw it or restart V8. Prebuilt investigation:
`docs/codex-android-v8-prebuilt.md` and `build/codex-android-v8-prebuilt-probe/`.
A V8 15.0.245.2 Android archive now links the unchanged smoke object after
building only its exact Temporal Rust support library (66 seconds). Candidate
C++ syntax and the original 15.6 consumer object both pass after the checked
Module::Evaluate conversion in src/js/js.cc. All 45 Rust registry versions and
checksums match upstream. However, the first phone run failed before main:
TLS alignment 8 instead of 64. Main traced this to linking a native-ELF-TLS
archive with API28 startup objects. Android ELF TLS requires API29; API29's
NDK crtbegin has the required alignment. The isolated API29 relink now passes
three actual phone runs: initialization, ICU date/number, Wasm, BigInt,
microtasks and teardown. TLS64, 16 KiB LOAD alignment and API29 dependency
closure pass. Runs took 0.097–0.106s, CPU 91.38–94.43% of one core, peak RSS
24.20–24.96 MiB (active smoke, not idle/browser metrics). Report:
`build/codex-android-v8-prebuilt-probe/phone-api29/REPORT.md`.
Bounded prebuilt-smoke milestone 95/100; daily browser still 20/100.
An opt-in prebuilt integration is now in the worktree: `AndroidV8Prebuilt.cmake`,
the SDK/Clang checksum manifest and verifier, ICU77.1 headers, explicit ABI
definitions/absolute vtables/Temporal, and Gradle prebuilt/compiler properties
with minSdk29. The source route retains API28. The prepared SDK lives at
`build/codex-android-v8-prebuilt-sdk`; archive/header pins are verified.
SDK/APK/compile-limit units pass 37/37. Gradle RED was confirmed before its
implementation; native CMake's first real configure found that NDK legacy mode
sets CMAKE_SYSTEM_VERSION=1. Main fixed API detection to use normalized
ANDROID_PLATFORM_LEVEL. The maintained CMake smoke now links and the actual
Gradle API29 prebuilt configuration passes. Cycle3 compiled the root objects
but exposed missing SDL C++ runtime linkage; the private NDK STL fix passes
cycle4, without leaking NDK headers into the custom-STL engine. Main's final
link then exposed omitted BZip2 and MediaCodec dependencies. Android-only
bz2/mediandk linkage fixes that real RED result. Cycle5 now passes the complete
native link, 37 units and 24 static checks: all 989 strong imports across the
four native libraries resolve against their API29 providers, and custom C++
runtime isolation passes. The link reused 289 objects; mean CPU97.74%, peak
131.40%, sampled peak RSS1015.37 MiB. Report:
`build/codex-android-prebuilt-integration/cycle5/REPORT.md`.
Main's bounded native-link milestone score is 95/100, not browser readiness.
Gradle's Ninja compile6/link1 pools and LLDthreads2 were added after an actual
configuration RED. Their real Gradle configuration and generated Ninja graph
now pass. The first actual debug APK assembles in 35.58s (mean CPU352.30%,
sampled peak RSS1460.60 MiB), but APK validation caught missing libc++_shared.so.
AGP records that runtime on SDL3-shared, not on main. The prebuilt Gradle route
now explicitly selects both shared targets, without exposing NDK STL to V8.
Cycle2 passes all 23 packaged-APK checks, including 1,093 strong imports,
API29 provider versions, debug signature, manifest and 16 KiB ZIP/ELF alignment.
The shared-library TLS layout is checked separately from executable TLS;
reproducing stripping matches every packaged library byte-for-byte.
Assembly took 20s, mean CPU256.37%, peak sampled RSS651.73 MiB. Report and APK:
`build/codex-android-apk-prebuilt/cycle2/REPORT.md` and
`build/codex-android-apk-prebuilt/cycle2/codex-android-own-engine-debug.apk`.
APK SHA256 `54d317853e63527641a051e0a50c2708d74af42e28366236f1ce37f2fab2a514`,
93,103,068 bytes. Main build/packaging milestone score: 95/100.
First phone run in `build/codex-android-browser-phone/cycle1/REPORT.md` installs
successfully, then crashes with UnsatisfiedLinkError for
AndroidFontRaster.nativeInitialize. Its symbol is LOCAL inside libmain because
the engine archive is hidden by --exclude-libs,ALL. The app remains installed
and force-stopped. Nine 1Hz samples were armed throughout, but the subsecond
process fell between samples; CPU/RSS/PSS are unavailable, not zero. No browser
screenshot, idle pass or feature fixture run occurred; no accounts were used.
Main now compiles the existing Android font bridge directly into main rather
than the hidden engine archive, retaining custom C++ runtime isolation.
APK verification now requires all four SDL/JNI entry-point exports. Its two
new regression groups first failed as intended (16 existing groups passed).
The JNI fix passes 39 units, rejects the old APK's missing font entry point and
builds a new APK that passes the maintained native verifier. All 291 existing
objects were reused; only the bridge's new main-target object was compiled.
Build mean CPU198.40%, peak sampled RSS1442.03 MiB, all 2705 thread observations
idle/nice19. New APK SHA256 is
`0e70d056106b52d8a75e4e0e11d54ce156ff691061477c273afc98bd8023cba5`.
Expanded packaging stopped twice on host niceness violations (SCHED_IDLE and
480% remained intact): first apksigner Java, then its retry's bash preflight
had nice -4. The host's ananicy bash rule uses Doc-View/nice-4, consistent with
the observations; no syscall trace establishes the actor. No host rules were
changed. The failed verification scope/controller were identity-safely
terminated without thawing. One shell-free packaging retry (the same SDK
apksigner JAR directly under Java with identical verification flags) passes
23/23 packaging checks and explicit GLOBAL definitions of all four entry points.
All 359 thread observations pass idle/nice19 and shared480%; mean CPU123.88%,
peak sampled RSS401.98 MiB. Report and validated APK are in
`build/codex-android-jni-entrypoints/green/retry2/`.
Original V8 stays frozen and seven hashes remain unchanged. Main's bounded
JNI/build milestone score is 95/100; browser readiness is still unproven.
Fresh phone verifier `01a13230-500d-78d2-9b88-b11bb28dba9b` is running the
corrected APK in `build/codex-android-browser-phone/cycle2/`.
Do not duplicate its device operations or claim startup before its result.
Its predecessor's monitor incorrectly included the unrelated nice0 anchor
sleep; the fresh verifier must inspect only owned scope descendants for
thread priority/RSS, while using the shared slice for aggregate CPU quota.
No device-browser startup pass is claimed yet.
Do not fake API28 compatibility or silently swap archives. Keep the source
fallback frozen. `tests/android/browser-smoke.html` is the offline DOM/Intl/Wasm/
Canvas/WebGL/WebGPU/storage/fetch fixture for the next actual browser runtime
gate; only its syntax has been checked so far.
The temporary normal-priority host exemption was reverted; the original
`dev-workload-priority.service` again enforces idle scheduling. Do not restore
normal priority without a new user request.

Android link-opening now has cold/warm HTTP(S) intent routing, a bounded native
FIFO and regression tests; actual APK/activity lifecycle testing remains pending.
Android Back now has API 33+ callback and legacy/hardware routing, a bounded
engine-thread queue, popup/address/focus/history/root-task behavior and
focus-transition IME activation (no automatic reopening after dismissal).
Test-first native policy/concurrency, ASan/UBSan and ThreadSanitizer tests pass;
100 threaded stress runs transfer 400,000 requests with exact wake counts.
The registered CTest passes; actual changed
Android app/JNI objects and SDK36/SDL Java sources compile. Verification report:
`build/codex-android-back-test/REPORT.md`. Gesture/IME/history/lifecycle behavior
still needs device testing; there is no predictive page-preview animation.
Android fonts now use the API 29+ platform matcher with copied file/index/axis
metadata, CSS family fallback and variable-weight handling. The isolated
`org.lumen.codex.fonttest` APK is installed/tested on the phone; it is NOT the
browser APK. The shared Android Canvas/JNI color-raster adapter repaired the
COLR system-font raster failures: 25 grouped phone checks and actual API28
object/SDK36 Java compilation pass. Evidence:
`build/codex-android-color-font-green/REPORT.md`; font Activity settled CPU was
0–2.02%, RSS 145.26–150.73 MiB. API28 runtime, full shaping coverage and browser
startup remain unverified. The font matcher checkpoint is `191e800`.
An expanded 30-check sequence gate then exposed keycaps (29/30 passed):
`build/codex-android-emoji-sequences-red/REPORT.md`. Main added bounded Android
combining-cluster font selection to prevent splitting the ASCII base from the
combining keycap, six further regression groups and an actual engine-pixel
preview chart. These changes form the Android color-font checkpoint, not a
browser APK or comprehensive standards certification.
All 36 groups now pass and the clipping counter is zero; root API28 objects
and real SDK36/SDL Java also compile. Evidence is in
`build/codex-android-font-visual-green/`. Main visually reviewed the actual
engine PNG: sizes, colors, scripts and combined emoji look correct. A subsequent
fresh test-Activity system-bar-insets run in
`build/codex-android-font-insets-green/` still passes all 36 groups; main
reviewed the new screenshot (header no longer overlaps) and confirmed the
native PNG is byte-identical to the earlier verified chart. Browser UI is
untested. The expanded preview run sampled 0–2% settled CPU and
151.14–168.95 MiB RSS; these are test-Activity, not browser/Chrome figures.
The final inset-fixed run measured 0–2% settled CPU (mean 0.20%) and
161.34–161.47 MiB settled RSS; startup/preview peaked at 103.92% CPU. Main
review score: 96/100 for this bounded API35 font milestone; daily-browser
readiness remains 20/100, with V8 linking/startup and site/API tests outstanding.
V8's native GN tool is built and Android generation passes. Actual compilation
required V8's checksum-pinned Clang24 and custom libc++ (NDK libc++19 lacks
std::atomic_ref). The active build uses generated v8-gn.h; CMake consumers still
need final archive-link verification. The Android CMake adapter now selects
matching Clang24/__Cr STL/relative-vtable ABI and imports the monolith's ICU;
Gradle forwards the required `lumenV8Dir` and optional `lumenWgpuDir` inputs.
See docs/codex-android-build.md. All 23 foundation packages have built; the
real arm64 prefix is available at `build/codex-android-deps/prefix/arm64-v8a`.
tests/android/v8_smoke.cc is the new dependency startup/ICU/Wasm runtime gate;
it must pass on Android before stronger integration claims.

Verified integration checkpoint: the actual root Android CMake project compiles
all 38 engine/app objects, including JNI, against real target OpenSSL/JPEG
headers; 155 graph/header/ABI checks pass. All 11 Gradle configuration cases
pass, including actual AGP-to-CMake flag forwarding. Reports:
`build/codex-android-engine-cmake-test/cycle1/REPORT.md` and
`build/codex-android-gradle-v8-test/REPORT.md`. These are object/configuration
gates, not a successful final native link, APK or device run. The real SDL3
shared library and engine/base/net archives now also build; all 44 artifact
checks and 67 SDL Java/native signature matches pass. Report:
`build/codex-android-sdl-engine-test/REPORT.md`. SDL requires the matching NDK
`libc++_shared.so` in the APK. Final `libmain.so` linking awaits the real V8
archive. `tools/android/verify_apk.py` checks actual packaged native dependency
closure and ELF layout when an APK exists, not signing or runtime behavior.

V8 iteration 6 stopped at 263/2855 resumed steps: the pinned `gc-info.cc`
checked its GC mode before including generated configuration. The minimal
maintained fix is `android/native-deps/patches/v8-15.6.75.8-gc-info-guard.patch`.
Both real host and Android objects now compile; all 19 mode/symbol/input/patch
checks pass (`build/codex-android-v8-gcinfo-test/REPORT.md`). No GC setting, GN
argument, generated configuration or engine behavior was changed.

The incremental six-worker build is now iteration 7, initially 2592 pending
steps, not a clean rebuild. At handoff its Ninja PID is 3601367 and monitor
service is `codex-lumen-v8-iteration7.service` (`Restart=no`). Current log:
`build/codex-android-v8/android-v8-monolith-iteration7.log`; read live process
state and fresh `build/codex-android-v8-recovery-host/guard-latest.json` before
reporting progress (the original monitor's guard/membership is historical).
The persistent monitor owns the child and records its eventual exit status;
it samples every second and retains memory guards without automatic restart.

Current branch is `codex/android-phone-engine-port`, after shared-engine
checkpoint `8cceb05` on `codex/android-linux-daily-browser` and original clean-tree
checkpoint `c742180`. Read `docs/codex-android-build.md` and
`docs/codex-android-linux-status.md` before continuing. Native core
tests and directory rendering now run on this Linux host; full Lumen and jsrun
now build with pinned V8/wgpu dependencies (see
`docs/codex-linux-native-dependencies.md`). Bounded offline GUI checks passed
discrete zoom, media controls and workspace switching with the default renderer.
CPU fallback hung after starting audio; 10% video zoom hides native controls.
Android is a same-engine
SDLActivity/CMake adapter, not a built or tested APK. An incomplete offline Chrome
comparison is recorded in `build/codex-local-chrome-benchmark/REPORT.md`; it is not
an Android or top-site benchmark. No personal-account tests have been performed.
Keep the goal open and do not treat older macOS results below as Linux/Android
verification. Continue test-first with fresh build/test-only agents and one-second
CPU/RSS monitoring. Main agent owns implementation and debugging.

## Current goal (user, 2026-10-08)
Lower RAM/CPU/GPU/load time than Chrome on: YouTube home, 2 YouTube videos playing,
bing.com, Google search with AI Overview — all loaded and active. Compare with
off-screen media eviction setting off and on (on: target < 150 MB for YouTube playing).

## Done
- RAM: decoder threads capped (4), CSS custom props share parent chain, script sources
  freed, VQ_MAX 3, selector vectors trimmed. Single YouTube ~300 MB (was 396).
- VideoToolbox hardware decode (`LUMEN_HWDEC=0` disables), software fallback.
- Real tabs in one process (`src/app/main.c`: `Tab`, `tab_new/select/open/close`,
  tab strip in chrome, Cmd+T/W/1-9, Ctrl+Tab; extra argv URLs open as tabs).
  Background tabs keep JS ticking and media playing.

## Next
- Off-screen media eviction: setting plumbing done (`g_lowmem` in main.c, `offscreen_media_eviction=1` in
  SDL pref dir settings.txt, `LUMEN_LOWMEM=1` env). Still to do: evict decoded images not painted for a few
  seconds (ImgSlot in main.c; img_size layout hook also calls node_img, so mark use from paint only), skip
  BGRA conversion for videos in background tabs/off screen (media.c emit_video). V8 MemoryPressureNotification
  + malloc_zone_pressure_relief did not help (tested, removed).
- GPU %: read from Activity Monitor % GPU column (CLI has none in this VM).
- CPU: profile shows swscale NV12->BGRA + memmove; consider GPU NV12 upload.
- Rerun 5-page compare: `~/sites/lumen-tabs.sh`, `~/sites/chrome-tabs.mjs`, `~/sites/cpusum.sh`.

## ChatGPT / Cloudflare Turnstile (2026-10-09)
Goal: chatgpt.com usable in Lumen incl. login. Fixed so far (each has a tests/js fixture):
- WHATWG streams: pipeTo/pipeThrough/tee/from, WritableStream, TransformStream, Text{En,De}coderStream
  (Cloudflare's chl_page rejected Lumen as "Browser not supported" without pipeTo). `streams.html`
- Iframes inside shadow roots never loaded (frames_scan walked light tree only; shadow trees were never
  NF_CONNECTED). Turnstile mounts its iframe in a closed shadow root. `shadow-iframe.html`
- postMessage / MessagePort / worker `message` events had isTrusted=false; Turnstile and its blob worker drop
  untrusted messages. `frame-msg-identity.html`, `worker-trusted.html`
- Worker fetch: headers, binary bodies, response headers. `worker-fetch.html`
Status: challenge now runs end to end (worker probes, PoW, timing) and ends in `interactiveBegin` (the
"Verify you are human" checkbox). That needs a human click in the GUI; agents must not click it.
`brunhild.challenges.cloudflare.com` does not resolve from the Devin VM (curl fails too), so one worker
probe always reports "Failed to fetch" here.
Debug: `LUMEN_FRAME_PRE='js'` evals a script in every non-about: iframe context before its scripts (pair with
`JSRUN_PRE`, `LUMEN_CONSOLE=1`). Don't wrap Worker/eval/Function with Proxies; it breaks the challenge.

## Chrome 154 baseline (5 tabs)
RAM 1,307-1,314 MB, peak sum ~1,850 MB, CPU 10-14%, GPU proc ~94 MB, video 854x480.
Load (DCL/load): Google 0.53/3.66 s, Bing 1.97/2.23 s, YT watch ~2.2/3.47 s, YT home 2.29/2.33 s.

## UI (2026-10-08)
- Tabs live in the title bar right of the traffic lights (`mac_style_window` in src/app/macui.m: full-size content view,
  transparent titlebar; `win_hit` makes empty tab-row space draggable).
- Workspaces: left bar `SIDEW` 68pt (right edge = 3rd traffic light), `Tab.ws`, `ws_select/ws_new/ws_close`,
  Workspace menu (`mac_install_menu`, events `EV_MENU` -> `menu_cmd`). Double-click a workspace to rename.
  `LUMEN_NO_SIDEBAR=1` hides the bar.
- Low-memory mode (`LUMEN_LOWMEM=1`): `img_evict` drops images not painted for 3 s (refetch on paint),
  `media_mark_visible` + `hidden` in media.c emit 1x1 frames for players outside the active tab.
  3-tab YouTube test: 523 -> 496 MB, images 24.8 -> 6.1 MB; JS heap (~70 MB/YouTube page) dominates.
- Tab CPU limiter: `cpu_monitor` (main.c) sums per-tab main-thread time (js_tick/restyle/render) + decoder-thread CPU
  (`media_cpu_ms`). >80% for 60 s -> `limited` (token bucket 0.4 ms/ms, `media_set_limit` sleeps decoder) + infobar
  (`g_info_h`, buttons `HB_INFO+k`: 1/4/10 h unlimit, x dismiss). `LUMEN_CPU_DEBUG=1` prints per-tab %. Test: tests/js/cpuburn.html
  (100% -> 40.6% process CPU after the limit kicks in). FFmpeg internal decoder threads and VideoToolbox are not counted.
- Workspace bar 84pt (= first tab's left edge). Click inactive workspace = switch; click active = native menu
  (`mac_ws_menu`): Rename, Change Icon (16 SF Symbols, `mac_icon_rgba` -> cached Image), Refresh All Tabs, New, Close.
- Benchmark: ~/sites/lumen-real.sh off|on URLs... (5 real tabs, `LUMEN_TAB_TOUR=6000`). Latest: off 619-640 MB / 29-37% CPU,
  on 574-596 MB / 24-28%; Chrome 1307 MB / 10-14%. Profile: biggest CPU = libswscale (BGRA conversion) -> next: VT BGRA
  output or GPU YUV upload; JS heap ~90 MB per YouTube page.
- Limited tabs show a green dot left of the tab's x (`HB_TABDOT`); hover or click (pinned 4 s, `tip_until`) shows
  `chrome_tip`. Hit codes: check `HB_TABDOT` (500) before `HB_INFO` (400) in the click handler. "+" / New Workspace
  creates "Workspace N" without a prompt (rename from the workspace menu).

## New tab page, bookmarks, settings, tab menu, video controls (latest)
- `+` opens `lumen://newtab` (built by `internal_page()` in src/app/main.c): left bar Bookmarks / History / Settings.
  Settings actions go through `apply_set()`; persisted in settings.txt (SDL pref dir) plus bookmarks.txt / history.txt.
- Toolbar: SF Symbol `arrow.clockwise` refresh, bookmark star right of it (`HB_STAR`, `bm_toggle`).
- Settings: Lite mode (`offscreen_media_eviction`, same as LUMEN_LOWMEM=1), CPU limit n%/m s/t% (`cpu_pct/cpu_secs/cpu_lim`),
  always-show video controls (`video_controls`).
- Second click on the active tab -> `mac_tab_menu()`: per-tab CPU policy (`Tab.cpumode/lim`) and per-tab `vctl` (-1 = follow global).
- Native video bar (`video_bars`/`vbar_click`): inside the video's bottom edge for `<video controls>`, below every video when
  always-show is on. Clicks send `lumenmediactl` to page JS (toggle / seek:frac). Test page: tests/media/controls.html.
- YouTube: `YT_VCTL` CSS keeps .ytp-chrome-bottom visible when always-show is on; `page_move_keep()` re-sends mousemove
  for 8 s after the pointer stops so the controls hide later.
- Layout fix: floats / atomic inlines inside inline formatting contexts now get the containing-block height
  (`Layout.inl_cbh`), so YouTube's progress list has height. Paint: axis-aligned `scale()` now applied (display.c),
  so YouTube's scaleX progress fill shows played/buffered correctly.
- Not done: video bar below a video overlaps following content (no layout space reserved); `loop` video state shows paused at end.

## Session 2026-10-08: YouTube search, Lite restore, DMG
- YouTube search works (type, Enter, search button, URL bar updates, shade closes). Fixes:
  - `document.all` via `N.makeAll` (undetectable object) — Polymer Resin sanitizer was replacing `hidden` binding with "zClosurez" because `document.all` was missing, hiding results.
  - `focus()`/`blur()` now fire bubbling `focusin`/`focusout` with `relatedTarget` (dropdown/scrim close).
  - `history.pushState` borrowed from an iframe realm delegates to the receiver (YouTube binds iframe history methods).
  - Click path fires pointerdown/pointerup; a prevented mousedown keeps focus.
  - Cmd+A selects all in page text fields.
- Lite mode: per-tab toggle in tab menu, dark green title, `IntersectionObserver` re-checks every 300 ms, images re-decoded from retained encoded bytes.
- `LUMEN_WINDOW=800x600` sets the window size.
- DMG: `python3 tools/mkdmg.py 0.1.0-preview` -> `dist/`. Signing identity "Lumen Debug Signing" lives in `~/.lumen-signing/` (keychain + p12 + passwords, NOT in repo); unlock the keychain with the password in `keychain-pass` before running, and pass `LUMEN_SIGN_ID=<sha1>`.
- Pushed to ecooxai/lumen branch devin/lumen-early-preview (PR #1) via `git push https://github.com/ecooxai/lumen HEAD:refs/heads/devin/lumen-early-preview`. Promo video tooling in tools/promo. Not done: GitHub release (needs a token), Lite-off CPU on YouTube results page (~30%).

## Video CPU work (branch devin/1791493877-youtube-ads-controls, after v0.1.1-preview)
- Profiling (LUMEN_DEBUG_PAINT=1 prints paint counts + ms/s for style/layout/dl/raster): on a YouTube watch page the
  cost was NOT decoding. YouTube updates progress-bar styles ~30x/s -> restyle -> full layout_run (~11 ms) + full raster
  every video frame (~45% CPU). VideoToolbox decode + sws_scale was small.
- Fix in progress: render() defers DOM/relayout-driven full paints to <=4/s while media plays and no user input for 1 s
  (`a->deferred`, `last_full`, `last_input`); video frames still go through the GPU punch path every frame.
- GPU YUV path (done): VideoToolbox CVPixelBuffer NV12 planes are copied into Image.yuv (no av_hwframe_transfer_data,
  no sws_scale) and uploaded as R8 + RG8 textures; WGSL shader does BT.601/709 + range conversion. Fallback: BGRA.
  Env: LUMEN_NO_YUV=1 (BGRA path), LUMEN_NO_HWDEC=1 (software decode). Local 1080p30 H.264 file (~/sites/v1080):
  hw+yuv 18.5%, hw+bgra 33%, sw 44%, Chrome 27.5% (top, 2x10 s). YouTube 1080p: 31-39% (rest is YouTube JS/layout).
- Deferred paints no longer rebuild the display list from a stale layout (restyle freed styles -> crash in cache_get);
  while deferred only the video texture is swapped using the saved rect (App.vown/vrect).
- Next: paint-only style changes (transform/opacity/color) should skip layout; true zero-copy via IOSurface->Metal texture.
  paint-only style changes (transform/opacity/color) should skip layout entirely.

## Video CPU, round 3
- Tried `thread_count = 1` for VT decode: no gain over 6x10 s windows (22-24% both ways); reverted.
  Short 10 s windows on this VM swing 12-24%, so use >= 60 s of samples before believing a change.
- `9e5dca5`: video-only frames reuse the saved video rect/owner and skip the display-list rebuild.
- VM floor (ffmpeg CLI, 1080p30, `-re`): VT decode only 6.3%, VT + transfer 6.6%, software 14.6%.
- Present costs ~0 (video in a background tab costs the same CPU); audio ~3.5%.
- YouTube 1080p still ~33%: main thread ~22% (YouTube JS ~9%, `getBoundingClientRect` -> full
  `layout_run` ~3%, render ~4%). Next: incremental layout for `n_rect`/`h_sync`, paint-only style
  invalidation, IOSurface zero-copy into Metal textures.
- `c5e83ab` paint-only restyle: recalcs that change only paint-time properties update the ComputedStyle in place
  (`paint_only`/`adopt` in src/css/style.c) and `style`/`class` attribute writes bump `dom_version` but not
  `layout_version`, so `restyle()` only sets `relayout` when needed. `LUMEN_NO_PAINT_ONLY=1` forces relayout.
  Correctness page ~/sites/po/index.html (getBoundingClientRect after style/class/text changes) passes.
  CPU: YouTube content 1080p measured 28-33% with it vs 23-28% without in back-to-back 6x10 s runs; another
  pair (one during an ad) went the other way. Layout ms/s dropped 24 -> 8 in one pair, equal in another.
  Conclusion: run-to-run noise on this VM (+-5 points) is bigger than the main-thread savings; needs a real Mac.

## ChatGPT session (2026-10-09)
- Login verified in native Lumen: email -> password -> TOTP -> logged-in chatgpt.com home.
- "hi" sent via ProseMirror composer (contenteditable support); conversation created, but the reply rendered as "This response couldn't load".
- Root cause candidate: WebSocket was a stub; ChatGPT streams replies via wss://ws.chatgpt.com. Native RFC 6455 WebSocket now in src/net/http.c (net_ws_*), JS in 07b-net.js; verified against echo server.
- Next: retest reply rendering with real WebSocket; fix remaining errors.
- Fixes this session: document.forms.namedItem (HTMLCollection), lazy console formatting, contenteditable editing, document.timeline, iframe.sandbox DOMTokenList, worker Blob/File + URL.createObjectURL, WebSocket.

## 2026-10-09 update
- UI: a workspace with one tab is "compact": toolbar + URL bar sit in the title bar, `+` new-tab button right of the URL (`g_compact`, `update_compact()` in src/app/main.c). 2+ tabs restore tab strip + URL row.
- WebSocket WPT: full `websockets/` dir via real `wpt serve` = 554/588 (94.2%). Setup: sparse clone ~/src/wpt (resources tools websockets webrtc common docs infrastructure interfaces), append tests/wpt/serve.py REPORT to resources/testharnessreport.js, `./wpt serve --no-h2 --config /tmp/wptcfg.json` with `{"check_subdomains": false, "server_host": "127.0.0.1"}`, then `WPT_URL=http://web-platform.test:8000 LUMEN_HOST_MAP=web-platform.test=127.0.0.1 tests/wpt/run.sh out/wpt-ws websockets`. Remaining: bfcache/unload, multi-globals, cookies timing, initUIEvent, NUL/windows-1252 URLs, serialized connects, basic auth.
- Workers: MessagePort transfer main<->worker works (tests/js/worker-ports.html).
- ChatGPT reply ("This response couldn't load"): rendered by DIL runner iframe https://cdn.platform.openai.com/deployments/dil/v14/runner.html (sandbox=allow-scripts). Its blob worker answers healthCheck correctly in jsrun (protocolVersion 14). Iframe gets createRunner then disposeRunner from chatgpt.com; next: capture createRunner payload + frame->parent replies (LUMEN_FRAME_PRE hooks) to find what fails.
- WebRTC: JS implementation in `src/js/prelude/07r-rtc.js` (RTCPeerConnection, SDP offer/answer, ICE candidates/states, RTCDataChannel, transceivers/senders/receivers, certificates, stats, fake `getUserMedia` tracks). Transport is in-process loopback: peers in the same Lumen process find each other by ICE ufrag (`rtcPeers`). No real UDP/DTLS/SCTP yet (needs native stack, e.g. libdatachannel from source). WPT: `WPT_URL=http://web-platform.test:8000 LUMEN_HOST_MAP=web-platform.test=127.0.0.1 tests/wpt/run.sh out/wpt-rtcN webrtc` (wpt serve on 127.0.0.1:8000); baseline 6/1158 -> 927/1039 (89.2%). Regression: `tests/js/webrtc.html`. Remaining gaps: per-m-section transports before BUNDLE, codec negotiation details in getParameters, ICE disconnected/failed states, DataChannel transfer to workers.
- Module workers now supported (d6d0640, a924029; tests/js/worker-module.html). ChatGPT's "Orb warmup" uses one. DIL reply still "couldn't load" after this. DIL frame receives createRunner (compiledDil ~5KB, data, dilComponents, statePersistence, globals) then disposeRunner; wrapping `event.source.postMessage` in LUMEN_FRAME_PRE logged no replies, so check whether Lumen's iframe `MessageEvent.source` is a real parent WindowProxy, or whether the runner replies on a transferred port.
- ROOT CAUSE of "This response couldn't load": MessagePort.postMessage ignored its transfer list. DIL frame hands the host a port in `ready`; host sends `createRunner` over that port with a second port, and the frame forwards runner ports to its worker the same way. Fixed in 2e6469b (page + worker + cross-realm `_deliver`; tests/js/port-xfer.html). Runner now compiles/evaluates/renders and the error is gone. Debug tip: frame pre that patches `URLSearchParams.prototype.get('dil-diagnostics')` to '1' makes the runner post `kind:diagnostic` records to the parent.

- ChatGPT: verified natively in Lumen (login email/password/TOTP, send "hi", reply "Hi! What can I help you with today?" renders). Last blockers were MessagePort transfer-through-port (2e6469b) and cross-realm `structuredClone` of ArrayBuffer (6dd8448, test `tests/js/port-xrealm.html`).
- Benchmark (2026-10-09, macOS VM, 7 tabs: local 1080p H.264 video + 6 long ChatGPT chats, 2 runs each; YouTube gives both browsers "confirm you're not a bot" here): RAM used Lumen 923/1326 MB vs Chrome 1285/2146 MB; footprint 1960/1832 vs 2448/2539 MB; CPU 17.2/16.6% vs 25.4/28.6%. Scripts in ~/bench (bench.sh NAME SETTLE cmd..., chk.mjs, cdp.mjs). Release held until ChatGPT UI parity fixes are done (some Lumen tabs rendered less text; one cause was a 429 rate limit from repeated test loads).
- ChatGPT UI parity: fixed unitless calc() line-height (Tailwind `--text-sm--line-height: calc(1.25/.875)` was treated as px, so sidebar rows were ~1px tall). Still to check: sidebar border colour, header icons (`<use>` sprites), composer box, avatar colour, video `width:100%` sizing.
- ChatGPT UI parity progress (2026-10-09): fixed flex column min-height:auto (673f9ae), custom-property var() resolved after cascade (e3be8a5), 0% flex-basis in indefinite container = content (1d1ec0e), column-reverse scrollers start at end (this commit). Sidebar now matches Chrome. Conversation text now shows but turns OVERLAP and text runs to right edge: next look at ChatGPT's virtualized turn positioning (inline top/transform from measured heights, ResizeObserver/offsetHeight) and thread max-width vars. Probes: ~/bench/probe*-lumen.js via LUMEN_PRE_FILE, Chrome via `node ~/bench/cdp.mjs 9334 eval`.
- ResizeObserver now re-checks sizes every 100ms while observers are live (it only fired once before, so ChatGPT's virtualized turns kept placeholder heights and overlapped). Test: `LUMEN_CONSOLE=1 ./build/lumen file://$PWD/tests/layout/resize-observer-late.html` (build/jsrun has no layout, so size tests must use build/lumen). Remaining ChatGPT diffs vs Chrome: thread column right-shifted/overflowing to window edge (check container queries + `--thread-content-max-width`, TW 680 vs content at x=620..1260), link underline drawn as strike-through, composer sometimes missing at bottom; then rerun benchmark (~/bench/bench.sh).
- ChatGPT UI parity (2026-10-09, later): abs boxes whose containing block is outside a scroller no longer scroll/clip with it (ChatGPT composer lives inside `.thread-scroll-container` but is positioned against `group/thread-scroll-layout`; 82a1986, test `tests/layout/colrev-abs.html`); `<use href="file.svg#id">` resolves external sprite documents, fetched once per URL via `svg_ext_ref_hook` in main.c (d940347, test `tests/layout/svg-use-external.html`); real `color-mix()` (c980230, `tests/js/color-mix.html`). Still different from Chrome: composer placeholder "Ask ChatGPT" + mic button, file-card divider line, `getComputedStyle` returns "" for border/box-shadow longhands, `scrollHeight` returns own height (03-element.js) and element `scrollTop` is JS-only (not wired to native scroll).
- FileReader.readAsDataURL spread the whole buffer into String.fromCharCode and hit "Maximum call stack size exceeded" on ChatGPT (152b400, `tests/layout/filereader-large.html`, run with build/lumen; jsrun's Blob has no bytes).
- Benchmark gotchas: Chrome blocks unmuted autoplay, so launch it with `--autoplay-policy=no-user-gesture-required` and check `video.paused` via CDP (`~/bench/vchk.mjs`); ChatGPT rate-limits repeated conversation loads ("Could not load this ChatGPT conversation"), so check every tab's text (`lumchk2.js`, `tchk.mjs`) and only count runs where all six loaded.

## Memory + scroll (2026-10-09, 6 ChatGPT tabs)
- Scrolling: wheel now scrolls the innermost `overflow:auto|scroll` box under the pointer (`wheel_scroll` in main.c), falling back to the page; element `scrollTop/scrollLeft/scrollTo/scrollBy/scrollHeight` are backed by native `Node.scroll_x/y` (`jsg_scroll`/`jsg_set_scroll`, N.scrollPos/N.setScroll). Test: tests/layout/scroll-api.html (run with build/lumen).
- `LUMEN_AUTOSCROLL=x,y,dy,start_ms,count` pushes wheel ticks every 16ms (CPU-while-scrolling measurement).
- Where RAM goes (footprint ~1.8-2.0 GB): page isolates ~1.0-1.3 GB (140-300 MB per ChatGPT tab), 18 worker isolates (3 per ChatGPT tab) ~370 MB, script sources ~50 MB per tab. DOM nodes (~20k, 216 B), ComputedStyle (~12k, 880 B), parsed CSS (12 MB) are small. malloc goes through PartitionAlloc (V8 shim) so malloc_zone stats are useless; use `LUMEN_MEM_STATS=1` (lumen-mem / lumen-mem2 lines), `LUMEN_MEM_DETAIL=1` (per isolate), `LUMEN_HEAP_SNAP=path` (heap snapshot of first isolate after 50s), analysis scripts ~/bench/snap.py, snap2.py.
- Done: big script sources (>=64 KB) are external strings shared across isolates by SHA-256 (`jsrc`), idle workers GC once after 10s, background tabs get IsolateInBackgroundNotification + one LowMemoryNotification after 15s (`js_set_background`).
- Tab sleep (ChatGPT-specific): idle background `https://chatgpt.com/` tabs drop their JsCtx after 60s (no fetch in flight) and keep DOM/layout on screen; activating reloads. `LUMEN_TAB_SLEEP_MS` (0 = off), `LUMEN_TAB_SLEEP=all` for any site. 6 ChatGPT tabs at 110s: 418 MB footprint with sleep vs 1317 MB without (was ~1.8-2.0 GB before this work).
- Scroll paint: page scroll by whole device pixels shifts the page canvas and rasters only the exposed strip + position:fixed bounds (`DisplayList.fix`); element-scroller wheel repaints only the scroller rect (`App.srect/sdirty`). Wikipedia 60 wheel ticks/s: 50% -> 38% CPU (remaining cost: dl_build ~55ms/s + event loop); ChatGPT thread scroll ~6%, idle ~2%. `LUMEN_DEBUG_PAINT=1` shows partial/full counts.
- Next ideas: cache display list across pure scrolls (dl_build is now the main per-frame cost); skip layout_hit on every wheel tick (cache scroll target for a gesture); throttle timers/rAF in background tabs (document.hidden is still always false).

## Stability / settings (Oct 9)
- Tab sleep is opt-in (Settings toggle, `tab_sleep=` in settings.txt); `LUMEN_TAB_SLEEP_MS` still overrides.
- UA: Chrome 150 default; Settings offers Chrome 150 / Firefox 150 / Lumen (`user_agent=` 0/1/2). `navigator.vendor` / `userAgentData` follow the UA.
- Crash recovery: SIGSEGV/SIGBUS on the main thread `siglongjmp`s back to the event loop; the tab's page is abandoned (leaked), the tab reloads (max 3 times) and a dismissable red notice is shown. `LUMEN_CRASH_TEST=<ms>` triggers a test fault. Faults on other threads still terminate. The handler logs the faulting pc/lr (use `atos -o build/lumen -l <load> <pc>`).
- ChatGPT sidebar toggle crash: `click_page` read `b->node` from a layout freed by handlers/restyle; now walks from the pinned target. `js_dispatch` pins targets until the event batch ends (`js_release_pins`).
- Open: collapsed ChatGPT sidebar keeps the aside at 340px (`--app-shell-left-panel-width` resolves empty on the aside in Lumen; Chrome gets it from `--app-shell-animated-left-panel-width` set inline on `.Layout-*`).

## CSS / fonts / audio (Oct 9, later)
- `@container` rules are now conditional (`ContainerCond` on `Rule`, `css_container_eval` in parse.c, `container_matches` in style.c). Size comes from `Node.cq_w/cq_h` set by `track_containers` after layout; `style()` queries read the container's custom props.
- `@font-face`: `sheet_fonts`/`font_pump` in main.c, `font_declare`/`font_loaded` in font.c. WOFF2 skipped (no brotli in FreeType build); TTF/WOFF fallbacks used. Check face-memory lifetime if fonts misbehave.
- Layout-dependent tests (`cq-units`, `container-query`, `webfont`) must run in `build/lumen`, not `jsrun`.
- Audio: `play()` before metadata never opened the SDL device (silent playback). Fixed in media.c. VM has no audio device: verify with `SDL_AUDIO_DRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE=out.raw` (F32LE stereo 48k) + `~/bench/audio/analyze.py`.
- Missing: Web Audio API (`AudioContext` undefined).

## ChatGPT layout parity notes (min()/max(), pseudo, cell valign)
- `Length` now carries a deferred second operand (`mm`, `px2`, `pct2`) so `min(100%, Npx)` / `max()` resolve at layout time via `res()`; arithmetic in `calc_term`/`calc_expr` propagates it, `clamp()` with percentages still uses the old best-guess. Test: `tests/js/minmax-pct.html` (full `lumen`, not jsrun).
- Absolute/fixed/floated `::before`/`::after` are blockified in `build_pseudo` (ChatGPT composer placeholder).
- Table cells with explicit `height` align their real content (`scroll_h`), fixing KaTeX `vlist` superscripts. Test: `tests/js/cell-valign.html`.
- With Lumen's 84px tab strip, ChatGPT's main area is <856px (53.5rem) so it uses its compact 640px column; at Chrome's width (`LUMEN_NO_SIDEBAR=1 LUMEN_WINDOW=1200x960`) it is 768px like Chrome. Not a bug.
- Open: `getComputedStyle` returns "" for shorthands (`padding`) and for `vertical-align`, `float` and pseudo-element styles (`getComputedStyle(el, '::before')`).

## ChatGPT live streaming (fixed in 6599817: fetch streams heads + decoded chunks; manual check ~/bench/stream/srv.py)
- Net layer has no body streaming: `http_once` (src/net/http.c) buffers the whole body; `NetChunkFn` is declared in net.h but unused. `fetch()` resolves only after the response completes, and `Response.body` is a single-chunk stream (07b-net.js).
- Repro: send "Count from 1 to 60 in words, one per line." in a new chat (`~/bench/fc5pre.js` tees the fetch). `POST /backend-api/f/conversation` returns the full ~54 KB `text/event-stream` in 1 chunk ending with `message_stream_complete` and `[DONE]`, but `main.textContent` stays ~158 chars (only the prompt) until reload. After reload, the reply renders fully (same length as Chrome, 5345 chars for 1..300).
- Plan: stream response heads and chunks from the worker to the main thread (queue drained in `net_poll`), resolve `fetch` on headers, and feed the body `ReadableStream` per chunk. Buffer when `content-encoding` needs whole-body decompression unless an incremental decoder is added. Then recheck whether the live reply renders.
- Interactive check (`~/bench/fcheck.js`): model picker, profile menu, search, chat actions, More, rate, copy, edit/cancel all match Chrome. Settings via `#settings` hash opens in neither browser.

## Typing CPU / damage raster / CPU readout (2026-10-09)
- `layout_box()` memoises results per box (mode + containing block + forced size) and re-registers
  absolute descendants from `L->alog`; tables are excluded. `LUMEN_DEBUG_LAYOUT=1` prints runs/s and hit rate.
- `font_get()` has a thread-local memo invalidated by `font_gen` when web fonts change.
- Damage raster (`render()` in main.c): each page display item gets a hash + bounds (`dl_sigs`);
  on DOM/layout/UI-only frames only the union of changed items is rastered. `LUMEN_CHECK_DAMAGE=1`
  compares against a full raster and logs mismatched pixels (0 on ChatGPT typing).
- Typing in the ChatGPT composer is now ~17-30% CPU (was 66-95%); remaining cost is ChatGPT's JS
  (`js_dispatch`) plus synchronous layouts from its geometry reads (`h_sync`). Target is <10%.
- Settings > "CPU usage" (`cpu_hud`, off by default, on in debug builds) shows Lumen's CPU and RAM (GB)
  at the bottom of the workspace sidebar every 2 s; clicking opens a popup with host CPU and RAM.
- Next: ChatGPT composer click-to-caret and selection (paint_edit_caret always draws at the end).
- Long ChatGPT conversations: nested flex columns measured each item (fh=-1) then laid it out again with
  fh = the same height, so the memo missed and work doubled per level (~2^11 calls; 490ms layouts, 100% CPU).
  `layout_box()` now treats a forced height equal to the natural one as a hit unless a child resolves
  percentages/insets against it (`hdep`): 490ms -> 7ms. `LUMEN_DEBUG_LAYOUT=2` prints the hottest chain.
- `LUMEN_DEBUG_JS=1` prints per-event-type dispatch counts and ms each second.
- contenteditable selection: JS `Selection` is canonical; every range change mirrors to `Document.sel[]/selo[]`
  (UTF-8 byte offsets) via `N.selSet`. Native clicks/drags/dblclick/triple/Cmd+A/arrows dispatch `lumensel-*`
  events into the prelude, which moves the JS selection. `edit_sel_sync()` copies it into `g_tsel` so the
  highlight and Cmd+C reuse the page-selection code; the caret paints at the actual focus offset.
  Text inside contenteditable counts as selectable even under `user-select: none` (ChatGPT composer).
- Wheel: whole-number SDL deltas = mouse notches -> fixed `WHEEL_STEP` (120px); fractional (trackpad) stay 40px/unit.
  Settings "Smooth scrolling" (`smooth_scroll`, off by default) animates notches (30% of remainder per 16ms frame).
  Wikipedia, 60 notches/s: ~21% CPU (was 38%); remaining cost is display-list rebuild (~36 ms/s).
- Web Audio (`src/js/prelude/07w-audio.js`): graph renders in JS in 128-frame quanta, pushed as stereo f32 @48 kHz
  through `N.waPush` to one SDL stream per AudioContext (`wa_*` in media.c, closed with the JS context).
  Timer runs only while sources play (idle clock advances by wall time). decodeAudioData = FFmpeg `media_decode_pcm`.
  Real: Gain, Oscillator (+PeriodicWave), BufferSource, ConstantSource, StereoPanner, Delay, Analyser, OfflineAudioContext.
  Pass-through stubs: BiquadFilter, DynamicsCompressor, Convolver. Missing: AudioWorklet, MediaElement/MediaStream sources.
  Verified with SDL disk driver (`SDL_AUDIO_DRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE=...`): 440 Hz osc + decoded mp3, ~1% CPU.

## ChatGPT feature audit (latest)
- Working: send/stream/stop, regenerate, edit, copy (navigator.clipboard → SDL clipboard), like/dislike, Share (this ChatGPT build creates the link and copies it; no dialog), rename, search dialog, projects expand, profile menu, Settings page.
- Fixed this round: getBoundingClientRect transforms/scroll (`jsg_vrect`), hit testing honours transforms and `pointer-events:none` on text/inline boxes, Chrome-like pointer event fields, clipboard + `ClipboardItem` + `execCommand('copy')`, box-shadow lists, backdrop-filter alpha approximation (not a real blur).
- Screenshots from `screencapture` are ~0.98x of screen points (1568 vs 1600): scale coords by 1.0204 before `click.js`.
- Also working: Settings page, Appearance dark/light switch, Cmd+Shift+O new chat, Cmd+K search, Escape.
- Keyboard: Cmd/Ctrl shortcuts go to the page first (browser keeps Q/W/T/N/L/Tab/1-9); unfocused keys go to body; events carry modifier flags and `code`. Test page: `tests/layout/keys.html` (drive with System Events after `set frontmost of (first process whose unix id is $P)`).
- `:hover` now works (NF_HOVER set from page_move; only nodes whose hover state changes are restyled). Before this, ChatGPT sidebar rows never showed their "..." (Chat actions) and pin buttons. Row menu shows Rename/Unpin/Share/Archive/Delete/Move to project like Chrome; Delete opens the confirm dialog and sends `DELETE /backend-api/conversation/id/<id>`. Regression: tests/layout/hover.html (`RESULTS hover row:PASS group:PASS inherit:PASS has:PASS off:PASS`).
- Hover invalidation (`style_hover_affects` / `style_hover_has` in src/css/style.c): `idx_build` records which id/class/tag keys have `:hover` rules (self, subtree, or parent subtree for `+`/`~`) and which have `:has(...:hover...)`; `page_hover` restyles only those nodes. Without this, every hover change restyled the whole ChatGPT document (~100-350 ms each).
- Archive and delete verified on throwaway chats (PATCH / DELETE sent, row removed, navigates home).
- Still to check: model picker/tools, GPT pages.
