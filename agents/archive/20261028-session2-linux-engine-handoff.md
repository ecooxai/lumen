## Linux engine handoff — handoff recorded 2026-10-28T13:46:14.663530+00:00

This section supersedes the previous 127/128 handoff state. Historical source, reports and rejected experiments remain in Git/private validation artifacts; do not treat their outcomes as measurements of the final binary.

Chromium-level full web-platform compatibility is not achieved. The inherited 19/100 readiness estimate is subjective and has no numerical weighting formula; scoped 96/97 scores describe bounded engineering changes, not overall parity. Report measured URL and reported-subtest counts with exact selection denominators. Testharness subtests are not individual assert_* calls; keep native CTest entries and authored correctness checks separate.

### Exact source checkpoint

- Final branch: gpt-6-astra-builtin-agent-lumen-linux-conformance-20261028-0920. The final metadata/package commit is identified in the package inclusion manifest and publication record; it may follow the tested engine commit without changing compiled sources.
- Commit matching compiled candidate 150: 0f62172b08683c7c0d89722c588af6ff94f82ca3.
- Native binary: build/codex-native/lumen-astra-i150.
- Binary SHA-256: 79874c9c16af2086cb1de93a6194276d4b17512897aeca118244beddef7bb967.
- Exact compiled source/resource files: 584, independently checked against Git.
- Build source-content digest: c31fd602a7c4a99ea976c7a9bccab8f8e79f26a90ab6382b4fa4d3fd57a8cfdd.
- Exact private source snapshot: agent/progress/runtime/private-prepared/source-snapshots/iteration-150.
- WPT checkout: /mnt/p3/project/browser/wpt, revision 0e1c0bce3defd59a76cf2f351c7da5c5a42b26fb.
- Pinned Chromium: /home/a/.local/archpkg/pacman/usr/bin/chromium, version 150.0.7871.114.

The final disjoint 4,200-variant native selection has 1,620 fully passing URLs and 199,144/215,657 PASS reported testharness subtests, versus 1,567 and 197,240/215,081 at baseline 128. There are 56 full-URL gains and 3 losses, net +53. Existing subtest identities have 1,460 gains and all 30 PASS losses retained; 474 newly registered PASS outcomes produce net +1,904. New identities total 579 (474 PASS, 105 FAIL); three old nonpasses are absent. The selected set is 10.66% of 39,394 pinned testharness variants, with six explicitly jsshell entries. This is not full WPT or Chromium parity.

Primary native: 783/2,000 full; 101,588/109,272 PASS; harnesses 1,765 OK, 149 TIMEOUT and 86 ERROR. Expanded native: 760/2,000 full and 95,789/103,015 PASS. Supplemental native: 77/200 full and 1,767/3,370 PASS. Fresh primary Chromium completed at 13:44:03 UTC with 1,695/2,000 full and 105,688/110,572 PASS (1,956 OK, 25 TIMEOUT, 8 ERROR, 9 CRASH, 2 SKIP); all 2,000 records were retained and owned processes/profiles drained. Fresh expanded Chromium has 1,648 full and 100,508/103,406 PASS; retained historical supplemental Chromium has 189 full and 3,337/3,370 PASS. The resulting mixed-date Chromium union is 3,532 full and 209,533/217,348 PASS.

All 30 native PASS losses remain in the totals: 21 unsupported tentative Grid 3 intrinsic-repeat cases, 5 flex-width failures reproduced in every fresh 128/142/150 run, 1 animation outcome whose broad loss did not reproduce in isolated runs, 2 inherited data-URL decoding failures with variability, and 1 unresolved referrer loss. Separate harness completion regressions are one primary animation and two expanded hidden-media timeouts. Inherited or non-reproduced attribution never erases the observed losses. Font causation is unproven.

Rendering: final 38 PASS, 60 FAIL, 1 UNSUPPORTED and 1 TIMEOUT across 100 painted reftests; baseline 37/61/1/1, retained Chromium 95/5. All 198 guarded capture launches drained. The 451.10-second scheduling duration is not a speed benchmark.

Performance: 21 balanced serial fresh-profile trials, seven per engine, all passing 9,395 custom checks. Median elapsed milliseconds for baseline 128 / final 150 / Chromium: DOM 44.74 / 45.19 / 11.60; absolute layout 359.38 / 385.06 / 67.50; Grid 136.76 / 190.03 / 56.90. Final native elapsed medians increased about 1.0%, 7.1% and 39.0%; do not claim a speed improvement. Median trial sampled-maximum PSS is 123.422 / 123.801 / 376.682 decimal MB, with one-second whole-browser-process-tree sampling. These are neither instantaneous peaks nor phase-specific memory. RSS can double-count shared mappings. All 21 cleanups drained and removed profiles. The resolved-track getter allocates a line-name array and reparses declarations twice per call; the workload performs 512 reads over 256 states. That is a profiling candidate, not an established explanation for the entire 39% slowdown. Cache transfer reuses computed arrays; metadata allocates per new Grid box.

Focused gates include 132/132 CTest, JSON modules 225/225 with 8/8 full URLs, module cohort 308/320 across 44 URLs with 38 full and +5/0 versus 144, identity controls 25/25 and graph controls 42/42, cache controls 47/47, Typed OM 479/542 with +8/0, and parser 1,087/1,172 with 42 unsupported tentative Grid 3 losses retained. The rejected 146 placement proposal and rejected 147 module behavior are not shipped gains.

Whole-goal estimate remains 19/100 against requested 98/100; the scoped patch correctness estimate is 96/100 and is not the whole-goal score. No new engine tests or builds were started during final publication. Public output is the distinct allowlisted aggregate artifact lumen-linux-gpt-6-astra-pro-builtin-agents-rubywebterm-chromium-public-report.json. The denied private working report is not published.

A fully passing URL requires harness OK, at least one registered subtest, and every reported subtest PASS. Zero-subtest OK records do not count. Preserve all selected URLs, including SKIP, CRASH, ERROR, TIMEOUT and PRECONDITION_FAILED outcomes; distinguish manifest URLs selected from browser tests actually executed. The two 2,000-URL cohorts and the rendering corpus use separate evidence and must not be presented as the entire Web Platform Test suite. Definitions and inventory audits are retained under agent/validation/linux-wpt-20261028-session2/reporting-final/.



The 132-entry native CTest gate and focused module/CSS/TypedOM/retained-Grid gates pass. Final module controls are 308/320 reported subtests across 44 unchanged URLs, 38 fully passing URLs, all harnesses OK; Chromium 320/320. Original 25 identity and new 42 cyclic/order/fetch-priority controls all pass. Used-track controls 47/47. Parser cohort 1,087/1,172 across 37 OK harnesses retains all 42 unsupported tentative Grid3 losses. Typed OM 479/542 across 9 OK harnesses restores the primary grid-area regression.

The unaccepted 146 auto-placement experiment is precisely removed; its 64 focused gains are not shipped. All 21 broader layout losses are restored by rollback; final 29-URL geometry and four-URL regression sets match pre-placement 145 exactly. Preserve its source, 54 experimental controls and raw results before a future logical-axis/baseline design.

### Reproducible and bounded tests

Retain the shared tools/codex_compile.py CPU budget and one-second exact process-tree monitoring described above. Full CTest TMPDIR must use canonical physical project storage; a symlink-spelled TMPDIR exposes an inherited Android plan-test assertion. Short physical agent/t is task-owned browser/benchmark scratch, ignored by Git. Do not delete unrelated temporary files.

Native WPT/reftest/guarded authored captures require the sealed pre-main capture guard and private profile gate. Do not weaken, bypass or silently replace it. Library: agent/validation/linux-wpt-20261028/capture-refusal/capture_guard.so, SHA-256 45643caa22bb659bc7b431acfc7b8af04d68f971c3610f4d56dcb2847d808900. Protected native_capture_guard.py SHA-256: d2099b548c524ae039533afe6c8f44fe4e1a4abe11c586bc08af98e3f91edc1d.

The source archive includes the tracked guard C sources and `agent/validation/build_capture_guard.py`, but intentionally excludes private generated guard binaries and raw results. On a clean checkout, build the guard into a **new private directory** using `python3 tools/codex_compile.py python3 agent/validation/build_capture_guard.py --output /absolute/private/new-capture-guard`. Verify the resulting artifact against the pinned guard hash before using this session's protocol; investigate any toolchain mismatch rather than weakening the guard. Never overwrite the sealed current artifact. External V8/SDL/toolchain dependencies and the separately pinned WPT/Chromium installations remain required.

Use agent/validation/run_guarded_cohort.py or run_guarded_fixture.py for new evidence. They drain actual cleanup threads and require real exit/profile cleanup; never synthesize missing audit events. Preserve older missing-exit-marker evidence separately from verified absent PIDs. Use compare_wpt_runs_v2.py to separate manifest selection coverage from full-URL pass rate. Preserve reported-subtest identities, missing registrations, NOTRUN, timeout and harness outcomes. Never alter an active cohort's source, controller, order, deadlines or restart policy.

Original batch controller 8790 and its relay are exclusive to the owning cohort. Diagnostic transport must use an explicitly fixed private controller and distinct route; environment defaults are insufficient. The compromised 136 primary run remains diagnostic only. The clean 142 run and later final runs retain observational request provenance.

### Build and fresh-run commands

Run from /mnt/p3/project/browser/lumen-android/lumen-android. Choose a new iteration ID and fresh output directory; never overwrite retained evidence. Do not rebuild mutable CTest executables while another gate is using them.

```sh
python3 tools/codex_compile.py python3 agent/validation/build_candidate.py --iteration NEW_ID --jobs 2 --startup
```

For a monitored native gate, create a new ignored run directory, launch CTest under the shared budget, and attach the exact-tree monitor immediately:

```sh
lumen_run_dir=agent/validation/UNIQUE_NATIVE_RUN
mkdir "$lumen_run_dir"
TMPDIR=/mnt/p3/project/browser/lumen-android/lumen-android/agent/progress/runtime-temp python3 tools/codex_compile.py ctest --test-dir build/codex-native --output-on-failure -j2 >"$lumen_run_dir/ctest.log" 2>&1 &
lumen_test_pid=$!
python3 agent/progress/monitor.py --root-pid "$lumen_test_pid" --output "$lumen_run_dir/resources.csv" --label 'owned native CTest tree' >"$lumen_run_dir/monitor.log" 2>&1 &
lumen_monitor_pid=$!
if wait "$lumen_test_pid"; then
  lumen_test_result=0
else
  lumen_test_result=$?
fi
wait "$lumen_monitor_pid"
test "$lumen_test_result" -eq 0
```

For browser smoke/regression runs, first unset LUMEN_WPT_CTRL/LUMEN_WPT_PRE, set LUMEN_CAPTURE_GUARD_LIBRARY and LUMEN_CAPTURE_GUARD_SHA256 to the sealed values above, and set LUMEN_CAPTURE_GUARD_AUDIT_DIR to a fresh absolute directory. Then:

```sh
unset LUMEN_WPT_CTRL LUMEN_WPT_PRE
export LUMEN_CAPTURE_GUARD_LIBRARY=/mnt/p3/project/browser/lumen-android/lumen-android/agent/validation/linux-wpt-20261028/capture-refusal/capture_guard.so
export LUMEN_CAPTURE_GUARD_SHA256=45643caa22bb659bc7b431acfc7b8af04d68f971c3610f4d56dcb2847d808900
export LUMEN_CAPTURE_GUARD_AUDIT_DIR=/mnt/p3/project/browser/lumen-android/lumen-android/agent/validation/UNIQUE_BROWSER_AUDIT
python3 tools/codex_compile.py python3 agent/validation/run_guarded_fixture.py --binary /mnt/p3/project/browser/lumen-android/lumen-android/build/codex-native/lumen-astra-i150 --fixture /mnt/p3/project/browser/lumen-android/lumen-android/tests/browser/module-graph-identity-regression.html --marker browser-module-graph-identity --output /mnt/p3/project/browser/lumen-android/lumen-android/agent/validation/UNIQUE_BROWSER_RUN
```

The fixture adapter supplies isolated HTTP/profile setup and one-second monitoring. For WPT, use run_guarded_cohort.py with --runner paired_wpt, a fresh --audit-report, and --kind lumen --binary ABS_BINARY --urls-file ABS_URLS --output ABS_OUTPUT after the separator. Keep the pinned original suite settings for comparable benchmark replays.

### Important remaining work

Module origin policy, error source locations and CSS-module serialization/import filtering/selector handling remain incomplete. CSS pending-substitution provenance, unsupported tentative Grid3 grammar, bounded numeric-unit/clamp coverage, omitted-content and hidden-host pseudo queries remain explicit limits. Grid column auto-flow requires a proper logical-axis and baseline-sharing design before reintroducing the reverted experiment.

Storage is still organized per document rather than a complete durable origin/session model; do not claim isolation/persistence completeness. Native queued input is bound to stable top-level tab/document generations and uses delivered virtual modifiers separately from physical SDL state. The inherited cross-origin frame completion fallback remains outside that repair.

The historical primary 142 TrustedTypes PASS-to-NOTRUN outcome is restored in final primary 150, without a dedicated repair claim. Retain it as evidence of unresolved batch/history-dependent execution variability: isolated 128/134/142 runs all produced the same 1 PASS/32 FAIL outcome with the target subtest passing. This does not justify changing security policy. The earlier Typed OM regression is also restored, with its independently validated property-aware repair recorded separately.

The three observed expanded-corpus PASS-to-FAIL outcomes remain counted. In three matched fresh-profile repetitions per engine, invalid-padding data navigation failed on both frozen 128 and 150 in 3/3 runs; base64url failed on 128 in 2/3 and 150 in 3/3. The network decoder bytes match the initial preservation commit and already accept URL-safe digits and ignore invalid padding. The upstream test uses a 500 ms absence-of-message window. This demonstrates inherited failures and baseline variability, without proving the absence of every runtime regression. All four referrer subtests passed in each isolated run on both binaries; that broad loss remains unresolved as a batch/history/timing observation. Evidence: agent/validation/linux-wpt-20261028-session2/expanded-loss-isolation/classification.json. Do not replace or filter the broad results using these controls.

A concrete next network task is standards-conforming data-URL base64 validation: implement the applicable forgiving-base64 algorithm rather than permissively accepting arbitrary characters. Preserve accepted whitespace and unpadded inputs while rejecting malformed padding and the URL-safe alphabet where the data-URL algorithm requires rejection. Use unchanged upstream fetch/navigation controls and independent valid-input holdouts. This is inherited incomplete behavior, not a repair claimed by candidate 150.

The final primary run has a new Web Animations effect-stack PASS-to-NOTRUN observation (harness OK to TIMEOUT). Matched fresh 128/142/150 controls all produce identical five reported subtests, 3 PASS and 2 FAIL, harness OK, with the target persisted-animation subtest PASS. Keep the broad loss counted as unreproduced in isolation and unresolved as a batch/history/timing issue. Evidence: agent/validation/linux-wpt-20261028-session2/primary-animation-loss-isolation/classification.json. No source or expectation was changed.

Five final-primary flex text-width PASS-to-FAIL observations remain counted. Nine fresh controls (three each on frozen 128, 142 and 150) all finish with harness OK, 13/18 reported subtests PASS, and the same five 39 px widths against 50/70 px expectations. The direct flex implementation, font implementation/header and shared FontFace logic are byte-identical from initial preservation to the tested engine commit. This supports inherited fresh-case failures, while broad baseline successes versus fresh failures remain an unresolved execution-history difference. Shared CSS/event paths did change, so indirect effects are not excluded. Ahem is declared, but used-font/readiness causality is unproven; do not weaken the test or claim a font-race fix. Evidence: agent/validation/linux-wpt-20261028/session2/final-flex-width-attribution/fresh-comparison.json and lead-source-history-review.json.

### Prioritized next-work queue

1. **Measured performance cost:** the quiet seven-trial medians worsened from baseline 128 to candidate 150: Grid 136.76 to 190.03 ms (+39.0%), absolute style/layout 359.38 to 385.06 ms (+7.1%), and DOM 44.74 to 45.19 ms (+1.0%). Investigate this before broader feature expansion. The Grid cache transfers existing size arrays rather than copying them. It adds one metadata allocation per new Grid box; the resolved-track getter also allocates line-name buffers and reparses declarations in two passes. The workload makes 512 track-property reads across 256 states. These are candidate costs, not a measured attribution of the full slowdown. Compare frozen stages and remove redundant getter work only with named/unnamed/repeat/calculated-track and existing mutation/pseudo/foreign-document controls intact. Evidence: agent/validation/linux-wpt-20261028-session2/performance-final/lead-grid-performance-history-review.json.
2. **Data-URL validation:** address the inherited permissive decoder using the unchanged malformed-padding, alphabet and whitespace cases plus valid-input holdouts. The frozen-baseline repetitions and unchanged source proof are in `expanded-loss-isolation/classification.json`; retain the observed broad losses until a separately validated repair exists.
3. **Batch execution stability:** reproduce the exact predecessor chains for the unresolved referrer and Web Animations observations, and retain the historical TrustedTypes variability even though its final-primary outcome recovered. Fresh-profile success alone does not identify a cause. Use a distinct private controller/route or direct isolated collector, preserving the original order, deadlines and all outcomes.
4. **Grid layout:** investigate logical-axis sizing, baseline sharing, orthogonal layout and intrinsic sizing using the retained failing WPT cases. Preserve accepted 139/143 placement and the used-track cache. The reverted 146 experiment and its 21 broader losses are design evidence; its 64 provisional focused gains are not shipped. Keep the 75 native geometry checks and the unchanged 29-URL/four-URL/supplemental controls.
5. **CSS Text and editing:** prioritize the actual failed reported subtests in the sealed final failure catalog, with matched controls before proposing a repair. Preserve the independently validated input target/generation and modifier behavior; do not infer a specific cause from category totals.
6. **WebNN registration gaps:** first distinguish missing test registration or early harness completion from unsupported API behavior, using exact reference identities and capability preconditions. Missing registrations are a coverage gap, not passing tests.
7. **Remaining module and CSS rules:** use the 12 remaining module-cohort failures and the documented CSS pending-substitution, tentative Grid grammar and pseudo-style limits as bounded follow-ups. Preserve the corrected per-script error identity/root-specific graph selection and the distinction between fetch failures and parse/link/evaluation errors. Avoid broad origin-policy changes without dedicated isolation tests.

### Preservation, publication and next-agent entry

Seven preexisting untracked agent/validation/fixtures files were preserved and left unstaged. Private raw validation reports, profiles, source snapshots and Git bundles stay outside served routes and source archives. Current user authorization explicitly requests source/Git publication; historical review notes are not a permanent ban. Any new automatic-review rejection must be respected, with concrete authorization/content checks or user approval, never bypassed.

The downloadable archive is a **reviewed current-source snapshot**, not a Git history export. The full development branch and history remain local; private raw results, profiles and Git bundles remain separate.

Publication state: Automatic publication review rejected exporting the tracked comparator agent/validation/compare_wpt_runs_v2.py in a new archive: the denied harness payload is not authorized by the general source-snapshot request. The full development branch and history are preserved locally and are not pushed. The separately reviewed cloud showcase publishes an engine-source snapshot with all 584 recorded inputs, the explicit prompt, build configuration and required Linux embed generator; validation/progress/agents tooling, other tools scripts, Git history, raw results and profiles are omitted. No new build was run, and complete standalone validation or Android build reproducibility is not claimed.
Final source-package/inclusion-manifest record: the separately published manifest identifies the final local metadata commit, tested engine commit, exact archive hash/size, all 584 matching recorded inputs and 678 packaged files. Validation helper sources are omitted. The archive is an engine-source snapshot, not the full repository or validation environment.
See devsummary.md and the committed agent/progress/iterations reports for decisions, timing, scores and rejected candidates. Read the exact binary/source/runner manifests before adding results. Continue from the measured remaining gaps; preserve the native engine identity and unchanged Chromium/WPT expectations.


## Local closing handoff — 2026-10-28T14:09:47.029539+00:00

Recorded 297.58 elapsed minutes after the 2026-10-28 09:12:12 UTC session start. This closing commit is local documentation metadata after the distributed snapshot; it changes neither the tested engine nor any published artifact. The whole-goal estimate remains 19/100 against the requested 98/100. Full WPT coverage and Chromium parity were not achieved.

- Tested engine: `0f62172b08683c7c0d89722c588af6ff94f82ca3`. All 584 recorded engine/resource/build inputs remain matched to the accepted binary. No new engine tests or builds were run during publication.
- Distributed engine-source snapshot metadata: `602a5f2f0b40ddc809e47b872eb1ec18758b558c`. The reviewed archive contains 678 files and all 584 recorded inputs. SHA-256: `ae97e4667153cc0d51f7d6ad1bfefc0d4d516d6ec3359cfa8d16f7691cd06362` (4,450,698 bytes). It retains the exact prompt and required Linux embed generator; validation/runtime tooling, other tools scripts and Git history are omitted. Complete standalone validation and Android build reproducibility are not claimed.
- Verified live cloud showcase: `f9405516cd136f8ad9f7484200c008c21d201665` on `gpt-6-astra-builtin-agents-rubywebterm-showcase-20261028`. Final Pages build completed at 14:01:27 UTC; the final receipt and index were verified with HTTP 200 and exact bytes at 14:01:37 UTC. The eight initial public assets were verified at 13:59:43 UTC.
- Cloud page: https://ecooxai.github.io/lumen/ . Final delivery receipt: https://ecooxai.github.io/lumen/publication-manifest.json . Engine-source download: https://ecooxai.github.io/lumen/src.tar.gz . Exact inclusion manifest: https://ecooxai.github.io/lumen/source-inclusion-manifest.json .
- Live preview: https://a0bb38e846444140.5x7aqstuceku.opentunnel.xyz/ . Its `/src.tar.gz` serves the same reviewed engine-source bytes. The public task-board final reply was appended once at 14:00:17 UTC, preserving previous messages. No future autonomous replies were promised.
- Final public report: 46,540 bytes, SHA-256 `b822ef66d760a7ad148a5e6bf31e337f34bb787de462c2caf99c07b67eb75c31`; 200 substantive checks plus 26 final metadata checks had zero mismatches. Public URL: https://ecooxai.github.io/lumen/lumen-linux-gpt-6-astra-pro-builtin-agents-rubywebterm-chromium-public-report.json .

The final public report and both approved videos were independently uploaded to FileHarbor and downloaded again to verify their exact reviewed bytes:

- Report: https://fileharbor-meak.basicgleam65.chatgpt.site/files/lumen-gpt-6-astra-pro/48009acb-327d-415d-8a7a-01a5313e703e/lumen-linux-gpt-6-astra-pro-builtin-agents-rubywebterm-chromium-public-report.json .
- Clean demo: https://fileharbor-meak.basicgleam65.chatgpt.site/files/lumen-gpt-6-astra-pro/498b3c18-b305-4ad3-a8cc-c1a911aaacba/lumen-linux-gpt-6-astra-pro-builtin-agents-rubywebterm-chromium-demo.mp4 .
- Credited showcase: https://fileharbor-meak.basicgleam65.chatgpt.site/files/lumen-gpt-6-astra-pro/f57ab91b-6bd4-4d34-8091-aa7b0ae9cead/lumen-linux-gpt-6-astra-pro-builtin-agents-rubywebterm-chromium-showcase.mp4 .

**Publication boundary:** automatic review rejected exporting the tracked comparator `agent/validation/compare_wpt_runs_v2.py` in the complete source archive, including an evidence-backed re-review. The full development branch `gpt-6-astra-builtin-agent-lumen-linux-conformance-20261028-0920` and its history remain local. They were not pushed through another route. Only the isolated reviewed showcase branch was pushed; the default branch and local-only backup refs were not published. The seven pre-existing user fixtures remain untouched.

**Next engineering priority:** profile the measured Grid slowdown before adding more work. Final median elapsed time increased 39.0% for Grid, 7.1% for absolute layout and 1.0% for DOM. Investigate repeated declaration parsing and line-name allocation in the resolved-track getter while preserving the accepted correctness behavior. These are candidate costs, not an established explanation for the whole Grid regression. Keep all observed conformance losses and harness-completion regressions in future comparisons.
