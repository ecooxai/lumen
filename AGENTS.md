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


## Current development handoff — session18, frozen1808 regression repair

Read current `agent/continue.md` and `agent/devsummary.md`; the source/freeze receipt is
`agent/source-work/session18/candidate1808-freeze.json`. Session began2026-11-03T06:58:59Z,
nominal deadline11:58:59Z; one additional hour is authorized only for important bugs and
required verification. No optional feature work remains. Root is CTO, repo_lead owns
canonical Git/source/docs, wpt_baseline owns tests, ops_recovery owns final delivery.

### Exact source and hold boundary

Engine commit d93c9769c750dacbfd4bf415aa0523fbc0de7d6f follows separately reviewed auto-duration e38e360265230131b418698fb3e0969e4de7eebe
and lexical sizing d93c9769c750dacbfd4bf415aa0523fbc0de7d6f. The following docs-only HEAD
is frozen for1808 build; do not infer executable identity from later documentation HEAD.
The prior exact1807 e238a82a0e60cc41c7d08d95de03e3dc3d0afe6b passed focused/native218/retained45-1318
but its full replay found15 priorPASS losses:6 auto-duration/timeline and9 size admission.
Its owned cohort was stopped11:09:16.881Z with1064 completed rawfiles to save time for
fresh repaired qualification. Keep partial raw data, true stop reason, guard cleanup and
all completed PASS identities. A partial replay is not a complete1248 score.

The duration fix recognizes valid auto components, resolving them to0s only for a single
computed auto timeline; other timelines preserveauto. Existing numeric/unsupported-random
fallback and canonical aliases remain. The sizing fix admits only the standard size/min-size/
max-size family and uses real lexical components plus existing scalar grammar for ordinary
inline/type1/type8 declarations. CSS-wide singleton, max-size none/auto distinction, comment/
escape handling, duplicate canonicalization, priority and empty removal are covered. Page/
font-face/other descriptor contexts remain separate. Native shorthand expansion/layout,
initial-duration metadata and wider animation solver behavior remain explicit limitations.

### Required gates and meaning of scores

First gate the unchanged12 time-list files, both size valid/invalid files, independentduration12
and sizing14 controls, then all previous focused/native218/retained1318 gates. The new full
replay must retain the historical22786 PASS union plus every genuine completed1807 PASS.
Do not alter WPT sources, expectations, skips, framework pins, guard adapters or old receipts.
Last accepted full baseline is1705:714/1248files (57.21/100),22637/26245assertions. Held1802,
1804,1805 were724/751/753 files with5/2/1 priorPASS losses respectively. Pinned Chromium150
is1062/1248files,24541/26064 assertions; compare common exact identities separately. This
selected sample is not whole upstream WPT, Chromium parity or an overall95/100 quality claim.

1807 independent focused review verified287 raw/hash/cleanup checks: XML31/native59/upstream22,
UA16,timing106 and narrowtree13/36 with23 unsupported failures retained. All218 native tests
and retained45fixtures/1318 assertions passed;334 retained independent checks passed. Native
269.21s slowdown versus~171s is spread across200/218 tests with hostCPUmedian40.27→82.33%,
sharedcgroupmean66.01→67.81% and stable~275.4MBpeakRSS. Existing evidence supports host
contention, not an isolated tree hot path; no duplicate benchmark or speculative change.

### Final delivery and remaining limits

Keep exact accepted source HEAD through the Android build/check/install boundary. After a
fresh complete no-loss full replay, ops builds/installs without clearing phone data, restarts
owned Linux Lumen on display:0 and connected Android, checks actual runtime identity, and
prepares actual-browser showcase/report. Heavy APK work follows full replay. Review every
public export against immutable tested inputs and approved public-only Git ancestry; never
publish private canonicalGit objects, test/profile/message data. Verify remote branch, archive
hash, offline restore and external byte-for-byte download before promotion. Prior public1804
remains a clearly held development checkpoint until a qualified successor replaces it.

No random sampling/cache computation was implemented; no fabricated samples may enter runtime.
Tree context is literal inline scalar/integer ownership only; stylesheet/custom/inherited/
typed-length/pseudo/shadow contexts retain explicit gaps. @import loading, complete stylesheet
sets/external CSSOM source reflection and native sheet.media invalidation remain missing.
See the ranked report-input next-work list; defer new features until after final delivery.

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

Start with the current continuation and immutable final candidate receipt. Preserve every
prior PASS identity and all recorded limitations; verify the active source/device/public
checkpoint before continuing. Keep source/Git ownership coordinated and runtime guards intact.
