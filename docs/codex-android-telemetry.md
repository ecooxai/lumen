# Android process sampling

`tools/android/codex_proc_sample.sh` defines `lumen_read_process_stats`.
Source it in the authorized app's `run-as` shell, then call the function with
the selected `/proc/<pid>` directory. It emits the process stat line followed
by surviving thread stat lines. Discard the entire output on nonzero exit.

The collector reads stat files with the shell's `read` builtin rather than
spawning a reader per thread. It buffers records and calls `printf` once after
the final process-presence check, instead of emitting once per thread. Do not
assume `printf` itself is a builtin on every Android shell. A validation failure
emits no partial sample to stdout; callers must still discard output on any
nonzero exit, including output/transport failures. One `printf` invocation is
not a guarantee of a single OS write or an atomic transport message.
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

Android reports use `SDL_Log`, visible in own-process logcat. The wall line starts
`lumen: paint wall` and contains the actual elapsed `interval_ms`, completed
paint-call counts (`frames`, `partial`, `full`, `blits`), and summed stage
durations. Report pairs occur only during rendering, at most once per elapsed
second. There is no periodic profiling thread or idle wakeup, and disabled
stage hooks do not read either clock. No URL, document title or page data is logged.

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

An accompanying `lumen: paint thread_cpu` line reports the same stages using
Android's `CLOCK_THREAD_CPUTIME_ID`. This measures CPU consumed by the calling
render thread, not the whole process, other GPU/driver threads, GPU execution,
or time waiting for presentation. Its `interval_ms` is still the wall interval;
stage values are summed thread CPU milliseconds. CPU and wall lines are distinct
so a wall-only parser cannot silently treat CPU samples as additional frames.

`frames` is the total completed-paint count, `measured_frames` counts paints
with a valid CPU clock throughout, and `unavailable_frames` counts the rest.
A failed, malformed, or backward CPU clock invalidates that entire paint's CPU
sample; its wall sample is retained. No partial CPU sample is accumulated.
Zero measured frames means unavailable CPU attribution, not zero CPU use.
Non-Android builds retain wall timing and report unavailable thread CPU timing.
Resetting an interval clears both CPU and wall sums and counts.

Both clocks are sampled only inside an explicitly enabled render path, with
no new timers, threads, heap allocations, debugger, perf-event access, root access,
or Android security-property changes. The clock calls themselves add overhead;
these timings do not replace a diagnostics-off benchmark or the 1Hz process
and all-live-thread CPU/RSS measurements. Join CPU and wall reports from the
same process and completed interval before attributing them to an interaction.

Host model and wiring checks: `tests/test_paint_profile.py` and
`tests/test_paint_cpu_profile.py`. Phone-stage
attribution still requires actual opt-in logs from a pinned APK and fixture.
