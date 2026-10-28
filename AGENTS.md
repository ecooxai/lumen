# Current task: Lume Apple WPT 10290244

Read `agent/task.md` and `agent/continue.md` before work.
Canonical source: `/Users/devin/repos/lumen` (requested `~/project/lumen`).
Current branch: `gpt6-astra-pro_release-integrator_lume-apple-wpt-10290244`.
Inherited tracked source checkpoint: `a8bd64552f9379cca33cd736a187924ec18eb356`.
Session: 2026-10-29 02:47:35 Asia/Shanghai to 05:47:35 (three hours).

## Current ownership and acceptance

- `release_integrator` owns Git staging/commits, task/handoff/timing records, and progress publication.
- `wpt_engine` owns standards and engine fixes with regression-first tests.
- `apple_runtime` owns Apple packaging/build/runtime and resource sampling.
- Root manages and brokers remote I/O because child MCP calls hang; three built-in workers inherit the same model and reasoning level.
- Coordinate shared source edits; serialize heavy builds. Preserve existing untracked evidence and tooling.
- Sample CPU/RAM every second for every test/build. Record wall times; investigate abnormal runs over five minutes. Reuse matching prebuilt V8 when supported.
- State WPT subtest numerator/denominator, file harness completion, exact suite and revision. Focused WPT and custom regressions never establish full-suite Chrome parity.
- The goal is 98/100 for the whole Apple task. Keep unverified gates visible; a build or package alone does not satisfy the goal. Same-IPA acceptance requires a real compatible package plus actual platform execution/provenance.
- Output evidence includes tunnel credentials: never stage or publicly archive `output/` wholesale. Publish only an explicit reviewed source/evidence allowlist, excluding credentials, `.DS_Store`, dependency environments, and `.git/config`.
- Record completed iterations and elapsed session/worker/test/build time in `agent/devsummary.md`; commit important progress and checkpoint at least every ten iterations.

Prior session guidance is archived in `agent/archive/AGENTS-20261029-024735+0800.md`; stable project instructions follow.

# Agent notes for Lumen

See `CONTINUE.md` for project state, build layout and next steps.

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
