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
  Many /html/ gaps there are stale (enctype setter exists) → rerun /html/ /dom/ /selection/ /xhr/ after the build.
- Ideas not done: detached `<img>` (not in document) never loads → onload never fires (fetch/metadata img/picture
  timeouts; also real-site preloading). Native Sec-Fetch-* for subresources/navigations (needs initiator/dest on
  NetRequest). fetch stream-safe-creation.any.js times out (cause unknown; probe it).

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
