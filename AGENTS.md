# Current Apple compatibility task — 2026-10-29

Read `agent/task.md` and `agent/continue.md` first. The current task uses branch
`gpt6-astra-pro_browser-engine_apple-compat-20261029`; preexisting source was
preserved in commit `ef11866750b52dfbdd800200965656c409b16e02`.
The requested goal is 98/100 across real Mac/iPad/browser capability, same-IPA
provenance and one-second resource measurements. Test counts alone do not
establish that score. Retain unsupported and unverified gates explicitly.

Only the browser_engine integrator stages/commits. Other agents own disjoint
files. The root agent brokers remote commands because child MCP calls hung.
Do not replace this native source with an older public-reference checkout.

Previous instructions are preserved below and in the dated archive.

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
