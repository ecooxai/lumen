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
  - Number the runs. The next run is **171**.
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
| /css/ (8030 tests) | 99,365/186,576 **53.3%** | /html/ (8917) | 83,756/154,909 **54.1%** |
| /editing/ | 89.8% | /svg/ | 68.9% |
| /fetch/ | 71.8% | /mathml/ | 31.5% |
| /domparsing/ | 38.8% | /web-animations/ | 54.9% |
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
(append here: RUN 171 ...)
