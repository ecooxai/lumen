# Android process sampling

`tools/android/codex_proc_sample.sh` defines `lumen_read_process_stats`.
Source it in the authorized app's `run-as` shell, then call the function with
the selected `/proc/<pid>` directory. It emits the process stat line followed
by surviving thread stat lines. Discard the entire output on nonzero exit.

The collector uses shell builtins rather than spawning a reader per thread.
A task disappearing between enumeration and reading is skipped only when its
directory is gone. This is a directory-lifetime check, not numeric errno
classification. A read failure for a still-present task remains fatal, including
a missing stat file. Empty/partial records, missing threads and a process
disappearing during collection also fail. The function does not parse stat fields.

The caller must still verify PID/start-time identity, parse complete records,
sample process CPU/RSS and thread CPU every second, enforce cadence, and retain
transport/permission errors. Never sum per-thread RSS. Continue monitoring during
GUI review and cleanup; mark only an intentional force-stop as the stopping phase.
An interrupted run cannot be reported as a complete continuous measurement.

Host fixture tests:

```sh
python3 tools/codex_compile.py python3 tests/test_android_proc_sample.py
```

Host tests are not Android-shell, phone-performance or browser-compatibility proof.
Keep historical failed-run evidence and pin the collector used by each fresh run.

## Opt-in paint-stage diagnostics

The native renderer can aggregate completed paint calls without debugger
attachment. Set `LUMEN_DEBUG_PAINT=1` before startup, or, in an Android Debug
build only, place a file named `paint-profile.enabled` in the app's SDL
preference directory. Its entire contents must be the single byte `1`, with
no newline. The directory is app-private; use an authorized `run-as` session,
not shared storage or a browser URL. Release builds ignore this marker.
Activation is checked once after preferences load. Remove only the marker
created for the test and stop the app after profiling; restarting without it
disables the additional diagnostics. Do not enable a property or attach LLDB.

Android reports use `SDL_Log`, visible in own-process logcat. Each line starts
`lumen: paint wall` and contains the actual elapsed `interval_ms`, completed
paint-call counts (`frames`, `partial`, `full`, `blits`), and summed stage
durations. Reports occur only during rendering, at most once per elapsed
second. There is no periodic profiling thread or idle wakeup, and disabled
stage hooks do not read the clock. No URL, document title or page data is logged.

- `prepare_ms`: frame/page buffer sizing and partial-paint eligibility setup.
- `layout_dl_ms`: layout when needed, display-list building and video-bar scan.
- `signatures_ms`: damage signatures, media/fixed-area and blit eligibility.
- `raster_ms`: page work including scroll memmove, exposed-strip/full raster,
  signature-cache updates and video-quad preparation. Video-only or empty-page
  work that skips list building also falls in this stage.
- `compose_ms`: browser UI, page-to-frame copy and popup composition.
- `present_ms`: GPU upload/submission/presentation or SDL software fallback.

These are **wall-clock durations**, including scheduling and presentation
waits, not thread CPU, GPU execution time, displayed FPS or a statistical
sample profile. `frames` counts renderer calls, not confirmed display frames.
The interval may include long idle gaps; do not label summed milliseconds as
milliseconds per second without dividing by that actual interval. The final
incomplete interval is not emitted during idle. Logging/timestamps themselves
perturb measurements. Pair diagnostics with the uninterrupted 1Hz process and
thread CPU/RSS sampler, and use a separate diagnostics-off run for comparisons.
Legacy stderr diagnostics are not a substitute for these Android stage logs.

Host model and wiring checks: `tests/test_paint_profile.py`. Phone-stage
attribution still requires actual opt-in logs from a pinned APK and fixture.
