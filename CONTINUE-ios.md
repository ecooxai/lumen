# Lumen on iOS / iPadOS (+ Mac from the same UIKit build) — handoff notes

Branch `devin/ios-ipados-port` (base: `devin/linux-x11-vulkan-port` v0.2.0 + main's bundled-ICU fix).
Goal (user): run the from-scratch Lumen engine (not WebKit) on iPad, fast for daily use; same build on Mac.
App Store acceptance is not a goal (sideload / Xcode run).

## Toolchain (outside the repo)
- Dep sources: ~/src/deps (openssl 3.5.4, brotli 1.1.0, zstd 1.5.7, libpng 1.6.50, libjpeg-turbo 3.1.1, libwebp 1.6.0,
  giflib 5.2.2, freetype 2.13.3, harfbuzz 11.2.1, dav1d 1.5.1, ffmpeg 9.0.2, SDL3 3.4.16).
- `ios/build-deps.sh ios|sim|cat [dep...]` -> ~/ios-deps/<plat>/{include,lib} (static libs). wgpu-native iOS prebuilts
  (v29.0.1.1 release zips) unzipped in ~/src/wgpu-ios.
- V8 15.6.75.16 (same as the Homebrew bottle the desktop build uses): ~/src/v8ios/v8 (gclient, target_os ios+mac),
  out/ios-simulator and out/ios-device, args: jitless (iOS forbids JIT), drumbrake wasm interpreter,
  pointer compression, no sandbox, use_custom_libcxx=false, monolith -> obj/libv8_monolith.a.
  Build: `third_party/ninja/ninja -C out/ios-simulator v8_monolith` (depot_tools in ~/src/depot_tools on PATH for gn).
  gn gotcha: with v8_jitless you must also set v8_enable_sparkplug/maglev/turbofan=false explicitly.
- brew: cmake ninja meson nasm pkgconf (plus desktop deps v8 wgpu-native webp ...).
- V8 15.6 drumbrake patch (needed to compile the wasm interpreter): in src/debug/debug-wasm-objects.cc add
  `bool DeclaresLocals() override { return false; }` to `DebugWasmInterpreterScopeIterator`.
- Also `v8_use_metagen_instance_types = false` in every out/*/args.gn (metagen libclang pass cannot find libc++ headers with use_custom_libcxx=false).
- JIT builds (`JIT=1`): out/{ios-simulator,ios-device,catalyst}-jit = same args.gn minus v8_jitless and the
  sparkplug/maglev/turbofan=false lines, plus `v8_enable_lite_mode = false` (gni/v8.gni forces lite = jitless for
  target_os=ios unless use_blink; without it the target lib is jitless while host mksnapshot is not, and start-up
  dies in `Deserializer: magic_number_ == kMagicNumber`). Local V8 patches: src/base/build_config.h enables
  V8_HAS_PTHREAD_JIT_WRITE_PROTECT for iOS, src/base/platform/platform-darwin.cc resolves
  pthread_jit_write_protect_np via dlsym on iOS (SDK marks it unavailable).
- Simulator JIT app is signed without entitlements: AMFI refuses ad-hoc entitlements in the Simulator
  (`Launchd job spawn failed`, `Security policy issue`) and Simulator processes may map MAP_JIT anyway.
- out/catalyst = ios-simulator args with target_environment = "catalyst". Chain: sim -> device -> catalyst (logs ~/lumen-ios-logs/v8-*.log).
- wgpu-native for Catalyst: ~/src/wgpu-native @ v29.0.1.1, built with rustup stable (PATH=~/.rustup/toolchains/stable-*/bin,
  RUSTUP_TOOLCHAIN=stable to override its rust-toolchain.toml), target aarch64-apple-ios-macabi.

## Build / run
- `make PLAT=sim app` -> build-sim/Lumen.app ; `xcrun simctl install booted build-sim/Lumen.app && xcrun simctl launch --console booted com.ecooxai.lumen`
- `make PLAT=ios ipa` -> build-ios/Lumen.ipa (ad-hoc signed; re-sign with Sideloadly/AltStore/Xcode for a real iPad)
- `make PLAT=cat app` -> Mac Catalyst build of the same UIKit app.

## Code
- src/app/iosui.m implements the macui.h hooks with UIKit (alerts/action sheets, SF Symbols); macui.m stays for the AppKit build.
- main.c `LUMEN_IOS` (TARGET_OS_IPHONE): touch -> tap/double-tap clicks, one-finger pan scroll with momentum (touch_event/touch_fling),
  background lifecycle (no GPU work when backgrounded, cookies saved), borderless full-screen window, bundled cacert.pem via SSL_CERT_FILE.
- font.c picks the iOS font table at runtime when /System/Library/Fonts/Core/Helvetica.ttc exists.

## Status (iPad Air 11" M4 Simulator, iOS 26.5)
- Works: Wikipedia, Hacker News, link taps, touch scroll + momentum, URL typing (hw keyboard), new tab page, tabs,
  YouTube home/search/watch with ad + skip and MSE playback (software decode: VideoToolbox hwaccel init fails in the
  Simulator only). Google search hits reCAPTCHA from this VM IP (not solved, by design).
- CPU: idle ~1% after blocking in CFRunLoop (ios_wait_event); YouTube 1080p-ish lofi playback ~7-70% in Simulator; RSS ~470-600 MB.
- Long press (>500 ms) = right click. Focused fields call SDL_SetTextInputArea (text_area), so SDL's UIKit view
  controller slides the view up to keep the field above the on-screen keyboard (verified in Simulator).
- Typing: ~0-1% CPU between keys, single-second 25-45% spikes when the keyboard animates in/out.
- TODO: pinch zoom (needs a page zoom factor: layout viewport /zoom, page canvas scale *zoom, input coords /zoom),
  safe-area insets for iPhone notch (iPad full-screen is fine), real-device test of VideoToolbox + perf.

## ChatGPT unauth landing: open investigation (2026-10-15)
- Loads, renders, composer accepts typing; but the octane home client never marks ready
  (`documentElement.dataset.octaneHomeBehaviorReady` stays unset, `__webMobileStartupRecovery.complete()` never
  runs), so submit falls through to native form submission -> "Method Not Allowed".
- Chain: home-client `Ie = Promise.all([E(document), loaders import])`; `E` = shell-shared export `dn` =
  `hd` -> `pd()` = `id("application-presentation-runtime", () => v(() => import(runtime), deps r([0..32])))`.
  `pd()` stays pending forever in Lumen although `import(runtime)` itself resolves at once -> suspect the
  vite preload helper (`v`, waits on modulepreload/stylesheet deps) or `id`'s retry path.
- Verified fine in Lumen: rIC, rAF, MessageChannel, postMessage, microtasks, fonts.ready, TLA, dynamic import
  of the loaders and shell-shared modules, no unhandled rejections logged.
- Debug recipe: `LUMEN_PRE_FILE=dump.js LUMEN_CONSOLE=1 LUMEN_DEBUG_MODULES=1` and fetch() the asset text from
  inside Lumen (curl gets a Cloudflare challenge).
- Narrowed further: `hd` = `pd().then(n(document,true))`; `n` -> runtime `runWithParsedApplicationDocument`, which
  waits for `octane-application-parsed` / `window.__octaneApplicationParsed` (this IS set to true in Lumen), then
  `await a()` (the presentation body in shell-shared `function n(e,t,n){let a=L(e);...}` – uses
  `_earlyVisualKeyboard`, `web-mobile-root`, `[data-octane-shell-layout]`). The pending await is inside that body;
  next step: dump `String()` of the runtime's `rn` / `createApplicationPresentation` and probe each await.

## Status 2026-10-15 (later)
- Simulator JIT crash #2 (SIGBUS in `MemoryChunk::MemoryChunk`, code page `---`): V8 iOS assumes BrowserEngineKit,
  `OS::RecommitPages` only madvises. Local V8 patches: `src/base/platform/platform-posix.cc` RecommitPages on
  V8_OS_IOS also mprotects to the requested access; `src/heap/code-range.cc` keeps the kRecommitOnly RWX commit
  when V8_HAS_PTHREAD_JIT_WRITE_PROTECT. Simulator: `pthread_jit_write_protect_supported_np()` = 0, MAP_JIT pages
  are plain RWX (tested ~/jittest/t.c, t2.c). Rebuild out/ios-simulator-jit then `make PLAT=sim JIT=1 app`, test.
- Then: out/catalyst-jit, out/ios-device-jit (same patches), `make PLAT=cat JIT=1 app`, `make PLAT=ios JIT=1 ipa`.
- New: workspaces get a default SF Symbol icon (main.c ws_new); file:// viewer (main.c file_page): compact folder
  list, image/video 1:1 device px with zoom steps 10..300%, audio card, OBJ SVG viewer (drag rotate).
  Video frame in LUMEN_SHOT screenshot rendered white – check whether that is the shot path or real.
- Pending user asks: top-sites pass (YouTube, Facebook, ChatGPT, Claude, Gemini, X, Google, Bing, Gmail,
  Wikipedia) with RAM/CPU vs Chrome; getDisplayMedia (ReplayKit on iOS, Android); ChatGPT stall: runtime `rn`
  awaits `ve.referenceSheetsDelivery$` status "ready" (never reached in Lumen).
- Done since: getDisplayMedia (capture.c `cap_screen_*`, fed from main.c present path, page area only; GPU-path
  video frames are not in `a->frame.px`, so captured/LUMEN_SHOT video shows white unless LUMEN_NO_GPU=1) and
  MediaRecorder (capture.c `cap_rec_*`, fragmented MP4, `LUMEN_REC_DUMP=file.mp4` writes the result for ffprobe).
  Tests: ~/fvtest/gdm.html, ~/fvtest/rec.html with LUMEN_PERMISSIONS=grant LUMEN_CONSOLE=1.
- MediaRecorder: video only (no audio mux), one blob at stop (timeslice ignored).
- Top-sites pass (macOS desktop build, VM loaded by V8 builds): 10 tabs in one Lumen process ~211 MB final /
  320 MB peak, avg CPU 23% vs Chrome 10 tabs 2.1 GB / 2.37 GB peak, 52% CPU (~/topsites/run.sh, each.sh).
  All 10 lay out like Chrome (montage ~/topsites/montage.png). Claude stops at Cloudflare's bot check (not
  bypassed). Fixed: DOMStringMap (Facebook), window.postMessage() arity (Turnstile), WebCrypto algorithm getters,
  IndexedDB (new 07c-idb.js, in-memory per origin, not persisted across restarts yet; test ~/fvtest/idb.html).
- Catalyst JIT V8 ninja was SIGSTOPped (kill -CONT 84646 + children) to let ios-simulator-jit finish first.
- JIT Simulator VALIDATED (2026-10-15): `make PLAT=sim JIT=1 app` with the rebuilt out/ios-simulator-jit
  (lite_mode=false + code-range recommit patch) prints `lumen: V8 JIT enabled`, loads Wikipedia and YouTube
  (2.2 s) without crashing; idle ~1% CPU, ~280 MB RSS on Wikipedia, ~640 MB on YouTube home.
- Module scripts / import() from blob: URLs now work (js.cc g_blob_mods, filled by URL.createObjectURL for
  JS-typed blobs). Bing's remaining `_w is not defined` is site-side (also seen on Chrome-UA HTML).
- Catalyst: only out/catalyst-jit is being built (jitless catalyst out/ still has ~4000 steps; the JIT build
  falls back to jitless at runtime anyway, so it covers both).
