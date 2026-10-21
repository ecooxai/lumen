# Agent notes for Lumen

See `CONTINUE.md` for project state, build layout and next steps.

## Host compilation CPU budget

All new project compilation may share 60% of all host logical CPUs, per the
user's latest instruction (480% total on this eight-core host). This is a
shared budget, not 480% per task. Keep at most six compiler jobs per build;
use fewer for memory-heavy work. The old V8 source build remains safety-frozen;
do not resume or restart it while adopting the verified prebuilt.
Launch those commands through `python3 tools/codex_compile.py COMMAND ARGS...`.
The wrapper uses the shared systemd user slice
`codex-lumen-compile.slice` with `CPUQuota=480%` and a 100 ms period, and fails
closed if quota setup fails. The compatibility `--v8` option uses the same
shared slice. All builds inherit SCHED_IDLE and nice 19. Use Gradle
`--no-daemon` and do not reuse compiler daemons launched
outside the slice. Do not change unrelated host workloads. This is a host build limit,
not an Android browser runtime performance result.

Avoid unnecessary shell wrappers inside the capped scope. This host's ananicy
bash rule assigns nice -4, and two packaging checks stopped on that niceness
violation. Invoke Python/Java tools directly. For SDK apksigner verification,
use the same SDK's `lib/apksigner.jar` through `java -Xmx1024M -jar`, preserving
all verification arguments, instead of its shell launcher. Do not change global
host rules or weaken priority checks to work around this.

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

## Monitoring subagent during every test

Every Lumen test run (phone or host) gets a second, separate monitor subagent
(Haiku 5.5, `model: haiku`, `effort: high`), started alongside the test
subagent and given the same privacy rules. It never taps or types. For the
whole run it samples Lumen's CPU and RAM (RSS) every second (on the phone a
host-side `adb shell top -b -n1 -p PID` loop, since a device-side loop can die
early). It also watches errors: on the phone logcat crashes, ANRs, tombstones,
native `lumen` errors, uncaught JS exceptions and console errors, and
GPU/Vulkan/wgpu validation errors; on the host Lumen's stderr. Its final report
to the main agent gives:

- CPU min/median/max and the last-10-s median;
- peak and final RSS;
- error counts with the first line of each distinct error.

It returns early with the finding as soon as something is abnormal:

- a crash or ANR;
- settled idle CPU above 5%, or typing above 10%;
- sustained high CPU with no heavy work going on;
- RSS still growing after the page settled, or above 1.5 GB on the phone;
- a repeating error.

The main agent then investigates and fixes the cause (profile the hot path,
find the leak or the error source) before moving on, and reruns the test with
a fresh monitor.

## Device test pages

`python3 tools/device_tests.py PAGE|GROUP ...` opens Lumen's test pages on the
phone (debug build), samples Lumen's CPU every second and prints the pages'
numeric results. Pages: cam, screen, mic, webgl, webgpu, aquarium, chatgpt
(newest chat bookmark), google, youtube, utils (WebGPU Weather World is under
its apps menu). Run `all` before an important commit or a release; for a small
fix run only the relevant pages (e.g. `google`, or `gfx` for WebGL/WebGPU
work). Each page gets its own cold start; `--tabs` opens them together, one
tab each (background tabs are throttled, so measure there only the front tab).
The script never taps or types; screen recording needs its system prompt
accepted. A settled site whose last-10-s CPU exceeds 5% is reported.
Sites are tested in desktop mode (Desktop site is on for all sites by
default): the desktop versions must work.

## Subagents for build, test and device work

Delegate every build, install/deploy, test run and computer-use or phone-control
task (adb taps, screenshots, page probes, CPU sampling) to a subagent running the
latest Claude Haiku model (currently Haiku 5.5, `model: haiku`) with high
reasoning effort (`effort: high`). Start a fresh subagent for each task with a
self-contained prompt that includes the compile rules above, the CPU-monitoring
requirement and the privacy rules: never type into page inputs, describe
third-party pages only structurally, and delete screenshots that show personal
content. It iterates at most five times, fixes only simple problems and returns a
short report. Writing and debugging code stays in the main agent, and source
files are not edited while a subagent is building.
