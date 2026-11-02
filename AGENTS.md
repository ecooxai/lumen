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


## Current development handoff — session17, qualified candidate1703

Canonical checkout: `/mnt/p3/project/browser/lumen-android/lumen-android`.
Branch: `gpt-6-astra-pro_builtin-agents_rubywebterm_session17-20261102`.
User request is preserved verbatim in `agent/task.md`. Session began **2026-11-02T11:45:32Z**;
normal deadline **16:45:32Z**, optional extra hour only for an important unresolved bug.
Root is CTO; equally capable built-in agents own implementation, testing, review and delivery.

**Latest incrementally qualified engine is1703**, source `70d99dc0d8e9646f0a256bc3cf68aa47235b92c9`,
immutable app `build/codex-native/lumen-astra-i1703`, SHA-256 `38f8185c73bf17b3ec625bc30945216a19950a2bf7d9f597afb53519f11122ba`,
source-manifest digest `0ed85c68a9944a34e4ab8351a8c536348b2c5d21c97b31a06b4b63e512f41825`.
`agent/validation/linux-wpt-20261102-session17/candidate1703-handoff/handoff.json`
binds all raw receipts and explicitly separates historical and newly measured scopes.

| Scope | Before | Candidate1703 | Pinned Chromium150 |
|---|---:|---:|---:|
| Exact eight-file keyframe subset of fixed1248, complete files | 0/8 | 5/8 | 6/8 |
| Same eight files, assertion identities | 20/55 | 44/55 | 49/55 |
| Original authored selector/string34 | 7/34 | 32/34 | 34/34 |
| Separate EOF/numeric13 | 7/13 | 13/13 | 13/13 |
| Versioned real-runtime2 | 2/2 | 2/2 | 2/2 |

The fixed8 replay has **24 existing assertion gains, five whole-file gains, zero prior-PASS losses,
zero missing/new identities, and no harness regression**. Original34 remains32/34: named timeline-range
normalization and escaped range identifiers are deliberately unsupported and their failures remain visible.
This is47/49 total authored rows, with no oracle removal. Ordinary selector identity preserves full parsed
precision separately from display rounding; append/find/delete and EOF handling share validated selectors.
The v1 immediate-getAnimations activation gap is pre-existing and remains unfixed; v2 runtime controls
wait two frames and do not assert that unresolved named-range effect metadata is absent.

Build/startup passed in46.165s; full core212.461s includes native218/218, new47/49 and retained
CSSPropertyRule21/21 plus exact descriptor13/13. The original retained45 fixtures pass1318/1318,
98.797s; independent raw-row/input/lifecycle audit563/563, SHA-256
`0fbe421365ba324014f50f36daf53feb4494a41a67ba5aa35cf52adc77172329`.

**The latest complete fixed1248 measurement remains candidate1603**,703/1248 files (56.33013%)
and22604/26245 assertions; Chromium150 is1062/1248 files and24541/26064 assertions.
Different assertion denominators must stay separate. This is a selected frozen sample, not full official WPT.
Keep objective score56.33/100 tied to that last full-sample measurement until the integrated replay.
1701's properties41 measured18/41 and589/813, +68 shared assertions with no losses, has **zero URL overlap**
with fixed1248.1703 reran its owned21/13 regressions, not the entire41-file cohort.

Next: integration_validation owns1704 shared easing token grammar, original timing5 and versioned42 controls;
engine review v2 accepts static scope. Published2024/pinnedChromium150 linear semantics are explicit;
later editor-draft single-stop/omission-preserving serialization remains future work. Preserve specified
step-start/step-end aliases generally, compute canonical step functions, and retain the original two
transition-valid prior-PASS rows even where Chromium fails. Original30 had27/30 Chromium passes;
three assumption corrections are separately versioned, and all original fixtures/results are preserved.
The proposed animation-valid sixth file is absent from fixed1248; its staging preflight failed before writes/tests.
Keep the verified original timing5. Candidate1702 was retired unused. Delivery audits bounded navigation
MIME metadata; javascript-navigation behavior is separate. A MediaList draft remains local behind timing/MIME.
First integrated unchanged1248 replay is planned14:30–14:45; do not postpone it for new drafts.

Only integration_validation approves `/tmp/lumen-session17-relay/approved-queue.jsonl` request rows.
Root's dispatcher executes exact authorized connector arguments and saves immutable responses.
Child direct Ruby calls hung initially; do not repeat that transport. Parse every generated worker layer
locally before queueing. Preserve setup-failure logs separately from engine/test outcomes.
One heavy task at a time; inherit canonical build/source-edit locks, use quota wrapper and one-second
process-tree/cgroup monitoring, build jobs2. Preserve protected runners, WPT oracles, user work and unrelated jobs.
No tracked source/Git changes during qualification; no arbitrary cleanup. Build shared quota480% of one core.

Public1701 source backup is verified at `https://github.com/ecooxai/lumen/tree/gpt-6-astra-pro-builtin-agents-rubywebterm-linux-source1701-20261102`,
public commit2ec6af183597791985b48c6a9c2689c4a8ef3cf2. Curated834-file export preserves734 build inputs
and approved218-commit ancestry, excludes private working history, and restores offline with exact blob IDs/modes.
Public archive is10,260,510 bytes, SHA-25655ab04c72136c8e35a6d4878d743eb0595c91dec27c1db06390be8fd22275bf0.
Delivery owns live telemetry/private messages, tunnel/Surge, persistence and final desktop/Android restart.
Live desktop/Android remain1603 until final delivery. Keep final qualified engine HEAD unchanged through
Android build/check/install and display`:0` restart; only then commit final docs/status. Preserve device data.
Reserve at least final60min for final replay/repair, delivery and verified persistence. This session is ongoing.

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
