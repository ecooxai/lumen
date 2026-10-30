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

## Linux engine handoff — session 8, 2026-10-30

The host, resource, privacy and display policies above remain unchanged. Root accepted candidate 226 for the measured scoped release; full WPT and whole-browser 98/100 remain unestablished. The complete previous handoff is preserved at `3f8f41434bfca738a7a23ddd9448e6acf1fe6c4d:AGENTS.md`.

The task started 2026-10-30 11:45:56UTC, with the requested primary deadline 16:45:56UTC and an extra hour reserved only for important bugs. The user asked for real non-URL WPT improvements, a Chromium comparison, honest iteration scores, one-second resource monitoring, and independent agents. The complete original request remains in `agent/task.md` (SHA256 `b55b43dd9839dc4e1d91e4dbf31a6385a1ebe96dceb681cf134fe6f0b421a378`). URL conformance remains deferred. The final actual completion time, cumulative wall minutes and stop rationale are `the technical iteration ended at 2026-10-30T15:59:39.154365+00:00, after 253.719239 elapsed wall minutes; root accepted the scoped engine after the independent packet. Final delivery accounting is recorded in the later private delivery-status receipt`; the full 12-iteration ledger, including failed candidates, is in `devsummary.md` and `agent/validation/linux-wpt-20261030-session8/iteration*-summary.json`. Do not sum overlapping agent work intervals.

### Source and executable identity

- Working branch: `gpt-6-astra-builtin-repo-baseline-rubywebterm-20261030-css-math`.
- Tested production candidate 226: source commit `3f8f41434bfca738a7a23ddd9448e6acf1fe6c4d`.
- Immutable executable: `/mnt/p3/project/browser/lumen-android/lumen-android/build/codex-native/lumen-astra-i226`.
- Binary SHA256: `0305887bb666840e82857f13d28e6c5af5e33c7e2071e015c2e47c669cbf35b2`.
- Default launch path: `/mnt/p3/project/browser/lumen-android/lumen-android/build/codex-native/lumen`. It matched the immutable 226 hash after the native gate; final delivery rechecks that equality. This is a Linux development executable, not a portable binary distribution.
- Recorded build-input manifest: `agent/validation/iteration-226/source-manifest-before.json`, file SHA256 `c94d38938ed7c4e0981dd7edf70308c6fa15bfbc529695aba1a7fed71102b691`; embedded recorded-input digest `1b26f13aa718584af4ac97126fe5367d32e01d9c1ee45430662c2c962500596b`, covering 646 inputs. Before/after inputs matched. This is not a compiler-generated transitive dependency closure.
- Build/startup:2026-10-30 15:36:28.399472–15:37:00.478171UTC,32.078699s, actual exit 0. Native gate:15:37:00.478699–15:38:22.546155UTC,82.067462s,143/143 tests,0 failures/skips,199 math/153 stretch/165 MIME direct checks, actual exit 0 and monitors joined. Exact receipts are `build226/receipt.json` and `native226/receipt.json` under the evidence root.
- Final documentation/progress commit: `recorded by the hash-bound post-commit handoff-equivalence receipt; it is a separate non-engine commit`. It must link back to the tested source above; recheck all 646 build inputs unchanged after documentation. AGENTS.md,devsummary.md and progress-site metadata are outside that recorded input set. Never relabel a rebuilt export or later changed source as the measured executable.

### Measured conformance: separate denominators

Evidence root: `agent/validation/linux-wpt-20261030-session8`. WPT checkout `/mnt/p3/project/browser/wpt`, revision `0e1c0bce3defd59a76cf2f351c7da5c5a42b26fb`. All upstream tests/support/fonts and expectations stayed pinned; crash-only smoke, authored controls, native tests and screenshot comparisons are separate cohorts.

| Scope | Initial Lumen evidence | Final 226 evidence | Chromium 156 control |
| --- | --- | --- | --- |
| CSS round/mod/rem,4 complete assertion files | 214: 462/594, 0/4 files | `594/594, 4/4 files` |592/594,3/4 files |
| CSS stretch sizing,48 assertion files including6 tentative alias files | 219: 218/1,012, 11/48 files | `1,012/1,012, 48/48 files` |1012/1012,48/48 files |
| CSS stretch rendering,29 reftests | 219: 1/29 | `29/29` |29/29 |
| MIME/nosniff,7 assertion files | 221 safe six: 37/86; old native importScripts intentionally unrun | `87/87, 7/7 files` |87/87,7/7 files |
| CSS progress,3 whole assertion files | 214: 12/139; 219: 90/139 | `138/139, 2/3 files; rotate3d deferred` |99/139 |
| DOM insertAdjacentHTML,4 whole assertion files | 223: 54/72 | `71/72, 3/4 files; XML fragment gap remains` |72/72 |
| Grid percentage sizing,2 whole assertion files | 223: 19/44 | `29/44, 1/2 files` |44/44 |
| CSS math neighbors,16 whole assertion files | 214: 115/432 | `173/432, 3/16 files` |430/432 |
| Separate stacking/background paint guards | first measured after paint correction | `11/11` |11/11 |

Final focused audit: `agent/validation/linux-wpt-20261030-session8/candidate226-focused-audit.json, SHA256 07c0362063c77061bf8acb637eed5caaf02b3b413b51e073fedc4386e73e4600`; rendering audits: `qa-css-stretch-reftests/lumen226-audit-v4.json (f04ccd286afd9590449b3edfc993e475da09e1899acdc11b96c99c2f636aa5ee) and qa-css-paint-reftests/lumen226-audit-v4.json (e652a0880f30d87cb4d652fbcb2231f6ccf4742102159762b788bc66cf78c4d6), relative to the evidence root`. The final authored/control outcomes and prior-pass identity audit are `22 mandatory authored fixtures, 424/424 registered fields, with all prior passes retained; the separate namespace diagnostic remains 1/2 with its SVG pass preserved and XML case failure disclosed. Ten grid geometry observations match the reference and add zero WPT assertions. All 80 rendering captures exited zero with owned cleanup complete`. The current Chromium draft implementation fails some unchanged NaN/no-clamp/equal-endpoint expectations; do not weaken those expectations to match its bugs or older semantics. The selected scope percentages do not establish overall WPT coverage or a whole-browser 98/100 score.

### Exact 640 broad regression sample

Frozen manifest SHA256 `8b5d0d491ce8b14e6ee8107d1213c10b90127a0193207d06057216bd54f3243f`; unchanged disjoint 320+320 schedule, same deadlines and at most two browser workers. Coverage is CSS 242, DOM 113, Fetch 98, HTML 85, XHR 34, Encoding 34 and DOMParsing 34 files. This is an assertion-test sample, not the full WPT repository, screenshot/WebDriver/manual coverage or full ECMAScript/Test262 conformance.

| Engine | Fully passing files | Passing reported assertions |
| --- | ---: | ---: |
| Starting accepted Lumen214 |401/640(62.65625%) |8764/10540(83.1499%) |
| Final Lumen226 | `411/640 (64.21875%)` | `9,162/10,540 (86.925996%)` |
| Chromium 156.0.8078.80 |560/640(87.5%) |9551/10552(90.5136%) |

Final exact prior-pass union, common-identity gains/losses, reference-only/native-only registrations, harness errors/timeouts,640 actual process exits and cleanup: `398 common nonpass-to-pass gains and zero losses/missing/added native identities versus 214; 10 whole-file gains and zero losses. All 9,162 required prior PASS identities and 411 whole files from 49 pinned inputs survived. Harness counts remain 624 OK, 15 TIMEOUT and 1 ERROR; all 640 actual native exits were zero and profiles/monitors were removed. Against Chromium, 10,538 identities are shared, with 1,198 reference-PASS/native-nonPASS and 821 native-PASS/reference-nonPASS; 14 reference-only and 2 native-only registrations are disclosed separately`. Preserve different denominators and duplicate-name indices. All earlier required PASS identities and whole files must be retained. Final interval and evidence paths/hashes: `2026-10-30T15:46:23.955777+00:00–2026-10-30T15:59:39.154365+00:00; qa-frozen640/lumen226/comparison-public.json (fe4e086aca5d941ec217804eb297542d0f645762d33d92216105e83bfdaaafa5) and prior-pass-union-audit.json (85658770e55822c7ecc4d9793d553ae4bb996497f3fa393d5339f43761852bd4)`. No stitched total from candidates 218/223/focused runs may substitute for the fresh final 640 replay. The stepped 594 family is outside this 640 manifest, so its gains are not added to that denominator.

### What changed and the boundaries to preserve

CSS math remains a 28-byte allocation-free `Length` POD copied safely through styles, grid tracks and movement snapshots. Two affine stepped operands plus an affine output offset handle the measured round/mod/rem percentage cases. Constant-denominator progress reuses affine/clamp arms with a provenance flag; only those marked values receive double intermediate resolution and strict unsupported-algebra rejection. There are no global interned derived expressions, retained AST allocations or layout-time expression reparsing. General percentage-dependent denominators and arbitrary nested nonlinear percentage expressions remain unsupported. Do not silently approximate them or widen the representation without a complete ownership/copy/destruction audit.

Scalar/numeric font contexts borrow current/root styles only on the parse stack. Real ex/rex/lh metrics and font/root changes use existing dependency invalidation. Font-affecting destinations retain parent/initial metric rules. Typed integer rounding, invalid-declaration retention, NaN/infinity/signed-zero behavior and exact arity remain guarded. Existing atan2 dispatch now accepts its digit-containing identifier; this was a lexical fix, not a signed-zero formula change.

Stretch is a destination-confined sizing keyword, not an alias for100% and not a global `len_def` change. `layout_stretch_border_size(box,percentage_cb,horizontal,available,definite)` takes explicit definiteness and returns the final border-box size; callers must not apply box-sizing or padding/border twice. Actual containing blocks, auto margins, collapse exceptions, float-excluded space, weaker absolute insets, flex lines and grid areas remain distinct. The grid intrinsic repair is bounded to validated explicit plain fixed/percentage tracks and single-column placements, including cyclic0% provenance; unsupported spans/fr/minmax/implicit cases retain the older path.

The response MIME helper is shared by actual classic script/style fetch boundaries, with ordered header extraction and first-value nosniff semantics. Generic fetch/image behavior and strict module/worker rules remain distinct. The worker importScripts borrowed-header use-after-free was fixed by owning its diagnostic text before response destruction. The old unsafe native rejection path was never executed as a baseline. Actual post-fix rejection/no-execution/continuation controls passed; the V8 worker path was not instrumented by the separate raster ASan/UBSan fixture.

Paint changes preserve true stacking-context boundaries and frame-owned deferred state; glyph blending now uses one correctly rounded straight-color source-over calculation. Existing image/emoji compositing is unchanged. ASan/UBSan raster tests plus native29 stretch/11 paint pixel comparisons provide bounded evidence, not a hardware-GPU certification or global performance benchmark.

Adjacent HTML uses method-local brand/conversion checks, original Trusted Types ordering and intrinsic insertion. Its fresh body is only a parsing context. Real 3D transform composition/rotate3d and XML fragment parsing remain deferred. Keep the explicit known XML/SVG diagnostic outcome visible; preserve its prior SVG namespace pass. Do not project each 3D transform independently or substitute HTML parsing to claim XML conformance.

### Preserve rejected evidence and corrected authored oracles

Keep every immutable candidate and receipt. Candidate 216 was rejected for an uninitialized-operand safety defect despite selected WPT passes;217 for a font destination regression;218 for broad progress losses;220/221 for stretch/layout regressions;222's new native inherited-float oracle was independently disproved;223 corrected only that native fixture but its fresh640 exposed four grid losses;224 failed compilation before testing;225 lost four previously passing constant0% controls. These are not accepted releases.

Two authored-oracle corrections have explicit retained provenance: the inherited-float50px margin case legitimately places parent/child at50/50 (the separate10px case is0/30), and literal-0 is positive zero in CSS, so the progress context v2 changes only two inputs to arithmetic `-1 * 0`. Original fixtures/results remain. The final two generated-negative-zero failures were isolated to unreachable atan2 syntax before the lexical correction. Never edit upstream expectations or relabel a corrected fixture as the original.

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

### Supported reproduction, reference and workspace

Use fresh candidate IDs/output directories and the established quota wrapper:

```sh
python3 tools/codex_compile.py python3 agent/validation/build_candidate.py --iteration NEW_ID --jobs 2 --startup
python3 tools/codex_compile.py ctest --test-dir build/codex-native -j1 --output-on-failure -E '^(url|url_host_nul|data_url)$' --output-junit /absolute/new/native/ctest.xml
```

Keep one-second owned-process and cgroup resource monitors, source/Git freezes and exact before/after pins. Builds use 2 jobs; native tests use 1; browser concurrency is 2 total. Project-backed TMPDIR is `agent/t/session8/tmp` because the host root filesystem is nearly full. Sampled RSS/PSS/CPU peaks may miss short-lived children and are not memory bounds.

Use the supported public interfaces `agent/validation/run_guarded_fixture.py`, `run_guarded_cohort.py` and the approved reftest pilot through the existing wrappers. Final focused scheduling is `session8/run-candidate-followup-focused-v2.py`; final640 scheduling is `session8/qa-frozen640/run-two-native-v2.py`. Read exact invocation receipts/CLI help for new runs rather than guessing arguments. Guarded reference wrappers that pin installedChromium 150 refuse156; retain that refusal. The established scope-local `run-cft156.py` route (for example `session8/qa-css-stepped-math/run-cft156.py`) uses the supported paired runner with pinned156 browser/driver identity. The approved configuration-only156 reftest adapter is `agent/ops/linux-wpt-20261030-session8/reftest156-config/official_reftest156.py`. Do not modify protected guards to accept a different browser.

Approved capture configuration remains in `agent/validation/README.capture-refusal.md`; use the published guard pin/configuration via supported launchers. Do not read, rebuild, rehash, replace or weaken protected guard libraries, private capture helpers or auth records. Preserve capture refusals, cleared control/preload variables, fresh isolated profiles, exact process ownership and cleanup. Required Ahem/ChTestZeroWidthZero assets and support files stay pinned. Native existing-display/physical media capture remains prohibited under the policies above.

RubyWebterm's workspace validator accepts only `workspace:"/"` in this session; put an explicit safe `cd /mnt/p3/project/browser/lumen-android/lumen-android || exit` in command text. Use task label `Lumen Linux WPT 1030`. Truncated output is read by retained terminal ID with `--full`; never repeat a mutation/build merely because output was truncated. An actual connector environment error requires the user's 30-second retries before fallback; ordinary compiler/test/controller errors are diagnosed normally.

### Publication and final delivery

Never push the private working branch/history containing task text, diaries, private evidence, profiles or capabilities. Curate a separate public history rooted in reviewed public214 commit `5c0e775cfad2084ab5b8107837fe98275d54b35e`, with exact final recorded inputs/modes and explicitly reviewed new paths. The session8 qualifier rechecks public ancestry/allrefs, root decision and QA audit identity/hashes, regular-file allowlists, source pins and noncore partial-scope rows. Use normal non-forced pushes only. Package the committed public tree plus the reviewed Git bundle/restore/provenance files; verify an offline restore. Do not archive raw.git/unreachable objects, private validation setup or dependency caches. A source export is not an independently rebuilt/hermetic binary.

Final root decision, independent checkpoint audit, public source commit/tree/branch, archive hash/size/offline restore, Pages commit/HTTPS verification, report and walkthrough identities are `publication pending. The root accepted the tested engine using candidate226-independent-checkpoint.json (SHA256 ae3fcafa7f0901d918692761436dae75c4ae06aa6c20e2e1995075bc5b3f0b11); public promotion requires the separate root publication decision, exact handoff-equivalence receipt, isolated source qualification and verified archive/Pages receipts. The later private delivery-status commit records actual published identities`. The planned walkthrough is a labeled 36-second 1024x768 4:3 Chromium-rendered results presentation with model/agent/tool attribution, not native Lumen UI footage or a speed benchmark.

Public results/archive remain at https://ecooxai.github.io/lumen/ and its `/src.tar.gz`. Live progress remains https://a0bb38e846444140.5x7aqstuceku.opentunnel.xyz with 2s telemetry, WebSocket latency and authenticated feedback; final health/unread status is `delivery verification pending; the previously healthy task dashboard remains separate from engine acceptance`. It does not imply an always-on agent after this session. Surge remains installed but unauthenticated; no account was created. Final desktop ownership/restart result is `delivery preflight pending; no verified task-owned desktop PID was found during development`; never kill a guessed PID or inspect the existing display. Default-binary equality is verified separately from any actual desktop launch.

### Final score interpretation

Iteration 12 uses the pass-based rubric approved by the root agent: each completed target has its own 100/100 score (594/594 stepped assertions, 1,012/1,012 stretch assertions, 87/87 MIME assertions, 29/29 stretch rendering, and 11/11 paint guards). No upstream WPT denominator or expectation changed; the separately documented native/authored oracle corrections retain their original evidence. Partial-family and fresh fixed640 rates remain separate; release acceptance is a binary evidence gate. There is no blended 98/100 or whole-browser parity claim.

### Observed resources

The completed 640 workload used two browser workers with one-second monitoring. Observed per-case tree peaks were 278.061 MiB RSS and 146.344 MiB PSS; these are not simultaneous workload totals or memory bounds. Shared cgroup CPU averaged 44.348% of one core, peaked at 209.287% in a sampled interval, and recorded zero throttled periods under the 480% quota. Outcomes and timeout paths differ from Chromium, so no speed, idle-memory or efficiency advantage is inferred. The hash-bound resource receipt is included in the independent checkpoint.

### Next-agent starting point

Start from the final tested source and hashes above, not rejected224/225 or old214 status. Keep URL suites deferred unless the user reprioritizes. Use the final strict common-identity residual taxonomy `qa-frozen640/lumen226/residual-taxonomy.json, SHA256 6f7a0541846661cfb788748c99e681e0ccd0628a6abab192eac2a860c14b1848; the 1,198 strict shared-identity gaps comprise CSS Values 491, Grid 216, Sizing 130, Flex 102, Variables 95, CSSOM 33, DOMParsing 30, DOM 24, Fetch 23, HTML 21, Selectors 16, XHR 13 and Encoding 4` to select the next coherent whole-file scope before editing. The largest sampled coherent gap is calc-size (230 shared identities where Chromium passes and Lumen does not), followed by orthogonal absolutely positioned grid cases (100); inventory complete upstream families and architecture before choosing either. Known bounded follow-ups include the remaining vertical/cyclic grid percentage family and incomplete neighboring CSS math; the final exact counts must come from the audited rows. XML fragment parsing and real3D composition require explicit architecture/ownership plans, not a one-test shortcut. Preserve all selected completed families and the broad prior-pass union, freeze current upstream/reference support first, use same-capability independent review and finish with fresh affected scopes plus the unchanged broad gate. A focused100% score is not overall browser conformance.
