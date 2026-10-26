# Lumen: handoff (2026-10-25, branch `devin-opus5.5/wpt-continue`, HEAD `b931e7d`)

**Goal:** Lumen should pass the Web Platform Tests that Chromium passes ("pass WPT like Chromium"). Measure each
area against Chrome's baseline, fix the biggest gaps first, rerun, and record every run here. The JS engine and
CSS/DOM code are shared between host and Android, so WPT runs on the host build. Check the phone with
`tools/device_tests.py` after each batch of engine changes.

Older notes, read on demand:
- `agents/archive/wptcontinue-2026-10-25.md`: WPT runs 100–170, with per-run findings and leads.
- `agents/archive/CONTINUE-2026-10-25.md`: Android, GPU, video, ChatGPT, x.com and build history up to v0.3.3.

## Rules (from AGENTS.md, short form)
- **Builds:**
  - Every compile and test goes through `python3 tools/codex_compile.py COMMAND ARGS...`, with no bash wrapper inside it.
  - Use at most 6 compiler jobs, or 2–4 while a WPT run is going.
  - Gradle needs `--no-daemon`.
  - Never restart the V8 source build; use the prebuilt.
- **Subagents:**
  - Builds, test runs and device control go to a fresh Haiku subagent (`effort: high`). Each runtime test also gets a parallel monitor subagent that samples CPU and RSS every second and watches the logs.
  - Code stays in the main agent. Don't edit sources while a build subagent is building.
  - If the subagent quota is exhausted, run the capped commands yourself, still with CPU/RSS sampling.
- **Privacy:**
  - Never type into page inputs.
  - Describe third-party pages only structurally.
  - Delete screenshots that show personal content.
- **Phone safety:**
  - Never `pm clear` or uninstall; install with `adb install -r`.
  - Never `pkill -f`.
  - Don't touch host PID 837634.
  - Someone may be using the phone: check `dumpsys input` RecentQueue before trusting UI results.
- **SDL:** don't modify `build/codex-android-deps/SDL` (that project forbids AI-written code).

## Commands
- **Host build:**
  - `python3 tools/codex_compile.py cmake --build build/codex-native -j 6`
  - Unit tests: `python3 tools/codex_compile.py ctest --test-dir build/codex-native` (104/104 at the last run).
  - Binaries: `build/codex-native/lumen` and `jsrun`. `jsrun` runs JS smoke pages without layout: `SDL_AUDIO_DRIVER=dummy LUMEN_CONSOLE=log build/codex-native/jsrun page.html`.
- **Prelude syntax check without a build:** `python3 tools/generate_embedded.py --root . --output-dir /tmp/embchk`. Then extract the `R"JS(...)JS"` parts and run `node --check` on them. This catches duplicate top-level names, which once broke the whole prelude.
- **WPT:** the harness is `tests/wpt/lumen_wpt.py`. Its docstring explains the controller, `wpt serve`, hosts and CA.
  - WPT checkout: `/mnt/p3/project/browser/wpt` (revision 0e1c0bce3d).
  - Data: `/mnt/p3/project/browser/wpt-data/lumen/` (`lumen.jsonl` results, `report.md`, `gaps.json`, `crash-logs.txt`).
  - Chrome baseline: `wpt-data/chrome-base.json`.
  - Run: `python3 tools/codex_compile.py python3 tests/wpt/lumen_wpt.py run --jobs 5 /css/css-grid/ ...`. Add `--rerun` to redo tests that already have results; otherwise they are skipped.
  - Report: `python3 tests/wpt/lumen_wpt.py report [PREFIX...] [--deep]`. It rewrites `report.md` and `gaps.json`, and "Lumen of Chrome-passing" is the number to compare.
  - Only one harness run at a time (fixed controller port 8790). For long runs, copy the binary
    (`cp build/codex-native/lumen build/codex-native/lumen-wpt`, then `--binary build/codex-native/lumen-wpt`) so rebuilds
    don't change it mid-run.
  - Number the runs. The next run is **227**.
- **Android APK:**
  ```
  python3 tools/codex_compile.py /usr/lib/jvm/java-21-openjdk/bin/java -Xmx64m -Xms64m -Dorg.gradle.appname=gradle \
    -classpath build/codex-android-toolchain/gradle-8.13/lib/gradle-gradle-cli-main-8.13.jar org.gradle.launcher.GradleMain \
    -p android --no-daemon --max-workers=4 :app:assembleDebug -PlumenSdlDir=$R/build/codex-android-deps/SDL \
    -PlumenDepsDir=$R/build/codex-android-deps/prefix -PlumenWgpuDir=$R/build/codex-android-wgpu/prefix \
    -PlumenV8PrebuiltDir=$R/build/codex-android-v8-prebuilt-sdk \
    -PlumenV8CompilerDir=$R/build/codex-android-v8/v8-15.6.75.8/third_party/llvm-build/Release+Asserts/bin
  ```
  - `$R` is the absolute repo path.
  - Verify the signature with `java -Xmx1024M -jar ~/Android/Sdk/build-tools/36.0.0/lib/apksigner.jar verify APK`.
  - Phone: `adb -s 2d4f6bc0`.
  - Releases: `gh release create vX.Y.Z-android-preview APK --prerelease --target BRANCH`. The latest is v0.3.4-android-preview.

## Standings (report of 2026-10-25: subtests Chrome passes that Lumen also passes)
Overall: 1,865,339 / 2,062,887 = **90.4%**. Encoding and wasm inflate this. Tests fully passing: Lumen 11,470,
Chrome 24,237. Harness statuses: OK 24,468, TIMEOUT 1,992, ERROR 592, CRASH 8.

| area | Lumen of Chrome-passing | area | Lumen of Chrome-passing |
|---|---:|---|---:|
| /css/ (8030 tests) | 99,365/186,576 **53.3%** | /html/ (8917) | 126,355/154,909 **81.6%** (after run 171) |
| /editing/ | 89.8% | /svg/ | 68.9% |
| /fetch/ | 71.8% | /mathml/ | 31.5% |
| /domparsing/ | 39.6% | /web-animations/ | 54.9% |
| /custom-elements/ | 64.7% | /IndexedDB/ | 80.6% |
| /shadow-dom/ | 95.2% | /trusted-types/ | 83.0% |
| /workers/ | 69.9% | /uievents/ | 26.8% |
| /performance-timeline/ | 18.6% | /xhr/ | 89.2% |
| /webmessaging/ | 59.9% | /webidl/ | 80.3% |
| /dom/ | 99.4% | /selection/ | 97.2% |
| /accname/ | 0% | /quirks/ | 82.7% |

In `/css/`, the biggest absolute gaps are css-grid (about 11,300 subtests short of Chrome), css-values (6,500),
css-color (5,700), css-typed-om, css-transitions, css-animations and css-anchor-position.

**Never run yet.** Large ones, each worth a first measurement: content-security-policy, cors, cookies,
service-workers, webaudio, webrtc*, websockets, eventsource, pointerevents, touch-events, intersection-observer,
resize-observer, navigation-api, navigation-timing, resource-timing, user-timing, referrer-policy,
mixed-content, mimesniff, permissions-policy, preload, loading, import-maps, js, focus, inert, fullscreen,
clipboard-apis, scroll-animations, page-visibility, requestidlecallback, storage, web-locks, webcodecs,
webvtt, media-source, mediacapture-*, screen-orientation, visual-viewport, viewport, html-aam/aria,
x-frame-options, subresource-integrity, secure-contexts, cssom (top level), xml, webgl, webgpu.
Some need hardware or permissions (bluetooth, serial, webusb, payment, xr); skip those.

## Loop
1. Pick an area (largest gap, or a never-run directory). Run it through `lumen_wpt.py` with a monitor.
2. `report PREFIX --deep`. Group the failures in `gaps.json` by message and by test, and fix the largest group.
   Errors that also happen in Chrome are not Lumen bugs.
3. Build, run ctest, smoke-test with `jsrun`, then rerun the touched directories. Record the run here as
   "RUN N (commit): area before -> after, regressions, next leads".
4. Commit (the user wants work committed and pushed). Periodically run all of `/css/` and all of `/html/`
   to catch regressions.

## Code map for WPT work
- **JS preludes (page realm):** `src/js/prelude/*.js`, concatenated in name order.
  - `03-element.js`: inline style, CSSOM accessors, `CSS_PROP_NAMES`, colour canonicalisation in `specColor`/`setProperty`.
  - `03e-typedom.js`: Typed OM property maps.
  - `08-window.js`: `window.open`, `CSS.*` factories, `getComputedStyle`, editing glue.
- **Shared with workers:** `src/js/shared/` contains `typedom.js` (Typed OM values), `fetch.js`, `xhr.js`, `file.js` and `safecoll.js`.
  - `safecoll.js` provides `SMap`/`SSet`, tamper-proof collections. Use them in preludes instead of page-visible `Map`/`Set`; real `Map`/`Set` constructors are exported for user-visible clones.
- **Natives:** JS natives are in `src/js/jsnative.cc`, frames and `window.open` in `src/js/frames.cc`, structured clone in `clone.cc`.
  - `jsnative.cc` can't include `css.h` (an enum clash), so declare the C functions directly.
- **CSS engine:**
  - `src/css/style.c`: computed values in `computed_of`, display canonicalisation, presentational hints, transitions via `style_change`.
  - `src/css/css.h`.
  - Selectors are parsed in the native parser (pseudo-element tail rules in `pe_tail_ok`).
- **Loading:** image load/complete blocking (`js_load_blocked`) and image fetches are in `src/app/main.c`.

## Open leads
- **window.open tokenizer:** the spec tokenizer (`openFeatures`/`openBool`) made all `open-features-*` tests time out with 0 subtests (run 170). It was reverted in `b931e7d`, so `noopener` tokenization is back to 3/8.
  - The cause wasn't found. Likely the popup never navigates or messages back for some feature strings.
  - The code is in run 170's notes in the archive.
- **Run 159 regressions, not followed up:** `keyword-sizes-for-intrinsic-contributions`, `@font-face` in shadow trees, `mvs-shaping`, one anchor-position test, one calc-size test. Also `host-is-featureless` and `host-in-host-context-selector`.
- **Serialisation still to do:** nested `calc()` simplification, `clamp()` of plain values, `grid-area: auto / auto`, `text-decoration-line` order.
- **Missing features:** SharedArrayBuffer clone to workers; 31× JS error "reading 'visible'" in window-object tests; Lumen's `innerText` drops a trailing space under `pre-wrap`.
- **Timeouts:** the html timeouts (idle stalls) and same-origin `window.open` scripting.
- **Bigger features:**
  - Web Animations model: KeyframeEffect, `Element.animate` and interpolation.
  - SVG pointer-events hit testing and the SVG DOM IDL.
  - Whitespace handling in `execCommand` delete/forwarddelete/insertText.

## Android UI state (v0.3.4, `b931e7d`) and what still needs checking on the phone
- **Done:**
  - Browser menu: icon lines Back/Forward/Reload/Bookmark/Downloads, then Keyboard/Copy/Paste/Undo/More.
  - Edit bar: Keyboard, [Copy], Paste, two slots, More. The More popup has a top icon line [Cut], [Select all], Undo, Redo, Clear, Hide, with left-aligned text rows below.
  - The address field gets the same bar (Java, `BrowserMenus.showFieldBar`), replacing the system text toolbar.
  - Image long-press menu (`img_ctx_menu`/`ctx_do` in `main.c`, `ClipProvider.java`).
  - Clipboard-history thumbnails.
- **Unverified on the device:**
  - The image long-press menu. The previous build failed on images inside links; `b931e7d` adds `img_under()`. Logcat tag `LumenUi` "long press node=… img= link=" shows what was hit.
  - The thumbnail when holding Paste.
  - The Java address-field bar and its More popup.
- **Not implemented:** native Redo has no fallback stack. It relies on the page handling Ctrl+Shift+Z.

## Runs
(append here: RUN 232 ...)

RUN 231: font-family / font shorthand family validation in mathValid (03-element.js familyValid, fontFamilyPart):
items are one string or identifiers; a lone CSS-wide keyword or 'default' is invalid. /css/css-fonts/: +166, none lost
(an earlier version rejected 'default bongo' and lost 144).

RUN 230: circle()/ellipse() 'at <position>' serialization for shape-outside/clip-path (03-element.js shapePos):
specified adds 'center' to a lone offset and puts the horizontal pair first; computed gives two values, keywords
as %, lengths in px, far-edge offsets as calc(100% - X). /css/css-shapes/ + /css/css-masking/: +1162, none lost.

RUN 229: three native fixes found via test_media_queries.html (698 -> 1029/1349):
- <style>/<link> media attribute was ignored natively: style_recalc now re-checks each sheet owner's media
  (StyleSheet.media_off) and rebuilds the index when it flips (style.c).
- getComputedStyle text-decoration-line read 'none' when set via the text-decoration shorthand (computed_of
  now serializes st->text_decoration unless a longhand value is stored).
- media queries: unknown names in (name: value), bad range syntax (a < b > c, '=' in a double range, bad
  values), min-/max- on discrete features and bad aspect ratios are 'unknown' (false even under not); iframes'
  media context follows their size (frame_update -> page_viewport).
/css/mediaqueries, css-text-decor, cssom, css-cascade: +385, -52 (all in test_media_queries should_not_apply
after an iframe resize: the resize reaches the frame's media only on the next frame, not synchronously).
document-metadata, css-conditional, replaced-elements, css-transitions: +108, -1 (container
query-evaluation '(not (unknown))' now true: lead). Unit tests 104/104.
PHONE after 229 (75f4bd0): APK 92,185,964 B, verifies, installed; google 1.3% / youtube 4.8% / webgl 0.8% settled
CPU, peak RSS 544 MB, WebGL 57.6 fps, no crashes/ANRs/new errors.

RUN 228: gradient specified-value serialization in specNormalize: 'in <space>' after the direction, default
space dropped (srgb for legacy colors, oklab otherwise), xyz -> xyz-d65, 'ellipse' dropped before two sizes.
gradient-interpolation-method-valid 585 -> 1194/1398, computed 932/932 kept. Lead: remaining valid failures.

RUN 227: position-area validity + canonical serialization in the style setter (03-element.js posArea, hooked
into mathValid/specNormalize). /css/css-anchor-position/ rerun: +2058, none lost (264 files, 1 timeout).

UI WORK (4704bd2, APK built+installed, not yet released): newtab.html icon top bar + floating search; viewer zoom
steps/list/shown size; tab_select blank-frame-first for loading tabs; Android background media (SDL
BLOCK_ON_PAUSE=0, MediaPlayback.java foreground service, nativeMediaPlaying). OPEN BUG (reproduced on phone, not
on desktop): tapping a section icon in lumen://newtab?s=home does nothing until the user scrolls (desktop xvfb
scripted click works; stub test pages /tmp/nt7.html). Suspect the Android touch->click path (android_dom_touch /
touch_tap in main.c) or a missing wake/render after the click. Background-audio test and device_tests for 4704bd2
not run yet (someone was using the phone). Then: GitHub release v0.3.5-android-preview, Linux UI port, WPT.
UPDATE 2523c52: the Home tap bug was the sticky top bar (nav position:sticky): after making it position:relative,
phone taps on section icons switch at once and the search icon opens its field. Lead (WPT-relevant): Lumen's
hit testing of position:sticky boxes doesn't match where they are painted on Android. Background audio verified:
MediaPlayback$Service isForeground=true, AAudio player started 20 s after Home, service gone on return. Lead: the
main loop logged nothing for ~10 s in the background while YouTube played (playback clock may stall). Workspace
tab cards are now 160x120 (4:3), title 2 lines, URL wraps and is clipped.
RELEASED v0.3.5-android-preview (94ec7de, asset lumen-v0.3.5-android-preview-debug.apk, sha256 d08ec1ea...).
LINUX PORT: the Lumen-pages panel (panel_open/close/service) is no longer Android-only; desktop toolbar has a
menu button (three dots, HB_MENU, right end) that toggles the panel at Home; "home" is a section on desktop too;
the desktop toolbar shows the covered tab (bar_tab) while the panel is up. Test: xdotool click under xvfb
(/tmp/menutest.sh). Next Linux items: Android's toolbar slots (download, tabs/workspaces), more-menu entries.

RUN 226: testdriver delete_all_cookies() -> "cookies-clear" td op (main.c td_cmd -> cookies_clear(), cookies.c).
lumen_wpt.py now rewrites testdriver-vendor.js from .orig whenever VENDOR_JS changes (it appended only once, so
vendor edits never reached the WPT checkout). /cookies/name, value, attributes: +241, none lost. Lead: the
cookie tests also use get_all_cookies/get_named_cookie; samesite form-*-blank tests hard-time out.

LEAD (memory, measured): every removed <iframe>/<frame> keeps its whole JsCtx (own V8 context + prelude,
~2.8 MB each) until the parent page is freed: jsrun creating+removing 400 about:blank iframes grew RSS
150 MB -> 1.27 GB, ~86 ms per frame. frame_kill() (frames.cc:60) only marks dead; js_free(kid) runs only from
the parent's js_free (js.cc:1293). Freeing early is unsafe as-is: frames share the root isolate, node
wrappers (n->js) are strong Globals, and the parent may still hold the frame's window/document/nodes.
Design: at kill, make the dead frame's Globals weak (ctx, protos, fire/report/api/node_tmpl, node wrappers)
with a weak callback on its global; when V8 collects it, js_free the frame at the next safe tick. Any native
path that touches a dead ctx's handles must then tolerate empty handles. Matters for long-lived pages that
rotate ad iframes.
Tried (run 226, reverted; patch kept at /tmp/frame-park-226.patch): js_frame_park() at frame_kill moved the dead
frame's handles (api/protos/fire/... and node wrappers) onto a private array on its global, made them and ctx
weak, and freed the frame from frames_scan once a weak callback on the global fired. No crash after guarding
the js_free path, but A/B on pixel-length-attributes.html alone: peak RSS 1.53 GB with it vs 1.57 GB without.
So something else still roots dead frames (not yet found: candidates are other modules' strong Globals,
event-listener/observer storage, testharness closures holding the frames' bodies, or GC not running).
Next: count live contexts with a heap snapshot or a JsCtx counter before trying again.

Phone check after run 225 (APK 250ff4a): google/youtube/webgl/webgpu pass, no crash/ANR/tombstone/new errors;
last-10-s CPU 0.8-3.5%, peak RSS 553 MB (youtube), WebGL 57.3 fps, WebGPU 23.7 fps.

RUN 225: <frame> elements get a browsing context like <iframe> (frames.cc is_iframe, dom.c frame_version bumps)
and HTMLFrameElement has contentWindow/contentDocument. pixel-length-attributes.html no longer errors;
/html/rendering/ +604 net, none lost after a 2-job rerun of the batch losses (svg-in-iframe sizing, fieldset,
table attrs: load). Note: that 2-job rerun peaked at 1.65 GB RSS in one process (pixel-length-attributes makes
hundreds of iframes); in the 4-job batch three instances rose to 1.1-1.5 GB together for ~2 min, then fell back.
Lead: frame-heavy tests' memory use; window.open/opener tests in /html/browsers/windows/ hard-time out (11).

RUN 224: style.setProperty/CSS.supports reject an invalid gradient <color-interpolation-method> (03-element.js
gradInterpValid, called from mathValid): "in <space> [<hue> hue]" only at the start or end of the first argument,
hue methods only for hsl/hwb/lch/oklch, none in the colour stops, and no empty first argument.
/css/css-images, backgrounds, values, masking: +293, none lost.

Phone check after run 223 (APK dff6f5a): google/youtube/webgl/webgpu pass, no crash/ANR/tombstone/new errors;
last-10-s CPU 0.9-3.3%, peak RSS 553 MB (youtube), WebGL 57.7 fps, WebGPU 23.8 fps.

RUN 223: font shorthand. Specified (03-element.js fontParts/specNormalize): drops normal parts and /normal, writes
"size / line-height". Computed (computedFont): built from the computed longhands (stretch percentages back to
keywords, '' when font-variant isn't normal/small-caps). Native apply_font (style.c) now sets font-stretch keywords
and small-caps, resets both, and resolves bolder/lighter through the font-weight longhand (they were fixed
700/300, and stretch keywords were taken as the size). /css/css-fonts/, cssom, cascade, css-text/parsing,
css-inline: +739, none lost. Lead: system fonts (caption etc.) only set font-family.

RUN 222: view-transition pseudo-elements in selectors (parse.c pe_args_ok/pe_tail_ok): ::view-transition takes no
argument; -group/-image-pair/-old/-new need '*' or a <custom-ident> (not default/CSS-wide) with optional .classes, or
only .classes; the only pseudo-class allowed after them is :only-child, and not after the root ::view-transition.
pseudo-elements-invalid 228 -> 675/675; /css/css-view-transitions/ about +450 net. In the 5-job selectors run,
focus-visible-* and active-observable losses came back under 3 jobs (load-sensitive); has-complexity.html
hard-times out as before.

Phone check after run 221 (APK 9095c31): google/youtube/webgl/webgpu pass, no crash/ANR/tombstone/new errors;
last-10-s CPU 0-3.3%, peak RSS 532 MB (youtube), WebGL 57.5 fps, WebGPU 23.3 fps.

RUN 221: computed background-image gradients serialize from the declared text (gradient_ser in style.c, stored in
the Gradient's own allocation as g->ser): colors computed, lengths to px, percentages and single-unit calc()
resolved, stop positions only where given, direction keywords kept, the default ellipse/farthest-corner dropped,
"at" positions as percentages/offsets, and the interpolation method unless it's the default (srgb for legacy
colors, else oklab; xyz -> xyz-d65, "shorter hue" dropped). /css/css-images, backgrounds, values, masking, color,
cssom: +1042, 1 lost (calc-size-flex-basis-on-row, layout, unrelated). Lead: angle calc() in gradients
(calc(0.1turn + 0.15turn) -> 90deg) and canonical order of mixed calc (calc(50% + 90deg)).

RUN 220: XHTML entity doctypes (xml.c): HTML named character references also for "-//W3C//DTD MathML 2.0//EN"
(the spec's list; any public id containing XHTML still counts, as Chrome). xhtml-mathml-dtd-entity-8.htm 5 -> 2125,
/html/the-xhtml-syntax/ +2120, none lost. First runs of /webgl/idlharness: window 2065/2095 (left: a few
WebGL2 members such as FLOAT_MAT2x3, drawingBufferFormat, getter brand checks); worker 4/2095 because workers have
only a 2D OffscreenCanvas and no WebGL interfaces (lead: WebGL on OffscreenCanvas in workers).

Phone check after run 219 (APK 539584e): google/youtube/webgl/webgpu pass, no crash/ANR/tombstone/new errors;
last-10-s CPU 0-3.2%, peak RSS 543 MB (youtube), WebGL 57.5 fps, WebGPU 23.8 fps.

RUN 219: teardown crash fix. The recovered SIGSEGV in GlobalHandles::NodeSpace::Release resolved (addr2line) to
~JsCtx <- js_free <- page_free: JsCtx members still holding v8::Global handles were released by `delete c` after
iso->Dispose(). js_free now resets pick_cb (set by showPicker(), matching show-picker-does-not-focus.html) and clears
rejs (pending unhandled rejections) with the other handles. /html/semantics/forms/the-input-element/ 5 jobs: no
crash, 0 gained/lost. The crash was intermittent (19 fatal-signal-11 entries over the whole crash log).

RUN 218: (1) selectors: the legacy case-insensitive attribute values (accept, type, ...) only for HTML elements in
HTML documents (select.c match), case-sensitivity/values.window.html. (2) text controls: with no stored selection
selGet gives 0,0 (was the end; a cloned or untouched control starts at 0, a script value change still moves it to
the end); form reset puts the caret at the end of the restored value. select-event.html 180 -> 270/270. Over
/html/semantics/forms/, selectors, /css/selectors/, /dom/nodes/, /editing/other/, /input-events/, /selection/: +280,
-233 in the batch, almost all load timeouts; rerun alone the rest were form-submission files (filename/\r tests,
unrelated) and the designMode typing-around-link timeouts (known). Lead: two instances hit a recovered SIGSEGV in
v8 GlobalHandles::NodeSpace::Release while moving to the next test (after click-user-gesture.html and
show-picker-does-not-focus.html in the-input-element/): a global handle released twice or after free.

Tried after 217 (reverted): making a child frame's load wait for its images (h_load_blocked on any frame of the
loading page, js_load_recheck recursing into kids). hash-name-reference.html stayed at 84 in a batch: a frame's
images aren't in icache (no slot) before its load fires, so nothing waits. Unrestricted (any frame of any page),
it made 41 files time out. A real fix needs the child page to request its images before its load check.

RUN 217: image-map hit testing for elementFromPoint (imageMapHit in 06-document.js): an <img usemap> hit maps to
the first <area> of the map (id or name = text after the first '#') containing the point, measured from the content
box (rect/circle/poly/default); none -> the img. hash-name-reference.html 84 -> 156/168 alone, but 84 in a 5-job
batch: the img (width from its natural size) is still 0-wide when the parent's onload runs, so the hit lands on the
div. Lead: the window load event must wait for iframes, and an iframe's load for its images, laid out. Lead: real
clicks/taps target through the native hit (c->host.hit), which doesn't map areas yet
(hash-name-reference-dynamic.html 2/8). Rest of the run (cssom-view, shadow-dom): +7, none lost.

Phone check after run 216 (APK d33bab1): google/youtube/webgl/webgpu pass, no crash/ANR/tombstone/new errors;
last-10-s CPU 0-3.6% (script), peak RSS 547 MB, WebGL 57.5 fps, WebGPU 23.5 fps. YouTube had a second ~10 s busy
stretch after load and its last 10 s read 3.3-10.7% (mean ~5.4%) in the monitor: lead, check a longer settle.

RUN 216: SVG <script> runs (jsg_is_script/jsg_script_src in jsglue.c: href or xlink:href; main.c parser scripts,
js_run_inserted). Parser: an SVG script is marked started at its start tag and released at its </script> (never in
fragments), so a breakout-ended one doesn't run (html.c foreign()). Clones copy "already started" (dom.c
node_clone). The language attribute is no longer whitespace-stripped ("text/" + language compared as is).
Over /html/semantics/scripting-1/, /svg/, /html/syntax/, /dom/nodes/: +1,647, -66; html5lib tests16 (64) is flaky
(168/112/191/120/127/191 across runs; 191 alone). Left: use-iframe-onload.html (1), a speculative-parsing case (1).
Also: css-sizing keyword-sizes-on-abspos / -flex-item-002 vary 10-13 / 16-27 between runs (Ahem font timing?).

Phone check after run 215 (APK 21cd8b9): google/youtube/webgl/webgpu pass, no crash/ANR/tombstone/new errors;
last-10-s CPU 0-3.4%, peak RSS 555 MB (youtube), WebGL 57.1 fps, WebGPU 23.7 fps.

RUN 215: CSS math-function type check (mathType/mathValid in 03-element.js), used by style.setProperty (and the
e.style[x] proxy) and CSS.supports(): syntax (args, commas, "+"/"-" need spaces, unclosed at end allowed), typed
arithmetic with unit exponents (px*px/px ok), round/mod/rem/min/max/clamp/hypot same types, length+% mixing,
one-argument round only for numbers, trig/pow/sqrt/log/exp domains; a whole-value function must give a number
for opacity/flex-grow/flex-shrink (opacity also %) and a length/% for width/height/min-/max-/block-/inline-size.
Unknown functions, units, identifiers (relative-color channels) and var()/env()/attr()/anchor make it unknown (no
check). Before this, setProperty accepted any value. Over 12 CSS dirs (1,599 files): +480, -14. The 14
(css-sizing/keyword-sizes-on-abspos.html 13->10, keyword-sizes-on-flex-item-002.html 27->16) fail the same with
this change stashed: an earlier regression, still to find (the files use stretch/-webkit-fill-available/Ahem).
Lead: the stylesheet (C) parser still accepts these invalid math values.

Phone check after run 214 (APK 56d508d): google/youtube/webgl/webgpu pass, no crash/ANR/tombstone/new errors;
last-10-s CPU 0-3.5%, peak RSS 556 MB (youtube), WebGL 57.6 fps, WebGPU 23.7 fps.

RUN 214: the older node constructors (Gain, StereoPanner, Delay, BiquadFilter, Analyser, Oscillator, ConstantSource,
AudioBufferSource) take ctorCheck (BaseAudioContext + dictionary) and nodeOpts (AudioNodeOptions) too
(07w-audio.js). /webaudio/: +70, lost 0 (3492 / 5787).

RUN 213: MediaElementAudioSourceNode (mediaElement; AudioContext only; renders silence, the element keeps its own
output) and createMediaElementSource() (07w-audio.js). /webaudio/: +44, lost 1 (an audit.js "AUDIT TASK RUNNER
FINISHED" summary subtest in cors-check.https.html). Lead: route media element audio into the graph.

RUN 212: every node's AudioParams (gain, frequency, detune, Q, delayTime, offset, playbackRate, pan, compressor and
panner/listener params) are prototype accessors; the constructor's first assignment is kept, later ones ignored
(07w-audio.js, Symbol-keyed storage). /webaudio/: +164, lost 0 (3379 / 5787 by regr.py's count).

RUN 211: AudioNode (context, numberOfInputs/Outputs, channelCount with range check, channelCountMode,
channelInterpretation) and AudioParam (defaultValue/minValue/maxValue, automationRate) members are prototype
accessors, not own properties (07w-audio.js). /webaudio/: +132, lost 0. Remaining idlharness leads: subclass
attributes (gain, frequency, buffer, ...) still own properties; MediaElementAudioSourceNode missing (26).

Phone check after run 210 (APK e239e35): google/youtube/webgl/webgpu pass, no crash/ANR/tombstone/new errors;
last-10-s CPU 0-3.6%, peak RSS 551 MB (youtube), WebGL 57.7 fps, WebGPU 23.5 fps. YouTube settled CPU still
alternates 0/3.5/7/11% (median 3.5% last 10 s), the same pattern as since run 193 (~75 timers/s).

RUN 210: /webaudio/ first measured (348 files, 0 crashes): 2,235 of 5,787 Chrome-passing subtests before this run.
07w-audio.js: added ChannelMergerNode, ChannelSplitterNode, WaveShaperNode (curve), IIRFilterNode (filtering +
getFrequencyResponse), PannerNode (params/attribute checks; audio passes through unspatialized), AudioListener
(context.listener), OfflineAudioCompletionEvent, create* methods; AudioParam automation argument checks (non-finite
TypeError, negative time RangeError, exponential 0, curve length/duration); node constructors need a
BaseAudioContext; ctorCheck/nodeOpts for the new nodes (dictionary + AudioNodeOptions with fixed-count rules).
Net about +1,250 (3461+ / 5787). The reported "losses" are mostly audit.js subtests whose names embed the old
error text or "AUDIT TASK RUNNER FINISHED: N tasks", so they change name, not result. Leads: graph is stereo-only
(merger/splitter don't route channels: audionode-disconnect, channel tests), PannerNode doesn't spatialize
(the-pannernode-interface ~680), idlharness (attributes are own properties, not prototype accessors, ~650),
AudioParam automation accuracy (~700, audit.js sample-exact checks), apply AudioNodeOptions to the older nodes.

RUN 209: no execCommand undo history in designMode documents (editing host = <html>; editing.js myExecCommand). Since
run 203, snapshotting the whole document per command made
typing-around-link-element-at-non-collapsed-selection.tentative.html?target=DesignMode&parent=b&child=i hard-time out
(it passed 1/21 in all 9 earlier runs; with run 202's editing.js it passed again). Root cause of the slowness not found
(a plain designMode page with jsrun is fast); lead: undo in designMode. Also seen: in designMode,
execCommand('insertText') over a selection of <b><a><i>abc</i></a></b> returns false and leaves
<p style="height: auto; max-height: none; min-height: 0px;">...<i><br></i> (same with run 202's editing.js).
Rerun: typing-around-link (that variant) back to 1/21, undo-redo.html 69/69.

RUN 208: focusing an editing host puts the caret where Chrome does (08-window.js focus handler): after collapsible
leading white-space of the first text, before an empty inline element unless it has padding/border (then inside),
before <br>/<img>. /selection/ + /editing/other/: +50 then +26 on the rerun files; lost 2:
typing-around-link-element-at-non-collapsed-selection DesignMode variants, which hard-time out (seen under load
since run 203; recheck alone).

RUN 207: a slot assigned to an outer slot fires slotchange on that outer slot too when its own assigned nodes change
(04-tags.js slotSignal). /shadow-dom/: +1, lost 0 (14493 / 15075).

Phone check after run 206 (APK fbfb042): google/youtube/webgl/webgpu pass, no crash/ANR/tombstone/new errors;
last-10-s CPU 0-3.4% (youtube 3.3-3.4%, so the slotchange bookkeeping costs nothing visible), peak RSS 529 MB,
WebGL 57.8 fps, WebGPU 23.6 fps.

RUN 206: slots in the DOM API (04-tags.js slotFind/slotAssigned/slotFlat/slotSignal): named assignment by the slot
attribute (first slot in tree order with that name), manual assign(), assignedNodes/assignedElements({flatten}),
Element/Text.assignedSlot (null for closed roots), and slotchange (bubbles) fired in a microtask on each slot whose
assigned nodes changed, driven from notify() (01-events.js) for childList on a host or inside a shadow root, slot
attribute on a host's child, and name on a slot. Before, assignedNodes() was always [] and assignedSlot null.
/shadow-dom/ /dom/nodes/ /custom-elements/ /css/css-scoping/ /dom/events/: +125, lost 1 (fixed: a slot outside a
shadow tree flattens to []); the slot files then +61. Not yet: signalling a parent slot when a slot assigned to it
changes, slotchange for fallback-content changes.

RUN 205: ShadowRoot.elementFromPoint()/elementsFromPoint(); both document and shadow versions retarget the hit
against the root (shadow hosts, not shadow-internal nodes, from document) and throw TypeError for missing or
non-finite coordinates (06-document.js hitRetarget/hitsFrom/hitArgs). /shadow-dom/ + /css/cssom-view/: +71 then +34
on the rerun files, lost 1: getBoundingClientRect-empty-inline.html is flaky (y 89.31 vs 92; passed 1 of its last 4
runs, doesn't hit-test). Open: slotchange-event.html 0/32 (slotchange isn't fired for default slots when assigned
nodes change).

RUN 204: Selection.extend() aborts silently (before the empty check and offset validation) when the node isn't in
the document, as the spec orders it; selectAllChildren() ends at the node's child count (0 for a Text node), not its
length (06c-traversal.js). /selection/ + /editing/run/: +490, lost 0 (selectAllChildren.html, extend-00/20).

Phone check after run 203 (APK fecdb19, 92,185,964 bytes): google/youtube/webgl/webgpu pass, no crash/ANR/tombstone,
no E-level lumen lines; last-10-s CPU 0-3.5%, peak RSS 549 MB (youtube), WebGL 57.6 fps, WebGPU 23.5 fps. Only
YouTube's known EvalError + 2 Polymer warnings. The 12+12 E/AdrenoUtils + E/GraphicBufferAllocator "(4 x 4) format
56/59" lines per GPU start are driver probing noise, present since run 175's logs at least. Android build: there is
no gradlew; use the Gradle command in the build section above.

RUN 203: execCommand('undo'/'redo') (editing.js undoList/redoList: each command that changes an editing host
records its markup + selection before and after; undo/redo only act when the selection is in that host;
06-document.js lets undo/redo past the supported check and fires historyUndo/historyRedo input events). Element
innerHTML setter now updates live ranges and the selection like textContent (03-element.js setInner
rangePreRemove); before, a selection in replaced content kept pointing into detached nodes and execCommand threw
"Invalid active range". /editing/ /input-events/ /dom/ranges/ /selection/ /domparsing/ /dom/nodes/: +558 net
(undo-redo.html 35 -> 69 / 69), lost 4 after reruns: 2 in selection/shadow-dom/tentative/Selection-direction.html
were false passes (Lumen's Selection ignores points in shadow trees: collapse/extend/setBaseAndExtent return when
rootOf(n) !== document; lead), 2 typing-around-link DesignMode variants hard-time out only under load (pass alone).
Big parallel batches cause whole-file TIMEOUTs early in the run (peak 940 MB): always rerun lost files alone.

RUN 202: Selection.addRange() accepts a Range from another realm (brand check on the internal _sc instead of
instanceof; 06c-traversal.js). /editing/run/undo-redo.html 1 -> 35 / 69 (no longer times out); /selection/ +1,
lost 0. Lead: the remaining undo-redo failures are undo not restoring the content after insertParagraph & co.
The only undo history is the edit bar's (08-window.js undoStack: per-host innerHTML snapshots taken on input
events, 'lumenundo'); execCommand('undo'/'redo') needs its own per-document transaction history (snapshot the
host's markup + selection before each execCommand, restore on undo, redo stack cleared by new edits).

Gap ranking after run 201 within /editing/ (Chrome-passing subtests Lumen misses): whitespaces/chrome-compat 387
(12 files), exec-command-with-text-editor.tentative.html textarea/text/password 112/94/84, plaintext-only/
styling-commands.html (both styleWithCSS) 104+104, run/multitest.html?9001-last 102, run/undo-redo.html 68 (1/69),
insertlinebreak-with-white-space-style?pre-wrap 42. /editing/ as a whole is now 94.3%; the bigger gaps are
elsewhere (CSS grid ~11k first).

RUN 201: deleting the last content of an inline editing host no longer leaves a <br> in it (editing.js delete
algorithm's "block node of parent has no visible children" step skips inline editing hosts, as Chrome).
inline-editing-host-has-placeholder-with-{before,after}-pseudo-element pass again; /editing/run/delete,
forwarddelete and /editing/other/: +8, lost 0. Still open from run 200's list: insertparagraph.html?6001-7000
("a&nbsp; " keeps a trailing plain space where Chrome makes "a&nbsp;&nbsp;"), typing into a text control that is
also an editing host, edit-in-textcontrol-immediately-after-hidden, plaintext-only paste into <b>.

RUN 200: editing join of a line into a following paragraph with another white-space (editing.js, the "end block
is a descendant of start block" branch): as Chrome, only the first line moves into the white-space <span> (also
when the line ends at a preserved \n inside a moved text or a <br> inside an inline), the line break goes, and the
<br> before the span is removed. /editing/: +2716 (join-different-white-space-style-left-line-and-right-paragraph
variants now pass), 101846 / 108016. 16 losses there are NOT from this change (same with HEAD's editing.js) but
from an earlier run since the last /editing/ measurement; lead: edit-in-textcontrol-immediately-after-hidden,
inline-editing-host-has-placeholder-with-{before,after}-pseudo-element (an empty host gets "<br>"),
input-in-text-control-which-is-also-editing-host (typed text becomes a child of the control),
insertparagraph.html?6001-7000 ("a&nbsp; " trailing space), plaintext-only/paste.https.html?white-space=pre-wrap.
Hard timeouts: delete-in-child-of-{head,html}.tentative.html, delete-in-shadow-hosted-in-body.html.

RUN 199: stream*HTML: options, Trusted Types and the sanitizer config are settled when the method is called
(htmlStream passes a precomputed cfg through htmlForInsertion). The Unsafe sinks in a require-trusted-types
document now get their parser options from the default policy's createParserOptions(options,
'TrustedHTMLParserOptions', sink) (htmlArgs; sink "Element|ShadowRoot|Node method"). /domparsing/ +
/trusted-types/ + /sanitizer-api/: +428, lost 1 (trusted-types-navigation.html?26-30, which timed out in 3 of its 4
recorded runs: flaky). Peak RSS 1.13 GB per process in this run.

Phone check after runs 194-198 (APK 1034e4b): google/youtube/webgl/webgpu pass, no crash/ANR/tombstone, settled CPU
0-3.7%, peak RSS 533 MB (youtube), WebGL 57.6 fps, WebGPU 23.7 fps; only the known YouTube EvalError and Polymer
warnings.

RUN 198: element/ShadowRoot/CharacterData stream{,Append,Prepend,Before,After,ReplaceWith}HTML(Unsafe)()
(07k-sanitizer.js htmlStream): a WritableStream that buffers chunks and applies the matching method on close;
Trusted Types/options checked at call time. /domparsing/ + template-element/ + /html/dom/partial-updates/:
+795, lost 30 (they passed only because the method was missing). Leads: the sanitizer config must be resolved
(and an invalid one throw) at call time, not at close (stream-html-sanitizer.html "mutating the sanitizer while
streaming", "invalid sanitizer config"); TT checks for elements adopted between TT and non-TT documents
(stream-html-with-trusted-types-adopt/from-non-tt); real incremental parsing into the target.

RUN 197: history traversal within a document fires hashchange after popstate when only the fragment changed
(08-window.js histUrls per entry). /html/browsers/history/: +2 net in the full run; history_pushstate_url.html
passes alone (its hard timeout and the 4 url_rewriting losses were flaky: both pass on rerun). 48 TIMEOUTs left in
/html/browsers/history/ (location-protocol-setter.html hard timeout, cross-document traversal).

RUN 196: harness fix: PRE_JS reported r.url = location at report time, so tests that pushState() reported under
the wrong URL and hit the hard timeout. It now sends the URL captured at load (U0). DOMParser pushstate tests:
0 -> 54 / 90. Rerun of history/the-history-interface/, the-location-interface/ and other pushState tests: +31,
lost 0 (190 / 478; 47 TIMEOUTs left there, e.g. history_pushstate_url.html hard timeout: next lead).

RUN 195: beforeHTML/afterHTML/replaceWithHTML(Unsafe) accept a template's content fragment as the parent
(07k-sanitizer.js htmlParent; new N.tplHost = inert template-content fragment; a plain DocumentFragment still
throws HierarchyRequestError). /domparsing/ + the-template-element/: +171, lost 0. Pre-existing hard timeouts:
DOMParser-parseFromString-url(-base)-pushstate.html (pushState to /fake/... seems to navigate for real).
Next leads: positional-methods-with-parser-options (createParserOptions / Trusted Types), stream*HTML methods.

RUN 194: <template for> (declarative partial updates), html.c template_for() at </template>: first <?marker name=N?>
gets the content inserted, or <?start name=N?> replaces up to its closing <?end?> (nested starts counted, end names
ignored) or to the end of the parent; markers and the template removed; empty name does nothing. template-for/
29 -> 235 / 463 (+198 net); html5lib losses in the same run were flaky (314/314 on rerun). Lost 1:
template-for-scripting-nested (the placeholder's script is removed before it runs). The remaining ~230 need
Chrome's streaming model: the target is cleared at the <template for> START tag and following content parses
directly into the target (append/prepend/gradual/move-while-appending tests, scripts inside patches executing,
innerHTML/shadow/setters). Also element.streamAppendHTMLUnsafe() (207 subtests) is missing.

NEXT LEAD (run 194): /html/semantics/scripting-1/the-template-element/template-for/ (434 short, 6.3%): WICG
declarative partial updates. `<?start name="p"?>` (and end markers) must survive parsing as processing-instruction
markers (the HTML parser makes them bogus comments today), and a parsed `<template for="p">` moves its content to the
marker's position instead of staying a template. Finding: html.c already tokenizes `<?start name="p"?>` into PI nodes
(pi_or_bogus -> T_PI -> node_new_pi), so only the tree-builder side is missing: on `</template>` of a template with
a `for` attribute, find the matching start PI, insert the template content there and drop the marker and the
template (expected e.g. "Some<span> </span><span>content</span>" from template-for-append-element.html). Then json/text-module leftovers, css-grid (~11k).

Phone check after runs 191-193 (APK a4c3432; the first build failed because Android's V8 15.0 lacks synthetic-module
host_defined_options, fixed with a per-isolate table): google/youtube/webgl/webgpu pass, no crash/ANR/tombstone,
settled CPU 1.1-3.5%, peak RSS 557 MB, WebGL 57.6 fps, WebGPU 23.7 fps; only the known YouTube EvalError.
Lead: YouTube settled CPU is spiky (median 3.6%, many 10-30% samples; ~75 50-ms timers/s from the page).
NOTE: host V8 is 15.6, Android V8 15.0: guard newer V8 APIs with V8_MAJOR/MINOR_VERSION.

RUN 193: import attributes and worker import(). js.cc: `with { type: 'json' | 'text' | 'css' }` builds V8 synthetic
modules (value in host_defined_options; JSON needs a JSON MIME type, CSS text/css -> constructed CSSStyleSheet);
module-map key = URL + '\x01' + type (mod_mkey/kurl/ktype); unsupported attribute key -> SyntaxError, type ->
TypeError. worker.cc: wm_load + wm_dynamic (import() was "Not supported" in every worker; it now loads the graph
in a microtask; service workers reject). json-module/ text-module/ module/ css-module/ /workers/modules/: +620,
lost 5 (they passed only because import() always rejected): data:-URL cross-origin dynamic import and CSP
script-src for worker import() are not enforced; JSON parse-error file location; base-url-worker-importScripts.

RUN 192: media queries: unknown features make the whole query false (even under `not`; parse.c `g_mq_unknown`);
boolean context true only for features with a non-none value (list in media_feature); inverted-colors,
prefers-reduced-data/-transparency values. /css/mediaqueries/ +9 net, /css/cssom/ +2, update-the-source-set
"not all and (!)" fixed. Remaining 1 lost: none.

RUN 191: innerText follows the rendered-text steps (03-element.js `innerTextOf`): computed display (none, contents,
block-level, table-cell tabs, table-row breaks), white-space collapsing per value (pre-line keeps breaks),
visibility, text-transform, required line-break counts; unrendered elements give textContent. innertext dir
33.9% -> **81.1%** (+604), /dom/nodes/ no change; lost 1 (<select><optgroup><option> contents: SELECT children are
skipped, lead).

Phone check after runs 188-190 (APK 2c5bb00, srcset now picks by dpr): google/youtube/webgl/webgpu pass, no
crash/ANR/tombstone, settled CPU 0.8-3.8%, peak RSS 549 MB (youtube, +15 MB vs before), WebGL 57.5 fps, WebGPU
23.6 fps; logcat only known probe lines and the YouTube EvalError.

RUN 190: srcset/sizes/<picture> source selection. `css_img_source()` (style.c) parses srcset (spec descriptor rules),
evaluates sizes (media conditions via css_media_matches, lengths via css_parse_length; default 100vw) and <picture>
<source> media/type, and picks the smallest density >= dpr (else the densest). main.c reads image sources through
`img_src()` (fetch, layout size / density, load events, load blocking); N.imgSrc backs currentSrc/complete; srcset,
sizes and media changes bump resource_version. the-img-element/ + the-picture-element/: +683 vs before (lost 11, of
which lazy-in-scroller-nested-4/5 flaky and the media="," case fixed). Leads: css_media_matches matches invalid
queries such as "all and (!)" (2); src-then-lazy-load.html (6); real sites on the phone now fetch dpr-sized images.

RUN 189: BMP decoding (`dec_bmp`, src/paint/image.c): core/info/V4/V5 headers, 1/2/4/8-bit palettes, 16/24/32-bit
with BI_BITFIELDS/ALPHABITFIELDS masks; RLE unsupported. The referrer-policy image tests get their result as a BMP.
/referrer-policy/gen/{req.attr,top.meta}/ + the-img-element/: +212, lost 0 (7 flaky, recovered on rerun); one hard
timeout (image-loading-lazy-base-url-2.html). Biggest the-img-element gap: sizes/ "ref currentSrc was ''" (858).

RUN 188: activating an SVG <a> resolves its href attribute (a.href is an SVGAnimatedString, which navigated to
"[object SVGAnimatedString]"), 06b-activate.js. No WPT gain yet (svg-a-tag referrer tests still time out: the
synthetic click on an SVG <a> doesn't reach `activate`, lead); 9 lost in iframe-tag/xhr tests that don't touch
this path (61 TIMEOUTs in the run); rerun alone: 36/36, all 9 recovered (flaky).

Phone check after runs 183-187 (APK 8e154ba): google/youtube/webgl/webgpu pass, no crash/ANR/tombstone, settled CPU
0.8-4.1%, peak RSS 534 MB, WebGL 57.8 fps, WebGPU 23.7 fps; logcat only known probe lines and the YouTube EvalError.

RUN 187: module script fetches (`mod_fetch`, js.cc) apply the default referrer policy through `url_referrer()`
(dom.c; no document there, may run on the loader thread) instead of always sending the full URL. /referrer-policy/gen/
+ script-element/module/: +149, lost 1 (module/referrer-unsafe-url.sub.html: the element's referrerpolicy and the
document policy don't reach msrc_get yet: lead).

RUN 186: document images (`sync_images`, main.c) send the policy's Referer (`doc_referrer`, element referrerpolicy).
No WPT change (iframe.http-rp + req.attr 738/1,110, lost 0): the img-tag tests decode the referrer from the image's
pixels via canvas and fail earlier ("[object Event]" = the Image's error event for `new Image()` loads, which don't go
through sync_images; iframe img-tag times out). Next: trace common/security-features/resources/common.sub.js
requestViaImage in Lumen; svg-a-tag "Failed to construct 'URL'" (SVGAElement.href is an SVGAnimatedString);
script-tag-dynamic-import referrer (mod_fetch in js.cc still sends the full URL).

RUN 184: first full /referrer-policy/gen/ (961 tests, 1,765 s, 82 TIMEOUT, no crashes, peak RSS 443 MB): **67.8%**
(4,772 / 7,042). Leads: img-tag requests (no Referer policy for images: 413 + 354 "[object Event]"), svg-a-tag
("reading undefined", 344), shared workers ("Failed to construct 'Worker'", 272), svg-a-tag URL (155), top.meta
document.referrer (133).

RUN 185: streams.js underlying source/sink/transformer dictionaries are null-prototype (`d = { __proto__: null }`),
so Object.prototype accessors (start/type/size/highWaterMark) don't break body streams. /fetch/api/basic/ + /streams/ +
/encoding/streams/ + /compression/: +359, lost 0 (scheme-about.any.serviceworker.html is flaky: 7 subtests flip).

RUN 183: referrer policy. Documents keep their Referrer-Policy header (`Document.referrer_policy`, frames.cc/main.c;
third element of N.csp()). `doc_referrer()` (dom.c) implements "determine request's referrer" for the native
iframe/script/module loads (element `referrerpolicy`, then `<meta name=referrer>` in the head, then the header, then
strict-origin-when-cross-origin; before, the full URL was always sent). JS fetch uses `FETCH_RPOLICY()` (07b-net.js)
for the same default. /referrer-policy/ + /fetch/api/{request,basic}/ + document-metadata: +133, lost 0 (7 flaky,
recovered on rerun).

RUN 182: src/js/lazy/temporal.js is now the polyfill's *full* global build (all non-ISO calendars; 220 KB vs 190 KB).
test262 intl402/ 53.2% -> **98.4%** (+1,513), built-ins/Temporal/ unchanged 99.8%; no crashes, peak RSS 367 MB.

Phone check after runs 179-181 (APK 5eb81c0, libmain.so contains the Temporal polyfill): google/youtube/webgl/webgpu
pass, no crash/ANR/tombstone, settled CPU 1.3-3.8%, peak RSS 547 MB, WebGL 57.6 fps, WebGPU 23.7 fps; logcat only the
known gralloc/AdrenoUtils format probes (12 per cold start) and the YouTube EvalError.

RUN 181: Temporal. The V8 prebuilt (15.6) is built without Temporal (`--harmony-temporal` has no effect), so
temporal-polyfill 1.0.6 (MIT, global build, vendored unmodified as src/js/lazy/temporal.js, ~190 KB) is compiled on a
page's first `Temporal` access (accessor in 08-window.js; `Date.prototype.toTemporalInstant` stub loads it too). It
also installs a Temporal-aware Intl.DateTimeFormat (only once loaded). test262 built-ins/Temporal/ 0% -> **99.8%**,
intl402/ -> 53.2%, together +5,144, lost 0; no crashes, peak RSS 364 MB. Uses `new Function`, so it fails on pages
whose CSP forbids eval (same as the lazy editing engine): lead.

RUN 180: window.postMessage detaches transferred ArrayBuffers in the sender (`xferBuffers` in 08-window.js, via
ArrayBuffer.prototype.transfer() after structuredClone), which is test262's $262.detachArrayBuffer. test262
DataView/ArrayBuffer/TypedArray + /html/webappapis/structured-clone/ + /webmessaging/: +152, lost 0.

RUN 179: event/MutationObserver internals append with `apush` (01-events.js, defineProperty, no [[Set]]), so index
getters a page puts on Array.prototype don't break dispatch. test262 built-ins/Array/ +22, /dom/events/ no losses
(80.2%). More prelude `.push` sites (postMessage, structured clone) may need the same.

Phone check after runs 176-178 (APK 210712f): google/youtube/webgl/webgpu pass, no crash/ANR/tombstone, settled CPU
1.3-4.1%, peak RSS 555 MB, WebGL 57.5 fps, WebGPU 23.5 fps (unchanged), only the known YouTube EvalError.

RUN 178: dynamic import() of a module still instantiating/evaluating (self import, cycles) no longer re-enters
v8::Module::Evaluate (V8 fatal check, SIGTRAP): `mod_eval` (js.cc) returns a resolved promise and the running
evaluation finishes it. test262 language/expressions/dynamic-import/: 12 CRASH -> 0, **99.8%** (830/832).

Phone check after run 175 (APK with color.c): google/youtube/webgl/webgpu pass, no crash/ANR, settled CPU 0.8-3.7%,
peak RSS 552 MB, WebGL 57.6 fps, WebGPU 23.5 fps (unchanged). Logcat: 12 gralloc 4x4 format-probe errors per cold start
(driver probing, harmless), the known YouTube EvalError.

RUN 176: first measurement of /third_party/test262/ (53,619 tests, 6,401 s at ~8.4 tests/s, copied binary):
**85.9%** of Chrome-passing (44,880 / 52,258). 160 TIMEOUT, 12 CRASH. Leads:
- CRASH: all 12 in language/expressions/dynamic-import/ (eval-export-dflt-*, eval-self-once-*, imported-self-update):
  V8 fatal "Check failed: module_status == kLinked..." in v8::Module::Evaluate called from Lumen's
  HostImportModuleDynamically callback (evaluates a module that is not linked yet, likely a cycle/self import).
- Prelude code breaks when tests put getters on Array.prototype indices ("Cannot set property 1 of [object Array]
  which has only a getter", "reading '__ls'" in 01-events.js invoke): event/postMessage paths use plain arrays.
- $262.detachArrayBuffer is postMessage(null, '*', [buf]): Lumen's transfer doesn't detach (DataView/ArrayBuffer
  "Expected a TypeError but got a RangeError", resize coerced-new-length-detach).
- RegExp/prototype/Symbol.split/last-index-exceeds-str-size: RSS to 2.85 GB, then hard timeout.

RUN 177: CSS Values 4 math functions in native lengths (`math_fn` in style.c: round (strategies)/mod/rem/abs/sign/
sin/cos/tan/asin/acos/atan/atan2/pow/sqrt/hypot/log/exp, pi/e/infinity/NaN, rad/grad/turn units); single math
functions compute to values for `scale`/`rotate`, and `z-index` evaluates them. `/css/css-values/` 34.6% -> **43.1%**
(+838). Lost 5: calc-infinity-nan-computed (3, infinity/NaN in width), random-serialize (1), typed_arithmetic
`sign(exp(-1vh / 0px))` (1). Remaining big css-values gaps: calc-size()/interpolate-size animation (~1,600, needs
layout), if() conditionals (199), random() (153), attr() types (116).

RUN 175: CSS colours in double precision. `/css/css-color/parsing/` 78.7%* -> **89.6%** (+1,240 against the first
175 measurement; earlier native colours: about 4,200 short), `/css/css-color/` animation unchanged (59.6%).
- New `src/css/color.c`/`color.h`: CSS Color 4/5 parser (hex, named + all system colours, rgb/hsl/hwb, lab/lch/oklab/oklch,
  color() incl. display-p3-linear, relative colours with channel keywords and calc()/min/max/clamp/round/..., color-mix()
  in any space with hue methods, 1 or 2 colours, `none`, carried-forward analogous components, powerless hues,
  alpha(from ...), contrast-color(), light-dark()), conversions via XYZ D65, computed serialization.
- `css_parse_color` (style.c) now uses it (no 128-byte limit; color-mix no longer sRGB-only). Computed `color` and
  `background-color` keep a full serialization (`ComputedStyle.color_ser`/`bg_ser`, atoms) when 8-bit sRGB can't express it.
- `N.cssColor(str)` validates/serializes for JS; `specColorFn` (03-element.js) gives Chrome's specified form of
  relative colours and color-mix() (nested colours specified-serialized, rgba->rgb, mix percentages normalized);
  absolute lab-family specified values normalized (alpha %, L clamp).
- Tried: transitions between non-legacy colours in OKLab (06d-anim.js `waMix`) -> color-interpolation.html threw
  "Cannot read properties of undefined (reading '0')" (test or later code reads tokens), reverted; lead.
- Remaining regressions: system-color-consistency dark scheme (12), `sign(100em - 1px)` in a mix percentage (1).

RUN 174: `/editing/other/` 42.1% -> **54.5%** (+1,890; `/editing/run/` unchanged at 98.5%), the-script-element 30.1% -> **43.4%** (+432).
- `src/js/lazy/editing.js`: joining sibling blocks of different white-space (the `whiteSpaceSpan` case) moves only the
  first line (`firstLineBreak`: first `<br>` or significant `\n`), removes that break and leaves the rest in the end
  block, as Chrome (`join-different-white-space-style-paragraphs.html`, 60 variants).
- `jsg_classic_script` (`src/js/jsglue.c`): the spec's type string: ASCII-whitespace-stripped exact essence match of
  the 16 JavaScript MIME types, `language` gives `text/` + language for HTML scripts, `type=" "` does not run.
- Never-run measurement (`/referrer-policy/ /mimesniff/ /user-timing/ /cookies/ /websockets/ /scroll-animations/
  /content-security-policy/ /webaudio/ /pointerevents/ /resource-timing/`, `--binary lumen-wpt`) stopped at 1236/4213:
  `/referrer-policy/4K+1/gen/top.http-rp/*` all hard-time out (about 90 s each, browsers idle); skip that directory
  (`--skip`) when resuming. CSP `form-action/*-target-blank*` and cookies `form-*-blank` also hard-time out.
- Phone check of runs 173–174 (debug APK, apksigner verifies, `install -r`, `device_tests.py google youtube webgl
  webgpu`): no crash/ANR, settled last-10-s CPU 0–3.7%, peak RSS 540 MB (youtube), WebGL 57.9 fps, WebGPU 23.5 fps
  (same as before; lead), one YouTube `EvalError: Code generation from strings disallowed` (seen before).
- Next leads: css-color computed values (needs a float-precision colour model in `src/css/style.c`: computed colours
  are 8-bit `Color`, `css_parse_color` truncates at 128 bytes, `color-mix` mixes only in sRGB (Tailwind v4 uses
  `in oklab`), relative colours unsupported; WPT fuzzy-compares numbers at 0.01 in the colour's own space, about
  3,500 subtests); popover-focus (Tab into popovers); JSON/text module tests reject with "Error: Not supported".

RUN 173: the popover API. `/html/semantics/popovers/` 25.0% -> **95.2%** (+2,741), interactive-elements 46.6% -> 49.5%,
`/html/` 81.6% -> **83.4%**. Phone check of runs 171–172 first (APK built, `device_tests.py google youtube webgl
webgpu`): all settled <= 3.5% CPU, peak RSS 540 MB, no crashes; WebGPU anim 23.7 fps vs WebGL 57.8 (not flagged).
- New `src/js/prelude/04p-popover.js`: show/hide/toggle popover algorithms, showing auto and hint lists, topmost popover
  ancestor, light dismiss (trusted pointerdown/up, hooked at the end of `dispatch` in `01-events.js`), Escape closes the
  topmost popover (else the last modal dialog gets a close request), coalesced toggle events, popovertarget activation
  (`06b-activate.js`), `toggle-/show-/hide-popover` commands, popover attribute changes hide (`ceAttr`), showModal hides
  popovers. Visibility is the native flag `NF_POPOVER_OPEN` (`N.popover`), matched by `:popover-open` and cleared on
  disconnect (lists drop such entries lazily). UA sheet: spec `[popover]` rules (fixed, inset 0, fit-content, border).
- Reflection: `popover` enum; nullable enum setters take `undefined` as null (WebIDL).
- Left: popover-focus (sequential Tab navigation into popovers), top-layer nesting tests time out, `overlay` property.
- Never-run first measurement started (referrer-policy, mimesniff, user-timing, cookies, websockets, scroll-animations)
  but stopped at 62/2571: 0.15 tests/s, many timeouts. testdriver-vendor lacks `delete_all_cookies` (cookies tests).

RUN 172: fixed run 171's `url-base-about` regression: `documentBaseURI` uses the document's own URL (`N.url()`) for
non-window documents without `__url` (DOMParser), the inherited base only for the window document. `/domparsing/`
57.3%, both subtests pass. The two `DOMParser-parseFromString-url*-pushstate` tests still hard-time out (lead).
Build clean, ctest 104/104, monitor: peak RSS 407 MB, no crashes.


RUN 171 (uncommitted -> this commit): `/html/dom/` 67.5% -> **94.5%** (+19,113 Chrome-passing subtests),
`/html/the-xhtml-syntax/` 0% -> **91.7%** (+23,486), `/dom/nodes/` 99.0%, `/domparsing/` 56.9% -> 57.2%. Net +42,609.
- `Document.getElementById` always searched the realm's own document (`N.byId` ignored `this`), so it returned null
  in every DOMParser/createHTMLDocument document. `N.byId(id, doc)` now takes the document node.
- Attribute values can hold U+0000: the setAttr natives store it as C0 80 (`aval` in `jsnative.cc`) and `jstr` turns
  C0 80 back into U+0000 (no valid UTF-8 contains it). Text nodes already kept NULs through their length.
- New `src/js/prelude/05r-reflect.js`: spec reflection types (string with TreatNullAs, url with document-URL default,
  boolean, long, limited long, unsigned long, limited unsigned long with/without fallback, clamped, double, limited
  double, enum with missing/invalid defaults, nullable, `''` keyword) in a per-interface table. It replaces accessors
  the `reflect*` helpers made (tracked in `REFLECTED`) and adds missing ones; hand-written accessors stay.
  Global `dir`/`enterKeyHint`/`inputMode` are enums on HTMLElement; `popoverTargetAction` added (popover API itself
  is still a no-op stub: `showPopover() {}` in `03-element.js`; `:popover-open`, top layer and toggle events missing).
- tabIndex parses with the HTML integer rules (`-0` -> 0, no NBSP skipping).
- Regression: `DOMParser-parseFromString-url-base-about` 2 subtests (fixed in run 172).
- Leads: remaining reflection fails (tokenlists, input.type, customGetter ones); `/domparsing/tentative/` 6.9%
  (streaming HTML APIs); render-blocking and partial-updates timeouts; the two pushstate DOMParser tests hang.

