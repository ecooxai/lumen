# Lume Apple browser: current task 20261029-cpu30

Read `agent/task.md`, `agent/continue.md`, this file, and `CONTINUE.md` before work.
Canonical repository is `/Users/devin/repos/lumen` (requested `~/project/lumen`).
Branch `gpt6-astra-pro_builtin-agents_lume-apple-wpt-20261029-cpu30`. Base source `1fcde424dcaa7ac842623478196d50abfa436760`; inherited dirty cache source is preserved in `f8d3e7380e3e3194c3f34b5cd066113c45dfa1a9`.
Start 2026-10-29T02:18:45+00:00; normal deadline 2026-10-29T05:18:45+00:00; only important bug fixes may extend to 2026-10-29T06:18:45+00:00.
The goal is98/100, maximum1000iterations, bounded by the authorized time. Successful tests, packages or commits are checkpoints.

## Ownership and execution

- Root coordinates and reviews. Three built-in subagents inherit the same model and reasoning: `integration`, `compatibility`, and `performance_release`.
- Child devinmac3d MCP calls have a confirmed hanging defect. Root must transport agent-authored remote commands and scripts; agents perform analysis, implementation and validation. Do not repeat opaque child calls. Prefer canonical workspace over tilde aliases.
- Only `integration` stages, commits or changes branches. Preserve unrelated untracked files and all prior candidate evidence. Do not publish `output/` wholesale.
- `compatibility` owns standards/engine regression work. `performance_release` owns src/app/chrome_paint.h, src/app/main.c, tests/test_render_overdraw.c, performance/Apple packaging and progress publication. Coordinate shared native files before touching them.
- Serialize heavy builds, browser test runs and controlled performance measurements. Use existing build caches and architecture/platform-compatible prebuilt V8.
- Monitor process CPU/RSS every second for every build/test. Record gaps, workload, PID identity, binary SHA-256, wall time, peaks, p95 and task-owned cleanup. Investigate stages exceeding five minutes. Do not kill unrelated processes or delete active caches.
- User now accepts ordinary scrolling below30%CPU. Idle still targets0–3%; ChatGPT typing remains below10%. Report measured CPU values and memory trends; the new scrolling ceiling does not authorize unchecked performance claims for a changed executable.

## Evidence and publication

- Keep all1515 frozen priority variants visible. WPT case pass rate and subtest pass rate are different. Focused/native custom regressions are not full official WPT results. Full-suite compatibility remains unknown.
- Compare identical upstream cases and revisions, explicit browser binaries and contexts. Preserve errors, timeouts, unsupported and unrun rows. Do not discard real Chrome failures or replace them with assumed100%.
- The qualified official signed Chrome reference is documented in `agent/evidence/102900614-006-chrome-reference.json`. Earlier quarantined reference results remain historical; use the qualified artifact and record its own native failures.
- Same IPA requires the identical signed iOS device package actually executing on each claimed platform. A native Mac build, simulator app, SDK probe or package alone is insufficient. Do not bypass signing/trust checks.
- Use reviewed tracked-source release allowlists. Exclude credentials, private message/tunnel capability fragments, profiles, `.git/config`, unrelated files and dependencies. Keep Git history in a verified bundle/archive when distributing source.
- Commit meaningful progress and save/publish at least every ten completed iterations. Keep exact raw and reviewed evidence distinct; refresh public provenance after each accepted checkpoint.
- `agent/devsummary.md` records each iteration, UTC/user Asia/Shanghai time, score basis and test/build duration. Parallel wall times must not be summed as elapsed session time; model compute duration is unavailable and must not be invented.

## Continuation

Read the current continuation for historical accepted artifact733, later candidates and known gaps. Initial score remains inherited45/100 pending validation of this task's source; it is not a WPT percentage. The inherited cache changes were preserved solely to avoid losing prior work, not accepted. Root's reliable I/O broker recovered access after child calls consumed about an hour; continue with bounded broker execution.
