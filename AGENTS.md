# Lume Apple WPT task 102900614

Read `agent/task.md`, `agent/continue.md`, and `agent/scorecard-102900614.json` first.
Canonical repository `/Users/devin/repos/lumen` (requested `~/project/lumen`).
Branch `gpt6-astra-pro_builtin-agents_lume-apple-wpt-102900614`. Session starts 2026-10-28T22:14:41.459Z, deadline 2026-10-29T01:14:41.459Z (09:14:41 Asia/Shanghai).
Goal 98/100, at most 1000 iterations, bounded by three hours. A build or package is not completion.

## Working rules

- Root coordinates and brokers Mac I/O. All three built-in workers inherit the main model and reasoning level. Child Mac MCP calls hang; send executable broker requests to root rather than calling repeatedly.
- `wpt_engine` owns standards fixes and regressions; `apple_runtime` owns Apple execution and performance; `release_integrator` owns exact staging, commits, evidence, docs and public progress. Coordinate shared files. Only the integrator commits; preserve unrelated/untracked prior work.
- Serialize heavy builds, browser runs and controlled performance measurements. Sample process CPU/RSS every second throughout every build and test. Log start/end, elapsed, gaps, peaks, binary hashes and cleanup. Profile abnormal CPU or stages over five minutes before broadening runs.
- Idle pages should stay near 0–3% CPU; typing in the ChatGPT composer must stay under 10%. Validate interaction on the actual accepted executable. Reuse matching prebuilt V8, preserving platform/architecture compatibility.
- Keep upstream WPT assertions and the frozen 1515-variant denominator intact. Custom regression counts are separate. Full WPT pass rate is unknown until a valid full run establishes it; six passing files do not establish conformance.
- The prior Chrome control is quarantined after native URL/anchor sanity failures. Restore a verified official reference without bypassing signature or trust checks; keep raw anomalous evidence.
- Same-IPA gates require the identical signed device package actually running on each claimed Apple platform. Native Mac builds, simulator apps and SDK link probes do not establish this.
- Never stage or publish `output/` wholesale: it contains profiles and private tunnel/message state. Source releases use reviewed tracked-source allowlists and secret scans; do not publish access fragments, credentials, `.git/config`, unrelated files or dependency environments.
- Record each global iteration and build/test timings in `agent/devsummary.md`. Do not sum parallel wall times or invent model compute durations. Push and refresh reviewed source/progress after important accepted work and at least every ten iterations.

## Current evidence and next work

Historical baseline50/100 from the previous accepted executable, not freshly earned this task. Original pinned subset1850/1850 assertions,6/6 files. Wider priority cohort158/207 recorded variants fully pass (76.33%);1515 variants frozen, so158/1515 (10.43%) are recorded fully passing. Full WPT pass rate unknown.

Priorities: meaningful high-impact red/green standards fixes; high scrolling CPU; actual same signed IPA execution; trustworthy Chrome control. Exact previous handoff and task are archived with suffix `before-102900614-20261029-062143`. See refreshed continuation for artifact identities and reproduction paths.
