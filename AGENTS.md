# Agent notes for Lumen

Start with `agent/continue.md` and `agent/devsummary.md`. The original user request is in `agent/task.md`. Historical root `devsummary.md`, `agents/archive/` and the archived prior AGENTS file remain evidence; their old candidate standings are not current-branch qualification.

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


## Current development handoff — session18, broad regression HOLD on1802

Canonical checkout: `/mnt/p3/project/browser/lumen-android/lumen-android`.
Current branch: `gpt-6-astra-pro_builtin-agents_rubywebterm_session18-20261103`.
The complete current user request is preserved verbatim in `agent/task.md`.
Session start **2026-11-03T06:58:59Z**; nominal five-hour deadline **11:58:59Z**,
with up to one additional hour only for an important unresolved bug.

Root is CTO. Same-model built-in agents own implementation/Git, independent WPT/build
qualification, and operations/delivery. Direct RubyWebTerm calls work; prior relay
queues are historical. Repo lead owns canonical Git operations. Coordinate source
ownership and freeze before every build. Preserve shared locks, quota 480%, jobs 2,
one-second CPU/RSS/cgroup monitoring, all untracked historical work and unchanged WPT
oracles/harnesses. Reserve the final hour for integrated verification and delivery.

### Candidate1802 focused gates passed; broad acceptance HOLD

**Current blocker:** the completed1802 full1248 replay measured724/1248
whole files and22676/26245 assertions, but lost5 priorPASS
assertions. Four signedzero cases in CSS math and one25-digit z-index case regressed.
Do not promote1802 as fully accepted. Last regression-free full baseline remains1705.
Repair all5 losses while preserving the union of1705 and1802 prior passes before1803.
The downloadable1802 archive is retained only as a labeled development checkpoint.

Integrated candidate **1802**, source `c60398214ad22f5d7578ec8f1f5239e2bab53010`,
binary SHA-256 `e54c56023927627937ea6bcd2e9f4e1bb835709e8d7eb90c6d8522e27577b4db`,
source manifest `711bc0fd75334746677edd4655198d4ce0dbb24f2abc59e8c39ece5f220a5909`.
Acceptance is `agent/validation/linux-wpt-20261103-session18/candidate1802-incremental-acceptance.json`.

- MediaList frozen WPT10: **10/10 files, 47/47 assertions**, up from 2/10 and 23/47;
  eight new fully passing files and 24 previously failing assertions now pass.
- Adjacent CSSOM13: **9/13 files, 76/113 assertions**, two assertion gains. Numeric3
  retains **1/3 files, 132/199 assertions**, including the two candidate1801 gains.
  These focused comparisons have no prior-pass loss or assertion/harness identity change.
- Original authored MediaList controls **15/15**, new owner controls **8/8**.
  Chromium150 is 13/15 and 7/8 because its readonly indexed Reflect return values
  diverge from the normative WebIDL algorithm; preserve that documented difference.
- **218/218 configured native tests** and unchanged **45 retained fixtures / 1318
  assertions** pass. Independent retained audit passed 334/334 checks.
- Keyframe contracts retain **47/49** with both named-range failures visible;
  CSSPropertyRule21/21 and descriptor comparison13/13 pass. Numeric28 remains24/28:
  four specified-value invalid-unit z-index failures remain despite computing to auto.

Candidate1801 introduced shared typed z-index integer conversion; candidate1802 adds
component-value MediaList parsing, canonical mutations, readonly indexed API semantics,
and live null-namespace owner media attributes. Media query semantic nesting beyond128,
full math simplification and native stylesheet.media rendering invalidation remain
explicit limitations. No complete MediaQueries implementation is claimed.

### Sample score and ongoing work

The last completed frozen1248 replay is still **candidate1705: 714/1248 files
(57.21154%), 22637/26245 assertions**. Chromium150 on that sample is1062/1248 and
24541/26064 assertions; denominators are engine-specific. Independent baseline receipt:
`agent/validation/linux-wpt-20261103-session18/verified-baseline1705.json`.
Score **57.21/100** describes only this selected-file pass rate. Candidate1802 full replay has completed with the5-loss HOLD described above.
Its larger raw pass count does not satisfy the no-regression acceptance gate.
Preserve that binary and all pinned WPT/harness/manifest files while drafting new source.
The live checkout may advance; distinguish source-worktree state from immutable build provenance.

Next candidate1803 source includes separately committed z-index specified-value validation
(2f31207 plus cssText8801d9d and CSSRule setter8b8ddc8 route fixes) and keyframe-only declaration context
(2d9e7e1 plus setter-priority correctiona4e38c1). Both are independently reviewed;
build and focused qualification are pending. Parsed keyframe text discards!important,
while CSSOM setters retain recognized important priority; empty removal follows CSSOM
before priority validation despite the documented Chromium reference difference. Background-clip and
border-image repeat/slice validation/serialization are approved next investigations.
Failed timing1704 remains separately preserved at527e190832b0ee6ff6a10def4bc42fe9b7d5cc22;
do not revive its wholesale draft or alter tests to manufacture passes.

### Persistence and final promotion

Qualified1802 source was independently checked against all739 compiled inputs and839
curated inventory files, then pushed as public commit `9f9dab43fa707adbb69ebc07f30d8107a5f4d9db` on branch
`gpt-6-astra-pro-builtin-agents-rubywebterm-linux-source1802-20261103`.
Public ancestry contains only the approved public history; private working Git objects
were excluded. This checkpoint records focused WPT26, native218 and retained45/1318
qualification; the later full1248 replay found5 prior-PASS losses and the checkpoint
is held as a development artifact, not a fully accepted release.

The1802 archive passed offline Git restore and public HTTPS byte/hash readback:
10,308,895 bytes, SHA-256 `a30b5dd525f623d50440fa16fa4f569177d8849cb4548e77337f73fa8c47b8c4`.
Exact receipt: `agent/ops/session18/artifacts1802/promotion-receipt.json`.
Offline restore verifies Git recovery and source identity, not an independent rebuild.
Desktop display:0 and connected Android promotion remain pending the final candidate.
Keep final engine source frozen through Android build/install and desktop restart;
use adb install-r, preserve profiles/data and verify task-owned PIDs before stopping them.

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

Read `agent/continue.md`, then the current iteration records in `agent/devsummary.md`. Preserve the exact tested source/binary identities and raw failures; continue from the latest qualified candidate, not an unqualified build or older public artifact.
