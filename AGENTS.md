# Agent notes for Lumen

Read agent/continue.md, agent/devsummary.md, agent/task.md and agent/next-agent-prompt.md first. The canonical repository is /mnt/p3/project/browser/lumen-android/lumen-android; WPT is /mnt/p3/project/browser/wpt. Use the RubyWebTerm MCP with the canonical workspace_id and relative paths. Current branch: gpt-6-astra-pro_builtin-agents_rubywebterm_session20-x-google-20261104. Older standings and device identities in archived handoffs are historical.

## Active session20 — 2026-11-04T12:52:19Z

User has authorized a new five-hour development window through 17:52:19 UTC, plus up to one hour only for important bug fixes. The latest full request is captured in agent/task.md. The previous session19 stop/deadline remains historical and does not limit this newly requested work. Priority: reproduce and fix X Google sign-in on display :0, then improve measured native web-platform failures. Preserve existing account state and require actual return-error evidence; do not infer success from synthetic GSI controls.

Same-model built-in agents: root is CTO; project_steward owns Git/docs and delivery infrastructure; auth_fix owns sign-in diagnosis/fix and display :0; platform_qa owns independent conformance testing. Coordinate exact file ownership and source freezes before builds. Checkpoint a8910478b3031c16dc0a6d8daf095837a7048321 preserves the prior tracked-clean source and handoff. Old untracked evidence stays in place. Prior continue/task/next-agent files are archived under agent/archive/*-session19-20261104T125219Z.md.

Accepted selected-cohort baseline is candidate2002 from source32e13542be0b4eacb801d878231e3e6f51461b40:800/1248 URLs and23168/26245 assertions, with every inherited assertion/URL/harness pass retained. Exact native gate218/218. Read agent/ops/session20/accepted-baseline2002.json and candidate2002-independent-full-acceptance.json. Display:0 and Android remain candidate1915 during active auth diagnostics; no2002 device installation is claimed. The session20 dashboard and scoped inbox were independently exercised; the private inbox watcher polls15s and an agent must review and respond to queued messages. Public source publication is in progress; preserve the qualified1915 download until complete2002 verification. Final Linux/Android restart requires exact accepted candidate identity and profile preservation.

## Historical session19 evidence and user priority

Session19 selected and delivered candidate1915. Original frozen1248 replay accepted at2026-11-04T12:23:57.325Z; selection12:23:57.846Z preceded the fixed12:24 cutoff. Result796/1248 URLs (63.782051%) and23158/26245 assertions, +37URLs/+332assertions over1808 and +15/+55 over1908, with zero required assertion/URL/harness losses and all1248 actualprocess exits0. This is a selected-manifest compatibility score, not95/100 overall quality or Chromium parity.

Tested productionHEAD b63df22ad8b67399a416ac0bc16c6d21613d23aa, src treec90cb233430c82237c7f05076415f2e7c28fac05, Linux binarySHA25671478e23bcfffeead3cbe0edc694a02bfae2b8ce78321f27212a5a656ef9c908. Final docs commits may advance taskbranchHEAD while keeping this source tree unchanged. No fallback checkout occurred. Last exact-source native gate was candidate1913 at238b68ba84881d3d853bb78fcf53202cdd40900a:218/218. Later1914/1915 changes were JavaScript-only; no exact1915 native run is claimed. Narrow1915 gates passed73/73 independent controls and the regression gate over59 URLs (34 fullURLPASS,511/592 assertions,59/59 harnessOK).

Qualified public source commit80fdbe8ccf9c38325c3f0ab16fd75cbc192402c1: https://github.com/ecooxai/lumen/tree/gpt-6-astra-pro-builtin-agents-rubywebterm-linux-source1915-20261104. Source archive10463864bytes SHA256521185eb206a2671a66dc3d6d80a80e484e6b30667e6f13a690048d11f9f51ab; offline Git restoration and complete external HTTPS readback passed12:31:25.721Z at https://a0bb38e846444140.5x7aqstuceku.opentunnel.xyz/src.tar.gz?checkpoint=1915. Public source uses only approved isolated ancestry, not canonical private history.

Display:0 final restart accepted12:27:37. Android build/install/finalrestart accepted, last guards sealed12:33:02.5; installedAPK SHA256b564cb4fc6e7e75d64d47bd70803cc67d809cbaff6cd5d228a81d4e23baf56aa,177615794bytes. Profile/UID preserved, no uninstall/data clear; four own-page checks and six foreground samples passed. Original shutdown/check failures and bounded recovery receipts are retained. The stale unselected CXX-model BuildID check was corrected in a separate operational wrapper; the actual packaged unique3-library identity and unchanged23+10APK gates passed. Two isolated36s1280x960 showcase copies were visually reviewed; no personal pages entered media. Android smoke is not AndroidWPT.

Session began2026-11-04T06:33:55Z. Five-hour deadline11:33:55Z was extended for concrete important bugs to12:33:55Z. Production work ended earlier; already-authorized final delivery/publication/docs receipts continued after that boundary. At this docs capture 2026-11-04T12:40:49.366207+00:00, actual elapsedwall22014.366s. Do not equate overlapping task/stage totals with user elapsed or model compute. Root was CTO; same-model built-in source_steward owned implementation/Git/docs, test_baseline owned immutable evidence, environment_delivery owned monitoring/preview/runtime/public delivery.

The earlier authenticated10:52:35UTC X/Google return-error and scrolling-CPU request is preserved in the archived session19 task. The following paragraph describes historical session19 observations, not the current session20 reproduction. No original X-return error or stable authenticated scrolling benchmark was reproduced. User-owned Google identity approval expired after inactivity; this is not proof of the reported engine failure. Account-free GSI3 and two unchanged messaging URLs pass1913/reference, not an auth-fix claim. The old1808 logged-out landing observation was median0%,p951%,max4% of one core, RSSmedian210.8/max279.2decimalMB; hostpressure was measured separately. Preserve profiles/auth state and private evidence; exclude the tool-induced malformed JavaScript-address probe from causality.

## Frozen conformance scope and acceptance

The1248 selected HTTP/default-timeout testharness URLs come from1247files/10DOM-CSS families, selected before results.44long-timeout cases and1HTTPS case were already excluded. This is not allWPT, visual/reftest/manual/network coverage, production certification, Chromium parity or95/100 quality. Pinned reference Chromium150.0.7871.114 with matching ChromeDriver is1062/1248 and24541/26064 assertions; compare exact common(URL,name,occurrence) identities, not mismatched assertion denominators.

Selected source files match upstream0e1c0bce3defd59a76cf2f351c7da5c5a42b26fb. Pre-existing resources/testdriver-vendor.js and resources/testharnessreport.js adapters and173framework pins are frozen and shared. Selection/manifests live under agent/validation/linux-wpt-20261101-session13/expanded-{selection,urls,manifest}.json. Selected manifestSHA256a97bc7c43083140f02c5f7ae3a46de905c8da6506c6a834928dbaa99d3ae63a6; upstream manifestv9SHA256440d5dc80c78ab184d977a7d58929732ce480522550a11c5664f17c76b5317ca.

Use never-reused positive candidate IDs and build_candidate.py --expected-head with the exact tracked-clean HEAD. Failed builds consume IDs. Historical1807 was accidentally overwritten and quarantined; no reconstruction is its original binary. Preserve the non-overwrite guard, runtime guards, framework/oracle/selection/timeout/worker settings and every retained original result. Source/Git/status freeze spans build and any exact-source native gate; after seal, immutable tests can proceed while later source changes. Prior passing URL/assertion/harness identities are binding; net gains alone do not qualify.

Original1808 first-full,1903,1911full,1912focus and1913full namespace TIMEOUTs after51PASS assertions remain HOLD. One earlier1808 preregistered reliability replay was accepted separately; do not erase its first failure. Matched1908/1912 diagnostics both showed slow completed initialization, prompt final parent completion and no missing-load evidence.40 remote frames create80 full initial/navigated realms; no central lazy-init barrier exists. No timeout fix, mutable-realm sharing, skipped initialization or relaxed deadline is permitted. LUMEN_TRACE_LOAD and LUMEN_PROFILE_PRELUDE are default-off diagnostics and must be explicitly unset for qualification.

Descriptor batching1910 failed fixed CPU/wall thresholds and was fully reverted; no speedup claim.1913 static importance preserves old assembly whenever anim_decls exists. Its larger transition-tier/ownership/private-channel draft is explicitly UNINTEGRATED because logical/physical membership and cancellation could introduce new prior-pass regressions. Retained artifacts are in source-work/session19/transition-tier-deferred-v1 and ops/session19/inline-importance-draft.

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

## Android data and device tests

Preserve application data: install with `adb install -r`; never `pm clear`, uninstall, or broad `pkill -f`.
Do not modify `build/codex-android-deps/SDL`. Physical media tests need explicit permission/isolation;
ordinary installation/restart does not authorize them. Historical device/UI commands remain in the archived handoff.

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
The user authorized relevant X/Google diagnosis on the existing display; preserve that active account flow and do not restart while it is in progress. Keep observations sanitized and private. Use a fresh isolated Xvfb
profile for showcase recording. Record the new exact PID for monitoring.


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


## Persistence, measurements and remaining work

Public exports must use exact tested input blobs and approved public-only ancestry in an isolated repository. Never push canonical private Git history or publish profiles, queues, credentials, screenshots of personal content or private validation machinery. Verify archive offline Git recovery and full external HTTPS bytes; archive recovery is not an independent rebuild. Keep accepted runtime/archive separate from WORK/HOLD source snapshots. Save significant progress and every10implementation iterations. Session19 completed31 implementation/review iterations. WORK10/20/30 were published with honest work-only labels; WORK30 publiccommit264562c7148e1454d094ce1cc4f2a4a454616419 preserves held1914. The corrected1915 source was published as a qualified release; an additional prepared WORK31 publication was superseded and was not executed.

Report per-iteration source mutation/review/build/test intervals honestly; the receipt-derived ledgers and environment worklog are separate from user elapsed wall time. Parallel/nested sums are not model compute or user elapsed time. Resource _mb and dashboardGB are decimal, CPU100% is one core. Investigate abnormal tasks over5minutes; complete1248 campaigns around17–23min are expected.

Concrete remaining priorities: reproduce the actual X/Google return error and measure stable X scrolling; preserve namespace reliability limitations; improve constructed/imported stylesheet lifecycle and native sheet.media semantics; typed attr/calc-size/context-sensitive numeric values; margin/padding storage and full all reset; ordinary animation/transition cascade and cancellation; separately declared rendering/network/real-site coverage. Random syntax acceptance is not sampling/cache runtime. Keep23unsupported tree-context controls visible. Do not infer Android WPT parity from Linux smoke/tests.

## Next agent

Use the final exact source/binary/proof/device/public mapping in agent/continue.md and agent/next-agent-prompt.md. Verify active owner/freeze before editing. Reuse the current task branch only while its session authorization continues; otherwise checkpoint safely and create the requested model/agent/tool-named branch. Preserve all HOLD/rejected/unintegrated distinctions and the architecture/approval boundaries above. Start from the highest-value measured gap rather than repeating historical runs.


## Operational dashboard during exact-source delivery

The live dashboard runs from the untracked operational mirror agent/ops/session19/delivery-dashboard/server.mjs with identical server/site bytes. Its runtime symlink points to canonical agent/progress/runtime, preserving current keys/messages; live mutable status is delivery-dashboard/status.json, not canonical agent/progress/status.json during an exact-source checkout. The authorized message deadline remains2026-11-04T12:33:55Z. Coordinate environment_delivery before canonical status synchronization or process restart. These runtime/private paths and key/message contents must never enter public source exports.

At11:50:25 the user-owned Google verification flow displayed a session-expired message after inactivity, with no X return observed and no assistant account action. This is the observed incomplete challenge outcome, not proof of the originally reported X-return failure or an engine fix. The final restart occurred only after fresh private preflight confirmed that expired state and no active flow. Recheck activity before any future restart.

## Final continuity pointers

Read agent/continue.md and the editable489-word agent/next-agent-prompt.md. Final QA and receipt-derived stage timings are agent/source-work/session19/test-baseline/gpt-6-astra-pro_builtin-subagents_rubywebterm_session19_final_qa.json and test-build-timings-v8.json. The latter records231 completed monitorstages (115outer/116nested), explicitly not elapsed/model time. Public final handoff publication receipt is build/codex-session19-tools/final-handoff/publication.json; its exact copied receipt is agent/source-work/session19/final-handoff-publication.json. Preserve original1808/1903/1911/1912/1913 reliability HOLDs,1914 invalid-alias HOLD and rejected/reverted1910. UNINTEGRATED transition research includes portable hash-checked reconstruction and must never be mistaken for production source.


## Final public handoff sealed

Published commit 59e3de4a3d0940be75c452851e6c5f94b64c9c86 at 2026-11-04T12:38:26.209329+00:00; exact remote commit verified. Handoff: https://github.com/ecooxai/lumen/tree/59e3de4a3d0940be75c452851e6c5f94b64c9c86/handoff/gpt-6-astra-pro_builtin-agents_rubywebterm_session19. Editable continuation: https://github.com/ecooxai/lumen/blob/59e3de4a3d0940be75c452851e6c5f94b64c9c86/handoff/gpt-6-astra-pro_builtin-agents_rubywebterm_session19/CONTINUE.md. Selected source: https://github.com/ecooxai/lumen/tree/gpt-6-astra-pro-builtin-agents-rubywebterm-linux-source1915-20261104. Branded showcase: https://raw.githubusercontent.com/ecooxai/lumen/59e3de4a3d0940be75c452851e6c5f94b64c9c86/handoff/gpt-6-astra-pro_builtin-agents_rubywebterm_session19/lumen_linux_gpt-6-astra-pro_builtin-agents_rubywebterm_candidate1915_showcase.mp4; clean recording: https://raw.githubusercontent.com/ecooxai/lumen/59e3de4a3d0940be75c452851e6c5f94b64c9c86/handoff/gpt-6-astra-pro_builtin-agents_rubywebterm_session19/lumen_linux_gpt-6-astra-pro_builtin-agents_rubywebterm_candidate1915_clean_recording.mp4. Deferred research: https://github.com/ecooxai/lumen/tree/59e3de4a3d0940be75c452851e6c5f94b64c9c86/handoff/gpt-6-astra-pro_builtin-agents_rubywebterm_session19/research-UNINTEGRATED. The handoff branch's parent source remains historical1908; its selected-source link is authoritative1915.

Complete external receipt/branded-video/preview/source readback passed12:39:22.969903UTC; clean-video/CONTINUE readback passed12:40:28.914502UTC. Actual delivery had completed12:33:02.5UTC. These later operations were posttask publication/readback/docs, not extended production development. Public/private receipt copies are in agent/source-work/session19/final-handoff-*.json. Canonical status was synchronized from the completed operational mirror during this final docs seal; future live state remains in the operational mirror.

## Session20 continuation at accepted candidate2002

Root accepted iteration1 after independent exact-pass and registration audit; overall assessment64/100, selected URL rate64.102564%, goal95 not reached. Full cohort ran13:28:18.233–13:49:17.471UTC (1259.185s); monitored orchestration1261.257s. CPU median69.001/max249.999% of one core, peak RSS615.023MB/PSS317.075MB; cgroup weighted87.06%. All1,248 exits0; harness1ERROR/9TIMEOUT remain. Registration26245 unchanged at every URL.

Preserve candidate2002-required-prior-pass-v1.json for the next candidate:23168 exactPASS identities,800 completeURLs,1238 harnessOK. Authored32 controls passed2002;14 Chromium computed-list disagreements remain outside parity claims. No allWPT or Chromium-parity claim.

Canonical source stays clean for priority auth integration. Edit-only auth worktree:../lumen-auth-session20-gpt6-astra-pro; platform worktree:../lumen-platform-session20-gpt6-astra-pro, both rooted at32e1354. No shared build/codex-native. platform_qa owns seven approved animation parser/name identity files there; root must coordinate canonical integration and one heavy lane. Auth has reproduced the real Google return error: valid server protocol framing contains the generic error; request correctness is under investigation. No production auth fix. Never publish agent/ops/session20/x-login-private, downloaded auth assets, account identifiers, messages or capability links.

Active deadline17:52:19UTC, extension undecided. Read agent/continue.md and latest receipts before acting on any PID or candidate. Do not restart display:0 while auth_fix owns an active diagnostic sequence.
