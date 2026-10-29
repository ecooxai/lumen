# Android Vulkan image spans (2026-10-15)

The daily browser goal remains active, with Vulkan first. Interior bilinear image
pixels now interpolate and blend in a branchless span. The Android compiler
vectorizes this loop (verified against the built ARM64 disassembly). Edge coverage,
rounded clips and minification retain the existing path. Arithmetic and pixel
rounding are unchanged; no persistent cache or allocation is added.

## Verification

All compilation used tools/codex_compile.py. Optimized ASan/UBSan raster tests
pass: 12 original golden hashes, 100 gradient oracle cases, 180 image comparisons
including local raster origins, and 18,504 span comparisons covering every weight,
vector tail lengths and representative alpha endpoints. The 2,000 scroll pixel
comparisons and four display damage scenarios also pass. APK native dependency
and v2 signature verification pass. Packaged libmain has no temporary probe paths.

## Phone results

MI8, Android 15, Vulkan, pixel_scale200. Baseline 01be9b2 versus this change,
same generated thumbnail/video page and six 450 ms swipes with 450 ms gaps.
The fixture waits for images and video, then pauses the video before reporting ready.

| Measurement | Baseline | Image spans |
| --- | ---: | ---: |
| Raster ms/frame | 6.371 | 4.695 |
| Total paint ms/frame | 8.832 | 7.335 |
| Reported frames in about 7.87 seconds | 362 | 364 |
| Scroll CPU mean (8 samples) | 45.70% | 39.75% |
| Scroll RSS mean | 250.79 MiB | 250.75 MiB |
| Idle CPU mean (10 samples) | 0.50% | 0.60% |

Real YouTube was visibly paused at 0:02 before and after 21 alternating swipes.
The 20-second profile recorded 15,487 samples, zero lost. Reported paint frames:
985; raster 10.173 ms/frame; total paint 14.234 ms/frame. Scroll CPU mean75.26%
(22 samples), peak112%, RSS361.85 MiB. Settled paused CPU3.12% (25 samples),
peak8.08%, RSS358.75 MiB, so this is not a clean idle CPU pass. Prior YouTube
paint16.807 ms/frame used different recommendations; do not infer a controlled
speed ratio. RSS increased in the real-site run: no overall memory gain proven.

SurfaceFlinger actual presentation timestamps cover only a 2.20-second tail:
126 intervals, median16.698 ms, p95 16.806 ms, five intervals above25 ms,
maximum50.106 ms. This shows remaining stutter, not proof of sustained60 FPS.
Profile self time: raster_rect38.44%, fill16.31%, memmove11.60%.

Whole-process and all-thread CPU sampled at1Hz throughout:968 samples, zero
cadence or parse errors. Evidence and matching symbols are under
build/codex-image-spans-20261015/, with real-site evidence in youtube-images/.
opacity-summary.py and summarize-youtube.py youtube-images reproduce summaries.

## Artifact and remaining work

Installed APK: build/codex-image-spans-20261015/lumen-vulkan-image-spans-debug.apk
SHA256:85de378794a3a362da52a7f50aab1566c599189bd1005b92d8cafb9a63a33368.
App force-stopped; monitor/server verified stopped and joined. Owned reverse
ports18765/18766 and private paint marker/perf file removed. No credentials used.

YouTube touch/control timing and playback reliability, RAM reduction,
ChatGPT/Gemini/Google login, zoom and remaining daily features are unfinished.
OpenGL ES and software rendering remain deferred. Android currently converts
completed taps to mouse clicks; investigate native touch semantics next.
