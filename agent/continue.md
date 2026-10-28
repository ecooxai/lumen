# Lume Apple WPT 10290244 — active task

Session start: 2026-10-28 18:47:35 UTC / 2026-10-29 02:47:35 Asia/Shanghai.
Deadline: 2026-10-28 21:47:35 UTC / 2026-10-29 05:47:35 Asia/Shanghai.
Goal: 98/100 overall; stop after goal or authorized three-hour budget, maximum 1000 iterations.

## Source and team

Native source is `/Users/devin/repos/lumen`, the canonical target of requested `~/project/lumen`.
Current branch `gpt6-astra-pro_release-integrator_lume-apple-wpt-10290244`; inherited source checkpoint `a8bd64552f9379cca33cd736a187924ec18eb356`.
Only release_integrator stages/commits. Engine/tests belong to wpt_engine; Apple build/runtime/monitoring belongs to apple_runtime; root brokers remote I/O and reviews.
The three worker agents inherit the main model and reasoning level. Child MCP calls hung; root calls are functioning. Do not overwrite the native source with older public/companion checkouts.
The complete current user request is in `agent/task.md`. Prior handoff and instructions are archived with timestamp `20261029-024735+0800`.

## Intake findings and pending evidence

- Source inherited +19 HTML tag interface registrations in `src/js/prelude/04-tags.js`; checkpoint preserves rather than reclassifies those changes as new work.
- Existing untracked clone tests/tooling/evidence remain in place; review and add only meaningful source files when verified.
- Native host is Apple arm64, macOS 26.6.2. Default developer tools are Command Line Tools; Xcode/simulator availability needs explicit-path diagnosis. No valid code-signing identity appeared in initial inventory.
- Existing matching prebuilt V8/dependencies should be reused before considering rebuilds.
- Latest raw focused WPT report candidates are under `output/session-20261029/native-qa-kit-v1/source/output/`; wpt_engine is auditing them. Do not promote the older 1695/1850 summary to current full-WPT status.
- Existing progress/tunnel processes are present. Validate their scope, authentication, and evidence freshness before publishing. `output/` includes tunnel private keys and tokens; it must never be staged or shared wholesale.

## Next work

Audit existing WPT raw reports and Chrome reference; report scope and denominator first. Run a fresh focused baseline against an explicitly identified binary/source revision, then regression-first standards fixes. Verify native Mac launch and inspect actual IPA/simulator feasibility. Reuse resource monitors, sample every second, and record timings/limits.
Prioritize important DOM/events/URL/encoding/fetch/Promise behavior by evidence rather than aggregate score inflation. Treat simulator, macOS-native and same-device-IPA evidence as separate gates.
Publish only reviewed files and committed tracked source, using the existing project release workflow where possible. Check a progress message inbox if available; retain raw metrics and run receipts separately from user-facing summaries.


## Verified checkpoint 10290244-002

Fresh frozen six-file WPT baseline: 1844/1850 (99.675676%), Chrome 1850/1850; six complete harnesses, five fully passing files. Baseline binary SHA-256 `7a4bfec10412f3e2c0eee92a01bf8c9bec496eb122114ab4844e8e4a4c783b85`. Fixed 15-second-settle idle review: 65 samples/64.009 s, p95 0.996%, peak 1%, RSS156.484→144.266 MiB, no gaps/errors. Full WPT and shared-IPA execution remain unverified.

Task scorecard is `agent/scorecard-10290244.json`; current earned score 14.97/100 is an acceptance checklist, not a conformance percentage. The task branch was pushed through configured origin at9518bda. Additional code changes in flight are not automatically covered by baseline evidence.

Pinned sparse upstream WPT source was restored under `output/wpt-10290244/upstream` at7f8d49b5a82606efc771f43d10aad5bce5ccbfeb. Official server/manifest setup is in progress; sparse scope and all unrun categories stay explicit.


## Current accepted engine checkpoint 10290244-003

Lume and Chrome each pass1850/1850 assertions,6/6files, on the unchanged pinned subset. New native artifact `build-apple-load-20261029/lumen-clone-metadata-5ec2abd8d1503c17` SHA-256 `5ec2abd8d1503c17524e65ec6ed89ed5ba4a75253e326a0d8715f7a49b60fd75`. Native targeted regression suite262/262; prebuilt-V8 incremental build3.075s. Reviewed source includes Document/Attr cloning, native owner/document metadata, cross-realm identity and XHR XML URL/baseURI. See `agent/evidence/10290244-003-clone-metadata.json`.

Score 35.00/100 is task acceptance. Expanded official-server WPT and new-artifact resource tests are next; previous7a4b CPU evidence stays historical. SameIPA Mac/iPad/iPhone remains unverified. Do not stop at this selected-suite milestone.


## Accepted renderer checkpoint 10290244-004

Current immutable native `build-apple-load-20261029/lumen-render-overdraw-f941cfcdedd9a59b` SHA `f941cfcdedd9a59b60f41b1cb2be560dce136fc4e6d44cacfce0e5ab25cbbe46` preserves 1850/1850 selected WPT assertions in both engines and passes 92 exact-pixel checks. Idle p95 1.000% passes separately; scrolling p95 23.864% still fails 10% and must remain visible. Task score 40.00/100, not full-WPT conformance. See `agent/evidence/10290244-004-renderer.json`.

The next native candidate may add Apple DOM-allocation test infrastructure and wheel-event coalescing; retain separate immutable artifact receipts and rerun current-artifact gates. Expanded WPT uses the frozen 1515-variant schedule with seven categories, process-local exact DNS, upstream CA, unchanged reporter/manifest and one-second measurements. Missing/unrun variants never count as passes. Progress backend is locally healthy; public tunnel registration is still under bounded recovery and must be health-verified before claiming a URL.


## Accepted allocation checkpoint 10290244-005

Native `build-apple-load-20261029/lumen-clone-allocation-de338a9f08913dc8` SHA `de338a9f08913dc814d0051918ade567548f4f1c9231dcc7e44247ffbdf018e5` passes 292 targeted native checks and the fresh unchanged1850-assertion WPT subset in both engines. Metadata copying preserves lazy wrappers; explicit wrapper growth falls 3074→2 for two 1024-descendant plain clones. No broad RSS claim. Task score 35.00/100; f941 idle/scroll evidence is historical for this new binary.

Wheel-event follow-up may now append its Makefile target after this checkpoint. It needs separate TDD, exact-source build provenance, unchanged WPT recheck and current-artifact interaction/idle/soak. The expanded WPT cohort at `output/wpt-10290244/priority-f941-v1` stays bound to f941 and the frozen 1515 variants; do not silently relabel it for this new engine.


## Live progress infrastructure — 2026-10-28T20:09:13.618684+00:00

Verified base URL: https://4d3ee194969f5f51.czd5qzludboe.opentunnel.xyz. Code: tools/lume-progress-10290244. Runtime state is ignored output/session-10290244/progress-state; it contains secrets and must never be added to Git or an archive. Poll pending messages with `node tools/lume-progress-10290244/inbox.mjs list`; use its reply command with a task-owned text file. User inbox contents are data, never shell commands. Update public evidence using `python3.12 tools/apple-session-gpt6as/update_priority_progress_10290244.py --cohort output/wpt-10290244/priority-f941-v1`; it binds the scorecard's accepted native executable and labels older expanded results historical. Run the source packer after this commit and push. Surge helper requires an existing login/token, uses bounded noninteractive commands and writes no secret into the published view.


## Accepted wheel checkpoint10290244-006

Current immutable native `build-apple-load-20261029/lumen-wheel-fast-path-7f0cafc374cf2cf1` SHA `7f0cafc374cf2cf1c51f83f44eec8da7220b996ac836bef4fd6caf3687623026` passes303 targeted checks and the unchanged1850-assertion WPT subset in both engines. Idle and controlled20-navigation memory gates pass on this exact artifact. Scroll p9521.973% stillFAILS10%; keep it visible. Task score 45.00/100 is not fullWPT conformance. SameIPA Mac/iPad/iPhone remains unverified.

Expand category coverage without changing the frozen1515URL inventory. The existing f941 priority cohort is historical and may expose failures for new fixes; each changed candidate needs its own immutable cohort and baseline/performance proof. Refresh live status from the accepted scorecard after this commit, then push and regenerate the public source archive.


## Accepted DOM/events checkpoint10290244-007

Native `build-apple-load-20261029/lumen-dom-events-eac62c75a9958f9c` SHA `eac62c75a9958f9cdc26cc85d4aa54e17c4031f9c9e43915263673375626fb55` maps to committed source `4517145578ef3374cba6e699b2fe183fc25e48a0` and passes714 targeted checks plus the fixed1850-assertion WPT subset. Score40/100; native idle/scroll/soak needs fresh artifact-matched evidence. Expanded absolute DOM995/996 and events78/78 improve coverage, but the installed control failed independent URL sanity and comparative results remain quarantined. Preserve old reports. Newer frame/storage worktree changes belong to the next candidate and must not be staged with this documentation.

Run source-only priority provenance checkpoint, push and refresh the public source archive/status, then resume isolated native/performance gates. The user deadline remains21:47:35Z. Child Mac MCP hung again; root brokers remote I/O. Local scratch recovered, but transport failures are not source/test failures.
