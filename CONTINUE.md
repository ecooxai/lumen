# Lumen: handoff notes

## Login identity, frames, raster bands, file chooser, Fullscreen API (2026-10-20/21, v0.3.2-android-preview)

- Sign-in hosts (`login_hosts` in src/net/http.c: accounts.google.com, accounts.youtube.com, the
  Microsoft and Apple login hosts) get a Firefox user agent (desktop one in Desktop site mode):
  Google refused "this browser may not be secure" with Lumen's Chrome identity. ctest
  `auth_user_agent`. The user signed in to Google manually; never script credentials.
- Frames: link/form `target` and `<base target>` navigate named frames (`linkTarget` in
  06b-activate.js, `N.navTarget`, frames.cc `named_ctx`), POSTs into frames; iframe `frameBorder`
  etc. reflect; a frame document without a background is transparent (tests/pages/frame-*.html).
- Google account page: rounded clips for images/overflow, outlines follow border-radius
  (display.c; ctest `round_clip`), WOFF2 fonts (FreeType + brotli; ctest `font_woff2`), SVG
  `<img>` gradients and root presentation attributes (`svg_gradient_img`).
- Multi-core raster: large rasters are cut into horizontal bands on a thread pool
  (raster.c `raster_band`, `band_thread`; small damage stays on the caller; ctest
  `raster_bands`); image decode runs on workers.
- Settings/workspace/bookmark/history panels float without dimming the page.
- File chooser for `<input type=file>` (SharedFiles.java / BrowserMenus.java): floating panel,
  path field defaulting to home, type icons, image/audio/video preview with Choose, second tap
  chooses, system-picker button on the path row. tests/pages/file-input.html logs `FILEIN`.
  Choosing a file through the system picker is still unverified.
- Fullscreen API: `requestFullscreen`/`exitFullscreen`/`fullscreenElement` (+ webkit aliases) in
  03-element.js / 06-document.js (`fsRequest`, `fsExit`), `N.fullscreen` sets `NF_FULLSCREEN`
  (`:fullscreen` in select.c, UA rules in ua.c) and calls `JsHost.fullscreen`; main.c
  `page_fs_set`/`page_fs_exit` hide the browser UI (`bare_set` on Android), Back or Escape exit
  (`js_fullscreen_exit`). `fullscreenchange` waits for the window's resize (`fsAfterResize`, 600 ms
  timer, nudged by `fsResized` from `viewportChanged`, 1 s cap) so pages read the full-screen size
  (YouTube: vh 488 → 540); `screen.width/height` report the window while fullscreen. ctest
  `js_fullscreen`.
- Crash: Android destroys the window on Home possibly mid-frame; `wgpuSurfaceConfigure` on it is a
  fatal wgpu panic. gpu.c `window_alive` (ANativeWindow_getWidth < 0) skips configuring until
  `gpu_resume`; main.c does not fall back to the SDL surface while the GPU waits. Verified twice.
- Back: one hardware Back press was handled twice (SDL's `SDLK_AC_BACK` key event and
  `onBackPressed` → `android_back_take`); the second step backgrounded the app. main.c now
  ignores `SDLK_AC_BACK`.
- IntersectionObserver (08-window.js): full `rootMargin` shorthand (px/%), element `root`,
  thresholds with real ratios and Chrome-like threshold-index change detection, zero-area targets
  intersect, and clipping ancestors cut targets (`N.vclip` → jsglue.c `jsg_vclip`). The old code
  read only the first margin value and needed a non-empty box, so lazy-load sentinels (YouTube
  comments: the watch page's left column ended above the viewport and looked blank) fired late.
  ctest `js_intersection_geometry` (jsrun lays the page out at 1280x800 when `JSRUN_LAYOUT=1`).
  `IntersectionObserverEntry` exists now: YouTube loads intersection-observer.min.js, which
  installs a polyfill (rechecks on every scroll/resize/DOM mutation) unless that class with
  `intersectionRatio` on its prototype is present.
- Definite heights for children are clamped by min/max-height (block.c `layout_box_in`):
  YouTube's theater `#full-bleed-container` (flex row, `height:56.25vw; max-height:calc(100vh -
  169px); min-height:480px`) was 480 tall but its stretched `#player-full-bleed-container` got
  607.5 (56.25vw), so the player ran past the screen and hid its control bar. ctest
  `js_minmax_definite`.
- YouTube tap behaviour is correct as is: the desktop player switches to `ytp-touch-mode`, one tap
  shows the controls, a double tap toggles pause. In fullscreen YouTube hides the masthead with
  `ytd-app[masthead-hidden] #masthead-container:not(:focus-within)` (translateY + delayed opacity
  transition), set once its player sees `fullscreenchange`. The player picks the event name by
  `'on' + name in document`; Lumen had no `onfullscreenchange`, so YouTube never noticed fullscreen
  (masthead stayed, second F did nothing). Element now has on(webkit)fullscreenchange/error and
  Document also onreadystatechange/visibilitychange/pointerlock*/freeze/resume (03-element.js
  `FS_HANDLERS`). Verified on the phone: masthead hidden, player 1080x540, F exits. Probe
  /tmp/lumen-scripts/probeFs.js logs `YTF`.
- Measured (apk-fsh): YouTube load median 165% (max 432), playback 178% median, fullscreen 182%,
  paused last 10 s median 3.5% (max 10.7).
- Hardware video decoding on Android (media.c `open_mediacodec`): H.264 (8-bit), HEVC Main, VP8,
  VP9 profile 0 open FFmpeg's `*_mediacodec` decoder first (NDK AMediaCodec, since no JVM is
  given to FFmpeg), software libavcodec is the fallback; `LUMEN_HWDEC=0` disables it. Frames
  arrive as NV12 and take the existing `yuv_copy` path. `decode_pkt` now retries a packet the
  decoder refused with EAGAIN (full input queue) instead of dropping it. Logs once
  `lumen-media: hardware decoder h264_mediacodec WxH`. YouTube 720p on the MI 8: playback median
  178% → 67%, fullscreen 182% → 43%; picture and colours checked. Qualcomm OMX logs harmless
  "Setting color format failed" / "Cache Clean failed" lines.
- x.com (logged out): load peak ~210%, idle 0%, scroll median 3.5%. Fixed:
  - the empty "Email or username" input was 0 tall (an empty control had no line), caret drawn
    below it: an empty text control is now one line tall with a baseline (inline.c, js_empty_input);
  - "Continue with Google" is x.com's own pill with `transform:scale(1.15)` (origin left) over an
    invisible (opacity 0) GSI iframe; axis-aligned scales scaled boxes/svg but not text, so the
    scaled G covered the label. A uniform scale now scales glyph positions and draws them with
    `font_resized` (same face/variations at the quarter-pixel-rounded scaled size) (test_scale_text);
  - `document.fonts` is a set-like iterable (pages spread it).
  Not a bug as far as known: the "By continuing…" line sits in a `margin:0 -12px` flex child under an
  `overflow-x:hidden` column and is clipped by 12px on each side, which Chrome's geometry would do too.
  Debug: `--es org.lumen.browser.FRAME_PRE_JS <base64>` runs a probe in every iframe document
  (LUMEN_FRAME_PRE). Probes in /tmp/lumen-scripts (probeX*.js, probeGsiF.js).
- `tools/device_tests.py x` (x.com) added to the `sites` group.
- Phone CPU sampler: `kill -0` fails (EPERM) on the MI 8; poll /proc and keep adb's stdin closed:
  `adb -s 2d4f6bc0 shell 'while [ -z "$(pidof org.lumen.browser)" ]; do sleep 0.2; done;
  P=$(pidof org.lumen.browser); while [ -d /proc/$P ]; do echo "$(date +%s) $(top -b -n1 -p $P |
  tail -1)"; sleep 1; done' </dev/null > FILE &`. simpleperf works with `--app org.lumen.browser`.
- Measured (phone): Google load peak ~180%, settled 0-7%; ChatGPT logged out peak ~300%, last 10 s
  ~1%; YouTube cold mean 39%, settled ~4%; playback 100-200% (decode); paused ~4%.
- Open: Shorts double tap and guide drawer; ChatGPT needs the user to sign in again on the phone before its
  full test; x.com test not run yet.

## Site info and Delete data (2026-10-20)

- The address field (BrowserToolbar) is a rounded `pill` holding a site icon and the EditText: a lock
  on https, a red warning on http, an info sign on other pages, a magnifier while editing
  (`siteIcon`). Tapping it opens `BrowserMenus.showSiteInfo`: host, URL, connection, the site's
  cookie count (`nativeSite`, `nativeSiteCookies` in android_ui.c) and Delete data, which after a
  confirmation sends `UI_SITE_CLEAR` (24). `site_clear` (main.c) deletes the cookies of the site and
  its subdomains (`net_site_of`: registrable domain approximated without a suffix list;
  `cookies_site`), reloads the page and clears local/sessionStorage in other open tabs of the site.
  Lumen has no service workers, IndexedDB or Cache Storage, and localStorage lives in memory per
  page; the HTTP cache stores only URL hashes, so it is not cleared per site.
- Tests: ctest `cookie_site`; `tools/device_tests.py sitedata` (tests/pages/site-data.html sets two
  cookies and storage and reports `load cookie=0|1`; no done marker, so it is flagged "no done
  report"). Verified on the phone (APK build/claude-chatgpt-scroll/apk-site/lumen-site-debug.apk):
  icons, popup, dialog, `cookie=0` after Delete; example.com idles ~1%.

## IN PROGRESS: touch indicator, first-scroll restyle, Google, WebGL/WebGPU (2026-10-19)

Same branch, on top of the sections below.
- Touch indicator: entirely on the Java UI thread, separate from the native engine loop, so a
  page busy loading or running script never delays it. `LumenActivity.dispatchTouchEvent` →
  `BrowserToolbar.indicatorTouch` reads every raw MotionEvent that starts outside the toolbar.
  Finger down shows a dark dot for at least `IND_MS` (1 s) and until up; 6 dp of travel
  (`IND_TRAVEL`) sets a blue arrow the way the finger moves (up/down/left/right), held 1 s
  after the last move (`indicatorRefresh` re-posts itself for the expiry). It is drawn inside
  the address field at its right end (`drawIndicator`); the field's right padding is 28 units
  so the URL text stops short of it. Nothing is drawn in the tab strip. (It used to be
  computed in main.c from SDL finger events, which waited behind JS/layout during loads.)
- Tabs on Android have no ×; double tap a tab for its menu (Close tab, CPU limit:
  `android_ui_tab_menu`, `BrowserMenus.showTabMenu`, `TAB_MENU = 23`).
- Keyboard: a page input needs a second tap (a quick double tap counts) to raise the keyboard,
  and JS `focus()` never raises it (`kbd_tap`). Keyboard resizes change only the viewport
  height; `uses_vh` in css.h lets style.c take a `height_only` path, so text is not restyled
  or stretched.
- First scroll on a long ChatGPT chat froze ~1.5 s: ChatGPT toggles `tabindex` (and other
  selector-only attributes) on scroll, which went through `mark_impl(reason=name)`, i.e.
  `selector_changes_unknown` and a whole-document reselect (~1.1 s, rules_iter 2.7M).
  `css_selector_only_attribute` (style.c; weak default in dom.c) now lists tabindex, role,
  title, hidden, disabled, draggable, ... plus data-/aria-, which take the precise
  `attribute_subjects` path. `:disabled`/`:enabled` are indexed as the `disabled` attribute
  (`simple_attribute`), and a fieldset's `disabled` also reselects its subtree. Measured on
  ui80: no `unknown=tabindex` passes, the first-swipe spike is 85% for one second, the load
  settles at 24 s and the last 10 s average 5.8%. Tests: `test_attribute_subject_invalidation`
  (tabindex/hidden/role and a `:disabled` matrix).
- The busy-window trace (LumenLoad) is followed by a `paths blit=.. full=.. why=.. noblit=a/b/c`
  line: render paths per 2 s, and page scrolls not shifted (sub-pixel/overlay/dirty scroller,
  fixed boxes over half the screen, video). On Google (ui82), every scroll frame after load was
  a blit at about 9 ms of render per frame; the full rasters in an earlier run were during load.
- Probes: `/tmp/lumen-scripts/probe_type.py FILE` finds the address field by content-desc
  "Address" through uiautomator and types a javascript: probe there. Page console reaches
  logcat (LumenJS) only when Lumen is started with `--es org.lumen.browser.CONSOLE 0..3`.
- The Android debug build is -O2; the LumenLoad/LumenStyle traces arm for 120 s after a
  navigation and 30 s after a tap.
- Google search-results page: focusing the search field made it vanish. The overlay is a
  fixed `height:100%` box whose first child is an empty focus-sentinel span; its anonymous
  block wrapper shared the parent's style and so took `height:100%`, pushing the field a
  viewport down. `anon_block_style` (box.c) gives anonymous block wrappers auto
  width/height/min, no max and no aspect ratio. Test: `test_anon_block_size`.
- `document.write('<script ...>')` scripts never ran (`parseFrag` marks a fragment's scripts
  started), which broke tdl.require (WebGL Aquarium). With a current script, `parseFrag(...,
  true)` queues the fragment's classic scripts in `JsCtx::written`, and `js_run_script` runs
  them right after the writer, in order (external ones via `net_fetch_sync` with a Referer),
  nested writes included. Fixture: `tests/js/docwrite-scripts.html` (+ `docwrite-ext.js`).
- Background tabs, as in Chrome: no requestAnimationFrame callbacks while `JsCtx::bg`
  (`js_set_background`), `document.hidden`/`visibilityState` follow it and `visibilitychange`
  fires on change; timers wake a hidden tab at most once a second (`timers_throttled`, unless
  it plays media or has used native WebRTC). Hidden WebGL Aquarium tabs had kept the app at
  ~110% CPU on other pages; ui87 idles at 1.1% with them open.
- Google's search-box mic/magnifier sat ~10 CSS px high: an inline-block svg in a 48px
  `display:block` `<button>`. A `<button>` with flow/inline content now centres its content
  vertically when taller (block.c, after the height clamp; the old single-line control centring
  only moved text fragments, never child boxes). Test: `test_button_center`.
- `getComputedStyle` returned "" for keyword properties (flex-direction, align-*, justify-*,
  order, text-align, white-space, float, vertical-align, cursor, user-select, ...);
  `css_get_computed_value` serializes them now. Fixture: `tests/js/computed-keywords.html`.
- Device tests: `tools/device_tests.py` (see AGENTS.md) serves `tests/device` (cam, mic, gfx)
  and `tests/pages` on 127.0.0.1:8930 via adb reverse, takes their `/r?n=TAG` result posts on
  the same port, and launches with the debug-only `TEST_URLS` extra (`LumenActivity.testUrls`,
  one tab per URL, through main's argv). `bm:TEXT` start arguments open the newest bookmark
  whose address or title contains TEXT (`start_url`). mic.html uploads recordings only on ?up=1.
- Settings → Hide tab bar (`hide_tabs`, `g_hide_tabs`): `update_compact` keeps the compact
  one-row layout with any number of tabs; the menu's Workspaces switches tabs. On Android
  `update_compact` now runs before `bar_push` so the toolbar's top follows at once.
- Desktop site is on for every site by default ("*"), seeded once on Android (`desktop_default`
  in settings.txt) so existing installs get it too. Turning one site off under "*" saves a
  `-site` exclusion (desktop_sites.txt) instead of turning "*" off; turning it on lifts it.
  `net_desktop_restore` loads entries verbatim. Test: `test_desktop_sites`. Site testing
  targets the desktop layouts.
- Workspaces persist: `session.txt` (pref dir) holds workspace names/icons and every tab's
  history, address, zoom and title, plus the shown tab and each workspace's last tab.
  `session_service` (main loop) hashes that state every 2 s, and at once on going to the
  background, and rewrites the file via `session.txt.new` + rename when it changed.
  `session_load` at startup restores it with every tab asleep (`Tab::stitle` is its saved
  title until the page loads); only the shown tab loads. A VIEW address opens in a new tab on
  top. TEST_URLS launches set `LUMEN_NO_SESSION`, so test runs neither restore nor save;
  desktop builds restore only when started without arguments.
- Caret blink stops 10 s after the last focus change or input, and the caret then stays
  shown (as GTK does); a focus change resets `caret_t`. ChatGPT's focused composer had kept
  the page re-rendering twice a second.
- Display-list culling (display.c): `box_ink` caches per box the vertical extent of its
  normal-flow paint and `defer_in` (the subtree holds positioned/layered boxes, which must
  still be collected). `culled` skips subtrees whose extent ±64 px misses the viewport or
  clip. The cache (`Box::ink_gen`) is valid while `doc_box_generation() + 1 == ink_gen`;
  layout (block.c) and restyle (style.c) call `doc_boxes_changed`, scrolling does not.
  `dl_cull` is off only in tests. Test: `test_paint_cull` (culled vs full walk at 8 offsets and
  after a move). ui93: ChatGPT long chat idles at 1–2% (was 14%), YouTube 4–5%.
- The LumenLoad `paths` line also counts wakes (`wake caret/anim/vbars/defer`) and why a
  scroller blit failed (`sb_fail nosbox/ui/clip/x/d/area`). A `jsstat` line (debug Android)
  counts per 2 s what woke page JS: ticks, timers by interval (0, ≤16, ≤50, ≤250 ms,
  longer) and their ms, rAF callbacks, media pumps (and how many found media playing), and
  intersection/resize pumps. `jsstat ... phase_ms p0..p8 settle=` splits tick_one's time into
  tasks, modules, pick+timers, viewport, media/rtc/webgpu/videoframe, visibility+rAF, anims,
  resize, intersection and the settle step (code refresh, workers, microtasks).
- `backdrop-filter` is not rendered. Its background is drawn more opaque instead, and fully
  opaque at blur ≥ 16 px with alpha ≥ 128: YouTube's 48 px frosted masthead let the feed's text
  show through sharply.
- `env_hook(ENV_...)` in main.c caches the debug env variables (LUMEN_SHOT, CRASH_TEST,
  TAB_SLEEP(_MS), MEM_STATS, AUTOSCROLL, CHECK_DAMAGE). getenv on every frame was 2.7% of
  YouTube's idle profile.
- Fixed regions (`DisplayList.fixed`) are collected with `paint_fixed_add` (regions.h). It merges
  two boxes only when their bounding box is no larger than both areas together, so a header
  band and a full-height side column stay apart. A page-scroll blit repaints and uploads only
  each region's own columns (`gpu_upload_page_rect_ring`). When the union of the exposed strip
  and the regions (`paint_regions_union_area`, overlaps once) passes 4/5 of the page, a full
  raster is used. YouTube's masthead and guide used to merge into one full-screen region, so
  every scroll frame was rastered in full. Scrolling up, the strip lies inside a header's
  shifted copy; summed separately it had pushed YouTube (112 CSS px header+chips band, steps of
  240 device px) to 84% and a full raster. When full, a new region joins the one it grows least.
  Debug Android logs `noblit fixed d= page= area= sdmg= fixed= unmatched= repaint px: ...` (geometry
  only, during the 120 s trace) for each refused shift.
- Scroll + damage (`bdmg` in the paths line): a frame that both scrolls by whole device pixels
  and has DOM/layout changes (why 1/3/4) also shifts the old pixels. It then rasters, together
  with the fixed regions, the union of items that differ from the last frame. Comparison
  signatures are hashed with the page-scrolling items moved back by the scroll (`dl_sigs`'
  dy; y is hashed in 1/1024 CSS px). Old boxes are moved up by the scroll, because their pixels
  were. `sig_damage` is shared with the no-scroll `dmg` path. Check:
  `LUMEN_CHECK_DAMAGE=1 LUMEN_AUTOSCROLL=600,400,-0.5,2000,250 build/lumen file://$PWD/tests/pages/scroll-damage.html`;
  each `scroll damage` line must show `mismatched px=0`. A fractional wheel delta takes the
  trackpad path (whole CSS px); whole notches scroll smoothly, by fractions of a pixel, and never blit.
  Display-list culling adds/drops items at both ends while scrolling, so `sig_damage` trims the
  common head/tail, then matches in order: each new item takes the first old item with its hash
  after the last match (sorted (hash, index) + binary search); unmatched old and new items are
  damage, kept as separate regions (`paint_fixed_add`), not one union. `dl_sigs`' context hash
  is a stack of the open clips/layers only (a cumulative one made every item after a culled clip
  differ). The paths line shows `bdmg=N (no a/b/c/d)`: not shifted because the shift was
  refused, the damage was too big, video, or not eligible. `LUMEN_CHECK_DAMAGE` also prints
  `scroll sigs d= unmatched old= new= regions= [first unmatched items]`.
- `idle=` in the paths line: frames skipped before the display list is built. Nothing changed
  (why 0, no scroll, no dirty scroller) and the last display list held no media frame
  (`App::last_nv`), e.g. YouTube playing a hidden/off-screen video: it had rebuilt the list at
  ~25 fps (600 ms of render per 2 s, now ~70 ms). A first frame or a size change relayouts, so
  a video that turns visible is still drawn.
- The user's phone has Smooth scrolling off with 30 CSS px steps (settings.txt), so a swipe
  jumps 60-240 device px per frame; test scrolling with that setting, never change it.
- Google home search box (phone, desktop site) painted square with a bump at its left edge, and its
  focused suggestion panel joined it with square top corners: the box (`.RNNXgb`, 26 px radius,
  1 px border, shadow) holds a white inner box with `background: inherit; border-radius: inherit`,
  and `inherit`/`initial`/`unset` of border-radius were ignored, so the white child covered the
  rounded corners. The keyword block in `apply_decl` (style.c) now copies the shorthand and each
  corner (`border_corner`); they mark the style parent-sensitive. Test: `test_radius_inherit`.
  (The host gets a different Google variant without that rule.) Left: with the panel open, a
  small shadow blob shows at the box's two bottom corners where the panel joins (shadow detail).
- Debug Android: `--es org.lumen.browser.PRE_JS BASE64` writes a probe script to the cache dir and
  sets `LUMEN_PRE_FILE`, so every page runs it before its own scripts (with `CONSOLE 2`, its
  console.warn lines reach logcat LumenJS). Use it instead of typing `javascript:` probes when a
  page input has the focus (Google home's autofocused field): `probe_type.py`'s text never reached
  the address field there (not investigated).
- 2026-10-20 (resumed): WebGL Aquarium 10000 fish runs WebGL2 at ~23 fps settled on the phone
  (was ~20.5; target > 10), CPU ~100% of a core because the page is CPU-bound (per-fish JS plus
  ~60k GL calls a frame). simpleperf showed `eglGetCurrentContext` (~13%, Android's EGL wrapper
  takes a lock and TLS) in every binding call through `make_current`; `src/gpu/webgl.c` now keeps
  the thread's current context in `g_current` (only that file changes it). Left in the bindings:
  `view_of`/`view_range` V8 type checks, `jctx`, readback memcpy (~6%).
- Settings (`src/app/newtab.html` `settings()`): the cards sit in four folded groups (Display,
  Scrolling and touch, Performance and memory, Sites and video), each header showing the option
  titles; a tap toggles one, a search opens every group with a match. Open groups live in the
  address (`lumen://newtab?s=settings&open=display,perf`); the pixel-scale / renderer links pass
  `open=` through `lumen://set?`, whose redirect in `navigate_ex` keeps it (letters and commas only).
  adb gotcha: quote URLs containing `&` for the phone's shell too (`adb shell "am start ... 'URL'"`),
  or it cuts the extra at `&`.
- ui104 (uncommitted: webgl.c context cache, settings groups) is installed on the phone; host
  ctest 75/75. Its run for the Aquarium bookmark (long press on Bookmarks) and WebGPU Weather World
  (utils bookmark → apps → Weather World, probe /tmp/lumen-scripts/gpu_pre.b64 via PRE_JS) had
  not reported when the session paused: re-run both. Next after that: ChatGPT long-chat load
  (`python3 -I /tmp/lumen-scripts/chat_load.py LABEL 40 perf jsprof`).
- The "address-bar typing ~67%" number was probe_type.py pasting a 1000-char script at machine
  speed plus running it, not human typing; measure ordinary typing before working on it.
- PAUSED (user, 2026-10-19: finish only the two most important items this session, YouTube scroll
  lag and the Google home input bug). Still open: WebGL Aquarium 10000 fish > 10 fps and bookmark;
  WebGPU via utils bookmark → apps → Weather World; ChatGPT long-chat load < 10 s and its
  sdirty bursts / 370 ms style passes; YouTube idle ~4-5% (its own 50 ms timers, js ~30-80 ms
  per 2 s); address-bar typing CPU (~67%); scroll+DOM frames on YouTube whose damage covers
  most of the page (see the `noblit fixed` lines with sdmg=1).

## IN PROGRESS: screen recording, Downloads viewer, Home/Settings UI (2026-10-19)

Same branch, on top of the section below.
- Screen share crash (entire screen): fixed; JNI entry points of `src/media/screen.c` (and
  `src/text/android_font_raster.c`) must be compiled straight into libmain, because
  `-Wl,--exclude-libs,ALL` hides every symbol of the static archives (CMakeLists
  `android_jni_bridges`). A missing JNI symbol is an UnsatisfiedLinkError crash, not an error.
- System audio: `AudioPlaybackCaptureConfiguration` (needs RECORD_AUDIO, asked before the
  consent dialog) streams mono float 48 kHz into a ring in screen.c; JS drains it every 40 ms
  (`N.screenAudio`). MediaRecorder types follow Chrome (webm vp8/vp9/av1/h264 + opus/pcm,
  x-matroska, mp4 avc1/hvc1/vp9/av01 + mp4a/opus, audio/webm, audio/mp4, audio/ogg);
  MediaCodec encoders are probed once. Demo + self-test: `tests/pages/screen-recorder.html`
  (`?auto=1&report=1&tag=X`, posts PASS/FAIL lines to 127.0.0.1:8932).
- `<video src=URL.createObjectURL(blob)>` never loaded (native media fetched the blob: URL);
  now `N.mediaOpenBytes` hands the Blob's bytes to `mp_open_bytes`. This was the "no picture"
  failure of every video format in the ui72 self-test.
- Downloads viewer (`newtab.html`, "Downloads viewer" block): images, video, audio, PDF and
  STL/OBJ/glTF/GLB in a float; zoom (buttons, wheel, pinch, double tap), pan, full screen
  (`set?fullscreen=0/1` → `android_ui_fullscreen`), open with an app, bottom facts overlay
  (tap toggles). Facts come from `lumenInternal('view?path=P[&page=N&w=PX]')` (`files_view` in
  main.c: size, `media_probe_file` FFmpeg probe, PDF pages via `LumenActivity.pdfRender` →
  PdfRenderer JPEG in the cache dir, URL with a `?v=` serial because images cache by URL).
  file: media now streams from the file (`Stream.fd`, pread in `read_cb`) instead of being read
  into memory, and only `lumen:`/`file:` pages may play file: media; `corsOk` lets `lumen:`
  pages read file: URLs (the 3D viewer fetches the model). Fixtures:
  `build/claude-chatgpt-scroll/fixtures-rtc/viewer.html` (+ `viewer/`), `scroll.html`.
- Settings: Smooth scrolling is on by default (saved as `smooth_scrolling`; the old
  `smooth_scroll` key is ignored). Off, a touch drag moves in steps (`android_step_scroll`): the
  drag slop drops to 5 px (`AndroidTouch.slop`); the page scrolls `scroll_px` (multiples of 10,
  default 20, at most one screen, `scroll_px_max`) after 5 CSS px of travel (`STEP_START`), then
  once per further 10 px (`STEP_TRAVEL`); no fling. The slider (`.rng`, `spMove`) is a plain track and thumb on a
  squared scale. Sleep background ChatGPT tabs is off by default and saved as `chatgpt_sleep`,
  so an older `tab_sleep=1` no longer turns it on.
- Viewer full screen is `g_bare` (`bare_set`): TABH/TB become 0, `build_chrome` draws nothing,
  the panel takes the full width, BrowserToolbar goes GONE on a zero-height state, and the
  system bars hide; it resets when the panel goes away.
- Open with / Share from other apps (`SharedFiles.java`, manifest VIEW content:/file: and
  SEND/SEND_MULTIPLE filters): a file readable in place (file:, or a content: URI whose path
  holds `/storage/…` with the same name and size) opens there, so a page's relative files load;
  otherwise it is copied to `files/opened/<time>-<n>/` (cleaned after a day). Pages (html, txt,
  svg, …) open as file: tabs with JS; pictures, video, audio, PDF and 3D models open
  `lumen://newtab?s=downloads&view=PATH` (newtab opens the viewer; `files_resolve` accepts
  `opened_dir()`). JNI `nativeOpenFile` → `android_launch_submit_file`, which builds and
  percent-encodes the URL; `android_launch_submit` still takes http(s) only. Shared text opens
  its first http(s) link.
- file: hardening: `net_file_protect` makes file: reads inside the app's data dir fail (except
  `files/opened` and `cache/viewer-pdf.jpg`); each file: document is its own origin for frames
  and V8 security tokens (`ctx_origin_key`); `corsOk` lets a file: page fetch only files in its
  own folder or below. Still open: http(s) pages can load file: subresources (img/script)
  natively, because NetRequest has no initiator.
- Home: compact rows, text-only workspace label, `+` right of the title, long press (DOM
  `contextmenu`; the TAP that ends the press is swallowed) opens Rename / Duplicate
  (`lumen://workspace?dup=I`, tabs come asleep) / Close workspace; the current workspace is
  scrolled to the middle. Toolbar CPU meter is a quarter smaller.

## IN PROGRESS: camera, screen capture, WebGL 2, WebGPU, bookmark UI (2026-10-19)

Same branch. Page-facing graphics and capture, measured on the phone (Adreno, Vulkan):
- WebGL 1/2: `src/gpu/webgl.c` (one offscreen GLES 3 context per canvas, FBO as the default
  framebuffer, async readback through two PBOs + fences), natives `src/js/webgl.cc`, prelude
  `07g-webgl.js`. Frames go to the canvas's media player and are composited like video frames.
- WebGPU: `src/js/webgpu.cc` on a wgpu-native instance of its own (not the compositor's);
  prelude `07h-webgpu.js`; enum table `src/js/webgpu_enums.inc` from `tools/gen_webgpu_enums.py`.
  Everything wgpu-native would panic on is validated first; errors only go to the page.
  Canvas present copies the texture into one of 3 MapRead buffers; `canvas_finish` hands the
  newest to `wgf_push` (`src/gpu/webgpu_frame.c`, pooled Images).
- Camera: `src/media/camera.c` (SDL3 camera, NV12 kept for GPU conversion). Screen:
  `src/media/screen.c` + `ScreenCapture.java` (MediaProjection). MediaRecorder and
  canvas.captureStream work on both.
- **gpu-allocator patch (not in the repo tree, apply by hand):** `tools/patches/gpu-allocator-0.28.0-cached-readback.patch`
  against `build/codex-android-wgpu/cargo-home/registry/src/index.crates.io-1949cf8c6b5b557f/gpu-allocator-0.28.0`,
  then rebuild wgpu-native (`python3 tools/codex_compile.py python3 /tmp/lumen-launch/wgpu_rebuild.py`
  does `cargo clean -p gpu-allocator`, the release build and the install into `prefix/lib`).
  Why: Adreno memory types are 0:0x1 1:0xb 2:0xf 3:0x1 4:0x7 5:0x21; readback buffers allow
  0x7 and stock gpu-allocator picks type 2 (coherent+cached), which reads like uncached memory
  (256 KiB memcpy 2.2-4.8 ms). The patch picks type 1 (cached, not coherent; wgpu invalidates
  before reads) and gives such allocations a block of their own, because kgsl's cache
  maintenance on invalidate covers the whole 64 MiB shared block (that cost ~62% of a core).
- Results (ui69): WebGPU 256x256 animation 57.4 fps at ~40% CPU (was ~50% stock, 65-70% with
  type 1 in a shared block), readback ~0.2 ms/frame, compute bad=0. WebGL 2 animation ~57 fps
  at ~36-46%. Camera 640x480/30, vp8+opus recording, CPU mean ~22%. Idle 0-3%.
- Remaining WebGPU cost (simpleperf): compositor present ~27% of samples, page submit/present
  ~15%, `vkCreateFramebuffer` ~10% (wgpu-hal 29 creates a framebuffer per render pass and
  drops it at encoder reset; it no longer supports imageless framebuffers; a device-level
  imageless cache in wgpu-hal would remove it), kgsl alloc/free ioctls ~9%.
- Bookmark UI (`src/app/newtab.html`, `bm_add`/`bm_store` in main.c, `BrowserMenus.notice`):
  long press on Bookmarks (menu row or toolbar slot) adds the current page with a notice below
  it; `+` right of the Bookmarks title adds it (`set?bm_add=1`, one-shot `msg` toast); each row
  has a ⋮ menu: edit (inline, `bm_save=I&bm_t&bm_u`), remove, open in new tab / new workspace
  (`lumen://workspace?bmtab=I` / `bmws=I`). The row with the open menu gets `z-index` because
  the engine treats a `z-index:auto` positioned box as its own layer for paint and hit testing
  (CSS says its z-indexed descendants join the parent context); fix that in
  `src/layout/block.c` `hit_layers` and `src/paint/display.c` together.
- Next: screen-capture run (`gfx_phone.py scr1 cam.html 'auto=screen' 60`), camera re-run,
  ChatGPT #settings shows only a Language card on mobile, WebM duration=N/A, the spurious
  unhandled-rejection warning (js.cc:683).

## IN PROGRESS: native WebRTC for ChatGPT live voice (2026-10-18)

Branch `claude-opus-5-5/android-chatgpt-vulkan-scroll`. `07r-rtc.js` still owns the
spec state machine, SDP text and the in-page simulation (two peers of one page
link directly, `tests/js/webrtc.html`). When the remote description comes from
outside the page (or `{lumenNative: true}` in the config, tests only),
`07s-rtc-native.js` starts a libdatachannel connection with
the same ICE ufrag/pwd, the same certificate (`N.rtcCert`, real ECDSA P-256, so
the SDP fingerprint matches) and the same mids:
- offerer: at setRemoteDescription(answer) the native side gets one track per
  local m-line (`rtcAddTrack`, our sender SSRC), the data channels, its own
  offer (`rtcLocal`), then the answer with `m=application` moved last
  (`sdp_reorder`, the native offer lists tracks first).
- answerer: the offer goes native as is; `track` events bind transceivers by
  mid; `rtcLocal('answer')` at setLocalDescription(answer).
- audio: sender tracks with a PCM hub (mic `MicHub`, `MediaStreamAudioDestinationNode`)
  feed `rtcSendPCM` (Opus 32 kbps VOIP, FEC, 20 ms frames). Received Opus is
  decoded on the network thread into an SDL playback stream (only while an
  `<audio>`/`<video>` plays the MediaStream: `mediaStreamPlay` in 05-ce.js) and
  a tap ring that `RtcHub` (40 ms) fans out to Web Audio.
- events reach JS through a queue + `net_wakeup`, drained by `rtc_pump` in `tick_one`.
- split: `src/rtc/rtc_core.cc` (target `lumen_rtc`, `-fexceptions`) owns all
  libdatachannel/Opus/SDL-audio code behind the plain-C `rtc_core.h`;
  `src/js/rtc.cc` is the V8 glue plus `N.rtcCert` (OpenSSL). On Android
  libdatachannel is built against NDK libc++ (`std::__ndk1`, exceptions) while the
  engine uses the V8 prebuilt's libc++ (`std::__Cr`, `-fno-exceptions`), so no C++
  type may cross that header; `lumen_rtc` compiles with the NDK libc++ headers and
  links `libc++_shared.so` (already packaged for SDL's HID code).
- libdatachannel 0.24.6 drops RTP from SSRCs the remote SDP does not declare;
  the glue warns "a remote audio section declares no SSRC" (structure only).
- Android microphone: `capture.c` records with AAudio itself (not SDL).
  echoCancellation (default on) uses the VOICE_COMMUNICATION input preset
  (platform echo canceller, noise suppressor, gain control), else CAMCORDER.
  Entering communication mode reroutes the input and disconnects a stream
  opened just before; SDL kept such default recordings on the dead device for
  good (silent mic: ChatGPT dictation spun, live voice heard nothing), so a lost
  stream is reopened from the JS poll (`mic_heal`, logs "microphone reopened").

- Android call audio (Chrome-like): the first voice mic (`capture.c` refcount)
  calls `LumenActivity.setVoiceCall(true)`: MODE_IN_COMMUNICATION, communication
  device = headset if any else built-in speaker (API 31+; older: speakerphone),
  volume keys on STREAM_VOICE_CALL; restored when the last voice mic closes. Needs
  MODIFY_AUDIO_SETTINGS. Received call audio plays on its own AAudio stream with
  USAGE_VOICE_COMMUNICATION (`VoiceOut` in rtc_core.cc, ring + 60 ms prebuffer,
  >250 ms behind drops, reopens after a disconnect); desktop keeps SDL. Mic streams
  close on a detached thread (`mic_release`): AAudio VOICE_COMMUNICATION input took
  seconds to stop on the MI 8 and blocked the JS thread inside track.stop().
- ChatGPT voice (phone, ui36-39): connects end to end. The server rejected offers
  with the data m-line first (`invalid_offer`); offers now list new media in
  creation order and a new data section last, like Chrome (`_assocSections`).
  dumpsys during a call: mode IN_COMMUNICATION, route speaker, capture source
  VOICE_COMMUNICATION, playback VOICE_COMMUNICATION; MODE_NORMAL ~1 s after end.
- Voice CPU (MI 8, profiler off): idle page 1-2%, call ~86-96% (startup peak ~340%).
  Remaining cost is ChatGPT's own JS (16 ms visualizer intervals, React updates,
  getComputedStyle/getBoundingClientRect) and V8 GC. Done: Web Audio nodes reuse
  ping-pong quantum buffers (`_buf`), `PcmQueue` is a ring, worklet buffers reused,
  shared Blackman windows, AudioParam inserts in order, timers indexed by due time
  (`timer_due`). Note: the debug jsprof (`files/jsprof` marker, 500 us sampling)
  itself costs ~45% CPU; remove the marker before measuring.

Host: ctest 69/69; `SDL_AUDIO_DRIVER=dummy LUMEN_CONSOLE=log build/codex-native/jsrun
tests/js/webrtc-native.html 9000` passes 9/9 (data channel text/binary, 440/660 Hz
tone both ways over SRTP, element playback, stats, close); `tests/js/webrtc.html` 4/4;
`tests/js/webaudio.html` 23/23.
Phone fixtures: `build/claude-chatgpt-scroll/fixtures-rtc/{rtc,mic}.html`.
Next: real echo check with a spoken reply, voice CPU (page JS), long-chat load CPU.

## IN PROGRESS: Android long ChatGPT sessions (2026-10-15)

Priority: CPU below 50% during typing/streaming in long sessions, Vulkan first.
Target remains UNMET. Twenty completed long replies persisted in the same conversation,
Google sign-in/session persistence confirmed. Do not claim full ChatGPT,
Chrome or Wasm conformance. Preserve all prior uncommitted work.

Last usable baseline: `build/codex-auth-20261015/lumen-empty-candidates-debug.apk`
(`empty-candidates-symbols/libmain.so`). Later `lumen-empty-host-debug.apk` has
its verified :host boundary correction and is a fallback artifact. Baseline
reply 9 diagnostic stream: 104.94% mean /184.85% peak, with logging; completed.
A later idle window was 4.47% mean /6.06% peak, RSS 618.4 MiB. Prior settled
long-history typing: 40.00% mean /102.91% peak; punctuation visibly fixed.

Child-list style reuse remains under validation. Reject these performance
candidates: tree-dependencies (97.46% queued typing), tree-anchored (4,307
matched elements/~1.1 seconds per cascade), tree-keyed (loading trace still
widens an unkeyed nested menu :where dependency to all elements).

Current installed candidate: `lumen-subject-index-debug.apk`, matching symbols
`subject-index-symbols`. Subject-key indexed structural candidate scan, native0.21s/sanitizer1.61s/Android18.84s/verified install-r. Prior hover-keys: Adds token-key attribute dependency filtering and safe
hover/active witness filtering (native0.19s,sanitizer2.68s,Android24.95s,APK verified).
Previous unchanged-keys candidate: Adds versioned caching during invalidation and retains
unchanged positive class/ID constraints in coalesced invalidation. Native style
PASS0.18s, sanitizer PASS1.80s, Android PASS19.06s, APK closure/signature PASS.
Previous child-layout candidate contains coalesced class/ID filtering plus avoiding
parent computed-style dirtiness for ordinary child-list edits. Layout/descendant
flags still propagate; structural selector subjects are indexed; parent's own
:empty transitions remain explicit. DOM-only fallback remains conservative.
Native style PASS0.20s; sanitizer PASS1.34s; Android PASS16.67s; native23/23
PASS2.08s; scroll parent/direct/nested PASS; APK closure/signature and install-r PASS.

Previous coalesced-keys candidate: draft17 mean62.862374%/peak138.834951%
(17samples), full correct. Reply17 completed LIVE with conclusion/controls:
mean105.979029%/peak176.237624% (36samples). SSE200,28batched callbacks,
620268bytes/33.777seconds,maxqueue5.581seconds,no error/cancellation.
Profile draft17:55.75%style_recalc,39.85%recalc,22.63%collect,14.94%invalidation.
Seventeen completed replies in same conversation. No material CPU improvement
established. Under50% target remains UNMET. A short quiet15sample window before
this test averaged0.333400%CPU; does not prove interaction performance.

Phone monitor/server remain active; current installed hover-keys, rehydrating19long
replies completed LIVE. Same18reply-history typing pair59.661807 ->42.694148%
mean, peaks101.960784/102.061856%; candidate full draft correct. Reply19
106.384030%mean/179.591837%peak,SSE200,noerror,complete conclusion/controls.
New token-key dependency index fixes another native1000history cost failure;
native/sanitizer/Android passed, token-keys artifact retained, not installed.
Hover-keys native/sanitizer/Android pass and installed. Baseline history19-a typing64.933533%mean/103.030303%peak; full draft correct then cleared. Candidate identical typing mean39.629295%,peak100.990099%,18samples,fullcorrect. Reply20 LIVE complete,mean108.031710%,peak183.505155%. Subject-index installed, rehydrating20history before matching draft test. Native regression/scroll fixture passed. All details in performance doc. No commits.

Android debug F7(137) arms120second numeric-only transport diagnostics; F9(139)
disarms. Never log request URLs, headers, payloads or credentials. Transport
trace retained in stream-trace-reply13-transport.log. F8(138) bounded style
trace; F9 disarms both. Current new process debug flags initially OFF.

Phone one-second app/all-thread monitor remains active, holding both ownership
locks. Fixture server remains active. All other completed build/profile sessions
were joined; check any pending commands before cleanup. See
`docs/android-chatgpt-performance-20261015.md` for artifacts and evidence.

## PROGRESS: Android Google / ChatGPT authentication (2026-10-15)

Latest priority is Google login and ChatGPT via Google. Clean candidate installed:
`build/codex-auth-20261015/lumen-auth-debug.apk`, SHA256
`c75e6c01ea642bdbd47f730f7a856d0964a7c413cf7b1ed1373115f59d8e8810`.
See `docs/android-auth-20261015.md` for changes, validation and limitations.
17 native tests + 8 adopted-sheet rendering checks + preload rendering check pass.
ChatGPT login dialog and Google OAuth navigation work. Google initially rejected
the Chrome identity after email; identifying Lumen on accounts.google.com now
advances through email/password to account verification on the user's iPad.
Owner verification completed. Signed-in ChatGPT replies and session persistence
across replacement APKs work. Current priority is CPU below 50% during streaming
and long sessions; this target is NOT met yet. See
`docs/android-chatgpt-performance-20261015.md`. Credentials were used only for the
authorized Google sign-in flow. Never persist credentials or complete OAuth URLs.

Phone monitor `build/codex-vulkan-pacing-20261015/monitor.py` is still active;
its monitor.ready identifies the owning PID. It holds both phone ownership locks.
Fixture server from that directory is also active on 18765/18766. Check exact
cmdlines before cleanup; another checkout has an unrelated monitor. Temporary
auth-debug startup hooks, device marker/script/log were removed. Preserve all
prior uncommitted work. Goal remains active; full browser parity is not achieved.

Earlier unrecorded work: YouTube controls now remain visible for eight seconds
after a player tap (excluding ads); controls fixture and Android touch tests pass.
Ad Skip reproduction deferred at user's request. Vulkan presentation-latency=1
experiment had no material benefit and was reverted; see
`docs/android-vulkan-presentation-20261015.md`. GL/CPU optimization still deferred.

## PROGRESS: Vulkan ARM64 image blending (2026-10-15)

Previous turn PROGRESS: circular CPU page cache. This turn PROGRESS: NEON
deinterleaved eight-pixel image blend preserves existing interpolation/alpha
results with less packed-channel arithmetic. No extra pixel allocation.
See docs/android-vulkan-image-blend-20261015.md. All prior pending work preserved.

Two alternating MI8 Vulkan comparisons against28a773cb...: scroll CPU47.20 ->
44.35% and47.03 ->44.95% (4–6% less), raster threadCPU3.782 ->3.160 and3.844 ->
3.169ms/frame (16–18% less). Presenter timings increased; RAM unchanged~254MiB.
Idle0.266–0.459% candidate; another67samples0.074%,peak2%. No generalwebsite/FPS
claim. Both paired1,690,200pixel content comparisons exact. Midpage wrap good.
Host rasterASan/UBSan and realphone ARM64 rasterUBSan PASS: allweights/alphas,
randompremul, tails, fractionalimages, layers, damage and circularrows. Native/
Android builds, API29/arm64/dependencies/v2signature PASS. Compilation capped.

Candidate installed and hash verified, Vulkan active:
build/codex-vulkan-neon-20261015/lumen-vulkan-neon-debug.apk
SHA2564af25ce3e5242b8ea9aab899d09c96ba29b5afd06a360e069ba7d2a4b7a0226e.
Artifacts build/codex-vulkan-neon-20261015. Current libmain symbols matchcandidate.
Sampled hotspots raster_rect30.86%,fill12.82%,memmove12.53%. SurfaceFlinger BLAST
latency works:127frame candidate tail,median16.707ms,6/126intervals>25ms,max33.422.
No baseline; underprofiler; not droppedframe/smoothness proof. NEXT: matched
presentation timing traces and remaining Vulkan raster/uploadcost, then real
YouTube/ChatGPT/Gemini/login, pinch/sessionrestoration/fullWasm. GL/CPU deferred.
Goal ACTIVE, worktree uncommitted, no permission blocker. Credentials unused.
Cleanup complete: all owned build/test/profile/comparison/monitor/server sessions
joined; reverse18765, standalone test executable and app profile marker/data
removed. Google URL+three blanktabs recreated, Google selected; cookies/settings
not cleared. FinalGoogle30samples CPUmean2.331%,peak5.051%,RSS388.93MiB after
heavy page loading. 506 total samples, zero parse/cadence errors. Google loadcost
remains open; exact tab scroll/history cannot be restored without persistence.

## PROGRESS: Vulkan circular CPU page cache (2026-10-15)

Previous turn PROGRESS: integrated phone validation. This turn PROGRESS:
profiled the Vulkan scroll path, removed its whole-page CPU memmove using a
circular row offset in the existing allocation, and adapted raster/layers,
GPU uploads, damage comparisons and fallback composition. No extra bitmap.
See docs/android-vulkan-cpu-ring-20261015.md. Worktree remains uncommitted.

Controlled MI8 comparison: process scroll CPU52.97% ->49.42% (~6.7% lower),
raster threadCPU4.737 ->3.688ms/frame (~22% lower). Presenter threadCPU increased
2.177 ->2.379ms/frame; wall3.120 ->2.852ms. RSS~255MiB scrolling/~251MiB idle,
essentially unchanged. Paired30sample idle0.334% ->0.301%. One short fixture
comparison; no FPS/general website/RAM improvement claim. Separate simpleperf:
memmove sample share26.29% ->10.17%; scaled-image raster remains largest hotspot.
Use controlled-idle.json, not aggregate ring-after-idle (includes later reload).

GPU upload15/15, page transfer20/20 UBSan and raster ASan/UBSan PASS. Native and
Android builds, APK dependencies/API29/arm64/v2 signature PASS. 1,690,200 content
pixels match baseline exactly after20swipes. Phone wrapped scroll, zoom/reset,
background/resume visually PASS; post-check idle40samples mean0.150%,peak1.980%.
Full runtime process/all-thread1Hz monitoring: no parse/cadence errors.

Installed and hash-verified candidate:
build/codex-vulkan-raster-20261015/lumen-vulkan-cpu-ring-debug.apk
SHA25628a773cbb46b0212e8837e75d2404c3eaba9219dfff79156b7045d136b38071b.
Vulkan active. Google URL+three blanks recreated, Google selected; exact tab
scroll/history not restorable. Owned sessions stopped/joined, reverse18765 and
app perf marker/data removed. Credentials unused. No permission blocker.
Follow-up Google settling check: initial28s load averaged67.3% CPU, final30s
2.200%,peak3.030%,RSS367.70MiB. Sampled settled userCPU350.7ms/15s; js_tick largest
named symbol. One transient56% V8-worker interval accompanied~13MiB RSS drop,
consistent with reclamation (not conclusively attributed). Follow-up monitor
and profile joined, app perfdata removed. Real-page load cost remains open.
NEXT: scaled-image raster optimization for Vulkan, then real YouTube/ChatGPT/
Gemini/login, pinch/session persistence and shared-memory isolation/full Wasm.
OpenGL/CPU optimization still deferred. Goal ACTIVE; not daily-browser complete.

## PROGRESS: Android integrated validation (2026-10-15)

User confirmed phone availability; fresh preflight found Lumen foreground and
no user input for >1 hour. The older pending-phone-window restriction is
superseded. See docs/android-device-integrated-20261015.md for full measurements.
Vulkan first; OpenGL/CPU optimization remains deferred. Worktree uncommitted.

Installed and verified latest combined APK0e5dac825a86ac3f741c5a4f0ae57502fcc23057f981b4b3ce21b4c1bae33a33
(build/codex-scroll-events-20261015/lumen-vulkan-scroll-events-debug.apk).
Standalone page-wakeup comparison: cold compile1003–1008ms →1.1–4.1ms;
800ms streams1815–1827ms →808–826ms. Candidate6/6. IdleCPU0.325%, RSS246.60MiB;
brief6% peak from Android housekeeping. Baseline0.264%, RSS244.59MiB.
Combined phone: wakeup6/6, streaming27/27, workers8/8 (including idle GC),
module clone15/15, broad Wasm12/13; shared Wasm memory to worker fails because
SharedArrayBuffer cannot be cloned. Do not claim full Wasm support.

Isolated Vulkan GPU-video-upload comparison ebc5...→c1bf...: identical20swipes
on paused-video thumbnail fixture. Presenter threadCPU2.336→2.178ms/frame,
wall3.622→2.641ms/frame, but totalCPU52.94→53.47% (no overall reduction),
raster4.707→4.720ms/frame. Idle~0.36%, RSS~251MiB. One short comparison;
no FPS/drop-frame or RAM improvement claim. Screenshots aligned. Marker removed.

Phone200% page zoom validates page/frame touch targets, DPR, nested scrolling,
and reset. Root trusted bubbling document/window events, handler follow-up,
boundary silence, bottom clamp, fixed click, frame75px isolation PASS. Final
root idle59samples mean0.102%, peak1.010%, RSS233.26MiB. Fixture server now serves
zoom/root/frame routes; fixtures additionally POST results (console absent from
logcat). HTTP reports can wake loop; not independent16ms deadline evidence.

All owned automation/server/monitor sessions exited; test reverses removed.
Google URL and three blank tabs recreated; cookies/settings/history untouched.
Exact tab scroll/history state cannot be restored: session persistence remains
missing. Latest APK left running, Vulkan active. Credentials unused.
NEXT: profile/reduce raster work during Vulkan scrolling (current largest cost),
then YouTube controls/playback and real ChatGPT/Gemini/login; finish pinch and
session restoration, shared-memory isolation/full Wasm. Keep all prior pending
source changes. No new build needed this turn: only fixtures/docs changed.
Goal ACTIVE and making progress; no blocker/permission question pending.

## IN-PROGRESS root scroll notifications (2026-10-15)

Previous goal turn PROGRESS: page zoom built/host-tested. This turn PROGRESS:
root viewport scroll now dispatches trusted bubbling document events to its
window; scroll-only changes avoid layout/media scans. Non-consuming viewport
checks schedule callback-induced changes at16ms instead of1000ms fallback, with
no idle timer. Root API positions clamp after layout; viewport reporting clamps
transient wheel/key boundary overshoot. Worktree remains uncommitted.

Node viewport8/8, native V8 runtime assertions, viewport state, fixed bounds11/11
and hit tests9/9 PASS. Real isolated Vulkan GUI at150%: trusted document/window
notifications, boundary silence, bottom clamp, handler120→240 follow-up16.522ms,
iframe75px scroll isolation, fixed visual/click position all PASS. Final GUI
CPUmean0.222%,peak1.000%,RSS176.32MiB over9 settled samples;23 whole-process/
all-thread1Hz samples total. Host-only evidence, not Android performance proof.
See docs/android-scroll-events-20261015.md. Native+Android build, APK dependencies
and v2 signature PASS. Uninstalled combined candidate:
build/codex-scroll-events-20261015/lumen-vulkan-scroll-events-debug.apk
SHA2560e5dac825a86ac3f741c5a4f0ae57502fcc23057f981b4b3ce21b4c1bae33a33.
Includes prior V8 wakeup/module clone/GPU video reuse/page zoom changes.

CORRECTION: fixed-button disappearance was overlapping iframe stacking in the
page-zoom fixture. Isolated old-binary100%/150% tests pass; old-binary original
fixture with only z-index:1 also passes screenshot/click after scroll. No fixed
renderer change needed; do not keep this as an open rendering defect.

All owned runtime/server/sampler/build/verification sessions finished and joined.
No phone interaction or new adb reverses. Separate uncontested-phone-window
question still pending; phone installed42af2d... streaming. Goal ACTIVE; meaningful
host progress continues, not blocked. Credentials unused. Full browser/Wasm not
complete. NEXT: broaden scroll queue conformance (net-zero changes between
snapshots, element ordering, scrollend), then Vulkan phone comparison once the
pending test window is resolved. Vulkan smoothness/RAM, YouTube, ChatGPT/Gemini/
Google login, pinch and Wasm shared-memory isolation remain outstanding. OpenGL
ES/CPU optimization deferred. Do not claim this checkpoint is full CSSOM support.

## IN-PROGRESS page zoom candidate (2026-10-15)

Previous turn PROGRESS: Vulkan redundant video uploads removed; uninstalled
candidate built. This turn PROGRESS: implemented 50–300% persistent default page
zoom, tab keyboard/Ctrl-wheel overrides, scaled layout/DPR/iframe paint and input,
Android finger-distance preservation and cache/gesture invalidation. Pinch and
per-site persistence remain unfinished. Worktree intentionally remains uncommitted.

Host full browser and Android build/native dependency/v2 signature PASS.
Scroll8/8, touch24/24, density15/15, viewport13/13 PASS (60 total). Real isolated
host Vulkan GUI checks confirm200% page/frame clicks and nested scrolling,
Settings150% persistence across restart, override/reset, and root scrolling80CSS
with correct iframe clicks afterward. Host settled CPUmean0.111–0.333%, peak~1%;
whole-process/all-thread1Hz samples retained. NOT phone performance evidence.
See docs/android-page-zoom-20261015.md for limitations and artifacts.

Candidate NOT installed:
build/codex-page-zoom-20261015/lumen-vulkan-page-zoom-debug.apk
SHA2563b326f001c8773dddc1edfa2ee6fdd54ac1e60450a5dca80a0d7305a32eed6be.
Includes pending page V8 wakeup, Wasm module clone and GPU video upload reuse.
Phone untouched, still42af2d... streaming; separate uncontested-window answer
pending. All owned build/test/server/sampler sessions finished and joined; no
new adb reverses. Goal ACTIVE; meaningful host progress, not blocked.

NEW FINDINGS: root scroll changes position but does not dispatch window scroll
notifications. src/js/js.cc viewport_update gates out SCROLL-only changes; fix
trusted document/window scroll event delivery with ordering/reentrancy coverage
and no idle polling. Fixed-position fixture button also disappears after root
scrolling; compare baseline and audit paint/layout/hit coordinates. Fixture
initial plain-path invocation loaded an HTTPS error page, not valid evidence;
all accepted tests use owned local HTTP servers. Native tests monitored at1Hz.
NEXT: these scroll compatibility gaps, then uncontested device baseline/candidate
comparison once pending phone window resolved. Full Wasm shared-memory isolation,
real apps, Vulkan smoothness/RAM, YouTube controls, ChatGPT/Gemini/login remain open.
OpenGL ES/CPU optimization stays deferred.

## IN-PROGRESS Vulkan video upload reuse (2026-10-15)

Previous goal turn PROGRESS: module-clone host checks/build completed. This turn
PROGRESS: removed redundant video pixel uploads during repaint/scroll using the
media pump's globally unique visible-frame serial. Both main render paths pass
identity; GPU retains separate RGBA/NV12 upload serials, resets on texture resize,
and keeps geometry/color uniforms live. Zero identity uploads conservatively.
No extra bitmap/texture allocation; two uint64_t cache values in Gpu.

GPU suite14/14 UBSan PASS; media presentation ASan/UBSan PASS; page transfer19/19
UBSan PASS. Fixed stale pre-video-scroll fallback expectation in page transfer
test after verifying its extracted production block matches HEAD exactly.
Android build/native dependency/v2 signature PASS. NO device performance claim.
See docs/android-video-upload-reuse-20261015.md. Candidate NOT installed:
build/codex-video-upload-reuse-20261015/lumen-vulkan-video-reuse-debug.apk
SHA256 c1bf28eb2f039c1f0fb5a0d5eae938148e42bb5cbd665ba687ff82ee537bdea9.
Includes pending page-wakeup + module-clone changes; all remain uncommitted.

Phone untouched this turn; separate uncontested-testing-window answer still
pending. Installed build remains42af2d... streaming. No owned build/test/server
session remains; no adb reverses created. Goal ACTIVE; meaningful host progress
continues, not blocked. For device comparison use module-clone candidate
ebc5d77a... as GPU baseline after completing the earlier wakeup/clone tests.
Full daily-browser and Wasm requirements remain open, Vulkan first.

## IN-PROGRESS Wasm module cloning candidate (2026-10-15)

Goal ACTIVE, Vulkan first. Host work progressed while the separate untouched-phone
window question remains unanswered. Implemented native compiled-module clone
delegates, ownership-safe worker messages, page module cloning/transfer rejection,
and worker lookup safety when a serialization getter terminates its worker.
Worktree includes the prior page-wakeup candidate and remains uncommitted pending
device verification. See docs/android-wasm-clone-20261015.md.

Host results: clone15/15, streaming27/27, worker8/8, worker-ports3/3 PASS;
broad Wasm12/13 (shared memory transport still fails). Whole-process/all-thread
1Hz monitoring: settled host CPUmean0.444–0.778%, peak2.001%, no runtime crashes.
These are NOT Android measurements or proof of UI wakeup latency improvement.
Android build/native dependency/v2 signature checks PASS. Combined candidate:
build/codex-wasm-clone-20261015/lumen-vulkan-wasm-clone-debug.apk
SHA256 ebc5d77a87414133d867ec0756b5677cd75087196e5e1e24672ca88dd052242b.
NOT installed. Phone remains on42af2d... streaming build; no phone interaction
this turn. Credentials unused. Device contention is unresolved; do not interrupt
phone use. Meaningful host progress continues, so this is not an impasse.
All owned host test/build/server sessions finished and joined; no owned runtime
test, compiler or fixture server remains. No adb reverses were created this turn.

NEXT: uncontested old-build timing baseline, standalone page-wakeup candidate,
then combined clone candidate with all regressions, Vulkan CPU/RAM and tab/worker
lifecycle checks. Shared memory/isolation, frame channels, generic structured
clone and transfer conformance, worker incremental fetch, real Wasm applications,
Vulkan scrolling, YouTube, login and zoom remain unfinished.

## IN-PROGRESS page V8 wakeup candidate (2026-10-15)

Previous goal turn PROGRESS: streaming committed1ac3260. This turn PROGRESS:
implemented page foreground queue/deadline/wakeup integration; built corrected
candidate, ASan/UBSan queue tests and APK native/v2 signature checks PASS.
Worktree intentionally uncommitted pending phone test. See
 docs/android-wasm-page-wakeup-20261015.md for evidence and next commands.
Candidate hash76b1031a246f1f237b9e4d7517d8cf341e6a68e3acccb0d340a159262f1c7be5,
build/codex-wasm-page-wakeup-20261015/lumen-vulkan-page-wakeup-debug.apk.
Candidate NOT installed. Phone still has42af2d... streaming APK.

DEVICE CONTENTION: phone opened new tabs and moved to Settings during baseline.
Asked asynchronously for three-minute untouched test window; answer pending.
Do not interrupt user phone use. This is the first contested-device turn, not
three-consecutive-turn blocker. Meaningful host work completed; goal ACTIVE.
Initial latency1003–1009ms/cold compile,1818–1886ms/800ms stream is provisional;
repeat old-build fixture uncontested before comparing. baseline-idle CPU INVALID.

Host tests+build are done. No owned compile/monitor/server job remains; monitor
and server joined, reverses removed. App left running for user use, not stopped.
Next after reply: restart sampler/server, repeat old-build /wasm-page-wakeup,
install candidate, repeat fixture, streaming/worker regressions, close/reopen root
tab, record idle CPU/RAM. Source audit removed default NotifyIsolateShutdown
because it dereferences a nonexistent default runner with our custom page queue.
The old source V8 build remains frozen. Broader browser/Wasm goal unchanged.

## Android Wasm streaming checkpoint (2026-10-15)

Goal ACTIVE: daily Android browser + full browser Wasm, Vulkan first.
Implemented V8 compileStreaming/instantiateStreaming bridge with private Response
brand/state, incremental page response consumption, MIME/status/body-use/error
checks and cancellation. Fixed truncated download errors after response headers.
Worker APIs pass but worker fetch still buffers entire downloads internally.
See docs/android-wasm-streaming-20261015.md and docs/wasm-browser-20261015.md.

Phone final27/27 streaming checks PASS, original audit10/13PASS, worker8/8PASS,
Fetch6/6PASS, Request5/5PASS; Node17/17PASS. Remaining audit failures: module
structuredClone, compiled module postMessage, shared Wasm memory postMessage.
Full Wasm NOT complete. Stable engine/API coverage, WPT, real Wasm applications,
worker incremental fetch and full Streams/Response conformance still required.

Installed verified Vulkan APK:
build/codex-wasm-streaming-20261015/lumen-vulkan-wasm-streaming-debug.apk
SHA256:42af2d0940212a04e9f24e41c726f6887d5b4977be59bf40406c3adf113fa54f.
Idle82samples (four boundary samples excluded): CPUmean0.319%,peak5.155%,
RSSending249.78MiB.567whole-process/all-thread1Hz samples, zero errors.
App stopped, worker tab closed successfully, sampler/server joined, owned
adb reverses removed. No owned live job. Credentials unused.

NEXT PERFORMANCE BUG: page V8 foreground completion lacks event-loop wakeup
and deadline integration.800ms delayed fetch compiled in1811.91ms; ordinary
Fetch chunks arrive on time. Page tick_one pumps libplatform, but main loop
can sleep1000ms and js_next_deadline omits V8 tasks. Reuse/adapt tested worker
task runner for page isolates; register before Isolate::Initialize, provide
js_wakeup with safe lifetime, include delayed deadlines, drain only outermost
root loop with microtasks, close/unregister before disposal. Preserve worker
lifecycle and default background compiler jobs. Test latency and idle CPU.
Then module cloning/shared memory isolation/lifetime and broad Wasm coverage.
Broader Vulkan scrolling, YouTube, login and zoom remain unfinished; OpenGL ES
and CPU software deferred. User confirmed phone available; no need reask.

## Android Wasm worker scheduling (2026-10-15)

Daily-browser + FULL browser Wasm goal ACTIVE; Vulkan first. Worker async Wasm
timeout fixed with a registered foreground task queue, condition-variable wakeup,
delayed deadlines and post-task microtask delivery. Root page disposal now calls
NotifyIsolateShutdown. Default platform still handles background compiler jobs and
page task runners. See docs/android-wasm-worker-20261015.md.

ASan/UBSan queue tests pass, including4,000 posts racing shutdown. Final phone
fixture8/8PASS: startup/self-close, async compile/instantiate, rejection/recovery,
12 pending compilations, four workers,20 creation cycles, eight large compilation
termination cycles, compile after idle GC. Final original audit8/13PASS; fiveFAIL
remain: compileStreaming, instantiateStreaming, structuredClone(module), compiled
module postMessage, shared Wasm memory postMessage. Full Wasm NOT complete.

Final verified installed APK:
build/codex-wasm-worker-20261015/lumen-vulkan-wasm-worker-debug.apk
SHA256:09eaabc3e581fdcfaeef8266456cf16cb782e8a427d3c56691bb16bd6fd7958a.
Vulkan confirmed. Worker round trips7.48ms,2.74ms after idle; startup16.25ms.
Final idle75samplesCPUmean0.174%,peak4.04%,RSSending240.64MiB. Initial stage named
wasm-worker-settled-idle is INVALID due to phone switching toYouTube; user then
confirmed phone availability and final test restarted. No comparative RAM gain.
553whole-process/all-thread1Hz samples, zero cadence/parse errors. Closing worker
tab succeeds. App stopped, monitor/server joined, reverses removed; no owned job.

Next: implement streaming compiler/Response integration, module clone delegates,
shared-memory isolation/lifetimes, expand conformance and real-app testing. Useful
read-only reference exists in sibling /mnt/p3/project/browser/lumen/src/js/wasm.cc
and worker.cc, but audit it before reuse (worker streaming there is buffered;
shared-memory support and browser conformance still need work). Vulkan scroll,
YouTube idle/longplayback, login, zoom and daily features remain unfinished.

## Android native touch + Wasm audit (2026-10-15)

Full daily-browser goal ACTIVE, Vulkan first. USER ADDED FULL WASM SUPPORT; do not
reduce that requirement to the existing V8 arithmetic smoke test. See
docs/android-native-touch-20261015.md and docs/wasm-browser-20261015.md.

Native Android pointer/touch start/move/end/cancel now reach top-level pages with
stable contact identity/targets and touch lists. Touch cancellation suppresses
native scrolling and duplicate clicks. Native scrolling cancels the pointer stream.
Touch capability exposed; Android YouTube fake mouse-move tap hook removed.
Six JS touch tests,24 Android adapter/gesture tests, six viewport tests and desktop
YouTube visibility test pass. Local phone fixture confirms single click, canceled
tap/no click, canceled drag/no scroll, native scroll/one pointercancel/no click.
Iframe contact routing, touch-action/pointer capture/full touch interface conformance,
mutation-induced click retargeting and pinch zoom remain unfinished.

YouTube off-center tap now reveals controls without pausing: pause icon/time1:09
visible in screenshots ending1.22/2.24s, hidden by capture ending3.54s. Later long
playback hit existing error; youtube-touch-idle stage is ERROR, NOT paused evidence.
Fresh early pause confirmed0:02/play triangle and controls persist. Confirmed paused
110samplesCPU3.170%,peak24.51%,RSS327.41MiB: NOT clean idle pass; profile periodic
work before declaring idle target met. Seeking/fullscreen/long playback unfinished.

Wasm PHONE audit7/13PASS: sync/async page compilation, memory growth, BigInt,i64,
SIMD,bulk memory,traps. FAIL: both streaming APIs absent, module structuredClone
loses module, Worker compilation times out, compiled module and shared Wasm memory
cannot be posted to Worker. worker.cc serializer has no delegates; worker taskloop
wait not woken by V8 foreground tasks is a candidate for async timeout. Next implement
proper browser Wasm integration and expand conformance/real-app coverage; do not
claim full support. All source entry points/requirements in Wasm audit doc.

Installed verified APK:
build/codex-native-touch-20261015/lumen-vulkan-native-touch-debug.apk
SHA256:219e9ebaea744aeeb976f92a712961b559d6c820071b7d12306aefd1cde97097.
Evidence build/codex-native-touch-20261015,728whole-process/all-thread1Hz samples,
zero cadence/parse errors. App stopped, monitor/server verified joined, owned
reverses removed; no owned live job. No credentials entered. Vulkan renderer
confirmed in log. RAM/login/daily features/zoom incomplete; GL/software deferred.

## Android vectorized image spans (2026-10-15)

Full goal ACTIVE, Vulkan first. See docs/android-image-spans-20261015.md.
Bilinear interior spans now vectorize on ARM64 with exact scalar pixel results.
Optimized sanitizer tests include18,504 span cases and local-origin image cases;
2,000 scroll pixel comparisons and4 display damage scenarios pass. APK checks pass.
Installed: build/codex-image-spans-20261015/lumen-vulkan-image-spans-debug.apk
SHA256:85de378794a3a362da52a7f50aab1566c599189bd1005b92d8cafb9a63a33368.
Controlled thumbnails: paint8.832->7.335ms/frame, CPU45.70->39.75%, RSSunchanged.
YouTube paused0:02:985paintframes, raster10.173ms, total14.234ms, CPU75.26%,
RSS361.85MiB. Different recommendations from prior run; no controlled speed ratio
or RAM improvement. Settled paused CPU3.12% is not a clean idle pass.
SurfaceFlinger short2.20s tail:126intervals, median16.698ms,p9516.806ms,
five>25ms,max50.106ms. Remaining jank; no sustained60FPS claim.
968whole-process/all-thread1Hz samples, zero cadence/parse errors. Profile15,487
samples, zero lost. Evidence/matching symbols under build/codex-image-spans-20261015.
App stopped; monitor/server joined; private profile files and owned reverses
removed. No owned live job. No credentials entered. Next investigate native touch
semantics for YouTube: Android taps currently synthesize mouse clicks and navigator
reports maxTouchPoints0. Controls/login/zoom/RAM remain unfinished; GL/CPU deferred.

## Android GPU video scroll reuse (2026-10-15)

Full goal ACTIVE, Vulkan first. See docs/android-video-scroll-20261015.md.
Scroll reuse now supports one valid GPU video hole with matching media owner and
rounded pixel geometry; fixed/exposed strips retain holes. Android page texture
ring can present separate video. Unsupported/changed cases still fully repaint.
2,000 sanitizer pixel comparisons,13 GPU scroll scenarios (including complete
RGBA/YUV presenter),11 fixed bounds and4 display damage tests pass.
Native/v2 APK checks pass; installed clean APK:
build/codex-video-scroll-20261015/lumen-vulkan-video-scroll-debug.apk
SHA256:1bfa49bdd7aca98c46f784a3de24c7f52260b44d42fea836e5f529a88893058b.

Fresh controlled baseline->new: raster7.172->4.398ms/frame, totalpaint13.595->7.721,
360->364reported frames/~7.9s, CPU76.22->47.45%, RSS253.47->253.13MiB (NO RAM gain).
Real YouTube confirmed paused0:21,20swipes:861frames/~20s,raster12.853ms,
totalpaint16.807ms,CPU78.11%,RSS331.54MiB. Prior35.457ms totalpaint comparison is
DIRECTIONAL (different recommendations/gestures), not stable60FPS proof.
Settled-paused17samples CPU2.637%. Earlier youtube-video-paused label was before
pause confirmation; exclude it. Subsequent playing scroll advanced0:21->0:29,
kept video positioned; controls/long playback not validated. Final screenshot
shows pause icon, not confirmed paused. App is now force-stopped.

560 whole-process/all-thread1Hz samples, zero cadence/parse errors. Profile16,302
samples zero lost: raster48.50%,fill13.59%,memmove9.51%. Hot instruction lookup
points to image vertical interpolation/coverage/blending, raster.c312-319.
Next Vulkan optimization should target that loop and validate exact pixels.
Evidence+matching symbols:build/codex-video-scroll-20261015/youtube-video/.
Monitor/server stopped+joined, owned reverse18765/18766/private profile files
removed. No owned live job. No credentials entered. Full remaining scope from
previous checkpoints still applies; OpenGL ES/software deferred.

## Android spans and image scanlines (2026-10-15)

Full goal ACTIVE, Vulkan first. See `docs/android-raster-spans-20261015.md`.
Constant-color fill/clear loops now operate on spans; vertical gradients compute
color once per row, with exact cardinal directions. Bilinear images cache only
2 horizontally interpolated source rows, preserving previous pixel rounding.
Extra cache is8bytes per damaged horizontal pixel, freed per image.
ASan/UBSan layer suite passes:12 original golden hashes,100 scalar gradient cases,
wide-gradient uniformity regression,180 direct-source image comparisons. Four
existing display-damage tests pass. Old raster fails the new gradient test.

Controlled fixed-video opacity fixture after spans: raster74.84->7.229ms/frame,
paint86.62->13.66ms,64->359reported frames/~7.9s;CPU80.79->67.24%,RSS252.09->253.24.
Real YouTube after spans37.03ms raster; final scanline early-pause repeat28.00ms,
35.46ms totalpaint,463frames/~20s,CPU86.80%,RSS335.26MiB. Different recommendations
and completed gestures; directional comparison, not controlled real-site speedup.
Still too slow for consistent60fps and NO demonstrated overall RAM reduction.
Final paused27sample mean3.87% including settling; do not claim idle CPU pass.

IMPORTANT: first youtube-scanlines run hit the pre-existing playback error; its
12.61ms/920frame result is INVALID for video-present comparison. Use
`youtube-scanlines2`, paused0:03 and retained frame/controls throughout. Runtime
profiles and matching symbols live separately in youtube-layers, youtube-spans,
youtube-scanlines, youtube-scanlines2 under build/codex-wakeups-20261015.
Parent symbols are older SDL-only. summarize-youtube.py/opacity-summary.py reproduce
numbers. New profile17719samples still raster63.93%self/fill12.95%/memmove6.77%.

Final clean APK: build/codex-wakeups-20261015/lumen-vulkan-scanlines-debug.apk
SHA256:cbaf13370e9f7f121d8ce29fa4c87cdd4993287d4c40440752c95d549e95da76.
Native/v2 checks pass; packaged libmain directly checked for absence of temporary
probe paths/counters. Installed. App stopped; monitor/server joined; owned reverse
18765/18766 and private paint marker/log/perf file removed. No owned live job.
Whole process + all threads sampled1Hz throughout; no cadence/parse errors.

Controls: resume works, hides controls; center AND off-center taps during playback
paused video instead of providing a reveal-only timing test. Paused controls stay
visible. Investigate proper touch event lifecycle next: current Android gesture
path synthesizes mouse clicks, click_page dispatches no touchstart/end, and
page_move_keep is a mousemove workaround. Root cause not yet proven with traces.
Playing hide duration, seeking/fullscreen and long playback remain incomplete.
ChatGPT/Gemini/Google login/zoom/daily feature coverage and memory goal unfinished;
no credentials entered; OpenGL ES/software optimization still deferred.

## Android bounded opacity groups and SDL waits (2026-10-15)

See `docs/android-opacity-layers-20261015.md`. Vulkan first; full goal active.
Opacity group pixel buffers/compositing now bounded to actual visible commands,
including glyph ink, shadows and nested overflow. ASan/UBSan layer tests and12
previous-raster golden hashes pass;4 damage tests and14 SDL wait tests pass.
SDL waits avoid a synthetic sentinel and duplicate initial pump; no meaningful
phone CPU improvement from SDL alone. Site25ms activity timer preserved.

Phone fixed-video opacity stress: raster2005.6->74.84ms/frame,3->64 reported
frames in~7.9sec; processCPU96.80->80.79%, RSS246.19->252.09MiB. NOT a RAM gain.
Corrected after2 run used; first after run had stale-ready matching. YouTube
paused0:23,20swipes:174 frames,80.03ms raster/frame (earlier230-265ms),CPU84.33%.
New profile17403 samples: remaining raster/gradient/fill/clear work dominates.
Next optimize those hot loops while keeping pixel correctness, then remeasure.

Clean native APK c523e97e65f774013cd67bfbd16d63b6169797b4b23056a76962a44cc341575a
at build/codex-wakeups-20261015/lumen-vulkan-bounded-layers-debug.apk; native/v2
checks pass. Temporary probes restored and packaged binary strings checked.
Phone app currently paused;1Hz monitor and fixture server are still active.
Monitor PID in monitor.ready; verify cmdline before cleanup. Owned reverse18765/
18766 and private paint-profile.enabled/detail-profile.log/wake-scroll.perf.data
must be removed on cleanup. Do not remove cookies/settings or enter credentials
without completing rendering work. Current symbols in youtube-layers/; parent
libmain.so matches the older pre-layer profile. All evidence in that build folder.

## Android event-driven visibility observation (2026-10-15)

Full goal active, Vulkan first. See `docs/android-intersection-observer-20261015.md`.
Removed permanent 300 ms IntersectionObserver polling. A shared document visual
version now tracks completed style/layout and native/scripted scrolling; viewport
tracking includes scroll offsets. Observation/disconnect/reentrant delivery use
native pending state, checks bounded to 20 Hz during changes, no idle timer.
This does NOT fix existing IO root/clipping/threshold/margin conformance gaps.

Five new JS tests failed before and pass after; 15 combined JS tests, native
viewport UBSan checks, 22 viewport-style/hit tests, 17 device checks and native
physical-scroll notification pass. 500-target fixture: 58,000 -> 0 geometry reads
in 35 seconds; 30-sample idle CPU 1.8978% -> 0.4260%, RSS234.43 ->233.20MiB.
Six-swipe CPU 28.6844% ->29.9769% (8 samples): modest added scrolling cost, NOT a
scrolling speedup. Initial 16 ms pump was31.3524%;50ms reduces that cost.

IMPORTANT PACKAGING CORRECTION: the previous controls APK described below as
clean retained old diagnostic startup paths in cached android_platform.c.o.
The initial observer APK did too; no preload JS file existed during benchmark
runs. Touching the restored source forced compilation. Final packaged libmain.so
was directly checked: both detail-profile.log and detail-probe.js paths absent.
Source cleanup/ELF checks alone are insufficient after restoring probe backups.
Clean final APK: build/codex-intersection-20261015/lumen-vulkan-intersection-clean-debug.apk
SHA256:3c4511a1cf8ff37eae30f2b4d2cc81420aafde81fa307195816d813e02236680.
Native and v2signature checks pass. Final clean fixture17 checks plus physical
scroll pass; one-observer settled idle0.0677%,RSS235.08MiB (not500-targetcomparison).

Real YouTube diagnostic confirmed target receives pause at57.419s. Some earlier
tap attempts only revealed controls and longer playback still reached its error
screen. Clean final run paused at0:13 and retained controls/frame throughout:
115 unprofiled samples mean3.2184%,peak28%,RSS315.41MiB; first30mean3.3387%,
last30mean2.8343%. No cadence errors. Do not claim universal idle pass or real-site
RAM improvement. New profile657userspace samples/20.0402sec/zero lost: no sampled
jsg_vrect/n_vrect; js_tick21.46%inclusive/3.20%self; SDL_WaitEventTimeoutNS21.92%.
NEXT: profile JS timers and main-loop wakeups, then full touch/control timing and
uninterrupted playback. frames_tick does not double-tick child JS; inspected.

Evidence under build/codex-intersection-20261015: samples/server log,summary,
whole-idle stats,profile and symbols/callgraph,controls diagnostic/screenshots.
App-UID simpleperf used without explicit settings changes; perf_harden0 and
perf_event_paranoid1 read both before/after; no root/app_profiler.py used.
Preserve matching current native symbols before rebuilding if needed.

Clean APK installed; app stopped; sampler/server stopped and joined; own reverse
removed; private probes/log/profile files removed. renderer=vulkan,pixel_scale200.
No credentials entered. ChatGPT/Gemini/Google login/zoom/daily functions unfinished;
OpenGL ES and software optimization deferred. No owned live job remains.

## Android positioned controls and fixed geometry (2026-10-15)

Full goal active, Vulkan first. See `docs/android-player-controls-20261015.md`.
Fixed hit testing of positioned overlays under static wrappers: the mobile
YouTube overlay painted above the video but taps went through to the video.
Layer selection now walks ordinary wrappers with reverse-tree/equal-z ordering,
without per-event heap allocations. Also fixed hidden inline hit rectangles.
Phone regression then exposed fixed getBoundingClientRect() subtracting page
scrolling; viewport geometry now stops document scrolling at fixed ancestors
and uses the target document's viewport. This is not full CSS conformance.

37 native CSS/layout/geometry assertions under UBSan and nine existing scrolling
hit tests pass. Initial ordering check failed 12 assertions; hidden-inline check
failed twice before its fix. Six final phone fixture checks pass, including
trusted control taps at scrollY 0 and 332.5. The post-scroll computed target
failed before the geometry fix. Fixture idle: 30 samples mean 0.4552%, peak
7.8431%, RSS 229.28 MiB, no cadence errors (settling spike retained).

Instrumented real YouTube: actual control receives pause, state paused at
30.218 s; timeline tap seeks to 79.438 s; resume plays through at least148.015 s.
No UMP rejection observed in that run, but it included pausing/seeking and does
NOT prove uninterrupted playback repaired. Final clean APK visually confirms
controls and paused 0:21/play icon at one/eight seconds. Playing hide duration,
full touch lifecycle, drag seeking, fullscreen and long playback remain pending.
No account credentials entered; ChatGPT/Gemini/login/zoom still unfinished.

Final clean paused YouTube idle: 30 unprofiled samples mean3.1673%, peak4.0816%,
RSS348.34MiB, zero cadence errors. Earlier settled window~4.0%; do not call this
an idle performance pass or controlled improvement. Native profile839 userspace
samples/20.0407s/zero lost: js_tick20.26% inclusive/5.60%self,jsg_vrect4.05%self,
plus SDL main-loop housekeeping. These are profile shares, not processCPU.
NEXT profile/repair repeated context ticking and geometry callbacks on Vulkan;
then complete controls/playback/real-page scrolling. OpenGL/software deferred.

Clean installed APK:`build/codex-youtube-controls-20261015/lumen-vulkan-controls-debug.apk`
SHA256:`ba443a7e6d04d98858ce2edda5bfb406474d2bd66082691acd585b00b895e67f`.
Native/dependency and v2signature checks pass. Temporary native probe code restored.
App stopped; monitor/server stopped and joined; reverse removed; private probe,
log and profile files removed. Raw process/thread samples and profiles preserved
in the ignored evidence folder. No owned live process/session remains.

## Android mobile identity and off-screen drawer (2026-10-15)

Full goal active, Vulkan first. See `docs/android-mobile-drawer-20261015.md`.
Android now uses mobile Android HTTP/JS identity and consistent low-entropy
client hints. YouTube selects m.youtube.com. Fixed specified percentage widths
of positioned elements resolving against inset-reduced space: the hidden drawer
at left:-100%, width:70% was twice as wide and covered 40% of the viewport.
Native regression fails before repair and passes after (14 assertions, UBSan).
Three JS tests and six phone identity checks pass. Real YouTube is now full width.

Vulkan drawer fixture, same six swipes: mean CPU 70.7192% -> 33.4966% (~53% less),
10 one-second samples each. Full repaint changes to partial/row reuse; example
40 frames/1005 ms -> 60 frames/1014 ms. Fixture only, not general site FPS.
Idle 0.3327% -> 0.3323%; RSS 230.34 -> 229.43 MiB. All spikes included.

Clean installed/native/signature-verified APK:
`build/codex-youtube-detail-20261015/lumen-vulkan-mobile-drawer-debug.apk`, SHA256
`3a0a9eb9071df9708d568cc8a0b2836c5e0abd809927d7762e1835cf87cdfe5b`.
Diagnostic native code restored; private probe/log/profile marker removed;
app stopped; sampler/server joined; adb reverse removed. No credentials entered.

YouTube NOT fixed. Captured exact error in desktop and mobile diagnostics:
ump.spsrejectfailure / HTML5_SPS_UMP_STATUS_REJECTED. Mobile fails at context
86.909 s after ~70 s video; desktop at 56.704 s. Public player maps a rejected
UMP status to this error; early 403s are not proven causal. Brave on same phone
continued playing, but is a different client. Mobile also throws at
`mobile-ERsVff.js:1027:199`, class extends undefined `_.EX`; relatedness unproven.
Clean final page renders/plays; taps remove mute prompt then leave a stationary
image, with controls absent at 0.5/3/7 s. Pause inferred, not instrumented.
Final 30 stationary samples mean 3.0287%, peak 4%, RSS 318.89 MiB; earlier window
3.3985%, peak 12%. Sustained clean playback and controls remain unverified.

Downloaded kevlar.js identifies earlier Xch/_.qE8 activity as generated periodic
consistency/adblock checks, not ordinary response caching. Do not suppress checks.
Next diagnose mobile controls/frontend exception and streaming rejection, profile
remaining CPU, validate real-page scroll. Auth/ChatGPT/Gemini/zoom/full daily use
remain unfinished. OpenGL ES/software deferred.

## Media notifications and replay race (2026-10-15)

Full goal active, Vulkan first. See `docs/android-media-notifications-20261015.md`.
Removed the unconditional 50 ms JS media interval: native state changes wake idle
players; playing elements retain position updates. Fixed replay-after-ended using
decoder-epoch-aware EOF and a post-demux epoch check. Native replay regression
fails before the fix; phone replay changes from fail to pass.

21 focused JS tests, two native sanitizer/lifecycle tests, eight phone media-event
checks and eleven phone video-frame callback checks pass. Controlled paused
fixture (video + 24 idle MediaSources): 30 settled samples, mean CPU 1.8632% ->
0.2017%, RSS 255.29 -> 245.37 MiB. Frame fixture idle 0.3680%. Fixture gains only.

Clean native/signature-verified APK installed:
`build/codex-vulkan-failure-20261015/lumen-vulkan-media-events-debug.apk`, SHA256
`93812de67b6b515e61704256139d028e7f2801dcfc5b8490524e8691d7a9d14d`.
No temporary diagnostic code in source or APK. App stopped, sampler/server joined,
reverse port removed, private diagnostic files removed. No credentials entered.

YouTube still plays then fails by 90 seconds. Final 30 error-idle samples average
6.2643%, peak 87% (spike included), RSS 423.42 MiB; above target. No site-wide gain.
Earlier diagnostic stack proves the site's error handler calls stopVideo/load,
not that load is the original cause. Detailed-error listener exists in ignored
`failure-probe.js`, but the runs with it did not fail; capture a failing run next.
Paused V8 profile confirms remaining recursive object cloning/JSON/GC. Profiling
itself adds CPU: do not use sampling/dump windows as idle measurements.
`paused-profile-final.log` is complete; `first-profile.log` is truncated;
`second-profile.log` covers playback/mixed state. The tap/3s/7s screenshots from an
earlier successful run show paused controls at 1:53, not broad controls acceptance.
ChatGPT/Gemini/auth/zoom and full daily-browser acceptance remain unfinished.
OpenGL ES/software deferred.

## Layout-driven ResizeObserver; real YouTube still errors (2026-10-15)

Full goal active, Vulkan first. See `docs/android-resize-observer-20261015.md`.
Replaced 100 ms observer polling with native completed-layout generation and
initial-registration notifications. Cross-document geometry reads now flush the
target's owning document; final device iframe initial size is correct (the first
candidate exposed a zero-before-layout bug). Callback-triggered updates are
bounded to one delivery per 16 ms; this is not full depth-based spec processing.

21 focused host tests and all 13 final phone fixture checks pass. With 500 observed
unchanged elements, 30 settled samples: CPU 2.8674% -> 0.3371% (~88% less), RSS
234.07 -> 227.17 MiB. These are controlled fixture results only.

Clean installed/native/signature-verified APK:
`build/codex-vulkan-resize-20261015/lumen-vulkan-resize-debug.apk`, SHA256
`e2aa988ba16551e81191bf989e2f8b28ba35f8d7586387547539da04e2969168`.
Only comment wording changed after that build; runtime code matches. App stopped,
owned monitor/server joined, temporary reverse removed. No credentials entered.

Actual YouTube initially plays then still errors. Final 60 settled samples average
4.2698% CPU, peak 10.1010%, RSS 434.70 MiB; no significant site improvement claimed.
Next profile remaining error-page work and player failure. Existing mediaPoll
still polls all attached players every 50 ms and allocates state each time;
confirm its current contribution before changing it. Prior trace shows player
error 5 after healthy playback, with native state reset by page load calls.
ChatGPT/Gemini/auth/zoom and full daily-browser acceptance remain unfinished.
OpenGL ES/software deferred.

## Blob allocation improvement; YouTube still fails (2026-10-15)

Vulkan first; full daily-browser goal remains active. See
`docs/android-blob-allocation-20261015.md`. A fresh V8 profile exposed YouTube's
`new Blob(q.split("")).size` allocation cost. Adjacent Blob text parts now share
one encoding allocation, preserving per-part Unicode conversion and mixed parts.
17 focused host tests and all seven device fixture checks pass. Actual-phone
20-construction fixture: 1,180.85 -> 121.49 ms (~9.7x faster), peak RSS
302.86 -> 241.06 MiB. Settled fixture CPU 0.3340%. These are fixture gains only.

Final clean APK installed, native/signature verified:
`build/codex-vulkan-tasks-20261015/lumen-vulkan-blob-debug.apk`, SHA256
`5ca4a8f8a2e8c7ccb174e646aed2acbf28b2cb796ca511a1e700c6769b7338d9`.
No temporary native profiling remains. Owned phone test processes stopped and
joined, reverse port removed, app stopped. No credentials entered.

Actual YouTube still errors (`final-4.png`); 30 settled samples average 4.3941%
CPU, 421.44 MiB RSS, above the idle target. Next continue allocation/scheduling
investigation and stable playback/controls validation. Navigator currently reports
zero touch points and desktop client hints; inspect real input support before any
capability changes. ChatGPT/Gemini/auth/zoom and full daily acceptance remain
unfinished; OpenGL/software deferred.

## Vulkan video frame callbacks and remaining YouTube idle cost (2026-10-15)

Full daily-browser goal remains active, Vulkan first. See
`docs/android-video-frame-callbacks-20261015.md`. Replaced no-op video frame
callbacks and zero quality counts with native presentation-driven delivery.
Cancellation, reentrancy, pause/seek, reload and dropped frames are covered.
Fixed an additional stale-frame delivery bug found during review: registration
on an already paused video waits for a new frame. Host regression fails before
that correction. 17 focused JS tests and both native sanitizer tests pass.
Final phone fixture passes all 11 checks, averaging 1.4663% CPU over 30 settled
samples with a paused video and pending callback (257.04 MiB RSS).

Final clean APK installed and native/signature verified:
`build/codex-vulkan-playback-20261015/lumen-vulkan-frame-callback-debug.apk`,
SHA256 `f9234ce0f980e4d6ae9812d60d8eb8f71ca60d4a1fa8a86c44b1ea038fd8d41d`.
No temporary diagnostics in source/APK and no credentials entered.

YouTube still intermittently fails. The first callback build played then showed
"Something went wrong"; settled error idle averaged 4.8413% CPU, 423.20 MiB RSS.
Fresh native profiling confirms substantial GC and JSON work; matching symbolized
report `frame-error-resolved.txt` is preserved. Next investigate JS allocation and
scheduling on the failed page, then validate playback and prompt controls again.
The final stale-registration correction has local device coverage; do not conflate
it with a new YouTube acceptance run. ChatGPT/Gemini/auth/zoom and broader daily
features are unfinished; OpenGL/software remain deferred.

## Request state repair and controls diagnosis (2026-10-15)

Vulkan still first; full daily-browser goal active. See
`docs/android-request-state-20261015.md`. Request copies/fetch now use private
metadata rather than overridden public getters. 12 focused host checks pass;
phone Request fixture 1/5 -> 5/5, streaming 6/6. Local idle 0.166% CPU.
Clean Request APK saved in `build/codex-vulkan-playback-20261015/`.
YouTube still errors, including while paused; error idle remains above target.
Temporary controls probe confirms tap removes auto-hide and opacity becomes 1;
controls appeared by the 4-second screenshot and remained at 7 seconds in one
paused run. This does not prove prompt control response or stable playback.
Next investigate advertised no-op video-frame callbacks/quality counters and
remaining idle work. ChatGPT/Gemini/auth/zoom and full feature acceptance remain
unfinished; OpenGL/software deferred. No credentials entered.

## Vulkan fetch streaming and URL lookup repair (2026-10-15)

Vulkan remains first; OpenGL ES and CPU/software remain deferred. Daily-browser
goal remains active. See `docs/android-vulkan-fetch-20261015.md` for evidence.

Fixed preflight callback shadowing that buffered streamed fetch responses until
completion. Actual-phone first data improved from about 3,010 ms to 51–74 ms.
Both denied-preflight and denied-response checks still pass. Rounded fractional
JS timer waits upward to avoid premature main-loop polling; this alone did not
resolve excess YouTube error-page idle CPU (still about 4.43%).

A playback V8 profile found repeated whole-document searches for `baseURI`.
A WeakMap cache keyed by document, mutation version and page URL reduced 250
reads on a large device fixture from 215.7 ms to 2.9 ms. All six mutation/history
correctness checks pass. This is not a whole-site speed or stability result.

26 focused host tests and final device streaming/base-URL fixtures pass.
Final clean APK installed, native/signature checks pass:
`build/codex-youtube-20261015/lumen-vulkan-stream-cache-debug.apk`, 85,240,084 bytes,
SHA256 `7a97676eeea48c2c477e706ed287f4987a5d086927397ef70eca25201510aadc`.
Temporary native profiling hooks are removed. Credentials were not entered.

YouTube remains intermittent: some diagnostic runs play for minutes, others
fail near 53–56 s despite healthy native queues and HTTP 200 media requests.
Final cache APK still needs actual-site acceptance and error-page idle profiling.
Do not claim YouTube fixed or daily readiness. Next: Vulkan YouTube/scrolling,
then ChatGPT/Gemini/Google login, composer CPU, zoom and remaining daily features.

## Vulkan YUV video and audio lifecycle repair (2026-10-15)

User priority remains Vulkan first until solid, then OpenGL ES, then CPU/software.
Daily-browser goal remains active. No delegation. See
`docs/android-vulkan-media-20261015.md` for measurements and remaining limits.

Planar YUV420 frames now feed the existing GPU NV12 presenter, avoiding CPU RGB
conversion and reducing queued-frame allocations. Handles signed/padded strides,
odd dimensions and BT.601/709 limited/full range; other formats use the old fallback.
Matched MI 8 Vulkan 720p30 video sample: mean CPU 81.8409% -> 56.4167%, mean RSS
318.6978 -> 300.9188 MiB. This is a fixture result, not general-site performance.

Fixed a reproduced SDL/player mutex inversion in clock reads and seek. The audio
callback records queue latency so clock reads need no SDL stream lock. Paused output
now releases its device while retaining queued stream samples; resume rebinds it.
Logical pause alone still consumed 5% CPU on Android. Final actual-device paused
sample is 1.3108% mean, 4% peak over 28.99 s, versus 7.3448% before lifecycle fix.
Delayed MSE audio metadata no longer prematurely disables the audio clock.

14 focused tests pass (YUV packing/color sanitizers, actual SDL concurrency and
device lifecycle, GPU upload, controls, raster layers and damage pixels). Old
clock and seek lock-order negative controls both fail as intended. Final APK
native dependency/signature checks pass; compiled only via capped wrapper and
verified V8 prebuilt. APK installed at
`build/codex-media-20261015/lumen-vulkan-media-debug.apk`, 85,240,084 bytes, SHA256
`767a83df50ec227b786aa094cfaeab80d24ad8f49b1bbd8f8ea4f792d6b82b16`.
Final local playback, seek, pause and resume pass; paused PNGs match across 30 s.
Temporary native/script diagnostic hooks are absent from final source/APK.

Final YouTube acceptance FAILS: independent final APK run loads slowly (blank at
25 s, video by 55 s), then shows "Something went wrong" at video time 0:56.
Controls/settings respond on the error screen, not evidence of healthy playback.
Next work should investigate this on Vulkan. Settings unchanged; app, sampler,
fixture server/reverse and private diagnostic files cleaned after capture.

YouTube diagnostic candidate played through 356.32 s; controls stayed visible
7 s after resume and settings opened correctly. Some runs still fail; HTTP400
FAILED_PRECONDITION and some403 responses occur on both successful/failed runs,
so their causality is unproven. Do not declare YouTube fully fixed or daily-ready.
Open work: further Vulkan real-site profiling, hardware decode, actual video-frame
callbacks/quality statistics, touch control semantics, ChatGPT/Gemini/Google login,
composer CPU, pinch/page zoom and remaining daily-browser features. The test email
domain was confirmed as gmail.com; credentials stay in conversation. None were
entered this pass. OpenGL/CPU acceptance remains deferred.

## Vulkan fixed-region scrolling installed (2026-10-15)

User priority: Vulkan first until solid, then OpenGL ES, then CPU/software.
Daily-browser goal remains active. No delegation requested; this work used one
agent. See docs/android-vulkan-daily-20261015.md for evidence and limitations.

Separate fixed header/footer damage now preserves partial raster and Vulkan row
reuse. Matched six-gesture MI 8 fixture samples: 80.31% -> 34.44% process CPU,
with steady painted intervals roughly40 ->59–60fps (not general site FPS).
Transient opacity layers allocate only repaint rows. Android YouTube taps now
wake the bounded control hold, restricted to player descendants. The committed
keyboard reveal repair was also built and validated on the device.

Fresh95 tests pass, including500 full-vs-partial scroll pixel comparisons and
sanitizers. Compact APK installed,85,240,084bytes,SHA256
 d4e94783a57e3e1b1d32a3f2df39e3bcaca22095a93b9cddbd146bba810fd76a
at build/codex-daily-20261015/lumen-vulkan-daily-debug.apk. Native/signature checks
pass; all ZIP entries match the tested pre-compaction candidate. Focused idle
89.02s: mean1.8086%,peak3.9604%,RSS219.211–219.219MiB. Settings preserved; app,
marker, reverse, server and sampler cleaned up; phone locks released.

Open work: Vulkan real-site profiling/presentation stalls; YouTube playback
errors around30s prevent full controls acceptance; ChatGPT Google login returns
home without authentication; Gemini main area blank/sign-in inactive; composer
CPU and login unvalidated; pinch/page zoom and Chrome parity remain incomplete.
The user confirmed the test account domain is gmail.com; credentials are only in
conversation, must not be persisted. No credentials entered yet. window.open()
is a stub but is not yet proven to cause the login failure. Other backends are
explicitly deferred. Do not label this browser daily-use ready.

## Caret repair installed; focused idle improves, keyboard reveal next (2026-10-13)

Installed source9e5e520808cad9c23e17f23ba2f1f709dbcab4cc, same branch
codex/android-vulkan-momentum-scroll. APK in build/codex-android-caret-damage/final:
codex-agent-android-caret-damage-compact-debug.apk,85240084bytes,0444,
SHA256dfd784c26aa2096202985581431430aa71836ad492f6b6ecf8e704eaae855853.
Fresh162 distinct host tests pass,24 focused sanitizer repeats pass; actual
raster320 baseline/sanitized transitions match full-render pixels. Removing
structural-context hashing in a throwaway negative control fails3 cases.
Actual API29 main.c compiles; fresh23 APK checks pass,14 objects rebuilt and281
reused, verified prebuilts, no V8 source build. Build45.698s,meanCPU242.27%,
peak450.18%,peakRSS1748.58MiB; shared480%,idle/nice19,all build scopes drained.

Fresh phone owner installs-r once, preserving data/settings. Main and owner
review actual PNGs: scrolled fixed overlay now opens/closes; full-width edges,
header/footer and toolbar remain intact. Band2 focus still hides below IME:
BrowserSurface1080x2112 shrinks to1080x1261. No manual scroll workaround.
Focused hold60.065s contains59 strict same-stage intervals/59.05s: processCPU
mean1.591871%,peak2.061856%,renderer mean1.253175%,RSS226.539-226.543MiB.
Prior failing five-second window was8.2% process/8.0% renderer; durations differ,
not a matched general benchmark. Current55 complete paint pairs contain110
partial frames and0 full frames; threadCPU raster52.858ms,compose0.510ms total.

Run is NOT all-pass: screenshot09 Back-recovery review receipt arrives0.243s
after harness timeout. Actual image shows recovered viewport, but gate fails;
optional typing/sites NOT executed, no retry. All299 nominal1Hz samples accepted,
maxdevicegap1.05s. Cleanup app/marker/reverse/server absent,sampler joined,locks
released,settings/display/rotation unchanged. Owners closed,source freeze lifted.
Evidence build/codex-android-caret-damage/{green,final,phone},phone/REPORT.txt.
Bounded phone evidence85/100; overall daily-use readiness25/100.

Next product bug: resize relayouts but never reveals the focused text control.
Main adds test-first geometry and App integration regressions for one-shot
post-layout focus reveal, nested/reverse/fixed scrollers and contenteditable.
Initial RED has2 wiring failures and2 setup errors from existing CSS partial
initializers under-Werror; Main aligns test flags with adjacent suites. Fresh
corrected RED executes23:5pass/18behavior-or-wiring failures,0errors. MeanCPU
78.03%,approximate peak104.71%,sampled peakRSS36.40MiB; scopes drained,V8 unchanged.
Evidence build/codex-android-keyboard-reveal/{red,red-corrected}.

Main implements Android-only one-shot reveal after fresh layout, before display
list generation. Focus and resize requests reveal the current text control or
contenteditable selection box, innermost scrollers first, then the document.
Fixed ancestry stops document scrolling; absolute containing blocks bypass
unrelated scrollers; column-reverse scroll origins, borders, translations and
clamping are respected. Visible/spanning oversized fields do not jump. A consumed
request does not keep overriding later manual scrolling. Non-translation
transforms are conservatively left unchanged; exact multiline caret/iframe and
fixed-overlay occlusion behavior are not established by these geometry tests.
GREEN/Android compilation and new phone acceptance pending; not installed.
Latest goal turn produces phone evidence,regression tests and a candidate repair,
not just a status update. Full browser goal remains active and incomplete.

## Hit-test repair passes host verification; caret regression in progress (2026-10-13)

Immutable bde548f passes125 distinct cases (9 focused,116 adjacent), plus9
ASan/UBSan focused repeats. Actual Android API29 main.c/layout/block.c compile
cleanly. No full link/APK/install/device acceptance from this verification.
Evidence build/codex-android-hit-scroll/green/REPORT.txt. Host mean CPU93.95%
tests/92.21% compile/73.69% sanitizers, peak aggregateRSS153.60MiB; shared480%,
SCHED_IDLE/nice19, scopes drained and frozen V8 unchanged. Both prior test
owners closed. Installed phone remains source910ef0e/be50a858.

Main adds damage-policy and actual-raster pixel regressions before changing
product code. Twenty policy cases first execute14pass/6behaviorfailures, zero
errors: inserting/removing a caret before fixed controls incorrectly damages
unchanged page regions, including when the caret is outside the viewport.
Clipping/layer-state changes must still conservatively invalidate affected
contents. Actual-raster tests compare incremental pixels against a full raster;
their first compile attempt had a test-only now_ms linkage conflict and missing
M_PI under strict C11, corrected by using actual util.c and GNU11. A fresh
owner verifies those tests against immutable bde548f, not modified main.c.

After policy RED, Main trims equal display-list prefixes/suffixes before
damaging unmatched items. Signatures include accumulated clip/layer operation
history so matching item content alone cannot hide changed drawing state.
This is linear-time, adds no signature fields or framebuffers, and retains
conservative full repaint for large changes. Product GREEN verification and
phone CPU acceptance are pending. Keyboard focus reveal remains a separate
unresolved bug. Overall daily-use readiness25/100.

## Android control regressions reproduced; hit-test repair pending validation (2026-10-13)

Phone baseline remains installed be50a858/source910ef0e. New committed fixture
and checklist are tests/pages/codex-android-scroll-controls.html and
docs/codex-android-controls-acceptance.md (e7bec38). Header-only scrolling and
optional footer separate row-reuse eligibility from full fixed-union fallback.

First controls attempt failed in the harness: startup was labelled before
am-start returned. Original failure preserved; nine offline tests verify corrected
ordering. PID31709 was force-stopped by the tester, not a demonstrated crash.
Fresh owner then runs the unchanged APK for182.558s with183 accepted1Hz samples,
zero rejects. Both owners verify cleanup/app absence/lock release/settings.
Evidence: build/codex-android-scroll-controls/{phone,phone-retry}.

Main reviews actual PNGs02/04/05/06/07/08: header and full-width edges survive
forward/reverse scrolling; footer is visible; yellow overlay opens/closes at
the top, but the same fixed button tap after scrolling has no effect. Band2
input near the lower edge is hidden when IME shrinks BrowserSurface from
1080x2112 to1080x1261. Five idle seconds then average8.2% process CPU, peak8.91%,
SDLThread8.0%, RSS226.01MiB. Guard stops before typing/Back/formal idle/sites.
Google/Wikipedia NOT tested. No new APK or general acceptance claim.

Source diagnosis: layout_hit reads Layout.scroll_x/y, which are never assigned.
Main replaces implicit stale offsets with explicit owning tab/frame offsets at
all11 call sites, including DOM hit testing, hover, wheel, selection and links.
New actual-code regression suite first records9 tests:3pass/6behaviorfailures,
zero compile errors. Main implements the repair after RED; fresh GREEN owner
checks it. No installed-phone fix claim yet. IME visibility remains unfixed.

Read-only postmortem captures21 complete paint wall/CPU pairs. Latest focused
idle pairs show two full paints per~1.06s, roughly39-42ms rasterCPU plus26-27ms
composeCPU per pair. Main is investigating blink-driven display-list insertion
and index-based damage comparison; thread attribution alone is not the cause.
Bounded phone coverage55/100; overall daily-use readiness remains25/100.

## Scroll row reuse verified and installed on Android (2026-10-13)

Installed source910ef0e07bcdd93acd6a014a0850af7e6da451e3, branch
codex/android-vulkan-momentum-scroll. APK:
build/codex-android-scroll-ring/final/codex-agent-android-scroll-ring-compact-debug.apk
SHA256be50a8581e6e6dcb5a76faa0285b68bb8630ec4f913e1c4c44b31f47734ad150,
85,240,084bytes,0444;338-file immutable snapshot. Main verifies hashes/mode,
builder terminal receipt and release PID absence. Fresh23 APK/API checks pass;
actual main/gpu compilation and gpu_scroll_page/gpu_upload_page links verified.
Build42.321s,meanCPU218.93%,peak384.94%,peakRSS1788.22MiB; shared480%,
SCHED_IDLE/nice19,verified prebuilts,V8 frozen,all owned scopes drained.

Host GREEN116distinct units/179executions and63sanitizer scenarios/63executions
are reused explicitly by the builder. Real host Vulkan llvmpipe validates the
actual WGSL/pipeline with38frames/1368exact pixels/36uniform readbacks, including
wrap,reverse,fixed rows,padded stride,linear reset and RGBA video metadata/alpha.
This is software Vulkan correctness,not phone GPU performance. Import-path and
default NVIDIA-ICD loader failures remain preserved; process-local lvp selection
succeeds. Failed first adjacent phase lacks saved CPU telemetry; not zero.

Fresh phone owner installs exactly once with -r and private settings unchanged.
Same30-band fixture,HTTP200,Vulkan200%,20swipes,1080x2112 own BrowserSurface,
inferred540x1056 layout. Main and owner actually inspect top/end PNGs live before
formal idle: Bands1/2 to23/24,full-width edges and intact toolbar,no obvious
corruption. Band23 heading is partly clipped at the endpoint: slightly further
scroll than prior45dc run,NOT pixel-identical endpoint or a pixel-parity test.

Scroll10.01s/10intervals:meanCPU38.5614%,peak41.4141%,RSS229.1016-231.1680MiB;
renderer SDLThread26.6733%. Prior45dc:50.2%,peak53%,peakRSS230.8984MiB,
renderer38.4%. Observed23.18% relative process-CPU reduction in this one-run
comparison; RAM essentially unchanged. Formal idle60.02s/60intervals:meanCPU
0.01666%,peak1%,RSS229.5859MiB,passes.134samples/attempts,zero rejects/errors,
maxdevicegap1.04s,process and every live thread sampled through review/cleanup.

Seven complete contained wall/CPU pairs:7016.4ms,406partial blit paints,all406
measured,none unavailable/missing. ThreadCPU sums(ms):prepare1.493,layout39.951,
signatures4.676,raster1039.555,compose2.911,present724.051. PresentCPU/paint
falls4.3373->1.7834ms(58.88%);raster rises1.9969->2.5605ms(28.22%). No claim
that every stage improves. Current wall sums:raster1105.905,compose5.213,
present922.848ms. CPU page shifting/raster remains; no FPS/GPU execution timing,
causal proof,real-site gain or Chrome50%-RAM proof from this fixture.

Evidence build/codex-android-scroll-ring/{green,final,phone}. Phone analysis and
cleanup pass: app/marker/reverse/server absent,sampler joined,both locks released,
display/rotation/settings unchanged; no root/global/security/cookie changes.
Fresh16 phone guard tests pass, prior8geometry+29parser checks explicitly reused.
Bounded implementation/evidence95/100; overall daily-use readiness25/100.
Next: on-device fixed content/overlay/backend/resize regression coverage, then
real-site compatibility tests. Raster-stage CPU remains the largest measured
render-thread cost; separate its page-shift/strip work before another change.
The full browser goal,web-facing WebGL/WebGPU and device APIs remain incomplete.

## Android scroll texture row reuse implemented (2026-10-13)

Main replaces eligible whole-page scroll uploads with a circular page region
inside the existing GPU frame texture. gpu_scroll_page advances its row offset
and transfers only exposed rows plus CPU-repainted fixed-position rows; ranges
merge and wrap safely. Toolbar pixels are not rotated. The32-byte RGBA uniform
includes row metadata; shader remapping uses one conditional subtraction rather
than per-pixel integer modulo. RGBA video uses zero row metadata; NV12 unchanged.
Normal frame uploads restore the entire coherent CPU frame and reset metadata;
texture recreation also resets it. Direct page uploads respect active mapping.
Main retains CPU memmove/raster and existing stale-frame/fallback exclusions.
No new full-size texture/framebuffer; no Android performance claim yet.

Test-first evidence build/codex-android-scroll-ring/red:40discovered,30executed,
23pass,7expected assertion failures,1missing-implementation setup error blocking
10new cases. After implementation, focused-stable42 and adjacent-retry74 cases
pass (116distinct);63 ASan/UBSan/float-cast scenarios pass. The new two normal
presentation-prefix regressions supplement the original RED scenarios. Tests
cover repeated forward/reverse/wrapped rows, fixed ranges, padded inputs,
full-page and linear-frame recovery, unavailable resources and app fallback.
Main verifies raw summaries and scenario journal, not a hardcoded aggregate.

Real host Vulkan shader/readback verification is still running with the fresh
GREEN owner; fresh APK builder and phone tester are offline-prepared, not
authorized to install. Installed phone APK remains45dc01bb from the preceding
entry. Do not substitute mock models for shader/driver or phone verification.
Host scopes retain shared480%/SCHED_IDLE/nice19; source V8 remains frozen.
Overall daily-use readiness remains25/100; this optimization is not yet accepted
on Android. Finish the real shader check, build/verify the committed source,
then repeat matched phone CPU/RSS and image checks before claiming improvement.

## Direct page upload APK installed and measured on Android (2026-10-13)

Source edc0d87 on codex/android-vulkan-momentum-scroll is now installed on MI8.
APK build/codex-android-direct-page/final/codex-agent-android-direct-page-compact-debug.apk
SHA25645dc01bb411e8471334a8f2e493a83e3bc285f2f7028080bebb5a158bec6dc7c,
85,240,084bytes,0444;336-file immutable source snapshot. Fresh23 APK/API gates
pass, actual main.c/gpu.c compile/link verified. Host102 distinct tests and36
sanitizer scenarios explicitly reused, not rerun. Build42.355s,meanCPU221.22%,
peak375.84%,peakRSS1729.24MiB; shared480%,SCHED_IDLE/nice19,V8 frozen/prebuilt.
Two supplemental build-harness audit errors remain preserved: final symbol is
valid local t,not global T; native commands are in AGP build_stdout_targets.txt,
not the Gradle log. Corrected evidence audit passes without rebuilding.

Fresh phone owner performs exactly one install-r, preserving private settings,
then the matched Vulkan2x fixture:20 swipes,1080x2112 BrowserSurface at0,88,
inferred540x1056 layout. Main and owner actually review top/end PNGs while live,
before formal idle: Bands1/2 to23/24,full-width edges,unchanged single toolbar.
No obvious corruption in these images; not comprehensive rendering acceptance.

Scroll10.0s/10intervals:meanCPU50.2%,peak53%,RSS228.8359-230.8984MiB;
renderer SDLThread38.4%. Prior5963 run:55.2%,peak56%,peakRSS231.3164MiB,
renderer44.1%. Observed process reduction5 percentage points/about9.1% relative;
single-run descriptive comparison,not causal proof. RAM essentially unchanged.
Formal idle61s/61intervals:meanCPU0.04918%,peak1.0101%,RSS229.3281MiB,passes.
129samples/attempts,zero rejects,maxdevicegap1.05s; all live threads monitored.

Seven wholly contained wall/CPU pairs span7015ms/405partial blit paints,all
measured with zero missing/unavailable frames. ThreadCPU sums(ms):prepare1.183,
layout36.070,signatures4.352,raster808.739,compose2.564,present1756.616.
Compose falls from1.4404 to0.00633ms/paint;present rises4.0823 to4.3373ms/paint
and raster rises1.8384 to1.9969ms/paint. Do not claim every stage improves.
Corresponding current wall sums: raster867.603,compose4.529,present1984.982ms.
Presentation remains the largest measured render-thread CPU stage; its upload,
driver and submission components are not separately established. GPU execution,
FPS,real-site performance and Chrome CPU/RAM comparisons remain unmeasured.

Evidence build/codex-android-direct-page/phone/analysis.json and cleanup-live.json:
app/marker/reverse/server absent,sampler joined,both locks released,settings and
display/rotation unchanged. No root/security/global setting changes or cookies.
Fresh12 binding/install guard checks pass; previous8 geometry and29 parser tests
reused with verified pins. Scope95/100; overall browser readiness25/100.
Next: reduce remaining scroll presentation/transfer work without replacing the
engine or sacrificing coherent fallback; validate resize/overlay/backend paths
on-device and expand real-site compatibility coverage. Do not call the50%-Chrome
RAM target, page-facing WebGL/WebGPU or daily-use browser goal complete.

## Android direct page upload implemented and host-tested (2026-10-13)

Main implements gpu_upload_page into an existing correctly sized GPU texture;
eligible full-width Android scroll blits now skip page-to-frame memcpy.
One frame_page_stale bool tracks skipped copies. Normal composition restores
all page rows before partial/full/skipped paints, screenshots, popups, video,
software rendering or resize. Failed direct upload/presentation also restores
the CPU frame before regular upload or SDL fallback. No added application
framebuffer/texture; existing wgpu staging may allocate. CPU page raster and
scroll memmove remain. No measured speedup or RAM reduction claim yet.

Test-first RED:23 new scenarios blocked by2 expected missing-implementation
setup errors;26 adjacent cases pass. GREEN:102 distinct unittest cases pass,
125 executions including focused rerun after a test-only identifier rename;
36 distinct ASan/UBSan/float-cast scenarios pass twice (72 executions).
Both runs include12,270-case composition matrices. Main reviews actual code,
guard/fallback paths and source-model coverage. Mean sampled hostCPU88.01%,
peak scopedRSS159.73MiB,shared480%/SCHED_IDLE/nice19;all5 scopes drain,V8 frozen.

Evidence build/codex-android-direct-page/{red,green}. Main catches the agent's
aggregate arithmetic error: adjacent79,not89;total102,not112. Original report
and receipt remain unchanged;green/MAIN-COUNT-CORRECTION.md supersedes their
incorrect112/135 counts using raw per-module summaries. No product failures.
Host implementation/test scope95/100; overall readiness25/100. Fresh APK build
and phone comparison are next; installed5963eecd still lacks this optimization.

## Real render-thread CPU profile complete: target redundant page copies (2026-10-13)

Fresh no-install phone run succeeds on installed5963eecd/source1405612.
Evidence: build/codex-android-cpu-profile/view-phone. Geometry8 and existing
CPU-parser29 checks pass. Main independently reviews actual top/end PNGs
during formal idle: Bands1/2 to23/24,20 exact swipes,full-width edges and one
toolbar. Own BrowserSurface bounds0,88-1080,2200 measure1080x2112, persisted
Vulkan200% implies540x1056 layout; this inference is explicitly recorded.

Complete scroll10s/10 process intervals:55.2% meanCPU,56% peak,
RSS231.3125-231.3164MiB; renderer SDLThread44.1% meanCPU. Formal idle61.01s:
0.01639% meanCPU,0.9901% peak,RSS229.4375MiB; passes.124 samples/attempts,
zero errors,max device gap1.04s. App/owned marker/reverse/server absent,
sampler joined,locks released,display/rotation unchanged. No reinstall.

Seven wholly contained wall/CPU pairs span7050.2ms and406 partial blit paints;
all406 CPU samples valid,none unavailable or missing. Summed thread CPU ms:
prepare1.162,layout36.851,signatures4.779,raster746.384,compose584.817,
present1657.404. Corresponding wall ms:raster798.969,compose593.124,
present1886.090. Presentation is predominantly calling-thread CPU, not just
waiting; this does NOT separate texture upload, driver/submission and other
presentation work, nor measure GPU execution. Compose alone uses about1.44ms
CPU per paint and8.3% of one core over these contained intervals. No FPS,
instrumentation-overhead,causal speedup or Chrome comparison claim.

Next product work: eliminate the redundant page-to-frame CPU copy on eligible
Android full-width GPU partial paints, uploading the authoritative page rows
directly into the existing GPU texture. Preserve full-frame coherence for
software fallback, screenshots, overlays, texture recreation and resize;
test stale-frame transitions first. Avoid adding another full-size buffer.
Do not just batch memcpy again: that earlier change had no measured gain.
The larger presentation cost remains a separate target; do not claim upload
alone is proven to account for it. Validate changes on this same phone fixture
before broadening site tests. Evidence scope95/100; overall readiness25/100.

## CPU APK installed; correct inset geometry rejected by test harness (2026-10-13)

5963eecd is now installed once with install-r and verified, preserving data.
The first CPU-stage run stops before swipes on a new harness predicate:
it incorrectly expects SDL/SurfaceView at0,0-1080,2112, but the own visible
org.lumen.browser.BrowserSurface has bounds0,88-1080,2200. Its actual drawable
is1080x2112; the inset origin is valid. This is not a product layout failure.
Main and owner independently view the captured correct Bands1/2, single
toolbar and full-width edges after cleanup. Do not backdate a live review.

Failure evidence remains in build/codex-android-cpu-profile/phone, especially
FINAL-FAILURE-REPORT.md and final-failure-audit.json.13 samples,zero errors;
four startup intervals4s:mean8% CPU,peak27.18%,RSS227.21-227.27MiB. No swipes,
formal idle or emitted stage reports: scroll CPU timing unavailable, not zero.
App/owned marker/reverse/server absent,sampler joined,locks released;
display/rotation unchanged,V8 frozen,host scope drained. Owner closed.

Main writes regression tests first in sibling view-phone:8 cases initially
give10 expected missing-helper errors, then implements browser_geometry to
select one visible own BrowserSurface and compute bound differences. Tests
cover inset/zero origin,landscape,wrong/hidden/duplicate surfaces and invalid
dimensions. Fresh owner must verify GREEN and run the same bounded profile
WITHOUT reinstalling. Geometry must come from measured bounds, not hardcoded
expected fields. Reviewed29 CPU-parser checks also require complete contained
wall/CPU pairs; missing CPU is never zero. Overall readiness remains25/100.

## CPU-diagnostics APK verified; phone profile pending (2026-10-13)

Source1405612 builds successfully using verified Android V8/wgpu prebuilts.
Main independently checks the immutable APK hash, size and mode and reviews
the terminal receipt. Artifact:
build/codex-android-cpu-profile/final/codex-agent-android-cpu-profile-compact-debug.apk
SHA2565963eecd031e8a7a6f3819ff561d1a6621a9f86629cc4ebef65e6cee8221126c,
85240084 bytes,0444. The332-file manifest hash is
e7867eec1e5b4f18e00e6a931e7fa68eb3a71a783186b984e370de694ffc813d.
Fresh23 APK gates and API29 clock_gettime import/export compatibility pass;
72 focused checks and10 sanitizer scenarios are explicitly reused with
matching source pins, not rerun. Build59.316s,mean184.96% host CPU,
peak sampled scoped RSS1738.43MiB; shared480%,SCHED_IDLE,nice19.

Two retained harness errors are not product failures: predecessor receipt
shape correction and a duplicate gate attempting exclusive log creation.
All15 gates had completed before the latter; final audit passes without
rebuilding or rerunning gates. All owned scopes drain; frozen V8 is unchanged.
Builder closed. Host evidence95/100, overall browser readiness25/100.
New APK is NOT yet installed. Fresh phone tester prepares separate wall/CPU
pair parsing and offline negative tests before authorized install/profile.
The last installed819345 fixture result remains55.67% scroll CPU and0.049%
idle CPU, approximately231MiB peak RSS; no performance gain is established.

## Opt-in render-thread CPU diagnostics implemented (2026-10-13)

Main adds Android CLOCK_THREAD_CPUTIME_ID alongside existing paint wall clocks.
paint_profile_begin initializes a per-paint CPU sample only when diagnostics
are enabled. Failed/malformed/backward clocks invalidate the entire paint's
CPU sample without dropping its wall sample. Completed valid frames accumulate
separate phase CPU sums and measured/unavailable frame counts. Report pairs
retain the unchanged paint-wall line and add a paint-thread_cpu line; no FPS,
whole-process CPU or GPU execution claim. Non-Android CPU timing is unavailable.
Disabled hooks read neither diagnostic clock; no extra threads/timers/heap
allocations, perf events, debugger, root or Android security-property changes.

Tests first:11 expected source-feature absence failures,13 adjacent passes.
After implementation,72 focused checks and10 additional ASan/UBSan/float-cast
scenarios pass. Tests extract actual functions but use MOCK clocks, not phone
measurements. Source review verifies disabled path, units, whole-frame failure
handling, reset and distinct output; docs updated. Evidence:
build/codex-android-cpu-profile/{red,green}. GREEN13.437s,mean84.26% host CPU,
peak sampled RSS152.86MiB; shared480%/SCHED_IDLE/nice19, scopes drained.
Scope95/100; overall readiness25/100. Build/install/profile still required.
Installed819345 is unchanged and predates CPU diagnostics and8be1bce bounds fix.
Next APK should contain both changes and use own-app CPU clocks only; retain
security.perf_harden=1 and perf_event_paranoid=3.

## CPU profiler preflight: retain phone security settings (2026-10-13)

Fresh read-only phone preflight completes in1.12s, both locks released and
agent closed. Evidence: build/codex-android-cpu-profile/preflight/REPORT.md.
MI8/API35, own run-as UID10196, installed819345 verified and app absent.
security.perf_harden=1, perf_event_paranoid=3; persistent simpleperf app UID
and expiration properties are empty. /system/bin/simpleperf is executable,
but it was NOT invoked, even for help/version. No device changes/app launch.
One host-only harness correction preserved; scope95/100, not profiling success.

Do not launch app_profiler.py or silently disable perf_harden. AOSP simpleperf
startup can modify that global property. Next: test-first app-local per-stage
render-thread CPU-clock diagnostics alongside existing wall timers, disabled
by default and with explicit unavailable-clock handling. Distinguish render
thread CPU from process/all-thread CPU and from GPU execution/present waits.
This avoids a security-setting change and can identify whether presentation
wall time is CPU work before changing the rendering pipeline. No such CPU
instrumentation has been implemented yet. Source8be1bce bounds fix is tested
but still not included in installed819345; combine it with the next diagnostic
APK instead of rebuilding solely for every small source change.

## Copy APK phone-tested; supported-scale overflow fixed in source (2026-10-13)

Installed819345 once with data preserved; app now stopped. Main independently
reviews actual top/end PNGs under build/codex-android-page-compose/phone:
Bands1/2 to23/24,20 swipes, intact full-width single toolbar,Vulkan2x.
Scroll9s:CPU55.667% mean/56.566% peak,RSS230.555-230.574MiB.
Formal61.03s idle:CPU0.0492% mean/1.0204% peak,RSS228.836MiB;passes.
124 samples,zero telemetry errors,max device gap1.22s. Cleanup verified,
locks released, phone agent closed. Compared with058271, composition remains
about1.47ms/paint and scroll CPU about55%: NO measured CPU/stage-cost gain.
Copy batching is only a structural call-count reduction, not a proven speedup.

Main finds and fixes real undefined float-to-int casts in fixed-scroll damage:
empty display lists use +/-1e9 bounds, which overflow int at supported2.5x/3x.
No-fixed pages now skip this calculation. Actual fixed bounds are clipped as
floats before int conversion, preserving shifted-in old/new fixed-box damage.
Tests first:11 actual-block cases yield5 expected float-cast-overflow failures,
6 passes; adjacent composition13 pass. After fix,73 focused checks +11 fresh
ASan/UBSan/float-cast-overflow scenarios pass, including10,605 scale/direction
cases. Evidence: build/codex-android-fixed-scroll-bounds/{red,green}.
This source fix is NOT in installed819345. Host scope95/100, readiness25/100.

Next performance diagnosis needs CPU attribution, not another wall-time guess.
NDK simpleperf/docs exist under build/codex-android-toolchain/sdk/ndk/28.2.13676358.
Do NOT blindly run app_profiler.py: it can request root/change global properties.
AOSP simpleperf startup can set security.perf_harden=0 even for help/version;
first use read-only availability/property checks with a fresh phone-test owner.
No global security changes authorized. App-only thread CPU timing is a fallback.

## Pacing phone evidence and verified copy APK (2026-10-13)

The fresh no-install pacing test under fling-pacing/view-phone completes20
swipes, Bands1/2 to23/24 (owner and Main actual image review). Scroll CPU over
9.01s is55.383% mean/58% peak,RSS228.129-230.148MiB. Formal61s idle passes:
0.0328% mean/1% peak,RSS228.543MiB.131 samples,zero telemetry errors,
max device gap1.03s. App/marker/reverse/server/sampler cleanup and both lock
releases verified; agent closed. One offline host-priority failure was corrected
and preserved. Seven contained wall windows show409 partial blits/7080.5ms:
raster814.408ms,compose603.178ms,present1871.981ms. No measured FPS/speedup.

Copy APK build is complete under build/codex-android-page-compose/final:
codex-agent-android-page-compose-compact-debug.apk,85240084bytes,0444,
SHA256819345808e0ef0a04dc63087ab543e59e95432812b95dd7a3013aaff3a6b524e.
Main independently verifies hash/mode and328-file source777b654 manifest
887df958100959dd9e6b6b11305627a5d09a0d8dbac25d329cc05f3d1525c6d2.
Fresh23APK gates pass;61+13 host GREEN explicitly reused with matching pins.
Only main.c.o rebuilt;27.396s/225.07% mean host CPU/1742.32MiB peak sampled RSS.
Shared480%/SCHED_IDLE/nice19 retained; all scopes drained, source V8 frozen.
One missing verifier fixture restored from predecessor, failure retained;
no product errors. Build agent closed. Phone acceptance pending under
build/codex-android-page-compose/phone; only its fresh phone agent may install.
Host-build and pacing-evidence scopes95/100; overall readiness remains25/100.

## Pacing installed; scroll acceptance still pending (2026-10-13)

Phone agent01a1346c installs the immutable058271 APK once with data preserved
and verifies installed bytes. Its42.17s live attempt stops at the top-image
review gate timeout BEFORE swipes; this is a harness coordination failure,
not a renderer failure or completed scroll/idle test. Both agent and Main
actually view the correct Bands1/2 fixture, full-width single toolbar. Evidence:
build/codex-android-fling-pacing/phone/LIVE-FAILURE.json and diagnostic-top.png.
Cleanup proves app/marker absent, server/reverse removed, sampler joined and
locks released, zero telemetry errors. Do not reinstall merely to retry:
the next fresh test agent should verify058271, acknowledge its own actual
visual review in the required receipt schema before gestures, and let Main
review independently afterwards. Preserve this failed attempt untouched.

Copy-batching commit777b654 remains host-tested, not phone-tested. Fresh
build-only agent01a13471-9bb8-7e33-b66c-49b4a9642013 prepares its immutable
APK under build/codex-android-page-compose/final. It has NO phone authority;
do not install over a live phone test. Source unchanged during its snapshot.

## Full-width page composition batching (2026-10-13)

Main replaces per-row page-to-frame memcpy calls with one contiguous copy
when sidebar is zero and both strides equal page width. Partial damage and
frame-height clipping are preserved; padded strides/sidebar retain row copies.
Empty/reversed ranges and skipped frames do not copy. No extra allocation or
GPU API change. Existing CPU rasterization and texture-upload costs remain.

Tests first:13 actual-block cases give3 expected copy-count failures before
the patch,10 passes; adjacent chrome damage8 pass. After patch,61 focused
checks and13 additional actual-block ASan/UBSan scenarios pass, including
12,270 pixel-equivalence combinations. Evidence:
build/codex-android-page-compose/{red,green}. This proves copy-call reduction
and pixel preservation in tested cases, NOT a measured phone performance gain.
Host-only implementation/test quality95/100; overall readiness remains25/100.

Phone-test agent01a1346c-97c2-7a60-b31b-3751ef350b31 owns the next bounded
run under build/codex-android-fling-pacing/phone, installing the immutable
058271 pacing APK with data preserved. That APK predates this copy patch.
It must monitor1Hz through cleanup, use ACTION_VIEW (no keyboard chords),
review actual fixture images and report measured CPU/RSS before acceptance.

## Valid Android scroll profile acquired (2026-10-13)

The single ACTION_VIEW diagnostics-ON fixture test is complete; main reviews
actual top/end PNGs (Bands1/2 to23/24). Evidence is under
build/codex-android-paint-profile/view-phone. Installed APK remains5818949;
the058271 pacing candidate is NOT installed. Exactly20 swipes,10.01s contained
scroll CPU mean53.047%/peak55.556%,RSS227.859-227.961MiB. Formal61.01s fixture
idle passes:CPU mean0.0328%/peak1%,RSS226.066MiB.124 samples,zero errors,
maximum device gap1.04s. App stopped, marker/reverse/server removed, sampler
joined and both locks released. Profile scope95/100, daily-use readiness25/100.

Seven wholly contained wall-stage reports show391 partial paints/blits over
7014.9ms:present1745.713ms,raster775.220ms,compose573.369ms. These are wall
times, NOT CPU/GPU execution or displayed FPS; present can include waits.
No paired OFF comparison: the earlier OFF geometry had an extra tab strip.
Main next tests batching contiguous full-width composition copies; this does
not remove CPU rasterization or establish any measured phone speedup yet.

## Pacing APK built; phone still uses diagnostics baseline (2026-10-13)

Commit0f78fd8 has a verified optimized Debug candidate, NOT yet installed:
build/codex-android-fling-pacing/final/codex-agent-android-fling-pacing-compact-debug.apk
SHA256058271aaa17e358a40d58f7189c564720a046a4b1dbad6a268532b66a8fa6d89,
85,240,084 bytes,0444. Main independently verifies artifact and manifest hashes.
The326-file snapshot manifest SHA256 is
5e422cd24900f7c65af8269f8abe13f3c4ccd6254880be3f5a2ce66d29a71c48.
232 fresh focused checks,29 fresh touch/fling/upload sanitizer scenarios,
6 JS groups, cache/V8 runtime and23 APK gates pass. The9 pacing ASan/UBSan
executions are explicitly reused from the fresh preceding GREEN verification.
Only main.c/android_fling.c objects rebuilt; Debug-O2/g/assertions, native
no-op, payload identity and zero ZIP gaps pass. Build39.194s,mean227.24% host
CPU,peak1721.53MiB sampled tree RSS; shared480%/SCHED_IDLE/nice19 respected.
All owned build scopes drain; predecessor581 APK/snapshot and frozen V8 unchanged.
Host-only quality95/100; no measured pacing improvement or phone acceptance yet.

The fresh phone owner is01a1345b-e8d3-75e2-aa5a-4dc6f75e92ae (Wegener), output
build/codex-android-paint-profile/view-phone. It has a bounded single-ON
ACTION_VIEW profiling grant for the INSTALLED581 baseline, not this candidate.
Do not install over its run or duplicate phone access; poll its tool handle
and review actual fixture PNGs/stage evidence before the next optimization.

## Diagnostics phone run: valid baseline, invalid ON navigation (2026-10-13)

The5818949 diagnostics APK is now installed once with data preserved and its
installed hash verified. The new batched collector passes an actual own-shell
preflight and131 consecutive1Hz samples with0 rejected attempts; maximum device
gap1.21s, host-start gap1.001772s. No cadence guard changed. Two harness binding
failures were preserved: historical prepare-module collision before device
access, then reading uptime inside run-as rather than the outer ADB shell.
The second occurred after install but before app launch; resume verified the
installed bytes without reinstalling. Evidence: paint-profile/phone under build/.

Main visually verifies OFF fixture Bands1/2 through Band24 after20 swipes.
Within10 complete same-phase seconds, process CPU averages53.4%, peaks55%,
RSS250.41-251.40MiB; main SDLThread contributes42.2%. This is not a measured
pacing-fix result or a comparable Chrome/old-collector benchmark.
The ON top/end screenshots instead show Home/New Tab, NOT the fixture; Main
rejects paired profiling, fixture-idle acceptance and stage attribution. The
automated success labels in raw artifacts are superseded by REPORT.md and
analysis.json. Settings navigation also stayed Home, and extra tabs appeared.
Persistent native cmd state after a lost Ctrl release is a source-level
hypothesis, NOT a proven cause. Successful ADB input is not proof of navigation.

App absence, own-marker removal, server/reverse removal, sampler join and both
lock releases are verified; correct tab restoration is not. No product retry
or debugger attachment. Bounded phone evidence quality65/100. A fresh test
agent owns view-phone for a single diagnostic ACTION_VIEW fixture launch with
an HTTP request journal and actual visual check BEFORE swipes; it must not use
the failed Ctrl+L path or claim to fix keyboard navigation. Android only.

## Android fling deadline pacing (2026-10-13)

Main fixes a source-level pacing delay: the event loop previously slept up to
16ms AFTER the preceding fling frame's rendering/presentation and other work.
android_fling_wait_ms now subtracts elapsed time since the last fling step,
rounds a fractional remainder up, preserves earlier event deadlines and leaves
inactive-fling waits unchanged. Late frames do not add another16ms sleep.
This changes Android momentum scheduling, not GPU rasterization or touch speed.

Tests first extract the real Android wait block:9 scenarios initially give
7 expected assertion failures/2 passes; the existing12 fling checks pass and
the updated7 scroll checks give1 expected integration failure. After the fix,
92 focused/adjacent tests pass, plus9 fresh actual-wait-block C executions under
ASan/UBSan. Evidence: build/codex-android-fling-pacing/{red,green}/.
No runtime browser runs in these host checks; simulated8ms frame work now uses
8ms remaining wait instead of16ms. Do NOT claim a measured phone FPS/CPU gain.
The diagnostic5818949 APK predates this fix and remains the phone baseline;
build/install and real-device verification of pacing are still required.
Shared480%/SCHED_IDLE/nice19 remains; source V8 stays frozen. Host-only quality
95/100; overall daily-use readiness remains25/100 with major release gates open.

## Android sampler output batching (2026-10-13)

Main isolates a major observer overhead source without running Lumen. The
phone's own-app and outer shell both resolve printf to /system/bin/printf,
while read/print are builtins. Four alternating40-record comparisons emit the
same10,040 bytes: per-record printf takes median0.977409s versus0.134994s
with one batched printf. All8 outputs match byte-for-byte. This demonstrates
shell-output overhead, NOT faster browser scrolling, lower browser RAM, or
complete attribution of the earlier1.55s cadence failure. Evidence:
`build/codex-android-proc-batch/phone-probe/`.

The earlier16-call probe finds median0.070738s for a shell token,0.121849s for
own-package run-as token,0.151475s for the own-shell collector and0.117388s
for the no-app sampler branch. Startup alone does not explain the~1s samples.
All these probes leave Lumen absent, make no device-state writes and release
both locks; host480%/SCHED_IDLE/nice19 and frozen V8 are retained.

Main changes tools/android/codex_proc_sample.sh to buffer process/thread
records, validate final process presence, then emit once. Failed validation
now publishes no partial stdout; callers still reject any nonzero exit.
Tests first:13 checks give10passes/3expected assertion failures; implementation
passes13/13 under /bin/sh and13/13 under Bash, plus22 error-stdout checks.
Local mksh/dash unavailable; do not claim the actual changed collector has
already passed Android from the fixed-output microbenchmark alone. Host-only
quality95/100; evidence `build/codex-android-proc-batch/green/TOPMAIN-REVIEW.md`.
New collector SHA2564b740464fe0357ff00d0d62872fdb7d2ba13cc25784a3a20ac7657c1b3bbbee5;
test SHA256f1bc3c284c0fe639510b1539976d5c5edcc0f6c92ebd4944cdeec9b61625adda.
Preserve historical collector approval receipts; a new caller must explicitly
authorize these two hashes and retain all old cadence/identity/error guards.
The ready5818949 APK is unchanged: these are external testing tools, not a
new app build. Next use the new observer for debugger-free paint profiling.

## Android paint diagnostics APK verified (2026-10-13)

Implementation commit63e74be has a verified optimized Debug APK:
`build/codex-android-paint-profile/final/codex-agent-android-paint-profile-compact-debug.apk`,
85,240,084 bytes,0444,SHA256
`58189497211d5f77a0c6238fc132123851842540db3b0030e081063ffd77d92c`.
Immutable324-file manifest SHA256
`6384115eb55b01f774b0ff97a6b435bb6cb7a03efe5d970405cab71a7db5d51c`.
Main independently verifies artifact/manifest hashes.219 focused checks,
29 fresh touch/fling/upload sanitizer scenarios,6 JS groups,native cache/V8
runtime and23 APK gates pass. The10 PaintProfile sanitizer executions are
explicitly reused from fresh green evidence, not reported as new executions.
Actual Debug-O2/g/assertions,no-op,payload identity and zero ZIP gaps pass.
Build45.894s,mean197.61% host CPU,peak1743MiB RSS. Shared480%/SCHED_IDLE/nice19
retained; source V8 stays frozen. All51 build/test scopes and controller drain,
258 sampled process identities gone. No build corrections or product failures.
Prior Home6a969dc unstripped main/SDL symbols are preserved read-only under
`final/home6a969dc-symbols/`, with matching BuildIDs and packaged hashes.
Host-only quality95/100. This new APK is NOT yet installed or phone-profiled.

Before another browser run, isolate sampling overhead without launching Lumen.
Main's independent raw-attempt analysis:259 accepted Home-card samples take
mean1.0481s,median1.0574s;217 exceed1s. Previous d086 profile and911-sample
scroll acceptance medians are0.9954s and0.9924s. The raw sampler command is
identical between Home-card and d086. This is close to the cadence budget even
before extra delay, not proof of a Lumen hot path or host starvation. Compare
bounded read-only ADB shell, own-package run-as and own-shell collector timings
while Lumen stays absent; preserve clock domains and host-pressure evidence.
Do not relax cadence guards or claim a new performance improvement. Android only.

## Android paint diagnostics and Home-card phone follow-up (2026-10-13)

The Home-card APK6a969dc is now installed with app data preserved. Main reviews
actual Settings, Settings-to-Home, titled local fixture and fixture-to-Home
PNGs: New Tab/lumen://newtab replaces the old title/address, pages use full
width, and Vulkan2x/native resolution remain correct. Initial Home review
measures0.352% mean CPU,2% peak,227.76MiB RSS over14.22s, NOT formal idle.
Evidence: `build/codex-android-home-card/phone/REPORT.md`.

Acceptance is PARTIAL: during second fixture navigation the device sampling
gap reaches1.55s, exceeding the unchanged1.5s guard. The corresponding host
attempt gap is1.355623s; attribution to host scheduling versus ADB/device delay
is unresolved. No browser crash is established. Subsequent cleanup rejections
compare against the retained last-valid sample, not three additional oversized
inter-attempt gaps. Preserve all259 accepted/4 rejected attempts. App absence,
sampler join, loopback cleanup and both lock releases are verified; continuous
valid1Hz sampling through termination, inactive-card acceptance, final60s idle
and single-Home restoration are not. One new test tab may remain. Four harness
corrections, no fifth/retry; bounded phone verification70/100.

Main adds opt-in paint-stage wall diagnostics, not a speculative optimization.
Tests first:8 expected RED assertions, then13 focused and61 adjacent checks
pass. The same13-test suite also passes with10 actual ASan/UBSan model scenario
runs, including Debug/Release/desktop configuration. One sanitizer harness
failure used nonexistent `/usr/bin/time` before any scenario ran; preserve it,
then reuse the already-built binary with Python wait4/rusage. No product fix or
scenario retry was needed. Evidence: `build/codex-android-paint-profile/red/`
and `green/`. Host checks do not prove phone logging or rendering integration.

`src/app/paint_profile.h` aggregates completed paint calls and six wall stages;
Android output uses SDL_Log at most once per elapsed second during painting.
Opt-in is LUMEN_DEBUG_PAINT=1 or, only in Android Debug, the app-private
paint-profile.enabled file containing exactly one byte1. Disabled stage hooks
do not read the clock; no new wakeups, thread, public URL or settings toggle.
See `docs/codex-android-telemetry.md` for interpretation and cleanup. The
currently installed Home-card APK does NOT contain these newer diagnostics.
Next: build this revision, resolve or isolate the cadence failure without
weakening guards, then collect stage logs plus independent CPU/RSS on the
matched scrolling fixture. Do not reattach LLDB or infer hot paths from the
prior zero-probe run. Broad daily-browser readiness remains25/100/unproven.

## Android Home-card APK and bounded scroll profiling (2026-10-13)

Home-card fix f2a5a2c has a verified optimized Debug APK:
`build/codex-android-home-card/final/codex-agent-android-home-card-compact-debug.apk`,
85,240,084 bytes,0444,SHA256
`6a969dc199c313ad0a6a08052b70daf2f769b612f80886bdfa63f91857a2f488`.
206 focused checks,29 sanitizer scenarios,6 JS groups, native cache/runtime,
23 packaging gates, actual O2/g/assertions, no-op, payload identity and zero
ZIP gaps pass. Build29.655s, mean210.09% host CPU, peak1703.77MiB RSS;
shared480%/SCHED_IDLE/nice19 retained. Verified prebuilt V8 only; source V8
remains frozen. All50 owned scopes and209 sampled processes released.
320-file source manifest SHA256
`79748128066aee1a31d483ead9a2d3c1b49a511f7cab9b2e118026415c0b8d67`.
Main independently hashes the artifact and manifest. Host-only quality95/100;
fresh phone acceptance is separate, not implied by these checks.

Exact installed older d086 scroll reference reproduces47.6138% mean CPU,
peak50%,RSS232.164-232.176MiB over9 complete intervals/9.01s. Main reviews
actual top and endpoint PNGs; the endpoint shows Band24/25, not bottom.
Six planned LLDB probes yield ZERO completed timed probes: an unexpected
SIGSEGV stops SurfaceSyncGrou before the first requested interrupt. The PC
0x680719b4 falls in the saved executable `/memfd:jit-cache` mapping; this is
not a symbolized Lumen frame or a proven root cause. Saved engine stacks show
lifecycle waiting, not busy scrolling. Do not derive optimization targets or
symbol percentages from them, suppress signals, or call this a profile pass.

`build/codex-android-scroll-profile/attempt1/REPORT.md` preserves all evidence:
83 nominal1Hz samples, zero telemetry errors, detach and app-stop verified,
both locks released, debugger/forward/reverse/server removed, pins unchanged.
Single-Home restoration was incomplete: one owned fixture tab remains.
Bounded profiling evidence50/100. A fresh GUI-only owner may close that tab
and verify the new Home-card APK without attaching a debugger. Next native
diagnostic approach: opt-in aggregate paint-stage wall timings with Android
logcat output, paired with the existing independent CPU/RSS sampler. Wall
timing must not be labeled CPU time, GPU execution time or measured FPS.

## Android Home-card destination fix (2026-10-13)

Phone GPU/scroll acceptance exposed a stale Settings address in the current
Home tab card. Internal HTML is generated before navigation replaces the old
tab URL/document. Render the current card from the requested destination and
New Tab title; keep other tabs' titles/URLs escaped and leave navigation state
untouched. Tests first:17 newtab checks yield2 expected assertion failures and
15 passes, zero errors. Fix passes53 focused adjacent checks and19 ASan/UBSan
scenario/platform executions (16 distinct scenarios), with no diagnostics.
Evidence: `build/codex-android-home-card/red/MAIN-REVIEW.md` and
`build/codex-android-home-card/green/MAIN-REVIEW.md`. Host verification quality
95/100; real phone verification of this small UI change remains pending.
The installed d086 GPU/scroll APK does not contain this newer Home-card fix.
Separate native scroll profiling preparation and archived matching d086
symbols are in `build/codex-android-scroll-profile/prepare/`. The completed
bounded run is described above; retain these immutable symbols. Android only.

## Android GPU/scroll phone checkpoint (2026-10-13)

Current Android-only branch: `codex/android-vulkan-momentum-scroll`.
Implementation commit377041a contains portable RGBA uploads, shader channel
correction and Android GPU diagnostics. Final host verification passes202
focused checks,29 ASan/UBSan scenarios,6 JS viewport groups, native cache and
verified-prebuilt V8 runtime,23 APK gates, Debug-O2/assertions, native no-op,
normal/compact payload identity and zero ZIP gaps. Build53.415s, mean198.08%
host CPU, peak1740MiB RSS; shared480% quota/SCHED_IDLE/nice19 retained.
Source-built V8 remains frozen; this build uses verified prebuilt archives.

Installed preserving app data and tested on the MI8/API35 phone:
`build/codex-android-scroll-gpu/rgba-final/codex-agent-android-vulkan-rgba-momentum-compact-debug.apk`
85,240,084 bytes,0444,SHA256
`d0868322b5fa7c4bf8d4afe492deeef56e33da8857155c9dff772e705e9d36fa`.
The APK was built before377041a from the exact same320 pinned source files.
Manifest SHA256a99c2ceb7b668fe21dfe741dd238c30f054ef4188d8b92b2b7df503101a398bf.

Phone attempt3 verifies actual OpenGL rendering with correct colors and no
former GL format error, then Vulkan -> Software -> Vulkan -> OpenGL -> Vulkan
without blank output or crashes. Main independently reviews actual screenshots.
Rotation, background/foreground, persisted Vulkan2x after relaunch and3x fit
pass. OpenGL logs CPU-copy PBO performance warnings; Vulkan logs bounded4x4
format56/59 allocation diagnostics and missing SURFACE_VIEW_FORMATS. Retain
these limitations: GPU composition works, not full GPU rasterization or web
WebGPU conformance. Do not describe these as error-free driver logs.

Matched30-band fixture,20 upward450ms swipes: mean48.2222% CPU, peak49%,
RSS232.99-233.06MiB across9 complete1Hz intervals. Old unoptimized APK mean
105.6667%:54.36% lower CPU for the complete change bundle, NOT isolated GPU
benefit, measured FPS, Chrome parity or reduced RAM. Main SDL thread37.11%.
End screenshot reaches Band23/24, not the document boundary. Timed screenshots
after input completion visibly show continued momentum and subsequent settling;
serial capture latency0.455-0.519s prevents any frame-rate claim.
Formal post-fling idle60.85s: mean0.08217%,peak1.02041%,RSS233.07MiB.
Formal final Home idle60.64s: mean0.065963%,peak1.02041%,RSS240.70MiB.
Both have60 intervals, valid cadence and no intervals above3%.

Phone evidence: `build/codex-android-scroll-gpu/phone/attempt3/MAIN-REVIEW.md`
and `continuation2/` underneath that directory. Original77-sample segment
stopped on a malformed harness state command; continuation records911 samples.
Zero telemetry errors in either segment; never claim continuous sampling across
the stopped gap. Two harness corrections retained (path binding and missing
command name); no product retry. Final full logs and cleanup are preserved.
Original single Home restored, Vulkan2x, density/rotation unchanged, app stopped
and absence verified, sampler joined, server/reverse removed, locks released.
All source/helper/frozen-V8 pins match at release. Phone owner is offline only.

Next: fix the visually observed stale current-tab URL in the Home card (shows
the prior Settings address); profile remaining scroll CPU before further
optimization. Current update quality90/100: measured improvement and lifecycle
verification, but no frame-time benchmark and residual UI defect. Broad daily-use
readiness remains unproven; public-site/API compatibility and Chrome CPU/RAM
comparison are still required. Historical pending notes below are superseded
only for the explicitly verified GPU/scroll/scale scope above.

## Android GL texture-upload fix (2026-10-13)

8fd13e3 window recreation passes194 focused checks,24 sanitizer scenarios and23
APK gates. Phone attempt2 verifies Software -> Vulkan twice, then OpenGL ES
shows a blank white app despite active wgpu/gl; process stays alive. No scroll
or formal idle acceptance yet. Attempt2 quiet-review mean0.165% CPU is not a
formal idle result. Last persisted renderer is OpenGL ES; app is stopped.

Main adds Android GPU errors and native wgpu warnings to logcat. Logging-only
build2 passes196 focused checks and23 APK gates, but phone gl-log still shows
blank output and no GPU errors. Driver diagnostics also require the instance
Validation flag; enable it only without NDEBUG. Three diagnostics checks and
eight lifecycle checks plus23 APK gates pass in gl-log-build3. Phone
gl-validation now captures two GLES API errors: pixel buffer format is not
compatible with level format. Both diagnostic phones stopped and released
ownership; no app-data reset. All runs use continuous1Hz process/thread/RSS
telemetry, shared480% host quota and SCHED_IDLE/nice19. Source V8 remains frozen.

Pinned wgpu-hal29.0.3 maps BGRA8 upload textures to internal RGBA8 with external
BGRA. The GLES EXT_texture_format_BGRA8888 valid combinations do not include
that pair: https://registry.khronos.org/OpenGL/extensions/EXT/EXT_texture_format_BGRA8888.txt
Main switches page and RGBA-video upload storage to portable RGBA8 and swaps
red/blue in the shared fragment shader, preserving alpha. CPU buffers, upload
size, caching and YUV-plane path stay unchanged; no CPU conversion/allocation.
Tests first: new upload model5 yields3 expected failures (page/video/shader),
2 existing cache/resize passes, zero errors. Fix GREEN/build/phone pending.
This is GPU composition, not GPU page rasterization or the WebGPU web API.

Evidence: `build/codex-android-scroll-gpu/phone/attempt2/REPORT.md`,
`build/codex-android-scroll-gpu/gl-log-build2/MAIN-REVIEW.md`,
`build/codex-android-scroll-gpu/gl-log-build3/MAIN-REVIEW.md`,
`build/codex-android-scroll-gpu/phone/gl-validation/quiet-full-log.json`,
`build/codex-android-scroll-gpu/gl-upload-red/red-result.json`.
Phone attempt3 preparation owns no device until exact new APK grant. Next:
verify OpenGL output/errors and full backend cycle, then matched scrolling,
momentum, rotation/scale/persistence and formal fixture/Home idle. Restore
original Home/Vulkan/2x; never call blank-rendering or unmeasured FPS a pass.
Keep outer bash alive around tools/codex_compile.py (trailing status/exit),
otherwise host ananicy can race the exec-reused shell PID to nice-4. No host
priority rule changes or weakening of the guard. Only one genuine pre-device
correction was needed for gl-validation; preserve its failed preflight evidence.

## Android renderer-window follow-up (2026-10-13)

3251476 passes194 focused checks,24 sanitizer scenarios and23 APK gates; native
Debug-O2/-g/assertions verified. Build55.376s/336.53% CPU/1725.92MiB peak RSS.
Phone candidatebc2c3e4 confirms actual wgpu/vulkan in Settings and own-PID log,
then switches successfully to Software. Software -> Vulkan aborts with signal6:
native_window_api_connect reports already connected. No scroll acceptance yet.
Read-only diagnostic did not recover a backtrace; exact aborting call unknown.
Pinned SDL source shows its software framebuffer uses an EGL surface owned by
the SDL window, not by the temporary window surface/renderer. Destroying only
SDL_DestroyWindowSurface leaves that native connection. Main now destroys and
recreates the SDL window before selecting a different graphics API, refreshing
dialog target/viewport and invalidating paint/video caches. Two lifecycle model
assertions fail before this fix. GREEN/build/phone re-verification pending.
Previous phone nonformal48.08s review CPU0.187% mean/2% peak; no scroll/formal
idle pass. App stopped, rotation/density preserved, locks released. Preference
may remain Software until the next verified UI restoration; no hidden data edit.
Evidence: `build/codex-android-scroll-gpu/phone/attempt1/REPORT.md`,
`build/codex-android-scroll-gpu/crash-diagnostic/MAIN-REVIEW.md`,
`build/codex-android-scroll-gpu/window-red/completion-audit.json`.

## Android Vulkan and momentum work (2026-10-13)

Branch `codex/android-vulkan-momentum-scroll`, based on6bbf0d6. Tests first:
standalone fling RED10/12 assertions, then12/12 GREEN plus12 ASan/UBSan;
renderer selection RED6 expected failures; root-scroll integration RED5 failures
and1 existing nested-scroll pass. New explicit wgpu backend API preserves the
desktop wrapper, defaults Android to Vulkan, and adds persisted Auto/Vulkan/
OpenGL ES/Software selection. Settings reports requested versus actual backend
and software fallback honestly: GPU composition is not GPU page rasterization.
Momentum uses event timestamps, bounded exponential decay and physical-pixel
deltas; cancels on new touch/navigation/resize/background. Boundary scrolling
must stop without repainting. Focused93 checks pass after correcting a missing
fling object in the touch-input test link. Physical-grid RED fails before root
position quantization; runtime renderer-switch RED fails before implementation.
Main also found Android Debug native compile commands had no optimization flag.
New CMake policy enables-O2 for Android Debug base/net/engine/app/SDL targets,
retaining symbols and assertions. Set LUMEN_ANDROID_OPTIMIZED_DEBUG=OFF for
unoptimized stepping. Desktop and Release flags remain unchanged. Five policy
tests precede implementation:2 expected failures,3 passes. Full build and phone
acceptance still pending; earlier phone APK is unoptimized.

Previous pixel-scale phone run is PARTIAL, report
`build/codex-android-pixel-scale/phone/REPORT.md`: installed candidate83e5987,
1/1.5/2/3x visual checks and exact toolbar ratio pass,1.5x persists, rotation
readouts refresh,3x fits360 layout pixels. Formal60.42s Settings idle mean0.199%
CPU, RSS227.34–227.52MiB.20 repeated450ms swipes yield105.67% mean CPU,
peak107%, main SDL thread98.78%; no FPS/smooth-scroll pass. Harness incorrectly
classified preceding active-scroll samples as idle when changing stages and
aborted; this is not evidence of high post-scroll idle.350 samples, no telemetry
failures. Backend log inconclusive. App stopped, density/rotation preserved,
server/reverse removed, locks released; test fixture tab cleanup and final Home
idle incomplete. Next authorized phone run must correct that stage guard and
restore original New Tab. Do not claim full acceptance or daily-use readiness.

## Android physical-pixel scale (2026-10-13)

Android now uses an explicit physical-pixel scale independent of SDL/Android
display density. Default2x means one layout pixel occupies2x2 surface pixels;
1x is1:1, not supersampling. Settings offers1x/1.5x/2x/2.5x/3x and saves the
selection. Strict parsing rejects malformed/out-of-range values. Screen mode,
actual app framebuffer and pre-scale layout dimensions are shown separately;
the render buffer stays at native surface resolution. Readouts refresh on paint,
including rotation/keyboard resize, without a polling timer. Cached Settings
tabs refresh selected buttons without repeated DOM mutations. Settings actions
now require immutable generated-page provenance, not the editable address text.
Desktop coordinate behavior remains unchanged. SDL contracts checked against
the pinned source and https://wiki.libsdl.org/SDL3/SDL_DisplayMode and
https://wiki.libsdl.org/SDL3/SDL_GetWindowSizeInPixels .

Tests first: initial33 cases yield15 expected assertion failures, zero setup
errors (`build/codex-android-pixel-scale/red/REPORT.md`). Partial hostGREEN161
passes before main finds stale selection on cached Settings tabs; separate
selection RED confirms it before the fix. Final GREEN passes162 focused cases,
12 touch sanitizer scenarios,6 JS viewport groups, native cache sanitizers and
actual prebuilt-host-V8 runtime, plus23 unchanged APK gates. Normal/fresh compact
payloads match, zero ZIP gaps, native no-op passes. Build24.112s, mean229.76% CPU,
peak sampled RSS1724.64MiB; runtime0.262s/51.28%/68.16MiB (startup, not idle).
Shared480%/SCHED_IDLE/nice19,304 source pins, frozen source-V8 and7 hashes intact;
41 owned scopes drained. Report: `build/codex-android-pixel-scale/final/REPORT.md`.
APK: `build/codex-android-pixel-scale/final/codex-agent-android-physical-pixel-scale-compact-debug.apk`,
87,484,692 bytes,0444, SHA256
`83e59871d75d4211c653d818a61263b84b363c11cee59051da323f143de3c265`.
Host/build score95/100; phone acceptance still pending. No broad browser pass.

New user report: Android scrolling is very slow; requests default Vulkan/wgpu
acceleration and backend switching in Settings. Source already requests wgpu
Vulkan on Android, but rasterizes page pixels on CPU before GPU composition.
Actual active phone backend remains unconfirmed. Touch currently has no fling
momentum. Next phone run should capture own-app startup backend evidence and a
controlled scrolling baseline after pixel-scale acceptance. Android wgpu build
metadata includes vulkan,gles,wgsl. No renderer/scroll production changes yet.

## Android New Tab checkpoint (2026-10-13)

Android web content now uses the full available surface width. Workspace icon
cards and open tabs live in the default New Tab Home, including landscape;
new tabs do not automatically open the keyboard. Main implementation is5996691.
Follow-up fixes center the icon/badge and prevent the invisible sidebar CPU HUD
from requesting full repaints. Tests preceded both fixes. Final verification:
153 focused checks,6 viewport JS groups, native sanitizer/runtime checks and23
APK gates pass. Build26.108s, mean227.91% host CPU, sampled peak1725.81MiB RSS;
shared480%/SCHED_IDLE/nice19, source pins and frozen source-V8 remain intact.
Report: `build/codex-android-newtab/final/REPORT.md`.

Installed, data-preserving candidate:
`build/codex-android-newtab/final/codex-agent-android-newtab-fullwidth-compact-debug.apk`,
87,484,692 bytes; SHA256
`5d708cfbd115048c477a8682825235c0029d80057ce023c36ec7e8e992dea2db`.
Fresh phone acceptance passes default Home, icon containment, workspace add/switch,
native plus, rotation, edge-to-edge fixture and direct scrolling. Formal idle:
60.68s, mean0.198% CPU, peak5.56%, longest above3% streak1, RSS226–228MiB.
215 continuous samples have zero failures; cadence0.98–1.32s. Original59.72s
duration-only failure is preserved, with one bounded harness correction.
Prior bad-badge/high-CPU runs and inconclusive old-candidate profiling are
preserved, not retroactively called passes. Phone is1080x2248 with existing320dpi
override (~540CSS portrait), not a320CSS physical-phone test. Test-created tabs
and workspace removed, original New Tab restored, app stopped, rotation/density
preserved, owned server/reverse removed and locks released. Report:
`build/codex-android-newtab/acceptance/REPORT.md`.

Bounded phone score90/100: a fixture paragraph appears after the subsequent END
heading in the scrolled image; this flow anomaly remains undiagnosed and is not
a layout pass. No public-site/account, typing-CPU or broad daily-use pass.
Host/build score95/100; overall daily-use readiness remains25/100.

Next user request: use a physical-pixel-based Android scale independent of system
density, default2x, configurable in Settings, with screen and render resolution
readouts. Proposed interpretation:1x maps one layout pixel to one physical pixel;
2x maps it to2x2 physical pixels, without supersampling. Clarification requested.

## Android/Linux checkpoint (2026-10-12)

### Current viewport event iteration

Main implements trusted window/VisualViewport resize notifications and viewport-
dependent matchMedia change notifications. A per-context native size/DPR snapshot
coalesces changes in the existing JS tick, after timers and before animation
frames; unchanged ticks add no JS callback, allocation or polling timer. Changed
viewports synchronize style/layout before dispatch. VisualViewport now inherits
EventTarget, exposes live CSS geometry/page offsets and rejects construction.
Media notification snapshots creation order and prunes dead weak references.
Reference: https://drafts.csswg.org/cssom-view/#resizing-viewports . This is not
full CSSOM conformance: separate visual/layout viewport zoom, pinch, visual
scroll/scrollend production, color-scheme-only notification and listener lifetime
across weak-reference collection still need work. No WPT or major-site pass is
claimed.

Tests precede implementation. Fresh RED records6 JS assertion failures, the
native cache rotation failure and an actual prebuilt-host-V8 VisualViewport
failure. An initial Node priority failure is preserved: host ananicy assigns
nice16. A pinned, byte-identical local Node alias avoids that rule while keeping
shared480%/SCHED_IDLE/nice19; no global policy change. The real engine RED abort
takes0.195s, mean52.94% CPU, peak RSS69.56 MiB. Report:
`build/codex-android-viewport-events/red/REPORT.md`.

Fresh GREEN passes6 JS groups, ASan/UBSan cache state, actual V8 runtime,
140 prior focused regressions,12 gesture sanitizer scenarios and23 APK gates.
Runtime assertions take0.137s, mean78.00% CPU, peak RSS70.57 MiB. This is startup
work, not idle evidence. Android assembly takes28.210s, mean241.52% scope CPU,
sampled peak RSS1766.89 MiB. Four JS objects rebuilt,290 reused; native no-op,
SDK/DEX, normal-to-compact payload identity and zero ZIP gaps pass. Only
libmain.so differs from the touch candidate. Warnings remain. All39 owned scopes
and descendants are gone;300 source pins and frozen V8/seven inputs remain
unchanged at verifier completion. Main independently checks APK/test hashes and
accepts95/100 bounded host/build quality; overall daily-use readiness stays25/100.
Report: `build/codex-android-viewport-events/green/REPORT.md`.
APK: `build/codex-android-viewport-events/green/codex-android-viewport-events-compact-debug.apk`,
87,468,308 bytes,0444, SHA256
`b9ecefe00702597ddf42e471a6ac949166c446c43328017f6e98216a18722098`.
No phone runtime or installation of this candidate yet. Host runtime uses
V8 15.6.75.8; Android uses prebuilt15.0.245.2, so device testing is still required.

User now explicitly authorizes closing Lumen, install-r with data preserved and
phone tests. User also requests a new narrow-phone UI: move workspace controls
into the default New Tab area and give web content full screen width. Implement
that test-first next, then use a fresh exclusive phone verifier for the combined
candidate. Capture pidof stdout/stderr/returncode separately, not the old merged
preflight check. Preserve previous runs; maintain1Hz process/thread CPU andRSS
through GUI work/cleanup, restore device settings, and do not access accounts.
No phone agent currently owns a grant or lock.

### Current Android touch iteration

The preceding turn made concrete progress: the sampler fix and9 host tests are
committed in `32da8d4`, with honest preflight limitations in `57a7015`. Phone
takeover permission remains unanswered; no agent has current phone authorization.
Host development continues rather than treating that as a project-wide blocker.

Main adds Android single-finger drag scrolling using the existing page/nested
scroll path, rather than a separate renderer. Tests precede production changes:
12 compiled gesture scenarios fail against a no-op API, plus7 missing wiring
assertions across3 methods. No setup errors; RED evidence:
`build/codex-android-touch/red/REPORT.md`. Seven extracted app-adapter model
scenarios were also written before implementation but not separately run RED.
The implementation recognizes an8 CSS-unit drag threshold, defers taps until
release, suppresses duplicate SDL touch-generated mouse events and leaves real
mouse input intact. It maps normalized fingers to density-independent viewport
coordinates and cancels gestures on focus/background/resize/key/context changes.
Multi-touch suppresses taps/scroll rather than misfiring; it is not pinch support.
Tracking is bounded, with no per-event allocation or polling timer. Direct drag
does not yet provide fling inertia, long-press selection, DOM touch events or
touch-action arbitration. Native nested-scroll target capture also needs broader
site testing. These are remaining daily-use requirements, not declared supported.
The first verifier passes140 unique focused groups and12 ASan/UBSan gesture
scenarios, but assembly stops before native compilation because inherited
`ANDROID_SDK_ROOT=/home/a/Android/Sdk` conflicts with pinned `ANDROID_HOME`.
Report: `build/codex-android-touch/green/REPORT.md`, explicitly PARTIAL. Failed
configuration takes20.149s at mean232.65% CPU and peak sampled RSS539.38 MiB;
480%/SCHED_IDLE/nice19 and frozen V8 remain intact. Main then only renames new
coordinate locals/parameters/result fields descriptively, without changing the
gesture behavior. A fresh verifier under `build/codex-android-touch/final` reruns
the final bytes with `ANDROID_SDK_ROOT` explicitly matched to pinned
`ANDROID_HOME` after sourcing the environment outside the compile wrapper.
Final verification passes140/140 unique focused checks,12/12 sanitizer scenarios
and23/23 unchanged packaging gates. Implementation/tests are committed in
`0ada96f`. Normal assembly takes48.031s at mean213.82%
CPU, sampled peak RSS1759.38 MiB. One new touch object and rebuilt main object;
292 other objects reused. Normal/fresh compact live payloads match with zero
ZIP gaps; only libmain.so changes. Actual SDL/SDK compilation and native no-op
checks pass. Shared480%/SCHED_IDLE/nice19 and frozen V8 remain unchanged.
Candidate: `build/codex-android-touch/final/codex-android-touch-compact-debug.apk`,
87,468,308 bytes,0444, SHA256
`d30911ad5f92cd45e8d2c3a99f3984b3a21a1bb67fe828b1862f515a9f3d3991`.
Report: `build/codex-android-touch/final/REPORT.md`. One report-only correction
records allowed concurrent documentation drift; warnings remain. Main verifies
APK/source/test hashes and accepts95/100 bounded build quality, not daily-use
readiness. No phone runtime or CPU claim for the new adapter yet; it is not
installed. The previous installed APK remains the focus candidate. Once device
permission is clarified, use a fresh verifier that first records separate pidof
returncode/stdout/stderr, then tests this candidate with the corrected collector:
real taps, direct swipes without accidental link clicks, nested scrolling,
resize/IME/background regression and1Hz CPU/RSS. Do not overwrite historical
failed runs or claim the new sampler has already been verified on Android.
The original failed report finalizer remains preserved. Its corrected guarded
run is `finalize-report-correction1.result.json` (passed, payload/wrapper exit0,
7 policy samples, no issues); `final-audit.json` records the allowed docs hashes.
Guard completion drains owned descendants. Main checks this evidence before
closing the verifier; no post-commit rerun is claimed.
Overall daily-use readiness remains25/100 pending device and major-site evidence.

### Previous density/focus iteration

**Android density/input and focus-highlight fixes committed; phone verification partial.**
The preceding viewport iteration was concrete progress: committed engine fixes,
host/build passes and protected API35 resize/IME phone evidence, not a status-only
turn. This iteration keeps Android as the priority; Linux development is deferred.
Tests were added before production edits. Density RED executes six extracted
`update_size` model failures, three missing-input-adapter prerequisites and four
static-wiring failures; both Android/desktop variants compile without errors.
The desktop/invalid-scale failures concern the new input-scale contract and do
not establish a preexisting desktop rendering regression. Responsive RED first
failed setup because the new fixture omitted HTML sources. Main added the HTML
sources; a fresh verifier then executes two initial portrait-style failures and
one wide-style pass using the actual CSS/DOM/HTML engine. The failed rotation
scenario stops at its portrait prerequisite, not a separately exercised rotation.
Evidence: `build/codex-android-density/red/REPORT.md` and
`build/codex-android-density/red-responsive/REPORT.md`. Test compiler errors now
print diagnostics, with bounded compilation timeouts.

The patch derives Android CSS/UI units from SDL display scale while retaining
the physical framebuffer size, maps pointer positions and relative motion into
those units, preserves wheel ticks, and refreshes metrics on scale-change events.
Desktop SDL logical coordinates remain unchanged. Canvas/icon caches include
scale; page and iframe style environments receive the presentation DPR. Internal
pages stack navigation above content below600 CSS pixels, retaining the wide
sidebar layout above that threshold. Existing native toolbar/workspace sizes are
not redesigned, nor are touch scrolling or JavaScript resize notifications added.
SDL contract checked against the pinned local source and official references:
https://wiki.libsdl.org/SDL3/SDL_GetWindowDisplayScale
https://wiki.libsdl.org/SDL3/SDL_MouseMotionEvent
Commit `a640356` passes16 new groups and the previous93 focused checks:109/109,
plus23/23 unchanged packaging gates. Only `main.c.o` rebuilt,292 objects reused;
normal assembly29.395 seconds, mean218.13% CPU, sampled peak RSS1785.80 MiB.
Shared480%/SCHED_IDLE/nice19 held. Normal assembly then fresh packaging yields
identical live payloads and zero ZIP gaps; only libmain.so changes. Candidate:
`build/codex-android-density/green/codex-android-density-compact-debug.apk`,
87,468,308 bytes, mode0444, SHA256
`f98c99bc16284dcbd47bcb4e84fe357cbdd707e5191a4551cc43c9f3fc02af9e`.
Report: `build/codex-android-density/green/REPORT.md`. One report-only harness
correction, no build/test retry; compiler/SDK warnings retained. Main independently
verified APK and five production/test hashes. Original source V8 remains frozen;
the Android build continues using verified prebuilt V8. Build score95/100.
The fresh GUI verifier received explicit current-candidate install/exclusive
authorization after GREEN. Candidatef98c99bc is installed on the API35 phone.
Initial main-reviewed portrait screenshots show readable enlarged controls,
horizontal three-link internal navigation and contained wrapping card content.
Actual effective device density is320dpi (scale2), not the model's example2.75.
Fresh60.04-second home idle passes at mean2.015% CPU, range0–6.12%, longest
above3% streak1, RSS222.75 MiB. Real History/Bookmarks taps select the expected
content; address tap opens the IME and literal `lumen://newtab` is visible before
submission. **Phone run1 fails a real visual gate after address submission:**
the entire SDL surface remains gray-tinted, while system bars remain untinted.
Main reviewed submission and a second screenshot67.22 seconds later with no
Back, touch, reload or rotation masking it. IME is hidden, own activity focused,
resumed/unlocked and unobscured. Normal GUI testing stops here; current-candidate
rotation/resume/final idle remain untested. Failure cleanup force-stops the app,
restores free rotation and releases the phone; full final UI restoration was not
accepted. Evidence: `build/codex-android-density/phone/run1/cleanup-final.json`.
Main's source-level diagnosis is the platform default focus highlight: SDL's
TEXTEDIT_HIDE returns focus to its SurfaceView, and the captured hierarchy shows
focused BrowserSurface in non-touch mode. Android15 draws a default highlight
over such views unless opted out. This matches the failure but has not yet been
confirmed by a corrected phone run. Official implementation:
https://raw.githubusercontent.com/aosp-mirror/platform_frameworks_base/android-15.0.0_r1/core/java/android/view/View.java
Final failed-run report: `build/codex-android-density/phone/REPORT.md`;
556 continuous samples through cleanup, host policy/source pins/frozen V8 all
preserved. Logs retain20 allocator errors,20 AHardwareBuffer errors and one
IME-hide timeout. Bounded partial GUI score55/100, not an overall pass.
New BrowserSurface model assertions precede the adapter opt-out. Fresh RED
compiles Java and fails the constructor focus-highlight assertion; the later
resize assertion is not reached. Report: `build/codex-android-focus/red/REPORT.md`.
Main adds `setDefaultFocusHighlightEnabled(false)` in BrowserSurface's constructor,
without changing keyboard focus, SDL listeners or touch mode. Commit `1d2075a`
passes109 unique focused groups plus23 unchanged packaging gates. All293 native
objects are reused; only classes3.dex changes. SDK36 compile and DEX evidence
verify the actual Android API call. Normal assembly takes18.611 seconds at mean
275.15% CPU, sampled peak RSS1027.88 MiB; shared480%/SCHED_IDLE/nice19 holds.
Report: `build/codex-android-focus/green/REPORT.md`; zero harness corrections,
build score95/100, frozen V8 and allseven hashes unchanged. New candidate:
`build/codex-android-focus/green/codex-android-focus-compact-debug.apk`,
87,468,308 bytes, mode0444, SHA256
`a06cbad1142051177fea1da649bb5eae9b95256039a40a382e2165595be79612`.
Main independently verifies APK/four source pins and grants the fresh GUI agent
exclusive phone testing. Its host-only preparation passes34 checks without
corrections. Corrected candidate is installed on the API35 phone. Main reviews
initial, protected post-submit and rotated-existing-IME screenshots: all pass.
After hardware Enter and more than10 seconds without touch/reload, the surface
has no gray tint, retains keyboard focus and remains in non-touch mode. This
confirms the focus-highlight correction in the original failing context.
Fresh61-second home idle averages2.443% CPU, range0–6.122%, RSS223.156 MiB,
longest above3% streak1. Closed-IME rotation and rotation of an already-visible
keyboard preserve readable responsive layout and a single keyboard inset.
**The full phone run is incomplete, not GREEN:** after same-process resume,
the sampler loses one transient thread's `/proc/1452/task/5203/stat` during
`resumed-history`. The legacy strict `cat /proc/$process/task/*/stat` collector
treats normal thread exit as failure. Main inspected the collector and failure
evidence; this is a monitoring race, not a demonstrated browser failure.
The pre-cleanup image shows normal History selection, closed keyboard and no
tint, but final navigation/20-second idle/restoration gates are not completed.
Cleanup confirms stopped/released/free rotation; full UI restoration remains
unverified. Evidence: `build/codex-android-focus/phone/run1/FAILURE.json` and
`build/codex-android-focus/phone/run1/cleanup-final.json`.
Final partial report: `build/codex-android-focus/phone/REPORT.md`, score65/100
for verification completion, not production readiness. It preserves507 samples,
a2.867-second successful-sample gap and a second expected-stop label mismatch
(`App disappeared during cleanup`); neither error is erased or called a pass.
The fresh tracked collector `tools/android/codex_proc_sample.sh` now passes9
host fixture tests. Main wrote tests before seeding the actual legacy cat
behavior: RED has6 behavioral assertion failures across4 methods, no setup
errors. The builtin-read fix passes all9 without test changes. It skips vanished
task directories, not arbitrary read failures, and checks process presence again
after collection. This is directory-lifetime detection, not numeric errno
classification; PID/start/cadence checks remain the caller's responsibility.
Reports: `build/codex-android-proc-sample/{red,green}/REPORT.md`; policy/frozen V8
checks pass, zero corrections. Scope/usage: `docs/codex-android-telemetry.md`.
A fresh GUI verifier passes49 host checks including33 test groups with this
collector and correct intentional-stop labeling. One fixture-only quoting
correction is retained; production collector and sampler command are unchanged.
Main rehashes APK/collector/tests, reviews preparation and grants exclusive phone
access for the full current-candidate protocol. No new APK build is necessary;
the installed production bytes do not change. **The fresh run stops at preflight**:
`App already running; no takeover`. It performs no install, launch, sampler or
GUI action. The old guard merges pidof stdout/stderr and failed to save the raw
response, so a live PID versus an ADB diagnostic cannot be established from this
evidence. The controller exits1 and releases host locks; it does not force-stop
or otherwise alter any externally running app. Current phone stopped/released
state is unverified, distinct from historical successful cleanup. Report:
`build/codex-android-proc-sample/phone/PREFLIGHT_REPORT.json`.
Main asks whether Lumen may be closed for a fresh phone test; no reply yet.
Next verifier must capture separate pidof returncode/stdout/stderr before
classifying device state, preserve this run, and obtain a fresh exclusive grant.
The new collector is host-verified only; live Android integration remains untested.
Historical helpers and failed run remain intact. No further phone access
is authorized to the completed verifier. Overall daily-use readiness remains25/100;
public sites, Chrome memory comparison and major graphics/media APIs are not
validated by these internal-page checks.
The preceding density host-only preparation passed38 checks after two corrections;
an initial policy5/nice-4 failure remains unexplained, not claimed fixed. Strict
runtime priority checks remain required; no global host policy was changed.

### Previous viewport checkpoint

**Viewport-style fix committed and verified on the API35 phone.**
Eight new actual-CSS-engine regression scenarios compile and fail behaviorally
before production changes: viewport units, media activation, nested queries,
inheritance/pseudo styles, stable viewport caching, independent documents,
resolution/color scheme and orientation. RED evidence:
`build/codex-android-viewport/red/REPORT.md` (8 failures, no test errors).
Two resource/late-sheet cases and three app-wiring assertions were added before
implementation but were not part of that executed RED run.
Commit `ae9976d` retains `@media` rule conditions and evaluates them once per
style environment change or index rebuild. Computed styles invalidate on
viewport/DPR/color-scheme changes; unchanged clean documents avoid restyling
and stylesheets are not reparsed on resize. Native page render, synchronous
style reads and iframe layout update their style viewport before layout.
Conditional font-face/import/property resource behavior remains parse-time;
this patch does not claim complete conditional-resource support, JavaScript
resize/matchMedia notifications, density-aware UI or public-site parity.
GREEN passes93 focused groups (new13 plus prior80) and23 unchanged packaging
gates. Actual Android header rebuild:17 rebuilt, five byte-changed,276 reused.
Compact candidate87,468,308 bytes, mode0444:
`build/codex-android-viewport/green/codex-android-viewport-compact-debug.apk`,
SHA256 `7fd5aa1b9f2079500ea82888408b0dcb8be31aef7499613208d223bc04d7b6a2`.
Report/source pins: `build/codex-android-viewport/green/REPORT.md`.
Normal assembly averaged234.87% host CPU, sampled peak RSS1737.07 MiB;
shared480% quota/SCHED_IDLE/nice19 held and original V8 remains frozen with
seven unchanged input hashes. Two build-harness corrections, no production
failure/retry; compiler/SDK warnings are retained. Verified prebuilt V8 is used.
Phone testing completed and released the device. Host-only preparation recorded one unexplained
priority-check failure before models ran; strict subsequent checks passed,
but no cause/fix is claimed. The actual runtime's1328 host-policy samples and
4435 thread observations all retain shared480%/SCHED_IDLE/nice19.

New candidate7fd5aa1b is installed. Evidence under
`build/codex-android-viewport/phone/run1` verifies the previously stale white
navigation column now fills the page after IME dismissal and closed-keyboard
portrait/landscape rotations, without page touches/reloads masking the defect.
Rotation with an already-visible IME also retains correct surface/column
geometry and one keyboard inset. Main reviewed actual protected dismissal and
already-open-IME rotation screenshots. One real Home/background/resume retains
the same process; fresh History/Bookmarks touches verify presentation.
Fresh HUD-On baseline60.01-second idle passes: mean2.45% CPU, range0–8%, max
three consecutive intervals above3%, RSS223.22–225.48 MiB. Final20-second home
idle passes: mean2.40%, range0–6.12%, max one consecutive interval above3%,
RSS233.14 MiB. Continuous process/all-thread telemetry covers330 samples over
329.02 seconds including review/restoration/cleanup; max host gap1.138 seconds.
Logs retain60 GraphicBufferAllocator and60 AHardwareBuffer errors, undiagnosed;
no matched fatal pattern does not mean error-free graphics. No site/Chrome
benchmark, public-site parity, media-query phone fixture or complete web-API
acceptance is claimed. Conditional media behavior is covered by actual-engine
host tests; phone visuals establish this home-page viewport regression only.
Phone restored to Bookmarks/HUD On, portrait, keyboard closed, rotation `free`,
then force-stopped and released; source pins and frozen V8 remain unchanged.
Final phone report: `build/codex-android-viewport/phone/REPORT.md`.
Main-reviewed scores: build/packaging95/100, bounded viewport GUI90/100;
overall Android daily-use readiness remains25/100, not a compatibility percentage.
Phone harness corrections: one host-preparation diagnostic, zero runtime;
the unknown preparation priority failure remains unresolved.
Next: density-aware mobile controls and responsive internal pages, plus native
JavaScript resize/matchMedia notification coverage. Missing real graphics and
device web APIs and unmeasured Chrome comparisons still block daily readiness.

### Previous safe-area checkpoint

**Android safe-area adapter committed and verified in bounded API35 phone tests.**
`BrowserInsets` pads SDL's shared surface/input parent for system bars, display
cutouts and the visible IME. It subtracts handled geometry from child insets
while preserving IME visibility, avoids cumulative padding, and explicitly
uses edge-to-edge layout on API30+ to prevent duplicate framework resizing.
API28/29 retain guarded legacy handling. Native engine/density/CSS viewport
invalidation are unchanged; model tests do not prove real keyboard behavior.
Tests-first RED was missing-class setup and Activity wiring, not seven executed
behavior failures. Two more legacy scenarios were added before implementation.
GREEN passes nine Java model scenarios plus wiring and the prior70 groups:
80 focused groups and23 packaging gates. SDK36 compilation and APK DEX are
verified, all293 native objects reused, and only classes3.dex changed.
Candidate: `build/codex-android-insets/green/codex-android-insets-compact-debug.apk`,
87,468,308 bytes, mode0444, SHA256
`13c702ca7047f7a0be357cab1ce978d992a132a7d770b3f4a79c4ad76ee177fb`.
Report/source pins: `build/codex-android-insets/green/REPORT.md`.
Normal assembly averaged258.03% host CPU, sampled peak RSS923.60 MiB;
shared480% CPU quota, SCHED_IDLE/nice19 held. Original V8 remains frozen;
verified prebuilt V8 is still used.
Phone run2 verifies the toolbar below the status bar, actual visible keyboard,
and typed `lumen://newtab` in the focused address. Main reviewed both screenshots.
Home idle passes60.01 seconds at2.33% CPU (range0–9%, at most two consecutive
intervals above3%), RSS222.02–224.28 MiB. Evidence lives under
`build/codex-android-insets/phone/run2`. The run stopped on an overstrict harness
assertion: raw mIsInputViewShown remained true after Back, although mInputShown
was false and the surface regained its full height. Android15's public
isInputViewShown uses mDecorViewVisible, not that cached input-view flag:
https://raw.githubusercontent.com/aosp-mirror/platform_frameworks_base/android-15.0.0_r1/core/java/android/inputmethodservice/InputMethodService.java
That run has no dismissal screenshot; the fresh follow-up supplies it. No
production change was made to satisfy the flawed check. API28–34 behavior remains model/SDK-only,
including ordering against SDL's queued legacy window-style changes.
Partial runtime report: `build/codex-android-insets/phone/REPORT.md`.
Run2 retained20 allocator and20 AHardwareBuffer startup errors (not the older
baseline's32 each), significance still undiagnosed. Telemetry covers the whole
successful run at0.754–1.260-second cadence. The earlier missing-namespace
harness run has no live CPU sample and is not represented as a monitored pass.

Commit `656cd64` adds the adapter and tests. Fresh follow-up evidence under
`build/codex-android-insets/followup` verifies real address typing/submission,
reopening and Back dismissal, portrait/landscape safe chrome and keyboard
geometry, and one same-process Home/background/resume with fresh History then
Bookmarks pixels. Main reviewed actual dismissal, landscape keyboard and
resumed History screenshots. Back hides the keyboard but leaves address
editing selected; an actual page touch exits editing. The existing CSS-vh
invalidation bug also leaves the inner white navigation column too short after
keyboard dismissal until page navigation. Neither is hidden as a polished
mobile layout. Rotation while the keyboard is already visible was not tested.
Post-resume home idle passes20.99 seconds, mean2.24% CPU, range0–6.06%, at most
one consecutive interval above3%, RSS233.45 MiB. Prior60-second home acceptance
was not repeated. The follow-up retains56 allocator and56 AHardwareBuffer
errors throughout its captured log; their significance remains undiagnosed.
Final restoration confirms Bookmarks, HUD On, portrait, keyboard closed and
rotation `free`, then force-stop and phone release. Installed APK is13c702ca,
not the historical f313d632 candidate below. No public-site/Chrome comparison,
WebGL/WebGPU, real camera/screen capture or daily-use readiness pass is implied.
Report: `build/codex-android-insets/followup/REPORT.md`. Its320 samples cover
319.02 seconds, maximum host cadence gap1.118 seconds, through cleanup. Two
test-harness corrections (visibility criterion and child-stdin isolation), no
production workaround or retry of a failed CPU gate. Main review scores:
build/packaging95/100, initial partial GUI coverage60/100, narrowed follow-up
90/100, Android safe-area milestone90/100. Overall daily-use remains25/100:
mobile density, viewport-style invalidation and missing real web APIs still
block the requested browser. Next: test-first viewport-dependent style
invalidation for Android resize/IME changes, then density-aware mobile controls.

### Previous verified phone baseline

**GPU lifecycle fix verified in bounded phone tests; IME and graphics APIs block daily use.**
Phone candidate SHA256 `f313d632...` passes HUD-On home idle over61 complete
intervals: mean2.38% CPU, range0–6.25%, RSS232.82 MiB, no sustained excess.
Three real Home/background/resume cycles retain the same process and visibly
repaint History/Bookmarks. SDL surface destroy/create logs pass without GPU
errors. Their20-second settled means are2.15%,2.65%,2.30%, RSS233.34–235.82 MiB.
Evidence: `build/codex-android-gpu-lifecycle/phone/narrow1` and
`build/codex-android-gpu-lifecycle/phone/narrow2-resume`.
The first harness stopped on a90-second agent visual-review timeout, not a
browser crash. Fresh verification completes rotations3/2/0 and a9.01-second
background rotation/resume, retaining process identity and fresh presentation.
Their20-second settled CPU means are2.20%,2.50%,2.35%,2.20%; resume RSS is
237.27 MiB. Reports: `build/codex-android-gpu-lifecycle/phone/REPORT.md` and
`build/codex-android-gpu-lifecycle/rotation-followup/REPORT.md`. Continuous1Hz
monitoring covered all runtime. Previous controller cleanup preceded the fresh
launch; no device overlap. Both controllers are finished and released the phone.
Natural fixed HUD numbers are not stale-frame evidence: actual page touches
now establish presentation freshness. These checks are not raster equivalence.

IME activation FAILED: address tap[450,22] followed by mInputShown=false and
mIsInputViewShown=false. Address focus itself is unverified; dismissal/reopen
were not attempted. Tiny controls and status-bar overlap persist. The inner
white navigation column also retains landscape height after portrait resize
until an interaction repaints it. Next main-agent work: test-first Android
safe-area/input/density integration and viewport-dependent style invalidation;
do not simply change tap coordinates or call the keyboard working.
Raw startup logs contain32 GraphicBufferAllocator4x4 format56/59 allocation
failures and32 corresponding AHardwareBuffer errors. Their significance is
undiagnosed; the inherited crash regex missed these. Do not claim error-free
GPU logs merely because no abandoned-window crash recurred.

The synchronous SDL
background/foreground watch detaches the presentation surface before SDL's
pause wait and recreates it from the current Android native window on resume.
It retains the GPU device, pipelines and cached textures, owns a native-window
reference, rejects incompatible replacement formats and suppresses rendering
while suspended. The prior SIGABRT does not recur in the three tested cycles;
this is not exhaustive device/lifecycle stress coverage. Commit `11e90a6`.

Tests were written first: missing APIs/callbacks produced compiler/assertion
RED, not executed behavioral-model failures. All eight actual-gpu.c mocked
lifecycle scenarios and two callback/wiring groups now pass with UBSan.
GREEN totals70 focused groups and23 packaging gates. Only two native objects
changed,291 reused. Fresh packaging-only state avoids incremental ZIP dead
space; compact candidate is87,468,308 bytes, mode0444:
`build/codex-android-gpu-lifecycle/green/codex-android-gpu-lifecycle-compact-debug.apk`,
SHA256 `f313d6320239574c4322f6382eaf25932c69841fac68054f4dd697455ab7fcb1`.
Build/report/source pins: `build/codex-android-gpu-lifecycle/green/REPORT.md`.
Two packaging-harness corrections, no production fixes by the build agent.
Shared480%/SCHED_IDLE/nice19 held; original V8 remains frozen and unchanged.
The exact owned loopback fixture now executes on the phone:7/11 checks PASS
(DOM, Intl, BigInt, Promise, Wasm, storage, fetch). Actual failures: Canvas2D
pixel mismatch, unavailable WebGL1/2 contexts and unavailable WebGPU. Main
visually reviewed all eleven results and the matching server report. Source
getImageData returns blank pixels, getContext supports only2D, and no
navigator.gpu implementation was found. The Vulkan presenter is not a working
WebGL/WebGPU web API. Camera/mic/screen API presence is not functional proof;
current getUserMedia constructs fake tracks and getDisplayMedia rejects capture.
Geolocation API is absent in this fixture. No public sites or Chrome RAM
comparison ran. Overall daily-use assessment remains25/100, not95/100.

Fixture HUD-On idle PASS:60.05 seconds, mean2.48% CPU, range0–13.27%, at most
one consecutive interval above3%, RSS241.15–245.63 MiB; Activity0%, engine2.21%.
Report: `build/codex-android-engine-smoke/phone/RUNTIME_REPORT.md`.
Independent fixture_no_keyboard scope explicitly preserved the failed IME
result rather than requiring a fake GUI pass. Phone is released, app stopped,
rotation `free`, HUD On; installed APK remains the compact f313d632 artifact.
Only the owned reverse/server were removed. Home restoration did not complete:
Back left the activity; the agent did not relaunch. No full feature pass claimed.

**Previous combined-fix APK passed the bounded home idle gate:**
60 complete seconds with HUD On, mean2.47% CPU, range0–6.93%, at most one
consecutive interval above3%, RSS229.98–232.11 MiB. Activity0.00%, engine2.22%.
At that earlier checkpoint the phone was released, app stopped, HUD On and
rotation `free`; installed SHA256 was the compact `43b721...` artifact below. Main visually
reviewed its actual home screenshot. Report:
`build/codex-android-chrome-damage/phone/REPORT.md`.
No resume retry or fixture/site/interaction acceptance ran at that checkpoint.
The then-known SIGABRT was a daily-use blocker; mobile UI remains tiny and
overlaps the status bar. Overall daily-use self-assessment remains25/100.

Pixel equivalence is **unverified**, not a measured mismatch. The attempted
diagnostic launch used SDL_ENV Intent extras, based on main's mistaken reading;
pinned SDL getManifestEnvironmentVariables reads manifest metadata instead.
No comparison or paint logs appeared, so do not claim the checker executed.
Its fprintf(stderr) output is also not verified as visible in Android logcat.
Next diagnostic work needs debug-only activation and observable logging (or
scoped own-app LLDB), not an arbitrary environment-intent bridge. Do not repeat
ignored extras or treat absent comparisons as zero mismatched pixels.

The Android surface now uses lifecycle-scoped DisplayManager notifications
instead of SDL's unused game-rate accelerometer to report display rotation.
Rotation mappings, deduplication, pause/resume, display replacement, resize,
destroy and non-accelerometer delegation have a Java model test. Its initial
RED was missing implementation (not a behavioral failure); existing phone ART
and CPU measurements motivated the change. The model, actual Android SDK
compilation, 52 total host unit groups and 23 APK packaging gates pass.
All 293 native objects are unchanged in this sensor-only APK:
`build/codex-android-idle-adapter/green/codex-android-idle-adapter-debug.apk`,
SHA256 `9be59756cc14a2b94b840e7b640024bff5f7aee463fd3b210d86f8ab5ede26ca`.
Phone verification now establishes Activity CPU at 0.00% and no sampled
sensor callbacks. HUD-On idle is still FAIL: 4.00% over 60 seconds, engine
3.80%, RSS232.26–234.34 MiB. Reversible HUD On/Off/restored gives
3.30%/0.10%/4.20%; disabling the HUD is diagnostic, not acceptance.
All four requested rotations visibly render, but background/resume FAILS:
SIGABRT after abandoned Vulkan BufferQueue errors. See
`build/codex-android-idle-adapter/phone/REPORT.md`; bounded sensor verification
80/100, not readiness. The agent restored rotation `free`, HUD On, and stopped
the app before releasing the phone. Main visually reviewed the home screenshot.

A separate native HUD regression test extracts the real damage policy and
uses UBSan/float-cast-overflow. RED: five failures, three passes; sentinel
1e30 bounds overflow integer conversion before raster assertions can run.
The fix clamps bounds before conversion and keeps page-cache validity
independent of chrome/HUD changes, while still recomposing changed chrome.
LUMEN_CHECK_DAMAGE now also compares unchanged cached pages with full raster.
All eight cases now pass, together with 60 total host groups, the native build
and 23 packaging gates under `build/codex-android-chrome-damage/green`.
These are policy-model checks, not pixel correctness or runtime acceptance.
Commits: sensor adapter `f9f7820`, native damage fix `5a50ae9`.

Main caught APK growth from 93.1 MB to 168.9 MB despite unchanged live-entry
sizes. The installed AGP incremental ZIP allocator leaves unused zero-filled
records after replacing the native library. Packaging-only fresh output and
incremental state produce identical live payloads in 87,451,924 bytes, with
no unreferenced gaps and all 23 gates passing; no native/Java compilation.
Use only the corrected delivery artifact:
`build/codex-android-chrome-damage/size-correction/codex-android-chrome-damage-compact-debug.apk`,
SHA256 `43b72133e4628a23b5337a4da9c3dd631665df3018594d88c5842c269742fd2f`.
The oversized GREEN archive is historical evidence, not endorsed for delivery.
Exact task-only Gradle command/init script and diagnosis are in the correction
directory's REPORT.md. Future native changes may fragment incremental APKs
again; compare container size with live payloads before delivery.
Fresh phone testing of the combined fixes was limited to HUD-On idle and
an ineffective cached-page/full-raster diagnostic attempt. Resume failure must be fixed
before interaction/site/fixture acceptance; no daily-readiness pass is claimed.

Historical lifecycle investigation (implemented and tested above): the presenter captured ANativeWindow once in
gpu_create. SDL replaces its Android window property on native surfaceCreated,
and releases the old window on surfaceDestroyed. Lumen handles neither
background/foreground presentation resources nor native-window rebinding;
gpu_present_frame instead reconfigures the old WGPUSurface on acquisition
failure. This strongly supports a stale presentation-surface hypothesis, not
a symbolized abort stack. Pinned SDL Android_OnPause blocks event pumping after
queuing lifecycle events and explicitly requires filter/watch handling before
the block. Test lifecycle surface detach/rebind while preserving device/cache,
then real repeated background/resume, rotation and post-resume CPU. Do not
disable Vulkan or replace the existing engine to mask the crash. SDL reference:
https://wiki.libsdl.org/SDL3/README-android and SDL_GetWindowProperties.

### Earlier measured phone baseline

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
Residual diagnostic (`build/codex-android-idle-profile/cycle2/REPORT.md`):
real-GUI HUD on/off/restored yields 8.56% / 5.58% / 8.55% total CPU; with HUD
off the native engine samples 0% while SDLActivity averages 4.80%. Main
visually confirmed HUD removal/restoration. ART sampling captures sensor
dispatch, SDLSurface.onSensorChanged, Display.getRotation and onNativeAccel;
app-scoped sensor history confirms successful 20 ms requests, not measured
delivery frequency. Next fixes to test: avoid full-page repaint for periodic
HUD updates, and replace unused game-rate accelerometer rotation polling with
event-driven display rotation tracking while preserving lifecycle/rotation.
No device-motion/orientation web API is currently wired to that SDL sensor.
Do not simply disable the HUD or lose rotation to pass idle. Phone is free,
HUD restored and app force-stopped; no diagnostic process remains active.
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
