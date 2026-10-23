# WPT work: handoff notes (paused 2026-10-22, about 19:55)

The goal (user): Lumen should pass the Web Platform Tests (WPT) that Chrome passes. Report the per-area pass rate
against Chrome, then fix gaps. The JS engine is shared between host and Android, so WPT runs on the host, and
the phone gets checked with `tools/device_tests.py` after each batch.

Read `AGENTS.md` first. In short:
- Every compile and test goes through `python3 tools/codex_compile.py ...`, with no bash wrapper inside it.
  Use at most 6 jobs, or 3–4 while WPT runs. Gradle needs `--no-daemon`.
- A fresh Haiku subagent (`model: haiku`, `effort: high`) does every build, test or device task. Each test run
  also gets a parallel monitor subagent that samples CPU and RSS every second and watches for errors.
- Code stays in the main agent. Don't edit sources while a build subagent is running.
- Privacy: never type into page inputs, and describe third-party pages only structurally. Never `pm clear` or
  uninstall; use `adb install -r`. Don't touch lumen PID 837634. Never `pkill -f`. Commit only when the user
  asks.

## Pass rate vs Chrome (subtests Chrome passes that Lumen also passes)

Measured before the newest fixes. Overall **90.6%**.

| Area | % | Area | % | Area | % |
|---|---:|---|---:|---|---:|
| wasm | 100 | webidl | 80.3 | fetch | 32.7 |
| WebCryptoAPI | 100 | html | 75.4 | uievents | 26.8 |
| encoding | 99.8 | xhr | 70.6 | custom-elements | 19.2 |
| dom | 99.3 | sanitizer-api | 69.5 | IndexedDB | 16.1 |
| streams | 98.0 | console | 65.4 | mathml | 15.1 |
| domxpath | 97.4 | workers | 60.7 | editing | 10.4 |
| selection | 96.8 | shadow-dom | 58.1 | input-events | 5.4 |
| url | 95.7 | hr-time | 56.8 | web-animations | 4.6 |
| webstorage | 93.5 | FileAPI | 51.8 | compression | 0.8 |
| css | 85.8 | svg | 40.4 | urlpattern | 0.6 |
| trusted-types | 83.0 | domparsing | 38.8 | | |

Chrome fails all 296 brotli subtests in /compression/, so Lumen doesn't need brotli to match it.

## WPT tooling

- WPT checkout: `/mnt/p3/project/browser/wpt`. Results: `/mnt/p3/project/browser/wpt-data/lumen/lumen.jsonl`,
  one JSON per test (`url, status, message, subtests[[name, status, msg]], ms, inst`); the last line per URL
  counts. Chrome baseline: `/mnt/p3/project/browser/wpt-data/chrome-base.json`, keyed by URL.
- Run: `python3 tools/codex_compile.py python3 -I tests/wpt/lumen_wpt.py run /editing/ /input-events/ --rerun --jobs 4 --binary /tmp/lumen-wpt/bin/lumen`.
  First copy `build/codex-native/lumen` to `/tmp/lumen-wpt/bin/lumen`, but only when no WPT run is using it.
- Report: `python3 -I tests/wpt/lumen_wpt.py report /editing/ --top 0` (one prefix at a time).
- Host build: `python3 tools/codex_compile.py cmake --build build/codex-native -j3`. ctest:
  `python3 tools/codex_compile.py ctest --test-dir build/codex-native -j3 --output-on-failure` (104 tests).
- Quick JS probes without the WPT server: `python3 tools/codex_compile.py build/codex-native/jsrun PAGE.html 3000`.
  A probe prints its result by throwing `new Error(...)`.
  - `/tmp/uptest/up.html`: the URLPattern WPT data, 369 entries.
  - `/tmp/zprobe/z.html`: compression.

## Run 102: probably still going when you start

Run 102 is a WPT rerun of /editing/ and /input-events/ (742 tests), with log `/tmp/run102-wpt.log`. At 19:54 it
was at 254/742 (177 OK, 72 TIMEOUT, 5 ERROR) with no crashes. CPU and RSS were normal: about 7% total, at most
245 MB per instance. Its binary predates the testdriver fix below, so key-driven tests in it still time out.
Rerun those after the fix.

## Uncommitted changes (whole session; nothing committed)

Done and verified (host build OK, ctest 104/104):
- **Editing engine.** `src/js/lazy/editing.js` (the WPT reference implementation) is compiled lazily via
  `N.lazySrc('editing')`. It drives `document.execCommand` and the `queryCommand*` methods
  (`src/js/prelude/06-document.js`), plus contentEditable and designMode (`03-element.js`).
  `tools/generate_embedded.py` emits `NAME_lazy.inc`.
- **Cache API.** `src/js/shared/cache.js` (CacheStorage, Cache, `caches`; in memory, 64 MB) for page and worker.
  It fixes the phone error "Uncaught (in service worker) ReferenceError: caches is not defined". Its helper is
  `cacheHget`; a clash with `hget` in 07b-net.js once broke the prelude.
- **URLPattern.** `src/js/shared/urlpattern.js`, spliced in at `/*@shared urlpattern.js*/` in 07-web.js and
  included in worker.cc. Probe: 354/369 pass. All 15 failures are invalid hostnames or ports that should throw.
- **CompressionStream / DecompressionStream** for gzip, deflate and deflate-raw: native `src/js/compressjs.cc`
  (`zNew`, `zPush` → `[bytes, err]`) plus `src/js/shared/compression.js`, wired into js.cc, worker.cc, the
  globals and CMake. The probe passes 24/24.

Edited after the last build (not built or tested yet):
1. **`src/base/url.c` `parse()`**: a state-override (setter) parse now returns false on a spec failure, through a
   `fine` flag. It still applies partial changes as before. The two non-failure spec returns (scheme `keep`, and
   hostname with empty buffer when the URL has credentials or a port) set `fine = true`. Only
   `urlSet(..., strict=true)` reads the result. This should fix the 15 URLPattern failures.
2. **srcdoc / about:blank base URL**:
   - `doc_base()` added to `src/dom/dom.h`.
   - `frame_create` sets `d->base_url` to the parent's base for `about:` URLs.
   - Script, frame and resource URL joins in frames.cc, jsnative.cc, js.cc and main.c now use `doc_base()`.
   - New native `N.docBase()` is used by `documentBaseURI` in 06-document.js.
   - This fixes WPT editing tests whose srcdoc iframe loads `/resources/testdriver.js` (it got
     "reading 'send_keys' of undefined").
3. **Testdriver wiring (the big one)**: `src/app/main.c`'s tab host passed `h_internal` only for internal UI
   pages. So `N.internal('td?ping')` was never answered, `__lumenTd` didn't exist, and testdriver-vendor.js
   stayed inactive. Then testdriver's own `send_keys` waits for a human (a 60 s TIMEOUT) and `action_sequence`
   throws "not implemented". That's why there are about 294 editing timeouts, plus "not implemented" failures in
   dom, html, input-events, selection, uievents and shadow-dom. Fixed: the host now passes `h_internal` also when
   `env_hook(ENV_TESTDRIVER)` is set. `h_internal_in` still refuses everything except `td?` for non-internal
   pages.
4. **Unfinished: frames and testdriver** (`src/js/prelude/08-window.js:174`). I moved `topTd` out and changed the
   condition to `N.internal('td?ping') === 'pong' || topTd()`. But `window` is not defined yet at that point in
   the prelude (globals are installed around line 624), so `topTd()` just returns null and frames still don't
   get `__lumenTd`. (`FN(internal)` in jsnative.cc returns undefined in frames, because of `c->parent`.)
   Options:
   - Let `FN(internal)` forward `td?` commands from frames to the top context's host.
   - Or define `__lumenTd` in frames when `N.topWin()` exists and the top window has `__lumenTd`, but only under
     LUMEN_TESTDRIVER. Don't expose it to frames in normal browsing.

## Next steps

1. Finish item 4, then build and run ctest through a Haiku subagent.
2. Run the probes: `/tmp/uptest/up.html` should reach 369/369; `/tmp/zprobe/z.html` should stay at 0 fail.
3. When no WPT run is active, copy the binary to `/tmp/lumen-wpt/bin/lumen`. Then rerun WPT (with a monitor
   subagent) for /editing/, /input-events/, /urlpattern/ and /compression/. After that, rerun the areas that
   had "not implemented by testdriver-vendor.js" failures: /uievents/, /selection/, /dom/, /html/ and
   /shadow-dom/. Report each area's numbers against Chrome to the user.
4. **Android:**
   - Rebuild the debug APK, as in `CONTINUE.md`: the Gradle 8.13 launcher from
     `build/codex-android-toolchain/gradle-8.13`, run via java through codex_compile, with the `-PlumenSdlDir=...`
     flags listed there.
   - Install with `adb -s 2d4f6bc0 install -r ...`.
   - Run `python3 tools/device_tests.py -s 2d4f6bc0 google chatgpt x` with a monitor subagent.
   - Last phone results (19:32 build): google last-10-s CPU 1.1%, ChatGPT 5.0% (borderline; about 10% for 30 s
     mid-run, to investigate), x.com 1.9% after 150 s. No crashes or ANRs.
5. Then the far-behind areas:
   - fetch: API timeouts.
   - IndexedDB: service-worker variants. One WPT instance once reached 1.5 GB RSS; investigate.
   - shadow-dom: referenceTarget.
   - The rest: custom-elements, web-animations, mathml, the remaining domparsing, sanitizer and trusted-types
     gaps, and EditContext (missing; "EditContext is not defined").
6. Other open items from before (not WPT): ChatGPT UI speed, ai.dev, the Google Docs keyboard, maps scroll lag,
   boot time under 15 s, YouTube Shorts, Canvas2D, CPU spikes on pause. Don't publish an x.com post without the
   user's text and go-ahead.
