# Agent notes for Lumen

Start with `agent/continue.md` and `agent/devsummary.md`. The original user request is in `agent/task.md`. Historical root `devsummary.md`, `agents/archive/` and the archived prior AGENTS file remain evidence; their old candidate standings are not current-branch qualification.

## Active session19 coordination — 2026-11-04

Current task branch: `gpt-6-astra-pro_builtin-agents_rubywebterm_session19-20261104`.
The five-hour session started06:33:55UTC and remains active through11:33:55UTC, with an
optional additional hour only for important fixes. Root is CTO; source_steward owns source,
Git/index and continuity; test_baseline owns independent immutable builds/tests;
environment_delivery owns preview/monitors/status and runtime/public delivery.

Latest qualified full candidate1908 is781/1248 URLs (62.580128/100 selected-manifest
compatibility) and23103/26245 assertions, +22 URLs/+277 assertions over1808, with zero
prior passing URL/assertion/harness losses. Exact source/binary/proof identities and current
unsealed follow-ups are in agent/continue.md. This selected DOM/CSS manifest is not all WPT,
current latest Chromium parity or a95/100 product-quality claim. Historical details below
are retained background, not the current runtime or qualification state.

Candidate1910 descriptor batching failed frozen CPU/wall thresholds and is reverted.
Candidates1911 and1912 retain namespace harness HOLDs after51 PASS assertions; no timeout
change or lifecycle fix is claimed. Candidate1913 seals the reviewed same-name CSSOM guard
and static-only importance assembly. Sampled animation/transition assembly stays unchanged;
the larger private transition-tier proposal is explicitly deferred and unintegrated.
The latest authenticated preview request is X/Google authentication-return failure diagnosis
and X scrolling CPU optimization. Exact text, ownership and security boundaries are recorded
verbatim in agent/task.md and agent/continue.md. Candidate1913 exact-source native218 has passed; browser/full qualification remains pending.
Coordinate exact tracked source/Git/status freezes before builds and native gates; immutable
sealed binaries may be tested while subsequent source is prepared. Use never-reused IDs
and one heavy job under the existing480% shared quota. Preserve all old HOLDs and private
historical evidence. The full unchanged user request is agent/task.md; old handoffs are
under agent/archive and initial fingerprints under agent/source-work/session19/preflight.json.

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
Do not inspect or record the user's existing display; use a fresh isolated Xvfb
profile for showcase recording. Record the new exact PID for monitoring.


## Historical development handoff - session18 final checkpoint

Read `agent/continue.md`, `agent/devsummary.md` and the verbatim request in
`agent/task.md`. The canonical workspace is `/mnt/p3/project/browser/lumen-android/lumen-android`;
the WPT checkout is `/mnt/p3/project/browser/wpt`. The task branch is
`gpt-6-astra-pro_builtin-agents_rubywebterm_session18-20261103`.

### Source, verification and measurement boundary

The tested engine snapshot is `fcf2d2d83686a554f492fbaf992173b12d924b64`, engine commit
`d93c9769c750dacbfd4bf415aa0523fbc0de7d6f`, source tree
`905f9cd45711a1730d05807d85d99002b34e31a0`. Its Linux binary SHA-256 is
`79af7023284551d597ce61ff4a5e3954a7edd2a43b4b8ee2af5d63a1658ff46d`; the 742-input
manifest is `5c73f1b303089995b99e281ae20025600ea0c5345848eaf09705ca9d7d7ca2eb`.
Later orchestration/documentation commits do not imply a rebuilt engine.

The single preregistered unchanged-engine reliability replay passed: 759/1248 selected cases (60.8173%) and 22,826/26,245 assertions (86.9728%). All 22,826 required historical PASS identities were retained with no harness regressions. Compared with starting 1705, this adds 45 passing cases and 189 assertion passes. Both final unchanged namespace repetitions passed 51/51 with harness OK, real exit 0 and cleanup. Native 218/218 and retained 1318/1318 also passed. The first complete 1808 run remains HOLD: 758/1248 with 51/51 namespace assertions PASS but harness TIMEOUT; its cause remains unknown and no source fix is claimed. This is qualified development evidence, not production certification, whole-WPT parity or the unachieved 95/100 overall goal. On 26,004 exact common reference identities: 22,037 both PASS, 2,444 Chromium-only PASS, 586 Lumen-only PASS and 937 neither PASS. Independent recount SHA d60094edb3bde0aa72ba9c50fe05714a31c0df8dac8873e6fd0176b58bdc0f4c; namespace review SHA ff06f877ed01b0bffddb2ae85c480c97b7f2d9757308e2e95673e41f6382e12a.

The denominator is 1248 selected HTTP/default-timeout testharness URLs/cases from 1247
source files, across ten DOM/CSS families. Selection predates the results; 44 long-timeout
cases and one HTTPS case were already excluded. This is not full upstream WPT,
reftest/manual/visual coverage, Chromium parity or an overall 95/100 quality claim.
The pinned reference is Chromium 150.0.7871.114 with matching ChromeDriver; its result is
1062/1248 selected cases and 24541/26064 assertions. Compare the 26004 common exact
(URL,name,occurrence) identities separately, not mismatched assertion denominators.

All 1247 selected source files were independently verified equal to pinned upstream
revision `0e1c0bce3defd59a76cf2f351c7da5c5a42b26fb`. The only tracked WPT changes are
pre-existing conditional vendor automation and native completion-reporting adapters in
`resources/testdriver-vendor.js` and `resources/testharnessreport.js`; their exact bytes,
all 173 framework pins, selection and manifest were shared by the paired reference and
native runs. No selected oracle, expectation, skip or timeout was changed this session.


The selected family counts are: CSS Values 282; CSSOM 265; CSS Syntax 48; CSS Variables 63;
CSS Sizing parsing 22; CSS Backgrounds parsing 74; CSS Transitions parsing 18;
CSS Animations parsing 41; DOM Nodes 394; DOM Ranges 41. The authoritative inputs are
`agent/validation/linux-wpt-20261101-session13/expanded-selection.json`,
`expanded-urls.json` and `expanded-manifest.json`. Official WPT manifest version9 SHA-256:
`440d5dc80c78ab184d977a7d58929732ce480522550a11c5664f17c76b5317ca`.
Selected manifest SHA-256: `a97bc7c43083140f02c5f7ae3a46de905c8da6506c6a834928dbaa99d3ae63a6`.
The reference resource bytes were shared, but native and WebDriver automation backends differ.

### Preserved failure history and build identities

The original complete 1808 run remains HOLD: 758/1248 cases and 22826/26245 assertions,
all 22824 earlier PASS identities retained, but the namespace case reported harnessTIMEOUT
with all 51 assertions PASS and clean exit 0. Unchanged old/current diagnostic runs passed
under unequal host load. No new lifecycle defect or source fix was established. Exactly
one preregistered reliability replay was authorized, requiring all 22826 first-run passes;
its result is separate above. Do not erase that initial failure or claim a proven cause.

Historical 1807's 1064-case partial replay had 15 semantic losses and 40 namespace assertion
timeouts observed before its deliberate stop. A later copied build CLI accidentally
reused 1807 and overwrote its original binary/build-state after cleanup. The replacement
is quarantined; original raw/guard/monitor/oracle evidence and historical digests remain,
but the original executable is unavailable. Never relabel a reconstruction as original.

Commit `1c1ac7cfc326a960aa91e250184883db9ee5604b` adds the independently reviewed
non-overwrite guard to `agent/validation/build_candidate.py`. Every future build must use
`--expected-head <full frozen commit>` and a never-used positive candidate ID. Existing
output directories, binaries, reservations and tombstones are rejected; failed attempts
also consume an ID. Do not add a reuse/overwrite bypass. The guard changed no compiled
input. Use the existing quota wrapper and one-second monitors; reserve shared build/source
locks and coordinate heavy jobs. Preserve all original runtime guards and old receipts.

### Runtime and publication

Linux display :0 was restarted successfully at 2026-11-03T12:48:21.656938+00:00 with the exact candidate 1808 binary. Actual keyboard input, navigation/history and four own-page semantic checks passed; clean and attributed 36-second 1280x960 recordings were preserved. Android was built from exact fcf, passed all 33 original APK/RTC checks, installed with install-r, and finally restarted at 2026-11-03T12:56:33.126372+00:00. Installed APK SHA-256 is 900e38488462b364076db09c574c21405e26e5b5a474b850537b953675cf4a02; package UID 10196 was retained and no app data was cleared. Six stable foreground/liveness samples and four actual page checks passed after restart. Android smoke coverage is separate from Linux WPT. Both verification-controller corrections and failed attempts remain recorded; neither changed engine/APK bytes. The exact-source delivery window was closed and the guarded task branch restored, with all 742 inputs/binary/build-state unchanged. Final runtime receipt hashes: Linux 846747c9ac1dd03d7f8d58fc930cb29909d5f868d7df1fd3c3a556c9b2e14cdb, Android ce2c885faf24dc7e5bf07c90c3fd318321a58860974bf192f8d6abb8f81481cd.

The exact tested source and final development-device metadata are published at public commit c57da5b98ecb52124d04d10964ce62c6793c5e47 on gpt-6-astra-pro-builtin-agents-rubywebterm-linux-source1808-20261103 (https://github.com/ecooxai/lumen/tree/gpt-6-astra-pro-builtin-agents-rubywebterm-linux-source1808-20261103). The source archive at https://a0bb38e846444140.5x7aqstuceku.opentunnel.xyz/src.tar.gz is 10,344,997 bytes, SHA-256 e0ded35eb3968778f2a14af49d21dbcdae66437028cd25d3a316feeb4eed0e57; offline Git restore and external HTTPS200/full-byte readback were verified at 2026-11-03T13:03:35.288168+00:00. The approved public export excludes canonical private Git objects, profiles, credentials, message queues and validation machinery; recovery is not an independent browser rebuild. All four user artifacts—the final five-page evidence PDF, clean 36-second 4:3 recording, attributed development showcase and exact installed APK—were durably saved by 13:08:18UTC. The private canonical artifact manifest agent/ops/session18/final-user-artifacts.json (SHA c8795e715225af3f9b67656f304a73ef289b5809b09d11d89fa6aab9dc8e26db) records their actual saved IDs, paths and hashes. Final PDF SHA-256 is a780cceeedbb63782e3538ec98560c4d8ace51f9fe2f819bbda44f92884a4d39, independently accepted for factual accuracy and all-page visual layout. The first full HOLD and unknown timeout cause remain explicit; no production, whole-WPT or 95 claim is made.

Public exports contain exact tested source inputs with approved public-only ancestry.
Never push the canonical private Git history or publish profiles, message queues,
credentials or private validation machinery. Archive recovery is not an independent
browser rebuild; external dependencies remain external. Resource `_mb` fields are decimal
MB, dashboard GB is decimal, and CPU 100% is one core (shared quota 480%). Earlier prose
that called these values MiB was a unit-label error; raw measurements were not rewritten.

### Remaining work

1. Investigate namespace/frame startup and parent-load completion reliability under controlled resource conditions. The first full run timed out after all 51 assertions passed; the cause remains unknown. Preserve the failure and every successful observation, with no timeout relaxation or test-specific bypass.
2. Improve stylesheet and CSSOM lifecycle behavior that affects real pages: import loading, constructable sheets, source reflection, media invalidation, and atomic frame/post-connection operations. The final reliability shared-reference comparison records 449 CSSOM and 106 DOM-node assertion gaps; use the preserved final replay ranking to select a bounded next cohort.
3. Address typed `attr()` values, cycles, `calc-size()` and context-sensitive numeric evaluation with explicit value provenance. CSS Values accounts for 1,230 of the final 2,444 shared Chromium-only passes; the large attr-all-types and calc-size-parsing files contribute 116 and 68 respectively. These counts are assertion gaps, not supported-feature percentages.
4. Extend animation/background shorthand and selector grammar using unchanged complete valid/invalid cohorts. The measured final gaps include 230 animation-parsing and 192 background-parsing assertions; preserve the already accepted alias, priority, numeric and keyword behavior.
5. Add separately declared rendering, network, visual/reftest and real-site coverage. The current HTTP DOM/CSS testharness sample cannot establish those broader platform properties.

Keep 23 unsupported tree-context controls visible: only exact literal inline scalar/integer
ownership is implemented. Random syntax/type acceptance has no sampling/cache runtime.
Sizing shorthand grammar/storage does not implement native width/height expansion/layout.
Animation-duration initial 0s metadata and broader animation/timeline behavior remain limited.
Stylesheet imports/sets/external CSSOM reflection and complete native sheet.media invalidation
remain incomplete. Preserve the retained architectural/authorization boundaries below.

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


## Next agent

Read the current agent/continue.md and latest session19 entries in agent/devsummary.md
before acting. Reuse the active task branch while this authorized session continues; do not
mistake the retained historical session18 final seal for the present session state. Verify the
latest exact source/binary/proof mapping, pending freeze and test owner before editing or
building. The source archive, live desktop executable and Android package have separate
identities and must each be verified when delivered. Preserve the selected-manifest scope,
all historical timeouts/HOLDs and bounded architecture/authorization rules documented above.
