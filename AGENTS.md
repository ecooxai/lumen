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


## Current development handoff — session18, focused candidate1801

Canonical checkout: `/mnt/p3/project/browser/lumen-android/lumen-android`.
Current branch: `gpt-6-astra-pro_builtin-agents_rubywebterm_session18-20261103`.
Current user request is preserved verbatim in `agent/task.md`; the prior task and
continuation are archived with the `20261103-session18-before` suffix.
Session start **2026-11-03T06:58:59Z**, nominal five-hour deadline **11:58:59Z**,
with up to one additional hour only for an important unresolved bug.

Root is CTO. Same-model built-in agents own repository/implementation, independent
WPT/build qualification, and operations/delivery. Direct RubyWebTerm calls are
working in this session; the previous session's relay queue is historical.
Repo lead owns source and Git branch/index operations. WPT agent owns heavy build/test
windows; operations owns progress service and deployment. Coordinate exact source
freeze boundaries before builds. Keep the shared build/source-edit locks, quota480%,
jobs2, and one-second process-tree/cgroup monitoring. Preserve all untracked prior work.

Starting source is candidate1705, commit `13c22d3d28a68746fede3f265f55c5eda3247da6`,
binary SHA-256 `a3db6b7179821e7f231060b2a5880f582b3d470b94845932b9274896d1d00271`.
Incremental qualification is recorded in
`agent/validation/linux-wpt-20261102-session17/candidate1705-incremental-acceptance.json`.
It retained MIME27/27, native218/218, original keyframes32/34 with both named-range
failures visible, and retained1318/1318 assertions across45 files.

The newer saved complete selected1248 replay, found after the prior handoff, is
`candidate1705-expanded-first-comparison.json` in the same directory. It records
**714/1248 complete files (57.21154%) and22637/26245 assertions**, versus prior1603
703/1248 and22604/26245. Pinned Chromium150 is1062/1248 and24541/26064; the assertion
denominators remain engine-specific. Saved comparison records33 assertion gains,
11 whole-file gains and no prior-PASS losses. Independent raw recount and compiled-source hashes are verified in `agent/validation/linux-wpt-20261103-session18/verified-baseline1705.json`.
This is the frozen selected sample, not full official WPT or a browser-quality score.

Failed timing1704 remains on its separate preserved branch at527e190832b0ee6ff6a10def4bc42fe9b7d5cc22;
it is not merged. Do not revive its wholesale draft. Select a bounded standards
fix using real current failures and preserve every test/oracle. The prior MediaList
draft is a candidate for independent review, not qualified source.

Operations will verify current live desktop/Android state and persistence before
promotion. Keep final qualified engine source frozen through Android build/install
and desktop:0 restart; preserve profiles and Android data. The prior public1701
backup is historical until a newer exact source checkpoint is verified.
Reserve at least final60min for integrated replay/repair, delivery and persistence.


### Session18 iteration1: focused candidate1801

Source1801 is `4e74e964347ed5608a3110a1e2b31b270e610ac9`, containing the isolated
z-index engine commit `8f9cdd82567a0609b18088f687c068299d19c919` plus status documentation.
Immutable binary SHA-256 `21e04192acf8602705c0aeb9e27ce673977a75a41c0b1c02618a79a1c65c7839`;
source manifest `6646326b25ee6dd2422bce96f1e3a92991165d291561fee8f3f3590088d392de`.
Accepted scope is the exact numeric3 WPT cohort and independent authored28 controls:
130→132/199 assertions and0→1/3 complete files; two existing assertion gains, no
prior-PASS losses, missing/new identities, or harness regressions. Authored28 improves
13→24/28. Four original declaration-validation failures remain visible: unit-valued
z-index declarations are accepted by the pre-existing specified-value path, although
all now compute to auto. This patch replaces z-index's ad-hoc numeric conversion with
the existing typed integer path, including CSS half-ties, NaN and range clamping.

Build/startup62.435s; numeric3 replay8.290s; authored28 gate3.855s. Build peak treeRSS
318.72MiB; cgroup CPUmean122.10%,max185.53%,62 one-second samples. Full native218 and
retained45 will run once on integrated1802; their old1705 results are not new1801 evidence.
Full1248 score remains the verified1705 **57.21/100** selected-file rate until a new replay.
No overall95/98 quality claim. Desktop/Android promotion and new external persistence
remain pending; operations owns them.

Next implementation is MediaList1802, staged outside source in
`agent/source-work/session18/medialist1802-v2/`, under independent review. It repairs
component-value list parsing, canonical single-query mutation, live indexed API state,
and null-namespace owner media attribute initialization/synchronization. Its semantic
nesting guard, unsimplified media math, and pre-existing native stylesheet.media rendering
invalidation are explicit limits; no full MediaQueries implementation claim is made.

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
