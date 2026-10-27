# Lumen: handoff (2026-10-27, branch `devin-opus5.5/wpt-continue`, last code commit `dc1a7b8`, nothing pushed)

**Goal:** Lumen should pass the Web Platform Tests that Chromium passes, without abnormal CPU or RAM use. Fix the biggest
gaps first, rerun, and record every run here. After WPT is much closer to Chrome, compare Lumen and Chromium with 10
tabs (2 ChatGPT, 2 YouTube, 1 Google, other top sites): record CPU/RAM and video. Target: about 50% less RAM and 30%
less CPU than Chromium with the same functionality. The JS engine and CSS/DOM code are shared by host and Android, so
WPT runs on the host build.

Older notes, read on demand:
- `agents/archive/continue-2026-10-27.md`: WPT runs 171–308 with per-run findings (anchor positioning, media
  queries, sizes, iframes, accname, ...). Search it for a test name before working on it.
- `agents/archive/wptcontinue-2026-10-25.md`: WPT runs 100–170.
- `agents/archive/CONTINUE-2026-10-25.md`: Android, GPU, video, ChatGPT, x.com and build history up to v0.3.3.

## Rules (short form of AGENTS.md)
- **Builds:** run every compile and test through `python3 tools/codex_compile.py COMMAND ARGS...`, with no bash wrapper.
  Use at most 6 compiler jobs. Gradle needs `--no-daemon`. Never restart the V8 source build.
- **Subagents:** builds, test runs, deploys and computer-use go to a fresh Haiku 5.5 subagent (`effort: high`). Its prompt
  must be self-contained and must require CPU/RSS sampling every second. Code stays in the main agent. Don't edit
  sources while a build runs.
- **Privacy:** never type into page inputs, describe third-party pages only structurally, and delete screenshots
  that show personal content.
- **Host safety:** don't touch PID 837634 (another Lumen from `/mnt/p3/project/browser/lumen/output/...`) or
  `lumen-wpt` processes of a running WPT run. Delete nothing in `/tmp` that you didn't create without asking. `/tmp` is a
  4 GB tmpfs. When it filled up on 2026-10-27, ctest `download` and `code_cache` failed; the user approved a cleanup,
  leaving 3 GB free.
- **Phone:** never `pm clear`, uninstall or `pkill -f`; install with `adb install -r`. Don't modify
  `build/codex-android-deps/SDL`.

## Commands
- **Host build:** `python3 tools/codex_compile.py cmake --build build/codex-native -j 6`.
- **Unit tests:** `python3 tools/codex_compile.py ctest --test-dir build/codex-native` must stay at 104/104.
- **Prelude syntax check:** `python3 tools/generate_embedded.py --root . --output-dir /tmp/embchk`, then join the
  `R"JS(...)JS"` parts of `/tmp/embchk/prelude.inc` into one file and `node --check` it. Prelude files are
  concatenated into one scope. `window` doesn't exist until `08-window.js`, so earlier files must use `G`. A
  duplicate top-level name breaks the whole prelude.
- **New shared JS module** (`src/js/shared/NAME.js`):
  - Page: add a `/*@shared NAME.js*/` marker in the prelude.
  - Worker: add `#include "NAME_shared.inc"` between `)JS"` / `R"JS(` in `src/js/worker.cc`.
  - Add `"${EMBEDDED_DIR}/NAME_shared.inc"` to both embedded lists in `CMakeLists.txt`.
- **One WPT test:** `python3 tests/wpt/wpt_one.py --timeout 60 /path/test.html` prints status, pass count, FAIL lines and
  CPU/RSS. Add `--binary /tmp/lumen-old` to run the last committed build. Copy the build there after every commit.
- **WPT runs:**
  - Command: `cp build/codex-native/lumen build/codex-native/lumen-wpt && python3 tools/codex_compile.py python3
    tests/wpt/lumen_wpt.py run --jobs 2 --rerun --binary build/codex-native/lumen-wpt DIR/ ... > /tmp/wpt-runN.log`.
  - Only one run at a time (controller port 8790).
  - Results append to `/mnt/p3/project/browser/wpt-data/lumen/lumen.jsonl` (`{"url","status","subtests":[[name,status,msg]]}`).
  - Per-URL comparison scripts: `/tmp/cmp30*.py`, args = jsonl line count before the run.
  - Instance logs: `/tmp/lumen-wpt/inst*.log`. Grep them for segfault|SIGSEGV|SIGABRT|double free|corrupted|invalid pointer.
- **Report vs Chrome:** `python3 tests/wpt/lumen_wpt.py report` rewrites `wpt-data/lumen/report.md` and `gaps.json`
  (Chrome baseline `wpt-data/chrome-base.json`, Chrome 158 canary). Use the "Lumen of Chrome-passing" column.
- **Probe pages:** put temporary testharness pages in `/mnt/p3/project/browser/wpt/_probe/` and print values with
  `assert_equals(JSON.stringify([...]), '')`. Delete the directory afterwards.
- **Display:** the user's X display is `:0`. Launch with `DISPLAY=:0 setsid nohup build/codex-native/lumen >
  /tmp/lumen-display.log 2>&1 &`. On 2026-10-27, PID 3573961 ran build `57335dc`: idle CPU ~1%, RSS ~580 MB with the
  restored ChatGPT session.
- **Android APK:** see `agents/archive/continue-2026-10-27.md` (Commands section). Phone: `adb -s 2d4f6bc0`.

## Standing (2026-10-27, latest result per test, 83,532 tests; may include run 309, the uncommitted perf build)
- **Lumen:** 2,040,063 / 2,183,858 subtests passing (93.4%).
- **Chrome:** 97.9%.
- **Lumen of Chrome-passing:** 94.1%.
- **wpt.fyi-style score:** Lumen 82.4%, Chrome 96.3%.
- **Statuses:** OK 80,599, TIMEOUT 2,300, ERROR 617, CRASH 7.
- **Biggest gaps:** `/css/` 62.7% vs Chrome 95.6%, `/html/` 88.1% vs 97.7%, `/uievents/` 35%, `/performance-timeline/` 18%,
  `/webmessaging/` 59%, `/svg/` 75%, `/fetch/` 74%.
- **By CSS directory** (failing subtests): css-grid ~10.7k, css-values ~5.5k (calc-size interpolation), css-text
  ~3.3k, css-gaps ~3.1k, css-flexbox ~3k, css-sizing, css-transforms, css-align, css-backgrounds, css-fonts,
  css-anchor-position ~2.1k.
- **Resource use in WPT runs:** peak RSS per instance is usually 200–450 MB. The worst peaks were 925–946 MB in
  iframe- and form-heavy directories. CPU summed over 2 instances: median ~50%, peaks ~200%. No runaway loops found.

## Uncommitted work (check first)
- **Shared performance timeline:** new `src/js/shared/perf.js`, plus edits to `src/js/prelude/08-window.js`,
  `src/js/worker.cc` and `CMakeLists.txt`.
  - It adds real `Performance`, `PerformanceEntry`, `PerformanceMark` and `PerformanceMeasure` classes, spec
    `measure()` argument rules, and `PerformanceObserver` validation. The observer callback runs in a task and gets
    `droppedEntriesCount`. Workers now have all of these; before they had a stub `performance`.
  - Built and focused-tested: `po-observe` 6/6 (page and worker), idlharness 40/58 (page) and 38/58 (worker),
    user-timing `mark-errors` 10/10, `measure-with-dict` 2/2.
  - **WPT run 309 result** (923 URLs, `/tmp/wpt-run309.log`, `/tmp/cmp309.py`): +1267 PASS subtests (1347 -> 2614):
    performance-timeline +168, user-timing +812, navigation-timing +144, event-timing +59, resource-timing +34,
    hr-time +33, workers +7. No crashes or memory errors; ctest 104/104.
  - **One real loss to fix before committing:** `buffered-flag-with-entryTypes-observer.tentative.any.html` 1 -> 0.
    `observe({entryTypes, buffered: true})` must ignore `buffered`; in `perf.js` `observe()` drop the
    `|| o.buffered !== undefined` part of the "both entryTypes and type" check.
  - `droppedentriescount.any.worker.html` now times out (0/5 before and after): workers produce no resource-timing
    entries, so the test waits. Not a subtest loss.
  - Then rerun `/performance-timeline/` and `/user-timing/`, commit, and `cp build/codex-native/lumen /tmp/lumen-old`.
  - **Memory:** `/hr-time/clamped-time-origin.html` peaks at ~2.9 GB RSS on old and new builds alike (not a
    regression). Worth investigating: it's the largest single-test RSS seen.

## Done this session (commits `4b2141e`..`dc1a7b8`)
- **Anchor positioning:**
  - `position-try-order`.
  - `inset: unset/initial/inherit`.
  - Comments and newlines in `position-try` lists.
  - `position-try-order-basic` and `-position-area` are both 27/27.
- **Scroll extents:** child border boxes past negative margins count. `scrollWidth`/`scrollHeight` are measured from
  the padding edge. `cssom-view` +217.
- **Accessibility:** roles (HTML-AAM + ARIA) and accessible names (accname 1.2) in
  `src/js/prelude/07zzz-accname.js`, as hidden global `__lumenAx {role,label}`. The WPT testdriver vendor
  (`tests/wpt/lumen_wpt.py`) uses it for `get_computed_label`/`get_computed_role`. Results: accname 0 → 436/496,
  html-aam 705, wai-aria 529.
- **getComputedStyle(el, '::before'/'::after'):** returns the pseudo-element's own style (`css_get_computed_pseudo`
  in `style.c`; `N.computed` has a third argument).
- **:dir():** uses real HTML directionality (`select.c` `el_dir_rtl`/`auto_dir`). +49.

## Next steps (in order)
1. Finish and commit the performance-timeline work above. Then run `/performance-timeline/` idlharness to find the
   rest of the missing members.
2. **Iframes** (CPU/RAM and timeouts):
   - Adding an iframe builds a whole new V8 context (~50–100 ms). Options: snapshot, or lazy context for blank
     iframes.
   - Removed iframes aren't freed after the parent read `contentDocument` (~3.5 MB each).
   - Both cause the 1.3 GB peak in `pixel-length-attributes` and the 200% CPU / timeout in
     `form-submission-0/form-double-submit-requestsubmit`.
3. **Remaining accname:** alternative text with `counter()` in `content: ... / "alt"`, `attr()` fallback in `content`,
   `comp_label` 125/131, `comp_embedded_control` 27/29.
4. **Logical sizes:** resolve `inline-size`/`block-size` after the cascade, so a `writing-mode` declared later in the
   same rule applies (last `sizes-auto` case, writing-modes and logical tests).
5. **Anchor positioning:**
   - `anchor-position-writing-modes-001` is 432/1296; it needs vertical writing-mode layout.
   - `position-area-chain` 0/5.
   - `calc()` inside `anchor()` (`try-tactic-percentage`).
   - Anchors in inline text and multi-column layout.
6. **Biggest areas:** CSS Grid (~10.7k failing), the HTML interface checks (`/html/dom/idlharness`), SVG hit testing,
   uievents, webmessaging.
7. Only after WPT is much closer to Chrome: the 10-tab Lumen vs Chromium CPU/RAM/video comparison.

## Known flaky or pre-existing timeouts (not regressions)
- `css-anchor-position/anchor-scroll-js-expose.html`
- `css-anchor-position/transform-015.html`
- `cssom/CSSImportRule-layer-false-positive.html`
- `cssom/CSSImportRule-layer-media.html`
- `cssom/medialist-mutation-after-owner-gc.html`
- `css/selectors/focus-visible-002.html` (parallel only)
- `form-submission-0/*` (iframe cost)

Always rerun losses alone on the new build and on `/tmp/lumen-old` before calling them regressions.

## Run log
The next run number is **310** (309 = performance timeline, +1267, one loss noted above, uncommitted). Append one line per run here:
`RUN N (committed|reverted): change. Result vs before. Losses and their cause.`
