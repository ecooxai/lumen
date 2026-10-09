# Lumen: handoff notes

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
