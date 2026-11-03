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


## Current development handoff — session18, frozen1806 qualification

Canonical checkout: `/mnt/p3/project/browser/lumen-android/lumen-android`.
Branch: `gpt-6-astra-pro_builtin-agents_rubywebterm_session18-20261103`.
Full request: `agent/task.md`. Session06:58:59–11:58:59UTC on2026-11-03, with at most
one extra hour for an important unresolved bug. Root is CTO; same-model built-in agents
own source/Git, independent tests and operations. Repo lead alone owns canonical Git.
Coordinate ownership and freezes, use the shared480%quota and one-second monitors, and
preserve all historical work, exact WPT/framework pins and original expectations.

### Candidate1805 scope and immutable full replay

Scoped source `0415871ac9ed76e69b85acf8430765c97bcc539f`, binarySHA
`fcf8016299063b119d48df212e4d262cd4f382aae29ee051e5a64568241360d4`, input manifest
`7faeddce9d30fde7681ba2e2acff5ca348c31825f3c41438a9f53161f16d9eaa`.
Acceptance: `agent/validation/linux-wpt-20261103-session18/candidate1805-incremental-acceptance.json`,
SHA `7055dc1aa3d055b488b48dbf40f0ff68cdbeecfdb9e78ed5c6766c57221fad8f`.

- Whole random serialization retains27/71 prior passes; typed random36/36 passes.
- Timing102/106 satisfies the predeclared102-row scope; four original live sibling rows
  remain failing and visible. TimingWPT gains48 existing assertions to170/204,3/5 files.
- Integer188/188, native218/218 and retained45fixtures/1318assertions pass; retained
  independent audit334/334. Focused MediaList/background/keyframe families lose no prior passes.
- Original timing42 remains28/42 diagnostic. Current-spec range/serialization differences
  from Chromium150 remain explicit. linear() and WebAnimations solver are unchanged.
- Random syntax/type validation never supplies a value to runtime; sampling/cache computation
  remains unimplemented. No nominal validation result may reach computed/rendered output.

The first random wrapper exit2 was an empty-shard cleanup return-value audit issue. Preserve
its original receipt and independent reconciliation483877f22ac2ec86a6255841e9729f9fc336e88bf0a784022b741b058dbe52bd;
actual PID/profile cleanup and guards passed, and no browser rerun erased the original evidence.

Immutable1805 full1248 completed09:44:07.415–10:04:56.649UTC:753/1248files,22785/26245assertions,
149 gains and one alias prior-PASS loss versus1705. All26,245 identities remain unchanged;
all seven earlier losses recovered. HOLD remains. Next full replay requires22,786 priorPASSs.
Engineb848e386c94b58a639a5a0071aaffdaac57c41a6 is frozen for1806 with a following docs-only HEAD;
tester owns build/focused/native/retained/full gates. No further feature work after this freeze.

### Source integration and limits

Frozen engineb848e38 includes XML39e511c, case/Unicode87b2438, canonical alias3e53a9f and
owned inline tree-countb848e38 as separate reviewed commits. XML uses sparse branded PI state/private header, preserving ordinary
Node layout. Staged parser59 passes; integrated parser59/browser31 and charset21 gates remain.
Ignoring doctype PI is the selected allowed implementation policy; keep Chromium's differing
behavior distinct from the normative post-root/invalid-label cases. No @import, stylesheet-set
selection or full external CSSOM source reflection is added.

Inline tree-count v4 is applied after independent semantic/context review and actual64-bit ABI
verification:ComputedStyle1720B,Node304B,MDecl24B,all205 old field offsets unchanged. Context
requires exact original literal inline property/value ownership and no pending substitution;
stylesheet/copied/custom/inherited/pseudo/shadow contexts stay unsupported. All36 tree controls
remain fixed, with13 predeclared literal/ordinary rows required; existing timing106 now requires106.
Native child-list invalidation must pass mutation gates; do not broaden into cascade/provenance work.
The14 legacy animation/transition aliases now share property identity across native/CSSOM paths,
with descriptor isolation and parsed priority preservation. Runtime validation remains pending.

Earlier qualified gains remain: MediaList10/10 and47/47; charset20/21; background8/9 and48/50;
keyframe declarations31/31; opacity10/10; image44+10+4; canonical charset44+1. MediaList native
stylesheet applicability, complete media math, two background computed-math cases and keyframe
named timeline ranges remain limited. Successful narrow gates do not establish full features.

### Baseline, publication and final delivery

Last regression-free fixed1248 baseline remains1705:714/1248 files(57.21154%),22637/26245
assertions. Chromium150:1062/1248 files,24541/26064 assertions; denominators remain separate.
Selected-file score57.21/100 is not full official WPT or overall browser quality.1804 measured
751/1248 and22735/26245 but lost2 prior passes; required union22737. The2 random losses are
recovered in focused1805, awaiting full replay. All5 earlier1802 numeric losses are recovered.
Preserve all historical HOLD/raw/audit receipts, including failed1803 and timing1704 evidence.

Current download is held1804 development source: testedc55bd950 maps to public4064675364488ee6583c3cc50dd8a907ab1c3997
and metadata-only2a8c99e6f80455618ffcf9cfd71afe64e2c57feb, pushed/verified on the source1804
public branch.739 exact compiled inputs,839 curated files and approved public ancestry are
independently checked; private working Git/test/profile material is excluded. Offline Git restore
and public HTTPS readback match10,322,694bytes, SHA68affbba63ea6f0c395de2fc8c3bf9af2c894e1e61c06c62011e60909ef6d3b6.
This proves source identity/recovery, not an independent rebuild. Public preview/WS is live;
Surge lacks authentication and FileHarbor returned403. See ops/session18/handoff.md and
agent/continue.md for exact operational commands and private-state precautions.

Reserve the final45–60minutes for complete replay, final Android build/install-r and verified
display0 restart, isolated actual-browser showcase, publication and final notes. Keep engine
source frozen through runtime promotion; preserve user profiles/data and verify task-owned PIDs.

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
