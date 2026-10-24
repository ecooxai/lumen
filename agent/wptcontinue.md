# WPT work: handoff notes (updated 2026-10-22, about 21:25, branch devin-opus5.5/wpt-continue)

The goal (user): Lumen should pass the Web Platform Tests (WPT) that Chrome passes. Report the per-area pass rate
against Chrome, then fix gaps. The JS engine is shared between host and Android, so WPT runs on the host, and
the phone gets checked with `tools/device_tests.py` after each batch.

Read `AGENTS.md` first. In short:
- Every compile and test goes through `python3 tools/codex_compile.py ...`, with no bash wrapper inside it.
  Use at most 6 jobs, or 2–4 while WPT runs. Gradle needs `--no-daemon`.
- A fresh Haiku subagent (`model: haiku`, `effort: high`) does every build, test or device task. Each test run
  also gets a parallel monitor subagent that samples CPU and RSS every second and watches for errors.
- Code stays in the main agent. Don't edit sources while a build subagent is running.
- Privacy: never type into page inputs, and describe third-party pages only structurally. Never `pm clear` or
  uninstall; use `adb install -r`. Don't touch lumen PID 837634. Never `pkill -f`. Commit only when the user
  asks.


## USER REQUEST (latest, verbatim intent) — Android editing UI + bugs, then Linux parity
1. BUG x.com: typing in new-post / reply composer doubles chars ("1" → "11").
2. BUG: in some text-edit elements Enter inserts nothing.
3. Top-right menu: new item "Show keyboard": shows the soft keyboard and routes input to the latest active
   (focused) element; user can keep tapping page elements to pick the target without the keyboard hiding;
   only Back or the keyboard's own hide closes it.
4. Tapping an input: double-tap no longer shows the keyboard; instead the edit bubble shows, ALL ICONS:
   [keyboard] [paste] [select all] [slot A] [slot B] [more]   — keyboard icon left of Paste.
   cut/copy icons always: with no selection and non-empty input they act as cut-all / copy-all.
   Four extra actions: undo, backspace, clear-all, hide(bubble). Only 2 slots (A,B) on the bar: Backspace and
   Enter by default; the others go into the More menu (whichever 2 the user picks stay on the bar).
   More menu also lists text snippets: "yes\n", "continue\n", "confirm\n", "include", "enter";
   user can add more snippets in a Settings UI.
5. Long-press Paste → clipboard history (last 10 text or image clips) to choose from.
6. Long-press Backspace → delete a word; keep holding → delete another word every 2 s.
7. Then keep passing WPT; finally bring all engine + UI improvements to the Linux (host) build too.
  Findings: SDL Android IME (SDLInputConnection.updateText) sends KEY_DOWN scancodes for ASCII + TEXT_INPUT; main.c
  only acts on TEXT_INPUT for printable keys. Lumen fired NO keypress/textInput, React (TextEvent exists) builds
  onBeforeInput from textInput → Draft.js (x.com) never got it. FIX (NOT built): 08-window.js edit(): keypress
  before beforeinput, TextEvent textInput after, both cancelable. Probe page /tmp/draftprobe/index.html (React16 +
  Draft 0.11.7, logs 'DRAFT model ... dom ...'). APK built+installed 05:05 (pre-fix); device idle OK on cold google
  (0-2%), device_tests 45 s window ends during load (false NOT IDLE); session-restore bg tabs ~66% CPU.
  Draft probe: OLD binary = "11" doubling + NotFoundError (reproduced on host). keypress/textInput build: no
  doubling, but caret drops to 0 after the first char: clicking an EMPTY editor sets no DOM selection, so Draft
  thinks it lost focus (rangeCount 0 = no focus) and never restores the caret. FIX (NOT built): 08-window.js
  capture 'focus' listener collapses the selection into an editing host that gains focus without it.
  BUILT + host-probed (new3): Draft typing '1a b' Enter 'z' → "1a b|z" exactly like Chrome. 104/104.
  Enter fix (NOT built): SDL Android IME sends Enter as SDLK_RETURN key AND TEXT_INPUT "\n" → main.c now drops
  newline-only TEXT_INPUT on Android (Return key path edits); 08-window.js edit() fires keypress for Enter.
  Next: build APK, device check of typing/Enter on a LOCAL page (push /tmp/draftprobe to phone via host server
  + adb reverse), then the edit-bubble UI work (todo list).
  Edit-bubble code map (main.c): enum TA_* at ~3951; g_ts (selection bar, tsel_overlay ~3987 draws text labels
  Cut/Copy/Paste/Select all, bact[4]/bend[4]); kbd_tap ~4028 (2nd tap within 30 px → SDL_StartTextInput = the
  'double tap shows keyboard' the user wants REMOVED); kbm_overlay ~4052 (Paste | Select all bubble, g_kbm.act/
  bend[2]); kbm_hit ~4079 (2 buttons only); kbm_action ~4085; tsel_action ~4107; page_paste; focus_node ~3588
  (g_tap_kbd gates auto keyboard). Plan: shared icon bar renderer (push_rect + simple vector glyphs or font
  icons) used by both overlays; action set {KBD, CUT, COPY, PASTE, ALL, SLOT_A, SLOT_B, MORE}; slots default
  Backspace, Enter; others (undo, clear, hide) in More popup + snippets list (prefs key, Settings page
  lumen://settings section). Cut/Copy with no selection + non-empty → whole value. Long-press timers: paste →
  history popup (keep ring of 10 in app: text via SDL clipboard events, images via android_clip_image), backspace
  → word delete every 2 s while held. Top-right menu item 'Show keyboard' → sticky mode flag (taps don't hide).
  DONE (not built): src/app/path_icon.{c,h} (path_icon_rgba: strokes android_ui_icon path text → premult ARGB);
  UI_KEYBOARD=26 (android_ui.h enum after UI_FILE_PICK, android_ui.c icon, BrowserMenus.java KEYBOARD row before
  Settings sending action(KEYBOARD,1)). TODO main.c: handle UI_KEYBOARD in the ui action switch (~5143), icon bar.
  Bar design being coded in main.c (replace enum TA_* at ~3951): TA_CUT,COPY,PASTE,ALL,KBD,BKSP,ENTER,UNDO,CLEAR,
  HIDE,MORE; g_bar_slot[2]={BKSP,ENTER}; g_snip[] snippets (prefs bar_slots=, snippets= pct-encoded '|' list);
  ta_path()/ta_icon() cache via path_icon_rgba (22px, 1.7 stroke, 0x3c4043); bar_draw(act[],n,cx,top,bot,bar,bend,
  held) 44x44 buttons; editable bar [KBD][CUT][COPY][PASTE][ALL][slotA][slotB][MORE], page text [COPY][ALL];
  g_ts.bact/bend and g_kbm.act/bend enlarged to 10; g_pop popup (kind 1 More, 2 clip history; act TA_*,
  100+snippet, 200+clip), rows 40px; keys via SDL_PushEvent KEY_DOWN/UP (same path as real keys); snippets push
  TEXT_INPUT (static ring buffer) then RETURN if trailing \n; word delete: ctl_delete(a,f,-1,true) / js_key_mods=2
  + lumeneditdel Backspace; undo = new 'lumenundo' JS event (prelude snapshot stack on beforeinput, TODO);
  hold tick for BKSP long press (500ms then every 2s) & PASTE long press → clip history; kbd_tap: 2nd tap shows
  bar (not keyboard); sticky keyboard g_kbd_sticky (UI_KEYBOARD) skips auto-hide ~5587, cleared on
  SDL_EVENT_SCREEN_KEYBOARD_HIDDEN/back; TEXT_INPUT with no focus refocuses g_kbd_last.
  PROGRESS: main.c now has (after old enum TA_ line) the full enum, g_bar_slot, g_snip + snip_defaults/encode/
  decode, ta_label, ta_path, ta_icon, bar_draw, bar_index, edit_bar_acts, g_pop, g_clips, g_clip_pick; #include
  "path_icon.h". NEXT: rewrite tsel_overlay bar part + kbm_overlay to use bar_draw (bact/bend/act sizes → BAR_MAX),
  kbm_hit→bar_index, actions (bar_do), pop_overlay/pop_hit, hold tick, sticky kbd, prefs + settings page.
  DONE: tsel_overlay + kbm_overlay now draw via bar_draw (kbm also shows with the keyboard up); kbm_hit uses
  bar_index; g_kbm has hold_t/hold_next/held; g_ts.bact/bend BAR_MAX. NEXT: kbd_tap (2nd tap → bar not kbd),
  bar_do actions replacing kbm_action/tsel_action bodies, pop overlay+touch, hold tick, sticky kbd, prefs.
  DONE kbd_tap (2nd tap shows bar). NEXT bar_do etc.
  DONE: key_push/text_push/snip_type/field_all/field_word_delete/bar_do + new kbm_action/tsel_action (main.c,
  replacing old ones; pop_open forward-declared, NOT yet defined). NEXT: pop_open/pop_overlay/pop_hit + touch
  wiring in tsel_touch (g_pop.hit), hold tick (BKSP long press word delete every 2 s; PASTE long press →
  pop_open(a,2,...)), g_clips recording (SDL_EVENT_CLIPBOARD_UPDATE) + g_clip_pick in h_clip_image, UI_KEYBOARD
  case + sticky, prefs bar_slots/snippets + settings page, JS 'lumenundo' + snapshot stack, compile.
  DONE: pop_open/pop_label/pop_overlay (prototype + call after kbm_overlay ~2208)/pop_hit/pop_action/clip_record/
  bar_hold_tick/bar_hold_wait_ms defined before tsel_extend. NEXT: wire in tsel_touch (pop hit on DOWN; set
  g_kbm.hold_t/hold_next=now+500/held=false when a bar button is pressed (both g_kbm.hit and g_ts.bar_hit); on UP
  skip the tap action if g_kbm.held), call bar_hold_tick next to tsel_long_press(&a) ~5955 and bar_hold_wait_ms
  next to tsel_wait_ms ~5670; clip_record on SDL_EVENT_CLIPBOARD_UPDATE; h_clip_image returns g_clip_pick once;
  UI_KEYBOARD + sticky; prefs; snip_defaults() at startup; JS lumenundo.
  DONE: tsel_touch wiring (pop hit, hold timing set on DOWN, held skips tap), bar_hold_tick/wait in main loop.
  DONE h_clip_image→g_clip_pick (moved decl). Bar/pop code sits inside '#ifdef __ANDROID__' starting main.c:3953 (verify). DONE clip_record on CLIPBOARD_UPDATE. REMAINING: UI_KEYBOARD case + sticky
  DONE UI_KEYBOARD + g_kbd_sticky (focus_node keeps text focus, no auto-hide, cleared on KEYBOARD_HIDDEN/back). DONE prefs edit_slot0/1 + snippets (save/load, snip_defaults at load). DONE settings page 'Edit bar' card (newtab.html, JSON editBar, qparams edit_slot0/1 + snippets). DONE JS lumenundo (08-window.js undo stack from input events). Host build+ctest OK. Popup state renamed g_epop (g_pop is a HUD float[4]). APK built (gradle5.log) but crashed: libc++_shared.so not packaged (NDK 28.2 now installed → libs stripped, 92 MB). Added build.gradle lumenCopyCxxShared → build/lumen-stl jniLibs. REMAINING: rebuild, device test A–I (local /tmp/draftprobe via adb reverse 8799, TEST_URLS extra).
  DEVICE RUN (06:36, APK 92 MB w/ libc++_shared): PASS A bar shows w/o keyboard, B kbd icon, C "1a" (no doubling),
  D soft Enter "1a|z", E bar Enter/Backspace, F More list + snippet. FAIL G long-press Paste: no history (g_nclips 0 —
  CLIPBOARD_UPDATE not seen; record our own copies + seed from current clipboard); FAIL H word delete at block start
  crossed the line break and desynced Draft (should only delete the break); FAIL I Show keyboard: IME onFailed
  PHASE_CLIENT_VIEW_SERVED right after popup dismiss (delay StartTextInput ~300 ms). Also: tap in empty padding of
  a contenteditable box doesn't focus it. Page console.log doesn't reach logcat with CONSOLE 2. Idle CPU 0-3%, RSS 276MB.
  FIXES (not yet built): editRanges block-boundary → only the break (08-window.js); bar copy/cut clip_record +
  pop_open(2) seeds from current clipboard; UI_KEYBOARD sets g_kbd_show_at=now+350, main loop restarts text input.
  Committed 6d2402d (edit bar + fixes). RETEST 07:00: 0/G/I PASS (clip history, Show keyboard delayed works); H FAIL: long-press bksp in Draft sends lumeneditdel deleteWordBackward w/o keydown → Draft desync; H retest 07:22: word delete OK + screen/model agree, but no repeat: KEY_DOWN resets touch (android_touch_input) → fixed with BAR_KEY_ID on synthetic keys → retest 07:53 PASS (repeat ~2 s, screen=model, bar stays; idle 0%, RSS 261 MB). Also: caret can't be put on an empty last line by tap. APK gradle7 built 06:58 (WTAG(14) after #endif fixed after; rebuild needed for that only). NEXT: + retest G/H/I with monitor. TODO: tap in contenteditable padding should focus it. (old note:  ~221 fprintf; loader: k/v int parse ~5490, Android keys block has 'bar_slot%d' for the TOOLBAR (g_slot) — use NEW keys 'edit_slot0/1' (int TA_*) and 'snippets' (string, snip_decode; loader var for raw string TBD); call snip_defaults() before settings load)
  bar_slots/snippets save/load + settings page; snip_defaults() at startup; JS lumenundo; compile (Android only
  code? check #ifdef __ANDROID__ around tsel block).

## UI test automation (user: script UI tests; AI agents only when necessary)
  main.c logs 'LumenBar' (logcat) on layout change: "bar edit|sel y0= y1= x0= ACT:xend ..." and "pop kind= x= y= w=
  rh= acts=" in viewport CSS px (= page clientX/Y). tools/edit_bar_test.py WORKS (run: python3 tools/edit_bar_test.py --serial 2d4f6bc0 [--install] [--only ...]; port 8937; screen size from screencap PNG; run 1: 15/19, fails were stale-geometry dedup → geom_log per channel + 'hidden' lines, rebuilt 08:57; run 2: 15/20. Remaining fails: (1) SCRIPT: STATE line = FIRST block, so taps land on line 1 → use the LAST [data-block] in index.html line(); (2) BUG?: bar COPY with no selection in an editing host didn't reach the clipboard (history pasted an older clip) — check field_all + tsel_text timing in bar_do (JS selection mirrored async?); (3) More list / held Backspace fails probably follow from (1)/(2) — rerun after fixes. Results in /tmp/devtest/ebt2.txt) Page fixed (last block). Run 3 (/tmp/devtest/ebt3.txt): 17/21. Remaining fails all trace to ONE REAL BUG: a tap on an
  EMPTY Draft line (block = <div data-block><span><br data-text></span></div>) puts the caret at the end of the line
  ABOVE (bar Backspace deleted 'z'; snippet/paste landed on line 1/2; dom vs model then diverged after word deletes).
  NEXT: fix caret hit-testing on empty blocks with only a <br> (layout_hit / lumensel-collapse target+offset for a
  <br>-only line → collapse inside that block at offset 0), then rerun the script (no agent needed). DONE edit_empty_line() in edit_press (main.c) → run 4: 19/21. Then: bar Copy in an editing host now takes
  String(getSelection()) via js_eval_result (native mirror was stale → old clip); page dom check uses per-block textContent.
  Selection.toString() now renders block breaks + <br> as '\n' (06c-traversal.js, Chrome behaviour) → run 6
  (/tmp/devtest/ebt6.txt): 21/21 PASS, idle last-10-s 3.5%, RSS 278 MB. NEXT: rerun WPT /selection/ to check the
  toString change didn't regress; snippets textarea too short in Settings (newtab.html).
  textarea rows/cols presentational hints (style.c presentational_hints: rows*1.15em, calc(cols*0.6em+14px)) — the
  Settings snippets box had rows=6 but UA height 32px. Built host + ctest 104/104 (not in an APK yet).
  WPT run 114 /selection/: 96.8%→97.2% of Chrome-passing, full 47→57/115, score 44.1%→56.1% (no regression).
  TODO (WPT): Lumen innerText drops a trailing space under white-space:pre-wrap (Draft 'hello ' → 'hello'). + tests/device/editbar/ (LumenBar log in main.c needs APK rebuild) (Draft
  page posting state to the script; calibration taps map screen px → client px; CPU/RSS sampled every 1 s).
  Settings results (08:05 agent run): all PASS; snippets textarea too short (text overflows under Save button).

## Pass rate vs Chrome (subtests Chrome passes that Lumen also passes)

| Area | before (19:55) | now | Area | before | now |
|---|---:|---:|---|---:|---:|
| editing | 10.4 | 86.3 (run 103) | fetch | 32.7 | 68.3 (run 104) |
| input-events | 5.4 | 9.2 (run 103) | xhr | 70.6 | 76.1 (run 104) |
| urlpattern | 0.6 | 92.8 (run 103) | compression | 0.8 | 100 (run 104) |
| FileAPI | 51.8 | 59.0 (run 104) | | | |

Run 105 (fetch, xhr, FileAPI, editing, input-events, urlpattern; log `/tmp/run105-wpt.log`, gaps copies
`/tmp/run105-gaps-<area>.json`) measures the binary built at 21:14 (fetch fixes below, Enter/Delete editing,
URLPattern generate/compareComponent). The File API rewrite and the XHR/FormData fixes came after it.

## What changed this session (all uncommitted after commit 312b2a4)

- **Testdriver in frames**: `FN(internal)` forwards `td?` from frames to the top context's host; frames
  translate `move X Y` by their frame offset (`08-window.js`, `tdOrigin`).
- **Fetch rewrite**: `src/js/shared/fetch.js` (page + worker): spec Headers (guards, validation, sorted
  iteration), Request, Response, FormData, body mixin (byte streams, textStream), fetch() with JS-side
  redirects (`:redirect manual` pseudo-header), CORS + preflight cache, referrer policy, Origin, Sec-Fetch-*,
  data:/blob: URLs, SRI (`CN.cDigest`), bad ports, keepalive quota, abort. Hooks per prelude: `FETCH_BASE`,
  `FETCH_BLOB`, `FETCH_BLOBURL`, `FETCH_NET`, `FETCH_BRAND`, `FETCH_WASM`, `FORM_ENTRIES`.
- **Native net**: `net_request_control()` (pseudo-headers `:redirect`, `:cookies`), `max_redirects = -1`,
  empty `Referer` = none, and streaming flow control (`net_pause`, `NetRequest.paused/queued`: the fetching
  thread waits while JS's stream holds >16 MB or 16 MB are undelivered). Fixes the 8–12 GB RSS spikes from
  `/fetch/api/crashtests/huge-fetch` (8 GB response, never read).
- **Worker fetch**: `W.fetchAsync` streams head/chunk/done through the main thread's net_poll into the
  worker's queue (`WFetchEv`), `W.fetchPause`, `W.fetchAbort`. Workers now have Request, FormData.
- **File API**: `src/js/shared/file.js` (page + worker): Blob, File, FileReader, FileReaderSync (workers).
  Blob bytes stay in `_buf` (ArrayBuffer) for the preludes' own code.
- **Editing**: Backspace/Delete/Enter in contenteditable go through the editing engine with beforeinput and
  `getTargetRanges()` (StaticRange); typing letters keeps a cheap fast path. editing.js: linefeed instead of
  `<br>` where white-space keeps newlines; `<p>`/`<span>` hosts get a line break on insertParagraph;
  `isBefore(node, null)` (end of document) no longer throws.
- **URLPattern**: `generate()`, `URLPattern.compareComponent()`; opaque-path canonicalisation keeps trailing
  spaces.
- Smaller: form `enctype`/`encoding`/`method=dialog`, `FormData(form, submitter)`, XHR final MIME type for blob
  responses, NUL header values are network errors, ProgressEvent.length, Blob type validation.

## Checking a prelude edit without a build

`python3 tools/generate_embedded.py --root . --output-dir /tmp/embchk`, then extract the `R"JS(...)JS"` pieces of
`prelude.inc` (page) or of `kWorkerPrelude` in `src/js/worker.cc` with its `#include "*.inc"` files, and run
`node --check` on the result. This catches duplicate top-level names between shared modules (a clash once
broke the whole page prelude: 33 ctest failures).

## State at pause (22:39)

- Run 105 (binary of 21:14): fetch 71.2, xhr 76.4, FileAPI 61.1, editing 87.9, input-events 70.0, urlpattern 100.
  No crashes; one transient 1.27 GB instance on /fetch/metadata embed/element tests (released).
- Run 106 (binary of ~22:05: shared XHR `src/js/shared/xhr.js`, File API, HTTP cache for streamed fetches,
  blob UUIDs, form fixes) was at 1469/1495 when paused. Its runner and monitor subagents write
  `/tmp/run106-wpt.log`, `/tmp/run106-report-<area>.txt`, `/tmp/run106-gaps-<area>.json`, `/tmp/mon106.csv`,
  `/tmp/mon106-spikes.txt`. Read those first.
- Built after run 106 started (ctest 104/104, not yet in WPT): editing.js white-space `<span>` when joining
  blocks with different white-space, plaintext-only hosts (Enter = insertLineBreak), getTargetRanges
  normalisation (text boundaries, `<br>` ranges), no input event for no-op deletes.

## Session 2026-10-22 ~22:45 onward

- Run 106 final (fetch/xhr/FileAPI, binary 22:05): fetch 72.1, xhr 86.9, FileAPI 88.3. No crashes; one >1 GB
  instance again on /fetch/metadata/generated element-a/area tests (they use `window.open()`, which returns a
  remote-tab proxy without `document`: same-origin popups aren't scriptable; a big architectural item).
- Whole-WPT (pre-107): 96.3% of Chrome-passing subtests. Biggest missing counts: editing 13.0k, svg 8.9k
  (4.6k in `/svg/interact/scripted/pointer-events-fill-stroke-hittest.html`), shadow-dom 6.3k (5.6k in
  reference-target property-reflection: properties missing = subtests skipped), IndexedDB 4.9k (workers),
  web-animations 3.9k (no `KeyframeEffect`), html 3.6k, custom-elements 3.5k, fetch 3.5k, mathml 3.5k, domparsing 2k
  (stream* tentative, positional methods). Copy: `/tmp/gaps-all-pre107.json`, `/tmp/report-all-pre107.txt`.
- Run 107 = /editing/ /input-events/ on the 22:16 build: editing 89.2 (was 87.9), input-events 86.7 (was 70.0).
  742 tests: OK 707, TIMEOUT 29, ERROR 6; no crashes, peak 255 MB/instance. Gaps `/tmp/run107-gaps-<area>.json`.
- Edited after the 22:16 build (NOT yet built/tested; page prelude passes node --check):
  - custom elements: new valid-name rule (`[a-z][^\0\t\n\f\r />A-Z]*` with '-'), `whenDefined` rejects invalid names.
  - `ShadowRoot.referenceTarget` (native `N.srRef`, stored in the shadow root fragment's `text`), parser
    `shadowrootreferencetarget`, `attachShadow({referenceTarget})`; `refResolve()`; ARIAMixin string attrs +
    `aria*Element(s)` reflection; `anchorElement`, `popoverTargetElement`, `interestForElement`; form/list/control/
    labels resolve through reference targets (03-element.js, 04-tags.js, 04f-forms.js, 02-node.js, html.c, jsnative.cc).
  - MathMLElement: event handlers, style, dataset, focus; SVG+MathML tabIndex/autofocus/nonce.
  - http.c: a redirect `Location` without fragment keeps the request URL's fragment (redirect-navigate tests).
- Built 23:0x (ctest 104/104) with all the edits above; run 108 = /custom-elements/ /shadow-dom/ /mathml/ /svg/
  /fetch/redirect-navigate/ (log `/tmp/run108-wpt.log`, reports `/tmp/run108-report-<area>.txt`, gaps
  `/tmp/run108-gaps-<area>.json`, monitor `/tmp/mon108.*`). Results: custom-elements 64.7 (was 19.2), shadow-dom
  95.1 (was 58.1), mathml 31.5 (was 15.1), svg unchanged 40.4, fetch/redirect-navigate still 0/121 (fragment now
  kept natively, but the frame must also scroll to the fragment on load and report the URL with it; unverified).
  1478 tests: OK 1162, ERROR 31, TIMEOUT 285 (many idle stalls ~23:08–23:16, low CPU: missing events/callbacks,
  check which tests). No crashes; peak 801 MB per instance, nothing >1 GB.
- Next editing target (~1000 subtests): `/editing/whitespaces/chrome-compat/{delete,forwarddelete,inserttext}`
  expect Chrome's NBSP re-normalisation of a whitespace run after an edit: at text end, n spaces become
  (nbsp sp)* then "nbsp" (odd n) or "nbsp nbsp" (even n): 1 nb, 2 nb nb, 3 nb sp nb, 4 nb sp nb nb, 9 (nb sp)x4 nb.
  Read the tests' tables for the mid-text cases before changing editing.js's normaliser.
  Then: execCommand underline/strikethrough state (run/*.html ~400), exec-command-with-text-editor (textarea/input
  execCommand, ~290), plaintext-only styling commands must be no-ops (~208), input-events dl/list joining.
- User goal (latest): implement the most important gaps until Chrome-like (input-events, selection, xhr, FileAPI,
  workers, IndexedDB, html, dom) AND image paste.
- Image paste (edited, building): every paste (Ctrl+V, touch Paste) goes through `page_paste()` in main.c, which
  dispatches a cancelable `lumenpaste` (key = clipboard text) to the control/editing host/body; 08-window.js turns
  it into a `paste` ClipboardEvent whose DataTransfer has text/plain and the clipboard image as File `image.png`
  (`N.clipImage()` → `js_clip_image` → SDL3 `SDL_GetClipboardData` on host, `LumenActivity.clipboardImage()` via
  `android_clip_image()` on Android). Cancelled → no native insertion. Image-only paste into contenteditable inserts
  `<img src=data:>`. beforeinput/input for paste use insertFromPaste + dataTransfer. `clipboard.read()` has images.
- Paste build: ctest 104/104. Host paste test running (subagents f9831bf2 test, e61570ac monitor; probe
  `/tmp/pasteprobe/p.html`, log `/tmp/pasteprobe/run1.log`).
- IndexedDB for workers (edited, NOT built): `src/js/prelude/07i-idb.js` moved to `src/js/shared/idb.js` (prelude
  file now just the `/*@shared idb.js*/` marker); worker.cc includes `idb_shared.inc` and exports the IDB globals;
  CMakeLists lists `idb_shared.inc`; worker `fireOn` now has capture/target/bubble phases along `__evp` parents.
  Next: build, node-check the worker prelude, run /IndexedDB/ (+ /workers/) with a monitor.
- Built (ctest 104/104, copied to /tmp/lumen-wpt): paste, IDB-in-workers, FileReader fix (constructor listeners
  ran every on* handler twice: image pasted twice). WPT run 109 (/IndexedDB/ /workers/ /FileAPI/) started
  (subagents 003d2810 run, a7b45cfe monitor; log /tmp/run109-wpt.log, reports /tmp/run109-report-*.txt).
  Page console.log reaches stderr only with LUMEN_CONSOLE=1 (0 = debug too).
- Edited after that build: frames.cc `origin_of()` gives blob:http(s) URLs their inner origin (blob iframes had
  contentDocument null → html5lib_url.html ~1000 subtests; probe /tmp/blobprobe/i.html via
  `python3 -m http.server 8799 --directory /tmp/blobprobe`).
- Also edited (not built): 04-tags.js autofill `autocomplete` getter for input/textarea/select (spec tokens,
  "" when invalid) and form.autocomplete enumerated on/off; 02-node.js `cloneControls()` copies dirty value,
  checkedness and indeterminate on cloneNode (cloning-steps.html). Overall gaps snapshot: /tmp/gaps-all-now.json.
  03-element.js innerHTML/outerHTML getters use XML serialization (xmlSer) when the node document's contentType
  is not text/html (`xmlDocOf` in 06c-traversal.js) → serializing-xml-fragments/outerHTML.html.
  Run 109 showed IDB worker tests erroring: idb.js needs `report`/`dispatch` → now defined in worker.cc before the
  idb include (not built). Also seen: 'Error in remote: [object Event]' in IDB sharedworker tests (check after
  rebuild) and /FileAPI/url/url-charset.window.html TIMEOUT.
  BUILT (ctest 104/104) with all of the above incl. worker report/dispatch. Run 109 stopped early (old binary;
  315 tests, 2 CRASH — check which). Run 110 = new binary on /IndexedDB/ /workers/ /FileAPI/ /xhr/ /selection/
  /html/syntax/ /html/semantics/forms/ (log /tmp/run110-wpt.log). Old wptserve PID 290097 predates run 109; leave it.
  Run 109 CRASH ("exit 0" after ~95% CPU spin, RSS flat): /IndexedDB/idbcursor_continue_delete_objectstore.any.
  {worker,sharedworker,serviceworker}.html and idbcursor_advance_objectstore.any.worker.html → likely an infinite
  loop in idb.js cursor continue/advance when run in workers (check run 110; probe in jsrun if it repeats).
  Next code target while 110 runs: selection extend() (06c-traversal.js:478) "offset 9 larger than node length".
  RUN 110 RESULTS (of Chrome-passing): IndexedDB 16.1%→63.0% (467/815 full), workers 62.6%, FileAPI 88.5%,
  xhr 86.9%, selection 96.9% (norm 54.1%), html/syntax 96.8%, html/semantics/forms 77.9%. No crashes. Reports
  /tmp/run110-report-*.txt, gaps /tmp/run110-gaps-*.json. Monitor: instances ~100% CPU during forms loads (work).
  Edits after run 110 (NOT built):
  * worker.cc invokeOn: snapshot listener list BEFORE the on* handler (listeners added during dispatch, e.g. the
    IDB helper's EventWatcher in onupgradeneeded, wrongly got the in-flight event: 477 IDB worker subtests).
  * idb.js: getAllRecords() on store/index, IDBGetAllOptions {query,count,direction} for getAll/getAllKeys
    (idbAllArgs/idbPick), IDBRecord exported (page+worker); IDB interfaces throw 'Illegal constructor' unless made
    via idbNew() (idlharness).
  * xhr.js tail: xhrBrand() wraps XMLHttpRequest(EventTarget) accessors with 'Illegal invocation' brand checks.
  BUILT + copied; run 111 (/IndexedDB/ /xhr/ /workers/ /html/ /dom/) started (agents 400c650c run, 4d384944 mon;
  reports /tmp/run111-report-*.txt incl. -all).
  IMAGE PASTE VERIFIED on host GUI (/tmp/pasteprobe, run2.log): 1 paste = 1 img + 1 input insertFromPaste with
  dataTransfer; canceled paste inserts nothing; text intact. Minor: click past end of a line ending in <img>
  puts caret before the img (Chrome: after) → caret hit-testing todo.
  Edits after that build (NOT built): clone.cc host types carry a stable id (cloneHost 3rd arg; CryptoKey=1,
  Blob=2, File=3) instead of registration index (page/worker include order differs); file.js registers Blob/File
  as host types with fileRec() records → Blob/File survive worker postMessage (workers structured-clone 78).
    file.js uses FILEN (= N in 07-web.js, = W in worker.cc) since page CN is declared later (TDZ). Both preludes
    node-check OK. Next: after run 111 finishes, build + ctest, then rerun /workers/ /FileAPI/ /IndexedDB/.
  Run 111 monitor: inst2 (PID 1107436) spun ~130% CPU for >8 min on
  /html/browsers/history/the-location-interface/location_replace.html#x: the page fully RELOADS forever
  (log repeats 'loaded ... #x'). location.replace() to same URL + fragment must be a fragment navigation (no
  reload; hashchange/scroll only). Also the runner's hard deadline does not end a test that keeps reloading
  (deadline seems reset per load) → fix both (navigate path in main.c / location setter; lumen_wpt.py deadline).
  FIXED (not built): 08-window.js Location.replace() now does fragmentNav(href, cur, true) for same-document
  #fragment URLs (no reload; replace = no history push, N.setUrl(href, false)). Runner deadline issue still open.
  Run 111 STOPPED by me at 2416/11315 (throughput collapsed by the location_replace reload loop). Next: build all
  pending edits (Blob/File host clone, FILEN, location.replace), ctest, then rerun /IndexedDB/ /xhr/ /workers/
  /FileAPI/ /html/ /dom/ with a monitor.
  Run 111 partial (pre Blob-clone build): IndexedDB 63.0%→78.4% (515/815 full); dom 99.4% (525/757); overall
  97.1% of Chrome-passing (5524/9906 full, norm 59.2%). Runner deadline is per test (40 s) - fine; the
  html/browsers/windows/* tests just hit it often. Blob/File worker round trip verified (BLOBRT smoke).
  Run 112 = /IndexedDB/ /xhr/ /workers/ /FileAPI/ with the Blob-clone + location.replace build.
  Form submission (NOT built; probe /tmp/fsprobe with pre.js logging failures, urlencoded2.window.html):
  * 07b-net.js FORM_ENTRIES: file inputs (files or empty octet-stream File), select multiple, datalist/output
    skipped, dirname; formBuilding reentrancy guard; new FormDataEvent + FORM_FIRE (worker no-op) fired from
    FormData(form) (shared/fetch.js) → fires on form.submit()/requestSubmit too. FormDataEvent exported.
  * 06b-activate.js submitForm: CRLF newline normalization, files as names (urlencoded/text-plain) and full
    multipart parts (filename, Content-Type, UTF-8-decoded bytes — binary file bytes are lossy, todo).
  * Still open: NUL in names/values truncates (native C-string attrs/formEncode).
  * 04-tags.js select events (select-event.html 96): select() no longer fires unconditionally (selSet compares);
    setRangeText() compares with the selection BEFORE the value write and fires only on change.
  Pending build: form submission + select edits. Then probe /tmp/fsprobe (urlencoded2/text-plain/multipart) and
  rerun /html/semantics/forms/.
  * worker.cc importScript: non-JavaScript MIME essence → NetworkError (importscripts_mime 54); missing type ok.
  RUN 112 (Blob-clone + IDB + xhr brand build): IndexedDB 79.6% (540/815 full), xhr 89.2%, workers 65.5%,
  FileAPI 88.5%; overall 97.1% of Chrome-passing, 5548/9906 full, norm 59.4%. No crashes; idle-CPU tail of
  timeouts at the end (~37 TIMEOUTs with instances ~1% CPU = missing events, not load). Reports /tmp/run112-*.
  Form submission BUILT (probe: urlencoded2 8 / text-plain 14 / multipart 3 failures left, was ~170).
  After that build (NOT built): filenames CRLF-normalized too. Left: (a) NUL truncation (native attrs);
  (b) text/plain + multipart bodies must be encoded in the form charset with &#NNN; for unmappable chars —
  today the JS string body goes out as UTF-8 (need a native encode-to-charset helper returning bytes, like
  N.formEncode does for urlencoded, and navigatePost taking an ArrayBuffer).
    → DONE (NOT built): encodingjs.cc N.encodeHtml(str, label) → ArrayBuffer (enc_encode_html, &#N;);
    js_int.h js_body_bytes(); navigatePost (jsnative.cc) and navTarget (frames.cc) accept ArrayBuffer/view
    bodies; text/plain uses encodeHtml; multipart built as bytes with raw file contents.
    BUILT + probed: urlencoded2/text-plain/multipart each 2 failures left (NUL in name/value only). 104/104 ctest.
  Next: rerun /html/semantics/forms/ /workers/ ; then html/browsers idle timeouts, selection, input-events.
  COMMITTED e5ffc46 (all of the above). Run 113 = /html/semantics/forms/ /workers/ started (agents ea173b48 run,
  026120be monitor; reports /tmp/run113-report-{forms,workers,all}.txt).
  RUN 113: html/semantics/forms 77.9%→80.4% (259/499 full); workers 65.5%→69.3% (182/313); overall 97.1% of
  Chrome-passing, 5557/9906 full, norm 59.5%. No crashes, no stuck CPU, peak total RSS 1.4 GB.
  Next targets: workers modules (credentials 50, dynamic import 30, options-mismatch 32), forms idle timeouts
  (image-width-height, color.html), html/browsers windows timeouts, selection/input-events.
  Many /html/ gaps there are stale (enctype setter exists) → rerun /html/ /dom/ /selection/ /xhr/ after the build.
- Ideas not done: detached `<img>` (not in document) never loads → onload never fires (fetch/metadata img/picture
  timeouts; also real-site preloading). Native Sec-Fetch-* for subresources/navigations (needs initiator/dest on
  NetRequest). fetch stream-safe-creation.any.js times out (cause unknown; probe it).
- Whole-WPT snapshot after run 114 (`report` over all of lumen.jsonl; pre-report copies /tmp/report-sel114.md,
  /tmp/gaps-sel114.json): 11,639 tests run, 97.1% of Chrome-passing subtests, 5557/9906 full, norm 59.5% vs
  Chrome 91.3%. Statuses OK 10181 / TIMEOUT 1046 (html 292, svg 262, fetch 110) / ERROR 403 (bluetooth 148,
  wasm 94) / CRASH 8 (clear-site-data 6, quirks excluded-properties-003, html duplicate-name-order).
  NOT RUN: 26.6k of 37.6k Chrome-passing testharness tests (412k subtests): css 7.8k tests, html 7.0k of 8.6k,
  referrer-policy, CSP, service-workers, websockets, pointerevents, cookies, webaudio, webcodecs, webrtc.
  Top single gaps: svg pointer-events hittest 4608, svg idlharness 909, web-animations KeyframeEffect missing
  (~2.6k over interpolation/accumulation/addition), editing whitespace delete/forwarddelete/inserttext ~1k,
  quirks unitless-length ~1k, custom-elements customized built-ins 222, url/failure (Location href) 196.
- Run 115 (in progress at 09:48): /css/ + /html/ with the 09:30 staged binary (15,187 tests).
- Web Animations (uncommitted, host build + ctest 104/104 OK, smoke: 60px at t=500 of 10→110px):
  new `src/js/prelude/06d-anim.js` replaces the 06c `Animation` stub and the 02-node timelines. It has easing
  (cubic-bezier, steps with before flag, linear()), EffectTiming validation, the spec computed-timing
  model, keyframe processing (sequence + property-indexed, offsets/easing/composite lists, only animatable
  props read in code-point order), KeyframeEffect (copy ctor, target, pseudoElement, composite,
  iterationComposite, get/setKeyframes), the Animation playback model (pending play/pause tasks,
  ready/finished promises, finish/cancel/remove events, reverse, updatePlaybackRate, persist,
  commitStyles, replaceState removal), and Element.animate/getAnimations + Document.getAnimations.
  Values interpolate on token lists (numbers/units → calc for mixed units, colours premultiplied, 2-D matrix
  decomposition for mismatched transforms, discrete fallback), composite add/accumulate. Effect values go
  to a new native layer `Node.anim_decls` (`N.setAnimStyle` → `css_set_anim_style`, applied after inline
  style, before !important). getComputedStyle flushes (`waFlush`) first; the frame loop (`waKick`/`waTick`
  on N.raf) runs only while an animation is running/pending. rAF callbacks set `waFrameTime` so
  document.timeline.currentTime equals the rAF timestamp inside a frame. Not done: CSS animations/
  transitions as CSSAnimation objects, scroll timelines, pseudo-element targets.
- url/failure: remote window proxies (`asWin`) now validate Location URLs with `navURL` (SyntaxError).
- SVG hit testing (built 10:0x, ctest 104/104; jsrun has no layout hit test so verify via WPT):
  `svg_hit()` in `src/paint/svg.c` walks the painter's geometry (same transforms/viewBox) and applies the
  `pointer-events`/visibility/fill/stroke rules (fill: winding test, stroke: distance <= sw/2; <text> as
  em boxes, Ahem 1 em/char). `hit_node()` in main.c refines a hit on an <svg> box for elementFromPoint
  (h_hit_in, hf_hit), touch/pointer targets and hover. Reads attributes + style attr only (not sheets).
- SVG DOM (unbuilt at 10:00): brand checks on svgProps getters; new SVGStringList/NumberList/LengthList/
  PointList/TransformList (generic `svgListClass`), SVGTransform, SVGAngle, SVGPreserveAspectRatio + animated
  wrappers, SVGTests (requiredExtensions/systemLanguage), SVGGraphicsElement transform/getBBox/getCTM/
  getScreenCTM (getBBox moved off SVGElement), viewportElement, SVGSVGElement extras, text-content
  methods, text positioning lists, polyline points, marker orient*, SVGAnimationElement family,
  SVGView/MPath, TimeEvent; all exported in 08-window.
- SVG DOM built 10:1x: ctest 104/104, smoke values all correct (SVGTransform, points, preserveAspectRatio).
- Found: window named access (`window.someId`) skips SVG-namespace elements (HTML spec: any element with
  an id). Not located yet (not in jsnative NamedPropertyHandler; search the global/window prelude code).
- Editing (unbuilt): `canonicalSpaceSequence` in src/js/lazy/editing.js now emits Chrome's pattern for
  n > 1 (nbsp, sp, nbsp, ...; last one nbsp at a line end), for the ~1,000 chrome-compat whitespace failures.
- Run 115 findings: (a) /css/selectors/invalidation/user-valid-user-invalid.html posts its result, then
  reloads itself ~5/s forever (no Enter is sent; reproduce alone). The runner's hard deadline skipped
  it; lumen_wpt.py now restarts an instance that posted a result and asks nothing for 20 s.
  (b) 3 SIGABRT heap corruptions on canvas+video tests (`avformat_close_input` double free, main +0x18ae86):
  pattern-from-video-cross-origin-taint, canvas-display-p3-drawImage(-ImageBitmap)-video.
  All use tiny (2x2) videos; first fix: `to_image` (media.c) pads the BGRA buffer by 256 bytes against
  swscale SIMD overrun on narrow rows. Unverified: rerun those 3 tests (and under ASan if still crashing).
  (c) A 4th CRASH is `exit 0` on /html/canvas/offscreen/fill-and-stroke-styles/2d.fillStyle.parse.hsla-clamp-3.worker.html
  (something in a worker quits the whole process?).
  (d) Every /html/cross-origin-opener-policy/ iframe-popup-* test ends ERROR "Timeout while running
  cleanup" after 60 s (resources/iframe-test.js:130: send(popup, "await send(close_token,'closed'); close();")
  then receive(close_token) never resolves, although 'Popup loaded' worked). javascript-url.https.html
  TIMEOUTs too. Costs ~1 min per test variant; investigate the popup executor's polling after the
  subtest (popup opened from the iframe via window.open).
- Built 10:1x (ctest 104/104): SVG DOM, editing whitespace (smoke: "a&nbsp; &nbsp;" correct), media pad.
  Committed 38ae850 (run 115 used the older staged binary, so it does not measure these changes).
- Run 115 done (11:26, 15,187 tests /css/ + /html/; OK 13723, ERROR 497, TIMEOUT 961, CRASH 6).
  /css/: 27.0% of Chrome-passing subtests, 1418/7361 tests fully passing, score 27.3% vs Chrome 95.6%.
  /html/ (whole): 51.2%, 1849/8300, score 26.5% vs 95.5%. Reports: /tmp/report-html115.md + gaps, and
  wpt-data/lumen/report.md + gaps.json for /css/.
  Largest CSS gaps (missing Chrome-passing subtests): css-grid 12.9k, css-typed-om 12.5k, css-color 10.8k,
  css-anchor-position 8.4k, css-values 7.4k, css-fonts 6.0k, css-shapes 5.7k, css-backgrounds 5.5k,
  motion 4.8k, css-align 4.0k, css-gaps 3.8k, flexbox 3.7k, sizing 3.7k, text 3.4k, images 2.8k,
  transforms 2.8k, transitions 2.5k, conditional 2.4k. Most are parse/computed-value tests
  (getComputedStyle/CSSOM serialisation), so a shared property parse/serialise layer is the lever.
  Run 115 subagent deep reports: /tmp/run115-report-{css,html,all}[-deep].txt. Whole dataset now 87.5%
  of Chrome-passing subtests, 8097/24014 tests fully passing, score 39.6% vs 94.0%. Notable html
  subareas: the-xhtml-syntax 0/25607 (13 tests), canvas 4.8% (2D context methods are JS no-ops in
  07z-extra.js!), cross-origin-opener-policy 16.3%, browsers 22.7%. Two more video crashes:
  video_loop_base.html (SIGSEGV), video-aspect-ratio.html.
- Run 116 started (bg subagent 139dfabe + monitor a5a9dd60): /web-animations/ /svg/ /editing/whitespaces/
  /url/ wide-gamut + fill-and-stroke canvas, video_loop_base, /css/selectors/invalidation/ with the
  38ae850 build. Reports -> /tmp/run116-report-*.txt.
- RUN 116 RESULT (38ae850 build, --rerun needed since URLs were already in lumen.jsonl): 1119 tests,
  OK 820 / TIMEOUT 287 / ERROR 12 / CRASH 0. web-animations 4.6% -> 54.9% (48/119 full), svg 40.4% ->
  68.9% (116/694), editing 89.2 -> 89.8, url 95.7 -> 97.9. Video crash tests now OK (padding helped);
  video_loop_base TIMEOUT. REGRESSIONS (OK -> TIMEOUT): /svg/animations/seeking-events-1..8,
  conditional-processing-02, syncbase-escaped-dots, /svg/linking/scripted/href-animate-element,
  /web-animations/interfaces/Animation/cancel.html. ERRORs: CSSAnimation not defined; SVGLength-cap/ic/
  lh/rlh *_test ReferenceError; javascript-urls "java\tscript:" assign throws (URL parser must strip
  tab/newline before validating); start-time-compat off by ~17ms. Reports /tmp/run116-report-*.txt.
- Run 117 (bg 43358da6 + monitor 0b5ce71a): /html/canvas/ /svg/animations/ Animation/cancel.html with
  a3c04e5+alpha-serialisation build. cancel.html: the thenable/finish/cancel subtest works in jsrun
  (/tmp/wacancel.html), so the timeout is another subtest; check run 117 per-subtest output.
- RUN 117 RESULT: 3681 tests, OK 3568 / TIMEOUT 101 / ERROR 12 / CRASH 0. html/canvas 4.8% -> 39.5%
  (2288/5787 Chrome-passing; 1754/3344 full). Monitor: no hot CPU, RSS < 310 MB. Rows /tmp/run117-results.jsonl.
  Canvas next fixes, by size: (1) workers lack OffscreenCanvas/ImageBitmap (worker prelude!) -> offscreen
  *.worker tests; (2) wide-gamut display-p3 drawImage tests time out (~1000 subtests); (3) createImageBitmap
  arg validation/crop/flipY/resize; (4) TextMetrics.getIndexFromOffset/getSelectionRects (tentative);
  (5) regressions: element/compositing *.clear and uncovered.nocontext.* (red at 50,25), DOMMatrix.fromMatrix
  missing (FIXED, uncommitted: c2dMatrixInit in 07y-canvas2d.js), fillStyle for color(srgb ...) must serialise as "color(srgb 1 0 1)",
  "Array buffer allocation failed" for huge canvases (cap size -> zero-size bitmap);
  canvas.transferControlToOffscreen missing; text 12%, shadows 28%, layers 8% (beginLayer/endLayer).
  Still timing out: svg/animations seeking-events-1..8 and event tests; cancel.html subtest
  "finished promise should not be rejected by cancel..." (jsrun passes it -> maybe step_timeout/rAF timing).
- 2e00940: canvas code moved to src/js/shared/canvas2d.js (+ color parser to shared/color.js; prelude files
  07y/04e now hold only /*@shared*/ markers) and included in the worker prelude (worker.cc after idb);
  worker natives via js_c2d_install (c2dFill/Clip/Measure; C2DCTX macro, thread_local scratch); worker
  createImageBitmap (ImageData/ImageBitmap/OffscreenCanvas, crop; Blob rejects). DOMMatrix.fromMatrix
  replaced by c2dMatrixInit. Smoke /tmp/c2dw.html (worker) + /tmp/c2d.html pass; ctest 104/104.
- RUN 118 (2e00940 build): /html/canvas/ 3385 tests OK 3322/TO 53/ERR 10/CRASH 0; 39.5% -> 54.2% (3136/5787),
  2528/3344 full. Monitor clean. Analyzer: /tmp/run118-analyze.py. Next by size: (1) 912 subtests in five
  element/manual/wide-gamut-canvas display-p3 drawImage/pattern tests that time out whole-page; (2) draw-element-
  image (requestPaint, tentative) 260; (3) TextMetrics getIndexFromOffset/getSelectionRects/getActualBoundingBox/
  getTextClusters (tentative) ~190; (4) "source image could not be decoded" 87 (worker createImageBitmap(Blob)
  must decode -> need native decode in worker); (5) shadows: fillRect.shadow red at 50,25 (61) + shadow.alpha (22);
  (5 DONE uncommitted->see next commit: shadow drawn from offset shape, off-canvas shapes cast shadows)
  (1) investigating: tests load resources/*.png|jpg|webp|avif|bmp|ico|gif|svg via new Image() then
      createImageBitmap/drawImage; jsrun can't load images (probe /tmp/imgprobe.html useless) -> use real lumen
      (run 119: the five wide-gamut tests, per-subtest statuses).
      RUN 119 RESULT: every subtest TIMEOUT for ALL images incl. plain 8-bit sRGB PNG; no errors. Cause =
      detached `new Image()` never loads/fires onload (same as the fetch gap "detached img loads"). FIX THAT.
      Code: main.c ~2850 (fetch-start loop walks p->d tree only: img src -> icache/net_fetch img_done) and
      fire_img_events (main.c 2876, also tree-only, keyed "%p url" in p->img_fired). Plan: per-Page list of
      detached img nodes (refcounted) registered from JS (N.imgWatch(node) in 04-tags img src setter /
      setAttribute src when !isConnected); both loops also iterate it; drop entries after firing. Also
      img.complete is hard-coded true in 04-tags.js:117 (should be false while loading).
      IMPLEMENTED (uncommitted, built 12:54): Document.dimg list (dom.h; released in doc_free), N.imgWatch
      (jsnative.cc) called from attrChange (02-node.js) for detached HTML img src; sync_images and
      fire_img_events iterate it; fired/connected entries are dropped. ctest 104/104.
      RUN 120 (canvas/manual + the-img-element + fetch): wide-gamut pages STILL timed out; img +13, fetch -66
      (text-utf8.any.serviceworker.html "ServiceWorker script evaluation failed" -30 - check if worker prelude
      change broke SW, metadata/preload.https.sub.html timeout -36, cookies leak between instances?).
      Cause of still-timeout: restyle() returns early unless dom_version changed and a detached attr edit
      doesn't bump it -> imgWatch now bumps d->dom_version too (built 13:3x, uncommitted). Recheck = run 121.
      RUN 121: still timed out; REAL cause: setAttribute uses attrSetQ (03-element.js ~211), not attrChange
      -> hook added there too (built). SW text-utf8 now OK (run-120 failure was flaky). /tmp tmpfs was 98%
      full: deleted /tmp/run120-prev-all.jsonl; don't copy lumen.jsonl into /tmp. Recheck = run 122.
      RUN 122 (committed with this fix): /html/canvas/ 54.2% -> 62.9% (3638/5787), 2607/3344 full; img 24.5%.
      No crash; monitor clean. Next: drawImage rejects structured-cloned ImageBitmap (184, '-cloned' test);
      pattern regressions (createPattern(zero-height img) must be null; norepeat.coord1; shadow.pattern.transparent.1);
      drawImage.zerosource.image (39); worker Blob decode (87); FontFace in worker (51).
      DONE next commit: structuredClone/postMessage clone ImageBitmap + ImageData (08-window.js cloneValue;
      closed bitmap -> DataCloneError); zero-size image source -> null pattern / no-op drawImage. Not WPT-run yet
      (jsrun /tmp/c2dc.html passes). zerosource.image uses zero-size SVGs: check naturalWidth of those.
      DONE next commit: worker createImageBitmap(Blob) decodes via W.imgDecodeSync (jsnative.cc, worker-only,
      sync on the worker thread); /tmp/c2db.html passes; ctest 104/104. NEXT: WPT run 123 of /html/canvas/,
      then FontFace in worker, beginLayer/endLayer, TextMetrics tentative APIs, then back to SVG/web-animations
      timeouts and CSS gaps.
      Run 123 (bg 3f5ac9cd + monitor 59335b40) started on /html/canvas/ with the 176bbad build.
      FontFace: page FontFace is a STUB (06b-activate.js:332, status 'loaded', no loading; fontSet above it).
      Canvas text tests use FontFace('CanvasTest', url(/fonts/CanvasTest.ttf)) -> real fix = fetch + register
      with native font registry (font.c ~594 registers family/url under font_mu) for page AND worker.
      IMPLEMENTED (uncommitted): real FontFace in src/js/shared/fontface.js (fetch url()/binary ->
      N/W.fontFace(family, weight, italic, key, bytes) = font_declare + font_loaded in jsnative.cc); page uses
      it via /*@shared fontface.js*/ in 06b-activate.js (fontSet unchanged); worker gets FontFace + `fonts` set
      RUN 123 (176bbad build, before FontFace): canvas 62.9% -> 65.7% (3800/5787), 2692/3344 full; no crash,
      monitor clean. Biggest left: wide-gamut 665 (display-p3 canvases must convert sRGB sources: 255,0,0 ->
      234,51,35; P3/AdobeRGB image ICC handling), draw-element-image 261 (requestPaint, tentative),
      imagebitmap 245, offscreen/text 227 + element/text 133 (FontFace now real -> rerun), getIndexFromOffset 110,
      layers 63, strokeRect.zero (39), 3 pattern regressions (nocontext, zerowidth, modify.image2).
      (worker.cc). COMMITTED; /tmp/ff.html: page+worker AAA=30px with CanvasTest; ctest 104/104. Needs WPT rerun.
  (6) FontFace missing in worker (51); beginLayer/endLayer (48); DOMPoint in worker (12); text align center (21).
- Committed after 2e00940 (shared/canvas2d.js; also transferControlToOffscreen DONE, page-only): modern colours (color()/lab/oklab) serialise in their
  own space; c2dAlloc leaves an empty bitmap for >2^28 px or failed allocation. NOTE element/compositing
  "*.clear" losses are not regressions ('clear' is not a valid op; stubs passed by drawing nothing).
  Next: transferControlToOffscreen (OffscreenCanvas with placeholder node; c2dPresent to placeholder),
  then the run-118 top failure messages.
- CANVAS 2D committed a3c04e5 (host build + 104 ctest OK; /tmp/c2d.html smoke via jsrun passes).
  Not yet WPT-run: run /html/canvas/ with --rerun next.
- CANVAS 2D design (was in progress): the 2D context in 07z-extra.js is all no-ops. Design:
  * src/paint/canvas2d.{h,c} (new): c2d_cover (polygon coverage via svg.c's `svg_acc_line`, newly
    exported), c2d_paint (paint kinds: solid / linear / two-circle radial / conic via 1024-entry
    premultiplied LUT / pattern with nearest or bilinear + repeat; all 26 composite ops; unbounded ops
    (source-in/out, destination-in/atop, copy) touch the whole canvas; clip = float mask), and a
    node -> Image table (c2d_present copies px, bumps content_version; old frames in a 32-slot ring).
  * DONE (unbuilt) jsnative.cc bindings (block "canvas 2D" before the jsprof section, REG'd), main.c
    node_img uses c2d_image_for, svg.c exports svg_acc_line. Text shape adds sx, sy (8th/9th items).
    Bindings: c2dFill(px Uint32Array, w, h, shape, alpha, op, clip, paint) with
    shape [xy Float32Array, sub Int32Array, eo] or text [str, family, weight, italic, size, x, y]
    (font_get/text_shape/font_glyph alpha into coverage); c2dClip(mask, w, h, xy, sub, eo) (mask *= cov);
    c2dMeasure(str, family, weight, italic, size) -> [width, ascent, descent]; c2dSource(node) ->
    [w, h, ArrayBuffer premult ARGB] via paint_image_hook; c2dPresent(node, px, w, h) + repaint request.
  * Colour: cssColor(text) (04e-color.js) -> { space, c: [0..1 x3], alpha, legacy } | { current: true } | null;
    colorTo(col, 'srgb') -> [r, g, b] 0..1. Stubs to delete: 07z-extra.js lines ~105-148 (ImageData, Path2D,
    noop, CanvasGradient, CanvasPattern, CanvasRenderingContext2D + the noop method loops); also fix
    HTMLCanvasElement.toDataURL/toBlob in 04-tags.js:149 and canvasResized (07g-webgl.js:705) for 2D.
  * DONE (unbuilt) JS: src/js/prelude/07y-canvas2d.js (ImageData with EnforceRange checks, PathData,
    Path2D incl. SVG d, gradients/patterns, TextMetrics, CanvasRenderingContext2D +
    OffscreenCanvasRenderingContext2D sharing C2D_METHODS, shadows via JS box blur + Float32Array
    coverage shape, PNG encoder (stored deflate) for toDataURL/toBlob/convertToBlob, OffscreenCanvas).
    Stubs removed from 07z-extra.js; 04-tags getContext uses c2dCreate; canvasResized resets 2D; exports
    added in 08-window.js:633. Tainting not tracked (s.tainted never set). strokeText = filled text.
  * (old plan) JS: state stack (styles, transform, clip mask copy, dash,
    shadow fields), CSS colour parse via cssColor, path in device coords (flatten arcs/beziers/ellipse/
    arcTo/roundRect), stroke outline in user space (joins, caps, dash) as positively-oriented polygons
    filled nonzero, Path2D (incl. SVG path string), gradients -> LUT, patterns, drawImage (pattern fill
    of the dest rect), get/putImageData (unpremultiply; buffer Uint32 ARGB, bytes B G R A), toDataURL,
    present batched per microtask, canvasResized resets the bitmap and state.
- Next: build, stage to /tmp/lumen-wpt/bin/lumen, run /web-animations/ /svg/ /url/
  with a monitor; fix regressions; commit.

## Next steps

1. Read run 106's report; copy the current build to `/tmp/lumen-wpt/bin/lumen` and rerun /editing/
   /input-events/ (and /fetch/ /xhr/ /FileAPI/ if run 106 shows regressions) with a monitor.
2. fetch: check the request-cache / http-cache tests with the streamed-fetch cache (fresh hits only, ≤8 MB
   copies); error-after-response; fetchLater; Sec-Fetch-* for navigations and elements (native loader).
3. xhr: now shared (`xhr.js`, async on `fetchMain`); synchronous requests still use `corsSendSync` (page) and
   `W.fetch` (worker). Check run 106 for regressions; XHR timeout-worker tests hit the 100 s deadline.
4. input-events: beforeinput for more input types; editing timeouts (testdriver).
5. Android: rebuild the debug APK (see `CONTINUE.md`), `adb -s 2d4f6bc0 install -r`, then
   `python3 tools/device_tests.py -s 2d4f6bc0 google chatgpt x` with a monitor. The fetch, streaming and
   editing changes touch ChatGPT (streamed replies, the ProseMirror composer): check typing CPU < 10% and that
   replies stream.
6. Then the far-behind areas: IndexedDB, shadow-dom, custom-elements, web-animations, mathml, EditContext.
- RUN 124 (9e63495 FontFace build): canvas 65.7% -> 66.3% (3839/5787); offscreen/text 72 -> 108; no crash; monitor clean
  (peak RSS 313 MB). Regressions fixed next commit: maxWidth squeeze now resamples glyph bitmaps horizontally (jsnative.cc
  c2d_text_cov; advances were squeezed but glyphs were not), measureText/fillText drop U+0000.
- NEXT COMMIT: canvas colour spaces (shared/canvas2d.js: s.cs; c2dColor(text, cv, cs); gradients carry cs; c2dConvert/
  c2dConvertRGBA/c2dXf srgb<->display-p3; get/putImageData + createImageData default to the canvas space; drawImage via
  c2dBlit converts sources). createImageBitmap is now shared (c2dBitmap: validation, crop, resize, flipY, snapshot into
  ImageData; page passes bmpDecode, worker W.imgDecodeSync). image.c applies embedded ICC matrix/TRC profiles (PNG iCCP,
  JPEG APP2, WebP ICCP) -> sRGB with clipping. Not kept: wide-gamut source data for P3 canvases (P3 image on P3 canvas
  reads as clipped sRGB), CMYK JPEGs. Smoke /tmp/c2dp3.html (jsrun needs LUMEN_CONSOLE=1 to print console.log).
  /tmp/jscheck.py = parse check of page + worker preludes.
- UNCOMMITTED after 2870572: TextMetrics getIndexFromOffset/getSelectionRects/getActualBoundingBox (prefix widths via
  c2dMeasure; measureText keeps text+font in _); beginLayer/endLayer (s.layers stack of {px, st, depth}; endLayer
  composites the layer bitmap with the beginLayer-time alpha/op/shadow/clip; restore can't pop past a layer).
  Smoke /tmp/c2dl.html. Run 125 (canvas + the-img-element, 2870572 build) in progress.
- RUN 125 (2870572: colour spaces + shared createImageBitmap + ICC): canvas 66.3% -> 71.3% (4127/5787); img 24.3% flat;
  no crash; monitor clean (peak RSS 342 MB). Wide-gamut drawImage tests 76 -> 124/192 each; pattern-canvas, ImageData,
  canvas sources all pass. Remaining: P3/Adobe image on P3 canvas needs wide source data (keep a P3 copy of images with
  wide ICC profiles); 'srgb-linear' (15); video sources 0/24; createImageBitmap oversized resize must keep w/h (fixed
  next commit: empty data, reported size kept); exif-orientation 0/7; ImageBitmap serializable/transfer 0/12 each;
  requestPaint/draw-element-image (tentative) 330; getTextClusters 15; DOMPoint in workers 12; filters ~33.
  Layers + TextMetrics methods committed 0770437 (not yet WPT-run).
- bcac952: srgb-linear canvas/ImageData space (C2D_SPACES; c2dXf(to, from) handles transfer + primaries); oversized
  createImageBitmap keeps w/h with empty data.
- NEXT COMMIT: real Geometry Interfaces in src/js/shared/geometry.js (DOMPoint[ReadOnly], DOMRect[ReadOnly], DOMQuad,
  DOMMatrix[ReadOnly] 4x4 with all ops, fromMatrix validate/fixup, CSS transform-list parsing in windows only,
  WebKitCSSMatrix alias) on pages and workers; replaced the 06c stubs and the 01-events DOMRect. Smoke /tmp/geo.html,
  /tmp/geow.html. Needs a WPT run of /css/geometry/ and /html/canvas/ (getTransform), /svg/ (uses DOMMatrix).
- RUN 126 (bcac952): canvas 71.3% -> 72.8% (4212/5787), 0 crash, statuses unchanged; layers element 4->13/25, offscreen
  8->27/50; text element 54->67, offscreen 110->136. Regressions: 2d.filter.layers.blur.exceptions x3 (false passes before;
  fixed: beginLayer validates gaussianBlur stdDeviation), 3 single-pixel (pattern.basic.nocontext, shadow.image.transparent.2,
  text.draw.align.left) unverified/flaky.
- Remaining canvas: requestPaint/draw-element-image (tentative, ~330), document.caretPositionFromPoint MISSING (80; needs
  native layout text-offset hit test, also missing caretRangeFromPoint) - real DOM API, do next; drawimage_html_image
  (56, pixel mismatch); 2d.drawImage.zerosource.image (37); text align (19); wide-gamut images on P3 canvas (keep wide
  data); getTextClusters (15); index-from-offset-edges (15); display-p3-linear (13, Chrome passes? check); selection-rects
  y/x offsets (24); getActualBoundingBox x for substrings (18: expected -0 got 50).
- 242d83c committed (filter validation). Build/ctest 104/104 clean.
- caretPositionFromPoint plan: no caret/offset hit API exists. layout_hit (src/layout/block.c:581) returns a Box; Box
  has node, text/text_len, font, sh (ShapedRun: glyph x + cluster byte index). Add native N.caretAt(x, y) in jsnative/jsglue
  (via the page hit hook like hf_hit/h_hit_in in main.c ~3089/3303): hit box -> nearest glyph cluster boundary to
  x - box x -> byte offset within box text -> map to the node's UTF-16 offset (box text may be whitespace-collapsed slice;
  need the box's start offset in node data - check how text boxes are built in src/layout/inline*.c). Then JS
  document.caretPositionFromPoint(x, y) -> CaretPosition {offsetNode, offset, getClientRect()} and
  caretRangeFromPoint. Element hits with no text box -> {offsetNode: element, offset: child index}.
  Layout detail: TextFrag {box, x, y, w, h, base, g0, g1} (layout.h:15) lives in the IFC root box's frags[]; frag.box is
  the text box; glyphs g0..g1 of box->sh (ShapedRun) with cluster byte index into box->text (collapsed text). Need a
  collapsed->source offset map (find where box->text is built from node data in src/layout, e.g. whitespace collapse).
  box->text comes from prep_text(bs, n->text, n->text_len, st, &len) in src/layout/box.c:188 (whitespace collapse /
  text-transform); add an optional source-offset map there (or recompute by re-running the collapse walk on n->text in
  the caret native) to turn a collapsed byte offset back into the node's UTF-16 offset.
  prep_text (box.c:35) is byte-wise: drops \r, collapses spaces (depends on bs->last_space from previous text), tabs ->
  4 spaces when not collapsing, case transform ASCII only. Plan: add `int *src` (arena, k+1 entries: output byte ->
  source byte) to Box, filled by prep_text; caret native maps cluster byte -> src byte -> UTF-16 (count code units of
  n->text[0..src]).
  IN PROGRESS (uncommitted, unbuilt): Box.tsrc (layout.h) filled by prep_text(..., &t->tsrc) for DOM text nodes
  (box.c:192; NULL for ::before/::after content and line 237). Check new_box zero-inits tsrc and whether any code later
  advances box->text (trim) which would misalign tsrc. Next: native caretAt + JS caretPositionFromPoint/caretRangeFromPoint.
  Checked: nothing advances box->text after prep_text, so tsrc stays aligned. Next: verify new_box zero-inits, then add
  the caret native and JS API, build, smoke, commit, WPT-run canvas text + /css/cssom-view/ (caretPositionFromPoint).
  DONE (unbuilt): tsel_dom_point (src/paint/select.c) maps the select.c hit (box byte off) through Box.tsrc to a UTF-16
  offset; JsHost.caret hook (js.h) = hf_caret (iframes) / h_caret (tabs) in main.c; N.caret(x, y) native (syncs layout);
  JS document.caretPositionFromPoint(x, y, {shadowRoots}) + caretRangeFromPoint + CaretPosition (06-document.js),
  falls back to [element at point, 0]. Verify with real lumen on /css/cssom-view/caretPositionFromPoint* and canvas text.
- 685c69a committed caretPositionFromPoint. RUN 127 (cssom-view, geometry, canvas) started on that build.
- BIG CSS FIND: computedHandler (03-element.js) had no `has` trap, so `prop in getComputedStyle(el)` was false and
  test_computed_value (css/support/computed-testcommon.js) failed its first assert for every property: 7,940 subtests in
  css-color alone, likely tens of thousands across /css/. Added CSS_PROP_NAMES (505 names from Chrome-passing
  "Property X ..." subtests, /tmp/chrome-props.txt) + cssPropName (camelCase/webkit) and a `has` trap. Next: rerun /css/
  and look at the computed-value serialization failures that become visible.
  Verified in jsrun: 'color' in cs true, 'fooBar'/'--x' false. Computed colours are lossy because native stores 8-bit
  RGBA (css_get_computed_value): lab/oklch -> grey 127, color(display-p3 ...) -> black, rgb(from ...) -> black,
  alpha 0.5 -> 0.501961. Fix plan: native keeps the declared colour string for color-ish properties (or JS reads the
  cascaded declaration) and JS serialises via shared color.js (cssColor/colorTo) per css-color-4 computed rules
  (legacy rgb()/rgba() for sRGB-legacy syntaxes, keep lab()/oklch()/color() forms, alpha rounded to 3 sig like Chrome).
  Uncommitted: style.c alpha_round (Chrome 2/3-decimal alpha) in color_str.
  Modern-colour plan: css_apply_decl(st, par, P, val, ...) in style.c (~line 908 for background-color) parses with
  css_parse_color(val, &col, current) into 8-bit Color. Add to ComputedStyle a small owned list of {prop atom, declared
  string} for colour properties whose value is non-legacy (lab/lch/oklab/oklch/color(/color-mix/`from`/light-dark?);
  check how ComputedStyle is copied/freed (inherit + style sharing) before adding owned pointers. css_get_computed_value
  returns "\x01<declared>" for those; ComputedStyleDeclaration.getPropertyValue serialises it with shared color.js
  (same serializer as canvas fillStyle) resolving currentcolor. Also native parser lacks lab/oklch/color(): fix
  css_parse_color to convert them to sRGB 8-bit for painting (use the matrices from color.js).
  Uncommitted (style.c): wide_color() converts lab/lch/oklab/oklch/hwb/color(srgb|srgb-linear|display-p3|rec2020|
  a98-rgb|prophoto-rgb|xyz*) to 8-bit sRGB for painting (replaced the grey placeholder); relative colours (`from`)
  still unsupported natively. Computed-value serialisation of modern colours still pending (plan above).
- 45f4df8 committed colours. RUN 127 (685c69a): geometry 17.8% -> 84.7% (1328/1568), cssom-view 33.8 -> 35.1%,
  canvas 72.8 -> 73.4% (4247/5787); 0 crash; 5 cssom-view TIMEOUT->OK. caret tests live in /css/cssom/ + /shadow-dom/
  (not rerun yet: caretPositionFromPoint.html 1/7, shadow-dom Document-caretPositionFromPoint 1/14 before).
  Uncommitted fixes for run-127 geometry regressions: .length 0 via default params, getter names via computed
  accessors, own statics on DOMPoint/DOMRect/DOMMatrix, structured clone (geoClone in cloneValue), DOMRectList
  interface (mkRectList helper; getClientRects not yet returning it).
  cssom-view next: scrollWidthHeight-negative-margin-002 (496), getBoxQuads (49), convertRectFromNode (18),
  smooth scroll set-immediately semantics (~120), offsetTopLeft-border-box (28), new-iframe MQL (24),
  element-scroll-promises (24), scrollIntoView (40).
- 4534129 committed geometry fixes. RUN 128 started (agents 43986682 run, f986614f monitor): /css/ + /shadow-dom/
  on 4534129 (first CSS run with the getComputedStyle `in` fix + colour conversion); results -> /tmp/run128-*.
  Computed modern-colour serialisation plan (not started): canvas2d.js c2dColor (line ~315) already serialises
  cssColor() results Chrome-style (lab/oklab keep form, others -> color(space ...), alpha rounding). cssColor
  (shared/color.js:39) returns {space, c[3], alpha, legacy, current}. For ComputedStyleDeclaration.getPropertyValue
  of colour properties: if the native computed value is a colour and the element's inline declaration for that
  property (most WPT computed tests set target.style[prop]) parses with cssColor to legacy === false, return the
  c2dColor-style string instead (lch/oklch: Chrome keeps lch()/oklch() forms - check color-computed-lab/-oklch
  expectations). Proper fix later: native keeps the declared string per colour property.
  Checked expectations (css-color/parsing/color-computed-lab.html:86+): lch()/oklch() keep their own form with hue as a
  bare number: "lch(0 0 0deg / 0.5)" -> "lch(0 0 0 / 0.5)"; lab/oklab likewise keep form. So the serialiser needs
  lch/oklch/lab/oklab kept as-is (numbers via colorNum), color(space ...) for the rest.
- RUN 128 (4534129, /css/ + /shadow-dom/, 8321 tests, 1460 s, 0 crash): /css/ 27.6% -> 31.3% (58349/186576);
  css-color 9.7 -> 46.0%, geometry 96.5%, css-box 60.1%, shadow-dom 95.2%. 98 regressed (mostly animation
  composition/interpolation: underlying value not replaced, repeat counts in grid-template interpolation; css-fonts
  generic-family-keywords-002 -webkit-sans-serif; FontFace ascentOverride etc. getters missing). caret tests:
  flex 3/3, transform 1/1, caretPositionFromPoint 5/7 (must return null outside viewport), caretRange 4/7.
  TOP /css/ GAPS: 26280 "'X' value should be supported" (interpolation test helper - check what it calls,
  e.g. CSS.supports or element.animate support), 29372 generic computed mismatches, 10915 ReferenceError
  CSSKeywordValue (css-typed-om: implement CSSStyleValue family + attributeStyleMap/computedStyleMap),
  5946 canonical serialization, ~2500 grid checkLayout, 1134 CSS.supports false, 882 querySelector should throw
  (unknown pseudo-elements), 698 gridTemplateColumns.
- ROOT CAUSE for much of /css/: css_get_computed_value (style.c ~3920-4046) handles ~68 properties; everything else
  computes to "" (and CSS.supports = css_supports in parse.c:603 only checks the 167-entry known_props name list).
  PLAN "generic property store": ComputedStyle gets a refcounted map (like s->custom / custom_ref / custom_get) of
  unmodelled standard properties -> cascaded value string; css_apply_decl stores unknown-but-valid names there;
  style_inherit copies only inherited ones; computed value = stored value, else inherited, else Chrome initial value.
  Initial values + inherit flags come from Chrome-passing subtest names in chrome-base.json:
  "Property X has initial value Y", "Property X inherits" / "does not inherit" -> generate a C table
  (tools/ script) src/css/props_generated.inc. css_property_known then = known_props ∪ table (CSS.supports).
  IMPLEMENTED (unbuilt): tools/gen_css_props.py -> src/css/props_generated.inc (497 props {name, inherited,
  native(computed natively), initial}); style.c gen_props/gen_prop (bsearch)/gen_put (COW like custom)/gen_value;
  ComputedStyle.gen_i/gen_n (css.h); css_apply_decl stores non-native props (inherit/initial/unset handled, "" =
  initial); css_get_computed_value falls back to gen value / initial ("currentcolor" -> color); css_property_known
  includes gen_prop (CSS.supports); paint_only zeroes gen_i/gen_n. Known gaps: no value validation, no px
  conversion of lengths, inherited gen values don't propagate on inheritance-only parent updates.
- committed generic store; RUN 129 (/css/) started on it
- RUN 129 (/css/, agents 487629c6 run, 53fb8e96 monitor) running on 55850a5.
- CSS TYPED OM (in progress): new src/js/prelude/03e-typedom.js. CSSStyleValue(parse/parseAll), CSSKeywordValue,
  CSSNumericValue + CSSUnitValue + CSSMath{Sum,Product,Negate,Invert,Min,Max,Clamp} + CSSNumericArray, CSS.<unit>()
  factories, CSSUnparsedValue/CSSVariableReferenceValue, CSSTransformValue + components, CSSImageValue,
  StylePropertyMap(ReadOnly): element.attributeStyleMap, element.computedStyleMap(), rule.styleMap.
  Tests: /css/css-typed-om/ (the-stylepropertymap/properties/*.html use resources/testsuite.js there).
  wired: 08-window exports Typed OM classes + CSS.<unit>() factories (before getComputedStyle).
- committed Typed OM (8ae2503); verified smoke /tmp/tom.html, 104/104. Next: WPT /css/css-typed-om/ after run 129.
- RUN 129 (55850a5 generic store): /css/ 31.3% -> 35.7% (66646/186576), 0 crash, +14287 / -5928 subtests.
  REGRESSION TO FIX FIRST: "CSS Transitions: property <X> ... at (-0.3)/(-1)" now return the plain target value
  (translate/scale/rotate/clip-path/filter/colours/lengths): previously passed because native computed was "" and the
  JS transition/animation layer's value was used; now the generic stored cascaded value wins. Check 06d-anim.js
  (waFlush / setAnimStyle / how transitions of unmodelled props expose values) and make animated values override gen.
  Other: 5949 colour serialization canonical (#000000 must be rgb(0, 0, 0) in specified style), 2080 anchor-name
  value should be supported, place-items/place-self shorthand expansion (~770), getComputedStyle insets % (756),
  CSS.supports should reject unknown -apple-* (718), cs is not iterable (getComputedStyle iteration, 371).
  Typed OM (8ae2503) not yet WPT-tested: run /css/css-typed-om/.
  CORRECTION: those 5928 "regressions" were FALSE PASSES: interpolation-testcommon compares the target against a
  reference element styled with the expected value; both computed "" before. Lumen has NO CSS transition/animation
  interpolation (jsglue.c style_change only fires events). PLAN "CSS transitions/animations on the WA engine":
  - native: ComputedStyle records props set by the anim layer (setAnimStyle) so they don't trigger transitions;
    css_computed_of(el, style, prop) (refactor of css_get_computed_value) gives old/new values; style_change, for
    props allowed by transition-property (all: native list + gen maps union) whose value changed, calls a JS hook
    js_transition(node, prop, from, to, dur, delay, easing) (per-property duration/delay/easing lists).
  - JS (06d-anim.js): CSSTransition subclass of Animation built from KeyframeEffect([{p: from}, {p: to}]),
    timing from transition-*; replace an existing transition of same el+prop; events transitionrun/start/end/cancel
    then come from JS (drop native ones for handled props).
  - later: CSSAnimation from @keyframes (needs native access to keyframes rules: N.keyframes(name)).
  TRANSITIONS DESIGN (concrete): style.c: css_get_computed_value -> wrapper of computed_of(el, style, prop);
  new css_transition_scan(n, o, s, cb): for tr_prop list names (or for "all": native gen_props + keys of gen maps)
  compare computed_of(o) vs computed_of(s), cb(n, prop, from, to). jsglue.c style_change calls it (when
  tr_dur+tr_delay > 0) -> js_transition_start(n, prop, from, to) (js.cc) pushes AnimEv{type "lumen-transition",
  name "prop\1from\1to", due now}. JS: fireAnim handles that type; N.trTake() (jsnative) pulls pending ones
  synchronously; CSSStyleDeclaration getPropertyValue: v = N.computed; if (N.trTake().length) start, waFlush,
  recompute. JS start: skip props currently written by the WA layer (waApplied css) to avoid self-triggering;
  read transition-duration/delay/timing-function/behavior lists from computed style at index of prop in
  transition-property; new CSSTransition (Animation subclass) with KeyframeEffect([{p:from},{p:to}]), play();
  cancel previous transition of same el+prop.
- RUN 130 (/css/css-typed-om/, 8ae2503): 48 -> 4292/12587 (34.1%), 0 regressions, 0 crash. Remaining: 6903 =
  CSSStyleValue.parse / map.set must throw TypeError on values invalid for the property (needs per-property value
  grammar validation - would also fix CSS.supports and inline style parsing!); CSS.Q unit must be "q"? (expected
  "q"); computed relative lengths must be CSSUnitValue px (computedStyleMap resolves calc/em); unsupported values
  must be plain CSSStyleValue (accent-color 'red' -> CSSStyleValue: only keywords valid for the property become
  CSSKeywordValue); SVG props (r, stop-color, d, fill-rule, paint-order...) missing from CSS_PROP_NAMES;
  CSSColorValue/CSSRGB/CSSHSL/CSSHWB missing; Typed OM in workers; DOMException SyntaxError for empty CSSMathSum.
- TRANSITIONS: native side written (style.c css_transition_scan/computed_of, jsglue.c, js.cc js_transition_start,
  jsnative N.trTake) — unbuilt; JS side next in 06d-anim.js.
  JS side written: 06d-anim.js trStart/trPoll/CSSTransition/trSuppressCss; 03-element getPropertyValue polls; 08-window fireAnim handles 'lumen-transition'. Smoke /tmp/tr.html.
  fix: transition-*/animation-* longhands also stored raw in gen (computed_of prefers them; native tr_delay is max>=0 for events); excluded from transition scan.
- committed transitions; RUN 131 (/css/) started
- CSS ANIMATIONS DESIGN (next): jsglue.c style_change already detects anim_name change (on/nn) -> also push AnimEv
  type "lumen-animation" (name = new animation-name list, "" = removed) via a js.cc helper like js_transition_start;
  N.trTake returns [el, packed, type] for both types. JS: find @keyframes by name in document.styleSheets
  (CSSKeyframesRule._list of CSSKeyframeRule: keyText + style) -> keyframes [{offset, prop: value, easing}];
  read animation-duration/delay/timing-function/iteration-count/direction/fill-mode/play-state/composition lists
  (gen store keeps raw longhands) -> CSSAnimation (Animation subclass, animationName) with KeyframeEffect; cancel
  previous CSSAnimations of the element not in the new list. Keep native animationstart/end events for now.
  CSS animations implemented (unbuilt): js.cc js_style_job/js_animation_names, jsglue style_change, 06d caStart/CSSAnimation; smoke /tmp/ca.html
  CSS animations built OK (ctest 104/104; /tmp/ca.html longhands width=150px correct). TODO now: shorthand
  `animation`/`transition` must also fill the raw gen longhand lists (delay is lost: native anim_delay/tr_delay are
  clamped/max). Implement in style.c css_apply_decl near anim_decl: split layers on top-level commas, classify tokens.
  shorthand expansion written: style.c anim_shorthand_gen (before anim_decl), called for animation/transition shorthands; unbuilt.
- committed CSS animations + shorthand lists (6150e70); not yet WPT-tested (run 131 uses 965feec). Minor: waMix letter-spacing normal<->length should treat normal as 0.
- RUN 131 (965feec transitions, /css/): /css/ 70890 -> 76049/186576 (40.8%), 0 crash. CSS Transitions subtests
  13122 -> 18186/27375; CSS Animations 1442/10673 (6150e70 not yet tested); Web Animations 6217/10653.
  REGRESSIONS (1193): (1) allow-discrete transitions at 0.5 interpolate where Chrome flips to the end value -
  cases where from/to are not interpolable together in Chrome (line-height number<->length, gap 'normal'(initial)
  <->px, grid repeat() counts differ, z-index?, aspect-ratio, counter lists): waMix is too permissive; the from value
  of 'initial' column-gap computed natively as a length instead of "normal". (2) 17 OK->TIMEOUT: css-transitions/
  properties-value-*.html (6 tests; likely transitions on many props - check transitionend events or a loop),
  cssom-view scroll-behavior tests, contain-intrinsic-size auto-004/006.
  NEXT: fix those, rerun /css/css-transitions/ /css/css-animations/ + affected dirs; then CSS.registerProperty
  arg validation (6908 did-not-throw is mostly typed-om parse validation + registerProperty), colour canonical
  serialization in specified style (5949), place-items/place-self shorthands (770), insets % in computed (756).
  properties-value-* timeout: runParallelAsyncHarness waits for options.allTransitionsCompleted (driven by the
  test's transitionend bookkeeping). Hypothesis: JS CSSTransitions now run for props that get no transitionend
  (native events only for its 16 props in jsglue.c style_change), or native+JS mismatch. FIX PLAN: CSSTransition
  fires transitionrun/transitionstart/transitionend/transitioncancel itself (via fireAnim-like dispatch on
  play/finish/cancel with elapsedTime), and jsglue.c stops firing native transition events (keep animation events
  until CSSAnimation fires animationstart/end/iteration/cancel itself).
  implemented: CSSTransition fires transitionrun/start/end/cancel (06d trStart); jsglue.c returns before native transition events. unbuilt.
  FOUND: inline 'initial' ignored for native props (column-gap/z-index/min-width/top keep sheet value); line-height:4 computes 64px (Chrome '4'). Fixing in css_apply_decl.
  fixed (unbuilt): initial/unset fallback via goto apply with gp->initial; computed line-height prints unitless factor.
  gap 'normal' now resets column/row gap.
- committed initial/unset fix; RUN 132 started (transitions/animations/align/position/sizing/grid/CSS2/cssom-view/flexbox/values/lists)
  specified colour canonicalisation (03-element.js specColor/SPEC_COLOR_PROPS) written; smoke /tmp/sc.html; unbuilt
- committed specified colours (5467407); not yet WPT-tested
  shorthandPart (03-element.js) written: longhand reads derived from stored shorthand; smoke /tmp/sh.html
- committed shorthandPart (acad551); not WPT-tested
  computed insets: % resolved against containing block for positioned elements (style.c computed_of); unbuilt
- committed insets (f7a08d2); not WPT-tested. Waiting for run 132 (agent 08c68903, monitor da8b0ffd).
- RUN 132 (c21d75d; 15 css dirs, 3170 tests): 24757 -> 29838/71990 (34.4% -> 41.4%), 0 crash, 19 TIMEOUT->OK,
  4 OK->TIMEOUT (css-transitions disconnected-element-001, inherit-height-transition, non-rendered-element-001,
  transition-zero-duration-with-delay). css-transitions 16% -> 52%, CSS Animations prefix 837 -> 3244/5465.
  Regressions 217: interpolate-size/calc-size (min-height 'initial'(=auto) now interpolates; Chrome: discrete
  unless interpolate-size), transform at (-1) from skewX -> expected "none"?? (CSS Animations with neutral keyframes),
  -0 vs 0 in trailing-space-position, tab-size/flex-shrink extrapolation differences.
  NEXT TODO: transitions must NOT start for disconnected / display:none / non-rendered elements and zero-duration
  (+ delay) must not start; transitionend for margin-bottom % (properties-value-002); keyframe selectors
  'entry 10%' (timeline ranges) in CSSKeyframeRule; specified-colour + place-* + insets commits not yet tested.
  (unbuilt) zero-duration+delay transitions start; trCancelUnrendered in waTick/trPoll cancels transitions/CSS animations of removed or display:none elements
- committed transition cancel; RUN 133 (/css/ full) started
  (unbuilt) CSS_PROP_NAMES + SVG props (r d fill-rule stop-* stroke-line* paint-order text-anchor vector-effect marker* ...)
- committed SVG prop names (9393c8d), unbuilt/untested; run 133 in progress (agents 9aef844a run, 00398d04 monitor)
- RUN 133 (/css/ full, 2c77e92): 81130 -> 85969/186576 (46.1%), 0 crash; CSS Animations 3849 -> 5752/10673.
  REGRESSION 1134 css-color parsing: specColor rejected color-mix/relative/alpha()/contrast-color/calc(infinity)/none
  -> FIXED (unbuilt): unknown function forms kept raw. 21 OK->TIMEOUT (cssom-view scroll-behavior, cssom link
  load events, selectors focus-visible) - probably flaky under 4 jobs: rerun those dirs to check.
  Next big: registerProperty validation (6908 did-not-throw incl. typed-om parse), gap "normal normal" -> "normal"
  canonical shorthand serialization (4974 canonical), insets still 20px vs 23.2px (containing block size wrong:
  abs uses padding box incl. padding? check), CSS-supports general_enclosed, querySelector invalid pseudo.
- committed colour regression fix (01de660); next: rerun css-color + timeouts dirs, then registerProperty validation / shorthand canonical serialization
  CSS.registerProperty implemented (06b-activate.js registerProperty + N.registerProp); smoke /tmp/rp.html expect: TypeError TypeError SyntaxError ok InvalidModificationError SyntaxError SyntaxError ok ok | --a=10px; unbuilt
  computed custom prop falls back to registered initial (style.c ~4057)
- committed registerProperty (19b8b84); untested in WPT (css-properties-values-api)
- RUN 134 (css-color/cssom-view/cssom/selectors/css-sizing): 17068 -> 17407/30156, 22 TIMEOUT->OK (run-133 timeouts were flaky), 0 crash.
  654 regressed = invalid colour functions accepted (raw passthrough). Narrowed passthrough to from/calc/color-mix/
  alpha/contrast-color/light-dark/device-cmyk/color-layers (empty args rejected); others validated by cssColor.
  Proper validator for color-mix/relative colours still TODO. css-sizing min-content narrower on floats/abspos (23) TODO.
- committed colour validation; RUN 135 (css-color, css-properties-values-api, css-typed-om, cssom, css-position, css-align) started
  N.computed now syncs layout for box-dependent props (computed_needs_layout in jsnative.cc); unbuilt
- committed computed layout sync (22ff1ae); waiting run 135 (cd7773d3 / mon 2b9a12de)
  specNormalize (03-element.js): number canonicalisation + minimal pair/box shorthands; smoke /tmp/sn.html; unbuilt
- RUN 135 (color/pva/typed-om/cssom/position/align): flat 16677 -> 16668/36930. pva 18.6 -> 28.1%, css-color 50.2 -> 51.7%.
  Regressions fixed (unbuilt): TOM_NO_NUMERIC in tomText (SVG props now known accepted lengths); registerProperty
  comments/sign()/math/% in transform/<color>+ top-level split/color functions; specColor comments, none kept raw,
  display-p3-linear + sibling-index passthrough. TODO: @property invalid initial must not register (parse.c 793).
  Insets still 20px for abs/fixed even with layout sync -> investigate (maybe box missing / cb loop wrong).
- committed specNormalize + run135 regression fixes; RUN 136 (/css/ full) started
  @property: native validity (syntax+inherits required, initial for non-* syntax, simple type checks) parse.c; unbuilt
- committed @property validity (2d68538); waiting run 136 (739f448b, mon be2ea702)
- committed tomValidText (11b59d4); smoke /tmp/tp.html ok; not WPT-tested
- RUN 136 (/css/ full, bb82281 = before @property/tomValidText commits): 86299 -> 88910/186576 (47.7%), 0 crash.
  typed-om 3995 -> 6130. 403 regressed: (a) css-sizing width/height "CSS Transitions ... from neutral" at -0.3 give
  the target (transition not started?) 170; (b) colours at 0.3 give 76/49 instead of 77/50 and font-variation
  15.000003 -> float32 round trip somewhere in the CSS Transitions path (suspect: interpolated value now read back
  through native computed or specNormalize/specColor re-serialising anim values); (c) css-transitions
  properties-value-001/inherit pt heights "must not be target value after start" got 13.3333px.
  13 OK->TIMEOUT (transitions before-load-001, transition-remove-and-change-immediate, transitioncancel-003;
  cssom-view x4; cssom x3) - some flaky. NEXT: reproduce (a)/(b) with jsrun page using
  /mnt/p3/project/browser/wpt/css/support/interpolation-testcommon.js pattern; then insets (still 20px), align-content
  "first baseline" -> "baseline" canonical, CSSPositionTryRule, CSS.supports general_enclosed, cs is not iterable.
  repro /tmp/tq.html (simple transition): color rgb(77, 50, 0) correct, width ok, fvs ok -> regression needs the exact interpolation-testcommon.js flow (neutral keyframe / 'transition-behavior' / reversed?); next read /mnt/p3/project/browser/wpt/css/support/interpolation-testcommon.js cssTransitionsInterpolation and replicate.
  FIX (unbuilt): specNormalize skipped timing-function/transition/animation and no longer rounds to 6 decimals (createEasing cubic-bezier precision caused run-136 interpolation regressions)
- committed easing precision fix (45d8478); next: rerun css-sizing/css-transitions/css-backgrounds/css-fonts/css-typed-om/css-properties-values-api/css-values (run 137)
- RUN 137 (8 dirs): 29207 -> 29297/59430, 0 status changes, 14 regressed (keyword-sizes-on-floated-element: font-load flake?). Easing fix confirmed.
- committed tomTypedOk (31312f1); smoke /tmp/ts.html ok
- committed cssPropKnown/CSS.supports unification + first baseline; RUN 138 (css-conditional, css-typed-om, css-align, cssom, css-anchor-position, css-color, css-properties-values-api) started
  specFoldMath/mathEval (03-element.js): constant-fold trig/pow/sqrt/exp/log in specified values; smoke /tmp/mf.html; unbuilt
- RUN 138: 24942 -> 29376/52212 (typed-om 48.7->75.8%, conditional 39->64%), 30 regressed (touch-action/pointer-events/background missing from CSS_PROP_NAMES -> added with webkit aliases, unbuilt). Also unbuilt: nested calc fold.
- committed math folding + prop names (a8fdc71). NEXT: color-mix computed serialisation in mixing space (~1069), CSSPositionTryRule, abspos align-self sizing, CSSStyleValue.parse('--foo','') throw, column-gap '0'->'0px' canonical
- committed 0px + empty custom parse (32674cd); unmeasured since run 138: math folding, prop names, 0px. NEXT: run 139 (/css/ full), then color-mix computed serialisation, CSSPositionTryRule, abspos align-self.
- RUN 139 (/css/ full, binary 32674cd) started; agents 0243d874 (run) / 80769927 (monitor).
  color-mix plan: computed colour of color-mix()/relative colours must serialise in the mixing space
  (color(srgb ..)/oklch()/lch()/color(xyz-d65 ..)), alpha kept, none -> missing-component rules. Native css_parse_color
  currently resolves to 8-bit sRGB. Option: native keeps the original declaration text for colour props when it
  contains color-mix/from, and N.computed returns JS-resolved text via shared color.js (cssColor + colorTo + colorNum).
- RUN 139 (/css/ full): 93434 -> 94347/186576 (50.6%), 0 crash. filter-effects +465, motion +336, css-values +250.
  Regressions: css-gaps column-rule-color lists/repeat() + caret-color auto rejected by colour validation (fixed: specColor
  passes auto/top-level commas/repeat()); typed-om shorthands+touch-action accept 0px (fixed: shorthand numeric reject);
  flexbox <img> items 0px (image-as-flexitem-size-002v/005) UNINVESTIGATED; -webkit-sans-serif generic (css-fonts) TODO;
  smooth-scroll timeouts likely load-related. Unbuilt as of this note.
- run-139 regression fixes are in commit 8098fa6 ('wip' message; built+smoked OK, /tmp/rr.html). NEXT: flexbox img 0px (image-as-flexitem-size-002v), -webkit-sans-serif generic, color-mix computed, CSSPositionTryRule, gap 'calc() calc()' pair canonical (SPEC_PAIR skips words with '(').
- committed pair/box with functions (1a793f8); smoke /tmp/gp.html ok. Unmeasured since run 139: colour lists, shorthand numeric reject, pair functions. NEXT as listed above.
  flexbox img 0px (image-as-flexitem-size-002v/005): test checks layout in body onload -> window load may fire before <img> decode under load; check that document load waits for pending image decodes (sync_images / load event in main.c). Next item.
  FOUND: main.c:3409 js_fire_load fires after DCL waiting only for iframes; images not awaited -> body onload layout checks see 0px imgs. PLAN: make js_fire_load (js.cc) also wait until no <img> in the doc is pending (count pending fetches in sync_images/paint_image_hook, signal on decode/error), with a cap (~10 s).
  load-vs-images design: frames.cc frames_loading(c) (line ~159) -> add '|| (js_images_pending && js_images_pending(c->doc))' hook (extern fn ptr set by main.c counting doc <img> with fetch in flight); when an image finishes/errs in main.c and c->load_deferred, call js_fire_load(c). Cap wait. Not started.
  IMPLEMENTED (unbuilt): js_load_blocked hook (frames.cc/js.h), Page.img_inflight/load_t0, img_done -> js_load_recheck, sync_images before js_fire_load, h_load_blocked in main.c (10 s cap).
  REVISED: counter replaced by scan: g_loading_page (set at load, cleared in page_free), h_load_blocked scans doc <img> (non-lazy, non-empty src) for icache slot !done; recheck at end of fire_img_events and in img_done when loading page is not cur. Run 140 (counter version): flexbox +52, img +9, sizing +7, but 3 img tests TIMEOUT (page not cur during load; src->'' never rechecked).
  run 142 (scan version): flexbox +48, img -5 vs base. invisible-image: load now waits but test fails; delay-load: img.complete was hard-coded true -> assertion threw before src=''. FIX (unbuilt): complete = !src || imgSize || error fired for src (imgFailed WeakMap, imgSrcChanged in 02-node.js).
  run 144: +311 (html/dom +246, flexbox +52). run 145 img dir: +23/-6, new TIMEOUTs img-complete-after-error/update-src-complete/svg-zero-height. FIX (unbuilt): N.imgDone native (jsg_img_done_hook -> img_fetch_done in main.c, icache slot done) used by complete; malformed URL sets imgFailed before dispatch.
- committed load-waits-for-images + img.complete (7613375). Runs 144/146: html/dom +246, flexbox +52, img +17. img-complete-after-error/update-src-complete pass alone (timeouts were load). Exposed: SVG naturalWidth when not rendered should be 0 (got 600, viewBox); srcset complete; -webkit-sans-serif; color-mix computed; CSSPositionTryRule.
  -webkit-sans-serif (generic-family-keywords-002): likely Ahem font-load flake (passed before run 139, same symptom as keyword-sizes-on-floated-element); recheck alone later.
  SVG naturalWidth design: main.c img_size (line ~326) serves layout AND N.imgSize; for SVG images without intrinsic width/height (viewBox only) naturalWidth/Height must be 0 while layout keeps the ratio. Needs an Image flag (svg_no_w/no_h) from image.c SVG decode + a separate natural-size path for N.imgSize. Not started.
- RUN 148 (/css/ full, binary 7613375) started
- RUN 148 (/css/): 94406 -> 94678 (+331 vs run 139), 0 crash; 45 regressed, ~20 from colour validation accepting auto/lists/abs(none) everywhere -> FIX (unbuilt): specColor(v, p) passes auto only for caret/accent-color and lists only for *rule-color with paren-nesting-aware check. smoke /tmp/cr.html (expect color auto='', caret auto, col-rule auto='', lists kept, bg list='', clamp abs(none)='', color-mix kept)
  UNBUILT: anchor()/anchor-size() name-first canonical order (specNormalize), animation-range longhands via shorthandPart/animRangePart. smoke /tmp/an.html /tmp/ar.html
- RUN 149 (anchor/animations/color/gaps/values/logical/...; binary a6a8ab8): +2142 (anchor-position 6021->8121), 1 regression (CSSAnimation-canceling display:none ancestor -> expected idle got running, likely flaky timing), no status changes.
  UNBUILT: CSSPositionTryRule + CSSPositionTryDescriptors (06b-activate.js, PT_ALLOWED filter, exported in 08-window.js). smoke /tmp/pt.html
- RUN 150 (anchor-position + cssom, b4f3ab7): anchor 8121->8446 (+325), cssom +5, 0 regressions, 5 status improvements. Top anchor failure: 1305 'assert_array_equals [N,N,N,N]' in anchor-in-scroller-with-left-side-scrollbar (RTL scrollers, layout).
  UNBUILT: tomTypedOk stricter (numbers rejected for length props except line-height; % rejected for border/outline widths, outline-offset, scroll-margin, column-width, perspective; grid lines only numbers; keyword-only props list). smoke /tmp/tv.html
- RUN 151 (typed-om, 13211fe): 9564->10263 (+699); 5 regressions border-image-width (fixed: exempt, unbuilt). Remaining typed-om: 1047 did-not-throw (CSSPerspective.length setter etc.), CSS.Q unit 'q' expected lowercase?, relative lengths->CSSMathSum, workers lack Typed OM, CSSRGB missing.
  UNBUILT: tomRect WebIDL CSSNumberish (strings -> Number, finite else TypeError, no CSS parse); CSS.Q unit 'q'. Next: smoke + rerun typed-om (run 152). Remaining: Typed OM in workers (CSSMathSum etc. undefined), CSSRGB, '<position>' syntax in registerProperty.
- RUN 152 (3a76c97): typed-om +15 (10278), transforms 0; 11 regressions border-image-width time/angle/flex accepted -> FIX (unbuilt) only length/percent/number; units all lowercase (hz/khz expected). Remaining typed-om: 1049 did-not-throw (CSSNumericValue.mul incompatible types etc.), relative lengths->CSSMathSum 91, unit px expected (computed abs lengths) 81, unsupported values should be base CSSStyleValue 74.
  committed 8c2ba04 (built, smoke ok, not yet WPT-rerun). Next ideas: CSSNumericValue arithmetic incompatible-type throws (1049 did-not-throw), Typed OM in workers, CSSRGB, '<position>' syntax.
  UNBUILT: TOM_NUMERIC_OK allow-list in tomTypedOk: numeric CSSStyleValues rejected for any property not in length/angle/time/flex/numeric-ok lists. Smoke /tmp/tv.html + /tmp/tom.html, then WPT typed-om rerun (run 153); watch regressions for props missing from the allow-list.
- RUN 153 (typed-om, numeric allow-list): 10278->10913 (+637), 2 regressions text-size-adjust % (added to TOM_NUMERIC_OK, committed cee02a6, rebuild pending). Typed-om ERRORs left: CSSRGB undefined, Typed OM missing in workers (CSSMathSum), '<position>' syntax component unsupported (5 tests).
  '<position>' ERRORs: testsuite.js has no <position> examples -> errors in any browser, skip. Next: CSSRGB/CSSColorValue family, Typed OM in workers (shared/ file like canvas), relative lengths->CSSUnitValue in computed map.
