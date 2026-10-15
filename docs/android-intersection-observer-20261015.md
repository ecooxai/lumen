# Android visibility-observer scheduling, 2026-10-15

The daily-browser goal remains active, with Vulkan first. This change removes
the permanent 300 ms IntersectionObserver timer. Native style, layout and
scroll updates now schedule checks; unchanged documents have no observer timer
or target geometry reads. New observations remain asynchronous, and removing
the last target immediately releases the registered pump. Callback-induced
changes survive for a later delivery. Continuous changes are coalesced to at
most 20 checks/second per context.

A shared visual generation covers style-only transforms, completed layouts,
scripted element scrolling and native wheel/touch scrolling. Viewport state
now also tracks scroll offsets, without generating resize/media events for
scroll-only changes. The generation lives in the document layer so CSS-only
consumers do not acquire a dependency on the layout driver.

This is a scheduling repair, not full IntersectionObserver conformance.
Existing limitations in roots, ancestor clipping, threshold ratios and margin
parsing remain. In particular, the physical scroller check below proves a
notification follows native scrolling; it does not establish correct clipping
against the scroller's bounds. The API's asynchronous model is described in
the [W3C specification](https://www.w3.org/TR/intersection-observer/).

## Validation

- Five new JS lifecycle/delivery tests fail against the old polling observer
  and pass after the change. Fifteen combined observer/viewport JS tests pass.
- Native viewport-state checks pass with UBSan, including horizontal/vertical
  scroll, combined resize/DPR/scroll changes and stable repeated updates.
- Twenty-two viewport-style and scrolling-hit tests pass. The first CSS-only
  test link exposed the misplaced generation implementation in the layout
  driver; moving the shared generation to the document layer fixed it.
- Connected MI 8, Android 15, arm64, Vulkan confirmed in logcat, pixel scale 200.
  Fourteen phone checks pass: asynchronous initial delivery, page scrolling,
  position changes, paint-only transforms, detach/reattach, element scrolling,
  unobserve and reobserve. Three more checks pass for callback mutation and
  iframe observation/style updates. A physical swipe caused a notification
  with element scrollTop 1118.245, without scripted scrolling in that interval.

## Controlled fixture measurements

Whole-process and per-thread CPU were sampled every second throughout every
device run. Idle windows use the final 30 samples before the fixture's 35-second
idle interval ends; all spikes are retained. Six identical swipes
(540,1800 to 540,500, 450 ms each, 450 ms gaps, two seconds settling) form each
scroll window. CPU percentages use one logical core as 100%.

| 500 observed targets | Previous build | Final scheduler |
| --- | ---: | ---: |
| Idle geometry reads, 35 s | 58,000 | 0 |
| Idle CPU mean, 30 samples | 1.8978% | 0.4260% |
| Idle CPU peak | 8.0000% | 8.7379% |
| Idle RSS mean | 234.43 MiB | 233.20 MiB |
| Six-swipe CPU mean, 8 samples | 28.6844% | 29.9769% |
| Six-swipe CPU peak | 37.7551% | 40.0000% |

All listed windows have zero sampling-cadence errors. Idle CPU improved about
78%; RAM is approximately unchanged. The short scroll test is slightly more
expensive, not a scrolling speedup. The initial 16 ms delivery limit measured
31.3524% scrolling CPU; the 50 ms limit reduces the extra scanning cost while
still updating more frequently than the old 300 ms poll. Further native/batched
geometry work could reduce cost during continuous changes.

Evidence is under `build/codex-intersection-20261015/`: `samples.jsonl`,
`server.log`, `summary.json`, `summarize.py`, JS/native outputs and screenshots.
Serve `tests/fetch_preflight_server.py`, reverse port 18765, and open
`/intersection-observer` for the performance interval. The `#extra` fragment
shortens idle waiting and enables callback/iframe/native-scroll checks.

## Packaging correction

The previously described clean controls APK actually retained the temporary
Android startup log/preload paths in a cached object. Source restoration alone
had not invalidated that object. Both the baseline and initial observer APKs
contained these hooks; no preload file was present during the fixture CPU
measurements. These measurements must not be described as diagnostic-free
binary runs. No account credentials were used.

Touching the restored `src/app/android_platform.c` forced recompilation. Direct
inspection of packaged `libmain.so` then confirmed both `/data/data/.../files/`
diagnostic paths absent. Future restoration of temporary probes must explicitly
invalidate native objects and verify the packaged binary, not only source or
ELF dependencies. The final clean APK is
`build/codex-intersection-20261015/lumen-vulkan-intersection-clean-debug.apk`,
SHA256 `3c4511a1cf8ff37eae30f2b4d2cc81420aafde81fa307195816d813e02236680`.
Native dependency checks and APK v2 signature verification pass.

## Real-page status

The prior build's confirmed paused YouTube window averaged 3.7670% CPU,
peak 10.1010%, RSS 321.11 MiB over 30 samples. Subsequent observer-build tap
attempts did not establish a matching paused state: screenshots retained the
pause icon, and longer playback reached YouTube's error screen again. Those
low-CPU error-page intervals are not paused-player performance results.
YouTube controls and uninterrupted playback therefore remain unresolved.
ChatGPT/Gemini/login and remaining daily-browser functions remain unfinished;
OpenGL ES and software-renderer optimization are still deferred.


A focused diagnostic run confirmed the real button target and an actual pause
at 57.419 seconds (`controls-diagnostic.log`); this is functional evidence, not
an idle performance comparison. After removing that probe and reinstalling the
clean APK, a fresh run paused at 0:13. Screenshots three seconds after the taps
and after the idle/profile intervals retain the play icon and the same frame.
The two center taps were two seconds apart, after the decoder was present.

Clean paused YouTube: all 115 unprofiled one-second samples averaged 3.2184%,
peak 28%, RSS 315.41 MiB, zero cadence errors. The first recorded 30-sample
window averaged 3.3387%; the last 30 averaged 2.8343%, peak 4.0404%, RSS
314.52 MiB. Keep the full-window result when assessing the idle target; the
last window alone is not a universal performance pass. The recommendations and
page timing differed from the baseline, so these are real-page observations,
not a controlled causal comparison or proof of reduced real-site memory.

A new app-UID userspace profile recorded 657 samples in 20.0402 seconds, zero
lost. Neither jsg_vrect nor n_vrect was sampled. js_tick is 21.46% inclusive,
3.20% self; SDL_WaitEventTimeoutNS is 21.92% inclusive. These are profile shares,
not whole-process CPU. The next profiling target is JavaScript timers and event
loop wakeups. frames_tick was inspected: it does not tick child JS contexts a
second time. Do not skip child contexts without preserving async delivery.
The direct run-as simpleperf invocation made no explicit settings changes;
security.perf_harden=0 and perf_event_paranoid=1 were read both before and after.
No app_profiler.py or root command was used. The recording was pulled and removed
from the phone. Symbols currently match the final clean APK in the Android
intermediates folder; preserve them before another native build if needed.

The final clean APK also passes all 17 fixture checks and the physical-scroll
notification (scrollTop 1123.598). This final fixture's settled 30-sample window
averaged 0.0677% CPU, peak 1.0204%, RSS 235.08 MiB, no cadence errors. It has only
one active observer after the extra tests, so do not substitute it for the
500-target before/after performance comparison above.

Cleanup: clean APK installed, app stopped, CPU sampler and fixture server
stopped and joined, owned port reverse removed, private probe/log/perf files
removed. Settings remain renderer=vulkan and pixel_scale=200. No account login
or credential entry occurred. Further YouTube control timing/touch, long
playback and CPU work precede other-site validation and secondary renderers.
