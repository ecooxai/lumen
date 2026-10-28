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

### Current checkpoint003

Verified transfer native `733943aa9496d1342dd9d90a76146d0433b24c9ef7848df5860881e57c2ac848`; original WPT1850/1850 and targeted222/222 pass. Current artifact acceptance40/100, prior50 historical. Fresh soak and task public delivery pending; scroll p9523.054% still fails10% ceiling. Keep GPU/stream candidates isolated. Details and exact sources in `agent/evidence/102900614-003-message-transfer.json`.

### Active progress delivery005

Verified public base `https://dd1cacbcfa84c17d.q4d3dgcpeeoq.opentunnel.xyz`; current acceptance45/100. Private message keys remain out of Git, source archive and tool output. The relay must receive explicit task XDG_DATA_HOME, XDG_CONFIG_HOME, XDG_CACHE_HOME and XDG_STATE_HOME; do not restart shared/default profiles. Preserve source publication hashes and refresh the archive after each accepted checkpoint.

### Current reference qualification006

The fresh official signed Chrome `0a8cb6eba2bee365c01fb0612f64f1d15855fd499dd54625ced1fe50215e5fb4` is qualified for new paired measurements by provenance, native reproducibility and exact implementation-source review. Its real URL standards failures remainFAIL. Historical quarantined reports stay immutable. Use `agent/evidence/102900614-006-chrome-reference.json`; never equate reference qualification with WPT conformance.
