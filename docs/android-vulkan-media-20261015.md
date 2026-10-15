# Vulkan video conversion and audio lock repair — 2026-10-15

The daily-browser goal remains active. Vulkan remains first priority; OpenGL ES
and CPU/software mode acceptance are deferred. This is a bounded playback and
performance improvement, not full Chrome parity or daily-use readiness.

## Changes

Common decoded planar 8-bit YUV420 frames now use the existing GPU NV12 presenter.
The new image helper copies luma and interleaves chroma without CPU RGB conversion.
It supports odd sizes, padding, and negative strides, retains matrix/range metadata,
and bounds allocation before multiplying dimensions. Supported BT.601/BT.709
frames up to the existing decoder width limit use this route when the presenter
supports YUV. Unsupported matrices/formats retain the existing conversion route.
The existing raster fallback materializes RGB when needed. Queued-frame memory
accounting now counts the actual YUV and/or RGB allocation.

The initial local baseline froze twice within approximately four seconds. Review
found a lock inversion: clock reads held the player mutex then queried the SDL
stream, while SDL invokes the audio callback with the stream lock held and the
callback takes the player mutex. Seek also cleared the stream under that mutex.
The callback now records queued-audio latency and timestamp; clock readers use
that snapshot and interpolate only up to decoded audio time. Pause freezes time.
Seek clears the SDL queue after releasing the player mutex, while seeking remains
true. This removes both opposite lock acquisitions without dropping audio on a
failed try-lock. Android native backtrace access was unavailable without root;
thread wait states, code inspection, deterministic concurrency tests, and device
reproduction/recovery provide the evidence instead.

The clean-build acceptance check exposed a separate paused-video problem:
29 seconds averaged 7.3448% CPU. Thread counters attribute 2.76% to SDLAudioP,
2.62% to AudioTrack, and 1.21% to the decoder awakened by silence callbacks.
Device simpleperf sampling was blocked by the device's perf_harden setting;
that setting was not changed. Logical device pause alone still averaged 5.0293%
CPU over 29.03 seconds because SDL kept its physical Android output running.
The final main-thread media pump closes the output device while retaining the
stream and its queued samples. Resume reopens/rebinds the device and refreshes the
queued-audio clock snapshot. All output operations remain outside the player
mutex. The SDL dummy-device test verifies callbacks stop and restart, queued data
survives pause, and ended playback closes its device too. Delayed MSE audio
metadata no longer causes the audio clock to be prematurely disabled.

References: [SDL device close](https://wiki.libsdl.org/SDL3/SDL_CloseAudioDevice),
[SDL stream binding](https://wiki.libsdl.org/SDL3/SDL_BindAudioStream),
[SDL device pause](https://wiki.libsdl.org/SDL3/SDL_PauseAudioStreamDevice),
[SDL device resume](https://wiki.libsdl.org/SDL3/SDL_ResumeAudioStreamDevice),
[SDL callback locking](https://wiki.libsdl.org/SDL3/SDL_AudioStreamCallback),
[FFmpeg frame strides](https://svn.ffmpeg.org/doxygen/trunk/structAVFrame.html),
[FFmpeg planar formats](https://www.ffmpeg.org/doxygen/trunk/pixfmt_8h.html).

## Matched phone measurement

MI 8, Android API 35, Vulkan, pixel scale 200, saved settings unchanged. Generated
45-second 1280x720 H.264/AAC sample with explicit SMPTE170M matrix metadata, a
translucent page overlay, and no input during the measured window. Both comparison
runs use the audio-lock repair and the same diagnostic APK. A one-launch test flag
disables GPU YUV only for the BGRA baseline. Both traces advance beyond 30 s, unlike
the discarded frozen baseline. Native diagnostics and the private script hook were
temporary local instrumentation and are absent from final product source/APK.

| Metric | CPU BGRA conversion | Vulkan YUV conversion |
| --- | ---: | ---: |
| Accepted same-stage 1Hz intervals |24|24|
| Measured time |24.01s|24.00s|
| Mean process CPU (100%=one core) |81.8409%|56.4167%|
| Peak process CPU |88.1188%|66.6667%|
| Mean resident memory |318.6978MiB|300.9188MiB|
| Resident memory range |314.77–323.07MiB|299.70–304.44MiB|

This is about 31% lower CPU and 17.78MiB lower mean RSS for this sample, not a
whole-browser or general-site benchmark. Each 720p YUV frame is 1,382,400 bytes
instead of 3,686,400 bytes for BGRA before any fallback materialization. PNGs show
correct test colors and the translucent overlay. Seek-to10s advances, pause holds
at 13.50 s, identical paused PNG hashes persist across 3 s, and resume reaches 16.66 s.

## Validation

14 distinct focused tests pass: 2 media tests, 5 GPU upload tests, 2 YouTube control
tests, 1 raster-layer test, 4 display-damage pixel tests. The packing test covers
4,490 layouts, 12 known color conversions, invalid dimensions/pointers/strides,
and idempotent software materialization under ASan/UBSan. The audio concurrency
test runs the real media functions against an SDL stream held as in its callback.
Restoring the old clock query causes operation 0 to time out; separately restoring
the old seek lock order causes operation 1 to time out. Both negative controls
fail as intended. Final dimension-overflow hardening was retested with sanitizers.
All compilation used tools/codex_compile.py (shared 480% quota, idle/nice19,
Gradle --no-daemon); the frozen V8 source build was not touched.

Final compact APK: build/codex-media-20261015/lumen-vulkan-media-debug.apk,
85,240,084 bytes, SHA256
`767a83df50ec227b786aa094cfaeab80d24ad8f49b1bbd8f8ea4f792d6b82b16`.
Native ELF/dependency closure for arm64/API 29 and APK v2 signature checks pass.

## Final APK phone acceptance

The final APK was installed with `adb install -r`, preserving app data. In the
same local 720p fixture, 19 seconds of playing intervals averaged 55.6842% CPU
(peak 59%), RSS 300.375–304.699 MiB. After pause, 28.99 seconds averaged 1.3108%
CPU (peak 4%), RSS 302.816–303.051 MiB. The paused screenshots are byte-identical
across 30 seconds. Playback resumes from 27.133 s to 31.300 s; seeking to 10 s
then playing reaches 14.467 s and pauses correctly. This final acceptance follows
the matched BGRA/YUV comparison above; it is not a separate matched baseline.

## Real-site limits and follow-up

YouTube traces show continuous filled audio/video queues on successful runs,
but other runs close the player. A player API returns HTTP400
FAILED_PRECONDITION and some video requests return403; these responses also occur
in successful playback, so they are not established as the cause. No server
restriction or CORS check was bypassed. The repaired diagnostic candidate reaches
356.32 seconds of video time in a 360.80-second trace, with a brief manual
pause/resume. The final clean APK's independent run was still blank/loading at
25 seconds and displaying video by 55 seconds, then reached the same visible
"Something went wrong" error at video time 0:56. Its controls/settings still
responded, but playback had failed, so final YouTube acceptance FAILS. The audio
clock/lifecycle changes are not established as the cause or a cure of this
intermittent failure, which was also observed before them. The final observation
window after 25 seconds mixed load, playback, and failure: 74.03 seconds averaged
82.33% CPU (peak 201%, RSS 235.1–521.0 MiB). It is not a steady playback benchmark.
Local deterministic playback remains the verified performance result.

Earlier controls repair is verified visible 7 seconds after resuming playback;
the gear/settings menu also opens over playing video with correct translucency.
The hovered play button can keep controls visible longer; complete touch semantics
still need review. Frame quality and requestVideoFrameCallback remain stubs.
Android hardware decoding is not implemented. ChatGPT/Gemini login, composer CPU,
pinch/page zoom, and remaining daily-browser features are still open. No test
credentials were entered or persisted during this work.

Ignored evidence: build/codex-media-20261015 (1Hz process/thread samples, screenshots,
private public-page diagnostic traces, fixtures, comparison.json, build log, and
negative-control sources/binaries). No account data was captured intentionally.

App settings compare byte-for-byte before/after testing. App, local reverse/server,
one-shot diagnostics and sampler were cleaned up after the final failure capture.
The daily-browser goal remains active; next work should investigate the YouTube
failure on Vulkan, without moving to OpenGL/software acceptance yet.

Telemetry audit: 2,752 one-second process/thread samples, zero command failures,
parse errors or cadence errors. Sampler joined, both phone locks released, app
PID absent, and adb reverse mappings absent at cleanup.
