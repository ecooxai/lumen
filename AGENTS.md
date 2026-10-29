# Lume Apple browser: current task 20261029-cpu30

Read `agent/task.md`, `agent/continue.md`, this file, and `CONTINUE.md` before work.
Canonical repository is `/Users/devin/repos/lumen` (requested `~/project/lumen`).
Branch `gpt6-astra-pro_builtin-agents_lume-apple-wpt-20261029-cpu30`. Base source `1fcde424dcaa7ac842623478196d50abfa436760`; inherited dirty cache source is preserved in `f8d3e7380e3e3194c3f34b5cd066113c45dfa1a9`.
Start 2026-10-29T02:18:45+00:00; normal deadline 2026-10-29T05:18:45+00:00; only important bug fixes may extend to 2026-10-29T06:18:45+00:00.
The goal is98/100, maximum1000iterations, bounded by the authorized time. Successful tests, packages or commits are checkpoints.

## Ownership and execution

- Root coordinates and reviews. Three built-in subagents inherit the same model and reasoning: `integration`, `compatibility`, and `performance_release`.
- Child devinmac3d MCP calls have a confirmed hanging defect. Root must transport agent-authored remote commands and scripts; agents perform analysis, implementation and validation. Do not repeat opaque child calls. Prefer canonical workspace over tilde aliases.
- Only `integration` stages, commits or changes branches. Preserve unrelated untracked files and all prior candidate evidence. Do not publish `output/` or `out/` wholesale.
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

## Current acceptance 20261029-cpu30-001

Current native `f52a08ca9a1bc6cd79eb082d8bedd7b5851e23341ee7427ff01cddfb038a8a9c` is accepted for the explicitly measured Mac gates at **50/100**. Frozen native source is `df4ae3bc8d52a06778e0d8a1f706138201dd043d`, manifest `agent/evidence/20261029-cpu30-001/compiled-source-sha256.json`; documentation/release HEAD is a separate identity. Original WPT **6/6 files,1850/1850 assertions** passes. Fresh selected Encoding WPT **96/103 variants (93.204%)** fully passes, with23623PASS/35FAIL assertions and **1412/1515 variants unrun**. Preserve every failure and the frozen1515 denominator. Historical226/237 is a different run selection and must not be presented as the fresh103 result. Full official WPT is unknown.

Fresh controlled native GUI scrolling p95 **11.085%**, peak **11.137%**,34 samples/33.010s meets the user's30%limit. Final idle p95~1%,20-navigation settled RSS157.344→146.562MiB with stable native counters. The private1000×700 window screenshot was visually reviewed by root; scope is this fixture, not all rendering or input behavior. Typing and IPA execution are not proved by these receipts.

Combined source reproduces inherited f52 with203 custom render/GPU/wheel checks; do not claim historical826 checks as rerun. Build2.103s, combined checks5.550s, original+selected WPT73.420s, GUI+soak282.666s. Existing prebuiltV8 reused. Meaningful publication3/5 RED→5/5 GREEN guards both current and historical `out/` material from public Git bundles. All source and raw evidence hashes are in `agent/evidence/20261029-cpu30-001-native-acceptance.json`.

Continue standards fixes (JSON document decoding, double BOM and independently qualified fatal-stream behavior), exact103 Chrome control, then same-candidate regressions/GUI/soak before promoting any changed executable. `src/html/html.c` may contain unaccepted active work and is deliberately excluded from this checkpoint. Public new-task source/telemetry/message delivery remains pending and earns0 until freshly verified. Apple device IPA and iPad app execution remain blocked/unverified; simulator OS availability alone is not app proof. No new iPhone execution gate applies.

Three same-model built-in agents remain active; root brokers Mac I/O. Normal deadline05:18:45UTC, critical-fix-only maximum06:18:45UTC. Only integration owns Git. Keep progress source/archive current after commits and poll the private inbox between jobs. Native output/private state stays ignored under `out/` and `output/`; never publish it wholesale.
