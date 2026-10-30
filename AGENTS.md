# Agent notes for Lumen

Start with the dated Linux handoff at the end of this file and `devsummary.md`. `agents/continue.md` and `agents/archive/` retain historical context; their aggregate WPT standings are not current-branch validation.

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
python3 agent/progress/monitor.py --root-pid EXACT_TASK_PID \
  --output agent/validation/UNIQUE_RUN/resources.csv --label "owned test process tree"
```

- Idle pages (nothing animating, no input) should sit near 0-3% CPU.
- Ordinary interaction (typing, scrolling, hover, clicking) should stay
  low; typing in the ChatGPT composer must stay under 10%.
- Sustained high CPU is only acceptable while doing real heavy work (page
  load, video decode, a long streamed reply). Anything else is a bug:
  profile it (`sample <pid> 5 -file out.txt` on macOS, `perf` on Linux),
  find the hot path and fix it before moving on.
- Report the measured CPU numbers with the test results.

## Independent resource monitoring

Every host test must retain one-second process-tree CPU/RSS samples; use
`agent/progress/monitor.py` with the exact task PID, and retain its CSV/summary.
Use `agent/progress/cgroup_monitor.py` for authoritative shared `cpu.stat`
deltas during concurrent work. Process samples can miss short-lived children;
new-PID tick estimates are not proof of quota violations. PSS is sampled less
frequently and RSS can double-count shared pages. Report these limits.

Keep CPU min/median/max, peak/final RSS, timing and distinct stderr/crash findings
with each substantive gate. Investigate a concrete crash, unexplained sustained
CPU, growing settled RSS or a guard threshold before broadening the run. Do not
change global scheduling, swap, driver or unrelated workloads. The experimental
CPUWeight tuning was reverted; no causal speed gain was established.

Use same-model built-in agents for independent review/test lanes when available,
as requested for this project session. A separate scripted monitor can satisfy
the one-second requirement without consuming another model slot. Keep source
and Git metadata frozen while a build records its provenance.

## Device test pages

`python3 tools/device_tests.py PAGE|GROUP ...` opens Lumen's test pages on the
phone (debug build), samples Lumen's CPU every second and prints the pages'
numeric results. Pages: cam, screen, mic, webgl, webgpu, aquarium, chatgpt
(newest chat bookmark), google, youtube, utils (WebGPU Weather World is under
its apps menu). For an explicitly authorized Android release, run the relevant approved
groups before release; `all` includes physical-media pages and requires separate
permission/isolation. It is not a gate for this Linux task. For a small authorized
device fix, run only relevant pages (e.g. `google`, or `gfx` for WebGL/WebGPU work). Each page gets its own cold start; `--tabs` opens them together, one
tab each (background tabs are throttled, so measure there only the front tab).
The script never taps or types; screen recording needs its system prompt
accepted. A settled site whose last-10-s CPU exceeds 5% is reported.
Sites are tested in desktop mode (Desktop site is on for all sites by
default): the desktop versions must work.

## Scripted UI tests

Prefer scripts over AI agents for UI tests; use an agent only when a script can't
do it. `python3 tools/edit_bar_test.py --serial SERIAL [--install] [--only typing,bar,more,clip,hold,showkbd]`
drives the Android edit bar on a local Draft.js page (tests/device/editbar), with
no person or agent. It locates things through the page's posts and Lumen's
`LumenBar` logcat lines, and samples CPU/RSS every second (that sampling counts as
the run's monitor). When a step fails, find out whether the script or Lumen is
wrong and fix that one.

## Collaboration and scope

Use built-in agents of the same model/capability when available. The current
session used GPT-6 Astra for implementation, independent conformance, operations
review and project management. Agree source-file ownership, index ownership and
whole-source/build freezes before concurrent changes; preserve and verify exact
patch restoration for isolated candidates. Do not edit source, generated build
inputs or Git metadata during a frozen build. Keep tests meaningful and bounded.

Preserve applicable privacy rules: do not enter private third-party page inputs,
inspect captured physical media, expose profiles/credentials/inbox data, or keep
screenshots containing personal content. Native Linux physical camera/microphone
acquisition is refused until real origin-consent UI exists. Do not treat a test
flag as capture isolation. Android/device media tests require their own explicit
permission and isolation; do not automatically run capture pages as a Linux gate.

## Live build on display :0

After finishing each main task, restart this repo's Lumen on the user's display so
they see the latest work (leave other Lumen builds, e.g. ../lumen/output/..., alone):

Before any restart, read `/tmp/lumen-display0.pid` and verify the live
`/proc/PID/exe` resolves to this repository's expected Lumen executable. A stale
PID file must never authorize terminating an unrelated process. Send a graceful
termination only to the verified task-owned process, then launch the final
accepted default binary on `DISPLAY=:0`, preserving the existing user profile.
Do not inspect or record the user's existing display; use a fresh isolated Xvfb
profile for showcase recording. Record the new exact PID for monitoring.

## Linux engine handoff — session 7, 2026-10-30

The host, CPU, monitoring, collaboration, privacy and display policies above remain unchanged. The previous session 6 handoff is preserved in Git at `ab44cc7f825279391a0f193e0b8a10b5ab151eb4:AGENTS.md`; its older benchmark, media and publication results are historical rather than candidate-214 evidence.

Evidence root: `agent/validation/linux-wpt-20261030-session7`. The complete user request is retained in `agent/task.md`. The active request defers URL conformance; three dedicated native URL tests remain outside the selected native gate. Do not modify the frozen WPT expectations, test sources, source pins, protected validation interfaces or result denominators to obtain passes.

### Tested source and branch

- Working branch: `gpt-6-astra-pro-builtin-integration-20261030-wpt-nonurl`.
- Tested engine candidate: **214**; source commit **ab44cc7f825279391a0f193e0b8a10b5ab151eb4**.
- Immutable executable: `/mnt/p3/project/browser/lumen-android/lumen-android/build/codex-native/lumen-astra-i214`.
- Binary SHA256: **7ea0a9999ec521e90fdd2c25fa8485675abf4149aa7aaa7e1696b99bf8aae8ca**.
- Recorded source-set manifest SHA256: **0bda0d47294b30cd710829a04d3d17b9193165440009e7b2fb8aea2d263cd956**, covering **642 recorded inputs**. This is not a compiler-generated transitive dependency closure.
- Build/startup completed with actual exit zero, unchanged inputs, **2026-10-30 10:14:16–10:15:08 UTC**. Native gate **140/140**, no failures/skips, actual exit zero and owned cleanup complete at **10:17:20 UTC**. The registered `url`, `url_host_nul` and `data_url` entries are deferred outside that denominator.

Later documentation or progress-UI commits must map explicitly to the tested source above. Do not relabel a later working tree or a separately rebuilt export as this measured binary. Linux validation does not establish Android release readiness, hardware-GPU rendering or a portable binary distribution.

### Fixed non-URL comparison

WPT revision: **0e1c0bce3defd59a76cf2f351c7da5c5a42b26fb**. Fixed 640-file manifest SHA256: **8b5d0d491ce8b14e6ee8107d1213c10b90127a0193207d06057216bd54f3243f**. The final replay uses the sealed disjoint 320+320 union, the existing runner and deadlines, and at most two browser workers. A historical receipt label saying two workers per browser denotes two total one-browser workers; do not silently rewrite the sealed receipt.

| Browser | Fully passing files | Passing reported assertions |
| --- | ---: | ---: |
| Starting Lumen 200 | 380/640 (59.375%) | 8,324/10,540 |
| Final Lumen 214 | 401/640 (62.65625%) | 8,764/10,540 (83.15%) |
| Chromium 156.0.8078.80 | 560/640 (87.50%) | 9,551/10,552 |

Final exact-identity delta against 200: **440 common-identity non-PASS→PASS gains, zero prior-pass losses, zero missing or added identities, and 21 whole-file gains with zero whole-file losses**. Final native/harness outcomes and cleanup: **624 harness OK, 15 TIMEOUT, one ERROR; assertion outcomes 8,764 PASS, 1,738 FAIL, 15 TIMEOUT and 23 NOTRUN. Actual interval 2026-10-30 10:26:49.084281–10:40:02.272173 UTC; controller exit zero, all 640 native browser exits zero, owned processes/profiles/monitors absent and source/binary/runner pins unchanged**. Final comparison evidence: **agent/validation/linux-wpt-20261030-session7/qa-frozen640/final214-public-aggregate.json, SHA256 33864b6a5d2538f631e23483bba4584cd3492f3f4bd6b810aca1b169cb1f24a5. Focused timing/retention aggregate: agent/validation/linux-wpt-20261030-session7/qa-script-scheduling/final214-timing-aggregate.json, SHA256 196012e2d735d0c7212c53b57b64a23ef2e6659cb44abea7cc5391b9a95cb275**. Preserve failed assertions, timeouts, errors, registration differences, duplicate-name indices and different native/reference denominators. A native process exit zero is not equivalent to a fully passing WPT file.

This is a fixed sample, not the entire WPT suite. **The whole-browser 98/100 goal remains unmet and is not certified.** Do not combine overlapping focused cohorts into a total WPT pass count or convert a completed feature into an overall readiness score. Historical Chromium 150 numbers remain separate from the installed Chromium 156 control. Native/control totals differ by 12 absent encoding-stream registrations; two pairs of text-decoding name variants are also retained in the identity comparison. No native identity is missing against either the 200 or 211 baseline.

### Latest focused evidence

These frozen whole-file cohorts overlap the broad sample and each other. Candidate labels identify the last actual measurement.

| Scope | Latest Lumen result | Starting result | Current Chromium 156 control |
| --- | --- | --- | --- |
| Parser options / related Trusted Types | 214: 1,961/1,961; 18/18 files | 200: 1,008/1,961; 3/18 | Normal 433/1,961; experimental 1,218/1,961 |
| Ordinary classic-script lifecycle | 214: 64/64; 35/35 files | 203: 30/59 reported; 13/35; five later registrations missing | 64/64; 35/35 |
| Document input and document.write | 214: 72/73; 71/72 files | 205: 17/73; 14/72 | 73/73; 72/72 |
| Parser custom elements | 213: 49/50; 12/13 files | 207: 19/50; 3/13 | 50/50; 13/13 |
| Element / Document creation | 213: 64/64; 7/7 files | 208: 24/64; 0/7 | 58/64; 5/7 |
| Template ownership / adoption | 213: 50/50; 12/12 files | 209: 26/50; 4/12 | 46/50; 11/12 |
| Node / Document cloning | 213: 156/157; 8/9 files | 209: 153/157; 6/9 | 157/157; 9/9 |
| CSS custom properties | 214: 219/331; 17/39 files; all 39 harnesses complete | 211: 154/330; 10/39 | 299/331; 35/39 |
| Iframe source changes | 214: 10/10; 6/6 files | 213: 9/9 reported; 5/6; one later registration missing | 10/10; 6/6 |
| DOMParser XML metadata | 214: 20/20 | 211 regressed to 4/20; all 16 old passes restored in 213/214 | 8/20 |

The parser-options scope includes newer draft features; control modes and upstream expectations are retained rather than treating reference nonpasses as permission to lower the bar. The CSS delta is **64 existing identities changing FAIL→PASS plus one restored after-refresh PASS**, with zero prior-pass losses or missing prior identities. Two CSS reference encoding/name variants remain distinct in each direction. The refresh child stylesheet received an additive pin before the iframe-neighbor baseline; do not claim it had been pinned retroactively in the original historical CSS manifest.

Latest 214 ordinary authored retention includes opened streams 22/22, CR boundary groups 3/3 and 5/5, delayed close 21/21, prefix diagnostics 12/12 and navigation 16/16. The broader authored lifecycle comparison remains 111/112 with complete registration. The separate internal-is diagnostic last passed 26/26 on 213. Authored checks are not WPT assertions and must not be added to the upstream totals. Independently isolated native ownership probes passed ASan/UBSan/leak checks with final live-node counts returning to zero; these do not establish a blanket security audit.

### Implemented architecture and review constraints

- `07k-sanitizer.js` / `07l-trusted-types.js` share parser-option compliance across modern and legacy sinks, private snapshots, captured internal operations and native-branded values. Accepted parser options do not bypass XML TrustedHTML checks; prototype shape is not authority. Private CSP snapshot/branding helpers stay in the inaccessible native prelude capability.
- Native script preparation uses a per-node force-async flag, explicit prepared external classification, generation cancellation and bounded asynchronous loading. Ordered dynamic `async=false`, parser-blocking, defer, DCL and load phases are distinct. Keep cancellation cleanup and generation checks before load bookkeeping; late jobs from document.open must not decrement the new generation.
- Persistent script-created input streams and initial-document prefix streams share the native tokenizer. Safe parser yields precede author constructors/reactions; do not call author code inside a C tokenizer frame that can be invalidated by document.open/write. Pending insertion parents/references and parser nodes remain retained across callbacks.
- Custom elements use fresh construction where required, actual native interface identity, a per-Document dynamic-markup counter and staged attributes/reactions. Preserve the current algorithm's already-parent insertion guard. Native `is` state is distinct from the mutable attribute and survives clone/import/serialization with exact byte lengths; the measured Linux Node layout remains **304 bytes**.
- Templates use real inert associated owner Documents; clone/import and parser insertion adopt into the actual target owner, including nested templates and shadow trees. Retired-document teardown releases resources before trees and keeps all participating owner metadata alive until final destruction. Never eagerly destroy the still-live top-level Page DOM during js_free.
- CSS resolution reads an immutable map of winning local custom declarations, tracks active cycle segments, selects fallbacks according to the pinned current algorithm, preserves token boundaries and checks actual math whitespace. CSS-wide dispatch and custom-declaration brace handling are bounded corrections; **full rule/layer/origin rollback is not implemented**.
- Native frame source mutation notification handles equal-value assignments, namespaced-attribute exclusion, srcdoc precedence and pending load accounting. Old pending responses cannot commit over a requested replacement. Keep ordinary attribute setter fast paths and the existing no-Node-growth design.

### Remaining feature limits and next work

The principal unresolved areas are typed/registered custom-property animation, true cascade rollback, computed/shorthand serialization, SVG presentation values and broader layout support. Complete fixed CSS groups now include cycles 11/11, var grammar 18/18, variable-reference 18/18, local declaration substitution 31/31, definition keywords 8/8, guaranteed-invalid 3/3, invalidation 4/4, legal values 23/23 and refresh 2/2. Remaining rollback files retain origin 1/4, layer 0/4 and rule 2/4; the two retained rule longhand passes are not a general provenance implementation.

The 72-file stream cohort retains popup/opener case 047: top-level Pages use separate isolates and restricted remote handles. Correct arbitrary same-origin opener access needs context-group ownership, WindowProxy retargeting/origin checks and navigation/close lifetimes; do not add a test-specific property bridge. The clone cohort retains inert Document.write/declarative-shadow-root behavior. One parser-CE anchor-order expectation differs from the current normative already-parent guard; preserve that guard rather than forcing a reference quirk.

Initial-prefix stylesheet prefetch can still delay JS context creation, and document-written external blocking fetch remains synchronous. **Bootstrap policy metadata is still captured by a preliminary native parse; later head meta-policy arrival therefore remains eager even though the author-visible DOM now advances by prefixes.** This preserved policy timing limitation is not covered by a blanket claim of streaming/security completeness.

Automatic approval review rejected the proposed broad provenance-aware cascade rewrite as a high-blast-radius change under general authorization. It was not applied, split into equivalent edits, or rerouted. Only the separately permitted small keyword/block fixes were applied. Separately blocked custom adversarial probes were not rerun or claimed as accepted. Keep those boundaries explicit; seek appropriate new authorization before undertaking a blocked action. A request to read protected monitor/endmonitor implementation in paired_wpt.py was also rejected; no read, retry or reroute occurred. Existing documented runner invocation remained unchanged, and an independent ordinary-process sampler was used only for separate media encoding. Ordinary production/conformance work continued through supported interfaces.

Historical session 6 form/angle/layout/network and performance results remain in the prior handoff and evidence root; they are not fresh 214 benchmark or hardware-rendering claims. Existing unrelated design cautions remain relevant:

A :valid/:invalid bridge should reuse shared validity rules with correct realm ownership, callback lifetime and invalidation. Avoid executing author-overridden JavaScript getters during native selector/style matching; form, radio peers, ancestors and sibling/:has dependencies need coherent invalidation. Typed attr(), stretch sizing, intrinsic interpolation and vertical layout need broader work than this session's bounded changes.

EventSource is currently undefined. If it is implemented later, SSE reconnection IDs require explicit UTF-8 encoding before crossing the ByteString HTTP header boundary. Do not treat the current Fetch/XHR repair as SSE support.

### Supported reproduction and protected interfaces

Project root: `/mnt/p3/project/browser/lumen-android/lumen-android`; WPT checkout: `/mnt/p3/project/browser/wpt`. Keep exact Chromium 156 launch/ELF receipts, not just a launcher filename. Never repoint a sealed run at a different binary.

Build with the shared quota wrapper:

    python3 tools/codex_compile.py python3 agent/validation/build_candidate.py --iteration NEW_ID --jobs 2 --startup

Never overwrite existing immutable candidate binaries or output directories. Keep one-second owned-process CPU/RAM evidence during every test/build. Supported interfaces are agent/validation/run_guarded_fixture.py, agent/validation/run_chromium_fixture.py and agent/validation/run_guarded_cohort.py (paired_wpt). The cohort wrapper joins cleanup; an older runner's original 638/640 exit-receipt defect remains historical evidence and was not silently discarded.

All current native gates use jobs 1; builds use jobs 2; at most two browser workers share the established quota. The 640 two-shard orchestrator is `agent/validation/linux-wpt-20261030-session7/qa-frozen640/run-two-native-v2.py`; its sealed schedule SHA256 is **7e17aeaf30a0d05982a5d69a11fe0bd062561fe9375b2fcad72fb1eba41d1c32**. Use fresh output paths and the existing protocol; do not broaden deadlines or skip problematic files to improve counts.

Approved capture configuration is documented in agent/validation/README.capture-refusal.md. The original approved library location is agent/validation/linux-wpt-20261028/capture-refusal/capture_guard.so. Use the published pin/configuration through supported launchers. **Do not read, rebuild, rehash, replace or weaken protected guard libraries, private capture helpers or auth records.** Preserve capture refusals, cleared LUMEN_WPT_CTRL/PRE, fresh profiles/audits and source/cleanup seals. No missing guard may be replaced by a stub or bypass.

Optional documented application media settings are `LUMEN_SHOT` pointing to a new caller-owned output image and `LUMEN_SHOT_MS=1`; these do not replace or bypass guard configuration and do not authorize capture of the user's existing display. Historical candidate-200 fixture commands remain in the prior handoff and must not be relabeled as a current run.

The unchanged performance CLI remains under agent/validation/linux-wpt-20261028-session2/performance-final/run_performance_v2.py, with --baseline, --final, --chromium, --mode measure and a new --output directory, through the quota wrapper. Public progress health is `node agent/progress/manage.mjs health`. Do not use its private capability URL command in logs or public artifacts.

External Ahem and ChTestZeroWidthZero test fonts remain required; their documented configuration and missing-font skip behavior are unchanged. Source archives do not include dependency caches or promise hermetic builds.

### Publication, accounting and desktop status

Public exports must use a separately audited source history rooted in existing public ancestry, never the private working branch containing task text, diaries, private validation, profiles or runtime capabilities. Preserve normal non-forced pushes and existing default branches. Every source tar must contain the reviewed public Git bundle, restore instructions and original-to-export mapping; verify an offline clone's commit, tree, blobs and executable modes.

The independently reviewed 214 public source preparation includes all 642 recorded inputs with exact hashes and executable modes. The public base is **0742c66b2c9b077c68006c9cb1b4413e281c328d**; published branch `gpt-6-astra-pro-builtin-integration-source214-20261030`. The archive helper enumerates only the committed public tree plus `PUBLIC-HISTORY.bundle`, `RESTORE-GIT.md` and `ARCHIVE-PROVENANCE.json`; it does not archive raw .git or unreachable objects. The public source was not independently rebuilt and excludes external V8/wgpu/system dependencies and private validation setup.

Final public source commit, archive SHA256/size, offline restore, URLs, media qualification and durable checkpoint: Public source commit **5c0e775cfad2084ab5b8107837fe98275d54b35e**, tree **09082da1bc88eaabd0622b12f9488d7df528a60f**. Final Pages commit **eb741a324ab2606da47fea9370a2dfd177d37cdc** follows initial214 Pages **ef2e1c37ecac6f189d234660daff19c9fa50e490**. Both normal non-forced remote heads were verified. The 9,433,743-byte source archive SHA256 is **b3cd72554339cafd84d4e0568272257f6852a4acc59b653ff79bdd2aaf6c88d8**; its 728 tracked members and three declared additions passed packaging and independent offline-restore checks.

Published results: https://ecooxai.github.io/lumen/ . Source archive: https://ecooxai.github.io/lumen/src.tar.gz . Source branch: https://github.com/ecooxai/lumen/tree/gpt-6-astra-pro-builtin-integration-source214-20261030 . Durable measured progress: https://chatgpt.com/space/page_331eeea00c28819182bc0cd8003fe7b6 (sequence6 after publication).

The 36-second,1024x768,4:3 H264 showcase is a labeled Chromium-rendered public-results walkthrough, not native Lumen UI footage: https://ecooxai.github.io/lumen/lumen-linux-gpt-6-astra-pro-builtin-rubywebterm-chromium-ffmpeg-checkpoint214.mp4 . Video SHA256 **0df401b60376874fafe73b7494dba1c57ad946b37e10f53779ec2c98e52e8c68**,467,513bytes. Actual frames were visually reviewed, encoded with two encoder/one filter threads and one-second resource monitoring, and browser/encoder/profile cleanup passed. Public source/archive/index/media bytes were verified over HTTPS; final media/index check completed at2026-10-30 10:59:13.172495UTC. Source and stable capture HTML remained unchanged during media publication.

Surge is installed but unauthenticated; no account was created. Use the existing authorized GitHub Pages and HTTPS tunnel unless the user supplies a new authenticated deployment target. Actual desktop restart/PID and preview/inbox status: At 2026-10-30 11:07:31.899248 UTC, the required /tmp/lumen-display0.pid ownership record was absent, so the preserved AGENTS desktop preflight could not authorize a restart. The default build/codex-native/lumen binary independently matched the accepted214 SHA256. No user desktop restart or existing-display capture was performed; do not substitute a guessed PID or a name-based process kill. Native validation and the Chromium results capture used isolated profiles through approved interfaces. The live progress service remains available at https://a0bb38e846444140.5x7aqstuceku.opentunnel.xyz ; HTTPS health200 and an empty unread-message queue were observed at the latest active-session check. It reports the exact214 sample/scope results; it must not imply whole-browser98/100 or an always-on agent after the session ends. Never infer a task-owned GUI process from a stale/missing PID file, inspect the user's display, or imply a successful restart without its exact verification.


Final media-inclusive publication: Pages commit `eb741a324ab2606da47fea9370a2dfd177d37cdc`; 15-payload publication manifest SHA-256 `9e9bee54074413b369a3b0cabaf939e749de7f0101f3ba8ba6e8bb766b36f954`. Earlier 12-payload review receipts predate the media addition and are preserved as historical checkpoints.

The session began **2026-10-30 06:26 UTC**; primary requested deadline **11:26 UTC**, with the extra hour reserved only for important bugs if needed. Exact completion time, total elapsed wall minutes and iteration accounting: **closed 2026-10-30 11:26:33.629287 UTC; 300.377155 elapsed wall minutes from 2026-10-30 06:26:11 UTC; 14 recorded candidate iterations (201–214); requested primary five-hour limit reached; the whole-browser 98/100 goal remains unmet; no extra-hour extension used**. The per-iteration ledger is `devsummary.md`; do not sum overlapping agent intervals or erase failed candidates. Preserve 204/206/212 build failures, 210's 138/140 native gate, and 211's 16 XML regressions with their later corrections.

The next agent should start from the tested 214 source and immutable results above. Choose a coherent remaining feature from the sealed failures, freeze its upstream scope and Chromium control before editing, maintain independent peer review and monitored cleanup, and finish with a fresh affected regression/broad comparison. Do not turn an authored or focused 100% score into an overall browser-quality claim.

### Remaining fixed-sample opportunities — final 214 versus current Chromium 156

This is an aggregate comparison of shared assertion identities, not an estimate of all-WPT coverage or a promise that one patch can close each category. There are 1,599 shared identities that pass in Chromium 156 and are non-PASS in Lumen 214 across 198 of the 640 sampled files. Missing registrations are excluded and remain separately qualified.

| Public feature prefix | Shared reference-pass / native-nonpass assertions | Affected / sampled files |
| --- | ---: | ---: |
| CSS values | 549 | 23 / 35 |
| CSS sizing | 405 | 26 / 35 |
| CSS Grid | 230 | 21 / 35 |
| CSS Flexbox | 112 | 18 / 35 |
| CSS custom properties | 97 | 22 / 34 |
| Fetch API | 43 | 15 / 98 |
| DOM Parsing | 40 | 12 / 34 |
| CSSOM | 33 | 10 / 34 |

CSS as a whole accounts for 1,442 of the shared opportunities, affecting 125 of 242 sampled CSS files. A next iteration should freeze a coherent whole-file scope within core CSS values or sizing, inspect current upstream expectations and actual native causes, and retain the completed script/parser/template gates. Keep URL conformance deferred unless the user changes that priority. The previously rejected broad cascade-provenance rewrite is not implicitly authorized by this prioritization.

Aggregate-only evidence: `agent/validation/linux-wpt-20261030-session7/qa-frozen640/final214-residual-category-with-file-denominators.json`, SHA-256 `d95707e747de27f361b0fe019635dc261119cbdebdaec64b84b1935320f74818`. This follow-up performed no tests, browser launches, builds or production source edits; raw comparison rows remain local.

### Concrete next diagnostic scope: CSS stepped math

The engine reviewer recommends freezing the complete non-tentative CSS Values family `round-mod-rem-invalid.html`, `round-mod-rem-computed.html`, `round-mod-rem-serialize.html`, and `round-function.html`, with the three `round-length-degrees-crash.html`, `mod-length-degrees-crash.html`, and `rem-length-degrees-crash.html` crash neighbors and their actual support files. Keep assertion-based and crash-only results separately labeled. Start with an immutable candidate-214/current-Chromium comparison under the existing guards; do not predict gains from source review alone.

Bounded entry points are `src/css/style.c` functions `math_fn`, `parse_dim`, `calc_term`, and `css_parse_length_context`, specified CSSStyleDeclaration serialization in `src/js/prelude/03-element.js`, and native computed-style serialization. Read-only hypotheses to inspect: eager rejection of some percentage/compact-comparison operands, complete-input-consumption checks differing between top-level functions, and division handling when the divisor scalar slot is zero. These are unmeasured hypotheses, not established root causes. Preserve angle/dimension checks and unchanged whole-file expectations, and add only evidence-backed context/serialization controls. This investigation does not require the rejected broader cascade-provenance rewrite. No browser, test, build or production edit was performed for this recommendation.
