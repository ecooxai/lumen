# Vulkan scrolling: constant spans and image scanlines (2026-10-15)

The full daily-browser goal is active, with Vulkan first. This extends the bounded
opacity groups described in android-opacity-layers-20261015.md. Vulkan currently
presents CPU-rasterized page pixels and native video; optimizing the measured page
raster bottleneck directly improves this mode. OpenGL/software mode work remains
deferred.

## Implementation

- Constant-color spans separate fractional edge pixels from the interior. Clear
  and fill loops use local buffer pointers and precomputed alpha, allowing vector
  stores/blending without repeated alias-sensitive raster metadata reads.
- Vertical linear gradients evaluate their color once per row, then draw a span.
  Rounded shapes/clips retain the general path. Cardinal directions are exact;
  sinf(pi) previously gave a vertical gradient a tiny horizontal component that
  could change its color by a quantization step across a wide row.
- Bilinear image scaling caches two horizontally interpolated source rows. Typical
  upscaled thumbnails reuse those rows repeatedly. The cache preserves the exact
  packed-integer rounding of the old two-stage filter. It is freed after each
  image; extra pixel storage is only8 bytes per damaged horizontal pixel, not a
  full scaled image. Nearest-neighbor minification remains unchanged.

## Validation

The original12 raster reference hashes still match. ASan/UBSan layer tests pass,
including100 cardinal-gradient scalar comparisons with transparency, hard/repeated
stops, fractional edges, damage and padded stride. A wide vertical-gradient
uniformity check passes; the previous raster fails the new gradient suite.
180 image cases compare against a scalar direct-source oracle, covering enlarged
and reduced images, repeated/advancing source rows,1-pixel sources, fractional
coordinates, rounded clips, varied alpha, scale and padded stride. Four existing
display-damage pixel tests pass.

On the connected MI8, Vulkan active, pixel_scale200, process AND all threads
sampled every second. Paint-profile enabled in every timing comparison.

The fixed-video opacity stress fixture, identical six swipes plus settling:

| Metric | Bounded layers | Plus span/gradient optimization |
| --- | ---: | ---: |
| Reported frames in ~7.9 seconds |64|359|
| Raster thread CPU per reported frame |74.841ms|7.229ms|
| Total paint thread CPU per reported frame |86.622ms|13.660ms|
| Mean process scroll CPU,8 samples |80.790%|67.238%|
| Mean scroll RSS |252.086MiB|253.241MiB|
| Mean idle CPU,10 samples |0.398%|0.504%|

No cadence errors. SDLThread averaged62.303% during the span run, SDLActivity2.395%.
This fixture does not exercise thumbnail scaling. It is not a universal speedup,
a presented-FPS measurement or an overall RAM improvement.

Real YouTube after spans, paused0:37:21 alternating physical swipes during a
20-second profile.371 reported frames; raster37.034ms/frame, totalpaint44.468ms.
ProcessCPU86.703%,RSS326.286MiB (21 samples). Paused21samples averaged2.761%,
peak5%,RSS320.926MiB. Earlier bounded-layer run:174 reported frames and80.030ms
raster/frame. Recommendations and number of completed gestures differ; these
real-site runs are directional, not a controlled page-content comparison.
The17777-sample profile identified bilinear thumbnail interpolation as the next
bottleneck, motivating the two-row image cache.

## Artifacts and scope

All raw process/thread samples, screenshots, test/verification outputs, profiles
and matching symbols are in build/codex-wakeups-20261015. Subdirectories
`youtube-layers`, `youtube-spans`, and `youtube-scanlines` hold matching symbols
for each native profile. Parent symbols match the older SDL-only build.
`opacity-summary.py` and `summarize-youtube.py` reproduce timing summaries.

Spans-only APK: lumen-vulkan-spans-debug.apk,
SHA256:d64986c963a8e77b3429594329854dbfc66c3d7762c0d342b15eb4f6e2e909f6.
Final scanline APK: lumen-vulkan-scanlines-debug.apk,
SHA256:cbaf13370e9f7f121d8ce29fa4c87cdd4993287d4c40440752c95d549e95da76.
Both native/dependency and v2signature checks pass. Packaged native binaries were
directly checked for absence of temporary detail-probe/detail-profile/timer/wake
instrumentation strings. No credentials entered.

The first scanline YouTube run (`youtube-scanlines`) hit the existing player error
while paused/during scrolling. Its920 reported frames and12.61ms raster/frame are
EXCLUDED from the paused-video comparison: loss of the video enables different
rendering paths. This is not evidence of fixed playback or a comparable speedup.
The raw run is retained. A fresh earlier-pause run follows.

The valid repeat (`youtube-scanlines2`, paused at0:03) retains the same video frame
and visible paused controls through21 alternating swipes.463 reported frames,
28.001ms raster/frame,35.457ms total paint/frame. ProcessCPU86.797%,peak115%,
RSS335.255MiB (21 samples); no cadence errors. Earlier27 paused samples averaged
3.873%,peak14.706%,RSS329.412MiB, including ongoing load settling. This does not
prove the idle CPU goal. Profile17719 userspace samples, zero lost; raster_rect
63.93% self, fill12.95%, memmove6.77%. There is still substantial raster work.

Control observations on the final build: resuming by center tap played successfully
and hid the controls. A subsequent center tap, and later an off-center tap while
playing, paused the video and showed persistent controls. Thus this did not
validate playing-state reveal/hide timing. Screenshot timing brackets are saved
in controls-events.json, reveal-events.json and offcenter-events.json; captures
cost1-2seconds, so requested delay labels alone are not exact observation times.
The interaction/playback window has173 samples, meanCPU11.90%,peak73.27%; this
mixes playback, pauses and actions and is not an idle or typing benchmark.

Source investigation for next control work: Android finger gestures are converted
to mouse activation; click_page dispatches pointer/mouse down/up/click without a
TouchEvent lifecycle. The YouTube-specific page_move_keep path periodically sends
mouse moves for8seconds, but that is not a native touch sequence. Investigate real
touch dispatch/cancellation/gesture ownership before applying site-specific timing
workarounds. Do not claim the tap behavior's root cause proven without event traces.

Final cleanup: verified the owned monitor/server command lines and working
directories before termination; both sessions joined. App force-stopped, private
paint marker/diagnostic log/perf data removed, owned reverse18765/18766 removed.
Final clean scanline APK remains installed; settings/cookies were preserved. All
samples across the multi-build session had zero cadence/parse errors. No owned
live job remains. Next: further Vulkan raster/composition work (28ms raster and
35ms paint still exceed a60Hz frame budget), controls touch lifecycle and playback
error diagnosis. ChatGPT/Gemini/login, memory goals, zoom and Chrome-style daily
features remain unverified/incomplete. No OpenGL ES/software optimization started.
