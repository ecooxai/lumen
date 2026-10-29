# Android Vulkan circular page cache (2026-10-15)

Implemented, built, installed and measured on the MI8 (Android 15, arm64).
The renderer reports `wgpu/vulkan`. This is a progress checkpoint; daily-browser
acceptance and full Wasm support remain incomplete.

## Change

The Vulkan scroll path already reused GPU texture rows, but shifted most of the
CPU page bitmap with memmove on each scroll frame. The CPU page now uses a row
offset into the same allocation. Rasterization, opacity-layer composition,
damage comparisons and fallback composition address logical rows through that
offset. Page uploads split at both CPU and GPU wrap boundaries. Linear-source
GPU entry points remain available, and a software fallback copies logical rows
in display order. No additional page bitmap is allocated.

This change targets CPU work feeding Vulkan presentation. OpenGL and software
renderer optimization remains deferred according to the user's priority.

## Controlled device comparison

Artifacts are in `build/codex-vulkan-raster-20261015/`. Baseline was the combined
scroll-events APK `0e5dac82...`; candidate adds the circular CPU page cache.
Both runs used the local thumbnail-scroll fixture, a paused 640x360 video,
30 scaled thumbnails, saved pixel scale 200, and 20 identical 450 ms gestures
(10 up, 10 down). Process and all threads were sampled at 1 Hz throughout.
There were no sampling parse/cadence errors. CPU uses 100% for one occupied core.

| Measurement | Baseline | Candidate |
| --- | ---: | ---: |
| Scrolling process CPU, 10 samples | 52.97% | 49.42% |
| Scrolling peak CPU | 56.57% | 53.00% |
| Raster thread CPU per frame | 4.737 ms | 3.688 ms |
| Raster wall time per frame | 4.818 ms | 3.780 ms |
| Presenter thread CPU per frame | 2.177 ms | 2.379 ms |
| Presenter wall time per frame | 3.120 ms | 2.852 ms |
| End-of-scroll RSS | 255.01 MiB | 254.75 MiB |
| Settled idle CPU, 30 samples | 0.334% | 0.301% |
| End-of-idle RSS | 251.13 MiB | 251.09 MiB |

This short comparison shows about 6.7% less process CPU and 22% less raster CPU
per frame. RAM is essentially unchanged. It does not establish an FPS or
dropped-frame improvement, general website performance, or Chrome parity.
Per-frame timings include 10 active intervals, 579 baseline / 583 candidate
frames. Presenter CPU increased even though wall time decreased.

Use `controlled-idle.json` for the paired 30-sample idle comparison. The broader
`ring-after-idle` stage in raw samples also includes time after the comparison,
including the next profile's restart/loading before its stage marker changed;
its aggregate is not a settled-idle measurement. The controlled idle peaks of
6% were predominantly Android HeapTaskDaemon/JIT work. After wrap/resume/zoom,
40 additional settled samples averaged 0.150%, peaking at 1.980%.

Separate identical 26-gesture simpleperf runs sampled user CPU at 499 Hz for
15 seconds. Baseline recorded 3268 samples; candidate 2988; neither lost samples.
The memmove symbol's share fell from 26.29% to 10.17%. Raster_rect remains the
largest symbol (30.75% to 37.58% of samples); baseline address resolution points
to scaled image processing. These percentages are relative profiles, not direct
absolute timing comparisons. Baseline symbolization was saved before rebuilding;
the current unstripped libmain matches only the candidate.

## Correctness and build checks

- GPU upload model: 15/15 under UBSan, including independent source/destination
  wrapping, direction reversal, fixed overlap, padded strides, full uploads at
  every source offset, and invalid-offset rejection without GPU state mutation.
- Android page transfer: 20/20 under UBSan, including wrapped-source direct
  presentation and fallback composition.
- Raster layers: ASan/UBSan pass. Existing independent pixel expectations and
  partial-damage comparisons now cover circular rows, nested opacity, images,
  gradients, shadows, text, punched video and padded strides.
- Native browser build, Android build, APK native dependencies, minimum API 29,
  arm64 ABI and v2 signature checks pass. Compilation used codex_compile.py;
  the frozen V8 source build was not touched.
- Baseline/candidate screenshots after the 20 gestures match exactly over
  1,690,200 pixels in the content rectangle (x=0..1079, y=540..2104), excluding
  video, browser chrome and system UI. See pixel-comparison.json.
- Midpage wrap, app background/resume, zoom increase and reset were visually
  checked on the phone without visible corruption. Software fallback correctness
  is covered by host models; it was not forced on the phone.

The restored Google result page initially averaged 67.3% CPU during 28 seconds
of continued loading. A follow-up observation and 15-second simpleperf profile
checked whether this persisted. The final 30 samples averaged 2.200%, peaking at
3.030%, RSS 367.70 MiB. The profile recorded only 175 samples (350.7 ms user CPU);
js_tick was the largest named symbol at 8% of samples. One earlier transient
56.4% interval involved V8 worker threads while RSS fell by about 13 MiB; this
is consistent with memory reclamation but was not conclusively attributed.
Google's loading cost still needs further work. These real-page observations
are separate from the controlled thumbnail comparison; they do not validate
ChatGPT/Gemini or login.

## Installed artifact and cleanup

`build/codex-vulkan-raster-20261015/lumen-vulkan-cpu-ring-debug.apk`

SHA256: `28a773cbb46b0212e8837e75d2404c3eaba9219dfff79156b7045d136b38071b`

Installed APK hash was read back and matched. Vulkan remains active. Google
search URL and three blank tabs were recreated, Google selected. Cookies and
settings were not cleared; original per-tab scroll/history cannot be restored
because session persistence remains missing. Credentials were unused.
Owned monitor/server/profile sessions were joined; the owned adb reverse and
app profile marker/data were removed. Worktree changes remain uncommitted.

Next: reduce scaled-image raster cost in the Vulkan path, then verify real
YouTube playback/controls and ChatGPT/Gemini/Google login. Pinch zoom, session
restoration, shared Wasm worker memory and cross-origin isolation still need
work. The previous integrated Wasm results remain applicable, but no broad Wasm
retest was run for this rendering-only change.
