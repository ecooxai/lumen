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


## Current development handoff — session 17, 2026-11-02

The new task branch is `gpt-6-astra-pro_builtin-agents_rubywebterm_session17-20261102`.
The starting canonical source is `fe3e72d6d2248077abea748811ff9ca268e2fdae` (candidate 1603),
with accepted development executable SHA-256 `b2591720be38f312a42654fab3d864094796d3d9a3dfc0bf19e19d34bc4f7202`.
Its prior full handoff is `agent/validation/linux-wpt-20261102-session16/candidate1603-final-handoff/README.md` and `handoff.json`.
The candidate 234/Oct27 standings in earlier files are historical. No session 17 engine change has yet been qualified.

| Frozen 1,248-URL sample | Lumen 1603 | Chromium 150 |
|---|---:|---:|
| Fully passing files |703/1,248 (56.33%)|1,062/1,248 (85.10%)|
| Passing/registered assertions |22,604/26,245 (86.13%)|24,541/26,064 (94.16%)|

These counts are recovered prior-session results bound to the same retained source/binary; they are not new session 17 runs.
The sample is not full WPT. Each engine's assertion denominator is distinct. Do not turn focused100% results into whole-browser95% claims.
The pinned WPT revision is `0e1c0bce3defd59a76cf2f351c7da5c5a42b26fb`; URL-list SHA-256
`4fbab34023a7fa57e1c6c85c9ecc17d884f200340c7ae65178d4ffbc4cc56ea2`, manifest SHA-256
`a97bc7c43083140f02c5f7ae3a46de905c8da6506c6a834928dbaa99d3ae63a6`.
Prior native218/218, authored45 fixtures/1,318 assertions, properties41 and matched-resource5 are separate scopes.
All raw historical losses remain visible, including typed attr() lengths, random() and variable-substituted revert-layer.

### Ownership and working protocol

- Root is CTO. Same-model built-in `integration_validation` owns Git, docs, coordinated builds/tests and qualification;
  `engine_improvements` owns CSSPropertyRule/CSSOM and its focused source edits; `delivery_performance` owns runtime resources,
  preview, checkpoint packaging and final Linux/Android deployment. Agree additional file ownership before editing.
- Child Ruby MCP calls hung while root calls worked. Workers author JSON request files and inspect locally saved responses;
  root relays only the exact MCP call. This is connector dispatch, not root coding/testing. Preserve every failed attempt and elapsed infrastructure time.
- The session starts 2026-11-02 11:45 UTC, normal deadline 16:45 UTC, extension to17:45 only for important unresolved bugs.
  Record actual iteration starts/ends, non-overlapping wall time, build/test duration and one-second CPU/RSS evidence.
- Preserve pre-existing untracked outputs. Do not mass-add them, wipe caches, kill unrelated tasks, or recreate old candidate IDs.
- Supported execution uses `agent/validation/build_candidate.py`, `run_guarded_cohort.py`, `run_guarded_fixture.py`, and the
  existing session10 pinned/monitored wrappers. Inspect their public help/README rather than protected implementation.
- Take the canonical `agent/validation/runtime/build.lock` and `source-edit.lock` during exclusive phases. Freeze both source
  and Git metadata for build provenance. Keep one heavy build or broad native/browser campaign at a time.
- Public exports must exclude private working history, the user prompt, private notes/evidence, authentication and profiles.
  Use the established curated public ancestry/source export and verify recovery. Private checkpoints may retain complete Git history.

### Immediate remaining work

1. Measure focused CSSPropertyRule/property-registration failures on the retained1603 build and pinned Chromium, implement a genuine bounded fix, and compare exact assertion identities.
2. Repeat/profile the matched registry workload: prior1603 work was29.53% above parent313 in single runs; no causal speed claim exists.
3. Preserve all prior native/browser regressions, then replay the unchanged1,248-file sample on the meaningful integrated milestone.
4. Publish truthful progress/source checkpoints and verify preview health. Restart only the final qualified binary on display`:0`
   and install/restart the same source on the connected Android device, preserving profiles/data.


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
