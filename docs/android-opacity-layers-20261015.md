# Android Vulkan opacity groups and event waits, 2026-10-15

The daily-browser goal remains active. Vulkan is first; OpenGL ES and software
mode optimization, account testing, zoom and feature coverage remain unfinished.

## Changes

Opacity groups allocate and composite only their visible drawing bounds, instead
of the viewport width and every damaged row. Bounds include descendant overflow,
actual glyph ink, shadows and nested groups. Clips conservatively narrow drawing,
while the bounds prepass uses the damage rectangle. Parent buffers retain their
own origin, stride and height. Empty and offscreen groups need no pixel area.

The pinned Android SDL event patch also avoids creating a poll sentinel during a
blocking wait and removes an immediate duplicate event pump. Polling and pumping
after real waits remain intact. The existing source hash gate is retained.

## Correctness

Twelve full-frame hashes captured with the previous rasterizer match. Layer tests
run with ASan and UBSan, including padded stride, partial damage, all primitives,
glyphs outside nominal text bounds, negative shadow spread, rounded clips, nested
layers, video punch and empty/overflow/unbalanced groups. A nested 4x3 group uses
at most 48 bytes per pixel allocation; the old viewport implementation fails that
gate. Four display-damage pixel tests pass. Fourteen SDL model/source tests pass;
the new sentinel/pump-count gates fail before the SDL change.

On the phone, event-wait timers/rAF/MessageChannel and a physical tap pass, as do
six fetch stream/CORS checks. The SDL-only 29-sample timer fixture CPU changed
1.653% to 1.620%; this is not a significant performance improvement. YouTube's
25ms mouse-activity timer is real site behavior, not a timer to suppress.

## Performance evidence

MI8, Android15/API35, Vulkan confirmed in startup log, pixel_scale200. Whole-process
and all-thread CPU sampled every second throughout each run. Paint timing enabled
in both comparisons. CPU is percent of one core; RSS is the process resident set.

The /opacity-scroll fixture has a fixed paused native video and 30 cards with
translucent badges. Six identical physical swipes plus two seconds settling:

| Metric | SDL-only baseline | Bounded groups |
| --- | ---: | ---: |
| Reported painted frames in ~7.9 seconds | 3 | 64 |
| Raster thread CPU per reported frame | 2005.616 ms | 74.841 ms |
| Total paint thread CPU per reported frame | 2018.820 ms | 86.622 ms |
| Mean scroll process CPU, 8 samples | 96.799% | 80.790% |
| Mean scroll RSS | 246.187 MiB | 252.086 MiB |
| Idle process CPU, 10 samples | 0.100% | 0.398% |

No sampling cadence errors. This is about 27x less raster work per frame on this
stress fixture, not universal browser speed or a RAM reduction. The first after
run matched stale readiness and is excluded; after2 requires a fresh load and
matching media-ready report. Paint logs report aggregates, not presentation FPS.

Real YouTube, paused at 0:23, 20 alternating physical swipes over ~20 seconds:
174 reported frames, mean raster80.030ms/frame. Earlier real-site raster timings
were230-265ms/frame. Dynamic recommendations differ, so this is directional,
not a controlled speedup claim. Scroll process CPU84.331% (21 samples),
RSS328.458MiB. Before scrolling, 27 paused samples averaged2.593%,RSS330.645MiB.
The video remains paused and controls visible after scrolling.

The baseline profile had88.32% self samples in raster_rect, dominated by opacity
buffer compositing. After bounding,17403 userspace samples still show72.98% in
raster_rect and15.08% in fill; the next work is pixel clearing, gradients/fills
and image rasterization. App-UID simpleperf used without explicit security-setting
changes (perf_harden0/paranoid1 before and after), no root, no lost samples.

## Artifacts

Ignored evidence directory: build/codex-wakeups-20261015. It contains process/thread
samples, fixture/server reports, opacity-summary.py/json, screenshots, native
profiles and matching symbols. The youtube-layers subdirectory contains the
post-change real-site profile. Parent libmain.so is the pre-layer-change binary;
use the matching subdirectory symbols when analyzing the new profile.

APK: build/codex-wakeups-20261015/lumen-vulkan-bounded-layers-debug.apk
SHA256:c523e97e65f774013cd67bfbd16d63b6169797b4b23056a76962a44cc341575a.
Native/dependency and v2 signature verification pass. Packaged libmain.so directly
checked to exclude temporary detail-profile.log/detail-probe.js/diagTimers/WAKE
instrumentation. Paint timing is an existing runtime marker, not probe code.

At this checkpoint phone monitoring and fixture server remain owned and active;
app is paused on YouTube. Cleanup and further Vulkan work follow this checkpoint.
No account credentials entered. Full controls timing, seeking/fullscreen and
uninterrupted playback still need validation; ChatGPT/Gemini/Google login and
Chrome-style daily features are not complete.
