# Lume task 10290244 — current evidence checkpoint

Read `agent/task.md`, `agent/continue.md`, and `agent/scorecard-10290244.json` first.
Canonical project: `/Users/devin/repos/lumen` (requested `~/project/lumen`).
Task branch: `gpt6-astra-pro_release-integrator_lume-apple-wpt-10290244`. Source checkpoint before this handoff: `952ed465f803118bc6ee8b4c31c82bea7ee1c0b3`.
Recorded: 2026-10-28T21:32:30.811977+00:00. Session start 2026-10-28T18:47:35Z; authorized deadline 2026-10-28T21:47:35Z.

## Ownership and operating rules

- The root coordinator brokers Mac I/O; child devinmac3d calls repeatedly hang. Three built-in workers inherit the same model and reasoning level. Do not replace this native repository with an older companion checkout.
- One integrator stages and commits exact reviewed allowlists. Engine/runtime owners coordinate shared Makefile, native bindings, workers and preludes. Serialize heavy builds, browser runs and controlled CPU measurements.
- Use existing one-second resource monitors for every build/test. Preserve stage timings, collection gaps, peaks, process cleanup and executable hashes. Investigate abnormal stages above five minutes. Reuse matching prebuilt V8.
- `agent/evidence/`, the fixed scorecard, raw output receipts and compiled-source hashes distinguish accepted, historical and unverified evidence. Custom regression counts are separate from upstream WPT assertions. A six-file pass is not full WPT conformance.
- Keep all 1515 frozen priority variants visible, including unsupported workers and unrun cases. Preserve original upstream assertions and reference anomalies. A changed executable needs fresh performance acceptance.
- Same-device IPA acceptance requires the identical signed iOS device package actually running on each requested platform. Native Mac builds, simulator binaries, SDK link probes and package creation alone do not establish this.
- Never stage or publish `output/` wholesale: it contains private tunnel/message state and browser profiles. Use the reviewed committed-source packer; omit credentials, private access fragments, `.git/config`, dependency environments and unrelated files.
- Record iteration start/end, elapsed wall time and timed build/test evidence in `agent/devsummary.md`. Separate worker/model compute duration is unavailable; do not invent or sum parallel wall times as elapsed time.

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

## Latest accepted evidence and next work

Iteration `10290244-010`; score 50/100 against the fixed 98/100 whole-task goal. This is an acceptance checklist, not a WPT percentage.
Native `build-apple-load-20261029/lumen-headers-codec-1b9d43ed19cd616b`, SHA-256 `1b9d43ed19cd616b0029e34fde97050b86c9acbdbda16a0dca5892922df704e9`.
Original pinned six-file subset: 1850/1850 assertions, 6/6 files fully passing.
The installed Chrome control remains quarantined after independent native URL/anchor sanity failures. Preserve its raw failures and do not award comparative retention from that reference.

Read the refreshed continuation for current gates and reproduction paths. Continue important standards failures, high scrolling CPU and real Apple package execution; do not treat a build or this handoff as completion. The complete previous handoff/instructions are archived under `agent/archive/` with timestamp `20261029-053230`.
