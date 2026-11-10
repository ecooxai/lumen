# Session21 continuation and accepted handoff

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

## Additional exact evidence and supported operation

- Native closure: `agent/ops/session21/engine/candidate2105-native-lane-close-v1.json`, SHA256 `5c1ac5a7ac634594b1f25007151cba220f48cc4b5c6364c2f39810c2382729f1`; all581 union-observed owned PIDs gone,748 inputs and113 native executable hashes verified.
- Required declaration: `agent/ops/session21/wpt-qa/candidate2105-required-acceptance-v1.json`, SHA256 `ea80b702b4fd44778344efa95795d27da9df226632560575b4b9e294df2e47aa`; exact-source addendum SHA256 `d382cfaa59e0c1b8222221963400519059f9a792977cb377fcef17a8fd9366b6`.
- Fast seal: `candidate2105-fast-independent-v1.json`, SHA256 `b4ec410629bbde48ddd477024f34d0d3b42901df81002e983deb3ed464c4e889`. All14 inherited scopes are separately accepted, historical seal SHA256 `f21350f551fcfbedc9b5127b98d6947b067fcbdca51c19320de33aa5bf234d70`.
- The original full collection completed once at16:07:10.165552UTC. A later read-only audit metadata-key correction is additive; it did not rerun browsers, change guards or relabel raw failures. Core acceptance is16:12:39.288744UTC; final required window evidence is16:24:59.641836UTC.
- Full cleanup covers2,721 observed owned PIDs and1,248 profiles; final window cleanup is separately verified. Process samples can miss short-lived children, and summed RSS can double-count shared pages; preserve the existing resource qualification.

The supported run_monitored.py and tools/codex_compile.py interfaces remain unchanged, with one-second process/cgroup samples and the shared480%CPU/max6-job build limit. Native expected-count222 is a documented configuration retaining all218 old identities plus four new tests, not a weakened count-only check. Old native URL tests remain solely as regression preservation.

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

## Documentation identity

The final ordinary documentation commit changes only the reviewed eight paths, preserving all748 actual build inputs, normal/immutable2105 bytes and ICU. Its own commit/tree is recorded after the commit in the separate update/equivalence receipt; the original build and acceptance records continue to name20a7. No new build or browser test is needed merely to rename a documentation HEAD.
