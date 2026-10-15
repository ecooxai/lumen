# Android Vulkan scrolling with video (2026-10-15)

The daily-browser goal remains active. Vulkan still has priority over OpenGL ES
and software rendering. This change removes the blanket full-page raster rule
for scrolling pages containing one independently composited video.

## Rendering change

Scroll reuse requires an already valid video hole, the same media owner, and
matching rounded device-pixel corners after the expected scroll movement. Fixed
videos retain their position and are covered by existing fixed-region damage.
Multiple videos, changed ownership/geometry, invalid previous holes, and large
fixed damage retain the full-raster fallback. Document/layout checks still apply.
Exposed/fixed strips preserve the transparent video hole while being repainted.
Android can use the page texture ring with video: the video has separate texture,
coordinates and uniforms, and is drawn before the page. No extra image cache or
persistent allocation was added.

## Verification

All compilation used tools/codex_compile.py and the shared host quota.

- Scroll raster ASan/UBSan: 2,000 frames exactly match full raster at five scales,
  with padded strides, reversals, fixed/flow video holes, rectangular clips,
  fractional video geometry and translucent overlays. Geometry rejection covers
  changed position/size and nonfinite/invalid scale.
- GPU scroll suite: 13 passing scenarios. The new scenario calls the complete
  actual gpu_present_frame for RGBA and YUV video through GPU API stubs, checks
  video-before-page draw order, independent uniforms, no redundant page upload,
  and preserved ring mapping with transparent/translucent page pixels.
- Fixed bounds suite: 11 passing scenarios. Display damage: 4 passing scenarios.
- APK build, native dependency/layout verification and v2 signature pass.
  Packaged libmain has no temporary diagnostic probe paths/counters.

## Phone results

MI8, Android 15/API35, Vulkan, pixel_scale200. Same controlled opacity/video
fixture and six 450 ms swipes with 450 ms gaps; about 7.9 seconds including settle.
Paint profiling enabled in both builds. Baseline is b002aa7, immediately before
this change. These are reported paint frames, not display presentation FPS.

| Measurement | Baseline | Video scroll reuse |
| --- | ---: | ---: |
| Raster ms/frame | 7.172 | 4.398 |
| Total paint ms/frame | 13.595 | 7.721 |
| Reported frames | 360 | 364 |
| Scroll process CPU mean | 76.22% | 47.45% |
| Scroll RSS mean | 253.47 MiB | 253.13 MiB |
| Idle CPU mean (10 samples) | 0.40% | 0.30% |

No meaningful overall RAM reduction demonstrated.

Real YouTube aqz-KE-bpKQ was confirmed paused at 0:21 with a play triangle before
20 alternating swipes and a 20-second simpleperf recording. The player image and
controls remained visible afterward. Results: 861 reported frames, raster
12.853 ms/frame, total paint16.807 ms/frame, process CPU78.11% (22 samples, peak
102.97%), RSS331.54 MiB. The previous valid scanline run was28.001 ms raster and
35.457 ms total paint with463 frames, CPU86.80%; recommendation content and
completed gestures differ, so this is directional, not a controlled speed ratio
or proof of stable60 FPS. The dedicated settled-paused stage has17 samples,
CPU2.637%, peak3.191%, RSS320.51 MiB. Earlier stage named youtube-video-paused
preceded definitive pause confirmation and is excluded from this idle result.

A subsequent four-swipe playback check advanced the video from0:21 to0:29 and
kept it positioned correctly. CPU71.84% mean over10 mixed playback/gesture samples,
peak116.16%. The final screenshot still shows a pause icon: do not label this
last action a confirmed pause or infer controls timing is fixed. Controls timing,
seeking, fullscreen and long playback reliability remain unfinished.

Whole process and all threads were sampled at1Hz throughout:560 samples, no
cadence or parse errors. Simpleperf recorded16,302 samples, zero lost, using the
app UID; perf_harden/paranoid settings unchanged. raster_rect48.50% self,
fill13.59%, memmove9.51%. Matching-address lookup identifies the image vertical
interpolation/coverage/blending loop (raster.c312-319) as the next main cost.

## Artifacts and terminal state

Evidence: build/codex-video-scroll-20261015/; opacity-summary.py and
summarize-youtube.py youtube-video reproduce reports. youtube-video/ contains
matching libmain/libSDL3 symbols, profile, hot-addresses.csv, hot-lines.txt,
screenshots and event times.

Installed APK: build/codex-video-scroll-20261015/lumen-vulkan-video-scroll-debug.apk
SHA256:1bfa49bdd7aca98c46f784a3de24c7f52260b44d42fea836e5f529a88893058b.
App force-stopped; monitor and server verified, stopped and joined; owned reverse
ports18765/18766 and private paint/profile files removed. Cookies/settings retained.
No owned live job remains. No credentials entered. ChatGPT/Gemini/Google login,
zoom/daily feature coverage and memory work remain unverified/incomplete.
