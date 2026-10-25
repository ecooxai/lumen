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
  - Number the runs. The next run is **198**.
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
(append here: RUN 198 ...)

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

