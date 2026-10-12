# Existing Android build guard

`tools/android/build_guard.py` monitors an already-running Linux build. It never
launches, resumes or restarts a compiler. Use `/usr/bin/python3` with Linux pidfd
support; some locally installed Python interpreters lack that support.

Write an explicit JSON configuration containing `pid`, `start` (Linux process
start ticks), `uid`, `parent_pid`, `parent_start`, `scope` (absolute unified
cgroup path), `max_cpu_percent`, `ancestor_limits` (path-to-percentage map),
`max_rss_bytes`, `min_available_bytes`, `max_compilers`, `input_hashes`
(absolute input paths mapped to SHA-256 hashes) and `log` (Ninja log path).
CPU percentages use one logical CPU as 100%, not the whole host.

Run in a persistent service with restart disabled:

```sh
/usr/bin/python3 -B tools/android/build_guard.py --config CONFIG.json --output OUTPUT_PREFIX
```

The guard checks identity, descendants, cgroup placement, quota hierarchy,
per-thread SCHED_IDLE/nice 19, compiler count and memory every second. Input
hashes are checked initially and every 60 seconds. It records cgroup CPU usage,
summed process RSS and Ninja progress. Use a fresh output prefix each time.
An existing samples file fails closed rather than overwriting evidence.

Failures and SIGTERM/SIGINT/SIGHUP attempt to freeze only the configured scope
and SIGSTOP identity-verified build processes through pidfds. Scope-freeze
permission must be provisioned separately; the guard does not elevate itself.
If scope freezing fails, the alert records that limitation and per-process
stop results. SIGKILL, host failure and scope migration between samples cannot
be made fail-safe by this userspace monitor.

Wait for `-ready.json`, verify fresh `-latest.json` samples and current placement
before manually resuming a stopped build. Readiness alone does not resume it.
Keep the original process owner alive: only it can report the actual build exit
status. The guard's successful exit means observation completed, NOT that the
compiler succeeded; its summary deliberately leaves `exit_code` null.

Tests: `/usr/bin/python3 -B tests/test_build_guard.py`.

The October 12 recovery uses a dedicated scope directly below `cpulimit.slice`:
its own 320% ceiling retains the existing shared 200% ancestor ceiling. The
host watcher exemption is restricted to that exact scope and quota. Nesting
below the host's threaded `cpulimit-70.slice` failed with EUCLEAN before any
process resumed. No unrelated host CPU limit should be raised to recover V8.
