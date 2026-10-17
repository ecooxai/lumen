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

- 2026-10-15 later: runtime `Ka(onUser,..., n, cb, ma.prepare)` builds the presentation model; its
  `referenceSheetsDelivery$` = `X(o,"reference-sheets-delivery",()=>requested?u(null):Z)` where `u` awaits the 6th
  arg `s` = `ma.prepare`. So the stall is whatever `ma.prepare` awaits. Verified OK in Lumen: dynamic
  `<link rel=stylesheet|modulepreload|preload>` fire load/error (~/fvtest/cgpt/preload.html). Dump scripts:
  ~/fvtest/cgpt/dump*.js (LUMEN_PRE_FILE). Sample CPU with `pgrep -n lumen`, not `$!` (that is the env wrapper).
  `ma = dt(()=>U("content-reference-sheets",()=>rt(()=>import("./content-reference-sheets-*.js"),[],..)))`;
  importing that chunk directly in Lumen resolves in ~245 ms (dump5.js). `U` = shell-shared `dd` (per-name
  cached module loader with retry), `dt` = lazy `{component,prepare}` wrapper, `rt` = vite preload (deps `[]`)
  (dump7.js). None of these obviously hangs, so reference-sheets may be a red herring: next, instrument
  `ma.prepare`/`dd` at runtime (patch via LUMEN_PRE_FILE fetch hook) and log which await in `n(e,t,n)` stays
  pending. Asset hashes change between runs (deploy churn); dumps locate files by name prefix.
  shell-shared `md(doc)` (activation) awaits `ud` (prerendering, false in Lumen) then `xl().restoration`
  (session-restoration manager). Runtime probe (dump10.js: import shell-shared, call its readable namespace
  objects): awaitPreparedDocumentActivation + activation.ready RESOLVE; loadApplicationPresentationForStartup
  stays PENDING. ROOT CAUSE AREA (dump11.js): `To.loadPresentationStyles(document)` never resolves
  (`arePresentationStylesReady` = false); it awaits `g(link)` for each stylesheet link `_t(doc)`.
  awaitTextareaFieldSizing resolves. The awaited link is parser-inserted
  `<link rel=preload as=style href=/unauth-mweb/assets/stylex-remaining-*.css>`; swapping it to stylesheet by
  hand makes loadPresentationStyles resolve. Fixed two Lumen bugs: (1) `sync_sheets` keyed by href only, so
  rel preload->stylesheet never loaded/fired load (relchg.html); (2) parser-inserted rel=preload never fetched
  or fired load/onload (relchg2.html) -> now fetched in `page_start_js` with `preload_done`.

## Status 2026-10-15 (later)
- Simulator JIT crash #2 (SIGBUS in `MemoryChunk::MemoryChunk`, code page `---`): V8 iOS assumes BrowserEngineKit,
  `OS::RecommitPages` only madvises. Local V8 patches: `src/base/platform/platform-posix.cc` RecommitPages on
  V8_OS_IOS also mprotects to the requested access; `src/heap/code-range.cc` keeps the kRecommitOnly RWX commit
  when V8_HAS_PTHREAD_JIT_WRITE_PROTECT. Simulator: `pthread_jit_write_protect_supported_np()` = 0, MAP_JIT pages
  are plain RWX (tested ~/jittest/t.c, t2.c). Rebuild out/ios-simulator-jit then `make PLAT=sim JIT=1 app`, test.
- Then: out/catalyst-jit, out/ios-device-jit (same patches), `make PLAT=cat JIT=1 app`, `make PLAT=ios JIT=1 ipa`.
- DONE 2026-10-15: out/catalyst-jit built; `make PLAT=cat JIT=1 app` runs (`V8 JIT enabled`, Wikipedia 1.18 s,
  0-2% settled CPU, wasm tests pass with JIT and with `LUMEN_V8_FLAGS="--jitless --wasm-jitless"`).
  DONE 2026-10-16: out/ios-device-jit v8_monolith finished (4238/4238); `make PLAT=ios JIT=1 ipa` ->
  build-ios-jit/Lumen-jit.ipa (arm64, iOS device, get-task-allow; 4.2 MB zipped). Not run on a real device yet. No separate jitless Catalyst V8 is
  built; the JIT build switches to jitless with `LUMEN_V8_FLAGS="--jitless --wasm-jitless"`.
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
- WebAssembly (test pages ~/fvtest/wasm/index.html, wk.html, perf.html): desktop + Simulator JIT pass validate/
  compile/instantiate, compileStreaming/instantiateStreaming (prelude polyfill, enforces application/wasm for
  http(s)), SIMD, relaxed SIMD, threads/shared memory, exceptions, GC, tail calls, memory64, JSPI, bulk memory,
  Module + SharedArrayBuffer postMessage to workers (worker.cc SerDel/DesDel; worker.cc built -fno-rtti because
  V8 has no RTTI), async compile inside workers (worker loop polls HasPendingBackgroundTasks).
  Jitless V8 hid `WebAssembly` entirely: V8 only implies --wasm-jitless from --jitless on tvOS, so js.cc now
  passes --wasm-jitless for jitless iOS builds and the JIT->jitless fallback (DrumBrake interpreter).
  DrumBrake CHECKs trap_handler::IsTrapHandlerEnabled(), so js_global_init() now calls
  V8::EnableWebAssemblyTrapHandler(true) on every platform (after main.c's crash handler; V8 restores the old
  handler for non-wasm faults). LUMEN_NO_WASM_TRAP=1 disables it. Simulator jitless + JIT both pass the full
  wasm page; 3M JS->wasm calls: jitless 565 ms, JIT 185 ms.
  Desktop (Homebrew V8 15.6) matches d8: 3M calls 263 ms cold, ~25 ms once the wrapper tiers up
  (~/fvtest/wasm/perf3.html). The 1.6 s in index.html is a benchmark artefact (one long top-level async loop).
  HTTP (~/fvtest/wasmhttp, python http.server serves application/wasm): instantiateStreaming/compileStreaming ok;
  wrong MIME, 404, used body and non-Response all reject with Chrome-style TypeErrors.
  crossOriginIsolated: Document.cross_origin_isolated set in main.c from COOP same-origin + COEP
  require-corp/credentialless on https/localhost; N.coi() for window, W.coi for workers (~/fvtest/coi/srv.py).
  COEP does not block non-CORP subresources yet; SharedArrayBuffer is exposed regardless.

### ChatGPT after send (2026-10-15)
- Logged-out send works: `/uc/<id>`, reply "LUMEN OK" (send.js probe).
- After the reply CPU stays 25-90%: ChatGPT store `update`/`getSnapshot` loop calls
  `document.cookie` (~hundreds of thousands of reads, `ae` in octane-shell-shared:8914)
  and `querySelector('input[name="conversationState"]')` repeatedly. LUMEN_SHOT never
  fires while this runs. Worker `conversation-source-dil.worker` throws `reading 'bind'`.
- Loop chain (stack at the 4000th `conversationState` read): React commit (`a-DXSTf3Zh.js`
  kl/Gl/pu/Au/Du) -> passive effect in `conversation-source-dil` (~1114813, deps list incl.
  ResizeObserver-driven `portalTarget/responseWidth` state) -> `$.update` -> getSnapshot.
  The conversationState value is stable (no writes), so an effect dep changes every render.
  Lumen's ResizeObserver reported widths 608,0,656,608,0,... for the portal target: suspect.
  Tasks come via MessagePort (setTimeout-based), so the JS watchdog never fires.
- FIXED (nested ports): the DIL worker gets `{kind: InitializeChannel, port}` with the port
  inside the message data. Lumen only delivered transferred ports via `e.ports`, so
  `e.data.port.postMessage.bind` threw, the DIL channel never initialised and React kept
  re-running the effect (the 40-90% CPU loop). `portSub`/`portRes` now swap transferred
  MessagePorts inside data both ways (07b-net.js + worker.cc). After the reply CPU is ~2-5%.

- FIXED (login dialog): ChatGPT's "Log in or sign up" `<dialog>` stayed at opacity 0 because
  `@starting-style` rules were flattened into normal rules (they pinned the transition's
  start state). parse.c now skips `@starting-style`. Also added `:modal` / `:popover-open`
  (NF_MODAL / NF_POPOVER_OPEN set via `N.setPseudo`) and real `showPopover`/`hidePopover`/
  `togglePopover`. Continue with Google now reaches accounts.google.com/v3/signin/identifier.
- Google sign-in (via ChatGPT "Continue with Google"): the identifier page renders and accepts the
  email, then Google redirects to /v3/signin/rejected ("This browser or app may not be secure").
  That is Google's unrecognised-browser check. Do not spoof Chrome to get around it; ask the user.
  Rejection is the same with the honest Lumen UA; the page's `Error: ge`/`Error: int32` come from BotGuard
  (google.com/js/bg/). The user rejected importing Chrome cookies as a workaround.
- Google sign-in layout fixes: `box-sizing: inherit` (Google's `*{box-sizing:inherit}` reset) now inherits, so
  the two 50% columns sit side by side; `transform-origin: bottom left` (vertical keyword first) maps to the right
  axes; uniform scale() transforms now scale text too (DItem.ts); SVG `<image>` (base64 data: and loaded URLs)
  draws, and clipPath/`url(#id)` resolves inside SVGs decoded for `<img>` (Google "G" logo).
Negative z-index: positioned boxes with z-index < 0 (including ones hoisted from non-context positioned
ancestors like `body{position:relative}`) now paint after their stacking context's background and before its
in-flow content (display.c dl_move_items). This removed the empty grey box on Google's sign-in page (a
`z-index:-1` accounts.youtube.com CheckConnection iframe that should sit under the body background).
- FIXED (idle CPU with paused media): `mediaPoll` in `05-ce.js` ran every 50 ms for as long as
  any `<video>`/`<audio>` existed, even paused. Each tick re-ran display-list building
  (Wikipedia's paused poster video: ~23 partial paints/s, 5-25% CPU forever). The poll now stops
  once no element is playing/seeking/waiting and state has been unchanged for 2 s; `play()`,
  `currentTime=` and attach restart it. Wikipedia idle: ~13% -> 0.3-3% CPU, RSS 226 -> 116 MB.
  Fixture: `tests/js/media-idle.html`.
- FIXED (test harness): `LUMEN_SHOT` forced a repaint on every event-loop wakeup, so every
  run that took a screenshot also repainted on each page timer (YouTube: 2 x 25 ms intervals,
  ~50 paints/s, 15-25% CPU). It now wakes once at the deadline. Earlier top-site CPU numbers
  measured with `LUMEN_SHOT` were inflated; re-measure with the fixed hook.
  Re-measured (each site alone, 30 s, 1 s samples; avg / tail after 20 s / peak RSS):
  YouTube 16.9/5.7%/262MB, Facebook 4.9/1.9/233, ChatGPT 7.9/4.3/170, Claude 13.2/3.7/337,
  Gemini 10.3/2.1/308, X 7.9/1.5/214, Google 5.0/1.2/187, Bing 8.6/2.2/196, Gmail 7.4/2.7/211,
  Wikipedia 5.3/0.8/248.
- ADDED (screen capture on iOS): `getDisplayMedia` on iOS/iPadOS/Catalyst starts ReplayKit
  in-app capture (`ios_rp_start` in `iosui.m`, BGRA or 420 YpCbCr -> ARGB via vImage) and
  `cap_screen_push` records its frames; until ReplayKit delivers it records page pixels.
  The iPad Simulator reports "ReplayKit capture started" but never delivers a sample, so
  Simulator recordings are page pixels; ReplayKit frames are still unverified on a device.
  `LUMEN_NO_REPLAYKIT=1` forces page pixels. No audio track yet; Android untested.
- FIXED (blob: media): `<video src=blob:...>` (e.g. a MediaRecorder result) now plays: the
  Blob is written to `$TMPDIR/lumen-blob-*` and opened as `file://`, deleted on
  `revokeObjectURL` (a never-revoked URL leaks the temp file until reboot).
- FIXED (video sizing): the UA sheet had `video{width:300px;height:150px}`, so a video with
  only a CSS width stayed 150 px tall. 300x150 is now only the default object size.
- KNOWN (test harness): with the GPU on, `LUMEN_SHOT` PNGs show a paused video as a white
  box because the video is drawn as a GPU layer; on screen it is correct. Use
  `LUMEN_NO_GPU=1` when a screenshot must show video frames.
- FIXED (daily use): `localStorage` was in-memory only, so every site lost its settings on
  restart. It is now one process-wide map per origin (shared by tabs) in `jsnative.cc`,
  stored under `<pref>/localstorage/<fnv64(origin)>` (mode 0600), written at most every 2 s
  from `js_tick`, on backgrounding and at exit. Opaque origins (`file:`) are not persisted;
  `LUMEN_NO_PERSIST_STORAGE=1` turns it off. No quota and no cross-tab `storage` events yet.
- PERF (iframe contexts, measured, not fixed): each new frame context costs ~65 ms on this
  loaded VM (100 about:blank iframes, `~/fvtest/iframes100.html`), almost all in running the
  prelude in `js_new_ex`; ~70% of that is V8 GC (scavenges and a mark-compact) triggered by
  `Runtime_DefineClass` allocations. YouTube home spent ~1 s of an 8 s load sample in
  `frame_done`/`frame_create`. Tried and rejected (no gain beyond noise): forcing the WebGL
  constant holders into dictionary mode, `--min-semi-space-size=8/16` (+10-20 MB RSS).
  Also tried: lazily running the WebGL setup on first use (works, WebGL tests pass) gave no
  measurable gain (63-68 ms vs 63-70 ms), so it was not kept. Removing `07r-rtc.js` breaks the
  prelude (08-window.js references its classes), so an earlier 'faster without RTC/WebGL' run
  was invalid. Next candidate: a V8 startup snapshot containing the prelude.
- PRELUDE SNAPSHOT (design, not started): the prelude (`src/js/prelude/*.js`, ~580 KB) is a
  function `(N, global)` run in every page and iframe context (`js_new_ex` in `src/js/js.cc`).
  A V8 startup snapshot would need: (1) all ~364 `REG(...)` native callbacks in an external
  reference table; (2) splitting the prelude into a tab-independent part (class/proto setup,
  snapshotted) and a per-tab part (`N.doc()`, location, storage origin, ~50 top-level `N.*`
  calls) run after `Context::FromSnapshot`; (3) worker isolates (`src/js/worker.cc`) either
  keeping the current path or using a second snapshot. Expect most of the ~65 ms per context.
- BUS (2026-10-15): `BroadcastChannel` (was a no-op stub) and cross-context `storage` events now go
  through an in-process bus (`busPost`/`js_bus_deliver` in `src/js/jsnative.cc`, delivered from
  `tick_one`). Messages are V8-serialized, same-origin only (opaque origins: own context only), kept
  5 s / 256 messages. Receiving contexts also update their `localStorage` copy, so same-origin tabs
  and iframes now see each other's writes live. Test: `~/fvtest/bus/bc.html` over http. Dedicated workers
  join the same bus (`bcw.html`; woken via `js_bus_listen`). Storage quota: 5 Mi UTF-16 code units (keys + values) per area,
  `QuotaExceededError` like Chrome (`~/fvtest/bus/quota.html`).
- IDB (2026-10-15): IndexedDB is now shared by all contexts of an origin and persisted under
  `<profile>/indexeddb/` (flushed with localStorage). Each committed write transaction
  V8-serializes its whole database (`idbPut` in `src/js/jsnative.cc`); other contexts reload it when
  `idbGen` changes, at `open()`/`transaction()`. Last committer wins if two tabs write the same
  database in overlapping transactions. Opaque origins (file://) stay in memory, per context. Also
  fixed: promise-based IDB code (`await` between requests, idb-keyval/Dexie style) hit
  `TransactionInactiveError`; requests now drain microtasks after each success/error event
  (`N.drainMicrotasks`). Test: `~/fvtest/bus/idb.html` (run twice; ids keep counting). Lumen
  instances are separate processes, so two running Lumen apps do not see each other's writes.
  Verified on the iPad Simulator too (force-quit + relaunch keeps data; 0-0.1% CPU idle).
- `navigator.storage.estimate()` now reports real usage (localStorage + IndexedDB bytes for the origin,
  `storageUsage` native); `persisted()` added (always false).

## Security features (in progress, order: SameSite/HSTS -> mixed content -> XFO/frame-ancestors -> iframe sandbox -> Referrer-Policy -> CSP -> CORP/COEP)
- Cookies (src/net/cookies.c): SameSite=None/Lax/Strict; unset = Lax (Chrome default). Requests carry
  `site_for_cookies` (top-level doc URL, `net_request_ctx`); cross-site requests and cross-site frames'
  document.cookie only see/set SameSite=None cookies. SameSite=None needs Secure; `__Secure-`/`__Host-` prefixes
  enforced. Site = approximate eTLD+1 (small two-level suffix table, no full PSL). Top-level navigations are treated as
  same-site (initiator isn't tracked), so Strict cookies are sent on cross-site navigations — known gap.
- HSTS (src/net/hsts.c): Strict-Transport-Security over https is recorded (includeSubDomains, max-age=0 clears),
  persisted to `<pref>/hsts.txt`; http:// URLs to known hosts (incl. redirects) are upgraded before connecting. No preload list.
- Mixed content (`net_secure_upgrade`, hsts.c): http:// subresources, iframes, fetch/XHR, workers and ws:// of an https
  top-level page are upgraded to https/wss (Chrome autoupgrades images/media and blocks the rest; upgrading is the
  secure equivalent). localhost/127.x are exempt. Applied on every redirect hop.
- tests/test_net.c (`make test`) covers these.
- X-Frame-Options / CSP frame-ancestors (src/js/frames.cc frame_embed_blocked): checked against every ancestor
  document; frame-ancestors wins over XFO; blocked frames get an empty about:blank and a stderr line. Test server:
  ~/fvtest/sec/srv.py (port 8765, localhost vs 127.0.0.1 = cross-origin), check with LUMEN_CONSOLE=log.
- Iframe repaint: frame canvases are redrawn in place, so Image.ver (paint.h) is bumped per raster and hashed in
  ditem_hash; before, iframes loaded after the parent's first paint stayed blank ~50% of runs (pre-existing race).
- iframe sandbox (js.cc js_new_ex, JsCtx sb_opaque/sb_noscript, inherited by nested frames): no allow-same-origin ->
  ctx_origin "null", unique V8 security token, no cookies, parent can't reach contentDocument; no allow-scripts ->
  frame scripts and inline on* handlers don't run. Not enforced yet: allow-forms/popups/top-navigation/modals.
  Test page ~/fvtest/sec/sb.html (serve with `python3 -m http.server 8766`).
- Referrer-Policy (src/net/referrer.c net_referrer): default strict-origin-when-cross-origin; Referrer-Policy header
  and <meta name=referrer> set Document.referrer_policy; applied to fetch/XHR, scripts, frames, images, CSS
  (main.c doc_req). Not yet: per-element referrerpolicy attr, fetch init.referrerPolicy, document.referrer ('').
  Test server ~/fvtest/sec/ref.py (port 8767, logs Referer to /tmp/ref-log.txt).
- ChatGPT home: "Where should we begin?" sits on the header row; same in 9666225 (pre-existing, not a regression).
- CSP (src/net/csp.c csp_allows_script; Document.csp = header + <meta http-equiv>, one policy per line; frames get
  their response's CSP, about:/srcdoc frames inherit the parent's): enforced in js_run_script/js_run_module for
  script-src-elem > script-src > default-src: nonces, sha256/384/512 hashes, 'unsafe-inline' (ignored when a nonce/
  hash/'strict-dynamic' is present), 'strict-dynamic' (only non-parser-inserted scripts), host/scheme/'self'/*.
  Not yet: inline on* handlers (script-src-attr), eval ('unsafe-eval'), img/style/connect/font/frame-src,
  report-only, Trusted Types. Test server ~/fvtest/sec/csp.py (port 8768, /c1../c5, log /tmp/csp-log.txt).
- CORP/COEP (net_corp_blocked in src/net/csp.c, checked in net_fetch_sync when NetRequest.no_cors_origin is set):
  no-cors subresources (img, CSS, classic scripts without crossorigin; main.c doc_req, frames.cc, jsnative.cc)
  are blocked for CORP same-origin/same-site mismatches, and with Document.coep_require_corp any cross-origin
  response without CORP is blocked. Not yet: COEP credentialless, nested-frame COEP, CORS-mode exemptions for
  crossorigin images. Test server ~/fvtest/sec/corp.py (port 8769, /page and /coep, log /tmp/corp-log.txt).
- CSP script-src-attr / 'unsafe-hashes' for inline event handlers (N.compileHandler in jsnative.cc, via
  ScriptCompiler::CompileFunction), and 'unsafe-eval' / 'wasm-unsafe-eval' for eval, new Function and WebAssembly
  compile (V8 code-gen callbacks in js.cc; contexts are created with AllowCodeGenerationFromStrings(false)).
  Workers don't track their own CSP yet. Test pages /c6../c9 on ~/fvtest/sec/csp.py (port 8768).
- SIGTERM/SIGINT/SIGHUP now quit promptly while idle (quit_sig_thread in main.c, desktop only).
- CSP fetch directives (csp_allows_url in src/net/csp.c; NetRequest.csp/csp_doc/csp_dir checked on every hop in
  net_fetch_sync): img-src, style-src(-elem) for <link> sheets, connect-src for fetch/XHR/WebSocket (WebSocket throws
  SecurityError), frame-src/child-src for iframes. font-src is checked in sheet_fonts() when
  @font-face is declared (font cache is global, redirects not re-checked). Not yet: media-src, worker-src, inline <style>/style= (style-src 'unsafe-inline'), report-only, Trusted Types. Test pages
  /r1 /r2 on ~/fvtest/sec/csp.py (request log /tmp/csp-req.txt).
- CSP 'strict-dynamic' also allows script-inserted (non-parser) *inline* scripts without nonce, matching Chrome
  (verified with headless Chrome on ~/fvtest/sec/sd.py, port 8771). Bing's includeScript() relies on this.
