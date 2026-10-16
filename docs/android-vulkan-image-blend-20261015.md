# Vulkan image blending on ARM64 (2026-10-15)

Status: implemented, tested on MI8, built and installed. Daily-browser goal
remains active. This follows the circular CPU page cache improvement.

## Implementation and correctness

The existing image_vertical loop was already compiler-vectorized, but processed
four packed pixels with repeated channel masks/shifts. The ARM64 little-endian
NEON path now deinterleaves eight premultiplied pixels into channel vectors.
It truncates each interpolation product separately, preserving scale_px's
integer results, and uses a 16-bit source-over factor to represent 256 for
transparent pixels. Endpoint interpolation weights use the appropriate source
directly. The scalar tail and other architecture paths remain available.
No new allocation or retained image copy is introduced.

This reduces raster CPU feeding the Vulkan presenter. The helper is shared
rendering code, but OpenGL/software runtime optimization/testing is deferred.

Host ASan/UBSan raster tests pass. The same raster suite was cross-compiled with
Android NDK clang, UBSan and -O2 and ran successfully on the actual MI8. It covers
every interpolation weight, lengths 0..71, alpha endpoints, every alpha value
with pseudorandom premultiplied colors, canaries, source-over, fractional image
edges, partial damage, nested layers, circular rows and padded strides. The
scalar oracle samples source pixels independently of the scanline cache.
Android binary disassembly confirms the new deinterleaved load instructions.

Native and Android browser builds, APK native dependencies/API29/arm64 ABI,
v2 signature and git whitespace checks pass. All compilation used the shared
codex_compile.py quota. The old V8 source build remained frozen.

## Phone comparison

Artifacts: `build/codex-vulkan-neon-20261015/`.
Baseline APK is 28a773cb... (circular CPU page cache); candidate is 4af25ce3....
Order was baseline1, candidate1, baseline2, candidate2. Each used a fresh app
process, Vulkan, the thumbnail-scroll fixture with paused video, saved pixel
scale 200, 10 seconds settling, 20 identical 450 ms swipes (10 each direction),
and 30 seconds idle. Process and all threads were monitored at 1 Hz throughout.
100% CPU corresponds to one occupied core.

| Measurement | Baseline 1 | Candidate 1 | Baseline 2 | Candidate 2 |
| --- | ---: | ---: | ---: | ---: |
| Scroll process CPU | 47.20% | 44.35% | 47.03% | 44.95% |
| Raster thread CPU/frame | 3.782 ms | 3.160 ms | 3.844 ms | 3.169 ms |
| Raster wall/frame | 3.881 ms | 3.269 ms | 3.944 ms | 3.291 ms |
| Presenter thread CPU/frame | 2.359 ms | 2.416 ms | 2.338 ms | 2.440 ms |
| Presenter wall/frame | 2.861 ms | 2.903 ms | 2.842 ms | 3.235 ms |
| End-scroll RSS | 254.59 MiB | 254.20 MiB | 251.90 MiB | 254.35 MiB |
| Settled idle CPU | 0.439% | 0.266% | 0.369% | 0.459% |

Both comparisons show 4–6% less process CPU and 16–18% less raster CPU/frame.
Presenter timings increased; overall savings are smaller than raster savings.
RAM is essentially unchanged. Short fixture runs do not establish general
website performance or an FPS improvement. Sampling had no parse/cadence errors.
Isolated idle peaks (3.96–8.74%) were predominantly Android HeapTaskDaemon/JIT/
Profile Saver work. Another 67 settled samples after profiling averaged 0.074%,
peaking at 2.0%, RSS 250.84 MiB.

Each paired screenshot comparison matched all 1,690,200 pixels in the content
rectangle x=0..1079, y=540..2104; video, browser chrome and system UI are excluded.
Midpage wrapped rendering was also visually inspected without corruption.
See pixel-comparison.json and candidate-wrapped.png.

## Remaining hotspots and timing evidence

A separate 26-gesture user-CPU profile at 499 Hz recorded 2706 samples with zero
lost. Raster_rect remains 30.86% of samples, fill 12.82%, memmove 12.53%, memset
3.10%. These are relative symbol shares, not absolute time measurements. The
current unstripped libmain matches this candidate, not the previous baseline.

SurfaceFlinger exposes usable timing for the app's BLAST SurfaceView. The dump
contains desired presentation, actual presentation and frame-ready timestamps,
as defined by [AOSP FrameTracker](https://android.googlesource.com/platform/frameworks/native/+/master/services/surfaceflinger/FrameTracker.cpp).
The captured 127-frame tail from the profiled candidate has a 16.707 ms median
actual presentation interval, six of 126 intervals above 25 ms (maximum
33.422 ms), and none above 50 ms. This is a partial tail under profiling, with
no paired baseline and possible gesture transitions; it does not prove a
dropped-frame rate or smoother scrolling. Next compare full presentation traces
over matched active gestures, then target the remaining raster/upload work.

## Artifact and remaining scope

Installed APK: `build/codex-vulkan-neon-20261015/lumen-vulkan-neon-debug.apk`

SHA256: `4af25ce3e5242b8ea9aab899d09c96ba29b5afd06a360e069ba7d2a4b7a0226e`

Installed hash was read back and matched; Vulkan remains active. Google search
URL and three blank tabs were recreated, Google selected. Cookies/settings
were not cleared; exact old scroll/history is not restorable without session
persistence. After substantial Google loading, the final 30 samples averaged
2.331% CPU, peak 5.051%, RSS 388.93 MiB. Real-page load cost remains open.
There were 506 total process/all-thread samples, with zero parse/cadence errors.
Owned build/test/comparison/profile/monitor/server sessions finished and were
joined. The owned adb reverse, standalone test executable and app profile
marker/data were removed.

Includes all pending changes from previous checkpoints. Worktree uncommitted.
YouTube acceptance, ChatGPT/Gemini/Google login, pinch zoom, session restoration,
shared Wasm worker memory and cross-origin isolation remain unfinished. No login
credentials were used and no broad Wasm retest was run for this raster change.
