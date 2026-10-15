# Android Vulkan: fetch streaming, timer waits and YouTube, 2026-10-15

Vulkan remains the first acceptance target. OpenGL ES and CPU/software work are
still deferred. The daily-browser goal remains active.

## Confirmed repair

`corsSend` used `st` for both its streaming callback object and the preflight
callback's status text. Successful OPTIONS requests therefore discarded the
response-header/chunk callbacks. Fetch buffered the entire actual response
before resolving it. Renaming the variables preserves streaming through the
preflight path; origin and credential checks are unchanged.

Four Node regression tests pass. The callback-preservation test failed before
the fix. Tests cover same-origin streaming, failed preflights, and credential
permission requirements. Run `node --test tests/test_fetch_preflight.js`.

The two-origin device fixture is reproducible with
`python3 tests/fetch_preflight_server.py`, adb reverse for tcp:18765 and
tcp:18766, then opening `http://127.0.0.1:18765/`. The local server deliberately
waits three seconds between response chunks. It prints requests and the browser's
report; it performs no external requests. CPU sampling is required throughout.

MI 8 / Android 15 / Vulkan, baseline versus diagnostic candidate:

| Case | Baseline first data (ms) | Candidate first data (ms) | Candidate result |
| --- | ---: | ---: | --- |
| Same origin | 32.49 | 29.94 | Pass |
| Cross origin, simple | 10.82 | 12.47 | Pass |
| Cross origin, preflight | 3013.31 | 50.40 | Pass |
| Cross origin, preflight with credentials | 3009.56 | 50.09 | Pass |

Both CORS-denial cases also pass: rejected preflight sends no actual POST;
allowed preflight with disallowed actual response rejects fetch. The baseline
fails only the two preflight streaming cases. This verifies response latency,
not general website or renderer throughput. Local server reports are saved in
`build/codex-youtube-20261015/stream-comparison.json`.

## YouTube remains intermittent

Public test: `https://www.youtube.com/watch?v=aqz-KE-bpKQ`.
Temporary private-file diagnostics captured native decoding, fetch start/end,
and player/media events. Signed query strings are excluded from native request
logs. No account credentials were entered.

A diagnostic baseline played through 166.75 seconds. Another run with the fetch
fix failed around 53 seconds. In the failed run, the buffered range stopped at
62.2 seconds while HTTP 200 media POSTs continued. Native audio/video queues
remained populated, and no media/SourceBuffer error or endOfStream was observed
before the page unloaded the video and reported player error 5. The same pattern
occurred in an earlier trace. These media requests do not use OPTIONS, so the
preflight repair is not the root cause of this failure.

Some fallback video GETs return 403 (or fail their TLS handshake), and a player
API POST has an empty body and returns 400. These also occur in successful runs.
An additional diagnostic run reached 105.01 seconds, with the same media CDN as
the failed run. At the public fetch entry point, the player API Request already
has a null body, so native transport did not lose its data. These are leads, not
established causes. Do not bypass CORS, TLS, or server access checks. Further
protocol/player investigation is needed.

Temporary native hooks are removed from final source. The clean APK's native
ELF/dependency closure check (arm64-v8a, API 29) and v2 signature check pass.
Builds used the capped compilation wrapper and verified V8 prebuilt.

Initial streaming-only APK (retained as `lumen-vulkan-stream-only-debug.apk`): `build/codex-youtube-20261015/lumen-vulkan-stream-only-debug.apk`,
85,240,084 bytes, SHA256
`6ad801b292fee451347a7851663f33de55db0e55638a5fde837609eb0a4485e0`.

The initial streaming-only clean APK repeats all six fixture passes. First chunks after
preflight arrive at 54.77 ms (omit credentials) and 51.95 ms (include), while
headers arrive at 10.97/8.77 ms. All three runs' server logs confirm zero actual
POSTs after a denied preflight.

CPU samples use one-second process/thread readings; 100% means one CPU core.
Final fixture run including load/requests: mean 1.4722%, peak 50.9615% over
55.02 s. Its final 29.99 s: mean 0.3334%, peak 7%, mean RSS 226.6303 MiB.
The idle window includes a screenshot; there was no sustained excess CPU.
Diagnostic baseline YouTube played through 166.75 s; its final 30 s averaged
63.0457% CPU and 459.3871 MiB RSS (actual playback, not idle). These are workload
measurements, not evidence of a GPU speedup from this fetch-only change.

## Timer wait repair

The streaming-only clean APK still showed the YouTube error page by 95.46 s
wall time. Its last 9.99 s averaged 5.2052% CPU, mostly the main thread. Repeated
error-page observation showed 4.5651% mean / 8.9109% peak over a later 30.01 s.
This exceeds the idle target and prompted further profiling.

Android simpleperf captured 1,588 samples with zero lost samples over 20.04 s.
The profile includes main-loop work, JSON work and V8 incremental/concurrent GC.
Paint tracing showed no ongoing repaints after the error screen settled.
`simpleperf-callgraph.txt`, `simpleperf-symbols.txt`, and the recording in the
ignored evidence directory preserve the observations. The later baseline CPU
window excludes the profiler itself.

The JS deadline conversion truncated positive fractional milliseconds to zero.
That repeatedly polled SDL and ran main-loop housekeeping before a timer was due.
`deadline_wait_ms` now rounds positive waits upward, preserving already-due
callbacks and the current timeout cap. A deterministic test checks 9,999
fractional deadlines, overdue/zero deadlines, caps, infinity and NaN; the old
truncation fails the early-poll assertion. UBSan passes.

22 focused host tests pass: 4 fetch, 1 deadline (including the old-code negative
control), 8 SDL event waits and 9 Android fling pacing. The first SDL test run
used the raw upstream source and failed 4 cases; rerunning against the generated
`codex_android_SDL_events.c` that the APK actually compiles passes all 8.

The intermediate clean APK includes both fixes and passes native/dependency and v2 signature
checks. `build/codex-youtube-20261015/lumen-vulkan-stream-debug.apk`,
85,240,084 bytes, SHA256
`e75f76bc64d1dc39279bb6d2d6455796c004707a893ddb19b00d6cea564f7ca9`.
The YouTube error still reproduces by 87.94 s wall time. This timer repair is
for idle CPU, not an established playback-error repair.

The deadline change alone did not bring the error page below the idle target:
a later 30 s window still averaged about 4.43%. Further idle profiling is needed.

## Cache repeated document base-URL searches

A temporary V8 CPU profile during successful YouTube playback recorded 1,021
samples, including 375 in native document queries reached through `baseURI`,
media `src`, and the site's time-update callback. This was playback, not the
error-page idle workload. A document-scoped WeakMap now caches the resolved base
URL against the DOM mutation version and current page URL. DOM changes and
history URL changes invalidate it; URL and origin policy are unchanged.

Four additional Node tests pass. On the MI 8, a fixture with 10,000 descendant
elements and 250 repeated base-URL reads improved from 215.7247 ms to 2.9059 ms.
All six device correctness checks pass before and after: repeated reads, first
base selection, href mutation, base removal, history changes and fallback.
Run the same fixture server and open `/base-uri-cache` to reproduce. This is a
specific lookup benchmark, not a claim of whole-site or frame-rate improvement.

Final clean APK includes all three repairs. All 26 focused host tests pass
(8 JavaScript, 1 deadline, 8 SDL waits, 9 fling pacing). Native dependency closure
and v2 signature checks pass. No temporary native profiling hooks remain.
The APK is installed and retained at
`build/codex-youtube-20261015/lumen-vulkan-stream-cache-debug.apk`,
85,240,084 bytes, SHA256
`7a97676eeea48c2c477e706ed287f4987a5d086927397ef70eca25201510aadc`.

All six streaming checks also pass on this final APK. First data arrives after
74.11 ms for preflight and 51.05 ms for credentialed preflight, versus about
3,010 ms before the repair. Denied preflight still sends no actual POST.
YouTube playback/control acceptance, error-page idle CPU, ChatGPT/Gemini/Google
login, composer CPU and zoom remain open; the final cache build has only the
local fixture acceptance so far. OpenGL ES and CPU/software remain deferred.

Final clean local-fixture idle window: 30.00 s, 0.0333% mean CPU, 1.0000% peak, 258.4648 MiB mean RSS. CPU/thread sampling ran at one-second intervals throughout.

Cleanup: app stopped, CPU sampler and local server joined, both owned device locks released, test port reverses removed, and private profiling markers/logs removed. Final source has no diagnostic native hooks.
