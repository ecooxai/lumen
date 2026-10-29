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

## Current continuation 20261029-cpu30-004

**Latest accepted native: 3ad884713a4aa896dec1d26e20c5a85af5543499467edf9b7ecf424196c63bce**. Exact compiled source commit `94feccb482c508a2461bb6b8a1a52c011829c883`, original build-base `7d2f27c1bacfeec19915a2c8b0b417cfa84c0963`, manifest `agent/evidence/20261029-cpu30-004/compiled-source-sha256.json`. All142 compiled Git blobs match the recorded build inputs. Documentation/release HEAD is separate. Current task score **55/100**, not a WPT percentage; full official WPT remains unmeasured.

**Broad priority WPT:245/279 (87.814%)**,38727PASS/34FAIL assertions,249OK/15TIMEOUT/15ERROR; **1236/1515 frozen variants unrun**. Fixed same103 Encoding selection improved **96/103→101/103 (98.058%)**, with23632PASS/26FAIL; remaining26 are missing workerXHR behavior. Original six unchanged source files still pass **1850/1850 assertions** separately. All failures/timeouts/errors remain preserved. Qualified Chrome same279 control has234fullypassing,38382PASS/895FAIL,all279OK; paired final retention is37839/38382 Chrome-passing assertions and201/234 Chrome-fully-passing variants. This is scoped comparison, not an overall browser ranking.

Final controlled scrolling p95 **15.823%**, peak **16.018%**,35 samples/33.996s, meets the30% ceiling. Final idle p950.996%, RSS153.453MiB. Twenty-navigation settled RSS growth **-10.609375MiB**, stable native counters/GPU. Root reviewed the fresh native capture; raw1Hz samples are committed. Synthetic wheel and controlled pages do not prove OS input, typing or arbitrary-site performance. Host dashboard RAM is decimal total-minus-free including caches and is separate from Lume RSS.

718 current-policy custom checks pass, including203 render/GPU/wheel checks. Preserve the old15-case frame fixture:10pass/5intentional policy differences, all exact formerUTF8/newWindows1252 messages. Locale HTML/plaintext network fallback now matches explicit en-US policy; eligible parents and header/meta/BOM precedence remain, file: staysUTF8, JSON/XML preserveUnicode defaults. A fresh6-case windows1251-parent isolation control removes the ambiguity between parent/default1252. FatalUTF8 invalid-leading-byte recovery is a bounded Chrome/WPT interop decision; unchanged invalid-continuation logic retains the documented mismatch. Earlier9137 diagnostic26/28 is not relabeled as a final3ad diagnostic.

Prebuilt HomebrewV8 15.6.75.16arm64 is reused and all three dylib hashes remain unchanged. Apple SDK/compiler feasibility improved: explicit SDK paths plus matching installedCLT successfully compile/link tiny device/simulator UIKit probes. Actual Lume iPad execution and shared signed deviceIPA onMac remain unverified because compatible mobileV8/dependencies, signing/profile/export artifacts are absent. No new iPhone-device execution gate is imposed.

Published live route and Surge use the preserved private message namespace102900614. Existing public source-byte, desktop/mobile,2sWSS and private message tests passed; publish the final documentation HEAD/archive and rerun version2 full-byte/view proof before reporting the final link. URLs: https://dd1cacbcfa84c17d.q4d3dgcpeeoq.opentunnel.xyz and https://lume-102900614-gpt6-astra-pro.surge.sh . Source https://github.com/ecooxai/lumen on branch gpt6-astra-pro_builtin-agents_lume-apple-wpt-20261029-cpu30. Private inbox/key/profiles stay ignored. Final public compositor removes stale functional-candidate labels and binds current WPT/CPU to this artifact.

Session began02:18:45UTC /10:18:45Asia/Shanghai; normal deadline05:18:45UTC, optional critical-fix-only maximum06:18:45UTC. No final goal98/100 claim is supported. Next work: real service/shared-worker context support, workerXHR transport, declarativeShadowDOM, broader unrun Encoding coverage, qualified mobile dependency/signing pipeline. Source is frozen for this window; preserve every immutable artifact and failure receipt. Read agent/task.md for the complete unchanged user request; integration alone manages Git. Detailed proof: `agent/evidence/20261029-cpu30-004-native-acceptance.json`.
