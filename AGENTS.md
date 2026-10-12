# Agent notes for Lumen

See `CONTINUE.md` for project state, build layout and next steps.

## Host compilation CPU budget

All project compilation on this host, including V8, dependencies, Gradle and
test binaries, must share a maximum of 140% CPU (1.4 logical cores), not 140%
per task. Launch build commands through `python3 tools/codex_compile.py COMMAND
ARGS...`. The wrapper uses the shared systemd user slice
`codex-lumen-compile.slice` with `CPUQuota=140%` and a 100 ms period, and fails
closed if quota setup fails. Retain at most two compiler jobs per build to bound
memory. Use Gradle `--no-daemon` and do not reuse compiler daemons launched
outside the slice. Do not change unrelated host workloads. This is a host build limit,
not an Android browser runtime performance result.

## CPU monitoring during every test

Whenever you run Lumen for a test (GUI, ChatGPT, scrolling, typing, video,
benchmarks), sample its CPU every second for the whole run and check it:

```sh
P=$(pgrep -n lumen)
while kill -0 $P 2>/dev/null; do ps -o %cpu=,rss= -p $P; sleep 1; done
```

- Idle pages (nothing animating, no input) should sit near 0-3% CPU.
- Ordinary interaction (typing, scrolling, hover, clicking) should stay
  low; typing in the ChatGPT composer must stay under 10%.
- Sustained high CPU is only acceptable while doing real heavy work (page
  load, video decode, a long streamed reply). Anything else is a bug:
  profile it (`sample <pid> 5 -file out.txt` on macOS, `perf` on Linux),
  find the hot path and fix it before moving on.
- Report the measured CPU numbers with the test results.
