# Integrated Android verification, 2026-10-15

Device: MI 8, Android 15; serial 2d4f6bc0. Vulkan remains the priority.
The user confirmed phone availability. A fresh read-only preflight also found
Lumen foreground and over an hour without user input. This supersedes earlier
notes waiting for an untouched test window. The old V8 source build remains frozen.

The existing installed APK was verified as SHA256 42af2d0940212a04e9f24e41c726f6887d5b4977be59bf40406c3adf113fa54f.
Whole-process and all-thread /proc monitoring runs at 1 Hz for the full device
session. Artifacts are in build/codex-android-integrated-20261015/.

## Page compiler completion latency

An initial run with existing tabs masked the missed wakeup: other tabs supplied
wakeups. The accepted comparison uses fresh processes with one fixture tab.
No application data was cleared. Visible pre-existing tab URLs were recorded
before restarting; this browser does not yet restore tabs across process restart.

| Measurement | Streaming baseline | Standalone page-wakeup fix |
|---|---:|---:|
| Six small unique Wasm compilations | 1002.7–1008.4 ms | 1.13–4.09 ms |
| Three 800 ms delayed streams | 1814.7–1827.0 ms | 808.5–826.2 ms |
| Fixture checks | 4/6; two latency failures | 6/6 |
| Idle samples | 42 | 40 |
| Mean process CPU | 0.264% | 0.325% |
| Peak process CPU | 5.10% | 6.00% |
| Ending process RSS | 244.59 MiB | 246.60 MiB |

The isolated candidate is 76b1031a246f1f237b9e4d7517d8cf341e6a68e3acccb0d340a159262f1c7be5.
Idle peaks came predominantly from Android HeapTaskDaemon and JIT housekeeping,
not a sustained page event-loop spin. These short intervals show a large latency
fix, not lower RAM use or general scrolling improvement. Logs confirm active
wgpu/vulkan. See baseline-isolated.json, candidate-wakeup.json and samples.jsonl.

## Combined candidate

SHA256 0e5dac825a86ac3f741c5a4f0ae57502fcc23057f981b4b3ce21b4c1bae33a33
includes page wakeups, module cloning, GPU video-upload reuse, page zoom and root
scroll notifications. All following checks ran on this APK on the phone:

- Page wakeup: 6/6.
- Streaming API integration: 27/27.
- Worker lifecycle and compilation: 8/8, including compilation after idle GC.
- Structured clone/module messaging: 15/15.
- Broader Wasm integration: 12/13. Shared Wasm memory sent to a worker fails:
  SharedArrayBuffer could not be cloned. Full browser Wasm support is unfinished.

The worker fixture first reports seven checks, then runs its eighth after a
15-second idle interval. The initial runner captured the first report; the final
8/8 report was recovered from server.log before advancing. The runner now waits
past idle-start. Its stage labeled worker-idle includes that final compilation.
The combined wakeup idle label includes the final paint in its first sample;
exclude that sample when discussing settled CPU. Subsequent fixtures separate
three seconds of settling before the idle stage. Raw samples are retained.
Streaming/clone/broad idle means were 0.36–0.40%, with brief Java housekeeping
peaks near 6–7%; ending RSS 232–238 MiB. These fixtures differ, so their memory
numbers are not a controlled RAM comparison.

GPU before/after measurement and Android zoom/scroll checks continue below.
YouTube acceptance, real ChatGPT/Gemini/Google login, pinch zoom, shared-memory
isolation, and daily-browser performance are still outstanding. Credentials have
not been used. OpenGL ES and CPU software optimization remain deferred.

## Isolated Vulkan video-upload comparison

Baseline ebc5d77a... (module-clone APK) and candidate c1bf28eb... differ by the
video-upload reuse change. Each loaded the local thumbnail-scroll fixture,
paused the same 640×360 video after it became ready, settled ten seconds, then
received ten upward and ten downward 450 ms swipes. Both used saved pixel_scale=200,
renderer=auto with active wgpu/vulkan, smooth scrolling, and native video controls.
The debug paint marker enabled the same timing instrumentation in both builds;
it was removed afterward. The latest combined APK was restored after comparison.

| Measurement | Before | After |
|---|---:|---:|
| Scroll whole-process CPU, ten 1 Hz samples | 52.94% | 53.47% |
| Scroll peak CPU | 58.42% | 56.00% |
| Presentation phase thread CPU/frame | 2.336 ms | 2.178 ms |
| Presentation phase wall time/frame | 3.622 ms | 2.641 ms |
| Raster phase thread CPU/frame | 4.707 ms | 4.720 ms |
| Settled idle CPU, 30 samples | 0.368% | 0.364% |
| Ending idle RSS | 251.32 MiB | 250.61 MiB |

Profile means include ten active intervals with at least 40 painted frames and
less than 1500 ms duration: 579 baseline frames and 585 candidate frames. This is
paint instrumentation, not SurfaceFlinger presentation timing or a dropped-frame
measurement. The CPU sample at a stage boundary may straddle two stages. One
short sequential comparison cannot separate thermal/run noise from a small gain.

The presenter uses less measured time, but total process CPU is effectively
unchanged. Do NOT claim a large scroll or RAM improvement. Rasterization remains
the largest measured rendering CPU cost and needs further Vulkan-path work.
Idle peaks near 6–7% again correspond to Android housekeeping. Screenshots before
and after show the fixed video and thumbnails aligned; broader source/seek/format
and fullscreen checks remain pending.

The first comparison automation attempt queried pidof before process startup
completed and stopped without sending gestures. It was corrected to read the
PID after fixture readiness, then both comparisons completed. The retained
initial failed-attempt load samples must not be included in active-scroll data.

## Phone zoom and root-scroll behavior

On the combined APK at saved Android pixel_scale=200, five Ctrl+= steps changed
100% to 200% page zoom: CSS viewport width540→270 and DPR2→4. A physical tap
reached the page button at CSS (90.08,45.03), and a frame button at (40.06,30.07)
with frame DPR4. Checks allow 0.25 CSS pixels (one physical pixel) for Android's
normalized touch coordinates; exact float equality was an invalid initial test
expectation. The frame retained its 160 CSS-pixel width. A native swipe scrolled
the nested container about95 CSS pixels while root scrollY stayed0. Ctrl+0 reset
to width540/DPR2. These are reflow zoom tests, not pinch support.

At100%, root document scroll reports were [120,240,0,1016,0], each with a trusted
bubbling document-target event reaching the window. A handler-induced second
scroll completed, fixed-position touch targeting worked after scrolling240px,
the bottom clamped to1016, and repeated Top at0 emitted no extra scroll event.
The iframe scrolled75px, emitted its own document/window events, and left the
parent at0 without an extra parent event. Assertions and screenshots are saved.
Final root idle:59 samples, mean0.102%, peak1.010%, RSS233.26MiB. Earlier root
idle:55 samples, mean0.200%, peak5.94% (Android housekeeping).

Android does not expose this fixture's console output through the captured
logcat path, so the fixtures now also POST their reports to the local test server.
The server gained explicit zoom/root/frame routes. HTTP reporting itself can
wake the browser: these phone event checks do NOT independently prove the16ms
follow-up scheduler deadline. The earlier host-only queue tests establish that.
Fixture JavaScript/Python syntax and git diff whitespace checks pass.

All test tabs were closed by the final restart; the pre-existing Google URL and
three blank tabs were recreated, with Google selected. This restores URLs, not
previous scroll/history state. Cookies/settings/history files were not cleared.
Session/tab restoration across process death remains a daily-browser gap.

Cleanup complete: owned server and monitor exited; adb reverses18765/18766
removed; profile marker removed. Final APK hash and Vulkan log verified.
Monitoring retained 1052 samples, 0 parsing errors, and
0 intervals over1.5 seconds. Sources remain uncommitted. Goal active.
