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

## Linux engine handoff — session 9, 2026-10-31, accepted candidate 234

Candidate **234** is the current technically accepted Linux engine. The CTO accepted all 16 independently reviewed gates. The selected static/parsing containment scope is **100/100: 286/286 assertions and 28/28 reftests**, not a whole-engine or full-WPT score. Dedicated URL work remains deferred. Final public artifacts and downloads are verified; use the dated delivery section, final receipts and `devsummary.md` for their exact identities.

The prior session8 handoff is archived intact at `agents/archive/gpt-6-astra-pro-builtin-rubywebterm-AGENTS-session8-20261031.md`. Its226-as-current standings are historical. The architecture/privacy/authorization boundaries reproduced below remain applicable.

### Accepted identity and evidence

- Tested source: `206bb89d161d91278c56899a3f664a99480374f8`.
- Immutable app: `build/codex-native/lumen-astra-i234`; SHA-256 `253f563c6529af4d8bab38b12373b51aec9941eb475fa16cae50093ff4e2b282`.
- **650** recorded build inputs, digest `61bf484b7bcfaf091adf135747423b8ce163c2b48884d5076bd06a469826094f`; both manifests and build binding are in `agent/validation/iteration-234/`.
- Independent acceptance: `agent/ops/linux-wpt-20261031-session9/candidate234-independent-final-review.json`, SHA-256 `aa0068187a3ddf7f4006c89543acd05a92b1a442f46ed9b7210042a7aa79f4af`.
- Main evidence: `agent/validation/linux-wpt-20261031-session9/`. Accepted report preparation: `agent/ops/linux-wpt-20261031-session9/public-docs-preparation/`; delivery metadata may be versioned without changing the engine.
- Working branch: `gpt-6-astra-pro-builtin-repo-baseline-rubywebterm-20261031-wpt-nonurl`. Later evidence/document commits are distinct from tested source. Start new engine work with a fresh immutable candidate; **235** is the next unused ID.

### Measured conformance — separate denominators

| Scope | Accepted234 | Recorded226 / pinned Chromium156 |
| --- | --- | --- |
| Fixed640 non-URL WPT sample, whole-file passes | **417/640 (65.15625%)** | 411/640 / 560/640 |
| Fixed640 native assertions | **9249/10540 (87.751423%)** | 9162/10540 / 9551/10552 |
| Selected39 containment assertions | **286/286** | 81/286 / 286/286 |
| Selected39 containment reftests | **28/28** | 1/28 / 28/28 |
| Whole `/html/syntax/parsing/the-end.html` | **4/4** | Previously load-target/pageshow failures; reference4/4 |
| Separate visibility/observer API3 | **18/23** | 10/23 / 23/23 |
| Selected native tests | **147/147**, zero skipped | 150 registered; exactly3 URL targets excluded |
| Retained rendering controls | **40/40** | 29 stretch plus11 paint cases preserved |

The640 campaign is the existing frozen sample, not the WPT repository. Relative to226,234 gains **6 whole files and87 assertions**; relative to231,1 whole file and3 assertions. No prior assertion, whole-file, harness or identity regression was found. The engines share10538 assertion identities: reference-PASS/native-nonPASS1111, native-PASS/reference-nonPASS821, reference-only14 and native-only2. Do not normalize these into an invented common denominator.

The39-file containment group is exactly logical001/002/003, numbered001–033 and the three parsing files under `css/css-sizing/contain-intrinsic-size/`. Its primary logical subset is48/48 assertions plus2/2 reftests. This does not complete the separate remembered-size/auto/interpolation family.

Authored controls are not WPT: unchanged visibility-options49/49 (231 baseline15/49; reference49/49) and separate slot-accessors2/2 in both engines. The lifecycle contract has12 cases/114 canonical records with explicit residual allowances, not full trace equivalence. Hidden-scroll covers7 cases/30 phases with one declared hidden-size distinction; the unchanged upstream hidden-scroll control is1/1. Keep all these separate from640 and39.

### Implementation boundaries to preserve

- Containment has real typed physical/logical properties, final writing-mode mapping, strict grammar/support checks, canonical generator metadata and native intrinsic contributions. Author width/height/aspect ratio, min/max and content/border-box rules remain authoritative. Descendants still lay out; there is no getter geometry substitution.
- The containment-family cascade replay preserves physical/logical declaration order and existing importance/origin ordering, resolves variables through the existing path, and projects `all` only to this family. It does **not** implement global revert/layer/origin rollback. Keep canonical generic `column-count` storage alongside its typed field and existing integer/math parser.
- Hidden content retains the owner decoration and suppresses descendant/replaced painting/hit testing only where applicable. Shared effective element-scroll helpers retain backing offsets, ignore hidden-owner writes, clamp revealed ranges and preserve state through non-scroll-container overflow changes. Visible nested scrollers, signed/from-end conversion and viewport routing remain intact. No per-box allocation or Android focus rewrite was added.
- Grid auto-repeat uses actual patterns, axes, gaps, auto-fit collapse and used/absolute track geometry; preserve gap-array ownership/bounds. Empty contained multicol max-content sizing is bounded, not general multicol layout. Normal button/select/video and authored dimensions have independent controls.
- Image bilinear interpolation combines premultiplied products and rounds once per pass in scalar/NEON paths. Six changed goldens have independent per-channel reference proof; unchanged punch frames and sanitizer evidence remain. ARM64 NEON was compiled, not runtime-tested; no hardware-GPU claim.
- Intersection updates remain change-driven: real rAF/RO opportunities,16ms fallback, dirty/pending gates and pre-callback generation snapshots. The settled3.3s control has zero geometry rescans/duplicate notifications. Hidden descendants preserve last-reported ResizeObserver size and do not intersect.
- `checkVisibility` implements visibility/opacity aliases plus complete ordered dictionary conversion and receiver/error/getter semantics. Native styles/geometry and a method-local native flat-parent resolver use private manual-slot metadata, without public style/geometry/slot-name getters. Shared slot helpers, unslotted box generation and auto-skipping are outside this patch.
- Genuine native DCL/load/pageshow use a private event route. Load/pageshow dispatch on Window with Document target override; synthetic/resource events stay unchanged. Pending/InProgress/Complete distinguishes active same-Document open, completed-open restart and actual context replacement/removal. Re-find live context identity before dereferencing after callbacks; retain measured Window registration/owner-load ordering.

### Preserve rejected candidates and real failure evidence

227 was incomplete: native priority/raster failures and a prior select pass loss remain recorded.228 exposed the unchanged old stepped-math test's NULL generic `column-count`;229 repaired canonical storage without weakening that test. All immutable candidate outputs remain.

232 passed normal lifecycle/WPT behavior but the actual instrumented host exited23 with **54,207 bytes/48 leaked allocations**. Its former CTest PASS regex masked that exit; independent review rejected it. A QA host copy with only three completed-batch pin releases, the same frozen instrumented engine and unchanged fixture, produced actual0 with identical125 raw rows and no sanitizer findings.233 applied that host cleanup and opt-in `JSRUN_EXPECT_ATTR_VALUE=PASS` with actual-exit gating. Missing/FAIL completion controls return1/1 after cleanup. No suppression, engine ownership workaround or changed lifecycle oracle was used.

Final234 passes targeted C/math/grid/raster sanitizers and the actual instrumented C++ lifecycle/options host, including49 options and the negative completion controls. This is targeted instrumentation, not every possible native configuration. Keep invalid adapter/transport attempts and corrected receipts separate; never replace historical evidence.

### Known unresolved behavior and comparison limits

- `checkVisibility.html` is **16/21**; separate IO and RO are1/1 each. Five failures remain: auto outside the viewport; auto with hidden content inside; unassigned child; child not assigned to the only named slot; and listbox option/optgroup boxes. No complete auto/remembered-size/layout-style-skipping or visibility-box implementation is claimed.
- Explicit hidden geometry retains layout data. Only `range-shrink-and-regrow` / `hidden-shrunk` leaf dimensions differ in the30-phase contract: native150×130 versus reference cached400×400. Every other declared field remains strict; never fabricate stale geometry.
- Initial child-load microtasks retain synchronous iframe-insertion timing. The specifically allowed pageshow-open load-microtask marker observes `activeGeneration=replacement` instead of reference initial. Post-removal author statements may finish, but stale native owner-load/pageshow follow-up is forbidden. Event metadata/counts are strict. Pinned Chromium owner-load-before-pageshow ordering is retained, not claimed as complete current-HTML ordering conformance.
- The old authored IO “below threshold still reports intersecting” control disagrees with Chromium in preservedv1. No actual threshold entry was logged, so neither fixture error nor Chromium bug is established. Cadence/retention agreement is separate; see session9 `gpt-6-astra-pro-builtin-rubywebterm-io-threshold-handoff.json`.
- XML fragments/full3D transforms, broad vertical/cyclic Grid, complete multicol, global CSS rollback and typed custom-property animation remain incomplete. `document.domain`/incumbent-global and asynchronous iframe/realm prerequisites are separate work; no origin shortcut or synchronous test-specific bridge was added.
- Reference adapters expose their own completion/owned cleanup, not Chromium OS exit. Keep native harness statuses explicit. Native physical-capture refusal and reference fake-device behavior do not establish positive media conformance.
- This is a dynamically linked Linux development executable. Exact dependencies are documented; there is no portable standalone package, fresh hermetic rebuild or byte-reproducible compilation claim.

Planning-only next-scope inventory: `agent/validation/linux-wpt-20261031-session9/gpt-6-astra-pro-builtin-rubywebterm-next-scope-inventory.json`. It does not authorize reopening deferred or previously rejected broad work.

### Reproduction, resources and supported execution

Use fresh candidate/output IDs and the established quota wrapper; freeze source/Git during provenance-sensitive work. Session9 used jobs2, native testsj1 and at most two browser workers. Do not change upstream WPT inputs/expectations, protected helpers or runner semantics to obtain a pass.

The exact147 names/portable commands are mapped in session9 `gpt-6-astra-pro-builtin-rubywebterm-native147-replay-inputs.json`. All650 exported inputs,457 tracked test files and241 canonical project dependencies match accepted source/base/extras. Generated embeds and the `viewer_fixture` producer/consumer are mapped separately.

Native147 needs **64 external WPT inputs**, separate from650 project inputs:62 parser `.dat` files, `fonts/Ahem.ttf` and `css/css-values/resources/ChTestZeroWidthZero.woff`. They match WPT commit `0e1c0bce3defd59a76cf2f351c7da5c5a42b26fb`, tree `473532b15684bbb7461cfcc6f5c1bb6adf909bc3`. Put WPT at `<source>/../../wpt` before CMake; a safe layout is source `<work>/browser/lumen/lumen` and WPT `<work>/browser/wpt`. Require the exact62-file parser set, all64 hashes, actual cached font bytes, all147 names and no skips. Missing WPT can silently omit `html5lib`; missing fonts are not equivalent coverage. The public Linux guide contains checked preflight/replay commands.

Supported public launchers remain `build_candidate.py`, `run_guarded_cohort.py`, `run_guarded_fixture.py` and pinned reference adapters. The reftest pilot documents WPT reference-graph screenshots. No supported arbitrary owned-page screenshot route was established: do not invent a graph/root workaround, inspect protected implementation or bypass guards. The live demo uses marker-only owned-fixture validation; its settled marker is transport completion, not a WPT score.

The sequential matched15s layout fixture completed the same200-node/50-step workload,10 checks and checksum21144 in226,234 and Chromium156. Final234 fixed-work CPU was0.289915 CPU-seconds over3.998444 covered seconds; late-idle0.039984 over4.004577 (about1% of one core). Sampled whole-window RSS/PSS maxima were167.748/152.391MiB (226:169.607/154.521; Chromium browser-only:1417.294/508.930). Fixed-work CPU rose0.019637 seconds versus226; memory was modestly lower; no predeclared material-regression threshold was met. These are single-run paced/sampled observations, not throughput/speedup or exact high-water marks. Keep ChromeDriver-inclusive values separate. Audit: `agent/ops/linux-wpt-20261031-session9/candidate234-independent-resource-audit.json`.

### Delivery state and private/public boundaries — verified

The accepted234 artifacts are published at **https://ecooxai.github.io/lumen/**. The final unauthenticated reader pass ended at **2026-10-31T11:12:51.252420Z**: all17 canonical files and the source checks returnedHTTP200 with exact expected bytes/hashes. The downloaded runtime contains the unchanged accepted app; both live-demo aliases match the measured fixture. Receipt: `agent/validation/linux-wpt-20261031-session9/qa-public-reader234-v2/receipt.json`, SHA-256 `0d0b4d86780fd180a019351f95c54e8a9e3ecee935fb5143229aeacec17d412e`.

- Public source branch: `gpt-6-astra-pro-builtin-rubywebterm-linux-containment-lifecycle-source234-20261031`; final commit `b3702f8a5f2a1b41f5baf346cbc17e3c516001da`, tree `e7560b9d2cae7d6e7bd014a15182876a1784dde7`.
- This is a normal three-file documentation child of engine export `d72c9d8225a733dd959e1b81389bb3dd699ebc86`. All650 measured engine inputs, the tested executable, report and16 technical gate hashes are unchanged. The corrected local commands use explicit `file://` URLs. The first published guide/package and all failed recovery attempts remain retained evidence.
- Final Pages content commit: `bb9562d265d2d09f5a10bf4a069299713353e69a`, tree `96d6f130e98a5356e652a7a5c2791523af8d53bb`, on `gpt-6-astra-builtin-agents-rubywebterm-showcase-20261028`. Normal non-forced source and Pages pushes were remotely verified.
- `/src.tar.gz`:9,755,285 bytes; SHA-256 `e23323d22d76ff0d655d38be5f7c8b4b1364e21d98dee0b131f2aff824c8ed29`.
- `gpt-6-astra-pro-builtin-rubywebterm-lumen-linux-source-i234.bundle`:4,937,370 bytes; SHA-256 `bff571dd105b6f7d7f289c7b961b6c70cc5c0a30a17f79bf1bc589a3f00e6e79`. It is offered separately and included in the source archive. ExactlyHEAD and the named public branch resolve to the same final public commit; both default and explicit-branch clones, fsck,217 public ancestors,749 files/modes and650 inputs were verified. Private history is excluded.
- `gpt-6-astra-pro-builtin-rubywebterm-lumen-linux-x86_64-i234.tar.gz`:2,566,009 bytes; SHA-256 `47e3794aa199129dc64c756ee97e834f6ecc0da0add905a33484d55c3ada44eb`. The extracted7,335,352-byte app has the accepted253f563c… hash above. This is the tested development executable with external library requirements, not a portable standalone build.
- `gpt-6-astra-pro-builtin-rubywebterm-lumen-linux-i234-showcase.mp4`:556,783 bytes; SHA-256 `c5c0905d24f60ef8b676e81e82bb0be60d45de4546cb348df8f8c5929376d5e9`;40 seconds,960×720,H.264,24fps. Five extracted frames were visually reviewed. It is a measured-results motion presentation, not native desktop footage.

The public build guide, reports, runtime identity and exact147 replay inventory are under `/docs/`; `ARTIFACTS234.json`, `SHA256SUMS234` and `PUBLIC-GIT-RECOVERY.json` bind the final downloads. The report is an immutable qualification-time snapshot; final delivery records supersede its earlier publication-preparation state without changing test results. Public publication review is `agent/ops/linux-wpt-20261031-session9/candidate234-independent-final-review-v3.json`, SHA-256 `63bbbe783fb2dbf70c0fddb052631ae2ffea788e8f2bd30c83447320742d5c65`.

The live preview remains `https://a0bb38e846444140.5x7aqstuceku.opentunnel.xyz`. Its `/src.tar.gz` and `/showcase.mp4` were verified against the same final source/video hashes. Server/authentication/WebSocket/message controls stayed unchanged. See `agent/ops/linux-wpt-20261031-session9/preview-promotion234-v2/receipt.json`; the final inbox and telemetry health checks are recorded by delivery. Active-session polling is not an unattended response promise.

The interactive demonstration is `gpt-6-astra-pro-builtin-rubywebterm-lumen-live-demo.html`. It runs in the browser that opens it; open it in Lumen to exercise Lumen. Its HTML is byte-identical to the marker-validated owned fixture9b330a84…; its real76×40 containment,80/90 retained scroll, visibility and genuine lifecycle readings are outside WPT scoring. No arbitrary-page screenshot workaround was used.

The documented FileHarbor API returned403. Private Page checkpoints preserved the accepted engine code and earlier rejection history; the final delivery checkpoint adds the verified public links. Final artifact delivery elapsed31.083606 minutes after the221.570601-minute technical phase, reaching252.654207 cumulative minutes at reader acceptance. The final post-task bookkeeping/checkpoint end and total wall time are recorded in `agent/validation/linux-wpt-20261031-session9/final-accounting234.json`; do not sum overlapping agents.

Public export is curated from approved public ancestry plus exact inputs and reviewed extras. Never push private working history, `agent/task.md`, these notes, authentication, profiles, private capture machinery or whole private evidence folders. The unchanged full user request remains in `agent/task.md`. Final private documentation commits must retain all650 source inputs and the accepted app; see the final handoff-equivalence receipt.

The display restart rule in the first121 lines remains applicable. Final preflight found no `/tmp/lumen-display0.pid`, so no existing display/process/profile was inspected or changed and no desktop process was launched. The accepted default executable is ready. Receipt: `agent/ops/linux-wpt-20261031-session9/desktop234-preflight.json`. A missing/stale PID never authorizes terminating an unrelated process.

### Retained prior architecture and authorization boundaries

These prior-session implementation constraints remain applicable; their older measurements are historical.

- `07k-sanitizer.js` / `07l-trusted-types.js` share parser-option compliance across modern and legacy sinks, private snapshots, captured internal operations and native-branded values. Accepted parser options do not bypass XML TrustedHTML checks; prototype shape is not authority. Private CSP snapshot/branding helpers stay in the inaccessible native prelude capability.
- Native script preparation uses a per-node force-async flag, explicit prepared external classification, generation cancellation and bounded asynchronous loading. Ordered dynamic `async=false`, parser-blocking, defer, DCL and load phases are distinct. Keep cancellation cleanup and generation checks before load bookkeeping; late jobs from document.open must not decrement the new generation.
- Persistent script-created input streams and initial-document prefix streams share the native tokenizer. Safe parser yields precede author constructors/reactions; do not call author code inside a C tokenizer frame that can be invalidated by document.open/write. Pending insertion parents/references and parser nodes remain retained across callbacks.
- Custom elements use fresh construction where required, actual native interface identity, a per-Document dynamic-markup counter and staged attributes/reactions. Preserve the current algorithm's already-parent insertion guard. Native `is` state is distinct from the mutable attribute and survives clone/import/serialization with exact byte lengths; the measured Linux Node layout remains **304 bytes**.
- Templates use real inert associated owner Documents; clone/import and parser insertion adopt into the actual target owner, including nested templates and shadow trees. Retired-document teardown releases resources before trees and keeps all participating owner metadata alive until final destruction. Never eagerly destroy the still-live top-level Page DOM during js_free.
- CSS resolution reads an immutable map of winning local custom declarations, tracks active cycle segments, selects fallbacks according to the pinned current algorithm, preserves token boundaries and checks actual math whitespace. CSS-wide dispatch and custom-declaration brace handling are bounded corrections; **full rule/layer/origin rollback is not implemented**.
- Native frame source mutation notification handles equal-value assignments, namespaced-attribute exclusion, srcdoc precedence and pending load accounting. Old pending responses cannot commit over a requested replacement. Keep ordinary attribute setter fast paths and the existing no-Node-growth design.

The 72-file stream cohort retains popup/opener case 047: top-level Pages use separate isolates and restricted remote handles. Correct arbitrary same-origin opener access needs context-group ownership, WindowProxy retargeting/origin checks and navigation/close lifetimes; do not add a test-specific property bridge. The clone cohort retains inert Document.write/declarative-shadow-root behavior. One parser-CE anchor-order expectation differs from the current normative already-parent guard; preserve that guard rather than forcing a reference quirk.

Initial-prefix stylesheet prefetch can still delay JS context creation, and document-written external blocking fetch remains synchronous. **Bootstrap policy metadata is still captured by a preliminary native parse; later head meta-policy arrival therefore remains eager even though the author-visible DOM now advances by prefixes.** This preserved policy timing limitation is not covered by a blanket claim of streaming/security completeness.

Automatic approval review rejected the proposed broad provenance-aware cascade rewrite as a high-blast-radius change under general authorization. It was not applied, split into equivalent edits, or rerouted. Only the separately permitted small keyword/block fixes were applied. Separately blocked custom adversarial probes were not rerun or claimed as accepted. Keep those boundaries explicit; seek appropriate new authorization before undertaking a blocked action. A request to read protected monitor/endmonitor implementation in paired_wpt.py was also rejected; no read, retry or reroute occurred. Existing documented runner invocation remained unchanged, and an independent ordinary-process sampler was used only for separate media encoding. Ordinary production/conformance work continued through supported interfaces.

EventSource is currently undefined. If it is implemented later, SSE reconnection IDs require explicit UTF-8 encoding before crossing the ByteString HTTP header boundary. Do not treat the current Fetch/XHR repair as SSE support.

Full rule/layer/origin cascade rollback, typed custom-property animation and broader computed/shorthand serialization remain incomplete. Preserve existing native selector validity and realm/lifetime rules; never invoke author-overridden getters inside native style matching. The previous auto-review refusals above concern those specific protected or broad actions; they do not describe the resolved session8 OPS publication-preparation ambiguity.

### Next-agent starting point

Read this dated handoff, `devsummary.md`, accepted234 final review and actual final delivery receipts first. Keep the selected39 score distinct from fixed640 rates, API3 residuals, authored controls and native147. Treat234 as accepted/immutable; preserve source/binary hashes when documentation/publication commits advance branch HEAD.

For a new task, establish ownership and a fresh candidate/branch before editing. Choose one whole bounded non-URL scope and pin baseline/reference identities. Do not resume deferred XML/auto/realm/cascade work from an old plan without checking current authorization. Use retained failures and explicit residuals rather than rediscovering them or claiming broader parity.
