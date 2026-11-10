# Lumen Linux project instructions

## Accepted session21 engine and provenance

Candidate **2105 is accepted for the selected work and all required preservation gates**. The tested engine commit is `20a7dd4b54f1a4e587168f10875e43f307a76fb8`, tree `2b07ae05ef2adf0b91398d98adf3a082915ddfcd`. Immutable executable `build/codex-native/lumen-astra-i2105` and the normal `build/codex-native/lumen` output both have SHA256 `2d38085828206f384aeafc3afc38c63009fda483c55f0fcd057e0df9255ffcb3`. The 748 actual build inputs are recorded in `agent/validation/iteration-2105/source-manifest-before.json`, digest `944c86116893691f5d186a716f2132aa85fe7347225791cdb633eefde8a06798`.

The independent core seal is `agent/ops/session21/wpt-qa/candidate2105-full-independent-v1.json`, SHA256 `a6cccfbf608ca25d3f9cb3cd776519ff3ca8fe7a78fc0daa2e2f7c9da7f29186`. The final native-window seal is `candidate2105-window-independent-v1.json` in the same directory, SHA256 `442e781d175d99450344cfeff73c33985d8fb15b165c6a750760b7c3d47fc553`, completed at **2026-11-10T16:24:59.641836UTC**. Root separately viewed all five raw/credited frames; visual receipt SHA256 is `ff7391aeed729f5b77f8601e3323c1161464f1c3e5d1a65a0a1b91033f0b3dc1`.

The final window independently verified all 748 inputs, source/default/immutable/ICU identity, 90 pixel checks, 1,127 changed pixels inside the radius fixture and zero outside, timing, and removal of all five owned PIDs, three groups and the fresh profile. Its frozen88.89 badge is an explicitly historical baseline; current sampled results are reported below. This is actual native Y-only automatic repaint evidence, separate from all-WPT or GPU qualification.

Accepted2104 remains intact at source `3edec1c75b9e332f16e2e5b0943eb881c3c00e1f`, immutable SHA256 `0e7f3d63259731afedafe2fd717394b49e4fe525d34d9c12b1c437dd5e6789d9`. Candidate2105 changes only01-events.js from that accepted source. A later documentation commit and any public export commit are distinct identities; never replace20a7 in the actual executable's build record. Final documentation equivalence is recorded separately under `agent/ops/session21/engine/` and independently reviewed by QA.

## Measured compatibility and preservation floors

The **1,248-entry frozen WPT sample** contains URL/test entries from **1,247 unique source files**. Candidate2105 completes **820 entries** and passes **23,365/26,245 registered assertions (89.02648%)**. The remaining assertion outcomes are2,852FAIL,7TIMEOUT and21NOTRUN; harness outcomes remain1,238OK,1ERROR and9TIMEOUT. These units are distinct. Original receipt field names such as files/complete_files are retained in raw evidence; they do not turn URL variants into unique source files. The whole-WPT/Chromium-parity goal and a global98/100 quality claim remain unmet.

Accepted2007's reliability replay established813 complete entries and23,329 passing assertions. Accepted2104 reached819 and23,364. The final gain is **seven complete entries and36 named assertion passes**, with no prior required pass lost and the exact26,245 registrations preserved. The first35 gains are radius-related: computed13, invalid-value rejection11, valid serialization3, prefixed5 and three assertions across two css-values radius files. The final one is MutationObserver callback exception-global reporting. No upstream background-position gain is inferred.

The **selected unprefixed parsing/computed group (3 files/48 assertions)** improved21/48 and0/3 complete to48/48 and3/3 complete, matching the pinned Chromium48/48. Its100/100 assertion score names only this selected group. The prefixed companion is separate: Lumen25/25, pinned Chromium24/25. Seventeen rendering comparisons, authored94, extra geometry9, math8, fixed-hit5 and adjacent background animation770 are distinct preserved gates. The three known background shorthand observations and mixed-arity radius animation failure remain visible.

The original fresh Chromium150 comparison is1,060/1,248 complete entries and24,517/25,969 passing assertions, retaining three startup HARNESS_ERROR entries. Its separately qualified diagnostic recovered95 registrations (24PASS/71FAIL); it does not replace the original run. Matrix `agent/ops/session21/wpt-qa/candidate2105-common-identity-matrices-v2.json`, SHA256 `972e08037555490592a5edcf38e14f2059f5cac24b5d37701dbfef2af295a30f`, gives25,909 original common identities:22,497 bothPASS,830 bothnonPASS,622 Lumen-onlyPASS and1,960 Chromium-onlyPASS. Different registration denominators and the separate diagnostic remain explicit.

Candidate2105 native tests pass222/222, including all218 original names and all222 accepted2104 names. Internal checks are ownership133, rendering/hit157, background dimensions30 and actual-main cache/signatures9. Supplemental sanitizer targets pass133 and157 checks. They instrument style.c plus its ownership test, and block.c plus the actual rendering/hit test, respectively; other engine objects and V8 are ordinary. This is not full-browser sanitizer coverage.

Authored callback observations improve8/13 to12/13 under the prospective routing-only contract. The unchanged foreign-constructor/no-callback observation remains a normative failure. **All16 selected upstream assertion files/163 assertions** improve157/163 to158/163; four parser/XML notification failures and the outerHTML combined-record failure remain. The separate `MutationObserver-nested-crash.html` crash guard was **not executed in this session**; no complete crash-suite coverage is implied.

The new main preservation registry is `agent/ops/session21/wpt-qa/candidate2105-required-prior-pass-v1.json`, SHA256 `a7e87b06bfa30497799ba48c3a65f8d6d35bd341877d60ca18a43baf7dbef295`:23,365 named passes,820 complete entries and1,238 harnessOK. Separate messaging registry `messaging2105-required-prior-pass-v1.json` in the same directory has SHA256 `5505a4bd84c0431d1b003f27c28187c2841837a1a4f6a76fecb64fa973196a3f`:34/39 assertions,15 complete of18 entries and18 harnessOK. Both preserve every accepted2104 requirement and its earlier chain. Named identities, not aggregate counts alone, remain mandatory. URL conformance campaigns are deferred; old native URL identities only preserve existing regressions.

## Implementation and explicit boundaries

Physical radius uses strict specified grammar, atomic four-corner expansion, canonical CSSOM serialization and ordinary WebKit aliases. Native X storage stays inline; optional distinct Y owns112 bytes. Actual sizes are ComputedStyle1736B (was1720), DItem224B (was208), Clip68B (was52), and Node304B unchanged. Copy/adoption/free, explicit inheritance, resets, pseudostyles, allocation failure and detached anonymous boxes have dedicated checks. One-value percentages still resolve horizontal radius against width and vertical radius against height.

Shared geometry scales the original nonnegative present radii before zero-axis squaring. Fragment masks precede overlap scaling. Insets subtract the corresponding border widths without a second normalization. Real paint, nested rounded clips and hit tests use both axes. Deferred clipping avoids duplicating an already active prefix. Hit traversal preserves resolved containing blocks, fixed/absolute escapes, layer order, affine and scroll coordinates, and the padding rectangle even for zero-radius scrollers. Actual application cache/signature hashes include Y; separately owned actual-window evidence tests dynamic repaint scheduling.

The secondary two-line background numeric-type correction is applied. Preserve all40 authored observations:37 required position identities and3 disclosed inherited shorthand limitations. The bounded image-mask repair uses one tiled display item for intrinsically sized default/repeat masks, including1×1 masks, within the existing background-colour mask approximation. Explicit no-repeat/other modes and fitted contain/cover keep their prior paths. It is not full CSS Masking conformance.

The2105 change only adds a private frozen-realm report hook and routes MutationObserver callback exceptions through the callback object's existing native creation-realm hook. It does not change observer registration, parser mutation notifications, outerHTML, native realm architecture or public APIs. Same-realm, foreign-realm, bound/proxy and reporting observations stay separately recorded.

No broad cascade/layer/origin rollback, general pending-substitution provenance, logical-radius family, complete shadow/outline/border-style algorithm, arbitrary radius endpoint resolver or mixed-arity animation repair was implemented. Existing off-viewport hit API policy is unchanged. The conservative escaped-descendant scan adds a real O(n) cost; do not restore the disproved incomplete paint-cache prune merely to make a benchmark faster.

Deferred proposals remain under `agent/ops/session21/engine-proposals/`: outerHTML replacement needs correct custom-element reaction and transient-observer sequencing; Attr replacement has a separate standards/reference discrepancy; getElementsByClassName quirks matching needs a lossless DOM-token route, because CSS.escape and C-string primitives cannot represent every token. No deferred proposal is implementation authorization.


## Measured hit-cost tradeoff

The same frozen5000-node diagnostic ran20 warmups and300 queries per case. Each cell below is p50 / p95 in microseconds. These are retained single-run observations, not a browser-speed benchmark.

| Case | Held2102 | Accepted2104 | Accepted2105 |
|---|---:|---:|---:|
| outside_clipped_no_fixed | 0.158 / 0.166 | 293.956 / 689.316 | 396.422 / 1449.901 |
| inside_same_tree | 535.161 / 814.839 | 571.592 / 1113.764 | 689.493 / 1963.158 |
| outside_inline_fixed | 0.199 / 0.231 | 134.223 / 242.154 | 211.897 / 793.991 |

The old no-fixed outside result was correct and very fast; the conservative escaped-descendant search has a real O(n) cost. The old escaped-inline-fixed case was incorrect0/300, whereas2104 and2105 hit300/300. Inside results remain correct.2105's slower single-run values are retained without a rerun or causal claim; its hit production source is unchanged from2104. Exact reports/raw CSVs remain under `agent/ops/session21/engine/hit-perf2102-v1`, `hit-perf2104-v1` and `hit-perf2105-v1`.


## Retained failed candidates and provenance

Candidate2101 remains HOLD: native220/222, cold ownership119/122 and rendering52/55. Its sanitizer target returned1 for failed assertions, despite no memory diagnostic, and is not a pass. Two independently identified pre-existing process-lifetime style caches were later warmed through real entrypoints, followed by strict zero-growth checks without reset or tolerance. Candidate2102 remains HOLD at221/222 with fixed-hit escape failures. Candidate2103 remains HOLD and incomplete:222 selected,181 observed,179PASS,2 old affine failures and41 unexecuted. Neither positive sanitizer results nor targeted family100/100 supersede those failures.

Accepted2007's original full run permanently remains HOLD at812 complete entries/1237 harnessOK; its separate unchanged reliability replay established813 complete entries/1238 harnessOK with the same23329 assertion passes. Preserve both. The original fresh Chromium1248 run and its three-startup diagnostic replay also remain separate; no recovered rows are silently substituted into the original reference.

The2105 source commit succeeded before a post-commit audit incorrectly used Path.exists on eight pre-existing dangling Chromium lock symlinks. Reconciliation used lstat/lexists without following targets, verified all14760 pre-existing entries remained, and did not replay patch or commit. Original terminal40799 and additive reconciliation remain under `engine-proposals/callback2105-v1/`. This was an audit predicate failure, not a semantic test pass or an observed deletion.


## Public delivery, build/run instructions and desktop ownership

The accepted2105 public source commit is `f2ea72ff37b6e52b9eef1e08bf902979f6fbdccf`, tree `acb04c8f4ff3de8e92350356a076a75efa85a74b`, on branch `gpt-6-astra-pro_builtin-agents_rubywebterm_linux-source2105-20261110`. The verified offline archive is at `/mnt/p3/project/browser/lumen-android/lumen-android/build/codex-session21-tools/public-checkpoint2105/lumen_linux_gpt-6-astra-pro_builtin-agents_rubywebterm_candidate2105_src.tar.gz`, SHA256 `978143cd6ba5fb39b0eb65cc2bd8e0f5af7bb51c97c240d1364985d41078aef9`; current report is `https://raw.githubusercontent.com/ecooxai/lumen/f2ea72ff37b6e52b9eef1e08bf902979f6fbdccf/docs/lumen_linux_gpt-6-astra-pro_builtin-agents_rubywebterm_session21_qualification_20261110.md`. Public verification receipt: `agent/ops/session21/delivery/source-push2105-v1/receipt.json`, SHA256 `cc1e49fc6ac10d2aba5177f5c4e3e04391ef79a93d7121b24976ce356aac111f`. These publication identities are separate from tested20a7.

The public build/run guide is `https://raw.githubusercontent.com/ecooxai/lumen/f2ea72ff37b6e52b9eef1e08bf902979f6fbdccf/docs/lumen_linux_gpt-6-astra-pro_builtin-agents_rubywebterm_session21_build_and_run_20261110.md`, independently read back in full with the exact accepted guide SHA256. The source/archive checkpoint was verified at2026-11-10T16:35:50.279970+00:00. Offline archive closure `agent/ops/session21/delivery/source-bundle2105-lane-close-v1.json`, SHA256 `302cf6b2339e80f06d3a1fa9fb42b31c4434f7a32181c5fff9b12ef39e0c37a1`, proves restoration of all852 exact public paths/modes from the11,748,544-byte archive.

Pages is an additional delivery channel whose live manifest records its deployment identity and served artifact hashes. The durable source/report/guide links above are already verified. This handoff does not assert a new2105 Pages deployment before its separate live verification.
 Historical accepted2104 public commit `54999c2cfe6b159167b89c1b5b98427e9eaf5370` and earlier releases remain intact.

Canonical Git history and OPS evidence remain private. Public export uses only the reviewed ancestry rooted at `e7569b9a15d4f323c5edb66ee7a79f9f0e85473a`, the815 tested source blobs and explicitly curated/history metadata, including the separately reviewed current build guide as the852nd public path. Do not export private canonical history, profiles, tokens, account observations or unrelated untracked files. Source archives are not portable binary/dependency bundles and were not independently rebuilt.

The full user request remains `agent/task.md`, SHA256 `dcd46c5d2cf2b5e0b0a4ca516055a65b8bec421b19dab71c9b363010534970dc`. The canonical branch is `gpt-6-astra-pro_builtin-engine-owner_rubywebterm_session21-wpt-20261110`. An unchanged checkpoint preceded the branch/task capture; held candidates and unrelated untracked artifacts are preserved. The three predecessor handoffs are archived byte-for-byte under `agent/archive/20261110-session21-gpt-6-astra-pro-builtin-agents-rubywebterm/`.

Current prerequisites and a concrete fresh-directory build recipe are in `docs/lumen_linux_gpt-6-astra-pro_builtin-agents_rubywebterm_session21_build_and_run_20261110.md`. Historical dependency documents remain historical, even though their listed package versions still match. The accepted guide SHA256 is `fab7355e455628e6f31d4bb69756c2c25cc99a7d9fd4ade061aa8dcef87146e5`; a new rebuild remains unqualified until separately validated.

The recorded display0 PID resolves to an out-of-project sibling release and remains untouched. It does not authorize a restart, signal, launcher repoint or profile inspection. A later manual launch uses immutable2105, `about:blank`, a fresh `build/lumen-2105-profile.XXXXXXXX` directory, the supported XDG variables and `LUMEN_NO_SESSION=1`. No HOME or DISPLAY override or unsupported --profile flag is used. The profile mechanism is source/library-derived; deleted capture profiles did not retain a child-path inventory. The manual launch example was not executed by this task.

Pinned ICU is `build/codex-deps/v8/bottle/v8/15.6.75.8/libexec/icudtl.dat`,10,822,000 bytes, SHA256 `319501bd0fdf9c9cb9926c0d0fd64ed95961118af700fbb4342981f088e73e8c`.

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

## Next task starting context

Session21's accepted engine is2105/source20a7/binary2d38. Read this file and `agent/continue.md`, verify the actual source-input/binary equivalence receipt after the documentation commit, then preserve the exact new main/messaging/native named-pass registries before any new work. The selected radius parsing/computed group is48/48; the1,248-entry sample remains89.02648% of registered assertions and is not the entire WPT corpus. All16 selected MO assertion files remain158/163 and the separate nested crash guard is unexecuted.

The next project must not silently apply deferred proposals: outerHTML needs correct custom-element/transient-observer sequencing; parser/XML mutation notifications need actual parser integration; Attr replacement remains a separate standards/reference discrepancy requiring correct same-slot, name/prefix, ownership and invalidation behavior; class-token work needs length-aware semantics; arbitrary radius animation endpoints need an isolated numeric context. Existing mixed-arity and broader shadow/outline/logical/pending limitations remain. The class-name selector shortcut was explicitly deferred because NUL/U+FFFD cannot be preserved through its current primitives.

Use the existing quota/monitor interfaces, one heavy lane and exact source provenance; do not mutate protected guard internals or revive the refused broad cascade rewrite. Later documentation/public commits remain distinct from tested20a7. Preserve original HOLD/raw runs and additive audit corrections. No unrelated desktop process, private profile or stale account workflow becomes part of a new task merely because it appears in historical notes.
