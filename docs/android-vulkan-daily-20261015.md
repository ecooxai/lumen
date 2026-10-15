# Android Vulkan progress — 2026-10-15

The daily-browser goal remains active and incomplete. The user's current order is
Vulkan first, then OpenGL ES, then CPU/software rendering only after Vulkan is
solid. No OpenGL ES or software performance acceptance is claimed here.

## Installed changes

- Track up to eight separate fixed-content row regions instead of the bounding
  rectangle from the highest header to the lowest footer. Overlapping regions
  merge; capacity overflow conservatively merges damage. Collect bounds after
  transforms, include shadow extent, and clamp before integer conversion.
- Repaint and upload each fixed region during a scroll. Preserve the existing
  Vulkan texture row ring and direct page upload, including full-upload and CPU
  composition fallback when an upload fails.
- Allocate opacity-layer storage only for the damaged rows. For a 1080-wide,
  1968-row page with a 32-row repaint, this changes each temporary layer from
  8,501,760 to 138,240 bytes. This is an allocation calculation, not a measured
  reduction in whole-process RSS or a claim about full-frame repainting.
- Android YouTube taps dispatch pointer motion to wake the existing bounded
  eight-second control hold. Periodic synthetic movement now targets player
  descendants only and stops when the pointer leaves the player or navigation
  replaces the page. Full live-site control timing remains unverified.
- Build and install the previously committed keyboard reveal repair (73c379d).

## Device evidence

MI 8, Android/arm64, Vulkan confirmed in the app log, pixel_scale=200.
The fixture is tests/pages/codex-android-scroll-controls.html served through an
owned adb reverse. Six alternating swipes at x=700, y=1650 to 700 and back,
600 ms each with one-second gaps, with the fixed footer enabled.

| Measurement | Before separate regions | After separate regions |
| --- | ---: | ---: |
| Same-stage CPU intervals | 9 / 8.99 s | 9 / 9.00 s |
| Process CPU mean (100% = one core) | 80.31% | 34.44% |
| Process CPU peak | 103.13% | 45.45% |
| Example steady paint interval | 41 frames / 1010.7 ms | 60 frames / 1016.5 ms |
| Scroll repaint path | Full frames | Partial frames with row reuse |

This is a short controlled fixture comparison, not a broad site benchmark or a
compositor frame-timing measurement. Gesture momentum and idle gaps affect frame
counts. The source before the region repair already contained the layer-memory
and YouTube changes; the fixture has no opacity groups or YouTube content.
Paint profiling identified full CPU raster and page composition as the regression
with two fixed areas. Vulkan presentation still has occasional long wall-time
intervals and needs further investigation on real sites.

Manual screenshots confirm full-width edges and fixed header/footer after
forward/reverse scrolling, successful overlay hit testing after scrolling,
visible typed input after keyboard resize, and recovered viewport after Back.
The fixed footer can overlap the right side of the focused field; general
fixed-overlay occlusion avoidance is still not implemented.

Focused idle: 89 same-stage intervals / 89.02 s, CPU mean 1.8086%, peak 3.9604%,
RSS 219.211–219.219 MiB. The earlier unfocused fixture held near zero CPU.
The post-scroll stage contains a momentum tail and must not be presented as an
entirely stationary idle sample. One-second process and thread raw samples span
all app tests; no sampling parse or cadence errors were recorded.

## Site checks and limitations

ChatGPT's homepage and login dialog rendered. Selecting Continue with Google
returned to the homepage without completing authentication. Gemini rendered its
header/sign-in/footer but its main area remained blank; sign-in did not navigate.
No credentials were entered. The account domain was subsequently confirmed by the
user; do not copy credentials into files or reports. ChatGPT composer typing
under 10% CPU has NOT been validated. Site load/login stages contain navigation,
interaction and load work, so their aggregate CPU values are not idle results.

YouTube played the Big Buck Bunny test video, and the updated build displayed
play/pause controls after a tap. Playback repeatedly changed to an error around
30 seconds. The control hold has sanitizer-tested targeting/cadence/timeout,
but an uninterrupted eight-second live playing-state acceptance is still needed.
Do not call YouTube fixed. Investigate media buffering/network errors and capture
relevant JS/media diagnostics before selecting a repair. window.open() is a stub;
its role in Google authentication is a hypothesis, not a proven cause.

Zoom/pinch zoom and full Chrome feature parity are not implemented by this change.
Continue Vulkan real-page profiling and compatibility work before other backends.

## Validation and artifact

95 targeted unittest cases pass, including the existing Android focus/touch,
display damage, GPU row-upload and fallback tests. New ASan/UBSan tests compare
500 scroll frames with full raster output across five scales and reversed
scrolls, test separated translucent fixed areas, merge/overflow behavior,
opacity storage bounds, padded strides, nested layers/clips, image punch-outs,
and YouTube timer targeting. A negative control using the original rasterizer
fails the bounded-layer allocation test. GPU recovery tests include failure of
the second fixed-region upload. git diff --check passes.

All compilation used tools/codex_compile.py, shared CPUQuota=480%, SCHED_IDLE,
nice 19, Gradle --no-daemon --max-workers=2 and the verified V8 prebuilt. The frozen
V8 source build was not resumed. No measured host-build CPU figure is claimed.
Android build, native ELF dependency/layout verification for arm64/API29 and
APK v2 signature verification pass. These do not establish full API/runtime
compatibility. Regenerating the APK removed unused incremental ZIP space; every
ZIP entry was byte-identical to the device-tested candidate.

Installed compact APK: build/codex-daily-20261015/lumen-vulkan-daily-debug.apk
85,240,084 bytes; SHA256:
d4e94783a57e3e1b1d32a3f2df39e3bcaca22095a93b9cddbd146bba810fd76a
Native libmain.so SHA256:
51f9bd6b9342d003207a1294a63d5e40bcad1fadb3ec130944567fe846bc547b
Source hashes are in compact-artifact.json. Evidence, screenshots, raw samples,
profiling logs and stage-stats.json are under build/codex-daily-20261015/.

Cleanup completed: app force-stopped after testing, test profiling marker and
owned reverse removed, fixture server and sampler joined, phone locks released.
Settings compare byte-for-byte with the initial settings, including Vulkan.
