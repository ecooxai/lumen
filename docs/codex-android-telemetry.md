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
